// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "../WayGraph/MTWayGraph.h"
#include "CesiumCartographicPolygon.h"
#include "CoreMinimal.h"
#include "Geolocator/WayGraph/MTChinesePostMan.h"
#include "MTSample.h"
#include "MTSamplerComponentBase.h"

#include "MTWayGraphSamplerComponent.generated.h"

USTRUCT()
struct FMTStreetData
{
    GENERATED_BODY()

    UPROPERTY()
    FMTWayGraph Graph;

    UPROPERTY()
    TArray<FMTWayGraphPath> Paths;

    UPROPERTY()
    double TotalPathLength = 0.;
};

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class GEOLOCATOR_API UMTWayGraphSamplerComponent : public UMTSamplerComponentBase
{
    GENERATED_BODY()

public:
    UMTWayGraphSamplerComponent();

    virtual int32 GetEstimatedSampleCount() override;

    virtual void BeginPlay() override;
    
protected:
    virtual TOptional<FTransform> SampleNextLocation() override;

    virtual TOptional<FTransform> ValidateSampleLocation() override;

    virtual FMTSample CollectSampleMetadata() override;

    virtual FJsonDomBuilder::FObject CollectConfigDescription() override;
    
private:
    UPROPERTY(EditAnywhere)
    TObjectPtr<ACesiumCartographicPolygon> BoundingPolygon;

    int32 EstimatedSampleCount;
    
    FMTStreetData StreetData;
    
    int32 CurrentImageCount;
    
    int32 CurrentPathIndex;

    int32 CurrentPathSegmentIndex;

    double CurrentPathSegmentStartDistance;

    double CurrentPathDistance;

    FVector CurrentSampleLocation;

    int32 CurrentWayIndex;

    int32 PrevWayIndex;
    
    FVector PrevSampleLocation;
    
    struct FSampledLocationWithGraphEdgeID
    {
        FVector Location;
        int32 WayID;

        bool operator==(const FSampledLocationWithGraphEdgeID& Other) const
        {
            return Location == Other.Location && WayID == Other.WayID;
        }

        friend uint32 GetTypeHash(const FSampledLocationWithGraphEdgeID& InPath)
        {
            return GetTypeHash(InPath.Location) ^ GetTypeHash(InPath.WayID);
        }
    };
    TSet<FSampledLocationWithGraphEdgeID> SampledLocationsLSH;

    FMTOverPassQueryCompletionDelegate OverpassQueryCompletedDelegate;

    void InitSamplingParameters();
    
    UFUNCTION()
    void OverpassQueryCompleted(const FOverPassQueryResult& Result, const bool bSuccess);
    
    TConstArrayView<int32> ViewCurrentPath();

    FString GetStreetDataCacheFilePath() const;
};
