#include "CombatEngine.h"
#include "Core/GameDataLibrary.h"

/** Fisher-Yates 洗牌（确定性，使用种子流） */
template<typename T>
static void ShuffleArray(TArray<T>& Arr, FRandomStream& Rng)
{
	for (int32 i = Arr.Num() - 1; i > 0; --i)
	{
		Arr.Swap(i, Rng.RandRange(0, i));
	}
}

// -----------------------------------------------------------
// 工具
// -----------------------------------------------------------

void UCombatEngine::Log(const FString& Msg) const
{
	UE_LOG(LogTemp, Display, TEXT("[Combat] %s"), *Msg);
	OnLog.Broadcast(Msg);
}

float UCombatEngine::ComputeTemplateThreat(const TArray<FEnemyData>& EnemyTemplates)
{
	if (EnemyTemplates.Num() == 0) return 1.f;

	float Score = 0.f;
	for (const FEnemyData& Enemy : EnemyTemplates)
	{
		// HP contributes sub-linearly so a single high-HP template does not make
		// every later encounter irrelevant to card quality.
		const float HPPart = FMath::Sqrt(static_cast<float>(FMath::Clamp(Enemy.MaxHP, 1, 100000)) / 18.f);
		float IntentPart = 0.f;
		for (const FEnemyIntent& Intent : Enemy.Intents)
		{
			const float Repetitions = FMath::Max(1, Intent.Times);
			const float Value = static_cast<float>(FMath::Max(0, Intent.Value)) * Repetitions;
			if (Intent.Action == TEXT("attack") || Intent.Action == TEXT("attack_multi"))
				IntentPart += Value / 8.f;
			else if (Intent.Action == TEXT("defend"))
				IntentPart += Value / 12.f;
			else
				IntentPart += (Value + Intent.StatusStacks * 1.5f) / 16.f;
		}

		float AbilityPart = 0.f;
		for (const FString& RawAbility : Enemy.Abilities)
		{
			FString AbilityId;
			FString RawValue;
			if (!RawAbility.Split(TEXT(":"), &AbilityId, &RawValue)) AbilityId = RawAbility;
			const int32 Value = RawValue.IsEmpty() ? 1 : FMath::Clamp(FCString::Atoi(*RawValue), 1, 100);
			if (AbilityId == TEXT("first_strike")) AbilityPart += 3.0f;
			else if (AbilityId == TEXT("thorns")) AbilityPart += Value * 0.60f;
			else if (AbilityId == TEXT("enrage_half")) AbilityPart += Value * 0.75f;
			else if (AbilityId == TEXT("regen")) AbilityPart += Value * 0.65f;
			else if (AbilityId == TEXT("block_aura")) AbilityPart += Value * 0.35f;
			else if (AbilityId == TEXT("poison_aura")) AbilityPart += Value * 0.85f;
			else if (AbilityId == TEXT("start_weak") || AbilityId == TEXT("start_vulnerable")) AbilityPart += Value * 0.55f;
			else if (AbilityId == TEXT("hand_limit")) AbilityPart += Value * 1.75f;
			else if (AbilityId == TEXT("nightmare")) AbilityPart += 3.5f;
			else if (AbilityId == TEXT("curse_ritual")) AbilityPart += Value * 1.5f;
			else if (AbilityId == TEXT("drain")) AbilityPart += Value * 0.65f;
			else if (AbilityId == TEXT("summon")) AbilityPart += 3.0f;
			else if (!AbilityId.IsEmpty()) AbilityPart += 0.5f;
		}

		if (Enemy.Tier == TEXT("elite")) AbilityPart += 3.0f;
		else if (Enemy.Tier == TEXT("boss")) AbilityPart += 8.0f;
		Score += 1.f + HPPart + IntentPart + AbilityPart;
	}
	return FMath::Max(1.f, Score);
}

FCombatDifficultyProfile UCombatEngine::BuildDifficultyProfile(int32 BattleSerial,
	float PreviousThreatScore, float PreviousHPScale, float PreviousIntentScale,
	const TArray<FEnemyData>& EnemyTemplates, bool bElite, bool bBoss)
{
	FCombatDifficultyProfile Profile;
	Profile.BattleSerial = FMath::Max(1, BattleSerial);
	Profile.bElite = bElite;
	Profile.bBoss = bBoss;
	Profile.Tier = bBoss ? TEXT("boss") : (bElite ? TEXT("elite") : TEXT("normal"));
	Profile.PreviousThreatScore = FMath::Max(0.f, PreviousThreatScore);
	Profile.PreviousHPScale = FMath::Max(0.f, PreviousHPScale);
	Profile.PreviousIntentScale = FMath::Max(0.f, PreviousIntentScale);
	Profile.TemplateThreat = ComputeTemplateThreat(EnemyTemplates);

	// A gentle continuous slope plus discrete every-third-battle steps keeps
	// long infinite runs moving without making early card rewards obsolete.
	const int32 SerialOffset = Profile.BattleSerial - 1;
	const int32 SerialStep = SerialOffset / 3;
	const float ContinuousPressure = 1.f + SerialOffset * 0.065f;
	const float TemplateBias = FMath::Clamp(Profile.TemplateThreat / 12.f, 0.72f, 1.55f) - 1.f;
	const float TierStep = bBoss ? 0.58f : (bElite ? 0.23f : 0.f);

	const float CandidateHP = 0.93f + ContinuousPressure * 0.085f
		+ TemplateBias * 0.035f + TierStep;
	const float CandidateIntent = 0.94f + ContinuousPressure * 0.052f
		+ TemplateBias * 0.022f + TierStep * 0.60f;
	const float CandidateThreat = Profile.TemplateThreat * ContinuousPressure
		+ static_cast<float>(SerialStep) * 1.50f + TierStep * 8.f;

	// These floors are the important invariant.  They deliberately use a
	// visible epsilon rather than a post-hoc score-only correction, ensuring
	// actual HP and intent output cannot retreat after an elite or Boss step.
	Profile.HPScale = FMath::Max(CandidateHP,
		Profile.PreviousHPScale > 0.f ? Profile.PreviousHPScale * 1.08f : 0.f);
	Profile.IntentScale = FMath::Max(CandidateIntent,
		Profile.PreviousIntentScale > 0.f ? Profile.PreviousIntentScale * 1.05f : 0.f);
	Profile.ThreatScore = FMath::Max(CandidateThreat,
		Profile.PreviousThreatScore > 0.f ? Profile.PreviousThreatScore * 1.08f : 0.01f);
	Profile.DifficultyStep = SerialStep + (bBoss ? 2 : (bElite ? 1 : 0));
	return Profile;
}

bool UCombatEngine::ValidateDifficultyProgression(const FCombatDifficultyProfile& Previous,
	const FCombatDifficultyProfile& Current, FString& OutError)
{
	OutError.Reset();
	if (Current.BattleSerial <= Previous.BattleSerial)
	{
		OutError = TEXT("战斗序号未严格递增");
		return false;
	}
	// A zero-serial profile is the explicit sentinel for the first encounter.
	// FCombatDifficultyProfile's display defaults (HP/Intent = 1) must not be
	// mistaken for a previous authoritative combat, otherwise a valid first
	// profile would be required to start at 1.08x/1.05x before any battle exists.
	const bool bHasPreviousProfile = Previous.BattleSerial > 0;
	if (bHasPreviousProfile)
	{
		if (!(Current.HPScale > Previous.HPScale)
			|| (Current.HPScale < Previous.HPScale * 1.08f))
		{
			OutError = TEXT("HPScale 未达到每战至少 +8% 的递增");
			return false;
		}
		if (!(Current.IntentScale > Previous.IntentScale)
			|| (Current.IntentScale < Previous.IntentScale * 1.05f))
		{
			OutError = TEXT("IntentScale 未达到每战至少 +5% 的递增");
			return false;
		}
		if (!(Current.ThreatScore > Previous.ThreatScore)
			|| (Current.ThreatScore < Previous.ThreatScore * 1.08f))
		{
			OutError = TEXT("ThreatScore 未达到每战至少 +8% 的递增");
			return false;
		}
	}
	else if (Current.HPScale <= 0.f || Current.IntentScale <= 0.f || Current.ThreatScore <= 0.f)
	{
		OutError = TEXT("首场难度档案含有非正数值");
		return false;
	}
	return true;
}

bool UCombatEngine::InitData(FString& OutError)
{
	TArray<FCardData> Cards;
	if (!UGameDataLibrary::LoadCards(Cards, OutError)) return false;
	CardTable.Reset();
	for (const FCardData& C : Cards) CardTable.Add(C.Id, C);

	TArray<FEnemyData> EnemiesArr;
	if (!UGameDataLibrary::LoadEnemies(EnemiesArr, OutError)) return false;
	EnemyTable.Reset();
	for (const FEnemyData& E : EnemiesArr) EnemyTable.Add(E.Id, E);

	TArray<FRelicData> Relics;
	if (!UGameDataLibrary::LoadRelics(Relics, OutError)) return false;
	RelicTable.Reset();
	for (const FRelicData& R : Relics) RelicTable.Add(R.Id, R);

	TArray<FPillData> Pills;
	if (!UGameDataLibrary::LoadPills(Pills, OutError)) return false;
	PillTable.Reset();
	for (const FPillData& P : Pills) PillTable.Add(P.Id, P);

	return true;
}

void UCombatEngine::RegisterRuntimeEnemies(const TArray<FEnemyData>& RuntimeEnemies)
{
	for (const FEnemyData& Enemy : RuntimeEnemies)
	{
		if (!Enemy.Id.IsEmpty()) RuntimeEnemyOverrides.Add(Enemy);
	}
}

void UCombatEngine::RegisterRuntimePlayerContent(const TArray<FCardData>& RuntimeCards,
	const TArray<FRelicData>& RuntimeRelics)
{
	RuntimeCardOverrides = RuntimeCards;
	RuntimeRelicOverrides = RuntimeRelics;
}

// -----------------------------------------------------------
// 战斗开始
// -----------------------------------------------------------

FCardInstance UCombatEngine::MakeCard(const FString& CardId) const
{
	FCardInstance Inst;
	if (const FCardData* Found = CardTable.Find(CardId))
	{
		Inst.Data = *Found;
		Inst.UID = const_cast<UCombatEngine*>(this)->NextCardUID++;
	}
	return Inst;
}

bool UCombatEngine::StartCombatWithDifficulty(const TArray<FDeckCard>& Deck,
	const TArray<FString>& EnemyIds, const TArray<FString>& OwnedRelicIds,
	int32 PlayerMaxHP, int32 PlayerCurrentHP, int32 EnemyHPBonus, int32 Seed,
	const FCombatDifficultyProfile& Difficulty)
{
	if (Difficulty.BattleSerial <= 0 || Difficulty.HPScale <= 0.f
		|| Difficulty.IntentScale <= 0.f || Difficulty.ThreatScore <= 0.f)
	{
		UE_LOG(LogTemp, Error, TEXT("[Combat] 无效的权威难度档案，拒绝开始战斗"));
		return false;
	}
	PendingDifficultyProfile = Difficulty;
	bHasPendingDifficultyProfile = true;
	const bool bStarted = StartCombat(Deck, EnemyIds, OwnedRelicIds, PlayerMaxHP,
		PlayerCurrentHP, EnemyHPBonus, Seed, Difficulty.DifficultyStep,
		Difficulty.BattleSerial, Difficulty.PreviousThreatScore,
		Difficulty.PreviousHPScale, Difficulty.PreviousIntentScale,
		Difficulty.bElite, Difficulty.bBoss);
	bHasPendingDifficultyProfile = false;
	return bStarted;
}

