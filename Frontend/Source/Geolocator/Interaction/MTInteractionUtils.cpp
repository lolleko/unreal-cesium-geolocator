// Fill out your copyright notice in the Description page of Project Settings.

#include "MTInteractionUtils.h"

#include "Cesium3DTileset.h"
#include "CesiumGeoreference.h"
#include "CesiumSunSky.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/Texture2DDynamic.h"
#include "Engine/TextureRenderTarget2D.h"
#include "EngineUtils.h"
#include "HttpModule.h"
#include "ImageUtils.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "JsonDomBuilder.h"
#include "Geolocator/MTShowcaseGameMode.h"

// FString UMTInteractionFunctionLibrary::SelectFileFromFileDialog()
// {
//     IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
//
//     TArray<FString> path;
//
//     FString FilePath;
//     TArray<FString> OutFiles;
//
//     TSharedPtr<SWindow> ParentWindow = FGlobalTabmanager::Get()->GetRootWindow();
//
//     if (DesktopPlatform->OpenFileDialog(
//             FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr),
//             TEXT("Select Query Image..."),
//             TEXT(""),
//             TEXT(""),
//             ImageWrapperHelper::GetImageFilesFilterString(false).GetData(),
//             EFileDialogFlags::None,
//             OutFiles))
//     {
//         FilePath = OutFiles[0];
//     }
//
//     return FilePath;
// }

UTexture2D* UMTInteractionFunctionLibrary::LoadImageFromFile(const FString& FilePath)
{
    return FImageUtils::ImportFileAsTexture2D(FilePath);
}
UTexture2DDynamic* UMTInteractionFunctionLibrary::CreateTexture2DDynamic()
{
    return UTexture2DDynamic::Create(512, 512);
}

FString UMTInteractionFunctionLibrary::GetBaseDir()
{
    return FPlatformProcess::BaseDir();
}

TArray<FMTSample> UMTInteractionFunctionLibrary::ResultsToSampleArray(const FString& Results)
{
    TArray<FMTSample> Samples;

    FJsonObjectWrapper Wrapper;
    Wrapper.JsonObjectFromString(Results);

    const auto ResultsArr = Wrapper.JsonObject->GetArrayField(TEXT("result"));
    for (const auto& Result : ResultsArr)
    {
        const auto PayloadObject = Result->AsObject()->GetObjectField("payload");

        FMTSample Sample;

        Sample.HeadingAngle = PayloadObject->GetNumberField(TEXT("HeadingAngle")) - 75;
        auto LonLat = PayloadObject->GetObjectField(TEXT("LonLat"));

        Sample.LonLatAltitude = FVector(
            LonLat->GetNumberField(TEXT("lon")),
            LonLat->GetNumberField(TEXT("lat")),
            PayloadObject->GetNumberField(TEXT("Altitude")));

        Sample.Score = Result->AsObject()->GetNumberField(TEXT("score"));

        Samples.Add(Sample);
    }

    return Samples;
}

FString UMTInteractionFunctionLibrary::EncodeCHWImageBase64(const TArray<float>& ImagePixels)
{
    return FBase64::Encode((uint8*)ImagePixels.GetData(), ImagePixels.Num() * sizeof(float));
}

void UMTInteractionFunctionLibrary::PrepareTilesestForThumbnailCapture(UObject* WorldContextObject)
{
    for (auto* Tileset : TActorRange<ACesium3DTileset>(WorldContextObject->GetWorld()))
    {
        Tileset->MaximumScreenSpaceError *= 2.;
        Tileset->SetShouldIgnorePlayerCamera(true);
        Tileset->PlayMovieSequencer();
    }
}

void UMTInteractionFunctionLibrary::ResetTilesetsAfterThumbnailCapture(UObject* WorldContextObject)
{
    for (auto* Tileset : TActorRange<ACesium3DTileset>(WorldContextObject->GetWorld()))
    {
        Tileset->MaximumScreenSpaceError /= 2.;
        Tileset->SetShouldIgnorePlayerCamera(false);
        Tileset->StopMovieSequencer();
    }
}

