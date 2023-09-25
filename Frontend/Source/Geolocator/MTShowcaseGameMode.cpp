// Fill out your copyright notice in the Description page of Project Settings.

#include "MTShowcaseGameMode.h"

#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "JsonDomBuilder.h"
#include "NNE.h"
#include "NNERuntimeCPU.h"

TUniquePtr<UE::NNE::IModelInstanceCPU> AMTShowcaseGameMode::CreateInferenceModelInstance()
{
    if (InferenceModelData.IsNull())
    {
        UE_LOG(
            LogTemp, Error, TEXT("LazyLoadedModelData is not set, please assign it in the editor"));
    }
    else
    {
        InferenceModelData.LoadSynchronous();
    }

    if (!InferenceModel.IsValid())
    {
        const TWeakInterfacePtr<INNERuntimeCPU> NNERuntime =
            UE::NNE::GetRuntime<INNERuntimeCPU>(FString("NNERuntimeORTCpu"));

        check(NNERuntime->CanCreateModelCPU(InferenceModelData.Get()));
        InferenceModel = NNERuntime->CreateModel(InferenceModelData.Get());
    }

    TUniquePtr<UE::NNE::IModelInstanceCPU> InferenceModelInstance =
        InferenceModel->CreateModelInstance();

    constexpr int32 BatchSize = 1;

    check(
        InferenceModelInstance->SetInputTensorShapes(
            {UE::NNE::FTensorShape::Make({BatchSize, 3, 512, 512})}) == 0);

    return InferenceModelInstance;
}

void AMTShowcaseGameMode::BeginPlay()
{
    Super::BeginPlay();
}