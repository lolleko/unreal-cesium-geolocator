﻿// Fill out your copyright notice in the Description page of Project Settings.

#include "MTWayGraphSamplerComponent.h"

#include "Geolocator/OSM/MTOverpassConverter.h"
#include "Geolocator/OSM/MTOverpassQuery.h"
#include "Geolocator/WayGraph/MTChinesePostMan.h"
#include "JsonDomBuilder.h"
#include "Kismet/KismetTextLibrary.h"
#include "MTSample.h"

UMTWayGraphSamplerComponent::UMTWayGraphSamplerComponent()
{
}

int32 UMTWayGraphSamplerComponent::GetEstimatedSampleCount()
{
    return EstimatedSampleCount;
}

void UMTWayGraphSamplerComponent::BeginPlay()
{
    Super::BeginPlay();

    if (ShouldSampleOnBeginPlay())
    {
        if (FPaths::FileExists(GetStreetDataCacheFilePath()))
        {
            FString StreetDataJSONString;
            FFileHelper::LoadFileToString(StreetDataJSONString, *GetStreetDataCacheFilePath());
            FJsonObjectConverter::JsonObjectStringToUStruct(StreetDataJSONString, &StreetData);
            InitSamplingParameters();
            BeginSampling();
        }
        else
        {
            OverpassQueryCompletedDelegate.BindUFunction(this, TEXT("OverpassQueryCompleted"));
            const auto OverpassQuery =
                MTOverpass::BuildQueryStringFromBoundingPolygon(BoundingPolygon);
            MTOverpass::AsyncQuery(OverpassQuery, OverpassQueryCompletedDelegate);
        }
    }
}

TOptional<FTransform> UMTWayGraphSamplerComponent::SampleNextLocation()
{
    const auto* GeoRef = ACesiumGeoreference::GetDefaultGeoreference(GetWorld());

    if (CurrentPathSegmentIndex == ViewCurrentPath().Num() - 1)
    {
        // GOTO next path or finish
        CurrentPathIndex++;
        CurrentPathSegmentIndex = 0;
        CurrentPathSegmentStartDistance = 0.;
        CurrentPathDistance = 0.;

        if (CurrentPathIndex > StreetData.Paths.Num() - 1)
        {
            EndSampling();
            return {};
        }

        CurrentSampleLocation = StreetData.Graph.GetNodeLocationUnreal(
            ViewCurrentPath()[CurrentPathSegmentIndex], GeoRef);
    }

    PrevSampleLocation = CurrentSampleLocation;
    PrevWayIndex = CurrentWayIndex;

    FQuat CurrentEdgeDir;

    auto SegmentEndDistance = CurrentPathSegmentStartDistance;

    const auto NextSampleDistance = CurrentPathDistance + GetActiveConfig()->SampleDistance;

    for (; CurrentPathSegmentIndex < ViewCurrentPath().Num() - 1; ++CurrentPathSegmentIndex)
    {
        const auto StartPoint = StreetData.Graph.GetNodeLocationUnreal(
            ViewCurrentPath()[CurrentPathSegmentIndex], GeoRef);

        const auto EndPoint = StreetData.Graph.GetNodeLocationUnreal(
            ViewCurrentPath()[CurrentPathSegmentIndex + 1], GeoRef);

        const auto SegmentLength = FVector::Dist(StartPoint, EndPoint);

        CurrentPathSegmentStartDistance = SegmentEndDistance;
        SegmentEndDistance += SegmentLength;

        if (NextSampleDistance <= SegmentEndDistance)
        {
            const auto Alpha = (SegmentEndDistance - NextSampleDistance) /
                               (SegmentEndDistance - CurrentPathSegmentStartDistance);

            CurrentSampleLocation = FMath::Lerp(StartPoint, EndPoint, 1 - Alpha);

            CurrentWayIndex = StreetData.Graph.GetEdgeWay(StreetData.Graph.NodePairToEdgeIndex(
                ViewCurrentPath()[CurrentPathSegmentIndex],
                ViewCurrentPath()[CurrentPathSegmentIndex + 1]));

            CurrentEdgeDir = (EndPoint - StartPoint).Rotation().Quaternion();

            break;
        }

        CurrentSampleLocation = EndPoint;
    }

    CurrentPathDistance = NextSampleDistance;

    const auto SampleLocationDuplicationGrid = FVector(
        FMath::Floor(CurrentSampleLocation.X / GetActiveConfig()->GetMinDistanceBetweenSamples()),
        FMath::Floor(CurrentSampleLocation.Y / GetActiveConfig()->GetMinDistanceBetweenSamples()),
        0.);

    if (SampledLocationsLSH.Contains({SampleLocationDuplicationGrid, CurrentWayIndex}))
    {
        return {};
    }
    else
    {
        const auto PrevSampleLocationDuplicationGrid = FVector(
            FMath::Floor(PrevSampleLocation.X / GetActiveConfig()->GetMinDistanceBetweenSamples()),
            FMath::Floor(PrevSampleLocation.Y / GetActiveConfig()->GetMinDistanceBetweenSamples()),
            0.);

        SampledLocationsLSH.Add({SampleLocationDuplicationGrid, CurrentWayIndex});
        SampledLocationsLSH.Add({PrevSampleLocationDuplicationGrid, CurrentWayIndex});
        SampledLocationsLSH.Add({SampleLocationDuplicationGrid, PrevWayIndex});

        // TODO deduplicate with CollectSampleMetadata
        FString SampleName = FString::Format(
            TEXT("{0}-{1}-{2}-{3}"),
            {FMath::RoundToInt64(CurrentSampleLocation.X * 1000),
             FMath::RoundToInt64(CurrentSampleLocation.Y * 1000),
             FMath::RoundToInt64(CurrentSampleLocation.Z * 1000),
             CurrentWayIndex});

        const auto RelativeFileName =
            FPaths::Combine("./Images", FString::Format(TEXT("{0}.jpg"), {SampleName}));

        const auto AbsoluteImageFilePath =
            FPaths::ConvertRelativePathToFull(GetSessionDir(), RelativeFileName);

        // Assume we are resuming previous run and don't overwrite image or metadata
        if (FPaths::FileExists(AbsoluteImageFilePath))
        {
            return {};
        }

        return {FTransform(CurrentEdgeDir, CurrentSampleLocation)};
    }
}