bool UCombatEngine::StartCombat(const TArray<FDeckCard>& Deck, const TArray<FString>& EnemyIds,
	const TArray<FString>& OwnedRelicIds, int32 PlayerMaxHP, int32 PlayerCurrentHP,
	int32 EnemyHPBonus, int32 Seed, int32 EnemyLevel, int32 BattleSerial,
	float PreviousBattleThreatScore, float PreviousBattleHPScale,
	float PreviousBattleIntentScale, bool bEliteEncounter, bool bBossEncounter)
{
	if (CardTable.Num() == 0)
	{
		FString Err;
		if (!InitData(Err))
		{
			Log(FString::Printf(TEXT("数据加载失败: %s"), *Err));
			return false;
		}
	}

	// 运行时变体覆盖静态 JSON 中的同名条目（通常是 proc_* 唯一 ID）。
	for (const FEnemyData& RuntimeEnemy : RuntimeEnemyOverrides)
	{
		if (!RuntimeEnemy.Id.IsEmpty()) EnemyTable.Add(RuntimeEnemy.Id, RuntimeEnemy);
	}
	RuntimeEnemyOverrides.Reset();
	for (const FCardData& RuntimeCard : RuntimeCardOverrides)
	{
		if (!RuntimeCard.Id.IsEmpty()) CardTable.Add(RuntimeCard.Id, RuntimeCard);
	}
	for (const FRelicData& RuntimeRelic : RuntimeRelicOverrides)
	{
		if (!RuntimeRelic.Id.IsEmpty()) RelicTable.Add(RuntimeRelic.Id, RuntimeRelic);
	}
	RuntimeCardOverrides.Reset();
	RuntimeRelicOverrides.Reset();

	// Resolve the profile after runtime enemy overrides have been installed so
	// LLM/procedural templates retain their own mechanics in the threat score.
	bHasPendingDifficultyProfile = bHasPendingDifficultyProfile
		&& PendingDifficultyProfile.BattleSerial > 0;
	if (bHasPendingDifficultyProfile)
	{
		DifficultyProfile = PendingDifficultyProfile;
	}
	else if (BattleSerial > 0)
	{
		TArray<FEnemyData> Templates;
		bool bResolvedElite = bEliteEncounter;
		bool bResolvedBoss = bBossEncounter;
		for (const FString& EnemyId : EnemyIds)
		{
			if (const FEnemyData* Found = EnemyTable.Find(EnemyId))
			{
				Templates.Add(*Found);
				bResolvedElite = bResolvedElite || Found->Tier == TEXT("elite");
				bResolvedBoss = bResolvedBoss || Found->Tier == TEXT("boss");
			}
		}
		DifficultyProfile = BuildDifficultyProfile(BattleSerial,
			PreviousBattleThreatScore, PreviousBattleHPScale,
			PreviousBattleIntentScale, Templates, bResolvedElite, bResolvedBoss);
	}
	else
	{
		// Legacy/test callers intentionally retain the pre-curve behavior.  The
		// zero serial is also an explicit signal that no persistent profile exists.
		DifficultyProfile = FCombatDifficultyProfile();
	}

	Rng.Initialize(Seed);
	NextCardUID = 1;
	EnemyDamageEvents.Reset();

	// 玩家
	Player = FCombatantState();
	Player.Name = TEXT("你");
	Player.MaxHP = PlayerMaxHP;
	Player.HP = (PlayerCurrentHP >= 0) ? FMath::Clamp(PlayerCurrentHP, 1, PlayerMaxHP) : PlayerMaxHP;
	Spirit = 0;
	SpiritCarryOver = 0;
	Toxicity = 0;
	TurnCount = 0;
	bVictory = false;
	bPlayerTurnSkipped = false;
	bCombatActive = true;
	bPlayerAttackedThisTurn = false;
	PlayingCardUID = -1;
	bFreeBasicGongfaThisTurn = false;
	BasicGongfaPlayedThisCombat = 0;
	FlowDrawsThisTurn = 0;
	ActivePowerIds.Reset();
	ActivePowerNames.Reset();
	PendingDiscoverChoices.Reset();
	CounterDamage = 0;
	// One Sword's enhancement is combat-local.  Combat controllers normally
	// create a fresh engine per encounter, but StartCombat is also a public
	// reuse path (and commandlets do reuse engines); never carry prior combat
	// enhancement into a new settlement.
	OneSwordEnhance = 0;

	// 卡组
	DrawPile.Reset();
	Hand.Reset();
	DiscardPile.Reset();
	ExhaustPile.Reset();
	for (const FDeckCard& DC : Deck)
	{
		FCardInstance C = MakeCard(DC.CardId);
		if (C.Data.Id.IsEmpty())
		{
			Log(FString::Printf(TEXT("警告: 卡组中未知卡牌ID '%s'，已跳过"), *DC.CardId));
			continue;
		}
		C.bUpgraded = DC.bUpgraded;
		DrawPile.Add(C);
	}
	ShuffleArray(DrawPile, Rng);

	// 敌人
	Enemies.Reset();
	for (const FString& Id : EnemyIds)
	{
		const FEnemyData* Found = EnemyTable.Find(Id);
		if (!Found)
		{
			Log(FString::Printf(TEXT("错误: 未知敌人ID '%s'"), *Id));
			bCombatActive = false;
			return false;
		}
		FEnemyCombatant E;
		E.Data = *Found;
		E.State = FCombatantState();
		E.State.EnemyId = Found->Id;
		E.LevelBonus = FMath::Clamp(EnemyLevel, 0, 3);

		// 强化前缀名（凶/厉/煞）
		static const TCHAR* LevelPrefix[] = {TEXT(""), TEXT("凶·"), TEXT("厉·"), TEXT("煞·")};
		E.State.Name = FString(LevelPrefix[E.LevelBonus]) + Found->Name;

		if (DifficultyProfile.BattleSerial > 0)
		{
			// The profile is the sole numerical authority for new encounters.  The
			// old EnemyLevel remains available for display/compatibility, but no
			// second multiplicative curve is applied here.
			E.State.MaxHP = FMath::Clamp(FMath::RoundToInt(
				(Found->MaxHP + EnemyHPBonus) * DifficultyProfile.HPScale), 1, 100000);
			for (FEnemyIntent& Intent : E.Data.Intents)
			{
				if (Intent.Value > 0)
					Intent.Value = FMath::Clamp(FMath::RoundToInt(Intent.Value * DifficultyProfile.IntentScale), 1, 10000);
				if (Intent.StatusStacks > 0)
					Intent.StatusStacks = FMath::Clamp(FMath::RoundToInt(
						Intent.StatusStacks * FMath::Sqrt(DifficultyProfile.IntentScale)), 1, 1000);
			}
		}
		else
		{
			// Legacy callers: Level 0=×1.0 1=×1.7 2=×2.6 3=×4.0.
			static const float LevelMult[] = {1.0f, 1.7f, 2.6f, 4.0f};
			const int32 LvlClamped = FMath::Clamp(E.LevelBonus, 0, 3);
			E.State.MaxHP = FMath::RoundToInt((Found->MaxHP + EnemyHPBonus) * LevelMult[LvlClamped]);
		}
		E.State.HP = E.State.MaxHP;
		Enemies.Add(E);
	}

	// 法宝
	ActiveRelics.Reset();
	RelicCounters.Reset();
	ActiveCardRules.Reset();
	ScriptVariables.Reset();
	CurrentScriptEventValue = 0;
	CurrentScriptEventTag.Reset();
	ScriptEventDepth = 0;
	LastPlayedCard = FCardInstance();
	for (const FString& Id : OwnedRelicIds)
	{
		if (const FRelicData* R = RelicTable.Find(Id))
		{
			ActiveRelics.Add(*R);
			FRelicRuntimeState RS;
			RS.RelicId = Id;
			RS.CurrentCount = 0;
			RelicCounters.Add(RS);
		}
	}
	CardsPlayedThisTurn = 0;
	LastTurnHandSize = 0;

	RefreshHandLimit();
	if (HandLimitReduction > 0)
		Log(FString::Printf(TEXT("手牌上限减少 %d"), HandLimitReduction));

	Log(TEXT("====== 战斗开始 ======"));
	if (DifficultyProfile.BattleSerial > 0)
	{
		Log(FString::Printf(TEXT("难度序号 #%d | %s台阶%d | HP×%.3f | 意图×%.3f | 威胁 %.2f"),
			DifficultyProfile.BattleSerial, *DifficultyProfile.Tier,
			DifficultyProfile.DifficultyStep, DifficultyProfile.HPScale,
			DifficultyProfile.IntentScale, DifficultyProfile.ThreatScore));
	}
	for (const FEnemyCombatant& E : Enemies)
	{
		Log(FString::Printf(TEXT("遭遇敌人: %s (HP %d)"), *E.State.Name, E.State.HP));
		if (!E.Data.Story.IsEmpty()) Log(FString::Printf(TEXT("  「%s」"), *E.Data.Story));
	}

	// 战斗开始法宝
	TriggerRelics(TEXT("combat_start"));

	// 敌人初始意图
	for (FEnemyCombatant& E : Enemies) RollIntent(E);

	// ---- 敌人开场机制 ----
	for (FEnemyCombatant& E : Enemies)
	{
		if (GetEnemyAbilityValue(E.Data, TEXT("start_weak")) > 0)
		{
			ApplyStatusTo(Player, TEXT("weak"), GetEnemyAbilityValue(E.Data, TEXT("start_weak")), E.State.Name);
		}
		if (GetEnemyAbilityValue(E.Data, TEXT("start_vulnerable")) > 0)
		{
			ApplyStatusTo(Player, TEXT("vulnerable"), GetEnemyAbilityValue(E.Data, TEXT("start_vulnerable")), E.State.Name);
		}
		if (GetEnemyAbilityValue(E.Data, TEXT("first_strike")) > 0)
		{
			Log(FString::Printf(TEXT("%s 抢先出手!"), *E.State.Name));
			ExecuteEnemyIntent(E);
			if (bCombatActive) RollIntent(E); // 重新掷意图，避免与首回合行动重复
		}
	}
	CheckCombatEnd();

	StartPlayerTurn();
	return true;
}

// -----------------------------------------------------------
// 回合流程
// -----------------------------------------------------------

void UCombatEngine::DrawCards(int32 Count)
{
	int32 DrawnCount = 0;
	for (int32 i = 0; i < Count; ++i)
	{
		if (DrawPile.Num() == 0)
		{
			if (DiscardPile.Num() == 0) break;
			DrawPile = DiscardPile;
			DiscardPile.Reset();
			ShuffleArray(DrawPile, Rng);
			Log(TEXT("弃牌堆洗回抽牌堆"));
			TriggerRelics(TEXT("on_reshuffle"));
			int32 ReshuffleEventValue = DrawPile.Num();
			TriggerCardRules(TEXT("on_reshuffle"), ReshuffleEventValue);
		}
		Hand.Add(DrawPile.Pop());
		DrawnCount++;
		IncrementHandCounters(TEXT("on_draw"));
	}
	PendingDrawCount += DrawnCount;
	if (DrawnCount > 0)
	{
		int32 DrawEventValue = DrawnCount;
		TriggerCardRules(TEXT("on_draw"), DrawEventValue);
	}
}

void UCombatEngine::BeginDiscoverFromDrawPile(int32 SampleCount)
{
	PendingDiscoverChoices.Reset();
	if (DrawPile.Num() == 0)
	{
		Log(TEXT("抽牌堆为空，无法获得指引"));
		return;
	}
	TArray<int32> Available;
	for (int32 Index = 0; Index < DrawPile.Num(); ++Index) Available.Add(Index);
	const int32 Count = FMath::Clamp(SampleCount, 1, FMath::Min(5, DrawPile.Num()));
	for (int32 Pick = 0; Pick < Count; ++Pick)
	{
		const int32 Slot = Rng.RandRange(0, Available.Num() - 1);
		PendingDiscoverChoices.Add(DrawPile[Available[Slot]]);
		Available.RemoveAtSwap(Slot);
	}
	Log(FString::Printf(TEXT("从抽牌堆的 %d 张候选中选择 1 张加入手牌"), PendingDiscoverChoices.Num()));
}

bool UCombatEngine::ResolveDiscoverChoice(int32 ChoiceIndex)
{
	if (!PendingDiscoverChoices.IsValidIndex(ChoiceIndex)) return false;
	const FCardInstance Chosen = PendingDiscoverChoices[ChoiceIndex];
	const int32 DrawIndex = DrawPile.IndexOfByPredicate(
		[&Chosen](const FCardInstance& Card) { return Card.UID == Chosen.UID; });
	if (DrawIndex == INDEX_NONE)
	{
		PendingDiscoverChoices.Reset();
		Log(TEXT("候选牌已不在抽牌堆，选择取消"));
		return false;
	}
	Hand.Add(DrawPile[DrawIndex]);
	DrawPile.RemoveAt(DrawIndex);
	PendingDiscoverChoices.Reset();
	PendingDrawCount += 1;
	IncrementHandCounters(TEXT("on_draw"));
	Log(FString::Printf(TEXT("指引生效：将【%s】加入手牌"), *Chosen.GetDisplayName()));
	return true;
}

void UCombatEngine::StartPlayerTurn()
{
	if (!bCombatActive) return;
	bPlayerTurnSkipped = false;

	TurnCount++;
	if (TurnCount > 1)
	{
		// 常规回合：罡气消散，灵力回满。
		// 首回合不重置——combat_start 法宝的罡气/灵力加成在 StartCombat 时已赋予，需保留。
		Player.Block = 0;
		Spirit = MaxSpirit;
		// 临时力量消散
		const int32 TS = Player.GetStatusStacks(TEXT("temp_strength"));
		if (TS > 0)
		{
			Player.AddStatus(TEXT("strength"), -TS);
			Player.AddStatus(TEXT("temp_strength"), -TS);
			Log(TEXT("临时力量消散"));
		}
	}
	else
	{
		Spirit += MaxSpirit;
	}

	// 储灵结转（spirit_next_turn 卡牌）
	if (SpiritCarryOver > 0)
	{
		Spirit += SpiritCarryOver;
		Log(FString::Printf(TEXT("储灵生效: 额外获得 %d 点灵力"), SpiritCarryOver));
		SpiritCarryOver = 0;
	}

	bPlayerAttackedThisTurn = false;
	bFreeBasicGongfaThisTurn = false;
	FlowDrawsThisTurn = 0;
	ResetTurnState();
	int32 TurnEventValue = TurnCount;
	TriggerCardRules(TEXT("on_turn_start"), TurnEventValue);

	// 功法「剑罡护体功」: 每个独立实例在回合开始时都结算一次。
	const int32 SwordGangCount = GetPowerCount(TEXT("sword_gang"));
	if (SwordGangCount > 0)
	{
		const int32 Intent = Player.GetStatusStacks(TEXT("strength"));
		if (Intent > 0)
		{
			Log(FString::Printf(TEXT("功法【剑罡护体功】运转: %d 重功法将剑意化为护体罡气"), SwordGangCount));
			ApplyBlock(Player, Intent * 2 * SwordGangCount);
		}
	}

	Log(FString::Printf(TEXT("--- 第 %d 回合 --- 灵力 %d/%d, HP %d/%d"), TurnCount, Spirit, MaxSpirit, Player.HP, Player.MaxHP));

	// 丹毒攻心
	if (Toxicity >= 5)
	{
		Player.HP -= Toxicity;
		Log(FString::Printf(TEXT("丹毒攻心! 受到 %d 点丹毒伤害 (HP %d/%d)"), Toxicity, Player.HP, Player.MaxHP));
		if (Player.HP <= 0) { Player.HP = 0; CheckCombatEnd(); return; }
	}

	// 灼烧/中毒
	if (TickDotStatuses(Player)) { CheckCombatEnd(); return; }

	TriggerRelics(TEXT("turn_start"));

	CardsPlayedThisTurn = 0;

	// 每个存活的玩家回合都先建立手牌。梦魇只禁止行动，不应让手牌留在空状态。
	DrawCards(5);

	// 功法「剑仙真解」: 每个独立实例每回合额外抽2张
	const int32 ImmortalDrawCount = GetPowerCount(TEXT("immortal_draw"));
	if (ImmortalDrawCount > 0)
	{
		const int32 ExtraDraws = ImmortalDrawCount * 2;
		Log(FString::Printf(TEXT("功法【剑仙真解】运转: %d 重功法额外抽%d张牌"), ImmortalDrawCount, ExtraDraws));
		DrawCards(ExtraDraws);
	}
	IncrementHandCounters(TEXT("on_turn_start"));

	// 梦魇: 累积3层则跳过此回合
	if (Player.GetStatusStacks(TEXT("nightmare")) >= 3)
	{
		Log(TEXT("梦魇缠身! 你陷入无尽噩梦，跳过此回合"));
		Player.AddStatus(TEXT("nightmare"), -3);
		bPlayerTurnSkipped = true;
		Log(FString::Printf(TEXT("梦魇回合仍抽取手牌：当前 %d 张；点击结束回合后恢复行动"), Hand.Num()));
		return;
	}
	FString HandStr;
	for (int32 i = 0; i < Hand.Num(); ++i)
	{
		HandStr += FString::Printf(TEXT("[%d]%s(%d费) "), i, *Hand[i].GetDisplayName(), Hand[i].GetCost());
	}
	Log(FString::Printf(TEXT("手牌: %s"), *HandStr));
	for (int32 i = 0; i < Enemies.Num(); ++i)
	{
		const FEnemyCombatant& E = Enemies[i];
		if (!E.State.IsAlive()) continue;
		FString IntentStr;
		if (E.CurrentIntent.Action == TEXT("attack"))
			IntentStr = FString::Printf(TEXT("攻击 %d"), E.CurrentIntent.Value);
		else if (E.CurrentIntent.Action == TEXT("attack_multi"))
			IntentStr = FString::Printf(TEXT("攻击 %dx%d"), E.CurrentIntent.Value, E.CurrentIntent.Times);
		else if (E.CurrentIntent.Action == TEXT("defend"))
			IntentStr = FString::Printf(TEXT("防御 %d"), E.CurrentIntent.Value);
		else if (E.CurrentIntent.Action == TEXT("buff"))
			IntentStr = FString::Printf(TEXT("强化 %s+%d"), *E.CurrentIntent.StatusId, E.CurrentIntent.StatusStacks);
		else if (E.CurrentIntent.Action == TEXT("debuff"))
			IntentStr = FString::Printf(TEXT("削弱 %s+%d"), *E.CurrentIntent.StatusId, E.CurrentIntent.StatusStacks);
		else if (E.CurrentIntent.Action == TEXT("heal_ally"))
			IntentStr = FString::Printf(TEXT("治疗盟友 %d"), E.CurrentIntent.Value);
		else if (E.CurrentIntent.Action == TEXT("buff_ally"))
			IntentStr = FString::Printf(TEXT("强化盟友 %s+%d"), *E.CurrentIntent.StatusId, E.CurrentIntent.StatusStacks);
		Log(FString::Printf(TEXT("  敌人[%d] %s HP %d/%d 罡气 %d 意图: %s"),
			i, *E.State.Name, E.State.HP, E.State.MaxHP, E.State.Block, *IntentStr));
	}
}

