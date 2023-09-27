// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/SceneCapture.h"
#include "JsonDomBuilder.h"
#include "MTSample.h"
#include "MTSceneCaptureCube.h"
#include "MTWayGraphSamplerConfig.h"

#include "MTSamplerComponentBase.generated.h"

USTRUCT()
struct FMTCaptureImagePathPair
{
    GENERATED_BODY()

    ASceneCapture* Capture;
    FString AbsoluteImagePath;
};

UCLASS(ClassGroup=(Custom), Abstract)
class GEOLOCATOR_API UMTSamplerComponentBase : public USceneComponent
{
	GENERATED_BODY()

public:
    void BeginPlay() override;
    
    void BeginSampling();

    UFUNCTION(BlueprintCallable)
    bool IsSampling();

    UFUNCTION(BlueprintCallable)
    int32 GetCurrentSampleCount();
    
    UFUNCTION(BlueprintCallable)
    virtual int32 GetEstimatedSampleCount()
    {
        PURE_VIRTUAL(ValidateSampleLocation)
        return 0;
    }

    UMTWayGraphSamplerConfig* GetActiveConfig() const;

protected:

    virtual TOptional<FTransform> ValidateSampleLocation()
    {
        PURE_VIRTUAL(ValidateSampleLocation)
        return GetComponentTransform();
    }

    virtual TOptional<FTransform> SampleNextLocation()
    {
        PURE_VIRTUAL(SampleNextLocation)
        return {};
    }

    virtual FMTSample CollectSampleMetadata()
    {

        PURE_VIRTUAL(CollectSampleMetadata)
        return {};
    };

    virtual FJsonDomBuilder::FObject CollectConfigDescription()
    {
        PURE_VIRTUAL(ConfigDescription)
        return {};
    }

    bool ShouldUseToneCurve() const
    {
        return GetActiveConfig()->bShouldUseToneCurve;
    }

    void IncrementCurrentSampleCount()
    {
        CurrentSampleCount++;
    }
    
    void EndSampling();

    bool ShouldSampleOnBeginPlay() const;

    FString GetSessionDir() const;

private:
    UPROPERTY()
    TObjectPtr<AMTSceneCaptureCube> PanoramaCapture;

    UPROPERTY(EditAnywhere)
    bool bShouldSampleOnBeginPlay = false;
    
    UPROPERTY(EditAnywhere)
    TObjectPtr<UMTWayGraphSamplerConfig> Config;

    int32 CurrentSampleCount;
    
    bool bIsSampling = false;

    int32 CesiumGroundLoaderCameraID;

    TStaticArray<int32, 4> CesiumPanoramaLoaderCameraIDs;

    double SampleArtifactProbability = 0.;

    int32 ConsecutiveSamplesWithArtifact = 0;

    int32 SamplesSinceLastTilesetRefresh = 0;

    TArray<FMTCaptureImagePathPair> CaptureQueue;

    FRenderCommandFence CaptureFence;

    TQueue<FString> FileAppendQueue;

    FString MetadataFilePath;

    bool bIsFirstWriteToExistingMetadataFile = false;

    bool bIsFirstMetadataFileWrite = true;

    TFuture<void> FileAppendWriterWorkerFuture;

    UFUNCTION()
    void InitSampling();

    UFUNCTION()
    virtual void FindNextSampleLocation();
    
    UFUNCTION()
    void PreCapture();
    
    UFUNCTION()
    void CaptureSample();

    enum class ENextSampleStep
    {
        InitSampling,
        FindNextSampleLocation,
        PreCaptureSample,
        CaptureSample,
    };

    void GotoNextSampleStep(const ENextSampleStep NextStep);

    void EnqueueCapture(const FMTCaptureImagePathPair& CaptureImagePathPair);
    
    void EnqueueFileWriteTask(const FString& Data);

    void WriteSampleMetadata(const FMTSample& Sample);

    void UpdateCesiumCameras();
    
    FString GetImageDir();
    
    void WaitForRenderThreadReadSurfaceAndWriteImages();

    void WaitForMetadataWrites();

    void CloseMetadataFile();

};
