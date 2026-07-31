#include "RunManager.h"
#include "GameDataLibrary.h"
#include "Map/MapGenerator.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "JsonObjectConverter.h"

void URunManager::Log(const FString& Msg) const
{
	UE_LOG(LogTemp, Display, TEXT("[Run] %s"), *Msg);
	OnLog.Broadcast(Msg);
}

bool URunManager::LoadAllData(FString& OutError)
{
	TArray<FCardData> Cards;
	if (!UGameDataLibrary::LoadCards(Cards, OutError)) return false;
	for (const FCardData& C : Cards) CardTable.Add(C.Id, C);

	TArray<FEnemyData> Enemies;
	if (!UGameDataLibrary::LoadEnemies(Enemies, OutError)) return false;
	for (const FEnemyData& E : Enemies)
	{
		EnemyTable.Add(E.Id, E);
		if (E.Tier == TEXT("normal")) NormalEnemyIds.Add(E.Id);
		else if (E.Tier == TEXT("elite")) EliteEnemyIds.Add(E.Id);
	}

	TArray<FRelicData> Relics;
	if (!UGameDataLibrary::LoadRelics(Relics, OutError)) return false;
	for (const FRelicData& R : Relics) RelicTable.Add(R.Id, R);

	TArray<FPillData> Pills;
	if (!UGameDataLibrary::LoadPills(Pills, OutError)) return false;
	for (const FPillData& P : Pills) PillTable.Add(P.Id, P);

	TArray<FEventData> Events;
	if (!UGameDataLibrary::LoadEvents(Events, OutError)) return false;
	for (const FEventData& E : Events) EventTable.Add(E.Id, E);

	return true;
}

// -----------------------------------------------------------
// 元进程（跨局成就）
// -----------------------------------------------------------

int32 URunManager::RarityToTier(const FString& Rarity)
{
	if (Rarity == TEXT("uncommon"))  return 1;
	if (Rarity == TEXT("rare"))      return 2;
	if (Rarity == TEXT("legendary")) return 3;
	return 0;
}

FString URunManager::GetMetaSavePath()
{
	return FPaths::ProjectSavedDir() / TEXT("SaveGames/meta_save.json");
}

void URunManager::EnsureMetaLoaded()
{
	if (bMetaLoaded) return;
	bMetaLoaded = true;

	FString Content;
	if (!FFileHelper::LoadFileToString(Content, *GetMetaSavePath()))
	{
		return;
	}

	TSharedPtr<FJsonObject> Root;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Content);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid()) return;

	Meta.HighestFloor = Root->GetIntegerField(TEXT("highest_floor"));

	const TArray<TSharedPtr<FJsonValue>>* KillsArr;
	if (Root->TryGetArrayField(TEXT("defeated_enemy_ids"), KillsArr))
	{
		for (const TSharedPtr<FJsonValue>& V : *KillsArr)
		{
			FString Id;
			if (V->TryGetString(Id)) Meta.DefeatedEnemyIds.Add(Id);
		}
	}

	// 品级由斩妖图鉴数推导（3种→中品 6种→上品 10种→仙品）
	const int32 N = Meta.DefeatedEnemyIds.Num();
	Meta.UnlockLevel = (N >= 10 ? 3 : N >= 6 ? 2 : N >= 3 ? 1 : 0);
}

void URunManager::SaveMetaToDisk() const
{
	TSharedPtr<FJsonObject> Root = MakeShareable(new FJsonObject);
	Root->SetNumberField(TEXT("highest_floor"), Meta.HighestFloor);
	Root->SetNumberField(TEXT("unlock_level"), Meta.UnlockLevel);

	TArray<TSharedPtr<FJsonValue>> KillsArr;
	for (const FString& Id : Meta.DefeatedEnemyIds)
	{
		KillsArr.Add(MakeShareable(new FJsonValueString(Id)));
	}
	Root->SetArrayField(TEXT("defeated_enemy_ids"), KillsArr);

	FString Content;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Content);
	FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);

	const FString Dir = FPaths::ProjectSavedDir() / TEXT("SaveGames");
	IFileManager::Get().MakeDirectory(*Dir, true);
	FFileHelper::SaveStringToFile(Content, *GetMetaSavePath());
}

void URunManager::RegisterEnemyKills(const TArray<FString>& EnemyIds)
{
	EnsureMetaLoaded();

	bool bChanged = false;
	for (const FString& Id : EnemyIds)
	{
		if (Id.IsEmpty() || Meta.DefeatedEnemyIds.Contains(Id)) continue;
		Meta.DefeatedEnemyIds.Add(Id);
		bChanged = true;

		const FEnemyData* E = EnemyTable.Find(Id);
		GainNotifications.Add(FString::Printf(TEXT("首次斩杀【%s】！斩妖图鉴 %d/%d"),
			E ? *E->Name : *Id, Meta.DefeatedEnemyIds.Num(), 10));
	}
	if (!bChanged) return;

	// 品级解锁判定
	const int32 N = Meta.DefeatedEnemyIds.Num();
	auto Grant = [&](int32 Level, const FString& What)
	{
		if (Meta.UnlockLevel < Level)
		{
			Meta.UnlockLevel = Level;
			GainNotifications.Add(FString::Printf(TEXT("成就解锁：%s 已进入今后的随机池"), *What));
		}
	};
	if (N >= 3) Grant(1, TEXT("【中品】法器与进阶卡牌"));
	if (N >= 6) Grant(2, TEXT("【上品】法器与高阶卡牌"));
	if (N >= 10) Grant(3, TEXT("【仙品】法器与传说卡牌"));

	SaveMetaToDisk();
}

