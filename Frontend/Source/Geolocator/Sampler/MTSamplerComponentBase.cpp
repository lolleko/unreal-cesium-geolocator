// Fill out your copyright notice in the Description page of Project Settings.

#include "MTSamplerComponentBase.h"

#include "Camera/CameraComponent.h"
#include "Cesium3DTileset.h"
#include "CesiumCameraManager.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/SceneCaptureComponentCube.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/TextureRenderTargetCube.h"
#include "EngineUtils.h"
#include "Geolocator/Interaction/MTPlayerPawn.h"
#include "JsonDomBuilder.h"
#include "MTSample.h"
#include "MTSamplingFunctionLibrary.h"
#include "MTSceneCaptureCube.h"
#include "MTWayGraphSamplerConfig.h"

UMTSamplerComponentBase::UMTSamplerComponentBase()
{
    PrimaryComponentTick.bCanEverTick = true;
}

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

    GotoNextSampleStep(ENextSampleStep::InitSampling, 4);
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
        Tileset->SetShouldIgnorePlayerCamera(true);
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

    Capture2D = GetWorld()->SpawnActor<AMTSceneCapture>();
    Capture2D->GetCaptureComponent2D()->FOVAngle = 65;
    Capture2D->GetCaptureComponent2D()->TextureTarget = NewObject<UTextureRenderTarget2D>(this);
    Capture2D->GetCaptureComponent2D()->TextureTarget->bAutoGenerateMips = false;
    Capture2D->GetCaptureComponent2D()->TextureTarget->CompressionSettings =
        TextureCompressionSettings::TC_Default;
    Capture2D->GetCaptureComponent2D()->TextureTarget->AddressX = TextureAddress::TA_Clamp;
    Capture2D->GetCaptureComponent2D()->TextureTarget->AddressY = TextureAddress::TA_Clamp;
    Capture2D->GetCaptureComponent2D()->TextureTarget->RenderTargetFormat = RTF_RGBA8;
    Capture2D->GetCaptureComponent2D()->TextureTarget->TargetGamma = 2.2F;
    Capture2D->GetCaptureComponent2D()->TextureTarget->bGPUSharedFlag = true;
    Capture2D->GetCaptureComponent2D()->ShowFlags.SetToneCurve(ShouldUseToneCurve());
    Capture2D->GetCaptureComponent2D()->TextureTarget->InitCustomFormat(
        512, 512, EPixelFormat::PF_B8G8R8A8, false);

    for (int32 I = 0; I < CesiumPanoramaLoaderCameraIDs.Num(); ++I)
    {
        CesiumPanoramaLoaderCameraIDs[I] = CameraManager->AddCamera(
            {{static_cast<double>(GetActiveConfig()->PanoramaWidth / 2.),
              GetActiveConfig()->PanoramaWidth / 2.},
             GetComponentLocation(),
             {0., I * 90., 0.},
             110.});
    }

    // TODO refactor into GetPriamryPlayerPawn
    const auto PlayerPawn = CastChecked<AMTPlayerPawn>(
        GetWorld()->GetGameInstance()->GetPrimaryPlayerController()->GetPawn());
    PlayerPawn->GetCaptureCameraComponent()->SetActive(true);
    PlayerPawn->GetOverviewCameraComponent()->SetActive(false);

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

    GetOwner()->SetActorTransform(UpdatedTransform);

    const auto PlayerPawn = CastChecked<AMTPlayerPawn>(
        GetWorld()->GetGameInstance()->GetPrimaryPlayerController()->GetPawn());
    PlayerPawn->SetActorTransform(UpdatedTransform);
    PlayerPawn->GetCaptureCameraComponent()->SetWorldRotation(GetOwner()->GetActorRotation());

    PanoramaCapture->SetActorTransform(UpdatedTransform);
    PanoramaCapture->AddActorWorldRotation(FRotator(0., 90., 0.));
    Capture2D->SetActorTransform(UpdatedTransform);

    UpdateCesiumCameras();

    GotoNextSampleStep(ENextSampleStep::CaptureSample, 1);
}