void UMTImageLoadAsyncAction::Activate()
{
    AsyncTask(
        ENamedThreads::AnyBackgroundThreadNormalTask,
        [this]()
        {
            TArray64<uint8> Buffer;
            if (FFileHelper::LoadFileToArray(Buffer, *FilePath))
            {
                FImage Image;
                if (FImageUtils::DecompressImage(Buffer.GetData(), Buffer.Num(), Image))
                {
                    FImage ImageForInference;

                    Image.ResizeTo(
                        ImageForInference, 512, 512, ERawImageFormat::BGRA8, EGammaSpace::sRGB);

                    TArray<float> ImageForInferenceNormalized;
                    ImageForInferenceNormalized.SetNumUninitialized(
                        ImageForInference.GetNumPixels() * 3);

                    // HWC -> CHW
                    for (int32 X = 0; X < ImageForInference.GetWidth(); ++X)
                    {
                        for (int32 Y = 0; Y < ImageForInference.GetHeight(); ++Y)
                        {
                            FLinearColor LinearPixel = ImageForInference.GetOnePixelLinear(X, Y);
                            LinearPixel.R = (LinearPixel.R - 0.485F) / 0.229F;
                            LinearPixel.G = (LinearPixel.G - 0.456F) / 0.224F;
                            LinearPixel.B = (LinearPixel.B - 0.406F) / 0.225F;

                            ImageForInferenceNormalized[0 * (512 * 512) + Y * 512 + X] =
                                LinearPixel.R;
                            ImageForInferenceNormalized[1 * (512 * 512) + Y * 512 + X] =
                                LinearPixel.G;
                            ImageForInferenceNormalized[2 * (512 * 512) + Y * 512 + X] =
                                LinearPixel.B;
                        }
                    }

                    AsyncTask(
                        ENamedThreads::GameThread,
                        [this, Image = MoveTemp(Image), ImageForInferenceNormalized]() mutable
                        {
                            TargetImageTexture->Init(
                                Image.GetWidth(), Image.GetHeight(), PF_B8G8R8A8);

                            FTexture2DDynamicResource* TextureResource =
                                static_cast<FTexture2DDynamicResource*>(
                                    TargetImageTexture->GetResource());
                            if (TextureResource)
                            {
                                ENQUEUE_RENDER_COMMAND(FWriteRawDataToTexture)
                                (
                                    [this,
                                     TextureResource,
                                     RawData = MoveTemp(Image.RawData),
                                     Width = Image.GetWidth(),
                                     Height = Image.GetHeight(),
                                     ImageForInferenceNormalized](
                                        FRHICommandListImmediate& RHICmdList)
                                    {
                                        TextureResource->WriteRawToTexture_RenderThread(RawData);
                                        AsyncTask(
                                            ENamedThreads::GameThread,
                                            [this, Width, Height, ImageForInferenceNormalized]() {
                                                Completed.Broadcast(
                                                    Width,
                                                    Height,
                                                    ImageForInferenceNormalized,
                                                    true);
                                            });
                                    });
                            }
                            else
                            {
                                Completed.Broadcast(0, 0, {}, false);
                            }
                        });
                    return;  // success
                }
            }
            AsyncTask(
                ENamedThreads::GameThread, [this]() { Completed.Broadcast(0, 0, {}, false); });
        });
}

UMTImageLoadAsyncAction* UMTImageLoadAsyncAction::AsyncImageLoad(
    UObject* WorldContextObject,
    const FString& FilePath,
    UTexture2DDynamic* TargetImageTexture)
{
    auto* Action = NewObject<UMTImageLoadAsyncAction>();
    Action->FilePath = FilePath;
    Action->TargetImageTexture = TargetImageTexture;
    Action->RegisterWithGameInstance(WorldContextObject);

    return Action;
}

