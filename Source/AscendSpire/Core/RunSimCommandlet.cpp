#include "RunSimCommandlet.h"
#include "RunManager.h"
#include "Combat/CombatEngine.h"
#include "Combat/CombatAI.h"

int32 UAscendRunSimCommandlet::Main(const FString& Params)
{
	if (FParse::Param(*Params, TEXT("savetest")))
	{
		return RunSaveTest();
	}

	int32 Runs = 20;
	FParse::Value(*Params, TEXT("runs="), Runs);
	int32 BaseSeed = 1000;
	FParse::Value(*Params, TEXT("seed="), BaseSeed);
	const bool bVerbose = FParse::Param(*Params, TEXT("verbose"));

	int32 Wins = 0;
	int32 TotalFloors = 0;
	int32 TotalKills = 0; // 杀人夺宝次数
	TMap<FString, int32> DeathByNode;

	for (int32 i = 0; i < Runs; ++i)
	{
		UE_LOG(LogTemp, Display, TEXT("========== Run %d (seed %d) =========="), i + 1, BaseSeed + i);
		URunManager* Run = NewObject<URunManager>();
		const bool bWon = SimulateRun(Run, BaseSeed + i, bVerbose && i == 0);
		if (bWon) Wins++;
		TotalFloors += Run->State.CurrentFloor;
		TotalKills += Run->State.KillStreak;
	}

	UE_LOG(LogTemp, Display, TEXT("====== %d 局模拟结束 ======"), Runs);
	UE_LOG(LogTemp, Display, TEXT("通关率: %d/%d (%.0f%%)"), Wins, Runs, Runs > 0 ? 100.f * Wins / Runs : 0.f);
	UE_LOG(LogTemp, Display, TEXT("平均到达层数: %.1f / %d"), Runs > 0 ? (float)TotalFloors / Runs : 0.f, URunManager::TotalFloors);
	UE_LOG(LogTemp, Display, TEXT("平均杀人夺宝次数: %.1f"), Runs > 0 ? (float)TotalKills / Runs : 0.f);
	return 0;
}

