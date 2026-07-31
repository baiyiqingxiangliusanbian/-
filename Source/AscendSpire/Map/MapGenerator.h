#pragma once

#include "CoreMinimal.h"
#include "MapTypes.h"
#include "MapGenerator.generated.h"

/**
 * 地图生成器 —— 生成7层 DAG 节点图（StS 风格）
 * 第0层: 战斗(起点)  第5层: 调息  第6层: Boss
 * 中间层按权重随机: 战斗/精英/坊市/事件
 */
UCLASS()
class ASCENDSPIRE_API UMapGenerator : public UObject
{
	GENERATED_BODY()

public:
	/** 生成地图（确定性种子） */
	static FMapData Generate(int32 Seed, int32 FloorCount = 7);

private:
	static EMapNodeType RollNodeType(int32 Floor, int32 FloorCount, FRandomStream& Rng);
};