void URunManager::UpdateMetaOnRunEnd()
{
	EnsureMetaLoaded();
	LastUnlockMessages.Reset();

	const int32 ThisRun = State.HighestFloorThisRun;
	if (ThisRun > Meta.HighestFloor)
	{
		Meta.HighestFloor = ThisRun;
		LastUnlockMessages.Add(FString::Printf(TEXT("新纪录：首次到达第 %d 层！"), ThisRun));
		SaveMetaToDisk();
	}
	else if (Meta.HighestFloor > 0)
	{
		LastUnlockMessages.Add(FString::Printf(TEXT("历史最高纪录：第 %d 层（本次第 %d 层）"),
			Meta.HighestFloor, ThisRun));
	}
}

TArray<FString> URunManager::ConsumeGainNotifications()
{
	TArray<FString> Result = GainNotifications;
	GainNotifications.Reset();
	return Result;
}

bool URunManager::StartNewRun(int32 Seed)
{
	RunSeed = Seed;
	Rng.Initialize(Seed);
	EnsureMetaLoaded();

	CardTable.Reset(); EnemyTable.Reset(); RelicTable.Reset();
	PillTable.Reset(); EventTable.Reset();
	NormalEnemyIds.Reset(); EliteEnemyIds.Reset();

	FString Err;
	if (!LoadAllData(Err))
	{
		Log(FString::Printf(TEXT("数据加载失败: %s"), *Err));
		return false;
	}

	// 初始状态
	State = FRunState();
	State.bRunActive = true;

	TArray<FString> StarterIds;
	if (!UGameDataLibrary::LoadStarterDeck(StarterIds, Err)) return false;
	for (const FString& Id : StarterIds)
	{
		FDeckCard DC;
		DC.CardId = Id;
		State.Deck.Add(DC);
	}

	// 迷雾探索初始化
	CurrentChoices.Reset();
	GainNotifications.Reset();
	LastCombatEnemyIds.Reset();
	EnsureFloorChoices();

	Log(TEXT("====== 踏上登仙路 ======"));
	Log(FString::Printf(TEXT("境界: %s | 气血 %d/%d | 灵石 %d | 卡组 %d 张"),
		*State.Realm, State.HP, State.MaxHP, State.Gold, State.Deck.Num()));
	Log(FString::Printf(TEXT("本局种子: %d"), Seed));

	return true;
}

// -----------------------------------------------------------
// 迷雾探索（未知路径 + 精英软保底）
// -----------------------------------------------------------

int32 URunManager::EnemyLevelForFloor(int32 Floor)
{
	if (Floor >= 9) return 3;
	if (Floor >= 6) return 2;
	if (Floor >= 3) return 1;
	return 0;
}

void URunManager::EnsureFloorChoices()
{
	if (!State.bRunActive || CurrentChoices.Num() > 0) return;
	if (State.CurrentFloor >= TotalFloors) return;

	// 叙事模式：当前 Beat 的选择作为路径选项
	if (bNarrativeMode && NarrativeSys)
	{
		const FNarrativeBeat Beat = GetCurrentNarrativeBeat();
		if (Beat.Choices.Num() > 0)
		{
			CurrentChoices.Reset();
			for (int32 i = 0; i < Beat.Choices.Num(); ++i)
			{
				FMysteryChoice MC;
				MC.bRevealed = true;
				// 使用叙事选项作为纯文本展示
				MC.StoryChoiceIndex = i;
				CurrentChoices.Add(MC);
			}
			return;
		}
	}

	// 选项使用独立随机流（由 种子+层数 决定），保证存档/读档后一致
	FRandomStream ChoiceRng(RunSeed * 31 + State.CurrentFloor * 7919 + 17);
	const int32 F = State.CurrentFloor;
	const int32 Level = EnemyLevelForFloor(F);
	const bool bBossFloor = (F == TotalFloors - 1);

	auto MakeCombatChoice = [&](bool bElite, bool bRevealed)
	{
		FMysteryChoice C;
		C.Type = bElite ? EMapNodeType::Elite : EMapNodeType::Combat;
		C.bRevealed = bRevealed;
		C.EnemyLevel = Level;
		// 敌人构成
		if (bElite)
		{
			C.EnemyIds.Add(EliteEnemyIds[ChoiceRng.RandRange(0, EliteEnemyIds.Num() - 1)]);
		}
		else
		{
			C.EnemyIds.Add(NormalEnemyIds[ChoiceRng.RandRange(0, NormalEnemyIds.Num() - 1)]);
			if (ChoiceRng.FRand() < 0.3f)
			{
				C.EnemyIds.Add(NormalEnemyIds[ChoiceRng.RandRange(0, NormalEnemyIds.Num() - 1)]);
			}
		}
		return C;
	};

	if (bBossFloor)
	{
		FMysteryChoice Boss;
		Boss.Type = EMapNodeType::Boss;
		Boss.bRevealed = true;
		Boss.EnemyLevel = Level;
		Boss.EnemyIds.Add(TEXT("blood_ancestor"));
		CurrentChoices.Add(Boss);
		return;
	}

	// ---- 精英软保底：第5层(索引4)起 30% 起步，每层无精英 +30% ----
	bool bEliteAppears = false;
	if (F >= 4)
	{
		const float EliteChance = 0.30f * (State.ElitePity + 1);
		if (ChoiceRng.FRand() < EliteChance) bEliteAppears = true;
	}
	else if (F >= 1 && ChoiceRng.FRand() < 0.08f)
	{
		bEliteAppears = true; // 前四层小概率偶遇
	}
	if (bEliteAppears) State.ElitePity = 0;
	else if (F >= 4) State.ElitePity += 1;

	// ---- 选项数量：第1层2个，其后3个 ----
	const int32 NumOptions = (F == 0) ? 2 : 3;
	for (int32 i = 0; i < NumOptions; ++i)
	{
		if (bEliteAppears && i == 0)
		{
			// 精英固定已揭示（软保底，玩家可见强敌）
			CurrentChoices.Add(MakeCombatChoice(true, true));
			continue;
		}

		const float Roll = ChoiceRng.FRand();
		const bool bRevealed = ChoiceRng.FRand() < 0.45f;
		if (Roll < 0.58f)
		{
			CurrentChoices.Add(MakeCombatChoice(false, bRevealed));
		}
		else
		{
			FMysteryChoice C;
			C.bRevealed = bRevealed;
			C.EnemyLevel = Level;
			if (Roll < 0.72f)      C.Type = EMapNodeType::Event;
			else if (Roll < 0.82f) C.Type = EMapNodeType::Shop;
			else                   C.Type = EMapNodeType::Rest;
			CurrentChoices.Add(C);
		}
	}

	// ---- 保命项：若全部为已揭示战斗/精英，追加「绕道而行」（隐藏、非精英） ----
	bool bAnyRevealedCombat = false;
	for (const FMysteryChoice& C : CurrentChoices)
	{
		if (C.bRevealed && (C.Type == EMapNodeType::Combat || C.Type == EMapNodeType::Elite)) bAnyRevealedCombat = true;
	}
	if (bAnyRevealedCombat)
	{
		FMysteryChoice Escape;
		Escape.bRevealed = false;
		Escape.EnemyLevel = Level;
		const float Roll = ChoiceRng.FRand();
		if (Roll < 0.60f)      Escape.Type = EMapNodeType::Combat;
		else if (Roll < 0.75f) Escape.Type = EMapNodeType::Event;
		else if (Roll < 0.87f) Escape.Type = EMapNodeType::Rest;
		else                   Escape.Type = EMapNodeType::Shop;
		if (Escape.Type == EMapNodeType::Combat)
		{
			Escape.EnemyIds.Add(NormalEnemyIds[ChoiceRng.RandRange(0, NormalEnemyIds.Num() - 1)]);
		}
		CurrentChoices.Add(Escape);
	}
}