namespace
{
    FString CreateImageNameFromSample(const FMTSample& Sample)
    {
        // @ UTM_east @ UTM_north @ UTM_zone_number @ UTM_zone_letter @ latitude @ longitude @
        // pano_id @ tile_num @ heading @ pitch @ roll @ height @ timestamp @ note @ extension
        double UTMEast;
        double UTMNorth;
        TCHAR ZoneLetter;
        int32 ZoneNumber;
        UTM::LLtoUTM(
            Sample.LonLatAltitude.X,
            Sample.LonLatAltitude.Y,
            UTMNorth,
            UTMEast,
            ZoneNumber,
            ZoneLetter);

        //const auto TimeStamp = FDateTime::UtcNow().ToString(TEXT("%Y%m%d_%H%M%S"));

        return FString::Printf(
            TEXT("@%.2f@%.2f@%d@%s@%.5f@%.5f@%s@%s@%.2f@%.2f@%.2f@%.2f@%s@art_prob_%.4f@.jpg"),
            UTMEast,
            UTMNorth,
            ZoneNumber,
            *FString().AppendChar(ZoneLetter),
            Sample.LonLatAltitude.Y,
            Sample.LonLatAltitude.X,
            TEXT(""),
            TEXT(""),
            Sample.HeadingAngle,
            Sample.Pitch,
            Sample.Roll,
            Sample.LonLatAltitude.Z,
            TEXT(""), // TimeStamp
            Sample.ArtifactProbability);
    }
}  // namespace

void UMTSamplerComponentBase::CaptureSample()
{
    TRACE_CPUPROFILER_EVENT_SCOPE(MTSampling::CaptureSample);

    // Write images from previous run doing this here gives them a couple of frames to complete
    // during this time we can already go to the next sample etc.
    // now we are about to capture again and therefore need to make sure the images are stored

    WaitForRenderThreadReadSurfaceAndWriteImages();

    FMTSample Sample = CollectSampleMetadata();

    FString ImageDir = Sample.ImageDir.IsSet() ? Sample.ImageDir.GetValue() : GetImageDir();

    FString ImageName =
        Sample.ImageName.IsSet() ? Sample.ImageName.GetValue() : CreateImageNameFromSample(Sample);

    const auto AbsoluteImageFilePath = FPaths::Combine(ImageDir, ImageName);

    // Assume we are resuming previous run and don't overwrite image or metadata
    if (FPaths::FileExists(AbsoluteImageFilePath))
    {
        GotoNextSampleStep(ENextSampleStep::FindNextSampleLocation);
        return;
    }

    Sample.ArtifactProbability = SampleArtifactProbability;

    if (bCapturePanorama)
    {
        EnqueueCapture({PanoramaCapture, AbsoluteImageFilePath});
    }
    else
    {
        EnqueueCapture({Capture2D, AbsoluteImageFilePath});
    }

    if (!CaptureQueue.IsEmpty())
    {
        // Basically the same as in CaptureScene()
        // But we only call SendAllEndOfFrameUpdates once
        GetWorld()->SendAllEndOfFrameUpdates();
        for (const auto& CaptureData : CaptureQueue)
        {
            if (CaptureData.Capture->IsA<AMTSceneCaptureCube>())
            {
                const auto* CubeCapture = CastChecked<AMTSceneCaptureCube>(CaptureData.Capture);

                CubeCapture->GetCaptureComponentCube()->UpdateSceneCaptureContents(
                    GetWorld()->Scene);
            }
            else if (CaptureData.Capture->IsA<AMTSceneCapture>())
            {
                const auto* Capture = CastChecked<AMTSceneCapture>(CaptureData.Capture);

                Capture->GetCaptureComponent2D()->UpdateSceneCaptureContents(GetWorld()->Scene);
            }
        }

        ENQUEUE_RENDER_COMMAND(SceneDrawCompletion)
        (
            [this](FRHICommandListImmediate& RHICmdList)
            {
                TRACE_CPUPROFILER_EVENT_SCOPE(MTSampling::ReadSurface);

                for (const auto& CaptureData : CaptureQueue)
                {
                    if (CaptureData.Capture->IsA<AMTSceneCaptureCube>())
                    {
                        auto* CubeCapture = CastChecked<AMTSceneCaptureCube>(CaptureData.Capture);

                        FIntPoint SizeOUT;
                        EPixelFormat FormatOUT;
                        CubemapHelpers::GenerateLongLatUnwrap(
                            CubeCapture->GetCaptureComponentCube()->TextureTarget,
                            CubeCapture->GetMutableImageDataRef(),
                            SizeOUT,
                            FormatOUT,
                            CubeCapture->GetRenderTargetLongLat(),
                            RHICmdList);
                    }
                    else if (CaptureData.Capture->IsA<AMTSceneCapture>())
                    {
                        auto* Capture2D = CastChecked<AMTSceneCapture>(CaptureData.Capture);

                        const auto* Resource = Capture2D->GetCaptureComponent2D()
                                                   ->TextureTarget->GetRenderTargetResource();
                        Capture2D->GetMutableImageSize() = {(int32)Resource->GetSizeX(), (int32)Resource->GetSizeY()};
                        RHICmdList.ReadSurfaceData(
                            Resource->GetRenderTargetTexture(),
                            FIntRect(0, 0, Resource->GetSizeX(), Resource->GetSizeY()),
                            Capture2D->GetMutableImageDataRef(),
                            FReadSurfaceDataFlags(RCM_UNorm, CubeFace_MAX));
                    }
                }
            });

        CaptureFence.BeginFence();
    }

    // Call Capture function on remaining samples
    GotoNextSampleStep(ENextSampleStep::FindNextSampleLocation);
}

