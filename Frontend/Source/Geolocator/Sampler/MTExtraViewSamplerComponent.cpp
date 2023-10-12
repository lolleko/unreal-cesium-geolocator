﻿// Fill out your copyright notice in the Description page of Project Settings.

#include "MTExtraViewSamplerComponent.h"

#include "Kismet/KismetMathLibrary.h"
#include "MTSamplingFunctionLibrary.h"

void UMTExtraViewSamplerComponent::BeginPlay()
{
    Super::BeginPlay();

    if (ShouldSampleOnBeginPlay())
    {
        FString GeolocatorPredictions;
        if (!FParse::Value(FCommandLine::Get(), TEXT("GeolocatorPredictions"), GeolocatorPredictions)) 
        {
            GeolocatorPredictions = TestPredictionFile;
        }

        Locations = UMTSamplingFunctionLibrary::PredictionsLocationsFromFile(
            GeolocatorPredictions, ACesiumGeoreference::GetDefaultGeoreference(GetWorld()));

        TogglePanoramaCapture(false);
        CurrentPredictionIndex = INDEX_NONE;
        CurrentSampleGridCellIndex = 0;
        SampleGridRadius = 1;
        SampleGridCellSize = FVector(800, 800, 200);
        SampleGrid = TMTNDGridAccessor<3>({2*SampleGridRadius, 2*SampleGridRadius, 1});

        InitialLocationCount = Locations.Num();

        BeginSampling();
    }
}
int32 UMTExtraViewSamplerComponent::GetEstimatedSampleCount()
{
    return InitialLocationCount * SampleGrid.CellCount();
}

TOptional<FTransform> UMTExtraViewSamplerComponent::SampleNextLocation()
{
    if (CurrentSampleGridCellIndex >= SampleGrid.CellCount() || CurrentPredictionIndex == INDEX_NONE)
    {
        CurrentSampleGridCellIndex = 0;
        if (CurrentPredictionIndex != INDEX_NONE)
        {
            Locations.RemoveAtSwap(CurrentPredictionIndex);
        }

        if (Locations.IsEmpty())
        {
            EndSampling();
            return {};
        }
        
        double ClosestSampleDistance = MAX_dbl;
    
        for (int32 I = 0; I < Locations.Num(); ++I)
        {
            const auto SampleLocation = Locations[I].Location;

            const auto Distance = FVector::DistSquared(GetOwner()->GetActorLocation(), SampleLocation.GetLocation());
            if (Distance < ClosestSampleDistance)
            {
                ClosestSampleDistance = Distance;
                CurrentPredictionIndex = I;
            }
        }
    }
    
    const auto CellCoords = SampleGrid.IndexToCoords(CurrentSampleGridCellIndex);
    const auto OffsetCellCoords = FVector(CellCoords.X, CellCoords.Y, CellCoords.Z) - FVector(SampleGrid.GetDimensionSizes().X, SampleGrid.GetDimensionSizes().Y, SampleGrid.GetDimensionSizes().Z)  / 2 + FVector(0.5, 0.5, 0.5);
    
    const auto CellCenter = FVector(OffsetCellCoords.X, OffsetCellCoords.Y, OffsetCellCoords.Z) * SampleGridCellSize;
    // TODO should we use pitch roll heading from original?
    
    auto CellTransform = Locations[CurrentPredictionIndex].Location;
    CellTransform.AddToTranslation(CellCenter);
    
    CurrentSampleGridCellIndex++;

    return CellTransform;
}

TOptional<FTransform> UMTExtraViewSamplerComponent::ValidateSampleLocation()
{
    const auto GroundTransform = ValidateGroundAndObstructions(true);
    if (!GroundTransform.IsSet())
    {
        return {};
    }
    
    const auto OriginalTransform =  Locations[CurrentPredictionIndex].Location;
    auto OriginalPositionWithGroundAdjusted = OriginalTransform.GetTranslation();
    OriginalPositionWithGroundAdjusted.Z = GroundTransform->GetLocation().Z;

    FHitResult AutoFocusHit;
    GetWorld()->LineTraceSingleByObjectType(
        AutoFocusHit,
        OriginalPositionWithGroundAdjusted,
        OriginalPositionWithGroundAdjusted +  OriginalTransform.GetRotation().Vector() * 5000,
        FCollisionObjectQueryParams::AllStaticObjects);

    const auto FocalDistance = AutoFocusHit.bBlockingHit ? FMath::Clamp(AutoFocusHit.Distance, 1000., 5000.) : 2500.;
    
    //const auto CurrentFocalPoint = Locations[CurrentPredictionIndex].Location.GetLocation() + Locations[CurrentPredictionIndex].Location.Rotator().Vector() * FocalDistances[CurrentFocalDistanceIndex];
    const auto CurrentFocalPoint = OriginalPositionWithGroundAdjusted + OriginalTransform.GetRotation().Vector() * FocalDistance;

    const auto LookAtRotation = FRotationMatrix::MakeFromX(CurrentFocalPoint - GroundTransform->GetLocation()).ToQuat();
    auto OrientedTransform = FTransform(LookAtRotation, GroundTransform->GetLocation() + FVector(0, 0, 200));

    return OrientedTransform;
}

FMTSample UMTExtraViewSamplerComponent::CollectSampleMetadata()
{
    const auto* Georeference = ACesiumGeoreference::GetDefaultGeoreference(GetWorld());

    const auto SampleLonLat =
        Georeference->TransformUnrealPositionToLongitudeLatitudeHeight(GetComponentLocation());
    const auto EastSouthUp = GetComponentRotation();
    const auto HeadingAngle = FRotator::ClampAxis(EastSouthUp.Yaw + 90.);
    const auto Pitch = FRotator::ClampAxis(EastSouthUp.Pitch + 90.);

    return {Locations[CurrentPredictionIndex].Path, {}, {}, HeadingAngle, Pitch, EastSouthUp.Roll, SampleLonLat, TEXT("")};
}

FJsonDomBuilder::FObject UMTExtraViewSamplerComponent::CollectConfigDescription()
{
    FJsonDomBuilder::FObject ConfigDescriptorObj;
    
    return ConfigDescriptorObj;
}
