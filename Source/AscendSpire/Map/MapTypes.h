#pragma once

#include "CoreMinimal.h"
#include "MapTypes.generated.h"

/** 节点类型 */
UENUM(BlueprintType)
enum class EMapNodeType : uint8
{
	Combat,     // 战斗
	Elite,      // 精英
	Rest,       // 打坐调息
	Shop,       // 坊市
	Event,      // 奇遇事件
	Boss        // Boss（剧情）
};

/** 地图节点 */
USTRUCT(BlueprintType)
struct FMapNode
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	int32 NodeId = -1;

	UPROPERTY(BlueprintReadOnly)
	EMapNodeType Type = EMapNodeType::Combat;

	/** 所在层（0 = 起点, 6 = Boss） */
	UPROPERTY(BlueprintReadOnly)
	int32 Floor = 0;

	/** 通往下层的节点ID列表 */
	UPROPERTY(BlueprintReadOnly)
	TArray<int32> NextNodeIds;

	/** 是否已访问 */
	UPROPERTY(BlueprintReadOnly)
	bool bVisited = false;
};

/** 一整章的地图 */
USTRUCT(BlueprintType)
struct FMapData
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	TArray<FMapNode> Nodes;

	/** 层数（含 Boss 层） */
	UPROPERTY(BlueprintReadOnly)
	int32 FloorCount = 7;

	const FMapNode* FindNode(int32 NodeId) const
	{
		return Nodes.FindByPredicate([NodeId](const FMapNode& N) { return N.NodeId == NodeId; });
	}

	FMapNode* FindNode(int32 NodeId)
	{
		return Nodes.FindByPredicate([NodeId](const FMapNode& N) { return N.NodeId == NodeId; });
	}
};
