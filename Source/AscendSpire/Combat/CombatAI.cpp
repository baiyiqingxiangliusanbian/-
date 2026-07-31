#include "CombatAI.h"
#include "CombatEngine.h"

bool FCombatAI::SimulateBattle(UCombatEngine* Engine, int32 MaxTurns)
{
	int32 SafetyCounter = 0;
	bool bUsedPills = false;

	while (!Engine->IsCombatOver() && Engine->TurnCount <= MaxTurns)
	{
		if (!bUsedPills)
		{
			bUsedPills = true;
			for (const FString& PillId : Engine->TestPills)
			{
				Engine->UsePill(PillId);
			}
		}

		int32 PlaysThisTurn = 0;
		while (PlaysThisTurn < 20)
		{
			const int32 CardIdx = ChooseCardToPlay(Engine);
			if (CardIdx < 0) break;
			if (!Engine->PlayCard(CardIdx, ChooseTarget(Engine))) break;
			PlaysThisTurn++;
			if (Engine->IsCombatOver()) break;
		}

		if (Engine->IsCombatOver()) break;
		Engine->EndPlayerTurn();

		if (++SafetyCounter > MaxTurns * 2) break;
	}

	return Engine->TurnCount <= MaxTurns && Engine->IsVictory();
}

int32 FCombatAI::ChooseCardToPlay(UCombatEngine* Engine)
{
	TArray<int32> Playable = Engine->GetPlayableCardIndices();
	if (Playable.Num() == 0) return -1;

	auto CardPriority = [&](const FCardInstance& C) -> int32
	{
		bool bHasDamage = false, bHasSpirit = false, bHasBlock = false, bHasHeal = false,
			bHasEnemyStatus = false, bHasPower = false, bHasCostFree = false;
		for (const FCardEffect& E : C.GetEffects())
		{
			if (E.Action == TEXT("damage") || E.Action == TEXT("damage_all") || E.Action == TEXT("damage_random")
				|| E.Action == TEXT("damage_per_block") || E.Action == TEXT("damage_per_status")
				|| E.Action == TEXT("damage_all_per_basic_gongfa")
				|| E.Action == TEXT("damage_all_per_repeat")) bHasDamage = true;
			else if (E.Action == TEXT("gain_spirit") || E.Action == TEXT("spirit_next_turn")) bHasSpirit = true;
			else if (E.Action == TEXT("block") || E.Action == TEXT("block_per_status")) bHasBlock = true;
			else if (E.Action == TEXT("heal")) bHasHeal = true;
			else if (E.Action == TEXT("apply_status") && E.Target == TEXT("enemy")) bHasEnemyStatus = true;
			else if (E.Action == TEXT("amplify_status") && E.Target == TEXT("enemy")) bHasEnemyStatus = true;
			else if (E.Action == TEXT("power")) bHasPower = true;
			else if (E.Action == TEXT("cost_free_basic_gongfa")) bHasCostFree = true;
		}
		if (bHasPower) return 0;      // 功法尽早运转
		if (bHasCostFree) return 1;   // 0费窗口先开
		if (bHasSpirit) return 1;
		if (bHasDamage) return 2;
		if (bHasEnemyStatus) return 3;
		if (bHasBlock) return 4;
		if (bHasHeal) return (Engine->Player.HP < Engine->Player.MaxHP) ? 5 : 99;
		return 6;
	};

	int32 BestIdx = -1, BestPrio = 100;
	for (int32 i : Playable)
	{
		const int32 P = CardPriority(Engine->Hand[i]);
		if (P < BestPrio)
		{
			BestPrio = P;
			BestIdx = i;
		}
	}
	return BestIdx;
}

int32 FCombatAI::ChooseTarget(UCombatEngine* Engine)
{
	int32 BestIdx = 0, LowestHP = INT32_MAX;
	for (int32 i = 0; i < Engine->Enemies.Num(); ++i)
	{
		if (Engine->Enemies[i].State.IsAlive() && Engine->Enemies[i].State.HP < LowestHP)
		{
			LowestHP = Engine->Enemies[i].State.HP;
			BestIdx = i;
		}
	}
	return BestIdx;
}
