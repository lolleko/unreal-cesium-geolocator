// Fill out your copyright notice in the Description page of Project Settings.

#include "MTSamplerComponentBase.h"

#include "Camera/CameraComponent.h"
#include "Cesium3DTileset.h"
#include "CesiumCameraManager.h"
#include "Components/SceneCaptureComponentCube.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/TextureRenderTargetCube.h"
#include "EngineUtils.h"
#include "JsonDomBuilder.h"
#include "MTSample.h"
#include "MTSamplingFunctionLibrary.h"
#include "MTSceneCaptureCube.h"
#include "MTWayGraphSamplerConfig.h"
#include "Geolocator/Interaction/MTPlayerPawn.h"

void UMTSamplerComponentBase::BeginPlay()
{
    Super::BeginPlay();
}

void UMTSamplerComponentBase::BeginSampling()
{
    if (!ensure(IsValid(GetActiveConfig())))
    {
        return;
    }

    bIsSampling = true;
    CurrentSampleCount = 0;
    
    GotoNextSampleStep(ENextSampleStep::InitSampling);
}

bool UMTSamplerComponentBase::IsSampling()
{
    return bIsSampling;
}

int32 UMTSamplerComponentBase::GetCurrentSampleCount()
{
    return CurrentSampleCount;
}

UMTWayGraphSamplerConfig* UMTSamplerComponentBase::GetActiveConfig() const
{
    return Config;
}

void UMTSamplerComponentBase::InitSampling()
{
    auto* CameraManager = ACesiumCameraManager::GetDefaultCameraManager(GetWorld());
    CesiumGroundLoaderCameraID = CameraManager->AddCamera(
        {{128, 128}, GetComponentLocation() + FVector(0, 0, 1000), {-90., 0., 0.}, 50.});

    for (auto* Tileset : TActorRange<ACesium3DTileset>(GetWorld()))
    {
        Tileset->PlayMovieSequencer();
    }


    PanoramaCapture = GetWorld()->SpawnActor<AMTSceneCaptureCube>();
    PanoramaCapture->GetCaptureComponentCube()->TextureTarget =
        NewObject<UTextureRenderTargetCube>(this);
    PanoramaCapture->GetCaptureComponentCube()->TextureTarget->CompressionSettings =
        TextureCompressionSettings::TC_Default;
    PanoramaCapture->GetCaptureComponentCube()->TextureTarget->TargetGamma = 2.2F;
    PanoramaCapture->GetCaptureComponentCube()->TextureTarget->Init(
        GetActiveConfig()->PanoramaWidth / 2, EPixelFormat::PF_B8G8R8A8);
    PanoramaCapture->GetCaptureComponentCube()->ShowFlags.SetToneCurve(ShouldUseToneCurve());


    PanoramaCapture->GetRenderTargetLongLat()->InitCustomFormat(
        PanoramaCapture->GetCaptureComponentCube()->TextureTarget->SizeX * 2,
        PanoramaCapture->GetCaptureComponentCube()->TextureTarget->SizeX,
        EPixelFormat::PF_B8G8R8A8,
        false);

    for (int32 I = 0; I < CesiumPanoramaLoaderCameraIDs.Num(); ++I)
    {
        CesiumPanoramaLoaderCameraIDs[I] = CameraManager->AddCamera(
            {{static_cast<double>(GetActiveConfig()->PanoramaWidth / 2.),
              GetActiveConfig()->PanoramaWidth / 2.},
             GetComponentLocation(),
             {0., I * 90., 0.},
             90.});
    }
    

    // TODO refactor into GetPriamryPlayerPawn
    const auto PlayerPawn = CastChecked<AMTPlayerPawn>(
        GetWorld()->GetGameInstance()->GetPrimaryPlayerController()->GetPawn());
    PlayerPawn->GetCaptureCameraComponent()->SetActive(true);
    PlayerPawn->GetOverviewCameraComponent()->SetActive(false);

    const auto ConfigDescriptorObj = CollectConfigDescription();
    
    const auto FileHeader = FString::Printf(
        TEXT("{\r\n\"Info\":\r\n%s,\r\n\"Samples\":\r\n["),
        *ConfigDescriptorObj.ToString<>());

    FString SamplingMetadata = FPaths::Combine(
    GetSessionDir(),
    TEXT("metadata.json"));
    
    MetadataFilePath = SamplingMetadata;
    bIsMetadataFileNew = true;

    if (!FPaths::FileExists(MetadataFilePath))
    {
        EnqueueFileWriteTask(FileHeader);
    }
    
    GotoNextSampleStep(ENextSampleStep::FindNextSampleLocation);
}

