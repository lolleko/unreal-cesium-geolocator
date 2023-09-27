// Fill out your copyright notice in the Description page of Project Settings.

#include "MTSFXLSamplerComponent.h"

#include "Async/ParallelTransformReduce.h"
#include "CesiumCartographicPolygon.h"
#include "GeomTools.h"
#include "MTSamplingFunctionLibrary.h"

void UMTSFXLSamplerComponent::BeginPlay()
{
    Super::BeginPlay();

    if (ShouldSampleOnBeginPlay())
    {
        Locations = UMTSamplingFunctionLibrary::PanoramaLocationsFromCosPlaceCSV(
            FilePath, ACesiumGeoreference::GetDefaultGeoreference(GetWorld()));

        if (IsValid(BoundingPolygon))
        {
            BoundingPolygon->Polygon->ConvertSplineToPolyLine(ESplineCoordinateSpace::World, 1., BoundingPolyline);
            BoundingPolyline2D.Reserve(BoundingPolyline.Num());
            for (const auto& Vert : BoundingPolyline)
            {
                BoundingPolyline2D.Add(FVector2D(Vert.X, Vert.Y));
            }

            BoundingPolyline2D.RemoveAtSwap(BoundingPolyline2D.Num() - 1);

            BoundingBox2D = FBox2d(BoundingPolyline2D);
        }

        CurrentSampleIndex = INDEX_NONE;

        // Initial data cleanup
        // Remove locations that are outside of the bounding polygon
        // Also remove locations that already have been rendered
        for (int32 I = 0; I < Locations.Num(); ++I)
        {
            const auto SampleLocation = Locations[I].Location;

            const auto TestPoint2D = FVector2D(SampleLocation.GetLocation().X, SampleLocation.GetLocation().Y);
            if (IsValid(BoundingPolygon) && (!BoundingBox2D.IsInsideOrOn(TestPoint2D) || !FGeomTools2D::IsPointInPolygon(TestPoint2D, BoundingPolyline2D)))
            {
                Locations.RemoveAtSwap(I);
                I--;
                continue;
            }

            const auto AbsoluteFileName =
            FPaths::Combine(GetSessionDir(), "Images", Locations[I].Path);
            
            if (FPaths::FileExists(AbsoluteFileName))
            {
                Locations.RemoveAtSwap(I);
                I--;
                continue;
            }
        }

        InitialLocationCount = Locations.Num();

        BeginSampling();
    }
}
int32 UMTSFXLSamplerComponent::GetEstimatedSampleCount()
{
    return InitialLocationCount;
}

TOptional<FTransform> UMTSFXLSamplerComponent::SampleNextLocation()
{
    if (CurrentSampleIndex != INDEX_NONE)
    {
        Locations.RemoveAtSwap(CurrentSampleIndex);
    }

    double ClosestSampleDistance = MAX_dbl;
    
    for (int32 I = 0; I < Locations.Num(); ++I)
    {
        const auto SampleLocation = Locations[I].Location;

        const auto Distance = FVector::DistSquared(GetOwner()->GetActorLocation(), SampleLocation.GetLocation());
        if (Distance < ClosestSampleDistance)
        {
            ClosestSampleDistance = Distance;
            CurrentSampleIndex = I;
        }
    }
    
    if (Locations.IsEmpty())
    {
        EndSampling();
        return {};
    }
    
    return Locations[CurrentSampleIndex].Location;
}

TOptional<FTransform> UMTSFXLSamplerComponent::ValidateSampleLocation()
{
    return Locations[CurrentSampleIndex].Location;
}

FMTSample UMTSFXLSamplerComponent::CollectSampleMetadata()
{
    const auto* Georeference = ACesiumGeoreference::GetDefaultGeoreference(GetWorld());

    const auto SampleLonLat =
        Georeference->TransformUnrealPositionToLongitudeLatitudeHeight(GetComponentLocation());
    const auto EastSouthUp = Georeference->TransformUnrealRotatorToEastSouthUp(
        GetComponentRotation(), GetComponentLocation());
    const auto HeadingAngle = FRotator::ClampAxis(EastSouthUp.Yaw);
    const auto RelativeFileName =
        FPaths::Combine("./Images", Locations[CurrentSampleIndex].Path);

    return {RelativeFileName, {}, HeadingAngle, SampleLonLat, TEXT("")};
}

FJsonDomBuilder::FObject UMTSFXLSamplerComponent::CollectConfigDescription()
{
    FJsonDomBuilder::FObject ConfigDescriptorObj;
    
    FJsonDomBuilder::FArray PolygonArray;

    if (BoundingPolygon)
    {
        const auto SampledPolygon = BoundingPolygon->CreateCartographicPolygon(FTransform::Identity);
        for (const auto& VertexCoord : SampledPolygon.getVertices())
        {
            FJsonDomBuilder::FObject CordObj;
            CordObj.Set(
                TEXT("Lon"), FMath::RadiansToDegrees(VertexCoord.x));
            CordObj.Set(
                TEXT("Lat"), FMath::RadiansToDegrees(VertexCoord.y));

            PolygonArray.Add(CordObj);
        }

    }
    
    ConfigDescriptorObj.Set(TEXT("BoundingPolygon"), PolygonArray);

    return ConfigDescriptorObj;
}
