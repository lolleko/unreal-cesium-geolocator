// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CesiumCartographicPolygon.h"
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Geolocator/OSM/MTOverpassSchema.h"
#include "Geolocator/WayGraph/MTWayGraph.h"
#include "MTChinesePostMan.h"

#include "MTWayGraphVisualizer.generated.h"

UCLASS()
class GEOLOCATOR_API AMTWayGraphVisualizer : public AActor
{
    GENERATED_BODY()

public:
    AMTWayGraphVisualizer();

protected:
    virtual void BeginPlay() override;

private:
    // Should be a a mesh with Forward X Axis and Pivot at the beginning of X
    UPROPERTY(VisibleAnywhere)
    TObjectPtr<UInstancedStaticMeshComponent> ISMC;

    UPROPERTY(VisibleAnywhere)
    TObjectPtr<UInstancedStaticMeshComponent> BoundaryISMC;
    
    UPROPERTY(EditAnywhere)
    TObjectPtr<ACesiumCartographicPolygon> BoundingPolygon;

    UPROPERTY(EditAnywhere)
    bool bShouldShowEulerTour = false;

    UPROPERTY(EditAnywhere)
    bool bShouldShowBoundary = true;

    FMTOverPassQueryCompletionDelegate OverpassQueryCompletedDelegate;

    FMTWayGraph WayGraph;

    TArray<FMTWayGraphPath> PathsContainingAllEdges;

    int32 EulerAnimationCurrentPathIndex = 0;
    int32 EulerAnimationCurrentNodeIndex = 0;
    FVector EulerAnimationZOffset = FVector(0, 0, 0);

    
    UFUNCTION()
    void OverpassQueryCompleted(const FOverPassQueryResult& Result, const bool bSuccess);

    void ShowOverview();
        
    void ShowEulerTour();
    
    void ShowEulerTourAnimated();

    void ShowBoundary();

    void AddNextEulerTourMesh();
};
