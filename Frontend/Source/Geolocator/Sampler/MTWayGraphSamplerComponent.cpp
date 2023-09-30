// Fill out your copyright notice in the Description page of Project Settings.

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
    const auto MaximumHeightDifference = FVector(0., 0., 100000.);

    FHitResult GroundHit;
    GetWorld()->LineTraceSingleByObjectType(
        GroundHit,
        GetComponentLocation() + MaximumHeightDifference,
        GetComponentLocation() - MaximumHeightDifference,
        FCollisionObjectQueryParams::AllStaticObjects);

    if (!GroundHit.bBlockingHit)
    {
        return {};
    }

    // ground offset of street view car probably ~250, we attempt to stick close to street view liek
    // scenarios because there is precedent in many papers e.g.
    // @torii24PlaceRecognition2015 & @bertonRethinkingVisualGeolocalization2022
    const auto GroundOffset = FVector(0., 0., 250.);

    auto UpdatedSampleLocation = GroundHit.Location + GroundOffset;

    TArray<FHitResult> Hits;

    constexpr auto TraceCount = 32;
    constexpr auto ClearanceDistance = 800.;

    // Line trace in a sphere around the updated location
    for (int32 TraceIndex = 0; TraceIndex < TraceCount; ++TraceIndex)
    {
        const auto DegreeInterval = 360. / TraceCount;
        FHitResult Hit;

        GetWorld()->LineTraceSingleByObjectType(
            Hit,
            UpdatedSampleLocation,
            UpdatedSampleLocation +
                FRotator(0., TraceIndex * DegreeInterval, 0.).Vector() * ClearanceDistance,
            FCollisionObjectQueryParams::AllStaticObjects);
        if (Hit.bBlockingHit)
        {
            Hits.Add(Hit);
        }
    }

    // Calcualte poitn with clearance form all hits
    if (Hits.Num() > 0)
    {
        FVector Center = FVector::ZeroVector;
        for (const auto& Hit : Hits)
        {
            const auto HitToCam = (Hit.TraceStart - Hit.TraceEnd).GetSafeNormal();
            const auto ClearPoint = Hit.Location + HitToCam * ClearanceDistance;
            Center += ClearPoint;
        }
        Center /= Hits.Num();
        UpdatedSampleLocation = Center;
    }

    GetWorld()->LineTraceSingleByObjectType(
        GroundHit,
        UpdatedSampleLocation + MaximumHeightDifference,
        UpdatedSampleLocation - MaximumHeightDifference,
        FCollisionObjectQueryParams::AllStaticObjects);

    if (!GroundHit.bBlockingHit)
    {
        return {};
    }

    UpdatedSampleLocation = GroundHit.Location + GroundOffset;

    return {FTransform(GetOwner()->GetActorRotation(), UpdatedSampleLocation)};
}

FMTSample UMTWayGraphSamplerComponent::CollectSampleMetadata()
{
    const auto* Georeference = ACesiumGeoreference::GetDefaultGeoreference(GetWorld());

    const auto SampleLonLat =
        Georeference->TransformUnrealPositionToLongitudeLatitudeHeight(GetComponentLocation());
    const auto EastSouthUp = Georeference->TransformUnrealRotatorToEastSouthUp(
        GetComponentRotation(), GetComponentLocation());
    const auto HeadingAngle = FRotator::ClampAxis(EastSouthUp.Yaw);

    const auto StreetName = StreetData.Graph.GetWayName(CurrentWayIndex);

    // We explicilty use the location calculated from SampleNextLocation
    // and not long lat above or current position after adjustments
    // this will ensure we have a consistent location for the same sample across multiple runs
    // we mutliple by 1000 and round to store the first 4 decimal places and ensuring we wont have
    // issues with floating point precision
    FString SampleName = FString::Format(
        TEXT("{0}-{1}-{2}-{3}"),
        {FMath::RoundToInt64(CurrentSampleLocation.X * 1000),
         FMath::RoundToInt64(CurrentSampleLocation.Y * 1000),
         FMath::RoundToInt64(CurrentSampleLocation.Z * 1000),
         CurrentWayIndex});

    const auto RelativeFileName =
        FPaths::Combine("./Images", FString::Format(TEXT("{0}.jpg"), {SampleName}));

    return {RelativeFileName, {}, HeadingAngle, SampleLonLat, StreetName, 0.};
}

FJsonDomBuilder::FObject UMTWayGraphSamplerComponent::CollectConfigDescription()
{
    const auto DistanceInterval = GetActiveConfig()->SampleDistance;

    FJsonDomBuilder::FObject ConfigDescriptorObj;
    ConfigDescriptorObj.Set(TEXT("SampleDistance"), DistanceInterval);

    FJsonDomBuilder::FArray PolygonArray;

    if (BoundingPolygon)
    {
        const auto SampledPolygon =
            BoundingPolygon->CreateCartographicPolygon(FTransform::Identity);
        for (const auto& VertexCoord : SampledPolygon.getVertices())
        {
            FJsonDomBuilder::FObject CordObj;
            CordObj.Set(TEXT("Lon"), FMath::RadiansToDegrees(VertexCoord.x));
            CordObj.Set(TEXT("Lat"), FMath::RadiansToDegrees(VertexCoord.y));

            PolygonArray.Add(CordObj);
        }
    }

    ConfigDescriptorObj.Set(TEXT("BoundingPolygon"), PolygonArray);

    return ConfigDescriptorObj;
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