void UMTSamplerComponentBase::FindNextSampleLocation()
{
    CurrentSampleCount++;

    auto PossibleSampleLocation = SampleNextLocation();

    while (!PossibleSampleLocation)
    {
        CurrentSampleCount++;

        PossibleSampleLocation = SampleNextLocation();

        if (!bIsSampling)
        {
            return;
        }
    }
    
    const FTransform SampleTransform = PossibleSampleLocation.GetValue();

    GetOwner()->SetActorTransform(SampleTransform);

    // make sure ground and surroudnigs are loaded
    UpdateCesiumCameras();

    GotoNextSampleStep(ENextSampleStep::PreCaptureSample);
}

void UMTSamplerComponentBase::PreCapture()
{
    TRACE_CPUPROFILER_EVENT_SCOPE(MTSampling::PreCapture);

    const auto PossibleUpdatedTransform = ValidateSampleLocation();

    if (!PossibleUpdatedTransform)
    {
        GotoNextSampleStep(ENextSampleStep::FindNextSampleLocation);
        return;
    }

    const auto UpdatedTransform = PossibleUpdatedTransform.GetValue();

    const auto MaximumHeightDifference = FVector(0., 0., 10000.);

    TStaticArray<FVector, 9> TraceOffsets;
    TraceOffsets[0] = {-75, 0, 0};
    TraceOffsets[1] = {0, 0, 0};
    TraceOffsets[2] = {75, 0, 0};
    TraceOffsets[3] = {-75, -75, 0};
    TraceOffsets[4] = {0, -75, 0};
    TraceOffsets[5] = {75, -75, 0};
    TraceOffsets[6] = {-75, 75, 0};
    TraceOffsets[7] = {0, 75, 0};
    TraceOffsets[8] = {75, 75, 0};
    
    double TraceMissCounter = 0;

    for (const auto& TraceOffset : TraceOffsets)
    {
        FHitResult TopDownTrace;
        GetWorld()->LineTraceSingleByObjectType(
            TopDownTrace,
            UpdatedTransform.GetLocation() + TraceOffset,
            UpdatedTransform.GetLocation() + TraceOffset - MaximumHeightDifference,
            FCollisionObjectQueryParams::AllStaticObjects);

        // if both traces miss we are most likely inside the ground
        if (!TopDownTrace.bBlockingHit)
        {
            TraceMissCounter++;
        }
    }
    
    SampleArtifactProbability = TraceMissCounter / TraceOffsets.Num();
    
    const auto PlayerPawn = CastChecked<AMTPlayerPawn>(
    GetWorld()->GetGameInstance()->GetPrimaryPlayerController()->GetPawn());
    PlayerPawn->SetActorTransform(UpdatedTransform);
    PlayerPawn->GetCaptureCameraComponent()->SetWorldRotation(GetOwner()->GetActorRotation());

    GetOwner()->SetActorTransform(UpdatedTransform);

    PanoramaCapture->SetActorTransform(UpdatedTransform);
    
    UpdateCesiumCameras();
    
    GotoNextSampleStep(ENextSampleStep::CaptureSample);
}