void UCombatEngine::EndPlayerTurn()
{
	if (!bCombatActive) return;
	if (PendingDiscoverChoices.Num() > 0)
	{
		Log(TEXT("请先完成候选牌选择"));
		return;
	}
	UE_LOG(LogTemp, Display, TEXT("[DIAG] EndPlayerTurn start"));

	LastTurnHandSize = Hand.Num();
	PendingTurnEndDiscardUIDs.Reset();
	int32 TurnEventValue = TurnCount;
	TriggerCardRules(TEXT("on_turn_end"), TurnEventValue);
	TriggerRelics(TEXT("on_player_turn_end"));

	// 弃掉手牌（「保留」牌留在手中）
	int32 DiscardedCount = 0;
	{
		TArray<FCardInstance> Kept;
		for (FCardInstance& C : Hand)
		{
			if (C.Data.bRetain) Kept.Add(C);
			else
			{
				C.RepeatCount = 0;
				PendingTurnEndDiscardUIDs.Add(C.UID);
				DiscardPile.Add(C);
				DiscardedCount++;
			}
		}
		Hand = Kept;
	}
	if (DiscardedCount > 0)
	{
		TriggerRelicsWithValue(TEXT("on_cards_discarded"), DiscardedCount);
		int32 DiscardEventValue = DiscardedCount;
		TriggerCardRules(TEXT("on_discard"), DiscardEventValue);
	}

	// 手牌上限: 若敌人有 hand_limit 能力，弃牌至上限
	const int32 HandCap = FMath::Max(0, 10 - HandLimitReduction);
	if (Hand.Num() > HandCap)
	{
		int32 Excess = Hand.Num() - HandCap;
		for (int32 e = 0; e < Excess; ++e)
		{
			int32 DropIdx = Rng.RandRange(0, Hand.Num() - 1);
			Hand[DropIdx].RepeatCount = 0;
			PendingTurnEndDiscardUIDs.Add(Hand[DropIdx].UID);
			DiscardPile.Add(Hand[DropIdx]);
			Hand.RemoveAt(DropIdx);
		}
		Log(FString::Printf(TEXT("手牌上限 %d: 弃掉 %d 张多余牌"), HandCap, Excess));
	}

	DecayStatuses(Player);

	// 功法「双刃剑意诀」: 回合结束本回合未获力量则剑意减半
	if (HasPower(TEXT("double_strength")) && !bGotStrengthThisTurn)
	{
		const int32 Cur = Player.GetStatusStacks(TEXT("strength"));
		if (Cur > 0)
		{
			const int32 Half = FMath::Max(1, Cur / 2);
			Player.AddStatus(TEXT("strength"), -Half);
			Log(FString::Printf(TEXT("功法【双刃剑意诀】: 本回合未获力量，剑意减半 (%d→%d)"), Cur, Cur - Half));
		}
	}

	Log(TEXT("你结束了回合"));

	UE_LOG(LogTemp, Display, TEXT("[DIAG] EndPlayerTurn RunEnemyPhase begin"));
	RunEnemyPhase();
	UE_LOG(LogTemp, Display, TEXT("[DIAG] EndPlayerTurn RunEnemyPhase done"));
	CheckCombatEnd();

	if (bCombatActive)
	{
		UE_LOG(LogTemp, Display, TEXT("[DIAG] EndPlayerTurn StartPlayerTurn begin"));
		StartPlayerTurn();
		UE_LOG(LogTemp, Display, TEXT("[DIAG] EndPlayerTurn StartPlayerTurn done"));
	}
}

void UCombatEngine::RunEnemyPhase()
{
	for (FEnemyCombatant& E : Enemies)
	{
		if (!E.State.IsAlive()) continue;

		// 灼烧/中毒（敌人回合开始时）
		if (TickDotStatuses(E.State))
		{
			Log(FString::Printf(TEXT("%s 倒下了"), *E.State.Name));
			TriggerRelics(TEXT("on_kill"));
			continue;
		}

		// ---- 敌人回合开始机制 ----
		const int32 RegenN = GetEnemyAbilityValue(E.Data, TEXT("regen"));
		if (RegenN > 0 && E.State.HP < E.State.MaxHP)
		{
			const int32 Healed = FMath::Min(RegenN, E.State.MaxHP - E.State.HP);
			E.State.HP += Healed;
			Log(FString::Printf(TEXT("  %s 恢复 %d 点气血 (HP %d/%d)"), *E.State.Name, Healed, E.State.HP, E.State.MaxHP));
		}
		const int32 BlockN = GetEnemyAbilityValue(E.Data, TEXT("block_aura"));
		if (BlockN > 0)
		{
			ApplyBlock(E.State, BlockN);
		}
		const int32 PoisonAuraN = GetEnemyAbilityValue(E.Data, TEXT("poison_aura"));
		if (PoisonAuraN > 0 && Player.IsAlive())
		{
			ApplyStatusTo(Player, TEXT("poison"), PoisonAuraN, E.State.Name);
		}

		// 怨灵缠身: 洗诅咒牌进玩家抽牌堆
		const int32 CurseN = GetEnemyAbilityValue(E.Data, TEXT("curse_ritual"));
		for (int32 c = 0; c < CurseN; ++c)
		{
			FCardInstance CurseCard = MakeCard(TEXT("curse_regret"));
			DrawPile.Insert(CurseCard, Rng.RandRange(0, DrawPile.Num()));
			Log(FString::Printf(TEXT("  怨念: 一张【怨念】混入了你的牌库")));
		}

		// 召唤: summon:enemy_id:interval
		FString SummonId; int32 SummonInterval = 0;
		{
			FString Raw = GetEnemyAbilityValueRaw(E.Data, TEXT("summon"));
			if (!Raw.IsEmpty())
			{
				TArray<FString> Parts;
				Raw.ParseIntoArray(Parts, TEXT(":"));
				if (Parts.Num() >= 2) { SummonId = Parts[0]; SummonInterval = FCString::Atoi(*Parts[1]); }
			}
		}
		if (!SummonId.IsEmpty() && SummonInterval > 0 && TurnCount % SummonInterval == 0 && GetAliveEnemyIndices().Num() < 6)
		{
			FEnemyCombatant Summoned = SpawnEnemy(SummonId);
			Enemies.Add(Summoned);
			Log(FString::Printf(TEXT("  %s 召唤出 %s!"), *E.State.Name, *Summoned.State.Name));
			RefreshHandLimit();
		}

		// 梦魇: 每回合施加梦魇
		if (GetEnemyAbilityValue(E.Data, TEXT("nightmare")) > 0 && Player.IsAlive())
		{
			ApplyStatusTo(Player, TEXT("nightmare"), 1, E.State.Name);
		}

		ExecuteEnemyIntent(E);
		DecayStatuses(E.State);

		if (!Player.IsAlive()) return;

		// 行动后掷下一个意图
		RollIntent(E);
	}
}

// -----------------------------------------------------------
// 出牌
// -----------------------------------------------------------

TArray<int32> UCombatEngine::GetPlayableCardIndices() const
{
	TArray<int32> Result;
	if (bPlayerTurnSkipped) return Result;
	for (int32 i = 0; i < Hand.Num(); ++i)
	{
		if (GetEffectiveCost(Hand[i]) <= Spirit) Result.Add(i);
	}
	return Result;
}

int32 UCombatEngine::GetEffectiveCost(const FCardInstance& Card) const
{
	if (bFreeBasicGongfaThisTurn &&
		(Card.Data.Type == TEXT("basic") || Card.Data.Type == TEXT("gongfa")))
	{
		return 0;
	}
	// 一剑归真诀 / 化剑为气: 本回合一剑免费
	if (bOneSwordFreeThisTurn && Card.Data.Id == TEXT("one_sword"))
	{
		return 0;
	}
	// 万剑归宗: 本回合下一张招式减费
	int32 Cost = Card.GetCost();
	if (NextZhaoshiCostReduce > 0 && Card.Data.Type == TEXT("zhaoshi") && Card.Data.Id != TEXT("one_sword"))
	{
		Cost = FMath::Max(0, Cost - NextZhaoshiCostReduce);
	}
	return Cost;
}

int32 UCombatEngine::GetPowerCount(const FString& PowerId) const
{
	if (PowerId.IsEmpty()) return 0;

	int32 Count = 0;
	for (const FString& ActiveId : ActivePowerIds)
	{
		if (ActiveId == PowerId) ++Count;
	}
	return Count;
}

void UCombatEngine::AddPower(const FString& PowerId, const FString& SourceCardName)
{
	if (PowerId.IsEmpty()) return;

	// 功法是“独立实例”而不是布尔开关：同一张功法可以再次打出，
	// 每次注册都保留一个实例，后续触发按实例数量分别结算。
	ActivePowerIds.Add(PowerId);
	ActivePowerNames.Add(SourceCardName);
	const int32 InstanceCount = GetPowerCount(PowerId);
	Log(FString::Printf(TEXT("功法运转: 【%s】第%d重（当前%d重，持续至本场战斗结束）"),
		*SourceCardName, InstanceCount, InstanceCount));
}

bool UCombatEngine::PlayCard(int32 HandIndex, int32 TargetEnemyIndex)
{
	if (!bCombatActive) return false;
	if (PendingDiscoverChoices.Num() > 0)
	{
		Log(TEXT("请先从地图揭示的候选中选择一张牌"));
		return false;
	}
	if (bPlayerTurnSkipped)
	{
		Log(TEXT("梦魇缠身，本回合无法出牌"));
		return false;
	}
	if (!Hand.IsValidIndex(HandIndex)) return false;

	const FCardInstance Card = Hand[HandIndex];
	const int32 EffectiveCost = GetEffectiveCost(Card);
	if (EffectiveCost > Spirit)
	{
		Log(FString::Printf(TEXT("灵力不足，无法打出 %s"), *Card.GetDisplayName()));
		return false;
	}

	// 需要目标的卡校验
	bool bNeedsTarget = false;
	for (const FCardEffect& E : Card.GetEffects())
	{
		if (E.Target == TEXT("enemy")) { bNeedsTarget = true; break; }
	}
	if (bNeedsTarget)
	{
		if (!Enemies.IsValidIndex(TargetEnemyIndex) || !Enemies[TargetEnemyIndex].State.IsAlive())
		{
			TArray<int32> Alive = GetAliveEnemyIndices();
			if (Alive.Num() == 0) return false;
			TargetEnemyIndex = Alive[0]; // 自动选第一个存活敌人
		}
	}

	int32 SpendEventValue = EffectiveCost;
	Spirit -= SpendEventValue;
	Log(FString::Printf(TEXT("打出 %s (耗费%d)"), *Card.GetDisplayName(), SpendEventValue));
	LastPlayedCard = Card;

	// 标记正在打出的牌（discard_random 等效果需排除自身）
	PlayingCardUID = Card.UID;
	ExecuteCardEffects(Card, TargetEnemyIndex);
	PlayingCardUID = -1;

	// 移出手牌（按 UID 查找，防止卡牌效果改变了手牌构成）
	for (int32 i = 0; i < Hand.Num(); ++i)
	{
		if (Hand[i].UID == Card.UID)
		{
			if (Card.Data.Id != TEXT("wan_jian_gui_zong"))
				Hand[i].RepeatCount = 0; // 入弃牌堆前清空计数
			Hand.RemoveAt(i);
			break;
		}
	}
	// ---- 一剑系统：归真诀处理 ----
	const bool bIsOneSword = (Card.Data.Id == TEXT("one_sword"));
	const int32 OneSwordReturnCount = GetPowerCount(TEXT("one_sword_return"));
	if (bIsOneSword && OneSwordReturnCount > 0)
	{
		// 归真诀：一剑不消失，放回手牌
		Hand.Add(Card);
		Log(FString::Printf(TEXT("功法【一剑归真诀】%d 重: 一剑不消失，保留在手"), OneSwordReturnCount));

		// 每个独立实例在每回合第一次打出一剑时各生成一张临时复制。
		if (!bFirstOneSwordReturnedThisTurn)
		{
			bFirstOneSwordReturnedThisTurn = true;
			for (int32 CopyIndex = 0; CopyIndex < OneSwordReturnCount; ++CopyIndex)
			{
				FCardInstance Copy = MakeCard(TEXT("one_sword"));
				Copy.Data.bExhaust = true; // 复制体正常消失
				Hand.Add(Copy);
			}
			Log(FString::Printf(TEXT("功法【一剑归真诀】: 生成%d张临时复制【一剑】"), OneSwordReturnCount));
		}
	}
	else if (Card.Data.Id == TEXT("wan_jian_gui_zong"))
	{
		// 万剑归宗：不消失，计数器递增，留在手牌
		FCardInstance Stacked = Card;
		Stacked.RepeatCount++;
		Hand.Add(Stacked);
		Log(FString::Printf(TEXT("万剑归宗: 返回手牌，重复次数 %d"), Stacked.RepeatCount));
	}
	else if (Card.Data.bExhaust)
	{
		ExhaustPile.Add(Card);
		Log(FString::Printf(TEXT("%s 被消耗"), *Card.GetDisplayName()));
		int32 ExhaustEventValue = 1;
		TriggerCardRules(TEXT("on_exhaust"), ExhaustEventValue, TargetEnemyIndex, Card.Data.Type);
	}
	else
	{
		DiscardPile.Add(Card);
	}

	CheckCombatEnd();
	if (!bCombatActive) return true;

	// 法器：出牌后触发
	CardsPlayedThisTurn++;
	TriggerRelicsWithValue(TEXT("on_card_played"), CardsPlayedThisTurn);
	int32 PlayedEventValue = CardsPlayedThisTurn;
	TriggerCardRules(TEXT("on_card_played"), PlayedEventValue, TargetEnemyIndex, Card.Data.Type);
	if (SpendEventValue > 0)
	{
		int32 SpentEventValue = SpendEventValue;
		TriggerCardRules(TEXT("after_spend_spirit"), SpentEventValue, TargetEnemyIndex, Card.Data.Type);
	}

	// ---- 流派钩子：基础卡/功法卡 ----
	const FString& CardType = Card.Data.Type;
	IncrementHandCounters(TEXT("on_card_played"), CardType);
	if (CardType == TEXT("basic") || CardType == TEXT("gongfa"))
	{
		BasicGongfaPlayedThisCombat++;

		// 功法「蓄剑诀」: 每个独立实例各提供1层临时力量
		const int32 SwordIntentCount = GetPowerCount(TEXT("sword_intent"));
		if (SwordIntentCount > 0)
		{
			ApplyTempStrength(Player, SwordIntentCount, TEXT("蓄剑诀"));
		}

		// 功法「流水剑经」: 每个实例每回合最多触发2次，实例之间互不共用上限。
		const int32 FlowSutraCount = GetPowerCount(TEXT("flow_sutra"));
		if (CardType == TEXT("basic") && FlowSutraCount > 0 && FlowDrawsThisTurn < FlowSutraCount * 2)
		{
			const int32 TriggerCount = FMath::Min(FlowSutraCount, FlowSutraCount * 2 - FlowDrawsThisTurn);
			FlowDrawsThisTurn += TriggerCount;
			Log(FString::Printf(TEXT("功法【流水剑经】运转: %d 重功法抽%d张牌"), TriggerCount, TriggerCount));
			DrawCards(TriggerCount);
		}

		// 功法「剑意不绝」: 每个实例每回合最多触发3次，实例之间互不共用上限。
		const int32 SwordArtFlowCount = GetPowerCount(TEXT("sword_art_flow"));
		if (SwordArtFlowCount > 0 && SwordArtFlowTriggeredThisTurn < SwordArtFlowCount * 3)
		{
			const int32 TriggerCount = FMath::Min(SwordArtFlowCount, SwordArtFlowCount * 3 - SwordArtFlowTriggeredThisTurn);
			SwordArtFlowTriggeredThisTurn += TriggerCount;
			OneSwordEnhance += TriggerCount * 2;
			Log(FString::Printf(TEXT("功法【剑意不绝】运转: %d 重功法强化【一剑】+%d (当前强化 %d)"),
				TriggerCount, TriggerCount * 2, OneSwordEnhance));
			EnsureOneSwordInHand();
		}

		// 功法「剑墟遗刻」: 每个实例都在本回合第一张基础卡上提供1层力量。
		const int32 SwordRelicInscriptionCount = GetPowerCount(TEXT("sword_relic_inscription"));
		if (CardType == TEXT("basic") && SwordRelicInscriptionCount > 0 && !bFirstBasicPlayedThisTurn)
		{
			bFirstBasicPlayedThisTurn = true;
			ApplyStatusTo(Player, TEXT("strength"), SwordRelicInscriptionCount, TEXT("剑墟遗刻"));
		}

		// 首张基础卡标记（即使没有剑墟遗刻也要标记防止重复）
		if (CardType == TEXT("basic") && !bFirstBasicPlayedThisTurn)
		{
			bFirstBasicPlayedThisTurn = true;
		}
	}

	// 招式卡也触发剑意不绝；每个实例独立消耗自己的每回合3次额度。
	const int32 SwordArtFlowCount = GetPowerCount(TEXT("sword_art_flow"));
	if (CardType == TEXT("zhaoshi") && SwordArtFlowCount > 0 && SwordArtFlowTriggeredThisTurn < SwordArtFlowCount * 3)
	{
		const int32 TriggerCount = FMath::Min(SwordArtFlowCount, SwordArtFlowCount * 3 - SwordArtFlowTriggeredThisTurn);
		SwordArtFlowTriggeredThisTurn += TriggerCount;
		Log(FString::Printf(TEXT("功法【剑意不绝】运转: %d 重功法强化【一剑】+%d (当前强化 %d)"),
			TriggerCount, TriggerCount * 2, OneSwordEnhance));
		EnsureOneSwordInHand();
	}

	return true;
}

