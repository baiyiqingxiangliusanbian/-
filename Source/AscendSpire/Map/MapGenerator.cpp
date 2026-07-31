#include "MapGenerator.h"

FMapData UMapGenerator::Generate(int32 Seed, int32 FloorCount)
{
	FMapData Map;
	Map.FloorCount = FloorCount;
	FRandomStream Rng(Seed);

	int32 NextId = 0;

	// 每层节点数: 第0层1个, Boss层1个, 调息层1个, 其余 2-3 个
	TArray<TArray<int32>> FloorNodeIds; // FloorNodeIds[f] = 该层节点ID列表
	FloorNodeIds.SetNum(FloorCount);

	for (int32 F = 0; F < FloorCount; ++F)
	{
		int32 Count;
		if (F == 0 || F >= FloorCount - 2) Count = 1;         // 起点/调息/Boss
		else Count = Rng.RandRange(2, 3);

		for (int32 i = 0; i < Count; ++i)
		{
			FMapNode Node;
			Node.NodeId = NextId++;
			Node.Floor = F;

			if (F == FloorCount - 1) Node.Type = EMapNodeType::Boss;
			else if (F == FloorCount - 2) Node.Type = EMapNodeType::Rest;
			else if (F == 0) Node.Type = EMapNodeType::Combat;
			else Node.Type = RollNodeType(F, FloorCount, Rng);

			FloorNodeIds[F].Add(Node.NodeId);
			Map.Nodes.Add(Node);
		}
	}

	// 连线: 每层节点连向下一层 1-2 个节点
	for (int32 F = 0; F < FloorCount - 1; ++F)
	{
		const TArray<int32>& Cur = FloorNodeIds[F];
		const TArray<int32>& Next = FloorNodeIds[F + 1];

		for (int32 FromId : Cur)
		{
			FMapNode* FromNode = Map.FindNode(FromId);
			if (!FromNode) continue;

			// 连 1-2 条边
			const int32 EdgeCount = FMath::Min(Next.Num(), Rng.RandRange(1, 2));
			for (int32 e = 0; e < EdgeCount; ++e)
			{
				const int32 ToId = Next[Rng.RandRange(0, Next.Num() - 1)];
				if (!FromNode->NextNodeIds.Contains(ToId))
				{
					FromNode->NextNodeIds.Add(ToId);
				}
			}
		}

		// 保证下一层每个节点至少有一条入边
		for (int32 ToId : Next)
		{
			bool bHasIncoming = false;
			for (int32 FromId : Cur)
			{
				if (Map.FindNode(FromId)->NextNodeIds.Contains(ToId)) { bHasIncoming = true; break; }
			}
			if (!bHasIncoming)
			{
				Map.FindNode(Cur[Rng.RandRange(0, Cur.Num() - 1)])->NextNodeIds.Add(ToId);
			}
		}
	}

	return Map;
}

EMapNodeType UMapGenerator::RollNodeType(int32 Floor, int32 FloorCount, FRandomStream& Rng)
{
	// 权重随层数变化: 前期多战斗，中后期精英/坊市/事件增多
	// 第1层不出精英（太弱），第4层（Boss前）多坊市/调息机会
	struct FWeight { EMapNodeType Type; int32 W; };
	TArray<FWeight> Weights;

	if (Floor == 1)
	{
		Weights = {{EMapNodeType::Combat, 70}, {EMapNodeType::Event, 30}};
	}
	else if (Floor == FloorCount - 3) // Boss前第二层
	{
		Weights = {{EMapNodeType::Combat, 30}, {EMapNodeType::Elite, 25},
			{EMapNodeType::Shop, 20}, {EMapNodeType::Event, 25}};
	}
	else
	{
		Weights = {{EMapNodeType::Combat, 45}, {EMapNodeType::Elite, 20},
			{EMapNodeType::Shop, 12}, {EMapNodeType::Event, 23}};
	}

	int32 Total = 0;
	for (const FWeight& W : Weights) Total += W.W;

	int32 Roll = Rng.RandRange(1, Total);
	for (const FWeight& W : Weights)
	{
		Roll -= W.W;
		if (Roll <= 0) return W.Type;
	}
	return EMapNodeType::Combat;
}
