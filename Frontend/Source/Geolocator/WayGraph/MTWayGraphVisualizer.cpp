// Fill out your copyright notice in the Description page of Project Settings.

#include "MTWayGraphVisualizer.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "MTChinesePostMan.h"
#include "Geolocator/OSM/MTOverpassConverter.h"
#include "Geolocator/OSM/MTOverpassQuery.h"

AMTWayGraphVisualizer::AMTWayGraphVisualizer()
{
    PrimaryActorTick.bCanEverTick = true;

    RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

    ISMC = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("ISMC"));
    ISMC->SetupAttachment(RootComponent);
    ISMC->NumCustomDataFloats = 3;

    BoundaryISMC = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("BoundaryISMC"));
    BoundaryISMC->SetupAttachment(RootComponent);
    BoundaryISMC->NumCustomDataFloats = 3;

}

void AMTWayGraphVisualizer::BeginPlay()
{
    Super::BeginPlay();

    if (bShouldShowBoundary)
    {
        ShowBoundary();
    }
    
    OverpassQueryCompletedDelegate.BindUFunction(this, TEXT("OverpassQueryCompleted"));
    if (BoundingPolygon)
    {
        const auto OverpassQuery = MTOverpass::BuildQueryStringFromBoundingPolygon(BoundingPolygon);
        MTOverpass::AsyncQuery(OverpassQuery, OverpassQueryCompletedDelegate);
    }
}

void AMTWayGraphVisualizer::OverpassQueryCompleted(
    const FOverPassQueryResult& Result,
    const bool bSuccess)
{
    if (!bSuccess)
    {
        return;
    }

    WayGraph = MTOverpass::CreateStreetGraphFromQuery(Result, BoundingPolygon);

    if (bShouldShowEulerTour)
    {
        ShowEulerTourAnimated();
    }
    else
    {
        ShowOverview();
    }
}

void AMTWayGraphVisualizer::ShowOverview()
{
    const auto* Georeference = ACesiumGeoreference::GetDefaultGeoreference(GetWorld());

    const FBoxSphereBounds StaticMeshBounds = ISMC->GetStaticMesh()->GetBounds();
    const auto MeshSizeInX = StaticMeshBounds.BoxExtent.Dot(FVector::XAxisVector) * 1.95;

    TArray<int32> EdgeMeshInstanceIds;
    EdgeMeshInstanceIds.Reserve(WayGraph.EdgeNum());

    TArray<FTransform> EdgeMeshTransforms;
    EdgeMeshTransforms.Reserve(WayGraph.EdgeNum());

    TArray<FTransform> EdgeMeshPrevTransforms;
    EdgeMeshPrevTransforms.Reserve(WayGraph.EdgeNum());

    TArray<float> EdgeMeshCustomFloats;
    EdgeMeshPrevTransforms.Reserve(WayGraph.EdgeNum() * ISMC->NumCustomDataFloats);

    for (int32 Node1 = 0; Node1 < WayGraph.NodeNum(); Node1++)
    {
        for (int32 Node2 = Node1 + 1; Node2 < WayGraph.NodeNum(); Node2++)
        {
            if (WayGraph.AreNodesConnected(Node1, Node2))
            {
                const auto EdgeIndex = WayGraph.NodePairToEdgeIndex(Node1, Node2);
                const auto EdgeWayIndex = 0;  // WayGraph.GetEdgeWay(EdgeIndex);

                const auto EdgeStartVector = WayGraph.GetNodeLocationUnreal(Node1, Georeference);
                const auto EdgeEndVector = WayGraph.GetNodeLocationUnreal(Node2, Georeference);

                const auto EdgeSize = FVector::Dist(EdgeStartVector, EdgeEndVector);
                const auto EdgeRotation =
                    (EdgeEndVector - EdgeStartVector).GetSafeNormal().Rotation();

                const auto MeshScale = EdgeSize / MeshSizeInX;

                const auto Transform = FTransform(
                    EdgeRotation,
                    EdgeStartVector,
                    FVector(
                        MeshScale, EMTWayToScale(WayGraph.GetWayKind(EdgeWayIndex)) * 2.F, 1.F));
                const auto InstanceID = EdgeMeshTransforms.Add(Transform);
                EdgeMeshPrevTransforms.Add(Transform);

                EdgeMeshInstanceIds.Add(InstanceID);

                FRandomStream StreetRandom(
                    0 /*WayGraph.GetEdgeWay(InstanceIndexToEdgeIndex[InstanceIndex])*/);

                const auto Hue = StreetRandom.RandRange(0, 255);
                const auto StreetColor =
                    FColor(240, 159, 0)
                        .ReinterpretAsLinear();  // FLinearColor::MakeFromHSV8(Hue, 255, 255);

                EdgeMeshCustomFloats.Append({StreetColor.R, StreetColor.G, StreetColor.B});
            }
        }
    }

    ISMC->UpdateInstances(
        EdgeMeshInstanceIds,
        EdgeMeshTransforms,
        EdgeMeshPrevTransforms,
        ISMC->NumCustomDataFloats,
        EdgeMeshCustomFloats);

    ISMC->MarkRenderStateDirty();
}