void UMTSamplerComponentBase::GotoNextSampleStep(
    const ENextSampleStep NextStep,
    const int32 StepWaitFrames)
{
    NextSampleStep = NextStep;
    StepFrameSkips = StepWaitFrames;
}

void UMTSamplerComponentBase::EnqueueCapture(const FMTCaptureImagePathPair& CaptureImagePathPair)
{
    CaptureQueue.Add(CaptureImagePathPair);
}

void UMTSamplerComponentBase::UpdateCesiumCameras()
{
    auto* CameraManager = ACesiumCameraManager::GetDefaultCameraManager(GetWorld());
    CameraManager->UpdateCamera(
        CesiumGroundLoaderCameraID,
        {{128, 128}, GetComponentLocation() + FVector(0, 0, 1000), {-90., 0., 0.}, 50.});

    for (int32 I = 0; I < CesiumPanoramaLoaderCameraIDs.Num(); ++I)
    {
        CameraManager->UpdateCamera(
            CesiumPanoramaLoaderCameraIDs[I],
            {{static_cast<double>(GetActiveConfig()->PanoramaWidth / 2.),
              GetActiveConfig()->PanoramaWidth / 2.},
             GetComponentLocation(),
             {0., I * 90., 0.},
             110.});
    }
}

FString UMTSamplerComponentBase::GetImageDir()
{
    return FPaths::Combine(GetSessionDir(), "Images");
}

void UMTSamplerComponentBase::WaitForRenderThreadReadSurfaceAndWriteImages()
{
    TRACE_CPUPROFILER_EVENT_SCOPE(MTSampling::WaitForReadSurface);

    for (auto& ImageWriteFuture : ImageWriteTaskFutures)
    {
        ImageWriteFuture.Wait();
    }

    ImageWriteTaskFutures.Reset();

    if (!CaptureQueue.IsEmpty())
    {
        // Allow other tasks to execute, we only care that out captures are not modified
        CaptureFence.Wait(false);

        for (const auto& CaptureData : CaptureQueue)
        {
            if (CaptureData.Capture->IsA<AMTSceneCaptureCube>())
            {
                auto* CubeCapture = CastChecked<AMTSceneCaptureCube>(CaptureData.Capture);
                auto ImageWriteFuture = UMTSamplingFunctionLibrary::WriteCubeMapPixelBufferToFile(
                    CaptureData.AbsoluteImagePath,
                    CubeCapture->GetMutableImageDataRef(),
                    {GetActiveConfig()->PanoramaWidth,
                     CubeCapture->GetCaptureComponentCube()->TextureTarget->SizeX},
                    {GetActiveConfig()->PanoramaTopCrop, GetActiveConfig()->PanoramaBottomCrop});
                ImageWriteTaskFutures.Emplace(MoveTemp(ImageWriteFuture));
            }
            else if (CaptureData.Capture->IsA<AMTSceneCapture>())
            {
                auto* Capture = CastChecked<AMTSceneCapture>(CaptureData.Capture);

                auto ImageWriteFuture = UMTSamplingFunctionLibrary::WritePixelBufferToFile(
                    CaptureData.AbsoluteImagePath,
                    Capture->GetMutableImageDataRef(),
                    Capture2D->GetMutableImageSize());
                ImageWriteTaskFutures.Emplace(MoveTemp(ImageWriteFuture));
            }
        }
    }

    CaptureQueue.Reset();
}