FNodeEncounter URunManager::ChooseOption(int32 Index)
{
	FNodeEncounter Enc;
	if (!State.bRunActive || !CurrentChoices.IsValidIndex(Index)) return Enc;

	const FMysteryChoice Choice = CurrentChoices[Index];
	CurrentChoices.Reset();

	// 叙事模式：推进剧情
	if (bNarrativeMode && Choice.StoryChoiceIndex >= 0)
	{
		Enc.bIsNarrative = true;
		Enc.NarrativeChoiceIndex = Choice.StoryChoiceIndex;
		// 不推进层数，叙事剧情后返回同一层
		return Enc;
	}

	State.CurrentFloor += 1;
	State.HighestFloorThisRun = FMath::Max(State.HighestFloorThisRun, State.CurrentFloor);
	Enc.Type = Choice.Type;
	Enc.EnemyHPBonus = State.KillStreak * 5;
	Enc.EnemyLevel = Choice.EnemyLevel;

	static const TMap<EMapNodeType, FString> TypeNames = {
		{EMapNodeType::Combat, TEXT("战斗")}, {EMapNodeType::Elite, TEXT("精英")},
		{EMapNodeType::Rest, TEXT("打坐调息")}, {EMapNodeType::Shop, TEXT("坊市")},
		{EMapNodeType::Event, TEXT("奇遇")}, {EMapNodeType::Boss, TEXT("BOSS")}
	};
	Log(FString::Printf(TEXT("--- 进入第 %d 层 [%s] ---"), State.CurrentFloor, *TypeNames[Choice.Type]));

	switch (Choice.Type)
	{
	case EMapNodeType::Combat:
	case EMapNodeType::Elite:
	case EMapNodeType::Boss:
		Enc.EnemyIds = Choice.EnemyIds;
		LastCombatNodeType = Choice.Type;
		LastCombatEnemyIds = Choice.EnemyIds;
		for (const FString& Id : Enc.EnemyIds)
		{
			if (const FEnemyData* E = EnemyTable.Find(Id))
			{
				if (!E->Story.IsEmpty()) Enc.StoryText = E->Story;
				Log(FString::Printf(TEXT("  遭遇: %s"), *E->Name));
			}
		}
		break;

	case EMapNodeType::Event:
	{
		TArray<FString> EventIds;
		EventTable.GetKeys(EventIds);
		CurrentEventId = Choice.EventId.IsEmpty()
			? EventIds[Rng.RandRange(0, EventIds.Num() - 1)]
			: Choice.EventId;
		Enc.EventId = CurrentEventId;
		if (const FEventData* Ev = EventTable.Find(CurrentEventId))
		{
			Log(FString::Printf(TEXT("  【%s】%s"), *Ev->Title, *Ev->Text));
		}
		break;
	}

	case EMapNodeType::Shop:
		GenerateShopStock();
		break;

	case EMapNodeType::Rest:
		Log(TEXT("  此处灵气充裕，宜打坐调息"));
		break;
	}

	return Enc;
}

