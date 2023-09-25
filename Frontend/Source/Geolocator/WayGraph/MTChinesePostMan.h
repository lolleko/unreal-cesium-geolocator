// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "MTWayGraph.h"

/**
 *
 */
class GEOLOCATOR_API FMTChinesePostMan
{
public:
    static TArray<TArray<int32>>
    CalculatePathsThatContainAllEdges(const FMTWayGraph& Graph, const ACesiumGeoreference* GeoRef);
};