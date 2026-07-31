#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "Map/MapTypes.h"
#include "RunManager.h"
#include "RunSimCommandlet.generated.h"

/**
 * 完整 Run 模拟 Commandlet —— 无头跑整局游戏（地图/战斗/奖励/事件/坊市/Boss）
 * 用法: UnrealEditor-Cmd AscendSpire.uproject -run=AscendRunSim -runs=20 [-seed=1000] [-verbose] -unattended -nullrhi -stdout
 */
UCLASS()
class UAscendRunSimCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	virtual int32 Main(const FString& Params) override;

private:
	/** 存档/读档回环测试 */
	int32 RunSaveTest();
	/** 模拟一整局，返回是否通关 */
	bool SimulateRun(URunManager* Run, int32 Seed, bool bVerbose);

	/** 节点选择策略 */
	int32 ChooseNode(URunManager* Run, const TArray<FMysteryChoice>& Available) const;

	/** 跑一场节点战斗，返回是否胜利 */
	bool RunNodeCombat(URunManager* Run, const FNodeEncounter& Enc, int32 Seed, bool bVerbose);

	/** 选择奖励卡牌下标（-1 跳过） */
	int32 ChooseRewardCard(URunManager* Run, const TArray<FDeckCard>& CardChoices) const;
};