FCombatReward URunManager::ResolveCombatVictory(bool bChoseKillLoot, int32 PlayerRemainingHP, int32 GoldFromCombat)
{
	FCombatReward Reward;
	const EMapNodeType NodeType = LastCombatNodeType;

	State.HP = PlayerRemainingHP;

	// 斩妖图鉴登记（新种类 → 成就）
	RegisterEnemyKills(LastCombatEnemyIds);

	// 基础灵石
	switch (NodeType)
	{
	case EMapNodeType::Elite: Reward.Gold = Rng.RandRange(25, 35); break;
	case EMapNodeType::Boss: Reward.Gold = 50; break;
	default: Reward.Gold = Rng.RandRange(10, 17); break;
	}
	Reward.Gold += GoldFromCombat; // 法宝等战斗内产出

	// 卡牌三选一（无重复，强度越高的敌人低品质卡越少）
	TSet<FString> UsedIds;
	for (int32 i = 0; i < 3; ++i)
	{
		FDeckCard DC = RollRandomRewardCard(NodeType);
		int32 Safety = 0;
		while (UsedIds.Contains(DC.CardId) && Safety < 30)
		{
			DC = RollRandomRewardCard(NodeType);
			Safety++;
		}
		UsedIds.Add(DC.CardId);
		Reward.CardChoices.Add(DC);
	}

	// 精英掉法宝
	if (NodeType == EMapNodeType::Elite)
	{
		Reward.RelicId = RollRandomRelic();
		Reward.bKillLootAvailable = true;
		Reward.KillLootGold = Reward.Gold; // 夺宝=翻倍灵石
	}

	Log(FString::Printf(TEXT("战斗胜利! 获得 %d 灵石"), Reward.Gold));
	State.Gold += Reward.Gold;

	// 杀人夺宝
	if (bChoseKillLoot && Reward.bKillLootAvailable)
	{
		float LootMult = 1.f;
		for (const FString& RId : State.RelicIds)
		{
			if (const FRelicData* R = RelicTable.Find(RId))
			{
				if (R->Trigger == TEXT("on_loot")) LootMult += R->Modifier;
			}
		}
		const int32 LootGold = FMath::RoundToInt(Reward.KillLootGold * LootMult);
		State.Gold += LootGold;
		State.KillStreak += 1;

		// 夺宝额外给一张牌
		const FDeckCard ExtraDC = RollRandomRewardCard(NodeType);
		State.Deck.Add(ExtraDC);
		Log(FString::Printf(TEXT("【杀人夺宝】搜刮尸身: +%d 灵石, 额外获得卡牌 [%s%s]"), LootGold, *ExtraDC.CardId, ExtraDC.bUpgraded ? TEXT("+") : TEXT("")));

		Log(FString::Printf(TEXT("魔道因果 +1 (当前 %d)——日后的敌人会更强"), State.KillStreak));
	}

	// 拾取法宝
	if (!Reward.RelicId.IsEmpty())
	{
		State.RelicIds.Add(Reward.RelicId);
		if (const FRelicData* R = RelicTable.Find(Reward.RelicId))
		{
			Log(FString::Printf(TEXT("获得法宝 【%s】: %s"), *R->Name, *R->Description));
			GainNotifications.Add(FString::Printf(TEXT("获得法宝【%s】"), *R->Name));
		}
	}

	PendingReward = Reward;

	// Boss 战胜利 -> 突破
	if (NodeType == EMapNodeType::Boss)
	{
		Breakthrough();
	}

	return Reward;
}

void URunManager::PickRewardCard(const FDeckCard& Card)
{
	if (Card.CardId.IsEmpty())
	{
		Log(TEXT("跳过卡牌奖励"));
		return;
	}
	State.Deck.Add(Card);
	if (const FCardData* C = CardTable.Find(Card.CardId))
	{
		Log(FString::Printf(TEXT("获得卡牌 【%s%s】"), *C->Name, Card.bUpgraded ? TEXT("+") : TEXT("")));
	}
}

void URunManager::ResolveDefeat()
{
	State.bRunActive = false;
	State.bRunVictory = false;
	UpdateMetaOnRunEnd();
	Log(TEXT("====== 道消身殒，登仙路断 ======"));
	for (const FString& M : LastUnlockMessages) Log(M);
}

void URunManager::ResolveRest(bool bHeal)
{
	if (bHeal)
	{
		const int32 Healed = FMath::RoundToInt(State.MaxHP * 0.55f);
		State.HP = FMath::Min(State.MaxHP, State.HP + Healed);
		Log(FString::Printf(TEXT("打坐调息，恢复 %d 点气血 (HP %d/%d)"), Healed, State.HP, State.MaxHP));
	}
	else
	{
		// 随机升级一张未升级的牌
		TArray<int32> NotUpgraded;
		for (int32 i = 0; i < State.Deck.Num(); ++i)
		{
			if (!State.Deck[i].bUpgraded) NotUpgraded.Add(i);
		}
		if (NotUpgraded.Num() > 0)
		{
			const int32 Idx = NotUpgraded[Rng.RandRange(0, NotUpgraded.Num() - 1)];
			State.Deck[Idx].bUpgraded = true;
			if (const FCardData* C = CardTable.Find(State.Deck[Idx].CardId))
			{
				Log(FString::Printf(TEXT("悟道突破: 【%s】获得提升"), *C->Name));
			}
		}
	}
}