void UMTSamplerComponentBase::CaptureSample()
{
    TRACE_CPUPROFILER_EVENT_SCOPE(MTSampling::CaptureSample);

    FString ImageDir = GetImageDir();

    // Write images from previous run doing this here gives them a couple of frames to complete
    // during this time we can already go to the next sample etc.
    // now we are about to capture again and therefore need to make sure the images are stored

    WaitForRenderThreadReadSurfaceAndWriteImages();

    WaitForMetadataWrites();
    
    FMTSample Sample = CollectSampleMetadata();
    Sample.ArtifactProbability = SampleArtifactProbability;

    if (FMath::IsNearlyEqual(SampleArtifactProbability, 1.))
    {
        ConsecutiveSamplesWithArtifact++;
    } else
    {
        ConsecutiveSamplesWithArtifact = 0;
    }
    
    WriteSampleMetadata(Sample);

    const auto AbsoluteFileName =
    FPaths::ConvertRelativePathToFull(GetSessionDir(), Sample.ImagePath);
    
    EnqueueCapture({PanoramaCapture, AbsoluteFileName});

    if (!CaptureQueue.IsEmpty())
    {
        // Basically the same as in CaptureScene()
        // But we only call SendAllEndOfFrameUpdates once
        GetWorld()->SendAllEndOfFrameUpdates();
        for (const auto& CaptureData : CaptureQueue)
        {
            CastChecked<AMTSceneCaptureCube>(CaptureData.Capture)
                ->GetCaptureComponentCube()
                ->UpdateSceneCaptureContents(GetWorld()->Scene);
        }

        ENQUEUE_RENDER_COMMAND(SceneDrawCompletion)
        (
            [this](FRHICommandListImmediate& RHICmdList)
            {
                TRACE_CPUPROFILER_EVENT_SCOPE(MTSampling::ReadSurface);

                FIntPoint SizeOUT;
                EPixelFormat FormatOUT;
                CubemapHelpers::GenerateLongLatUnwrap(
                    PanoramaCapture->GetCaptureComponentCube()->TextureTarget,
                    PanoramaCapture->GetMutableImageDataRef(),
                    SizeOUT,
                    FormatOUT,
                    PanoramaCapture->GetRenderTargetLongLat(),
                    RHICmdList);
            });

        CaptureFence.BeginFence();
    }

    FileAppendWriterWorkerFuture = Async(
        EAsyncExecution::ThreadPool,
        [this]()
        {
            FString CurrentTask;
            while (FileAppendQueue.Dequeue(CurrentTask))
            {
                FFileHelper::SaveStringToFile(
                    CurrentTask,
                    *MetadataFilePath,
                    FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,
                    &IFileManager::Get(),
                    FILEWRITE_Append);
            }
        });


    if (ConsecutiveSamplesWithArtifact >= 1000)
    {
        ConsecutiveSamplesWithArtifact = 0;
        for (auto* Tileset : TActorRange<ACesium3DTileset>(GetWorld()))
        {
            Tileset->RefreshTileset();
        }
    }
    
    // Call Capture function on remaining samples
    GotoNextSampleStep(ENextSampleStep::FindNextSampleLocation);
}

void UMTSamplerComponentBase::GotoNextSampleStep(const ENextSampleStep NextStep)
{
    if (!bIsSampling)
    {
        return;
    }

    switch (NextStep)
    {
        case ENextSampleStep::InitSampling:
            // skip one frame for initialization
            GetWorld()->GetTimerManager().SetTimerForNextTick(
                this, &UMTSamplerComponentBase::InitSampling);
            break;
        case ENextSampleStep::FindNextSampleLocation:
            GetWorld()->GetTimerManager().SetTimerForNextTick(
                this, &UMTSamplerComponentBase::FindNextSampleLocation);
            break;
        case ENextSampleStep::PreCaptureSample:
            GetWorld()->GetTimerManager().SetTimerForNextTick(
                this, &UMTSamplerComponentBase::PreCapture);
            break;
        case ENextSampleStep::CaptureSample:
            GetWorld()->GetTimerManager().SetTimerForNextTick(
                this, &UMTSamplerComponentBase::CaptureSample);
            break;
        default:
            check(false);
    }
}


void UMTSamplerComponentBase::EnqueueCapture(
    const FMTCaptureImagePathPair& CaptureImagePathPair)
{
    CaptureQueue.Add(CaptureImagePathPair);
}

void UMTSamplerComponentBase::EnqueueFileWriteTask(const FString& Data)
{
    FileAppendQueue.Enqueue(Data);
}

