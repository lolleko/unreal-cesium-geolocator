from PIL import Image
import torch.utils.data as data
from dataset_types import SampleDataset, Sample, DatasetInfo
import torchvision.transforms as transforms

def get_base_image_transform(size=[512, 512]):
    return transforms.Compose([
        transforms.Resize(size),
        transforms.ToTensor(),
        transforms.Normalize(mean=[0.485, 0.456, 0.406], std=[0.229, 0.224, 0.225]),
    ])

def get_base_tensor_transform(size=[512, 512]):
    return transforms.Compose([
        transforms.Resize(size),
        transforms.Normalize(mean=[0.485, 0.456, 0.406], std=[0.229, 0.224, 0.225]),
    ])

class SampleImageDataset(data.Dataset):
    def __init__(self, dataset: SampleDataset, filter = None):
        super().__init__()

        if not filter:
            filter = lambda dataset, samples: samples

        self.dataset = dataset

        self.base_transform = get_base_image_transform()

        self.samples_not_in_db: list[Sample] = filter(self, self.dataset.Samples)

    def get_all_samples(self):
        return self.dataset.Samples

    def get_info(self) -> DatasetInfo:
        return self.dataset.Info

    def __getitem__(self, index):
        sample = self.samples_not_in_db[index]
        pil_img = Image.open(sample.AbsoluteImagePath).convert("RGB")
        normalized_img = self.base_transform(pil_img)
        return normalized_img, index

    def get_sample_not_in_db_by_index(self, index) -> Sample:
        return self.samples_not_in_db[index]
    
    def __len__(self):
        return len(self.samples_not_in_db)