void AMTWayGraphVisualizer::ShowEulerTour()
{
    const auto* Georeference = ACesiumGeoreference::GetDefaultGeoreference(GetWorld());

    PathsContainingAllEdges =
        FMTChinesePostMan::CalculatePathsThatContainAllEdges(WayGraph, Georeference);

    const FBoxSphereBounds StaticMeshBounds = ISMC->GetStaticMesh()->GetBounds();
    const auto MeshSizeInX = StaticMeshBounds.BoxExtent.Dot(FVector::XAxisVector) * 2.;

    TArray<int32> EdgeMeshInstanceIds;
    EdgeMeshInstanceIds.Reserve(WayGraph.EdgeNum());

    TArray<FTransform> EdgeMeshTransforms;
    EdgeMeshTransforms.Reserve(WayGraph.EdgeNum());

    TArray<FTransform> EdgeMeshPrevTransforms;
    EdgeMeshPrevTransforms.Reserve(WayGraph.EdgeNum());

    TArray<float> EdgeMeshCustomFloats;
    EdgeMeshPrevTransforms.Reserve(WayGraph.EdgeNum() * ISMC->NumCustomDataFloats);

    FVector ZOffset = FVector::ZeroVector;
    for (int32 PathIndex = 0; PathIndex < PathsContainingAllEdges.Num(); ++PathIndex)
    {
        for (int32 PathNodeIndex = 0; PathNodeIndex < PathsContainingAllEdges[PathIndex].Nodes.Num() - 1;
             ++PathNodeIndex)
        {
            const auto Node1 = PathsContainingAllEdges[PathIndex].Nodes[PathNodeIndex];
            const auto Node2 = PathsContainingAllEdges[PathIndex].Nodes[PathNodeIndex + 1];

            const auto EdgeIndex = WayGraph.NodePairToEdgeIndex(Node1, Node2);
            const auto EdgeWayIndex = 0;  // WayGraph.GetEdgeWay(EdgeIndex);

            const auto EdgeStartVector = WayGraph.GetNodeLocationUnreal(Node1, Georeference);
            const auto EdgeEndVector = WayGraph.GetNodeLocationUnreal(Node2, Georeference);

            const auto EdgeSize = FVector::Dist(EdgeStartVector, EdgeEndVector);
            const auto EdgeRotation = (EdgeEndVector - EdgeStartVector).GetSafeNormal().Rotation();

            const auto MeshScale = EdgeSize / MeshSizeInX;

            const auto Transform = FTransform(
                EdgeRotation,
                EdgeStartVector + ZOffset,
                FVector(MeshScale, EMTWayToScale(WayGraph.GetWayKind(EdgeWayIndex)) * 2.F, 1.F));
            const auto InstanceID = EdgeMeshTransforms.Add(Transform);
            EdgeMeshPrevTransforms.Add(Transform);

            EdgeMeshInstanceIds.Add(InstanceID);

            const auto ColorRange = FMath::GetRangePct(
                0.,
                static_cast<double>(PathsContainingAllEdges[PathIndex].Nodes.Num() - 1),
                static_cast<double>(PathNodeIndex));
            const auto StreetColor = FLinearColor(2.0f * ColorRange, 2.0f * (1 - ColorRange), 0);

            EdgeMeshCustomFloats.Append({StreetColor.R, StreetColor.G, StreetColor.B});

            ZOffset += FVector(0., 0., 2.);
        }
    }

    ISMC->UpdateInstances(
        EdgeMeshInstanceIds,
        EdgeMeshTransforms,
        EdgeMeshPrevTransforms,
        ISMC->NumCustomDataFloats,
        EdgeMeshCustomFloats);

    ISMC->MarkRenderStateDirty();
}