FString URunManager::ResolveEventChoice(int32 ChoiceIndex)
{
	const FEventData* Ev = EventTable.Find(CurrentEventId);
	if (!Ev || !Ev->Choices.IsValidIndex(ChoiceIndex)) return TEXT("");

	const FEventChoice& Choice = Ev->Choices[ChoiceIndex];
	Log(FString::Printf(TEXT("选择: %s"), *Choice.Text));

	for (const FEventEffect& Eff : Choice.Effects)
	{
		ApplyEventEffect(Eff);
	}

	Log(FString::Printf(TEXT("  %s"), *Choice.ResultText));
	return Choice.ResultText;
}

// -----------------------------------------------------------
// 叙事系统
// -----------------------------------------------------------

void URunManager::InitNarrative(UNarrativeSystem* Sys, const FString& ActId)
{
	NarrativeSys = Sys;
	CurrentActId = ActId;
	bNarrativeMode = true;
	CurrentBeatId = TEXT("");
	PendingNarrativeSummary = TEXT("");
}

FNarrativeBeat URunManager::GetCurrentNarrativeBeat() const
{
	if (NarrativeSys)
	{
		if (!CurrentBeatId.IsEmpty())
		{
			const FNarrativeBeat* B = NarrativeSys->FindBeat(CurrentBeatId);
			if (B) return *B;
		}
		return NarrativeSys->GetStartBeat(CurrentActId);
	}
	FNarrativeBeat Fallback;
	Fallback.BeatId = TEXT("fallback");
	Fallback.NarratorText = TEXT("前路未卜……");
	return Fallback;
}

bool URunManager::AdvanceNarrative(int32 ChoiceIndex)
{
	if (!NarrativeSys) return false;

	const FNarrativeBeat Beat = GetCurrentNarrativeBeat();
	if (!Beat.Choices.IsValidIndex(ChoiceIndex)) return false;

	const FNarrativeOutcome& Outcome = Beat.Choices[ChoiceIndex].Outcome;

	// 应用效果
	if (Outcome.HPChange != 0)
	{
		State.HP = FMath::Clamp(State.HP + Outcome.HPChange, 0, State.MaxHP);
	}
	if (Outcome.GoldChange != 0)
	{
		State.Gold = FMath::Max(0, State.Gold + Outcome.GoldChange);
	}
	if (!Outcome.GainRelicId.IsEmpty())
	{
		State.RelicIds.Add(Outcome.GainRelicId);
	}
	if (!Outcome.GainCardId.IsEmpty())
	{
		FDeckCard DC;
		DC.CardId = Outcome.GainCardId;
		DC.bUpgraded = Outcome.bUpgraded;
		State.Deck.Add(DC);
	}

	PendingNarrativeSummary = Outcome.SummaryText;

	switch (Outcome.Type)
	{
	case ENarrativeOutcomeType::NextBeat:
		CurrentBeatId = Outcome.Param;
		return false; // 继续剧情（不进入游戏节点）

	case ENarrativeOutcomeType::Combat:
	case ENarrativeOutcomeType::Elite:
	case ENarrativeOutcomeType::Boss:
	case ENarrativeOutcomeType::Rest:
	case ENarrativeOutcomeType::Shop:
	case ENarrativeOutcomeType::Event:
		// 进入游戏节点，当前剧情段结束
		bNarrativeMode = false;
		return true;

	case ENarrativeOutcomeType::GameOver:
		State.HP = 0;
		return false;

	default:
		return false;
	}
}

void URunManager::ApplyEventEffect(const FEventEffect& Effect)
{
	const FString& A = Effect.Action;

	if (A == TEXT("gold"))
	{
		State.Gold = FMath::Max(0, State.Gold + Effect.Value);
		Log(FString::Printf(TEXT("  灵石 %+d (共 %d)"), Effect.Value, State.Gold));
	}
	else if (A == TEXT("hp"))
	{
		State.HP = FMath::Clamp(State.HP + Effect.Value, 1, State.MaxHP);
		Log(FString::Printf(TEXT("  气血 %+d (HP %d/%d)"), Effect.Value, State.HP, State.MaxHP));
	}
	else if (A == TEXT("max_hp"))
	{
		State.MaxHP += Effect.Value;
		State.HP += Effect.Value;
		Log(FString::Printf(TEXT("  气血上限 %+d (HP %d/%d)"), Effect.Value, State.HP, State.MaxHP));
	}
	else if (A == TEXT("add_relic"))
	{
		const FString RId = RollRandomRelic();
		if (!RId.IsEmpty())
		{
			State.RelicIds.Add(RId);
			if (const FRelicData* R = RelicTable.Find(RId))
			{
				Log(FString::Printf(TEXT("  获得法宝 【%s】"), *R->Name));
				GainNotifications.Add(FString::Printf(TEXT("获得法宝【%s】"), *R->Name));
			}
		}
	}
	else if (A == TEXT("add_pill"))
	{
		const FString PId = RollRandomPill();
		if (!PId.IsEmpty())
		{
			State.PillIds.Add(PId);
			if (const FPillData* P = PillTable.Find(PId))
			{
				Log(FString::Printf(TEXT("  获得丹药 【%s】"), *P->Name));
				GainNotifications.Add(FString::Printf(TEXT("获得丹药【%s】"), *P->Name));
			}
		}
	}
	else if (A == TEXT("upgrade_random"))
	{
		TArray<int32> NotUpgraded;
		for (int32 i = 0; i < State.Deck.Num(); ++i)
		{
			if (!State.Deck[i].bUpgraded) NotUpgraded.Add(i);
		}
		if (NotUpgraded.Num() > 0)
		{
			const int32 Idx = NotUpgraded[Rng.RandRange(0, NotUpgraded.Num() - 1)];
			State.Deck[Idx].bUpgraded = true;
			if (const FCardData* C = CardTable.Find(State.Deck[Idx].CardId))
			{
				Log(FString::Printf(TEXT("  【%s】获得提升"), *C->Name));
				GainNotifications.Add(FString::Printf(TEXT("卡牌升级【%s】"), *C->Name));
			}
		}
	}
	else if (A == TEXT("add_card"))
	{
		FString CId;
		if (Effect.Param == TEXT("random_rare")) CId = RollRandomCard(TEXT("rare"));
		else if (Effect.Param == TEXT("random")) CId = RollRandomCard();
		else CId = Effect.Param;

		if (!CId.IsEmpty())
		{
			FDeckCard DC;
			DC.CardId = CId;
			State.Deck.Add(DC);
			if (const FCardData* C = CardTable.Find(CId))
			{
				Log(FString::Printf(TEXT("  获得卡牌 【%s】"), *C->Name));
				GainNotifications.Add(FString::Printf(TEXT("获得卡牌【%s】"), *C->Name));
			}
		}
	}
	else if (A == TEXT("kill_streak"))
	{
		State.KillStreak += Effect.Value;
		Log(FString::Printf(TEXT("  魔道因果 %+d (当前 %d)"), Effect.Value, State.KillStreak));
	}
}