void UCombatEngine::IncrementHandCounters(const FString& Event, const FString& PlayedCardType, int32 Amount)
{
	if (Amount <= 0) return;
	for (FCardInstance& Card : Hand)
	{
		const FString Condition = Card.GetCounterCondition();
		bool bMatches = Condition == Event;
		if (Event == TEXT("on_card_played"))
		{
			bMatches = Condition == TEXT("on_any_card_play")
				|| (Condition == TEXT("on_basic_play") && PlayedCardType == TEXT("basic"))
				|| (Condition == TEXT("on_basic_gongfa_play")
					&& (PlayedCardType == TEXT("basic") || PlayedCardType == TEXT("gongfa")))
				|| (Condition == TEXT("on_same_type_play") && Card.Data.Type == PlayedCardType)
				|| (Condition == TEXT("on_sword_play") && PlayedCardType == TEXT("sword"))
				|| (Condition == TEXT("on_spell_play") && PlayedCardType == TEXT("spell"));
		}
		if (!bMatches) continue;
		Card.RepeatCount += Amount;
		Log(FString::Printf(TEXT("【%s】计数+%d (当前%d)"), *Card.GetDisplayName(), Amount, Card.RepeatCount));
	}
}

bool UCombatEngine::UsePill(const FString& PillId)
{
	if (!bCombatActive || !Player.IsAlive()) return false;
	if (PendingDiscoverChoices.Num() > 0) return false;
	if (bPlayerTurnSkipped)
	{
		Log(TEXT("梦魇缠身，本回合无法服用丹药"));
		return false;
	}

	const FPillData* Pill = PillTable.Find(PillId);
	if (!Pill)
	{
		Log(FString::Printf(TEXT("未知丹药: %s"), *PillId));
		return false;
	}

	// 九转丹鼎：效果增强50%，丹毒+1
	float EffectMult = 1.f;
	int32 BonusToxicity = 0;
	for (const FRelicData& R : ActiveRelics)
	{
		if (R.Trigger == TEXT("pill_modifier"))
		{
			EffectMult += R.Modifier;
			BonusToxicity += 1;
		}
	}

	FCardEffect BoostedEffect = Pill->Effect;
	BoostedEffect.Value = FMath::RoundToInt(BoostedEffect.Value * EffectMult);

	Log(FString::Printf(TEXT("服下 %s%s: %s"), *Pill->Name,
		EffectMult > 1.f ? TEXT(" (丹鼎加持)") : TEXT(""), *Pill->Description));
	ExecuteEffect(BoostedEffect, 0, true);

	const int32 TotalToxicity = Pill->Toxicity + BonusToxicity;
	if (TotalToxicity > 0)
	{
		Toxicity += TotalToxicity;
		Log(FString::Printf(TEXT("丹毒 +%d (当前丹毒 %d)"), TotalToxicity, Toxicity));
		if (Toxicity >= 5)
		{
			Log(FString::Printf(TEXT("警告: 丹毒已深入脏腑，每回合将受到丹毒反噬!")));
		}
	}
	return true;
}

void UCombatEngine::ExecuteCardEffects(const FCardInstance& Card, int32 TargetEnemyIndex)
{
	// 功法「剑心通明经」: 每个独立实例都让基础卡额外生效一次。
	const int32 HeartClearCount = GetPowerCount(TEXT("heart_clear"));
	const int32 Reps = (Card.Data.Type == TEXT("basic")) ? 1 + HeartClearCount : 1;
	if (HeartClearCount > 0)
	{
		Log(FString::Printf(TEXT("功法【剑心通明经】运转: %d 重功法让基础卡额外生效%d次"),
			HeartClearCount, HeartClearCount));
	}

	PendingPowerSourceName = Card.GetDisplayName();
	for (const FCardEffect& E : Card.GetEffects())
	{
		if (!E.Trigger.IsEmpty() && E.Trigger != TEXT("on_play"))
			RegisterCardRule(E, Card.GetDisplayName());
	}
	for (int32 Rep = 0; Rep < Reps; ++Rep)
	{
		for (const FCardEffect& E : Card.GetEffects())
		{
			if (!E.Trigger.IsEmpty() && E.Trigger != TEXT("on_play")) continue;
			ExecuteEffect(E, TargetEnemyIndex, true, &Card);
		}
	}
	PendingPowerSourceName.Empty();
}

void UCombatEngine::RegisterCardRule(const FCardEffect& Effect, const FString& SourceName)
{
	if (Effect.Trigger.IsEmpty() || Effect.Trigger == TEXT("on_play")) return;
	FActiveCardRule Rule;
	Rule.Effect = Effect;
	if (Rule.Effect.Duration.IsEmpty() || Rule.Effect.Duration == TEXT("instant"))
		Rule.Effect.Duration = TEXT("combat");
	Rule.SourceName = SourceName;
	ActiveCardRules.Add(MoveTemp(Rule));
	Log(FString::Printf(TEXT("【%s】建立规则：%s（持续%s）"), *SourceName, *Effect.Trigger,
		Effect.Duration == TEXT("turn") ? TEXT("本回合") : TEXT("本场战斗")));
}

int32 UCombatEngine::ReadScriptValue(const FString& Source, int32 TargetEnemyIndex) const
{
	if (Source == TEXT("event_value")) return CurrentScriptEventValue;
	if (Source == TEXT("turn")) return TurnCount;
	if (Source == TEXT("enemy_count")) return GetAliveEnemyIndices().Num();
	if (Source == TEXT("self_block")) return Player.Block;
	if (Source == TEXT("self_hp")) return Player.HP;
	if (Source == TEXT("missing_hp")) return FMath::Max(0, Player.MaxHP - Player.HP);
	if (Source == TEXT("self_spirit")) return Spirit;
	if (Source == TEXT("hand_size")) return Hand.Num();
	if (Source == TEXT("draw_pile")) return DrawPile.Num();
	if (Source == TEXT("discard_pile")) return DiscardPile.Num();
	if (Source == TEXT("exhaust_pile")) return ExhaustPile.Num();
	if (Source == TEXT("last_card_cost")) return LastPlayedCard.Data.Id.IsEmpty() ? 0 : GetEffectiveCost(LastPlayedCard);
	if (Source == TEXT("target_hp") && Enemies.IsValidIndex(TargetEnemyIndex)) return Enemies[TargetEnemyIndex].State.HP;
	if (Source == TEXT("target_missing_hp") && Enemies.IsValidIndex(TargetEnemyIndex))
		return FMath::Max(0, Enemies[TargetEnemyIndex].State.MaxHP - Enemies[TargetEnemyIndex].State.HP);
	if (Source == TEXT("target_block") && Enemies.IsValidIndex(TargetEnemyIndex)) return Enemies[TargetEnemyIndex].State.Block;
	if (Source.StartsWith(TEXT("var:"))) return ScriptVariables.FindRef(Source.Mid(4));
	if (Source.StartsWith(TEXT("self_status:"))) return Player.GetStatusStacks(Source.Mid(12));
	if (Source.StartsWith(TEXT("target_status:")) && Enemies.IsValidIndex(TargetEnemyIndex))
		return Enemies[TargetEnemyIndex].State.GetStatusStacks(Source.Mid(14));
	return 0;
}

void UCombatEngine::WriteScriptValue(const FString& Destination, int32 Value, const FString& WriteMode,
	int32 TargetEnemyIndex, const FString& SourceName)
{
	auto ResolveWrittenValue = [&WriteMode, Value](int32 Current)
	{
		if (WriteMode == TEXT("set")) return Value;
		if (WriteMode == TEXT("min")) return FMath::Min(Current, Value);
		if (WriteMode == TEXT("max")) return FMath::Max(Current, Value);
		if (WriteMode == TEXT("multiply")) return FMath::Clamp(Current * Value, -9999, 9999);
		return FMath::Clamp(Current + Value, -9999, 9999);
	};
	if (Destination == TEXT("self_block"))
	{
		const int32 NewValue = FMath::Max(0, ResolveWrittenValue(Player.Block));
		const int32 Delta = NewValue - Player.Block;
		if (Delta > 0) ApplyBlock(Player, Delta);
		else Player.Block = NewValue;
	}
	else if (Destination == TEXT("self_hp"))
	{
		Player.HP = FMath::Clamp(ResolveWrittenValue(Player.HP), 1, Player.MaxHP);
	}
	else if (Destination == TEXT("self_spirit"))
	{
		Spirit = FMath::Max(0, ResolveWrittenValue(Spirit));
	}
	else if (Destination == TEXT("target_block") && Enemies.IsValidIndex(TargetEnemyIndex))
	{
		FCombatantState& Target = Enemies[TargetEnemyIndex].State;
		Target.Block = FMath::Max(0, ResolveWrittenValue(Target.Block));
	}
	else if (Destination == TEXT("target_hp") && Enemies.IsValidIndex(TargetEnemyIndex))
	{
		FCombatantState& Target = Enemies[TargetEnemyIndex].State;
		Target.HP = FMath::Clamp(ResolveWrittenValue(Target.HP), 0, Target.MaxHP);
	}
	else if (Destination.StartsWith(TEXT("var:")))
	{
		const FString Key = Destination.Mid(4);
		ScriptVariables.FindOrAdd(Key) = ResolveWrittenValue(ScriptVariables.FindRef(Key));
	}
	else if (Destination.StartsWith(TEXT("self_status:")))
	{
		const FString StatusId = Destination.Mid(12);
		const int32 Current = Player.GetStatusStacks(StatusId);
		const int32 Delta = ResolveWrittenValue(Current) - Current;
		if (Delta != 0) ApplyStatusTo(Player, StatusId, Delta, SourceName);
	}
	else if (Destination.StartsWith(TEXT("target_status:")) && Enemies.IsValidIndex(TargetEnemyIndex))
	{
		const FString StatusId = Destination.Mid(14);
		FCombatantState& Target = Enemies[TargetEnemyIndex].State;
		const int32 Current = Target.GetStatusStacks(StatusId);
		const int32 Delta = ResolveWrittenValue(Current) - Current;
		if (Delta != 0) ApplyStatusTo(Target, StatusId, Delta, SourceName);
	}
}

TArray<FCardInstance>* UCombatEngine::ResolveCardZone(const FString& Zone)
{
	if (Zone == TEXT("hand")) return &Hand;
	if (Zone == TEXT("draw") || Zone == TEXT("draw_top") || Zone == TEXT("draw_random")) return &DrawPile;
	if (Zone == TEXT("discard")) return &DiscardPile;
	if (Zone == TEXT("exhaust")) return &ExhaustPile;
	return nullptr;
}

const TArray<FCardInstance>* UCombatEngine::ResolveCardZone(const FString& Zone) const
{
	return const_cast<UCombatEngine*>(this)->ResolveCardZone(Zone);
}