TOptional<FTransform> UMTWayGraphSamplerComponent::ValidateSampleLocation()
{
    return ValidateGroundAndObstructions();
}

FMTSample UMTWayGraphSamplerComponent::CollectSampleMetadata()
{
    const auto* Georeference = ACesiumGeoreference::GetDefaultGeoreference(GetWorld());

    const auto SampleLonLat =
        Georeference->TransformUnrealPositionToLongitudeLatitudeHeight(GetComponentLocation());
    const auto EastSouthUp = GetComponentRotation();
    const auto HeadingAngle = FRotator::ClampAxis(EastSouthUp.Yaw + 90.);
    const auto Pitch = FRotator::ClampAxis(EastSouthUp.Pitch + 90.);

    const auto StreetName = StreetData.Graph.GetWayName(CurrentWayIndex);
    
    return {{}, {}, {}, HeadingAngle, Pitch, EastSouthUp.Roll, SampleLonLat, StreetName, 0.};
}

void UMTWayGraphSamplerComponent::InitSamplingParameters()
{
    EstimatedSampleCount = (StreetData.TotalPathLength / GetActiveConfig()->SampleDistance);

    const auto* GeoRef = ACesiumGeoreference::GetDefaultGeoreference(GetWorld());

    CurrentPathSegmentIndex = 0;
    CurrentPathSegmentStartDistance = 0.;
    CurrentPathDistance = 0.;
    CurrentPathIndex = 0;
    CurrentWayIndex = 0;
    SampledLocationsLSH.Reset();
    CurrentSampleLocation = StreetData.Graph.GetNodeLocationUnreal(ViewCurrentPath()[0], GeoRef);
}

void UMTWayGraphSamplerComponent::OverpassQueryCompleted(
    const FOverPassQueryResult& Result,
    const bool bSuccess)
{
    if (!bSuccess)
    {
        return;
    }

    StreetData.Graph = MTOverpass::CreateStreetGraphFromQuery(Result, BoundingPolygon);

    const auto* GeoRef = ACesiumGeoreference::GetDefaultGeoreference(GetWorld());
    StreetData.Paths =
        FMTChinesePostMan::CalculatePathsThatContainAllEdges(StreetData.Graph, GeoRef);

    for (int32 PathIndex = 0; PathIndex < StreetData.Paths.Num(); ++PathIndex)
    {
        for (int32 PathNodeIndex = 0; PathNodeIndex < StreetData.Paths[PathIndex].Nodes.Num() - 1;
             ++PathNodeIndex)
        {
            const auto StartLocation = StreetData.Graph.GetNodeLocationUnreal(
                StreetData.Paths[PathIndex].Nodes[PathNodeIndex], GeoRef);
            const auto EndLocation = StreetData.Graph.GetNodeLocationUnreal(
                StreetData.Paths[PathIndex].Nodes[PathNodeIndex + 1], GeoRef);

            StreetData.TotalPathLength += FVector::Dist(StartLocation, EndLocation);
        }
    }

    FString StreetDataJSONString;
    FJsonObjectConverter::UStructToJsonObjectString(StreetData, StreetDataJSONString);
    FFileHelper::SaveStringToFile(StreetDataJSONString, *GetStreetDataCacheFilePath());

    InitSamplingParameters();
    BeginSampling();
}

TConstArrayView<int32> UMTWayGraphSamplerComponent::ViewCurrentPath()
{
    return StreetData.Paths[CurrentPathIndex].Nodes;
}

FString UMTWayGraphSamplerComponent::GetStreetDataCacheFilePath() const
{
    return FPaths::Combine(GetSessionDir(), TEXT("StreetDataCache.json"));
}