void AMTWayGraphVisualizer::ShowEulerTourAnimated()
{
    const auto* Georeference = ACesiumGeoreference::GetDefaultGeoreference(GetWorld());

    PathsContainingAllEdges =
        FMTChinesePostMan::CalculatePathsThatContainAllEdges(WayGraph, Georeference);

    AddNextEulerTourMesh();
}
void AMTWayGraphVisualizer::ShowBoundary()
{
    const FBoxSphereBounds StaticMeshBounds = ISMC->GetStaticMesh()->GetBounds();
    const auto MeshSizeInX = StaticMeshBounds.BoxExtent.Dot(FVector::XAxisVector) * 2.;

    
    // Create spline mesh components for BoundingPolygon->Polygon
    for (int32 SplineSegmentIndex = 0;
         SplineSegmentIndex < BoundingPolygon->Polygon->GetNumberOfSplineSegments();
         ++SplineSegmentIndex)
    {   
        const auto EdgeStartVector = BoundingPolygon->Polygon->GetLocationAtSplinePoint(SplineSegmentIndex, ESplineCoordinateSpace::World);
        const auto EdgeEndVector = BoundingPolygon->Polygon->GetLocationAtSplinePoint(SplineSegmentIndex + 1, ESplineCoordinateSpace::World);

        const auto EdgeSize = FVector::Dist(EdgeStartVector, EdgeEndVector);
        const auto EdgeRotation = (EdgeEndVector - EdgeStartVector).GetSafeNormal().Rotation();

        const auto MeshScale = EdgeSize / MeshSizeInX;

        const auto Transform = FTransform(
            EdgeRotation,
            EdgeStartVector,
            FVector(MeshScale, 8.F, 1.F));

        const auto StreetColor = FLinearColor::Blue;

        const int32 Instance = BoundaryISMC->AddInstance(Transform);
        BoundaryISMC->SetCustomData(Instance, {StreetColor.R, StreetColor.G, StreetColor.B});
    }

}

void AMTWayGraphVisualizer::AddNextEulerTourMesh()
{
    for (int32 I = 0; EulerAnimationCurrentNodeIndex <
                          PathsContainingAllEdges[EulerAnimationCurrentPathIndex].Nodes.Num() - 1 &&
                      I < 16;
         ++I)
    {
        const auto* Georeference = ACesiumGeoreference::GetDefaultGeoreference(GetWorld());

        const FBoxSphereBounds StaticMeshBounds = ISMC->GetStaticMesh()->GetBounds();
        const auto MeshSizeInX = StaticMeshBounds.BoxExtent.Dot(FVector::XAxisVector) * 2.;

        const auto Node1 =
            PathsContainingAllEdges[EulerAnimationCurrentPathIndex].Nodes[EulerAnimationCurrentNodeIndex];
        const auto Node2 = PathsContainingAllEdges[EulerAnimationCurrentPathIndex].Nodes
                                                  [EulerAnimationCurrentNodeIndex + 1];

        const auto EdgeIndex = WayGraph.NodePairToEdgeIndex(Node1, Node2);
        const auto EdgeWayIndex = 0;  // WayGraph.GetEdgeWay(EdgeIndex);

        const auto EdgeStartVector = WayGraph.GetNodeLocationUnreal(Node1, Georeference);
        const auto EdgeEndVector = WayGraph.GetNodeLocationUnreal(Node2, Georeference);

        const auto EdgeSize = FVector::Dist(EdgeStartVector, EdgeEndVector);
        const auto EdgeRotation = (EdgeEndVector - EdgeStartVector).GetSafeNormal().Rotation();

        const auto MeshScale = EdgeSize / MeshSizeInX;

        const auto Transform = FTransform(
            EdgeRotation,
            EdgeStartVector + EulerAnimationZOffset,
            FVector(MeshScale, EMTWayToScale(WayGraph.GetWayKind(EdgeWayIndex)) * 5.F, 1.F));

        const auto ColorRange = FMath::GetRangePct(
            0.,
            static_cast<double>(PathsContainingAllEdges[EulerAnimationCurrentPathIndex].Nodes.Num() - 1),
            static_cast<double>(EulerAnimationCurrentNodeIndex));
        const auto StreetColor = FLinearColor(2.0f * ColorRange, 2.0f * (1 - ColorRange), 0);

        const int32 Instance = ISMC->AddInstance(Transform);
        ISMC->SetCustomData(Instance, {StreetColor.R, StreetColor.G, StreetColor.B});

        EulerAnimationCurrentNodeIndex++;

        EulerAnimationZOffset += FVector(0., 0., 0.001);
    }

    if (EulerAnimationCurrentNodeIndex >=
        PathsContainingAllEdges[EulerAnimationCurrentPathIndex].Nodes.Num() - 1)
    {
        EulerAnimationCurrentNodeIndex = 0;
        EulerAnimationCurrentPathIndex++;
        if (EulerAnimationCurrentPathIndex < PathsContainingAllEdges.Num())
        {
            GetWorld()->GetTimerManager().SetTimerForNextTick(
                this, &AMTWayGraphVisualizer::AddNextEulerTourMesh);
        }
    }
    else
    {
        GetWorld()->GetTimerManager().SetTimerForNextTick(
            this, &AMTWayGraphVisualizer::AddNextEulerTourMesh);
    }
}