bool UCombatEngine::CardMatchesScriptSelector(const FCardInstance& Card, const FString& Selector) const
{
	if (Selector.IsEmpty() || Selector == TEXT("any") || Selector == TEXT("random")
		|| Selector == TEXT("highest_cost") || Selector == TEXT("lowest_cost")) return true;
	if (Selector == TEXT("upgraded")) return Card.bUpgraded;
	if (Selector == TEXT("non_upgraded")) return !Card.bUpgraded;
	if (Selector == TEXT("retained")) return Card.Data.bRetain;
	if (Selector == TEXT("exhausting")) return Card.Data.bExhaust;
	if (Selector.StartsWith(TEXT("type:"))) return Card.Data.Type == Selector.Mid(5);
	if (Selector.StartsWith(TEXT("rarity:"))) return Card.Data.Rarity == Selector.Mid(7);
	if (Selector.StartsWith(TEXT("cost_at_most:"))) return GetEffectiveCost(Card) <= FCString::Atoi(*Selector.Mid(13));
	return false;
}

void UCombatEngine::ExecuteCardZoneEffect(const FCardEffect& Effect)
{
	TArray<FCardInstance> LastPlayedSource;
	TArray<FCardInstance>* SourceZone = nullptr;
	if (Effect.Source == TEXT("last_played"))
	{
		if (!LastPlayedCard.Data.Id.IsEmpty()) LastPlayedSource.Add(LastPlayedCard);
		SourceZone = &LastPlayedSource;
	}
	else SourceZone = ResolveCardZone(Effect.Source);
	if (!SourceZone) return;

	TArray<int32> Candidates;
	for (int32 Index = 0; Index < SourceZone->Num(); ++Index)
	{
		const FCardInstance& Candidate = (*SourceZone)[Index];
		if (Candidate.UID == PlayingCardUID || !CardMatchesScriptSelector(Candidate, Effect.Param)) continue;
		Candidates.Add(Index);
	}
	if (Effect.Param == TEXT("highest_cost"))
		Candidates.Sort([SourceZone](int32 A, int32 B) { return (*SourceZone)[A].GetCost() > (*SourceZone)[B].GetCost(); });
	else if (Effect.Param == TEXT("lowest_cost"))
		Candidates.Sort([SourceZone](int32 A, int32 B) { return (*SourceZone)[A].GetCost() < (*SourceZone)[B].GetCost(); });
	else if (Effect.Param.IsEmpty() || Effect.Param == TEXT("random"))
		ShuffleArray(Candidates, Rng);
	const int32 Count = FMath::Min(FMath::Max(1, Effect.Value), Candidates.Num());
	if (Count <= 0) return;

	if (Effect.Action == TEXT("modify_card_cost"))
	{
		for (int32 Pick = 0; Pick < Count; ++Pick)
			(*SourceZone)[Candidates[Pick]].CostModifier = FMath::Clamp(
				(*SourceZone)[Candidates[Pick]].CostModifier + Effect.StatusStacks, -9, 9);
		Log(FString::Printf(TEXT("【%s】修改%d张牌的费用 %+d"), *PendingPowerSourceName, Count, Effect.StatusStacks));
		return;
	}
	if (Effect.Action == TEXT("upgrade_cards"))
	{
		for (int32 Pick = 0; Pick < Count; ++Pick) (*SourceZone)[Candidates[Pick]].bUpgraded = true;
		Log(FString::Printf(TEXT("【%s】在本场强化%d张牌"), *PendingPowerSourceName, Count));
		return;
	}
	if (Effect.Action == TEXT("transform_cards"))
	{
		const FCardData* Replacement = CardTable.Find(Effect.Destination);
		if (!Replacement) return;
		for (int32 Pick = 0; Pick < Count; ++Pick)
		{
			FCardInstance& Card = (*SourceZone)[Candidates[Pick]];
			Card.Data = *Replacement;
			Card.bUpgraded = false;
			Card.CostModifier = 0;
			Card.RepeatCount = 0;
		}
		Log(FString::Printf(TEXT("【%s】将%d张牌变化为【%s】"), *PendingPowerSourceName, Count, *Replacement->Name));
		return;
	}
	if (Effect.Action == TEXT("shuffle_zone"))
	{
		ShuffleArray(*SourceZone, Rng);
		return;
	}

	TArray<FCardInstance>* DestinationZone = ResolveCardZone(Effect.Destination);
	if (!DestinationZone) return;
	TArray<FCardInstance> Selected;
	for (int32 Pick = 0; Pick < Count; ++Pick) Selected.Add((*SourceZone)[Candidates[Pick]]);
	if (Effect.Action == TEXT("move_cards") && SourceZone != &LastPlayedSource)
	{
		TArray<int32> RemovalIndices;
		for (int32 Pick = 0; Pick < Count; ++Pick) RemovalIndices.Add(Candidates[Pick]);
		RemovalIndices.Sort([](int32 A, int32 B) { return A > B; });
		for (const int32 Index : RemovalIndices) SourceZone->RemoveAt(Index);
	}
	else if (Effect.Action == TEXT("copy_cards"))
	{
		for (FCardInstance& Card : Selected) Card.UID = NextCardUID++;
	}
	for (FCardInstance& Card : Selected)
	{
		if (Effect.Destination == TEXT("draw_random"))
			DestinationZone->Insert(Card, Rng.RandRange(0, DestinationZone->Num()));
		else DestinationZone->Add(Card);
	}
	Log(FString::Printf(TEXT("【%s】%s%d张牌：%s→%s"), *PendingPowerSourceName,
		Effect.Action == TEXT("copy_cards") ? TEXT("复制") : TEXT("移动"), Count,
		*Effect.Source, *Effect.Destination));
}

void UCombatEngine::TriggerCardRules(const FString& Trigger, int32& EventValue, int32 TargetEnemyIndex,
	const FString& EventTag)
{
	if (ScriptEventDepth >= 12)
	{
		Log(FString::Printf(TEXT("原创规则递归超过安全上限，已停止：%s"), *Trigger));
		return;
	}
	++ScriptEventDepth;
	const int32 PreviousEventValue = CurrentScriptEventValue;
	const FString PreviousEventTag = CurrentScriptEventTag;
	CurrentScriptEventValue = EventValue;
	CurrentScriptEventTag = EventTag;
	for (FActiveCardRule& Rule : ActiveCardRules)
	{
		if (Rule.Effect.Trigger != Trigger) continue;
		if (Rule.Effect.MaxTriggers > 0 && Rule.TriggerCount >= Rule.Effect.MaxTriggers) continue;
		if (!ShouldExecuteEffect(Rule.Effect, nullptr, TargetEnemyIndex)) continue;
		++Rule.TriggerCount;
		if (Rule.Effect.Action == TEXT("transfer"))
		{
			const int32 SourceValue = ReadScriptValue(Rule.Effect.Source, TargetEnemyIndex);
			const int32 Factor = Rule.Effect.ScaleFactor == 0 ? 1 : Rule.Effect.ScaleFactor;
			const int32 Amount = Rule.Effect.Value
				+ (SourceValue / FMath::Max(1, Rule.Effect.ScaleDivisor)) * Factor;
			WriteScriptValue(Rule.Effect.Destination, Amount, Rule.Effect.WriteMode, TargetEnemyIndex, Rule.SourceName);
			if (Rule.Effect.bConsumeSource)
			{
				if (Rule.Effect.Source == TEXT("event_value")) EventValue = 0;
				else WriteScriptValue(Rule.Effect.Source, 0, TEXT("set"), TargetEnemyIndex, Rule.SourceName);
			}
			Log(FString::Printf(TEXT("【%s】转化规则生效：%d"), *Rule.SourceName, Amount));
		}
		else
		{
			FCardEffect Executable = Rule.Effect;
			Executable.Trigger = TEXT("on_play");
			const FString PreviousSourceName = PendingPowerSourceName;
			PendingPowerSourceName = Rule.SourceName;
			ExecuteEffect(Executable, TargetEnemyIndex, true, nullptr);
			PendingPowerSourceName = PreviousSourceName;
		}
		CurrentScriptEventValue = EventValue;
	}
	CurrentScriptEventValue = PreviousEventValue;
	CurrentScriptEventTag = PreviousEventTag;
	--ScriptEventDepth;
	if (ScriptEventDepth == 0)
	{
		ActiveCardRules.RemoveAll([](const FActiveCardRule& Rule)
		{
			return Rule.Effect.MaxTriggers > 0 && Rule.TriggerCount >= Rule.Effect.MaxTriggers;
		});
	}
}

int32 UCombatEngine::ResolveEffectValue(const FCardEffect& Effect, const FCardInstance* Card, int32 TargetEnemyIndex) const
{
	int32 SourceValue = 0;
	const FString& Source = Effect.ScaleBy;
	if (Source == TEXT("counter")) SourceValue = Card ? Card->RepeatCount : 0;
	else if (Source == TEXT("self_block")) SourceValue = Player.Block;
	else if (Source == TEXT("missing_hp")) SourceValue = FMath::Max(0, Player.MaxHP - Player.HP);
	else if (Source == TEXT("hand_size")) SourceValue = Hand.Num();
	else if (Source == TEXT("draw_pile")) SourceValue = DrawPile.Num();
	else if (Source == TEXT("discard_pile")) SourceValue = DiscardPile.Num();
	else if (Source == TEXT("cards_played_this_turn")) SourceValue = CardsPlayedThisTurn;
	else if (Source == TEXT("basic_gongfa_played")) SourceValue = BasicGongfaPlayedThisCombat;
	else if (Source == TEXT("event_value")) SourceValue = CurrentScriptEventValue;
	else if (Source == TEXT("turn")) SourceValue = TurnCount;
	else if (Source == TEXT("enemy_count")) SourceValue = GetAliveEnemyIndices().Num();
	else if (Source == TEXT("self_hp")) SourceValue = Player.HP;
	else if (Source == TEXT("self_spirit")) SourceValue = Spirit;
	else if (Source == TEXT("exhaust_pile")) SourceValue = ExhaustPile.Num();
	else if (Source == TEXT("last_card_cost")) SourceValue = LastPlayedCard.Data.Id.IsEmpty() ? 0 : GetEffectiveCost(LastPlayedCard);
	else if (Source == TEXT("target_hp") && Enemies.IsValidIndex(TargetEnemyIndex)) SourceValue = Enemies[TargetEnemyIndex].State.HP;
	else if (Source == TEXT("target_missing_hp") && Enemies.IsValidIndex(TargetEnemyIndex))
		SourceValue = FMath::Max(0, Enemies[TargetEnemyIndex].State.MaxHP - Enemies[TargetEnemyIndex].State.HP);
	else if (Source == TEXT("target_block") && Enemies.IsValidIndex(TargetEnemyIndex)) SourceValue = Enemies[TargetEnemyIndex].State.Block;
	else if (Source.StartsWith(TEXT("var:"))) SourceValue = ScriptVariables.FindRef(Source.Mid(4));
	else if (Source.StartsWith(TEXT("self_status:"))) SourceValue = Player.GetStatusStacks(Source.Mid(12));
	else if (Source.StartsWith(TEXT("target_status:")) && Enemies.IsValidIndex(TargetEnemyIndex))
		SourceValue = Enemies[TargetEnemyIndex].State.GetStatusStacks(Source.Mid(14));

	const int32 Divisor = FMath::Max(1, Effect.ScaleDivisor);
	return Effect.Value + (SourceValue / Divisor) * Effect.ScaleFactor;
}

bool UCombatEngine::ShouldExecuteEffect(const FCardEffect& Effect, const FCardInstance* Card, int32 TargetEnemyIndex)
{
	if (Effect.Chance <= 0.f || (Effect.Chance < 1.f && Rng.FRand() > Effect.Chance)) return false;
	const FString& C = Effect.Condition;
	if (C.IsEmpty() || C == TEXT("always")) return true;

	auto EvaluateAtom = [this, Card, TargetEnemyIndex](FString Atom) -> bool
	{
		Atom.TrimStartAndEndInline();
		if (Atom.IsEmpty() || Atom == TEXT("always")) return true;
		auto NumberAfterColon = [&Atom]() -> int32
		{
			int32 Colon = INDEX_NONE;
			return Atom.FindChar(TEXT(':'), Colon) ? FCString::Atoi(*Atom.Mid(Colon + 1)) : 0;
		};
		if (Atom.StartsWith(TEXT("self_hp_below:"))) return Player.HP * 100 < Player.MaxHP * NumberAfterColon();
		if (Atom.StartsWith(TEXT("self_hp_above:"))) return Player.HP * 100 > Player.MaxHP * NumberAfterColon();
		if (Atom.StartsWith(TEXT("self_has_status:"))) return Player.GetStatusStacks(Atom.Mid(16)) > 0;
		if (Atom.StartsWith(TEXT("self_missing_status:"))) return Player.GetStatusStacks(Atom.Mid(20)) <= 0;
		if (Atom.StartsWith(TEXT("target_has_status:"))) return Enemies.IsValidIndex(TargetEnemyIndex)
			&& Enemies[TargetEnemyIndex].State.GetStatusStacks(Atom.Mid(18)) > 0;
		if (Atom.StartsWith(TEXT("target_missing_status:"))) return Enemies.IsValidIndex(TargetEnemyIndex)
			&& Enemies[TargetEnemyIndex].State.GetStatusStacks(Atom.Mid(22)) <= 0;
		if (Atom.StartsWith(TEXT("counter_at_least:"))) return Card && Card->RepeatCount >= NumberAfterColon();
		if (Atom.StartsWith(TEXT("hand_size_at_least:"))) return Hand.Num() >= NumberAfterColon();
		if (Atom.StartsWith(TEXT("draw_pile_at_most:"))) return DrawPile.Num() <= NumberAfterColon();
		if (Atom.StartsWith(TEXT("discard_pile_at_least:"))) return DiscardPile.Num() >= NumberAfterColon();
		if (Atom.StartsWith(TEXT("event_tag_is:"))) return CurrentScriptEventTag == Atom.Mid(13);
		static const TArray<FString> ComparePrefixes = {
			TEXT("source_at_least:"), TEXT("source_at_most:"), TEXT("source_equals:")
		};
		for (const FString& Prefix : ComparePrefixes)
		{
			if (!Atom.StartsWith(Prefix)) continue;
			const FString Comparison = Atom.Mid(Prefix.Len());
			int32 Separator = INDEX_NONE;
			if (!Comparison.FindLastChar(TEXT('='), Separator) || Separator <= 0) return false;
			const int32 Actual = ReadScriptValue(Comparison.Left(Separator), TargetEnemyIndex);
			const int32 Expected = FCString::Atoi(*Comparison.Mid(Separator + 1));
			if (Prefix == TEXT("source_at_least:")) return Actual >= Expected;
			if (Prefix == TEXT("source_at_most:")) return Actual <= Expected;
			return Actual == Expected;
		}
		return false;
	};

	TArray<FString> OrGroups;
	C.ParseIntoArray(OrGroups, TEXT("||"), true);
	for (const FString& Group : OrGroups)
	{
		TArray<FString> AndTerms;
		Group.ParseIntoArray(AndTerms, TEXT("&&"), true);
		bool bAll = AndTerms.Num() > 0;
		for (const FString& Term : AndTerms) bAll = bAll && EvaluateAtom(Term);
		if (bAll) return true;
	}
	Log(FString::Printf(TEXT("原创规则条件不满足: %s"), *C));
	return false;
}