void UMTSamplerComponentBase::EndSampling()
{
    // Wait for last batch
    WaitForRenderThreadReadSurfaceAndWriteImages();

    PanoramaCapture->Destroy();
    Capture2D->Destroy();

    // TODO refactor into GetPriamryPlayerPawn
    const auto PlayerPawn = CastChecked<AMTPlayerPawn>(
        GetWorld()->GetGameInstance()->GetPrimaryPlayerController()->GetPawn());
    PlayerPawn->GetCaptureCameraComponent()->SetActive(false);
    PlayerPawn->GetOverviewCameraComponent()->SetActive(true);

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

TOptional<FTransform>
UMTSamplerComponentBase::ValidateGroundAndObstructions(const bool bIgnoreObstructions) const
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

    if (!bIgnoreObstructions)
    {
        TArray<FHitResult> Hits;

        constexpr auto TraceCount = 64;
        constexpr auto ClearanceDistance = 1000.;

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

            // DrawDebugLine(
            //     GetWorld(),
            //     UpdatedSampleLocation,
            //     UpdatedSampleLocation +
            //         FRotator(0., TraceIndex * DegreeInterval, 0.).Vector() * ClearanceDistance,
            //     FColor::Orange,
            //     false,
            //     0.01,
            //     0,
            //     1);
            if (Hit.bBlockingHit)
            {
                //DrawDebugSphere(GetWorld(), Hit.Location, 50, 6, FColor::Red, false, 0.01, 0, 1);
                Hits.Add(Hit);
            }
        }

       // DrawDebugSphere(GetWorld(), UpdatedSampleLocation, 100, 12, FColor::Green, false, 0.01, 0, 1);

        // Calcualte poitn with clearance form all hits
        if (Hits.Num() > 0)
        {
            FVector Center = FVector::ZeroVector;
            for (const auto& Hit : Hits)
            {
                const auto HitToCam = (Hit.TraceStart - Hit.TraceEnd).GetSafeNormal();
                const auto ClearPoint = Hit.Location + HitToCam * ClearanceDistance;
                //DrawDebugSphere(GetWorld(), ClearPoint, 50, 6, FColor::Yellow, false, 0.01, 0, 1);
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


        //DrawDebugSphere(GetWorld(), UpdatedSampleLocation, 100, 12, FColor::Blue, false, 0.01, 0, 1);

        if (!GroundHit.bBlockingHit)
        {
            return {};
        }

        UpdatedSampleLocation = GroundHit.Location + GroundOffset;
    }

    return {FTransform(GetOwner()->GetActorRotation(), UpdatedSampleLocation)};
}

void UMTSamplerComponentBase::TickComponent(
    float DeltaTime,
    ELevelTick TickType,
    FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    if (StepFrameSkips > 0)
    {
        StepFrameSkips--;
        return;
    }

    if (!bIsSampling)
    {
        return;
    }

    switch (NextSampleStep)
    {
        case ENextSampleStep::InitSampling:
            // skip one frame for initialization
            InitSampling();
            break;
        case ENextSampleStep::FindNextSampleLocation:
            FindNextSampleLocation();
            break;
        case ENextSampleStep::PreCaptureSample:
            PreCapture();
            break;
        case ENextSampleStep::CaptureSample:
            CaptureSample();
            break;
        default:
            check(false);
    }
}
