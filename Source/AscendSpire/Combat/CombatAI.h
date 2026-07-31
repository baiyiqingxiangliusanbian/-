#pragma once

#include "CoreMinimal.h"

class UCombatEngine;

/**
 * 战斗 AI 策略 —— 无头模拟与自动测试共用
 */
class ASCENDSPIRE_API FCombatAI
{
public:
	/** 贪心策略自动打完一场战斗（含可选丹药使用），返回是否胜利 */
	static bool SimulateBattle(UCombatEngine* Engine, int32 MaxTurns);

	/** 从手牌选牌（贪心），返回手牌下标，-1 = 结束回合 */
	static int32 ChooseCardToPlay(UCombatEngine* Engine);

	/** 选择攻击目标（HP 最低的存活敌人） */
	static int32 ChooseTarget(UCombatEngine* Engine);
};