void UCombatEngine::ExecuteEffect(const FCardEffect& Effect, int32 TargetEnemyIndex, bool bFromPlayer, const FCardInstance* Card)
{
	if (!ShouldExecuteEffect(Effect, Card, TargetEnemyIndex)) return;
	const FString& A = Effect.Action;
	const int32 V = ResolveEffectValue(Effect, Card, TargetEnemyIndex);

	if (A == TEXT("damage"))
	{
		for (int32 t = 0; t < Effect.Times; ++t) DealDamageToEnemy(V, TargetEnemyIndex, TEXT("卡牌"));
	}
	else if (A == TEXT("damage_all"))
	{
		for (int32 Idx : GetAliveEnemyIndices()) DealDamageToEnemy(V, Idx, TEXT("卡牌"));
	}
	else if (A == TEXT("damage_all_per_repeat"))
	{
		// 计数卡: 对全体敌人造成 Value 点伤害，重复 RepeatCount 次
		const int32 Rpt = (Card && Card->RepeatCount > 0) ? Card->RepeatCount : 1;
		if (Card) Log(FString::Printf(TEXT("【%s】牵引 %d 道剑痕"), *Card->GetDisplayName(), Rpt));
		for (int32 t = 0; t < Rpt; ++t)
		{
			for (int32 Idx : GetAliveEnemyIndices()) DealDamageToEnemy(V, Idx, TEXT("卡牌"));
		}
	}
	else if (A == TEXT("damage_random"))
	{
		for (int32 t = 0; t < Effect.Times; ++t)
		{
			TArray<int32> Alive = GetAliveEnemyIndices();
			if (Alive.Num() == 0) break;
			DealDamageToEnemy(V, Alive[Rng.RandRange(0, Alive.Num() - 1)], TEXT("卡牌"));
		}
	}
	else if (A == TEXT("block"))
	{
		ApplyBlock(Player, V);
	}
	else if (A == TEXT("draw"))
	{
		DrawCards(V);
		Log(FString::Printf(TEXT("抽 %d 张牌"), V));
	}
	else if (A == TEXT("discover_draw"))
	{
		BeginDiscoverFromDrawPile(V);
	}
	else if (A == TEXT("gain_spirit"))
	{
		Spirit += V;
		Log(FString::Printf(TEXT("获得 %d 点灵力 (当前 %d)"), V, Spirit));
	}
	else if (A == TEXT("lose_spirit"))
	{
		const int32 Lost = FMath::Min(FMath::Max(0, V), Spirit);
		Spirit -= Lost;
		Log(FString::Printf(TEXT("失去 %d 点灵力 (当前 %d)"), Lost, Spirit));
	}
	else if (A == TEXT("heal"))
	{
		int32 Healed = FMath::Min(V, Player.MaxHP - Player.HP);
		Player.HP += Healed;
		Log(FString::Printf(TEXT("恢复 %d 点气血 (HP %d/%d)"), Healed, Player.HP, Player.MaxHP));
	}
	else if (A == TEXT("apply_status"))
	{
		if (Effect.Target == TEXT("self"))
			ApplyStatusTo(Player, Effect.StatusId, Effect.StatusStacks, TEXT("卡牌"));
		else if (Effect.Target == TEXT("all_enemies"))
			for (int32 Idx : GetAliveEnemyIndices()) ApplyStatusTo(Enemies[Idx].State, Effect.StatusId, Effect.StatusStacks, TEXT("卡牌"));
		else if (Effect.Target == TEXT("random_enemy"))
		{
			const TArray<int32> Alive = GetAliveEnemyIndices();
			if (Alive.Num() > 0) ApplyStatusTo(Enemies[Alive[Rng.RandRange(0, Alive.Num() - 1)]].State, Effect.StatusId, Effect.StatusStacks, TEXT("卡牌"));
		}
		else if (Enemies.IsValidIndex(TargetEnemyIndex))
			ApplyStatusTo(Enemies[TargetEnemyIndex].State, Effect.StatusId, Effect.StatusStacks, TEXT("卡牌"));
	}
	else if (A == TEXT("remove_status") || A == TEXT("set_status"))
	{
		auto ModifyStatus = [&](FCombatantState& Target)
		{
			const int32 Amount = Effect.StatusStacks != 0 ? Effect.StatusStacks : V;
			const int32 Current = Target.GetStatusStacks(Effect.StatusId);
			const int32 Delta = A == TEXT("set_status") ? Amount - Current : -FMath::Abs(Amount);
			Target.AddStatus(Effect.StatusId, Delta);
		};
		if (Effect.Target == TEXT("self")) ModifyStatus(Player);
		else if (Effect.Target == TEXT("all_enemies")) for (int32 Idx : GetAliveEnemyIndices()) ModifyStatus(Enemies[Idx].State);
		else if (Enemies.IsValidIndex(TargetEnemyIndex)) ModifyStatus(Enemies[TargetEnemyIndex].State);
	}
	else if (A == TEXT("apply_temp_strength"))
	{
		ApplyTempStrength(Player, Effect.StatusStacks, TEXT("卡牌"));
	}
	else if (A == TEXT("gain_gold"))
	{
		Gold += V;
		Log(FString::Printf(TEXT("获得 %d 枚灵石 (共 %d)"), V, Gold));
	}
	else if (A == TEXT("gain_max_hp"))
	{
		const int32 Gain = FMath::Max(0, V);
		Player.MaxHP += Gain;
		Player.HP += Gain;
		Log(FString::Printf(TEXT("最大气血 +%d (HP %d/%d)"), Gain, Player.HP, Player.MaxHP));
	}
	else if (A == TEXT("cleanse_toxicity"))
	{
		Toxicity = 0;
		Log(TEXT("丹毒已清除"));
	}
	else if (A == TEXT("damage_per_block"))
	{
		// 罡气共鸣: 造成 Value + 当前护体罡气 的伤害
		DealDamageToEnemy(V + Player.Block, TargetEnemyIndex, TEXT("卡牌"));
	}
	else if (A == TEXT("discard_random"))
	{
		DiscardRandomCards(V);
	}
	else if (A == TEXT("discard_hand"))
	{
		int32 Discarded = 0;
		for (int32 Index = Hand.Num() - 1; Index >= 0; --Index)
		{
			if (Hand[Index].UID == PlayingCardUID || Hand[Index].Data.bRetain) continue;
			Hand[Index].RepeatCount = 0;
			DiscardPile.Add(Hand[Index]);
			Hand.RemoveAt(Index);
			Discarded++;
		}
		if (Discarded > 0)
		{
			TriggerRelicsWithValue(TEXT("on_cards_discarded"), Discarded);
			int32 DiscardEventValue = Discarded;
			TriggerCardRules(TEXT("on_discard"), DiscardEventValue);
		}
	}
	else if (A == TEXT("spirit_next_turn"))
	{
		SpiritCarryOver += V;
		Log(FString::Printf(TEXT("封存灵气: 下回合额外获得 %d 点灵力"), V));
	}
	else if (A == TEXT("self_damage") || A == TEXT("damage_self"))
	{
		// 以血为引: 失去气血（不致死，无视罡气）
		const int32 Loss = FMath::Min(V, Player.HP - 1);
		if (Loss > 0)
		{
			Player.HP -= Loss;
			Log(FString::Printf(TEXT("你失去 %d 点气血 (HP %d/%d)"), Loss, Player.HP, Player.MaxHP));
		}
	}
	else if (A == TEXT("create_card") || A == TEXT("add_card_to_draw") || A == TEXT("add_card_to_discard"))
	{
		const int32 Count = FMath::Max(1, V);
		for (int32 Index = 0; Index < Count; ++Index)
		{
			FCardInstance Created = MakeCard(Effect.Param);
			if (Created.Data.Id.IsEmpty())
			{
				Log(FString::Printf(TEXT("无法生成未知卡牌: %s"), *Effect.Param));
				break;
			}
			if (A == TEXT("create_card")) Hand.Add(Created);
			else if (A == TEXT("add_card_to_draw")) DrawPile.Insert(Created, Rng.RandRange(0, DrawPile.Num()));
			else DiscardPile.Add(Created);
		}
		Log(FString::Printf(TEXT("生成 %d 张【%s】"), Count, *Effect.Param));
	}
	else if (A == TEXT("power"))
	{
		// 功法卡: 注册整场战斗生效的功法（StatusId = 功法ID）
		// 注: ExecuteCardEffects 中调用时附带卡名，这里由 ExecuteCardEffects 保证顺序
		AddPower(Effect.StatusId, PendingPowerSourceName);
		if (Effect.StatusId == TEXT("counter_stance") || Effect.StatusId == TEXT("thorns_aura"))
		{
			// 每个反震功法实例都保留自己的伤害，多个同名实例按总和叠加。
			CounterDamage += V;
		}
	}
	else if (A == TEXT("damage_per_status"))
	{
		// 剑意爆发: 造成 Value + 目标状态层数 × StatusStacks 的伤害（读取自身剑意）
		const int32 Stacks = Player.GetStatusStacks(Effect.StatusId);
		DealDamageToEnemy(V + Stacks * Effect.StatusStacks, TargetEnemyIndex, TEXT("卡牌"));
	}
	else if (A == TEXT("damage_all_per_basic_gongfa"))
	{
		// 万剑朝宗: 本场战斗每打出过一张基础卡/功法卡，对所有敌人造成 Value 点伤害
		const int32 Count = BasicGongfaPlayedThisCombat;
		if (Count > 0)
		{
			Log(FString::Printf(TEXT("万剑朝宗: 牵引 %d 道剑痕"), Count));
			for (int32 t = 0; t < Count; ++t)
			{
				for (int32 Idx : GetAliveEnemyIndices()) DealDamageToEnemy(V, Idx, TEXT("卡牌"));
			}
		}
	}
	else if (A == TEXT("damage_all_per_status"))
	{
		// 群体剑意爆发: 对所有敌人造成 Value + 自身状态层数 × StatusStacks 的伤害
		const int32 Stacks = Player.GetStatusStacks(Effect.StatusId);
		for (int32 Idx : GetAliveEnemyIndices())
		{
			DealDamageToEnemy(V + Stacks * Effect.StatusStacks, Idx, TEXT("卡牌"));
		}
	}
	else if (A == TEXT("cost_free_basic_gongfa"))
	{
		bFreeBasicGongfaThisTurn = true;
		Log(TEXT("无妄剑境: 本回合基础卡与功法卡灵力消耗为0"));
	}
	else if (A == TEXT("block_per_status"))
	{
		// 防守反击: 获得 Value + 自身状态层数 × StatusStacks 的罡气
		const int32 Stacks = Player.GetStatusStacks(Effect.StatusId);
		ApplyBlock(Player, V + Stacks * Effect.StatusStacks);
	}
	else if (A == TEXT("amplify_status"))
	{
		// 状态倍增: 目标现有 StatusId 层数 × Value；未持有则施加 StatusStacks 层
		FCombatantState* Tgt = nullptr;
		if (Effect.Target == TEXT("self")) Tgt = &Player;
		else if (Enemies.IsValidIndex(TargetEnemyIndex)) Tgt = &Enemies[TargetEnemyIndex].State;
		if (Tgt)
		{
			const int32 Cur = Tgt->GetStatusStacks(Effect.StatusId);
			if (Cur > 0)
			{
				ApplyStatusTo(*Tgt, Effect.StatusId, Cur * FMath::Max(1, V - 1), TEXT("卡牌"));
			}
			else if (Effect.StatusStacks > 0)
			{
				ApplyStatusTo(*Tgt, Effect.StatusId, Effect.StatusStacks, TEXT("卡牌"));
			}
		}
	}
	else if (A == TEXT("enhance_one_sword"))
	{
		OneSwordEnhance += V;
		Log(FString::Printf(TEXT("强化【一剑】+%d (当前强化 %d)"), V, OneSwordEnhance));
		EnsureOneSwordInHand();
	}
	else if (A == TEXT("one_sword_strike"))
	{
		const int32 BaseDmg = V;
		// One Sword's own enhancement is a flat per-stack contribution to the
		// strike base.  Strength is applied exactly once by DealDamageToEnemy()
		// below, so this explicit enhancement must never become a multiplier for
		// Strength (or any other attacker status).
		const int32 TotalDmg = BaseDmg + OneSwordEnhance * 3;
		const int32 Reps = bOneSwordMultipliedThisTurn ? 2 : 1;
		Log(FString::Printf(TEXT("【一剑】! 基础 %d + 强化 %d×3 = %d 伤害%s"),
			BaseDmg, OneSwordEnhance, TotalDmg, Reps > 1 ? TEXT(" (触发两次)") : TEXT("")));
		for (int32 r = 0; r < Reps; ++r)
		{
			for (int32 Idx : GetAliveEnemyIndices()) DealDamageToEnemy(TotalDmg, Idx, TEXT("一剑"));
		}
	}
	else if (A == TEXT("one_sword_multiply"))
	{
		bOneSwordMultipliedThisTurn = true;
		Log(TEXT("剑意共鸣: 本回合一剑额外触发一次"));
	}
	else if (A == TEXT("one_sword_free"))
	{
		bOneSwordFreeThisTurn = true;
		Log(TEXT("化剑为气: 本回合一剑灵力消耗为0"));
	}
	else if (A == TEXT("next_zhaoshi_cost_reduce"))
	{
		NextZhaoshiCostReduce = FMath::Max(NextZhaoshiCostReduce, V);
		Log(FString::Printf(TEXT("本回合下一张招式费用-%d"), V));
	}
	else if (A == TEXT("wan_jian_damage"))
	{
		const int32 Base = V;
		const int32 Bonus = Card ? Card->RepeatCount * Effect.StatusStacks : 0;
		const int32 Total = Base + Bonus;
		for (int32 Idx : GetAliveEnemyIndices())
			DealDamageToEnemy(Total, Idx, TEXT("万剑归宗"));
		Log(FString::Printf(TEXT("万剑归宗: %d+%d=%d 伤害 (重复%d次)"), Base, Bonus, Total, Card ? Card->RepeatCount : 0));
	}
	else if (A == TEXT("defense_to_strength"))
	{
		// 以守为攻: 若本回合获得过护甲则获得双倍力量
		const int32 BaseStr = V; // 3
		const int32 BonusStr = Effect.StatusStacks; // 3
		const int32 TotalStr = bGotBlockThisTurn ? (BaseStr + BonusStr) : BaseStr;
		Log(FString::Printf(TEXT("以守为攻: 获得 %d 层力量%s"), TotalStr, bGotBlockThisTurn ? TEXT(" (守转攻!)") : TEXT("")));
		ApplyStatusTo(Player, TEXT("strength"), TotalStr, TEXT("以守为攻"));
	}
	else if (A == TEXT("move_cards") || A == TEXT("copy_cards") || A == TEXT("modify_card_cost")
		|| A == TEXT("upgrade_cards") || A == TEXT("transform_cards") || A == TEXT("shuffle_zone"))
	{
		ExecuteCardZoneEffect(Effect);
	}
	else if (A == TEXT("transfer"))
	{
		const int32 SourceValue = ReadScriptValue(Effect.Source, TargetEnemyIndex);
		const int32 Factor = Effect.ScaleFactor == 0 ? 1 : Effect.ScaleFactor;
		const int32 Amount = Effect.Value
			+ (SourceValue / FMath::Max(1, Effect.ScaleDivisor)) * Factor;
		WriteScriptValue(Effect.Destination, Amount, Effect.WriteMode, TargetEnemyIndex,
			PendingPowerSourceName.IsEmpty() ? TEXT("原创卡") : PendingPowerSourceName);
		if (Effect.bConsumeSource)
			WriteScriptValue(Effect.Source, 0, TEXT("set"), TargetEnemyIndex, PendingPowerSourceName);
	}
}