void URunManager::GenerateShopStock()
{
	CurrentShopStock.Reset();

	// 3 张卡
	for (int32 i = 0; i < 3; ++i)
	{
		FShopItem Item;
		Item.ItemType = TEXT("card");
		Item.ItemId = RollRandomCard();
		if (const FCardData* C = CardTable.Find(Item.ItemId)) Item.Price = CardPrice(C->Rarity);
		CurrentShopStock.Add(Item);
	}
	// 2 件法宝
	for (int32 i = 0; i < 2; ++i)
	{
		FShopItem Item;
		Item.ItemType = TEXT("relic");
		Item.ItemId = RollRandomRelic();
		if (const FRelicData* R = RelicTable.Find(Item.ItemId)) Item.Price = RelicPrice(R->Rarity);
		CurrentShopStock.Add(Item);
	}
	// 2 颗丹药
	for (int32 i = 0; i < 2; ++i)
	{
		FShopItem Item;
		Item.ItemType = TEXT("pill");
		Item.ItemId = RollRandomPill();
		Item.Price = 18;
		CurrentShopStock.Add(Item);
	}

	Log(TEXT("坊市货品:"));
	for (int32 i = 0; i < CurrentShopStock.Num(); ++i)
	{
		const FShopItem& It = CurrentShopStock[i];
		FString Name = It.ItemId;
		if (It.ItemType == TEXT("card")) { if (const FCardData* C = CardTable.Find(It.ItemId)) Name = C->Name; }
		else if (It.ItemType == TEXT("relic")) { if (const FRelicData* R = RelicTable.Find(It.ItemId)) Name = R->Name; }
		else if (const FPillData* P = PillTable.Find(It.ItemId)) Name = P->Name;
		Log(FString::Printf(TEXT("  [%d] %s - %d 灵石"), i, *Name, It.Price));
	}
}

TArray<FShopItem> URunManager::GetShopStock()
{
	return CurrentShopStock;
}

bool URunManager::BuyShopItem(int32 ItemIndex)
{
	if (!CurrentShopStock.IsValidIndex(ItemIndex)) return false;
	FShopItem& Item = CurrentShopStock[ItemIndex];
	if (Item.bSold || State.Gold < Item.Price) return false;

	State.Gold -= Item.Price;
	Item.bSold = true;

	if (Item.ItemType == TEXT("card"))
	{
		FDeckCard DC;
		DC.CardId = Item.ItemId;
		State.Deck.Add(DC);
	}
	else if (Item.ItemType == TEXT("relic"))
	{
		State.RelicIds.Add(Item.ItemId);
	}
	else
	{
		State.PillIds.Add(Item.ItemId);
	}

	FString ItemName = Item.ItemId;
	if (Item.ItemType == TEXT("card")) { if (const FCardData* C = CardTable.Find(Item.ItemId)) ItemName = C->Name; }
	else if (Item.ItemType == TEXT("relic")) { if (const FRelicData* R = RelicTable.Find(Item.ItemId)) ItemName = R->Name; }
	else if (const FPillData* P = PillTable.Find(Item.ItemId)) ItemName = P->Name;
	Log(FString::Printf(TEXT("买下 [%s]，剩余灵石 %d"), *ItemName, State.Gold));
	GainNotifications.Add(FString::Printf(TEXT("购得【%s】"), *ItemName));
	return true;
}

const FEventData* URunManager::GetCurrentEvent() const
{
	return EventTable.Find(CurrentEventId);
}

bool IsCardExcludedFromRewards(const FString& CardId)
{
	return CardId == TEXT("one_sword") || CardId == TEXT("strike") || CardId == TEXT("defend");
}

// -----------------------------------------------------------
// 随机工具
// -----------------------------------------------------------