int32 UAscendRunSimCommandlet::RunSaveTest()
{
	UE_LOG(LogTemp, Display, TEXT("====== 存档/读档回环测试 ======"));

	// 1. 开一局，走完前两个选项
	URunManager* RunA = NewObject<URunManager>();
	if (!RunA->StartNewRun(7777)) return 1;

	for (int32 Step = 0; Step < 2; ++Step)
	{
		RunA->EnsureFloorChoices();
		if (RunA->CurrentChoices.Num() == 0) break;
		FNodeEncounter Enc = RunA->ChooseOption(0);
		if (Enc.Type == EMapNodeType::Combat || Enc.Type == EMapNodeType::Elite)
		{
			if (!RunNodeCombat(RunA, Enc, 7777 + Step, false)) break;
		}
		else if (Enc.Type == EMapNodeType::Rest) RunA->ResolveRest(true);
		else if (Enc.Type == EMapNodeType::Event) RunA->ResolveEventChoice(0);
	}

	// 2. 存档
	if (!RunA->SaveRun())
	{
		UE_LOG(LogTemp, Error, TEXT("[SAVETEST] 存档失败"));
		return 1;
	}

	// 3. 新实例读档
	URunManager* RunB = NewObject<URunManager>();
	if (!RunB->LoadRun())
	{
		UE_LOG(LogTemp, Error, TEXT("[SAVETEST] 读档失败"));
		return 1;
	}

	// 4. 逐字段比对
	bool bOk = true;
	auto Check = [&](bool bCond, const FString& What)
	{
		UE_LOG(LogTemp, Display, TEXT("[SAVETEST] %s: %s"), *What, bCond ? TEXT("OK") : TEXT("MISMATCH"));
		if (!bCond) bOk = false;
	};

	Check(RunA->State.HP == RunB->State.HP, FString::Printf(TEXT("HP %d==%d"), RunA->State.HP, RunB->State.HP));
	Check(RunA->State.MaxHP == RunB->State.MaxHP, TEXT("MaxHP"));
	Check(RunA->State.Gold == RunB->State.Gold, FString::Printf(TEXT("Gold %d==%d"), RunA->State.Gold, RunB->State.Gold));
	Check(RunA->State.Deck.Num() == RunB->State.Deck.Num(), FString::Printf(TEXT("Deck %d==%d"), RunA->State.Deck.Num(), RunB->State.Deck.Num()));
	Check(RunA->State.RelicIds == RunB->State.RelicIds, TEXT("Relics"));
	Check(RunA->State.PillIds == RunB->State.PillIds, TEXT("Pills"));
	Check(RunA->State.KillStreak == RunB->State.KillStreak, TEXT("KillStreak"));
	Check(RunA->State.CurrentFloor == RunB->State.CurrentFloor, TEXT("CurrentFloor"));
	Check(RunA->State.ElitePity == RunB->State.ElitePity, TEXT("ElitePity"));
	Check(RunA->State.bRunActive == RunB->State.bRunActive, TEXT("RunActive"));

	bool bDeckSame = true;
	for (int32 i = 0; i < RunA->State.Deck.Num() && i < RunB->State.Deck.Num(); ++i)
	{
		if (RunA->State.Deck[i].CardId != RunB->State.Deck[i].CardId ||
			RunA->State.Deck[i].bUpgraded != RunB->State.Deck[i].bUpgraded)
		{
			bDeckSame = false;
			break;
		}
	}
	Check(bDeckSame, TEXT("DeckContent"));

	// 5. 读档后继续打到通关/失败（验证可玩性不受影响）
	bool bFinished = SimulateRun(RunB, 9999, false);
	UE_LOG(LogTemp, Display, TEXT("[SAVETEST] 读档后续跑完成: %s"),
		RunB->State.bRunVictory ? TEXT("通关") : (bFinished ? TEXT("完成") : TEXT("败北(正常)")));

	UE_LOG(LogTemp, Display, TEXT("[SAVETEST] %s"), bOk ? TEXT("ALL PASSED") : TEXT("FAILED"));
	return bOk ? 0 : 1;
}

bool UAscendRunSimCommandlet::SimulateRun(URunManager* Run, int32 Seed, bool bVerbose)
{
	if (!Run->StartNewRun(Seed))
	{
		UE_LOG(LogTemp, Error, TEXT("Run 初始化失败"));
		return false;
	}

	int32 SafetyCounter = 0;
	while (Run->State.bRunActive && ++SafetyCounter < 30)
	{
		Run->EnsureFloorChoices();
		if (Run->CurrentChoices.Num() == 0) break;

		const int32 ChoiceIdx = ChooseNode(Run, Run->CurrentChoices);
		FNodeEncounter Enc = Run->ChooseOption(ChoiceIdx);

		switch (Enc.Type)
		{
		case EMapNodeType::Combat:
		case EMapNodeType::Elite:
		case EMapNodeType::Boss:
			if (!RunNodeCombat(Run, Enc, Seed * 100 + SafetyCounter, bVerbose))
			{
				Run->ResolveDefeat();
				return false;
			}
			break;

		case EMapNodeType::Rest:
			Run->ResolveRest(Run->State.HP < Run->State.MaxHP * 0.6f);
			break;

		case EMapNodeType::Shop:
		{
			TArray<FShopItem> Stock = Run->GetShopStock();
			// 贪心: 优先法宝, 其次丹药
			for (int32 Pref = 0; Pref < 3; ++Pref)
			{
				const FString WantType = Pref == 0 ? TEXT("relic") : (Pref == 1 ? TEXT("pill") : TEXT("card"));
				bool bBought = false;
				for (int32 i = 0; i < Stock.Num(); ++i)
				{
					if (!Stock[i].bSold && Stock[i].ItemType == WantType && Stock[i].Price <= Run->State.Gold)
					{
						Run->BuyShopItem(i);
						bBought = true;
						break;
					}
				}
				if (!bBought) break; // 一类都买不起就逛完了
			}
			break;
		}

		case EMapNodeType::Event:
			// 贪心选第一个
			Run->ResolveEventChoice(0);
			break;
		}
	}

	return Run->State.bRunVictory;
}