// -----------------------------------------------------------
// 伤害与状态
// -----------------------------------------------------------

int32 UCombatEngine::CalcAttackDamage(int32 Base, FCombatantState& Attacker, FCombatantState& Defender) const
{
	// Strength is a flat additive bonus to each emitted attack segment. Callers
	// invoke this once per hit; explicit repeat/multiplier effects are handled by
	// their own effect branches and must not be folded into the Strength term.
	float Dmg = static_cast<float>(Base) + static_cast<float>(Attacker.GetStatusStacks(TEXT("strength")));
	if (Attacker.GetStatusStacks(TEXT("weak")) > 0) Dmg *= 0.75f;
	if (Defender.GetStatusStacks(TEXT("vulnerable")) > 0) Dmg *= 1.5f;
	// 被动法器加成（每损失5%HP+1伤害等）
	Dmg += static_cast<float>(const_cast<UCombatEngine*>(this)->GetRelicPassiveValue(TEXT("hp_missing_5pct")));
	return FMath::Max(0, FMath::FloorToInt(Dmg));
}

void UCombatEngine::DealDamageToEnemy(int32 Amount, int32 EnemyIndex, const FString& SourceName)
{
	if (!Enemies.IsValidIndex(EnemyIndex)) return;
	FEnemyCombatant& E = Enemies[EnemyIndex];
	if (!E.State.IsAlive()) return;

	int32 EffectiveAmount = FMath::Max(0, Amount);
	TriggerCardRules(TEXT("before_deal_damage"), EffectiveAmount, EnemyIndex, E.Data.Tier);
	const int32 Dmg = CalcAttackDamage(EffectiveAmount, Player, E.State);
	const int32 Absorbed = FMath::Min(E.State.Block, Dmg);
	E.State.Block -= Absorbed;
	E.State.HP -= (Dmg - Absorbed);

	Log(FString::Printf(TEXT("  %s 受到 %d 点伤害 (罡气抵挡 %d, HP %d/%d)"),
		*E.State.Name, Dmg, Absorbed, FMath::Max(0, E.State.HP), E.State.MaxHP));
	EnemyDamageEvents.Add({EnemyIndex, Dmg});

	TriggerRelicsWithValue(TEXT("on_damage_dealt"), Dmg);
	int32 DamageEventValue = FMath::Max(0, Dmg - Absorbed);
	TriggerCardRules(TEXT("on_damage_dealt"), DamageEventValue, EnemyIndex);
	if (Dmg > 0) IncrementHandCounters(TEXT("on_damage_dealt"));

	// ---- 受击机制 ----
	// 反噬必须在击杀判定前结算：致命攻击同样属于“受到攻击”，不能因为
	// 下面的击杀提前返回而漏掉反噬。仅在实际造成伤害时触发，避免无效
	// 的零伤害效果凭空扣血。
	const int32 ThornsN = GetEnemyAbilityValue(E.Data, TEXT("thorns"));
	if (ThornsN > 0 && Dmg > 0 && Player.IsAlive())
	{
		// 反噬是固定数值伤害，但仍属于对玩家的伤害，必须先由罡气吸收；
		// 不直接调用 DealDamageToPlayer，避免把反噬误判为敌方攻击并触发
		// 反击功法，形成“反噬 -> 反击 -> 反噬”的递归链。
		const int32 ThornsAbsorbed = FMath::Min(Player.Block, ThornsN);
		Player.Block -= ThornsAbsorbed;
		const int32 ThornsHPDamage = ThornsN - ThornsAbsorbed;
		Player.HP = FMath::Max(0, Player.HP - ThornsHPDamage);
		TriggerRelicsWithValue(TEXT("on_hp_changed"), ThornsHPDamage);
		Log(FString::Printf(TEXT("  %s 的反噬: 你受到 %d 点反噬伤害 (罡气抵挡 %d, HP %d/%d)"),
			*E.State.Name, ThornsN, ThornsAbsorbed, FMath::Max(0, Player.HP), Player.MaxHP));
		if (!Player.IsAlive())
		{
			CheckCombatEnd();
		}
	}

	if (E.State.HP <= 0)
	{
		E.State.HP = 0;
		Log(FString::Printf(TEXT("  %s 被击杀!"), *E.State.Name));
		TriggerRelics(TEXT("on_kill"));
		int32 KillEventValue = 1;
		TriggerCardRules(TEXT("on_kill"), KillEventValue, EnemyIndex, E.Data.Tier);
		RefreshHandLimit();
		return;
	}

	// 魇梦蝶: 攻击它额外获得1层梦魇
	if (GetEnemyAbilityValue(E.Data, TEXT("nightmare")) > 0 && Player.IsAlive())
	{
		ApplyStatusTo(Player, TEXT("nightmare"), 1, E.State.Name);
		Log(FString::Printf(TEXT("  攻击魇梦蝶: 梦魇加深!")));
	}

	// 守财奴: 受到伤害时掉落灵石
	const int32 GoldDropN = GetEnemyAbilityValue(E.Data, TEXT("gold_drop"));
	if (GoldDropN > 0)
	{
		Gold += GoldDropN;
		Log(FString::Printf(TEXT("  守财奴掉落 %d 枚灵石 (灵石 %d)"), GoldDropN, Gold));
	}

	// 狂暴（enrage_half）: 气血首次低于一半时力量+N（一次性）
	if (!E.bEnraged && E.State.HP * 2 < E.State.MaxHP)
	{
		const int32 EnrageN = GetEnemyAbilityValue(E.Data, TEXT("enrage_half"));
		if (EnrageN > 0)
		{
			E.bEnraged = true;
			Log(FString::Printf(TEXT("  %s 狂性大发! (力量 +%d)"), *E.State.Name, EnrageN));
			ApplyStatusTo(E.State, TEXT("strength"), EnrageN, E.State.Name);
		}
	}
}

void UCombatEngine::DealDamageToPlayer(int32 Amount, FCombatantState& Attacker, const FString& SourceName)
{
	int32 EffectiveAmount = FMath::Max(0, Amount);
	TriggerCardRules(TEXT("before_take_damage"), EffectiveAmount);
	int32 Dmg = CalcAttackDamage(EffectiveAmount, Attacker, Player);

	// 首次受伤减免法器（如玄龟护心镜: 每回合首次受到的攻击伤害减半）
	if (!bPlayerAttackedThisTurn && Dmg > 0)
	{
		const float Reduce = FMath::Clamp(GetRelicPassiveModifierSum(TEXT("first_damage_halve")), 0.f, 0.9f);
		if (Reduce > 0.f)
		{
			const int32 NewDmg = FMath::Max(0, FMath::RoundToInt(Dmg * (1.f - Reduce)));
			Log(FString::Printf(TEXT("  法宝生效: 首次受伤减免 %d%% (伤害 %d → %d)"),
				FMath::RoundToInt(Reduce * 100.f), Dmg, NewDmg));
			Dmg = NewDmg;
		}
	}
	bPlayerAttackedThisTurn = true;

	const int32 Absorbed = FMath::Min(Player.Block, Dmg);
	Player.Block -= Absorbed;
	Player.HP -= (Dmg - Absorbed);

	Log(FString::Printf(TEXT("  你受到 %d 点伤害 (罡气抵挡 %d, HP %d/%d)"),
		Dmg, Absorbed, FMath::Max(0, Player.HP), Player.MaxHP));

	TriggerRelicsWithValue(TEXT("on_hp_changed"), Dmg - Absorbed);
	int32 DamageEventValue = FMath::Max(0, Dmg - Absorbed);
	TriggerCardRules(TEXT("on_damage_taken"), DamageEventValue);

	// 功法「听劲反击诀」/「罡气反震」: 受到攻击后反击攻击者
		if (CounterDamage > 0 && bCombatActive && Player.IsAlive())
		{
			FString PowerName;
			const int32 CounterStanceCount = GetPowerCount(TEXT("counter_stance"));
			const int32 ThornsAuraCount = GetPowerCount(TEXT("thorns_aura"));
			if (CounterStanceCount > 0 && ThornsAuraCount > 0)
				PowerName = TEXT("听劲反击诀 / 罡气反震");
			else if (CounterStanceCount > 0)
				PowerName = TEXT("听劲反击诀");
			else if (ThornsAuraCount > 0)
				PowerName = TEXT("罡气反震");
		if (!PowerName.IsEmpty())
		{
			for (int32 i = 0; i < Enemies.Num(); ++i)
			{
				if (&Enemies[i].State == &Attacker && Enemies[i].State.IsAlive())
				{
					Log(FString::Printf(TEXT("功法【%s】运转: 反震 %d 点伤害"), *PowerName, CounterDamage));
					DealDamageToEnemy(CounterDamage, i, PowerName);
					break;
				}
			}
		}
	}

	if (Player.HP <= 0)
	{
		Player.HP = 0;
	}
}

void UCombatEngine::ApplyBlock(FCombatantState& Target, int32 Amount)
{
	int32 EffectiveAmount = Amount;
	if (&Target == &Player && EffectiveAmount > 0)
		TriggerCardRules(TEXT("before_gain_block"), EffectiveAmount);
	if (EffectiveAmount <= 0) return;
	if (&Target == &Player) bGotBlockThisTurn = true;
	Target.Block += EffectiveAmount;
	Log(FString::Printf(TEXT("  %s 获得 %d 点护体罡气 (共 %d)"), *Target.Name, EffectiveAmount, Target.Block));
	if (&Target == &Player)
	{
		int32 AppliedAmount = EffectiveAmount;
		TriggerCardRules(TEXT("after_gain_block"), AppliedAmount);
	}
}

void UCombatEngine::ApplyTempStrength(FCombatantState& Target, int32 Stacks, const FString& SourceName)
{
	if (&Target == &Player && Stacks > 0)
	{
		const int32 Before = Player.GetStatusStacks(TEXT("strength"));
		ApplyStatusTo(Target, TEXT("strength"), Stacks, SourceName);
		const int32 Gained = Player.GetStatusStacks(TEXT("strength")) - Before;
		if (Gained > 0) Player.AddStatus(TEXT("temp_strength"), Gained);
	}
	else
	{
		ApplyStatusTo(Target, TEXT("strength"), Stacks, SourceName);
	}
}

void UCombatEngine::ApplyStatusTo(FCombatantState& Target, const FString& StatusId, int32 Stacks, const FString& SourceName)
{
	// 状态增强被动法器（如万毒鼎: status_boost_poison，对敌方施加状态时层数+N）
	if (Stacks > 0 && &Target != &Player)
	{
		const int32 Bonus = GetRelicPassiveValue(FString::Printf(TEXT("status_boost_%s"), *StatusId));
		if (Bonus != 0) Stacks = FMath::Max(1, Stacks + Bonus);
	}

	// 功法「双刃剑意诀」: 每个独立实例各翻倍一次，两个实例即为四倍。
	const int32 DoubleStrengthCount = GetPowerCount(TEXT("double_strength"));
	if (Stacks > 0 && StatusId == TEXT("strength") && &Target == &Player && DoubleStrengthCount > 0)
	{
		int32 Multiplier = 1;
		for (int32 InstanceIndex = 0; InstanceIndex < DoubleStrengthCount; ++InstanceIndex)
		{
			Stacks = Stacks > MAX_int32 / 2 ? MAX_int32 : Stacks * 2;
			Multiplier = FMath::Clamp(Multiplier * 2, 1, MAX_int32);
		}
		Log(FString::Printf(TEXT("功法【双刃剑意诀】%d 重运转: 力量获取变为%d倍"),
			DoubleStrengthCount, Multiplier));
	}

	// 记录本集是否获得过力量/护甲（供双刃剑意诀/以守为攻检测）
	if (StatusId == TEXT("strength") && &Target == &Player && Stacks > 0)
	{
		bGotStrengthThisTurn = true;
	}

	Target.AddStatus(StatusId, Stacks);
	if (Stacks != 0)
	{
		int32 StatusEventValue = FMath::Abs(Stacks);
		int32 TargetEnemyIndex = INDEX_NONE;
		for (int32 Index = 0; Index < Enemies.Num(); ++Index)
		{
			if (&Enemies[Index].State == &Target) { TargetEnemyIndex = Index; break; }
		}
		TriggerCardRules(TEXT("on_status_applied"), StatusEventValue, TargetEnemyIndex, StatusId);
	}

	static const TMap<FString, FString> StatusNames = {
		{TEXT("burn"), TEXT("灼烧")}, {TEXT("poison"), TEXT("中毒")},
		{TEXT("weak"), TEXT("虚弱")}, {TEXT("vulnerable"), TEXT("易伤")},
		{TEXT("strength"), TEXT("力量")}
	};
	const FString CN = StatusNames.Contains(StatusId) ? StatusNames[StatusId] : StatusId;
	Log(FString::Printf(TEXT("  %s 获得 %d 层%s (共 %d 层)"),
		*Target.Name, Stacks, *CN, Target.GetStatusStacks(StatusId)));
}

bool UCombatEngine::TickDotStatuses(FCombatantState& Unit)
{
	for (const TCHAR* DotId : {TEXT("burn"), TEXT("poison")})
	{
		const int32 Stacks = Unit.GetStatusStacks(DotId);
		if (Stacks > 0)
		{
			Unit.HP -= Stacks; // 持续伤害无视罡气
			const TCHAR* CN = (FCString::Strcmp(DotId, TEXT("burn")) == 0) ? TEXT("灼烧") : TEXT("中毒");
			Log(FString::Printf(TEXT("  %s 受到 %d 层%s伤害 %d 点 (HP %d/%d)"),
				*Unit.Name, Stacks, CN, Stacks, FMath::Max(0, Unit.HP), Unit.MaxHP));
			Unit.AddStatus(DotId, -1);
			if (Unit.HP <= 0)
			{
				Unit.HP = 0;
				return true;
			}
		}
	}
	return false;
}

