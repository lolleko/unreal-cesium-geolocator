// Fill out your copyright notice in the Description page of Project Settings.

#include "MTWayGraphSamplerComponent.h"

#include "JsonDomBuilder.h"
#include "MTSample.h"
#include "Geolocator/OSM/MTOverpassConverter.h"
#include "Geolocator/OSM/MTOverpassQuery.h"
#include "Geolocator/WayGraph/MTChinesePostMan.h"

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
        OverpassQueryCompletedDelegate.BindUFunction(this, TEXT("OverpassQueryCompleted"));
        const auto OverpassQuery = MTOverpass::BuildQueryStringFromBoundingPolygon(BoundingPolygon);
        MTOverpass::AsyncQuery(OverpassQuery, OverpassQueryCompletedDelegate);
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

        if (CurrentPathIndex > PathsContainingAllEdges.Num() - 1)
        {
            EndSampling();
            return {};
        }

        CurrentSampleLocation =
            Graph.GetNodeLocationUnreal(ViewCurrentPath()[CurrentPathSegmentIndex], GeoRef);
    }

    PrevSampleLocation = CurrentSampleLocation;
    PrevWayIndex = CurrentWayIndex;

    FQuat CurrentEdgeDir;

    auto SegmentEndDistance = CurrentPathSegmentStartDistance;

    const auto NextSampleDistance = CurrentPathDistance + GetActiveConfig()->SampleDistance;

    for (; CurrentPathSegmentIndex < ViewCurrentPath().Num() - 1; ++CurrentPathSegmentIndex)
    {
        const auto StartPoint =
            Graph.GetNodeLocationUnreal(ViewCurrentPath()[CurrentPathSegmentIndex], GeoRef);

        const auto EndPoint =
            Graph.GetNodeLocationUnreal(ViewCurrentPath()[CurrentPathSegmentIndex + 1], GeoRef);

        const auto SegmentLength = FVector::Dist(StartPoint, EndPoint);

        CurrentPathSegmentStartDistance = SegmentEndDistance;
        SegmentEndDistance += SegmentLength;

        if (NextSampleDistance <= SegmentEndDistance)
        {
            const auto Alpha = (SegmentEndDistance - NextSampleDistance) /
                               (SegmentEndDistance - CurrentPathSegmentStartDistance);

            CurrentSampleLocation = FMath::Lerp(StartPoint, EndPoint, 1 - Alpha);

            CurrentWayIndex = Graph.GetEdgeWay(Graph.NodePairToEdgeIndex(
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

        return {FTransform(CurrentEdgeDir, CurrentSampleLocation)};
    }
}

TOptional<FTransform> UMTWayGraphSamplerComponent::ValidateSampleLocation()
{
    const auto MaximumHeightDifference = FVector(0., 0., 10000.);

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

    const auto StreetName = Graph.GetWayName(CurrentWayIndex);
    
    const auto RelativeFileName =
    FPaths::Combine("./Images", FString::Printf(TEXT("%d.jpg"), CurrentImageCount));
    CurrentImageCount++;

    return {RelativeFileName, {}, HeadingAngle, SampleLonLat, StreetName, 0.};
}

FJsonDomBuilder::FObject UMTWayGraphSamplerComponent::CollectConfigDescription()
{
    const auto DistanceInterval = GetActiveConfig()->SampleDistance;
    
    FJsonDomBuilder::FObject ConfigDescriptorObj;
    ConfigDescriptorObj.Set(
        TEXT("SampleDistance"), DistanceInterval);

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

void UMTWayGraphSamplerComponent::OverpassQueryCompleted(
    const FOverPassQueryResult& Result,
    const bool bSuccess)
{
    if (!bSuccess)
    {
        return;
    }

    Graph = MTOverpass::CreateStreetGraphFromQuery(Result, BoundingPolygon);

    const auto* GeoRef = ACesiumGeoreference::GetDefaultGeoreference(GetWorld());
    PathsContainingAllEdges = FMTChinesePostMan::CalculatePathsThatContainAllEdges(Graph, GeoRef);

    double TotalDistance = 0.;
    for (int32 PathIndex = 0; PathIndex < PathsContainingAllEdges.Num(); ++PathIndex)
    {
        for (int32 PathNodeIndex = 0; PathNodeIndex < PathsContainingAllEdges[PathIndex].Num() - 1;
             ++PathNodeIndex)
        {
            const auto StartLocation = Graph.GetNodeLocationUnreal(
                PathsContainingAllEdges[PathIndex][PathNodeIndex], GeoRef);
            const auto EndLocation = Graph.GetNodeLocationUnreal(
                PathsContainingAllEdges[PathIndex][PathNodeIndex + 1], GeoRef);

            TotalDistance += FVector::Dist(StartLocation, EndLocation);
        }
    }

    EstimatedSampleCount = (TotalDistance / GetActiveConfig()->SampleDistance);
    
    CurrentPathSegmentIndex = 0;
    CurrentPathSegmentStartDistance = 0.;
    CurrentPathDistance = 0.;
    CurrentPathIndex = 0;
    CurrentWayIndex = 0;
    CurrentImageCount = 0;
    SampledLocationsLSH.Reset();
    CurrentSampleLocation = Graph.GetNodeLocationUnreal(ViewCurrentPath()[0], GeoRef);

    BeginSampling();
}

TConstArrayView<int32> UMTWayGraphSamplerComponent::ViewCurrentPath()
{
    return PathsContainingAllEdges[CurrentPathIndex];
}

