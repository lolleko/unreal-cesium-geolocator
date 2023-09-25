// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CanvasItem.h"
#include "CanvasTypes.h"
#include "CesiumGeoreference.h"
#include "CoreMinimal.h"
#include "CubemapUnwrapUtils.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/TextureRenderTargetCube.h"
#include "UObject/Object.h"

#include "MTSamplingFunctionLibrary.generated.h"

/**
 * 
 */
UCLASS()
class GEOLOCATOR_API UMTSamplingFunctionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	static const FRandomStream& GetRandomStream();

	static void SetRandomSeed(const int32 NewSeed);

	// Bias = 1/N to prefer vertical directions, upper part of hemisphere (e.g. Bias = 1/15)
	// Bias = N to prefer horizontal directions, lower part of hemisphere (e.g. Bias = 2)
	static FVector SampleUniformDirOnPositiveHemisphere(const float Bias = 1.F);
    
	static bool IsViewObstructed(const UWorld* World, const FTransform& SampleTransform, const float MinClearance, const float SweepSphereRadius);

    static const inline FDateTime SessionStartTime = FDateTime::Now();
    static const inline TCHAR* TimeFormat = TEXT("%Y%m%dT%H%M%S%sZ");

    static bool ReadPixelsFromRenderTarget(UTextureRenderTarget2D* RenderTarget2D, TArray<FColor>& PixelBuffer);
    
    static bool WritePixelBufferToFile(const FString& FilePath, const TArray<FColor>& PixelBuffer, const FIntVector2& Size);

    static bool WriteCubeMapPixelBufferToFile(const FString& FilePath, const TArray<FColor>& PixelBuffer, const FIntVector2& Size, const FIntVector2& TopAndBottomCrop);

    struct FLocationPathPair
    {
        FTransform Location;
        FString Path;
    };
    static TArray<FLocationPathPair> PanoramaLocationsFromCosPlaceCSV(const FString& FilePath, const ACesiumGeoreference* Georeference);

private:
	static FRandomStream RandomStream;
};

// copied from CubemapUnwrrapUtils
// modified to work on the RenderThread

namespace CubemapHelpers
{
	/**
     * Helper function to create an unwrapped 2D image of the cube map ( longitude/latitude )
     * This version takes explicitly passed properties of the source object, as the sources have
     * different APIs.
     * @param	TextureResource		Source FTextureResource object.
     * @param	AxisDimenion		axis length of the cube.
     * @param	SourcePixelFormat	pixel format of the source.
     * @param	BitsOUT				Raw bits of the 2D image bitmap.
     * @param	SizeXOUT			Filled with the X dimension of the output bitmap.
     * @param	SizeYOUT			Filled with the Y dimension of the output bitmap.
     * @return						true on success.
     * @param	FormatOUT			Filled with the pixel format of the output bitmap.
     */
    inline bool GenerateLongLatUnwrap(
        const FTextureResource* TextureResource,
        const uint32 AxisDimenion,
        const EPixelFormat SourcePixelFormat,
        TArray<FColor>& PixelsOUT,
        FIntPoint& SizeOUT,
        EPixelFormat& FormatOUT,
        UTextureRenderTarget2D* RenderTargetLongLatCache,
        FRHICommandListImmediate& RHICmdList)
    {
        TRefCountPtr<FBatchedElementParameters> BatchedElementParameters;
        BatchedElementParameters = new FMipLevelBatchedElementParameters(
            (float)0, (float)-1, false, FMatrix44f::Identity, true, true, false);
        const FIntPoint LongLatDimensions(AxisDimenion * 2, AxisDimenion);

        // If the source format is 8 bit per channel or less then select a LDR target format.
        const EPixelFormat TargetPixelFormat =
            CalculateImageBytes(1, 1, 0, SourcePixelFormat) <= 4 ? PF_B8G8R8A8 : PF_FloatRGBA;
        
        FTextureRenderTargetResource* RenderTarget = RenderTargetLongLatCache->GetRenderTargetResource();

        FCanvas* Canvas = new FCanvas(RenderTarget, NULL, FGameTime(), GMaxRHIFeatureLevel);

        // Clear the render target to black
        Canvas->Clear(FLinearColor(0, 0, 0, 0));

        FCanvasTileItem TileItem(
            FVector2D(0.0f, 0.0f),
            TextureResource,
            FVector2D(LongLatDimensions.X, LongLatDimensions.Y),
            FLinearColor::White);
        TileItem.BatchedElementParameters = BatchedElementParameters;
        TileItem.BlendMode = SE_BLEND_Opaque;
        Canvas->DrawItem(TileItem);

        Canvas->Flush_RenderThread(RHICmdList);
        
        PixelsOUT.AddUninitialized(LongLatDimensions.X * LongLatDimensions.Y);

        switch (TargetPixelFormat)
        {
            case PF_B8G8R8A8:
                RHICmdList.ReadSurfaceData(
                    RenderTarget->GetRenderTargetTexture(),
                    FIntRect(0, 0, RenderTarget->GetSizeX(), RenderTarget->GetSizeY()),
                    PixelsOUT,
                    FReadSurfaceDataFlags(RCM_UNorm, CubeFace_MAX));
                break;
            case PF_FloatRGBA:
            {
                check(false)
            }
            break;
        }
        
        // Clean up.
        delete Canvas;

        SizeOUT = LongLatDimensions;
        FormatOUT = TargetPixelFormat;

        return true;
    }

    inline bool GenerateLongLatUnwrap(UTextureRenderTargetCube* CubeTarget, TArray<FColor>& PixelsOUT, FIntPoint& SizeOUT, EPixelFormat& FormatOUT, UTextureRenderTarget2D* RenderTargetLongLatCache, FRHICommandListImmediate& RHICmdList)
	{
		check(CubeTarget != NULL);
		return GenerateLongLatUnwrap(CubeTarget->GetRenderTargetResource(), CubeTarget->SizeX, CubeTarget->GetFormat(), PixelsOUT, SizeOUT, FormatOUT, RenderTargetLongLatCache, RHICmdList);
	}
}