void UCombatEngine::DecayStatuses(FCombatantState& Unit)
{
	for (const TCHAR* Id : {TEXT("weak"), TEXT("vulnerable")})
	{
		if (Unit.GetStatusStacks(Id) > 0) Unit.AddStatus(Id, -1);
	}
}

// -----------------------------------------------------------
// 法宝
// -----------------------------------------------------------

void UCombatEngine::TriggerRelics(const FString& Trigger)
{
	for (int32 ri = 0; ri < ActiveRelics.Num(); ++ri)
	{
		const FRelicData& R = ActiveRelics[ri];
		if (R.Trigger != Trigger) continue;

		// 条件检查
		if (R.Condition == TEXT("unused_hand") && LastTurnHandSize == 0) continue;

		Log(FString::Printf(TEXT("法宝 [%s] 触发: %s"), *R.Name, *R.Description));

		const FCardEffect& E = R.Effect;
		if (E.Action == TEXT("block")) ApplyBlock(Player, E.Value);
		else if (E.Action == TEXT("gain_spirit")) { Spirit += E.Value; Log(FString::Printf(TEXT("  获得 %d 点灵力"), E.Value)); }
		else if (E.Action == TEXT("draw")) DrawCards(E.Value);
		else if (E.Action == TEXT("heal")) { int32 H = FMath::Min(E.Value, Player.MaxHP - Player.HP); Player.HP += H; Log(FString::Printf(TEXT("  恢复 %d 点气血"), H)); }
		else if (E.Action == TEXT("apply_status")) ApplyStatusTo(Player, E.StatusId, E.StatusStacks, *R.Name);
		else if (E.Action == TEXT("gain_gold")) { Gold += E.Value; Log(FString::Printf(TEXT("  获得 %d 枚灵石"), E.Value)); }
		else if (E.Action == TEXT("damage_random"))
		{
			TArray<int32> Alive = GetAliveEnemyIndices();
			if (Alive.Num() > 0) DealDamageToEnemy(E.Value, Alive[Rng.RandRange(0, Alive.Num() - 1)], *R.Name);
		}
		else if (E.Action == TEXT("damage_all"))
		{
			for (int32 Idx : GetAliveEnemyIndices()) DealDamageToEnemy(E.Value, Idx, *R.Name);
		}
	}
}

void UCombatEngine::TriggerRelicsWithValue(const FString& Trigger, int32 Value)
{
	for (int32 ri = 0; ri < ActiveRelics.Num(); ++ri)
	{
		const FRelicData& R = ActiveRelics[ri];
		if (R.Trigger != Trigger) continue;

		if (R.Counter > 0)
		{
			// 计数型法器
			if (RelicCounters.IsValidIndex(ri))
			{
				RelicCounters[ri].CurrentCount++;
				if (RelicCounters[ri].CurrentCount < R.Counter) continue;
				RelicCounters[ri].CurrentCount = 0;
			}
		}

		// 弃牌触发（per_discarded_card）: 按弃牌数量重复生效
		const int32 Repeat = (Trigger == TEXT("on_cards_discarded") && R.Condition == TEXT("per_discarded_card"))
			? FMath::Max(1, Value) : 1;

		Log(FString::Printf(TEXT("法宝 [%s] 触发: %s"), *R.Name, *R.Description));

		const FCardEffect& E = R.Effect;
		for (int32 Rep = 0; Rep < Repeat; ++Rep)
		{
			if (E.Action == TEXT("block")) ApplyBlock(Player, E.Value);
			else if (E.Action == TEXT("gain_spirit")) { Spirit += E.Value; Log(FString::Printf(TEXT("  获得 %d 点灵力"), E.Value)); }
			else if (E.Action == TEXT("draw")) DrawCards(E.Value);
			else if (E.Action == TEXT("heal"))
			{
				int32 Amount = (R.Condition == TEXT("heal_on_damage")) ? Value * E.Value / 100 : E.Value;
				int32 H = FMath::Min(Amount, Player.MaxHP - Player.HP);
				Player.HP += H;
				Log(FString::Printf(TEXT("  恢复 %d 点气血"), H));
			}
			else if (E.Action == TEXT("apply_status")) ApplyStatusTo(Player, E.StatusId, E.StatusStacks, *R.Name);
			else if (E.Action == TEXT("gain_gold")) { Gold += E.Value; Log(FString::Printf(TEXT("  获得 %d 枚灵石"), E.Value)); }
			else if (E.Action == TEXT("damage_random"))
			{
				TArray<int32> Alive = GetAliveEnemyIndices();
				if (Alive.Num() > 0) DealDamageToEnemy(E.Value, Alive[Rng.RandRange(0, Alive.Num() - 1)], *R.Name);
			}
			else if (E.Action == TEXT("damage_all"))
			{
				for (int32 Idx : GetAliveEnemyIndices()) DealDamageToEnemy(E.Value, Idx, *R.Name);
			}
		}
	}
}

int32 UCombatEngine::GetRelicPassiveValue(const FString& Condition, int32 ContextValue) const
{
	int32 Total = 0;
	for (const FRelicData& R : ActiveRelics)
	{
		if (R.Condition == Condition)
		{
			if (Condition == TEXT("hp_missing_5pct"))
			{
				int32 PctMissing = (Player.MaxHP - Player.HP) * 100 / Player.MaxHP;
				Total += (PctMissing / 5) * static_cast<int32>(R.Modifier);
			}
			else
			{
				Total += static_cast<int32>(R.Modifier);
			}
		}
	}
	return Total;
}

float UCombatEngine::GetRelicPassiveModifierSum(const FString& Condition) const
{
	float Total = 0.f;
	for (const FRelicData& R : ActiveRelics)
	{
		if (R.Condition == Condition) Total += R.Modifier;
	}
	return Total;
}

void UCombatEngine::DiscardRandomCards(int32 Count)
{
	int32 Discarded = 0;
	for (int32 n = 0; n < Count; ++n)
	{
		// 候选（不含正在打出的牌）
		TArray<int32> Candidates;
		for (int32 i = 0; i < Hand.Num(); ++i)
		{
			if (Hand[i].UID != PlayingCardUID) Candidates.Add(i);
		}
		if (Candidates.Num() == 0) break;

		const int32 Pick = Candidates[Rng.RandRange(0, Candidates.Num() - 1)];
		FCardInstance C = Hand[Pick];
		C.RepeatCount = 0;
		Hand.RemoveAt(Pick);
		DiscardPile.Add(C);
		Discarded++;
		Log(FString::Printf(TEXT("弃掉 %s"), *C.GetDisplayName()));
	}
	if (Discarded > 0)
	{
		TriggerRelicsWithValue(TEXT("on_cards_discarded"), Discarded);
		int32 DiscardEventValue = Discarded;
		TriggerCardRules(TEXT("on_discard"), DiscardEventValue);
	}
}

void UCombatEngine::ResetRelicCounters()
{
	for (FRelicRuntimeState& RS : RelicCounters) RS.CurrentCount = 0;
}

// -----------------------------------------------------------
// 敌人 AI
// -----------------------------------------------------------

void UCombatEngine::RollIntent(FEnemyCombatant& Enemy)
{
	if (Enemy.Data.Intents.Num() == 0) return;

	int32 TotalWeight = 0;
	for (const FEnemyIntent& I : Enemy.Data.Intents) TotalWeight += I.Weight;

	int32 Roll = Rng.RandRange(1, TotalWeight);
	for (const FEnemyIntent& I : Enemy.Data.Intents)
	{
		Roll -= I.Weight;
		if (Roll <= 0)
		{
			Enemy.CurrentIntent = I;
			return;
		}
	}
	Enemy.CurrentIntent = Enemy.Data.Intents[0];
}

void UCombatEngine::ExecuteEnemyIntent(FEnemyCombatant& Enemy)
{
	const FEnemyIntent& I = Enemy.CurrentIntent;

	if (I.Action == TEXT("attack"))
	{
		const int32 Val = I.Value + Enemy.LevelBonus;
		Log(FString::Printf(TEXT("%s 发动攻击 (%d)"), *Enemy.State.Name, Val));
		DealDamageToPlayer(Val, Enemy.State, Enemy.State.Name);
	}
	else if (I.Action == TEXT("attack_multi"))
	{
		const int32 Val = I.Value + Enemy.LevelBonus;
		Log(FString::Printf(TEXT("%s 发动连续攻击 (%dx%d)"), *Enemy.State.Name, Val, I.Times));
		for (int32 t = 0; t < I.Times; ++t) DealDamageToPlayer(Val, Enemy.State, Enemy.State.Name);
	}
	else if (I.Action == TEXT("defend"))
	{
		ApplyBlock(Enemy.State, I.Value + Enemy.LevelBonus);
	}
	else if (I.Action == TEXT("buff"))
	{
		ApplyStatusTo(Enemy.State, I.StatusId, I.StatusStacks, Enemy.State.Name);
	}
	else if (I.Action == TEXT("debuff"))
	{
		ApplyStatusTo(Player, I.StatusId, I.StatusStacks, Enemy.State.Name);
	}
	else if (I.Action == TEXT("heal_ally"))
	{
		for (FEnemyCombatant& Other : Enemies)
		{
			if (&Other == &Enemy) continue;
			if (!Other.State.IsAlive()) continue;
			const int32 Healed = FMath::Min(I.Value, Other.State.MaxHP - Other.State.HP);
			Other.State.HP += Healed;
			Log(FString::Printf(TEXT("%s 治疗 %s %d 点 (HP %d/%d)"),
				*Enemy.State.Name, *Other.State.Name, Healed, Other.State.HP, Other.State.MaxHP));
			break;
		}
	}
	else if (I.Action == TEXT("buff_ally"))
	{
		for (FEnemyCombatant& Other : Enemies)
		{
			if (&Other == &Enemy) continue;
			if (!Other.State.IsAlive()) continue;
			ApplyStatusTo(Other.State, I.StatusId.IsEmpty() ? TEXT("strength") : I.StatusId, I.StatusStacks, Enemy.State.Name);
			break;
		}
	}

	// 攻击附带的异常状态（如蛇妖的毒牙）
	if ((I.Action == TEXT("attack") || I.Action == TEXT("attack_multi")) && !I.StatusId.IsEmpty() && Player.IsAlive())
	{
		ApplyStatusTo(Player, I.StatusId, I.StatusStacks, Enemy.State.Name);
	}

	// 吸血机制（drain）: 攻击后汲取气血
	if ((I.Action == TEXT("attack") || I.Action == TEXT("attack_multi")) && Player.IsAlive())
	{
		const int32 DrainN = GetEnemyAbilityValue(Enemy.Data, TEXT("drain"));
		if (DrainN > 0 && Enemy.State.HP < Enemy.State.MaxHP)
		{
			const int32 Healed = FMath::Min(DrainN, Enemy.State.MaxHP - Enemy.State.HP);
			Enemy.State.HP += Healed;
			Log(FString::Printf(TEXT("  %s 汲取你 %d 点气血 (HP %d/%d)"),
				*Enemy.State.Name, Healed, Enemy.State.HP, Enemy.State.MaxHP));
		}
	}
}

// -----------------------------------------------------------
// 一剑系统
// -----------------------------------------------------------

void UCombatEngine::EnsureOneSwordInHand()
{
	for (const FCardInstance& C : Hand)
	{
		if (C.Data.Id == TEXT("one_sword")) return;
	}
	FCardInstance NewCard = MakeCard(TEXT("one_sword"));
	Hand.Insert(NewCard, 0);
	Log(TEXT("生成【一剑】加入手牌"));
}

void UCombatEngine::ResetTurnState()
{
	ActiveCardRules.RemoveAll([](const FActiveCardRule& Rule)
	{
		return Rule.Effect.Duration == TEXT("turn");
	});
	bOneSwordMultipliedThisTurn = false;
	bOneSwordFreeThisTurn = false;
	NextZhaoshiCostReduce = 0;
	SwordArtFlowTriggeredThisTurn = 0;
	bFirstBasicPlayedThisTurn = false;
	bGotStrengthThisTurn = false;
	bGotBlockThisTurn = false;
	bFirstOneSwordReturnedThisTurn = false;
	PendingDrawCount = 0;
}

// -----------------------------------------------------------
// 结束判定
// -----------------------------------------------------------

int32 UCombatEngine::GetEnemyAbilityValue(const FEnemyData& Data, const FString& AbilityId)
{
	for (const FString& A : Data.Abilities)
	{
		FString Left, Right;
		if (A.Split(TEXT(":"), &Left, &Right))
		{
			if (Left == AbilityId) return FCString::Atoi(*Right);
		}
		else if (A == AbilityId)
		{
			return 1; // 开关型机制（无数值）
		}
	}
	return 0;
}

FString UCombatEngine::GetEnemyAbilityValueRaw(const FEnemyData& Data, const FString& AbilityId)
{
	for (const FString& A : Data.Abilities)
	{
		FString Left, Right;
		if (A.Split(TEXT(":"), &Left, &Right))
		{
			if (Left == AbilityId) return Right;
		}
	}
	return TEXT("");
}

FEnemyCombatant UCombatEngine::SpawnEnemy(const FString& EnemyId)
{
	FEnemyCombatant Result;
	const FEnemyData* Data = EnemyTable.Find(EnemyId);
	if (!Data) return Result;
	Result.Data = *Data;
	Result.State.Name = Data->Name;
	Result.State.MaxHP = Data->MaxHP;
	Result.State.HP = Data->MaxHP;
	RollIntent(Result);
	return Result;
}

TArray<int32> UCombatEngine::GetAliveAlliesExcept(int32 SelfIdx) const
{
	TArray<int32> Result;
	for (int32 i = 0; i < Enemies.Num(); ++i)
	{
		if (i != SelfIdx && Enemies[i].State.IsAlive()) Result.Add(i);
	}
	return Result;
}

void UCombatEngine::RefreshHandLimit()
{
	HandLimitReduction = 0;
	for (const FEnemyCombatant& E : Enemies)
	{
		if (!E.State.IsAlive()) continue;
		HandLimitReduction += GetEnemyAbilityValue(E.Data, TEXT("hand_limit"));
	}
}

TArray<int32> UCombatEngine::GetAliveEnemyIndices() const
{
	TArray<int32> Result;
	for (int32 i = 0; i < Enemies.Num(); ++i)
	{
		if (Enemies[i].State.IsAlive()) Result.Add(i);
	}
	return Result;
}

void UCombatEngine::CheckCombatEnd()
{
	if (!bCombatActive) return;

	if (!Player.IsAlive())
	{
		bCombatActive = false;
		bVictory = false;
		Log(TEXT("====== 你倒下了……修行之路止步于此 ======"));
		return;
	}

	if (GetAliveEnemyIndices().Num() == 0)
	{
		bCombatActive = false;
		bVictory = true;
		Log(FString::Printf(TEXT("====== 战斗胜利! 剩余 HP %d/%d ======"), Player.HP, Player.MaxHP));
		TriggerRelics(TEXT("on_victory"));
	}
}