int32 UAscendRunSimCommandlet::ChooseNode(URunManager* Run, const TArray<FMysteryChoice>& Available) const
{
	const float HPPct = (float)Run->State.HP / Run->State.MaxHP;

	auto Priority = [&](EMapNodeType T) -> int32
	{
		switch (T)
		{
		case EMapNodeType::Rest:   return HPPct < 0.45f ? 0 : 5;
		case EMapNodeType::Shop:   return Run->State.Gold > 60 ? 1 : 4;
		case EMapNodeType::Elite:  return HPPct > 0.45f ? 2 : 6;
		case EMapNodeType::Combat: return 3;
		case EMapNodeType::Event:  return 4;
		case EMapNodeType::Boss:   return 0;
		default:                   return 9;
		}
	};

	// 隐藏选项按平均风险评估（与战斗同级）

	int32 BestIdx = 0, BestPrio = 99;
	for (int32 i = 0; i < Available.Num(); ++i)
	{
		const int32 P = Priority(Available[i].Type);
		if (P < BestPrio)
		{
			BestPrio = P;
			BestIdx = i;
		}
	}
	return BestIdx;
}

bool UAscendRunSimCommandlet::RunNodeCombat(URunManager* Run, const FNodeEncounter& Enc, int32 Seed, bool bVerbose)
{
	UCombatEngine* Engine = NewObject<UCombatEngine>();

	// 精英/Boss 或残血时服丹
	if (Enc.Type != EMapNodeType::Combat || Run->State.HP < Run->State.MaxHP * 0.5f)
	{
		Engine->TestPills = Run->State.PillIds;
	}

	if (!Engine->StartCombat(Run->State.Deck, Enc.EnemyIds, Run->State.RelicIds,
		Run->State.MaxHP, Run->State.HP, Enc.EnemyHPBonus, Seed))
	{
		UE_LOG(LogTemp, Error, TEXT("战斗初始化失败"));
		return false;
	}

	const bool bWon = FCombatAI::SimulateBattle(Engine, 50);
	if (!bWon) return false;

	// 已服丹药从库存移除
	if (Engine->TestPills.Num() > 0) Run->State.PillIds.Reset();

	// 杀人夺宝: 精英战且气血健康时选
	const bool bLoot = (Enc.Type == EMapNodeType::Elite) && (Engine->Player.HP > Run->State.MaxHP * 0.4f);
	FCombatReward Reward = Run->ResolveCombatVictory(bLoot, Engine->Player.HP, Engine->Gold);

	// 选奖励卡
	const int32 Pick = ChooseRewardCard(Run, Reward.CardChoices);
	Run->PickRewardCard(Pick >= 0 ? Reward.CardChoices[Pick] : FDeckCard());

	return true;
}

int32 UAscendRunSimCommandlet::ChooseRewardCard(URunManager* Run, const TArray<FDeckCard>& CardChoices) const
{
	// 优先攻击牌，其次其他
	for (int32 i = 0; i < CardChoices.Num(); ++i)
	{
		if (const FCardData* C = Run->GetCardData(CardChoices[i].CardId))
		{
			for (const FCardEffect& E : C->Effects)
			{
				if (E.Action == TEXT("damage") || E.Action == TEXT("damage_all") || E.Action == TEXT("damage_random"))
				{
					return i;
				}
			}
		}
	}
	return CardChoices.Num() > 0 ? 0 : -1;
}
