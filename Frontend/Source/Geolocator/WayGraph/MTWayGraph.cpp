﻿// Fill out your copyright notice in the Description page of Project Settings.

#include "MTWayGraph.h"

double EMTWayToScale(const EMTWay& Way)
{
    constexpr auto NormalScale = 1.;
    switch (Way)
    {
        case Motorway:
            return NormalScale;
        case Trunk:
            return NormalScale / 2.;
        case Primary:
            return NormalScale / 2.;
        case Secondary:
            return NormalScale / 2.;
        case Tertiary:
            return NormalScale / 2.;
        case Unclassified:
            return NormalScale / 3.;
        case Residential:
            return NormalScale / 3.;
        case Motorway_Link:
            return NormalScale / 3.;
        case Trunk_Link:
            return NormalScale / 3.;
        case Primary_Link:
            return NormalScale / 3.;
        case Secondary_Link:
            return NormalScale / 3.;
        case Tertiary_Link:
            return NormalScale / 3.;
        case LivingStreet:
            return NormalScale / 3;
        case Pedestrian:
            return NormalScale / 3.;
        case Cycleway:
            return NormalScale / 3.5;
        case Footway:
            return NormalScale / 3.5;
        case Path:
            return NormalScale / 3.5;
        case Service:
            return NormalScale / 3.5;
        case Max:
        default:
            check(false);
            return 0.;
    }
}

int64 FMTWayGraph::NodePairToEdgeIndex(const int32 Node1, const int32 Node2) const
{
    if (Node1 < Node2)
    {
        return (int64)Node1 * (int64)NodeNum() + (int64)Node2;
    }
    else
    {
        return (int64)Node2 * (int64)NodeNum() + (int64)Node1;
    }
}

int32 FMTWayGraph::AddWay(const FString& Name, const EMTWay Kind)
{
    return Ways.Add({Name, Kind});
}

EMTWay FMTWayGraph::GetWayKind(const int32 WayIndex) const
{
    return Ways[WayIndex].Kind;
}

void FMTWayGraph::UpdateWayKind(const int32 WayIndex, const EMTWay Kind)
{
    Ways[WayIndex].Kind = Kind;
}

FString FMTWayGraph::GetWayName(const int32 WayIndex) const
{
    return Ways[WayIndex].Name;
}

int32 FMTWayGraph::AddNode(const FOverpassCoordinates& Coords)
{
    const int32 NodeID = Nodes.Add({Coords});
    AdjacencyList.AddDefaulted();
    return NodeID;
}

void FMTWayGraph::ConnectNodes(const int32 Node1, const int32 Node2, const int32 WayIndex)
{
    if (WayIndex >= Ways.Num())
    {
        check(WayIndex == Ways.Num());
        Ways.AddDefaulted();
    }
    
    AdjacencyList[Node1].Add(Node2);
    AdjacencyList[Node2].Add(Node1);

    EdgeData.Add(NodePairToEdgeIndex(Node1, Node2), {WayIndex});
}

int32 FMTWayGraph::GetEdgeWay(int64 EdgeIndex) const
{
    return EdgeData[EdgeIndex].WayIndex;
}

int32 FMTWayGraph::EdgeNum() const
{
    return AdjacencyList.Num();
}

int32 FMTWayGraph::WayNum() const
{
    return Ways.Num();
}

TConstArrayView<int32> FMTWayGraph::ViewNodesConnectedToNode(const int32 NodeIndex) const
{
    return AdjacencyList[NodeIndex];
}

FOverpassCoordinates FMTWayGraph::GetNodeLocation(const int32 NodeIndex) const
{
    return Nodes[NodeIndex].Coords;
}

FVector
FMTWayGraph::GetNodeLocationUnreal(const int32 NodeIndex, const ACesiumGeoreference* GeoRef) const
{
    const auto NodeLocation = GetNodeLocation(NodeIndex);
    return GeoRef->TransformLongitudeLatitudeHeightPositionToUnreal(
        FVector{NodeLocation.Lon, NodeLocation.Lat, GeoRef->GetOriginHeight()});
}

bool FMTWayGraph::AreNodesConnected(const int32 NodeIndex1, const int32 NodeIndex2) const
{
    check(AdjacencyList.IsValidIndex(NodeIndex1));
    if (AdjacencyList[NodeIndex1].Contains(NodeIndex2))
    {
        check(AdjacencyList[NodeIndex2].Contains(NodeIndex1));
    }
    return AdjacencyList[NodeIndex1].Contains(NodeIndex2);
}

int32 FMTWayGraph::NodeNum() const
{
    return Nodes.Num();
}

void DrawDebugStreetGraph(
    const UWorld* World,
    const FMTWayGraph& WayGraph,
    const ACesiumGeoreference* Georeference)
{
    for (int32 Node1 = 0; Node1 < WayGraph.NodeNum(); Node1++)
    {
        for (int32 Node2 = Node1 + 1; Node2 < WayGraph.NodeNum(); Node2++)
        {
            if (WayGraph.AreNodesConnected(Node1, Node2))
            {
                const auto EdgeWayIndex =
                    WayGraph.GetEdgeWay(WayGraph.NodePairToEdgeIndex(Node1, Node2));
                FRandomStream StreetRandom(EdgeWayIndex);
                const auto StreetColor = FColor(
                    StreetRandom.RandRange(0, 255),
                    StreetRandom.RandRange(0, 255),
                    StreetRandom.RandRange(0, 255));

                const auto EdgeStartVector = WayGraph.GetNodeLocationUnreal(Node1, Georeference);
                const auto EdgeEndVector = WayGraph.GetNodeLocationUnreal(Node2, Georeference);

                constexpr auto MaxThickness = 1024.F;

                DrawDebugLine(
                    World,
                    EdgeStartVector,
                    EdgeEndVector,
                    StreetColor,
                    true,
                    -1.F,
                    0,
                    EMTWayToScale(WayGraph.Ways[EdgeWayIndex].Kind) * MaxThickness);
            }
        }
    }
}