void UMTSamplerComponentBase::WriteSampleMetadata(const FMTSample& Sample)
{
    FJsonDomBuilder::FObject SampleObj;
    SampleObj.Set(TEXT("ImagePath"), Sample.ImagePath);
    SampleObj.Set(TEXT("Lon"), Sample.LonLatAltitude.X);
    SampleObj.Set(TEXT("Lat"), Sample.LonLatAltitude.Y);
    SampleObj.Set(TEXT("Altitude"), Sample.LonLatAltitude.Z);
    SampleObj.Set(TEXT("HeadingAngle"), Sample.HeadingAngle);
    SampleObj.Set(TEXT("StreetName"), Sample.StreetName);
    SampleObj.Set(TEXT("ArtifactProbability"), Sample.ArtifactProbability);
    
    const auto SampleMetadataString = SampleObj.ToString<>();
    
    const auto JSONString = bIsMetadataFileNew ? SampleMetadataString : TEXT(",") + SampleMetadataString;
    EnqueueFileWriteTask(JSONString);

    if (bIsMetadataFileNew)
    {
        bIsMetadataFileNew = false;
    }
}

void UMTSamplerComponentBase::UpdateCesiumCameras()
{
    auto* CameraManager = ACesiumCameraManager::GetDefaultCameraManager(GetWorld());
    CameraManager->UpdateCamera(
        CesiumGroundLoaderCameraID,
        {{128, 128}, GetComponentLocation() + FVector(0, 0, 1000), {-90., 0., 0.}, 50.});
    
    for (int32 I = 0; I < CesiumPanoramaLoaderCameraIDs.Num(); ++I)
    {
        CameraManager->UpdateCamera(CesiumPanoramaLoaderCameraIDs[I],
{{static_cast<double>(GetActiveConfig()->PanoramaWidth / 4.), GetActiveConfig()->PanoramaWidth / 2.}, GetComponentLocation(), {0., I * 90., 0.}, 90.});
    }

}

FString UMTSamplerComponentBase::GetImageDir()
{
    return FPaths::Combine(GetSessionDir(), "Images");
}

void UMTSamplerComponentBase::WaitForRenderThreadReadSurfaceAndWriteImages()
{
    TRACE_CPUPROFILER_EVENT_SCOPE(MTSampling::WaitForReadSurface);

    if (!CaptureQueue.IsEmpty())
    {
        // Allow other tasks to execute, we only care that out captures are not modified
        CaptureFence.Wait(true);

        for (const auto& CaptureData : CaptureQueue)
        {
            auto* Capture = CastChecked<AMTSceneCaptureCube>(CaptureData.Capture);
            if (!UMTSamplingFunctionLibrary::WriteCubeMapPixelBufferToFile(
                    CaptureData.AbsoluteImagePath,
                    Capture->GetMutableImageDataRef(),
                    {GetActiveConfig()->PanoramaWidth,
                     Capture->GetCaptureComponentCube()->TextureTarget->SizeX},
                    {GetActiveConfig()->PanoramaTopCrop,
                     GetActiveConfig()->PanoramaBottomCrop}))
            {
                // stop sampling
                check(false);
                return;
            }
        }

        CaptureQueue.Reset();
    }
}

void UMTSamplerComponentBase::WaitForMetadataWrites()
{
    FileAppendWriterWorkerFuture.Wait();
}

void UMTSamplerComponentBase::CloseMetadataFile()
{
    WaitForMetadataWrites();
    
    FFileHelper::SaveStringToFile(
        TEXT("]}"),
        *MetadataFilePath,
        FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,
        &IFileManager::Get(),
        FILEWRITE_Append);
}

void UMTSamplerComponentBase::EndSampling()
{
    // Wait for last batch
    WaitForRenderThreadReadSurfaceAndWriteImages();

    WaitForMetadataWrites();

    PanoramaCapture->Destroy();

    // TODO refactor into GetPriamryPlayerPawn
    const auto PlayerPawn = CastChecked<AMTPlayerPawn>(
        GetWorld()->GetGameInstance()->GetPrimaryPlayerController()->GetPawn());
    PlayerPawn->GetCaptureCameraComponent()->SetActive(false);
    PlayerPawn->GetOverviewCameraComponent()->SetActive(true);

    CloseMetadataFile();

    bIsSampling = false;
}

bool UMTSamplerComponentBase::ShouldSampleOnBeginPlay() const
{
    return bShouldSampleOnBeginPlay;
}

FString UMTSamplerComponentBase::GetSessionDir() const
{
    FString SessionDir = FPaths::ConvertRelativePathToFull(FPaths::Combine(
        FPaths::ProjectSavedDir(),
        TEXT("WorldIndex"),
        GetWorld()->GetMapName(),
        GetActiveConfig()->GetConfigName()));

    return SessionDir;
}