void UMTCapturePreviewImageAction::Activate()
{
    AsyncTask(
        ENamedThreads::GameThread,
        [this]()
        {
            CaptureActor = WorldContextObject->GetWorld()->SpawnActor<AMTSceneCapture>();
            CaptureActor->GetCaptureComponent2D()->TextureTarget =
                NewObject<UTextureRenderTarget2D>(CaptureActor->GetCaptureComponent2D());
            CaptureActor->GetCaptureComponent2D()->TextureTarget->CompressionSettings =
                TextureCompressionSettings::TC_Default;
            CaptureActor->GetCaptureComponent2D()->TextureTarget->TargetGamma = 2.2F;
            CaptureActor->GetCaptureComponent2D()->TextureTarget->bAutoGenerateMips = false;
            CaptureActor->GetCaptureComponent2D()->TextureTarget->CompressionSettings =
                TextureCompressionSettings::TC_Default;
            CaptureActor->GetCaptureComponent2D()->TextureTarget->AddressX =
                TextureAddress::TA_Clamp;
            CaptureActor->GetCaptureComponent2D()->TextureTarget->AddressY =
                TextureAddress::TA_Clamp;
            CaptureActor->GetCaptureComponent2D()->TextureTarget->RenderTargetFormat = RTF_RGBA8;
            CaptureActor->GetCaptureComponent2D()->TextureTarget->bGPUSharedFlag = true;
            CaptureActor->GetCaptureComponent2D()->TextureTarget->InitCustomFormat(
                512, 512, EPixelFormat::PF_B8G8R8A8, true);
            TargetImageTexture->Init(512, 512, PF_B8G8R8A8);

            auto* GeoRef =
                ACesiumGeoreference::GetDefaultGeoreference(WorldContextObject->GetWorld());

            GeoRef->SetOriginLongitudeLatitudeHeight(LongLatHeight);
            TActorIterator<ACesiumSunSky>(WorldContextObject->GetWorld())->UpdateSun();

            auto Location = GeoRef->TransformLongitudeLatitudeHeightPositionToUnreal(LongLatHeight);
            FRotator Rotation = GeoRef->TransformEastSouthUpRotatorToUnreal(
                FRotator(0, YawAtLocation, 0), Location);
            Rotation.Pitch = PitchAtLocation;

            CaptureActor->SetActorLocation(Location);
            CaptureActor->SetActorRotation(Rotation);

            WorldContextObject->GetWorld()
                ->GetFirstPlayerController()
                ->GetPawnOrSpectator()
                ->SetActorLocation(Location);
            WorldContextObject->GetWorld()
                ->GetFirstPlayerController()
                ->GetPawnOrSpectator()
                ->SetActorRotation(Rotation);
            WorldContextObject->GetWorld()->GetFirstPlayerController()->SetControlRotation(
                Rotation);

            WorldContextObject->GetWorld()->GetTimerManager().SetTimerForNextTick(
                [this]()
                {
                    FTimerHandle TimerHandle;
                    WorldContextObject->GetWorld()->GetTimerManager().SetTimer(
                        TimerHandle,
                        [this]()
                        {
                            WorldContextObject->GetWorld()->SendAllEndOfFrameUpdates();

                            CaptureActor->GetCaptureComponent2D()->UpdateSceneCaptureContents(
                                WorldContextObject->GetWorld()->Scene);

                            ENQUEUE_RENDER_COMMAND(FREadSurfaceDataAndWriteToDyanmicTexture)
                            (
                                [this](FRHICommandListImmediate& RHICmdList)
                                {
                                    TRACE_CPUPROFILER_EVENT_SCOPE(MTSampling::ReadSurface);

                                    const auto* Resource =
                                        CaptureActor->GetCaptureComponent2D()
                                            ->TextureTarget->GetRenderTargetResource();
                                    RHICmdList.ReadSurfaceData(
                                        Resource->GetRenderTargetTexture(),
                                        FIntRect(0, 0, Resource->GetSizeX(), Resource->GetSizeY()),
                                        CaptureActor->GetMutableImageDataRef(),
                                        FReadSurfaceDataFlags(RCM_UNorm, CubeFace_MAX));

                                    FTexture2DDynamicResource* TextureResource =
                                        static_cast<FTexture2DDynamicResource*>(
                                            TargetImageTexture->GetResource());
                                    if (TextureResource)
                                    {
                                        const auto View = TArrayView64<uint8>(
                                            (uint8*)CaptureActor->GetMutableImageDataRef()
                                                .GetData(),
                                            512 * 512 * 4);
                                        TextureResource->WriteRawToTexture_RenderThread(View);
                                        AsyncTask(
                                            ENamedThreads::GameThread,
                                            [this]()
                                            {
                                                Completed.Broadcast(true);
                                                CaptureActor->Destroy();
                                            });
                                    }
                                    else
                                    {
                                        check(false);
                                        Completed.Broadcast(false);
                                    }
                                });
                        },
                        0.1,
                        false);
                });
        });
}

UMTCapturePreviewImageAction* UMTCapturePreviewImageAction::CapturePreviewImage(
    UObject* WorldContextObject,
    const FVector& LongLatHeight,
    double YawAtLocation,
    double PitchAtLocation,
    UTexture2DDynamic* TargetImageTexture)
{
    auto* Action = NewObject<UMTCapturePreviewImageAction>();
    Action->LongLatHeight = LongLatHeight;
    Action->YawAtLocation = YawAtLocation;
    Action->PitchAtLocation = PitchAtLocation;
    Action->TargetImageTexture = TargetImageTexture;
    Action->WorldContextObject = WorldContextObject;

    Action->RegisterWithGameInstance(WorldContextObject);

    return Action;
}

