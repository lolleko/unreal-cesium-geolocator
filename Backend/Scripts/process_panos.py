
from itertools import repeat
import itertools
from multiprocessing import Pool
import torch
from PIL import Image
import torchvision.transforms as T

import json
from pathlib import Path
import argparse
from dataset_types import load_dataset, SampleDataset, Sample
from tqdm import tqdm

PANO_WIDTH = 3328
HEADING_INDEX = 9
LATITUDE_INDEX = 5


def process_sample(data):
    sample, out_dir = data
    pano_heading = int(sample.HeadingAngle)
    assert 0 <= pano_heading <= 360
    north_heading_in_degrees = (180-pano_heading) % 360

    heading_offset_in_pixels = int((north_heading_in_degrees / 360) * PANO_WIDTH)

    pano_tensor = T.functional.to_tensor(Image.open(sample.AbsoluteImagePath))

    pano_tensor = torch.cat([pano_tensor, pano_tensor], 2)
    assert pano_tensor.shape == torch.Size([3, 512, PANO_WIDTH*2]), f"Pano {sample.AbsoluteImagePath} has wrong shape: {pano_tensor.shape}"

    new_samples = []

    for crop_heading in range(0, 360, 30):
        
        crop_offset_in_pixels = int(crop_heading / 360 * PANO_WIDTH)
        
        offset = (heading_offset_in_pixels + crop_offset_in_pixels) % PANO_WIDTH
        crop = pano_tensor[:, :, offset : offset+512]
        assert crop.shape == torch.Size([3, 512, 512]), f"Crop {sample.AbsoluteImagePath} has wrong shape: {crop.shape}"

        crop_name = Path(sample.ImagePath).stem + f"_crop_{crop_heading:03d}.jpg"
        
        crop_relative_path = Path("./Images") / crop_name

        crop_image_dir  = Path(out_dir) / "Images"

        crop_absolute_path = crop_image_dir / crop_name

        crop_pil_image = T.functional.to_pil_image(crop)
        crop_pil_image.save(crop_absolute_path)

        new_samples.append(Sample(
            ImagePath=str(crop_relative_path),
            AbsoluteImagePath=str(crop_absolute_path),
            HeadingAngle=crop_heading,
            Lat=sample.Lat,
            Lon=sample.Lon,
            Altitude=sample.Altitude,
            StreetName=sample.StreetName,
            ArtifactProbability=sample.ArtifactProbability,
        ))

    return new_samples

if __name__ == "__main__":

    parser = argparse.ArgumentParser(description="Process some integers.")
    parser.add_argument("--dataset", type=str, help="directories to convert")
    parser.add_argument("--out_dir", type=str, help="directories to convert")

    args = parser.parse_args()

    dataset = load_dataset(args.dataset)

    out_new_dataset = SampleDataset(Info=dataset.Info, Samples=[])

    image_dir  = Path(args.out_dir) / "Images"

    image_dir.mkdir(parents=True, exist_ok=True)

    with Pool() as pool:
        out_new_dataset.Samples = list(itertools.chain.from_iterable(tqdm(pool.imap(process_sample, zip(dataset.Samples, repeat(args.out_dir))), total=len(dataset.Samples))))

    with open(Path(args.out_dir) / "metadata.json", 'w') as f:
        json.dump(out_new_dataset.to_dict(), f, indent=4)