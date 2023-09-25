// Fill out your copyright notice in the Description page of Project Settings.


#include "MTHUD.h"

#include "Algo/Copy.h"

#include "Blueprint/UserWidget.h"


AMTHUD::AMTHUD()
{
	PrimaryActorTick.bCanEverTick = true;
}

TArray<UMTSamplerComponentBase*> AMTHUD::GetAllSamplerComponents() const
{
    return SamplerComponents;
}

void AMTHUD::BeginPlay()
{
	Super::BeginPlay();
    Algo::Copy(TObjectRange<UMTSamplerComponentBase>(), SamplerComponents);

	auto* Menu = CreateWidget(GetWorld(), MenuWidget);
	
	Menu->AddToViewport();

}


