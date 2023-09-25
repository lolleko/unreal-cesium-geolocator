// Fill out your copyright notice in the Description page of Project Settings.

#include "MTSamplingFunctionLibrary.h"

#include "CesiumGeoreference.h"
#include "CubemapUnwrapUtils.h"
#include "Engine/TextureRenderTarget2D.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"

FRandomStream UMTSamplingFunctionLibrary::RandomStream;

const FRandomStream& UMTSamplingFunctionLibrary::GetRandomStream()
{
    return RandomStream;
}

void UMTSamplingFunctionLibrary::SetRandomSeed(const int32 NewSeed)
{
    RandomStream.Initialize(NewSeed);
}

FVector UMTSamplingFunctionLibrary::SampleUniformDirOnPositiveHemisphere(const float Bias)
{
    // uniform random direction in positive Unit hemisphere
    const auto RotationXi1 = GetRandomStream().FRand();
    const auto RotationXi2 = GetRandomStream().FRand();

    // For full hemisphere we would use 2*Xi1
    const auto Theta = FMath::Acos(FMath::Pow(1. - RotationXi1, Bias));
    const auto Phi = 2. * PI * RotationXi2;

    double ThetaSin;
    double ThetaCos;
    FMath::SinCos(&ThetaSin, &ThetaCos, Theta);

    double PhiSin;
    double PhiCos;
    FMath::SinCos(&PhiSin, &PhiCos, Phi);

    return FVector(ThetaSin * PhiCos, ThetaSin * PhiSin, ThetaCos);
}

bool UMTSamplingFunctionLibrary::IsViewObstructed(
    const UWorld* World,
    const FTransform& SampleTransform,
    const float MinClearance,
    const float SweepSphereRadius)
{
    FHitResult OutHit;
    World->SweepSingleByChannel(
        OutHit,
        SampleTransform.GetLocation(),
        SampleTransform.GetLocation() + SampleTransform.Rotator().Vector() * UE_FLOAT_HUGE_DISTANCE,
        FQuat::Identity,
        ECC_Visibility,
        FCollisionShape::MakeSphere(SweepSphereRadius));

    if (OutHit.bStartPenetrating)
    {
        return true;
    }

    if (OutHit.Distance < MinClearance)
    {
        return true;
    }

    if (!OutHit.bBlockingHit)
    {
        return true;
    }

    return false;
}

bool UMTSamplingFunctionLibrary::ReadPixelsFromRenderTarget(
    UTextureRenderTarget2D* RenderTarget2D,
    TArray<FColor>& PixelBuffer)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(UMTSamplingFunctionLibrary::ReadPixelsFromRenderTarget);

    FTextureRenderTargetResource* RenderTargetResource =
        RenderTarget2D->GameThread_GetRenderTargetResource();

    FReadSurfaceDataFlags ReadSurfaceDataFlags(RCM_UNorm);
    // ReadSurfaceDataFlags.SetLinearToGamma(true);

    PixelBuffer.SetNum(RenderTarget2D->SizeX * RenderTarget2D->SizeY);
    ensure(RenderTargetResource->ReadPixelsPtr(PixelBuffer.GetData(), ReadSurfaceDataFlags));

    return true;
}

bool UMTSamplingFunctionLibrary::WritePixelBufferToFile(
    const FString& FilePath,
    const TArray<FColor>& PixelBuffer,
    const FIntVector2& Size)
{
    AsyncTask(
        ENamedThreads::AnyBackgroundThreadNormalTask,
        [PixelBufferCopy = PixelBuffer, FilePath, Size]()
        {
            IImageWrapperModule& ImageWrapperModule =
                FModuleManager::GetModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));

            TArray64<uint8> ImgData;
            ImageWrapperModule.CompressImage(
                ImgData,
                EImageFormat::JPEG,
                FImageView(
                    (uint8*)(PixelBufferCopy.GetData()),
                    Size.X,
                    Size.Y,
                    ERawImageFormat::BGRA8),
                85);
            FFileHelper::SaveArrayToFile(ImgData, *FilePath);
        });

    return true;
}
bool UMTSamplingFunctionLibrary::WriteCubeMapPixelBufferToFile(
    const FString& FilePath,
    const TArray<FColor>& PixelBuffer,
    const FIntVector2& Size,
    const FIntVector2& TopAndBottomCrop)
{
    AsyncTask(
        ENamedThreads::AnyBackgroundThreadNormalTask,
        [PixelBufferCopy = PixelBuffer, FilePath, Size, TopAndBottomCrop]()
        {
            IImageWrapperModule& ImageWrapperModule =
                FModuleManager::GetModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));

            TArray64<uint8> ImgData;
            ImageWrapperModule.CompressImage(
                ImgData,
                EImageFormat::JPEG,
                FImageView(
                    (uint8*)(PixelBufferCopy.GetData() + TopAndBottomCrop.X * Size.X),
                    Size.X,
                    Size.Y - TopAndBottomCrop.X - TopAndBottomCrop.Y,
                    ERawImageFormat::BGRA8),
                85);
            FFileHelper::SaveArrayToFile(ImgData, *FilePath);
        });

    return true;
}

TArray<UMTSamplingFunctionLibrary::FLocationPathPair>
UMTSamplingFunctionLibrary::PanoramaLocationsFromCosPlaceCSV(const FString& FilePath, const ACesiumGeoreference* Georeference)
{
    TArray<FString> CSVLines;
    FFileHelper::LoadFileToStringArray(CSVLines, *FilePath);

    TArray<FLocationPathPair> Result;

    for (const auto& Line : MakeArrayView(CSVLines).Slice(1, CSVLines.Num() - 1))
    {
        TArray<FString> CSVRowEntries;
        Line.ParseIntoArray(CSVRowEntries, TEXT(","));
        // PATH,UTM_EAST,UTM_NORTH,UTM_ZONE_NUMBER,UTM_ZONE_LETTER,LAT,LON,PANO_ID,HEADING,YEAR,MONTH,ALTITUDE,PITCH,ROLL
        const auto Path = CSVRowEntries[0];
        const auto Lat = FCString::Atod(*CSVRowEntries[5]);
        const auto Lon = FCString::Atod(*CSVRowEntries[6]);
        const auto Alt = FCString::Atod(*CSVRowEntries[11]) - 28;
        const auto Heading = FCString::Atod(*CSVRowEntries[8]);
        const auto Pitch = FCString::Atod(*CSVRowEntries[12]);
        const auto Roll = FCString::Atod(*CSVRowEntries[13]);

        const FVector UnrealLocation = Georeference->TransformLongitudeLatitudeHeightPositionToUnreal(FVector{Lon, Lat, Alt});
        const FRotator HeadingRotation = FRotator{Pitch - 90, Heading, Roll};
        const FRotator UnrealRotationOnlyHeading = Georeference->TransformEastSouthUpRotatorToUnreal(HeadingRotation, UnrealLocation);
        const FRotator UnrealRotation = FRotator{0, UnrealRotationOnlyHeading.Yaw, 0};

        const FTransform Transform{UnrealRotation, UnrealLocation};

        Result.Add({Transform, Path});
        
    }

    return Result;
}