void UMTHttpJsonRequestAsyncAction::Activate()
{
    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> HttpRequest = FHttpModule::Get().CreateRequest();
    HttpRequest->SetVerb(Verb);
    HttpRequest->SetHeader("Content-Type", "application/json");
    HttpRequest->SetURL(URL);
    HttpRequest->SetContentAsString(Body);

    HttpRequest->OnProcessRequestComplete().BindLambda(
        [this](FHttpRequestPtr Request, FHttpResponsePtr Response, bool bSuccess)
        {
            FString ResponseString = "";
            if (bSuccess)
            {
                ResponseString = Response->GetContentAsString();
            }

            this->HandleRequestCompleted(ResponseString, bSuccess);
        });

    HttpRequest->ProcessRequest();
}

UMTHttpJsonRequestAsyncAction* UMTHttpJsonRequestAsyncAction::AsyncJSONRequestHTTP(
    UObject* WorldContextObject,
    const FString& URL,
    const FString& Verb,
    const FString& Body)
{
    // Create Action Instance for Blueprint System
    auto* Action = NewObject<UMTHttpJsonRequestAsyncAction>();
    Action->URL = URL;
    Action->Verb = Verb;
    Action->Body = Body;
    Action->RegisterWithGameInstance(WorldContextObject);

    return Action;
}

void UMTHttpJsonRequestAsyncAction::HandleRequestCompleted(
    const FString& ResponseString,
    bool bSuccess)
{
    Completed.Broadcast(ResponseString, bSuccess);
}

void UMTInferenceAsyncAction::Activate()
{
    const auto InputTensorShape = InferenceModelInstance->GetInputTensorShapes()[0];
    const auto OutputTensorShape =
        UE::NNE::FTensorShape::Make({InputTensorShape.GetData()[0], 512});
    // Example for creating in- and outputs
    InputData.SetNumZeroed(InputTensorShape.Volume());
    InputBindings.SetNumZeroed(1);
    InputBindings[0].Data = InputData.GetData();
    InputBindings[0].SizeInBytes = InputData.Num() * sizeof(float);

    OutputData.SetNumZeroed(OutputTensorShape.Volume());
    OutputBindings.SetNumZeroed(1);
    OutputBindings[0].Data = OutputData.GetData();
    OutputBindings[0].SizeInBytes = OutputData.Num() * sizeof(float);

    InputData = ImagePixels;
    check(InferenceModelInstance->RunSync(InputBindings, OutputBindings) == 0);

    auto Request = FHttpModule::Get().CreateRequest();
    Request->SetVerb(TEXT("POST"));
    Request->SetURL(TEXT("http://localhost:6333/collections/geo-location/points/search"));
    Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));

    FJsonDomBuilder::FArray Descriptor;
    for (const auto& Value : OutputData)
    {
        Descriptor.Add(Value);
    }

    FJsonDomBuilder::FObject JsonBuilder;
    JsonBuilder
        .Set(
            TEXT("filter"),
            FJsonDomBuilder::FObject().Set(
                "must",
                FJsonDomBuilder::FArray().Add(
                    FJsonDomBuilder::FObject()
                        .Set(TEXT("key"), TEXT("IsActive"))
                        .Set(TEXT("match"), FJsonDomBuilder::FObject().Set("value", true)))))
        .Set(TEXT("vector"), Descriptor)
        .Set(TEXT("with_payload"), true)
        .Set(TEXT("offset"), Offset)
        .Set(TEXT("limit"), Limit);

    const auto Content = JsonBuilder.ToString<>();

    Request->SetContentAsString(Content);

    Request->OnProcessRequestComplete().BindLambda(
        [this](FHttpRequestPtr Request, FHttpResponsePtr Response, bool bSuccess)
        {
            FString ResponseString = "";
            if (bSuccess)
            {
                ResponseString = Response->GetContentAsString();
            }

            this->HandleRequestCompleted(ResponseString, bSuccess);
        });

    Request->ProcessRequest();
}

UMTInferenceAsyncAction* UMTInferenceAsyncAction::AsyncInferenceRequest(
    UObject* WorldContextObject,
    const TArray<float>& Pixels,
    const int32 Offset,
    const int32 Limit)
{
    auto* Action = NewObject<UMTInferenceAsyncAction>();
    Action->ImagePixels = Pixels;
    Action->InferenceModelInstance =
        CastChecked<AMTShowcaseGameMode>(WorldContextObject->GetWorld()->GetAuthGameMode())
            ->CreateInferenceModelInstance();
    Action->Offset = Offset;
    Action->Limit = Limit;
    Action->RegisterWithGameInstance(WorldContextObject);

    return Action;
}

void UMTInferenceAsyncAction::HandleRequestCompleted(const FString& ResponseString, bool bSuccess)
{
    Completed.Broadcast(ResponseString, bSuccess);
}