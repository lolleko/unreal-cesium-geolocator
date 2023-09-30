import argparse
from qdrant_client import QdrantClient
from qdrant_client.http import models

import torch

from pytorch_dataset import SampleImageDataset
from qdrant_utils import build_unique_sample_id, qdrant_upload_sample_batch, qdrant_wait_for_collection_to_be_ready, qdrant_create_client, qdrant_create_collection
from dataset_types import Sample, SampleDataset, load_dataset

from tqdm import tqdm
from torch.utils.data import DataLoader

def load_images_into_db(sample_dataset: SampleDataset, model, qdrant_client: QdrantClient, qdrant_collection_name: str, region_tag: str):
    def sample_filter(image_dataset: SampleImageDataset, sample_paths: list[Sample]) -> list[str]:
        active_samples = []
        path_batch_size = 10000
        current_batch_samples: list[Sample] = []
        current_batch_sample_ids: list[str] = []

        for i in tqdm(range(0, len(sample_paths), path_batch_size), ncols=100, desc="filtering out existing images", unit_scale=True):
            for sample in sample_paths[i:i+path_batch_size]:
                sample_id = build_unique_sample_id(sample)
                current_batch_samples.append(sample)
                current_batch_sample_ids.append(sample_id)

            exists_results = qdrant_client.scroll(
                collection_name=qdrant_collection_name,
                scroll_filter=models.Filter(must=[models.HasIdCondition(has_id=current_batch_sample_ids)]),
                with_payload=False,
                limit=len(current_batch_sample_ids))
            
            existing_ids = [doc.id for doc in exists_results[0]]

            for sample_index in range(0, len(current_batch_sample_ids)):
                if not current_batch_sample_ids[sample_index] in existing_ids:
                    active_samples.append(current_batch_samples[sample_index])

            current_batch_samples.clear()
            current_batch_sample_ids.clear()
        return active_samples
    
    image_dataset = SampleImageDataset(sample_dataset, filter=sample_filter)
    batch_size = 8
    upload_batch_size = 2000 # 512 * 4bytes * 2000 ~ 4MB

    # Skip inference if all sampes are already in db
    if len(image_dataset) != 0:
        image_dataloader = DataLoader(dataset=image_dataset, num_workers=8,
                                        batch_size=batch_size, pin_memory=True)
        
        with torch.inference_mode():
            qdrant_sample_batch: list[Sample] = []

            for images, sample_indices in tqdm(image_dataloader, ncols=100, desc="inference of new images"):
                descriptors: torch.FloatTensor = model(images.to("cuda")).cpu()

                samples: list[Sample] = []

                for batch_element_index, sample_index in enumerate(sample_indices):
                    sample = image_dataset.get_sample_not_in_db_by_index(sample_index)
                    samples.append(sample)
                    sample.Descriptor = descriptors[batch_element_index].numpy()

                qdrant_sample_batch.extend(samples)

                if len(qdrant_sample_batch) >= upload_batch_size:
                    qdrant_upload_sample_batch(qdrant_client, qdrant_sample_batch, qdrant_collection_name, region_tag)
                    qdrant_sample_batch.clear()

            if len(qdrant_sample_batch) > 0:
                qdrant_upload_sample_batch(qdrant_client, qdrant_sample_batch, qdrant_collection_name, region_tag)
                qdrant_sample_batch.clear()
    
if __name__ == "__main__":

    parser = argparse.ArgumentParser(description='Load dataset into vetor database')
    parser.add_argument('--dataset', type=str, help='dataset to load into vector db')
    parser.add_argument('--api_key', type=str, help='qdrant api key')
    parser.add_argument('--db_collection_name', type=str, help='qdrant db collection name')
    parser.add_argument('--region_tag', type=str, help='region tag')
    parser.add_argument('--model_weights', type=str, help='model_weight path')

    args = parser.parse_args()
    
    model = torch.hub.load("gmberton/eigenplaces", "get_trained_model", backbone="ResNet50", fc_output_dim=512).to("cuda")
    model.load_state_dict(torch.load(args.model_weights))
    model.eval()

    qdrant_client = qdrant_create_client(args.api_key)
    qdrant_create_collection(qdrant_client, args.db_collection_name)

    qdrant_client.update_collection(
        collection_name=args.db_collection_name,
        optimizer_config=models.OptimizersConfigDiff(
            indexing_threshold=0,
        )
    )
    qdrant_wait_for_collection_to_be_ready(qdrant_client, args.db_collection_name)

    print(f"Loading dataset from {args.dataset} into {args.db_collection_name}")
    load_images_into_db(load_dataset(args.dataset), model, qdrant_client, args.db_collection_name, args.region_tag)

    # qdrant_client.update_collection(
    #     collection_name=args.db_collection_name,
    #     optimizer_config=models.OptimizersConfigDiff(
    #         indexing_threshold=20000
    #     )
    # )
