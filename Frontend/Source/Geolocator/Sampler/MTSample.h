// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"

#include "MTSample.generated.h"

/**
 * 
 */
USTRUCT(BlueprintType)
struct GEOLOCATOR_API FMTSample
{
    GENERATED_BODY()
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FString ImagePath; // Empty if image not saved
    
    TArray<ANSICHAR> Descriptor;
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    double HeadingAngle;
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FVector LonLatAltitude;
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FString StreetName;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    double ArtifactProbability;

    // Only present during inference
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    double Score;
};
