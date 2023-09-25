// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"

#include "GameFramework/HUD.h"

#include "Geolocator/Sampler/MTSamplerComponentBase.h"

#include "MTHUD.generated.h"

UCLASS()
class GEOLOCATOR_API AMTHUD : public AHUD
{
	GENERATED_BODY()

public:
	AMTHUD();

    UFUNCTION(BlueprintCallable)
    TArray<UMTSamplerComponentBase*> GetAllSamplerComponents() const;
    
protected:
	virtual void BeginPlay() override;

private:
	UPROPERTY(EditDefaultsOnly)
	TSubclassOf<UUserWidget> MenuWidget;

    UPROPERTY()
    TArray<TObjectPtr<UMTSamplerComponentBase>> SamplerComponents;
};
