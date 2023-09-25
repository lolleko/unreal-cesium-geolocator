// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CesiumGeoreference.h"
#include "CoreMinimal.h"
#include "Geolocator/OSM/MTOverpassSchema.h"

UENUM()
enum EMTWay
{
    Motorway,
    Trunk,
    Primary,
    Secondary,
    Tertiary,
    Unclassified,
    Residential,
    Motorway_Link,
    Trunk_Link,
    Primary_Link,
    Secondary_Link,
    Tertiary_Link,
    LivingStreet,
    Pedestrian,
    Cycleway,
    Footway,
    Path,
    Service,
    Max
};

double EMTWayToScale(const EMTWay& Way);

UENUM()
enum class EMTEdgeDirection
{
    Forward = 1,
    Reverse = -1
};

struct FMTWayGraph
{
    struct FNode
    {
        FOverpassCoordinates Coords;
    };

    struct FEdge
    {
        int32 WayIndex;
    };

    int64 NodePairToEdgeIndex(const int32 Node1, const int32 Node2) const;

    int32 AddWay(const FString& Name, const EMTWay Kind);

    EMTWay GetWayKind(const int32 WayIndex) const;

    void UpdateWayKind(const int32 WayIndex, const EMTWay Kind);

    FString GetWayName(const int32 WayIndex) const;

    int32 AddNode(const FOverpassCoordinates& Coords);

    void ConnectNodes(const int32 Node1, const int32 Node2, const int32 WayIndex);

    int32 GetEdgeWay(int64 EdgeIndex) const;

    int32 EdgeNum() const;

    int32 WayNum() const;

    TConstArrayView<int32> ViewNodesConnectedToNode(const int32 NodeIndex) const;

    FOverpassCoordinates GetNodeLocation(const int32 NodeIndex) const;

    FVector GetNodeLocationUnreal(const int32 NodeIndex, const ACesiumGeoreference* GeoRef) const;

    bool AreNodesConnected(const int32 NodeIndex1, const int32 NodeIndex2) const;

    int32 NodeNum() const;

private:
    TArray<FNode> Nodes;
    TArray<TArray<int32, TInlineAllocator<4>>> AdjacencyList;
    TMap<int64, FEdge> EdgeData;

    struct FMTWay
    {
        FString Name;
        EMTWay Kind;
    };
    TArray<FMTWay> Ways;

    friend void DrawDebugStreetGraph(
        const UWorld* World,
        const FMTWayGraph& WayGraph,
        const ACesiumGeoreference* Georeference);
};

void DrawDebugStreetGraph(
    const UWorld* World,
    const FMTWayGraph& WayGraph,
    const ACesiumGeoreference* Georeference);