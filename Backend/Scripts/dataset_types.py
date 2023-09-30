from dataclasses import dataclass, field, fields
import json
from pathlib import Path, PurePath
from typing import Type
from dataclass_wizard import JSONWizard, json_field

@dataclass
class Sample():
    ImagePath: str
    Lon: float
    Lat: float
    Altitude: float = 0.0
    HeadingAngle: float = 0.0
    StreetName: str = ""
    IsActive: bool = json_field("IsActive", dump=False, default=1)
    ArtifactProbability: float = 0.0
    Descriptor: list[float] = json_field("Descriptor", dump=False, default_factory=list)
    AbsoluteImagePath: str = json_field("AbsoluteImagePath", dump=False, default="")

@dataclass
class Prediction():
    Status: bool
    Score: float
    Sample: Sample

@dataclass
class GroundTruth():
    Sample: Sample

@dataclass
class GroundTruthAndPredictions():
    GroundTruth: GroundTruth
    Predictions: list[Prediction]

@dataclass
class Coordinates():
    Lon: float
    Lat: float
@dataclass
class DatasetInfo():
    SampleDistance: float = 0
    SliceCount: int = 0
    SampleCount: int = -1
    BoundingPolygon: list[Coordinates] = field(default_factory=list)

@dataclass
class SampleDataset(JSONWizard):
    class _(JSONWizard.Meta):
        key_transform_with_load = 'PASCAL'
        key_transform_with_dump = 'PASCAL'

    Info: DatasetInfo
    Samples: list[Sample]

@dataclass
class EvaluationResult(JSONWizard):
    class _(JSONWizard.Meta):
        key_transform_with_load = 'PASCAL'
        key_transform_with_dump = 'PASCAL'

    Info: DatasetInfo
    Radius: float
    RecallIntervalls: list[int]
    RecallCounts: list[int]
    RecallPercentages: list[float]
    PredictionPairs: list[GroundTruthAndPredictions] = field(default_factory=list)

def load_dataset(dataset_path: str) -> SampleDataset:
    dataset_json = json.load(open(dataset_path, "r"))
    dataset = SampleDataset.from_dict(dataset_json)
    root_location = str(Path(dataset_path).parent)
    dataset.Info.SampleCount = len(dataset.Samples)
    for sample in dataset.Samples:
        sample.AbsoluteImagePath = str(root_location / PurePath(sample.ImagePath))
    return dataset