FDeckCard URunManager::RollRandomRewardCard(EMapNodeType NodeType) const
{
	// 敌人越强，roll 到低品质的概率越低
	float CommonChance = 0.60f;
	float UncommonChance = 0.30f;
	float RareChance = 0.10f;
	switch (NodeType)
	{
	case EMapNodeType::Elite: CommonChance = 0.35f; UncommonChance = 0.40f; RareChance = 0.25f; break;
	case EMapNodeType::Boss:  CommonChance = 0.15f; UncommonChance = 0.35f; RareChance = 0.50f; break;
	default: break;
	}

	FString Rarity;
	const float Roll = Rng.FRand();
	float Acc = 0.f;
	Acc += CommonChance;   if (Roll < Acc) Rarity = TEXT("common");
	if (Rarity.IsEmpty()) { Acc += UncommonChance; if (Roll < Acc) Rarity = TEXT("uncommon"); }
	if (Rarity.IsEmpty()) { Acc += RareChance;     if (Roll < Acc) Rarity = TEXT("rare"); }
	if (Rarity.IsEmpty()) Rarity = TEXT("legendary");

	const int32 Tier = FMath::Min(RarityToTier(Rarity), Meta.UnlockLevel);
	switch (Tier)
	{
	case 1: Rarity = TEXT("uncommon"); break;
	case 2: Rarity = TEXT("rare"); break;
	case 3: Rarity = TEXT("legendary"); break;
	default: Rarity = TEXT("common"); break;
	}

	// 按品级收集可用卡池，排除 one_sword、未强化 strike/defend
	TArray<FString> Pool;
	for (const auto& Pair : CardTable)
	{
		if (Pair.Value.Rarity != Rarity) continue;
		if (!Pair.Value.Class.IsEmpty() && Pair.Value.Class != TEXT("sword")) continue;
		if (Pair.Value.Type == TEXT("curse")) continue;
		if (IsCardExcludedFromRewards(Pair.Key)) continue;
		Pool.Add(Pair.Key);
	}
	if (Pool.Num() == 0)
	{
		// 回退到已解锁全部品级
		for (const auto& Pair : CardTable)
		{
			if (RarityToTier(Pair.Value.Rarity) > Meta.UnlockLevel) continue;
			if (IsCardExcludedFromRewards(Pair.Key)) continue;
			Pool.Add(Pair.Key);
		}
	}

	FDeckCard Result;
	if (Pool.Num() > 0)
	{
		Result.CardId = Pool[Rng.RandRange(0, Pool.Num() - 1)];
		// 40% 概率出现强化版
		Result.bUpgraded = Rng.FRand() < 0.4f;
	}
	return Result;
}

FString URunManager::RollRarity(FRandomStream& Stream) const
{
	// 品级按成就解锁：roll 出品级后压至已解锁上限
	const float Roll = Stream.FRand();
	int32 Tier = (Roll < 0.6f) ? 0 : (Roll < 0.9f ? 1 : 2);
	// 仙品解锁后小概率出现
	if (Meta.UnlockLevel >= 3 && Stream.FRand() < 0.05f) Tier = 3;
	Tier = FMath::Min(Tier, Meta.UnlockLevel);

	switch (Tier)
	{
	case 1: return TEXT("uncommon");
	case 2: return TEXT("rare");
	case 3: return TEXT("legendary");
	default: return TEXT("common");
	}
}

FString URunManager::RollRandomCard(const FString& RarityFilter) const
{
	FString Rarity = RarityFilter;
	if (Rarity.IsEmpty())
	{
		Rarity = RollRarity(Rng);
	}
	else
	{
		// 显式指定品级时同样受解锁上限约束
		const int32 Tier = FMath::Min(RarityToTier(Rarity), Meta.UnlockLevel);
		switch (Tier)
		{
		case 1: Rarity = TEXT("uncommon"); break;
		case 2: Rarity = TEXT("rare"); break;
		case 3: Rarity = TEXT("legendary"); break;
		default: Rarity = TEXT("common"); break;
		}
	}

	TArray<FString> Pool;
	for (const auto& Pair : CardTable)
	{
		if (Pair.Value.Rarity != Rarity) continue;
			// 职业过滤：当前仅剑修，非空且非 sword 的卡不出现
			if (!Pair.Value.Class.IsEmpty() && Pair.Value.Class != TEXT("sword")) continue;
			// 诅咒牌不进入正常奖励池
			if (Pair.Value.Type == TEXT("curse")) continue;
		Pool.Add(Pair.Key);
	}
	if (Pool.Num() == 0)
	{
		// 回退：全部已解锁品级
		for (const auto& Pair : CardTable)
		{
			if (RarityToTier(Pair.Value.Rarity) <= Meta.UnlockLevel) Pool.Add(Pair.Key);
		}
	}
	return Pool.Num() > 0 ? Pool[Rng.RandRange(0, Pool.Num() - 1)] : TEXT("");
}

FString URunManager::RollRandomRelic() const
{
	TArray<FString> Pool;
	for (const auto& Pair : RelicTable)
	{
		if (State.RelicIds.Contains(Pair.Key)) continue;
		if (RarityToTier(Pair.Value.Rarity) > Meta.UnlockLevel) continue;
		Pool.Add(Pair.Key);
	}
	return Pool.Num() > 0 ? Pool[Rng.RandRange(0, Pool.Num() - 1)] : TEXT("");
}

TArray<FString> URunManager::RollInitialRelicChoices(int32 Count)
{
	// 初始三选一法器 100% 为凡品（不受解锁进度影响）
	TArray<FString> Result;
	TArray<FString> Pool;
	for (const auto& Pair : RelicTable)
	{
		if (RarityToTier(Pair.Value.Rarity) == 0) Pool.Add(Pair.Key);
	}
	while (Result.Num() < Count && Pool.Num() > 0)
	{
		int32 Idx = Rng.RandRange(0, Pool.Num() - 1);
		Result.Add(Pool[Idx]);
		Pool.RemoveAt(Idx);
	}
	return Result;
}

