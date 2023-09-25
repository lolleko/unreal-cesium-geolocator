// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"

#include "MTWayGraphSamplerConfig.generated.h"

/**
 * 
 */
UCLASS()
class GEOLOCATOR_API UMTWayGraphSamplerConfig : public UDataAsset
{
    GENERATED_BODY()

public:
    // Roughly the distance between street view captures
    UPROPERTY(EditAnywhere)
    double SampleDistance = 800.;
    
    UPROPERTY(EditAnywhere)
    int32 PanoramaWidth =  3328;

    UPROPERTY(EditAnywhere)
    int32 PanoramaTopCrop =  512;

    UPROPERTY(EditAnywhere)
    int32 PanoramaBottomCrop =  640;

    UPROPERTY(EditAnywhere)
    bool bShouldUseToneCurve = true;
    
    FString GetConfigName() const;

    double GetMinDistanceBetweenSamples() const;
};
