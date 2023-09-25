// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Interfaces/IHttpRequest.h"
#include "MTGameModeBase.h"
#include "NNEModelData.h"
#include "NNERuntimeCPU.h"
#include "NNERuntimeRDG.h"

#include "MTShowcaseGameMode.generated.h"

/**
 * 
 */
UCLASS()
class GEOLOCATOR_API AMTShowcaseGameMode : public AMTGameModeBase
{
    GENERATED_BODY()

public:
    TUniquePtr<UE::NNE::IModelInstanceCPU> CreateInferenceModelInstance();

protected:
    virtual void BeginPlay() override;
    
private:
    UPROPERTY(EditAnywhere)
    TSoftObjectPtr<UNNEModelData> InferenceModelData;

    TUniquePtr<UE::NNE::IModelCPU> InferenceModel;
};