FString URunManager::RollRandomPill() const
{
	TArray<FString> Pool;
	for (const auto& Pair : PillTable) Pool.Add(Pair.Key);
	return Pool.Num() > 0 ? Pool[Rng.RandRange(0, Pool.Num() - 1)] : TEXT("");
}

int32 URunManager::CardPrice(const FString& Rarity) const
{
	if (Rarity == TEXT("rare")) return 75;
	if (Rarity == TEXT("uncommon")) return 40;
	return 25;
}

int32 URunManager::RelicPrice(const FString& Rarity) const
{
	if (Rarity == TEXT("rare")) return 130;
	if (Rarity == TEXT("uncommon")) return 90;
	return 60;
}

// -----------------------------------------------------------
// 存档 / 读档
// -----------------------------------------------------------

bool URunManager::HasSaveFile()
{
	return IFileManager::Get().FileExists(*(FPaths::ProjectSavedDir() / TEXT("SaveGames/run_save.json")));
}

bool URunManager::SaveRun() const
{
	// 主结构
	TSharedPtr<FJsonObject> Root = MakeShareable(new FJsonObject);
	Root->SetNumberField(TEXT("save_version"), 2);
	Root->SetNumberField(TEXT("seed"), RunSeed);

	// Run 状态（USTRUCT 自动序列化，含 CurrentFloor/ElitePity）
	TSharedPtr<FJsonObject> StateJson = FJsonObjectConverter::UStructToJsonObject(State);
	if (!StateJson.IsValid()) return false;
	Root->SetObjectField(TEXT("state"), StateJson);

	Root->SetStringField(TEXT("current_event"), CurrentEventId);

	// 坊市库存
	TArray<TSharedPtr<FJsonValue>> ShopArr;
	for (const FShopItem& It : CurrentShopStock)
	{
		TSharedPtr<FJsonObject> ItemJson = FJsonObjectConverter::UStructToJsonObject(It);
		if (ItemJson.IsValid()) ShopArr.Add(MakeShareable(new FJsonValueObject(ItemJson)));
	}
	Root->SetArrayField(TEXT("shop"), ShopArr);

	FString OutString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutString);
	if (!FJsonSerializer::Serialize(Root, Writer)) return false;

	const FString SavePath = FPaths::ProjectSavedDir() / TEXT("SaveGames/run_save.json");
	const bool bOk = FFileHelper::SaveStringToFile(OutString, *SavePath);
	if (bOk) Log(FString::Printf(TEXT("已存档: %s"), *SavePath));
	return bOk;
}

bool URunManager::LoadRun()
{
	const FString SavePath = FPaths::ProjectSavedDir() / TEXT("SaveGames/run_save.json");
	FString Content;
	if (!FFileHelper::LoadFileToString(Content, *SavePath)) return false;

	TSharedPtr<FJsonObject> Root;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Content);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid()) return false;

	// 存档版本检查（v1 全图地图存档与迷雾探索不兼容，废弃）
	int32 SaveVersion = 0;
	if (!Root->TryGetNumberField(TEXT("save_version"), SaveVersion) || SaveVersion < 2)
	{
		UE_LOG(LogTemp, Display, TEXT("[Run] 旧版本存档不兼容，开始新的征程"));
		return false;
	}

	EnsureMetaLoaded();

	// 数据表
	CardTable.Reset(); EnemyTable.Reset(); RelicTable.Reset();
	PillTable.Reset(); EventTable.Reset();
	NormalEnemyIds.Reset(); EliteEnemyIds.Reset();
	FString Err;
	if (!LoadAllData(Err)) return false;

	RunSeed = Root->GetIntegerField(TEXT("seed"));
	Rng.Initialize(RunSeed);

	const TSharedPtr<FJsonObject>* StateJson;
	if (Root->TryGetObjectField(TEXT("state"), StateJson))
	{
		FJsonObjectConverter::JsonObjectToUStruct(StateJson->ToSharedRef(), &State);
	}

	// 当前层选项（由 种子+层数 确定性重建）
	CurrentChoices.Reset();

	Root->TryGetStringField(TEXT("current_event"), CurrentEventId);

	// 坊市库存
	CurrentShopStock.Reset();
	const TArray<TSharedPtr<FJsonValue>>* ShopArr;
	if (Root->TryGetArrayField(TEXT("shop"), ShopArr))
	{
		for (const TSharedPtr<FJsonValue>& V : *ShopArr)
		{
			const TSharedPtr<FJsonObject>* ItemJson;
			if (V->TryGetObject(ItemJson))
			{
				FShopItem It;
				FJsonObjectConverter::JsonObjectToUStruct(ItemJson->ToSharedRef(), &It);
				CurrentShopStock.Add(It);
			}
		}
	}

	Log(FString::Printf(TEXT("读档成功: 境界 %s, HP %d/%d, 灵石 %d, 卡组 %d 张"),
		*State.Realm, State.HP, State.MaxHP, State.Gold, State.Deck.Num()));
	return true;
}

void URunManager::Breakthrough()
{
	State.Realm = TEXT("筑基期");
	State.MaxHP += 10;
	State.HP = State.MaxHP;
	State.bRunVictory = true;
	State.bRunActive = false;
	UpdateMetaOnRunEnd();

	Log(TEXT(""));
	Log(TEXT("=============================================="));
	Log(TEXT("  天雷淬体，道基初成！"));
	Log(TEXT("  恭喜突破至 【筑基期】"));
	Log(FString::Printf(TEXT("  气血上限 +10 (HP %d/%d)"), State.HP, State.MaxHP));
	Log(TEXT("  （第一章·完 —— 更多境界敬请期待）"));
	Log(TEXT("=============================================="));
}
