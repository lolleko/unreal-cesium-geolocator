import hashlib
import time
import uuid
from qdrant_client.models import Distance, VectorParams
from qdrant_client import QdrantClient
from qdrant_client.http import models

from dataset_types import Sample

def qdrant_create_client(api_key: str):
    return QdrantClient("localhost", port=6333, timeout=60, api_key=api_key, https=False)

def qdrant_create_collection(client: QdrantClient, collection_name: str):
    try:
        client.create_collection(
            collection_name=collection_name,
            vectors_config=VectorParams(size=512, distance=Distance.EUCLID, on_disk=True),
            on_disk_payload=True,
            hnsw_config=models.HnswConfigDiff(on_disk=True, m=32),
            optimizers_config=models.OptimizersConfigDiff(
                max_optimization_threads=0
            )
        )
        # client.create_payload_index(collection_name=collection_name, 
        #                     field_name="LonLat", 
        #                     field_schema="geo")
        # client.create_payload_index(collection_name=collection_name, 
        #                     field_name="IsActive", 
        #                     field_schema="bool")
        # client.create_payload_index(collection_name=collection_name, 
        #                     field_name="Region", 
        #                     field_schema="keyword")
    except Exception as e:
        print(e)

def qdrant_wait_for_collection_to_be_ready(client: QdrantClient, collection_name: str):
        time.sleep(1)

        while True:
            status = None
            try:
                status = client.get_collection(collection_name).status
            except Exception as e:
                print(e)
                time.sleep(1)
                continue

            if status == "green":
                return
            else:
                print(f"Collection status is {status}")
                # sleep for one second
                time.sleep(1)

def build_unique_sample_id(sample: Sample):
    sample_file_path_hash = hashlib.blake2b(sample.AbsoluteImagePath.encode("utf-8"), digest_size=8).hexdigest()
    # this is 99% guaranteed to be non colliding,
    # would require collision in file name hash and lat lon coords and heading angle
    sample_identifier = f"{sample_file_path_hash}.{sample.Lon:.8f}.{sample.Lat:.8f}.{sample.HeadingAngle:.8f}"

    return str(uuid.uuid5(uuid.NAMESPACE_URL, sample_identifier))

def qdrant_upload_sample_batch(client: QdrantClient, samples: list[Sample], collection_name: str, region_tag: str):

    qdrant_points: list[models.PointStruct] = []
    for sample in samples:
        sample_identifier = build_unique_sample_id(sample)
        sample_dict = {}
        sample_dict["LonLat"] = {
            "lon": sample.Lon,
            "lat": sample.Lat
        }
        sample_dict["IsActive"] = True
        sample_dict["ArtifactProbability"] = sample.ArtifactProbability
        sample_dict["StreetName"] = sample.StreetName
        sample_dict["HeadingAngle"] = sample.HeadingAngle
        sample_dict["Altitude"] = sample.Altitude
        sample_dict["Region"] = region_tag
        sample_dict["AbsoluteImagePath"] = sample.AbsoluteImagePath
        qdrant_points.append(models.PointStruct(
            id=sample_identifier,
            vector=sample.Descriptor,
            payload=sample_dict
        ))

    client.upsert(
        collection_name=collection_name,
        points=qdrant_points
    )
