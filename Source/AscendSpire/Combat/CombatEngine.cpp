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

bool UCombatEngine::StartCombat(const TArray<FDeckCard>& Deck, const TArray<FString>& EnemyIds,
	const TArray<FString>& OwnedRelicIds, int32 PlayerMaxHP, int32 PlayerCurrentHP,
	int32 EnemyHPBonus, int32 Seed, int32 EnemyLevel)
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

	Rng.Initialize(Seed);
	NextCardUID = 1;

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
	bCombatActive = true;
	bPlayerAttackedThisTurn = false;
	PlayingCardUID = -1;
	bFreeBasicGongfaThisTurn = false;
	BasicGongfaPlayedThisCombat = 0;
	FlowDrawsThisTurn = 0;
	ActivePowerIds.Reset();
	ActivePowerNames.Reset();
	CounterDamage = 0;

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

		// 敌人缩放: Level 0=×1.0 1=×1.7 2=×2.6 3=×4.0
		static const float LevelMult[] = {1.0f, 1.7f, 2.6f, 4.0f};
		const int32 LvlClamped = FMath::Clamp(E.LevelBonus, 0, 3);
		E.State.MaxHP = FMath::RoundToInt((Found->MaxHP + EnemyHPBonus) * LevelMult[LvlClamped]);
		E.State.HP = E.State.MaxHP;
		Enemies.Add(E);
	}

	// 法宝
	ActiveRelics.Reset();
	RelicCounters.Reset();
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
	PendingDrawCount += Count;
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
		}
		Hand.Add(DrawPile.Pop());
	}
}

void UCombatEngine::StartPlayerTurn()
{
	if (!bCombatActive) return;

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

	// 功法「剑罡护体功」: 回合开始时获得 剑意×2 罡气
	if (HasPower(TEXT("sword_gang")))
	{
		const int32 Intent = Player.GetStatusStacks(TEXT("strength"));
		if (Intent > 0)
		{
			Log(TEXT("功法【剑罡护体功】运转: 剑意化为护体罡气"));
			ApplyBlock(Player, Intent * 2);
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

	// 梦魇: 累积3层则跳过此回合
	if (Player.GetStatusStacks(TEXT("nightmare")) >= 3)
	{
		Log(TEXT("梦魇缠身! 你陷入无尽噩梦，跳过此回合"));
		Player.AddStatus(TEXT("nightmare"), -3);
		ResetTurnState();
		RunEnemyPhase();
		return;
	}

	TriggerRelics(TEXT("turn_start"));

	CardsPlayedThisTurn = 0;

	DrawCards(5);

	// 功法「剑仙真解」: 每回合额外抽2张
	if (HasPower(TEXT("immortal_draw")))
	{
		Log(TEXT("功法【剑仙真解】运转: 额外抽2张牌"));
		DrawCards(2);
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
	UE_LOG(LogTemp, Display, TEXT("[DIAG] EndPlayerTurn start"));

	LastTurnHandSize = Hand.Num();
	TriggerRelics(TEXT("on_player_turn_end"));

	// 弃掉手牌（「保留」牌留在手中）
	int32 DiscardedCount = 0;
	{
		TArray<FCardInstance> Kept;
		for (FCardInstance& C : Hand)
		{
			if (C.Data.bRetain) Kept.Add(C);
			else { C.RepeatCount = 0; DiscardPile.Add(C); DiscardedCount++; }
		}
		Hand = Kept;
	}
	if (DiscardedCount > 0)
	{
		TriggerRelicsWithValue(TEXT("on_cards_discarded"), DiscardedCount);
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

void UCombatEngine::AddPower(const FString& PowerId, const FString& SourceCardName)
{
	if (PowerId.IsEmpty() || ActivePowerIds.Contains(PowerId)) return;
	ActivePowerIds.Add(PowerId);
	ActivePowerNames.Add(SourceCardName);
	Log(FString::Printf(TEXT("功法运转: 【%s】将持续生效至本场战斗结束"), *SourceCardName));
}

bool UCombatEngine::PlayCard(int32 HandIndex, int32 TargetEnemyIndex)
{
	if (!bCombatActive) return false;
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

	Spirit -= EffectiveCost;
	Log(FString::Printf(TEXT("打出 %s (耗费%d)"), *Card.GetDisplayName(), EffectiveCost));

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
	if (bIsOneSword && HasPower(TEXT("one_sword_return")))
	{
		// 归真诀：一剑不消失，放回手牌
		Hand.Add(Card);
		Log(TEXT("功法【一剑归真诀】: 一剑不消失，保留在手"));

		// 每回合第一次打出一剑时生成临时复制
		if (!bFirstOneSwordReturnedThisTurn)
		{
			bFirstOneSwordReturnedThisTurn = true;
			FCardInstance Copy = MakeCard(TEXT("one_sword"));
			Copy.Data.bExhaust = true; // 复制体正常消失
			Hand.Add(Copy);
			Log(TEXT("功法【一剑归真诀】: 生成一张临时复制【一剑】"));
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

	// ---- 流派钩子：基础卡/功法卡 ----
	const FString& CardType = Card.Data.Type;
	if (CardType == TEXT("basic") || CardType == TEXT("gongfa"))
	{
		BasicGongfaPlayedThisCombat++;

		// 手牌中计数卡累计
		for (FCardInstance& HC : Hand)
		{
			const FString Cond = HC.GetCounterCondition();
			if (Cond == TEXT("on_basic_gongfa_play") ||
				(CardType == TEXT("basic") && Cond == TEXT("on_basic_play")))
			{
				HC.RepeatCount++;
				Log(FString::Printf(TEXT("【%s】计数+1 (当前%d)"), *HC.GetDisplayName(), HC.RepeatCount));
			}
		}

		// 功法「蓄剑诀」: 每打出基础卡/功法卡，获得1层临时力量
		if (HasPower(TEXT("sword_intent")))
		{
			ApplyTempStrength(Player, 1, TEXT("蓄剑诀"));
		}

		// 功法「流水剑经」: 每打出基础卡抽1张（每回合至多2次）
		if (CardType == TEXT("basic") && HasPower(TEXT("flow_sutra")) && FlowDrawsThisTurn < 2)
		{
			FlowDrawsThisTurn++;
			Log(TEXT("功法【流水剑经】运转: 抽1张牌"));
			DrawCards(1);
		}

		// 功法「剑意不绝」: 每打出基础卡/招式卡，强化一剑+3（每回合至多3次）
		if (HasPower(TEXT("sword_art_flow")) && SwordArtFlowTriggeredThisTurn < 3)
		{
			SwordArtFlowTriggeredThisTurn++;
			OneSwordEnhance += 3;
			Log(FString::Printf(TEXT("功法【剑意不绝】: 强化【一剑】+%d (当前强化 %d)"), 3, OneSwordEnhance));
			EnsureOneSwordInHand();
		}

		// 功法「剑墟遗刻」: 每回合第一张基础卡额外+1力量
		if (CardType == TEXT("basic") && HasPower(TEXT("sword_relic_inscription")) && !bFirstBasicPlayedThisTurn)
		{
			bFirstBasicPlayedThisTurn = true;
			ApplyStatusTo(Player, TEXT("strength"), 1, TEXT("剑墟遗刻"));
		}

		// 首张基础卡标记（即使没有剑墟遗刻也要标记防止重复）
		if (CardType == TEXT("basic") && !bFirstBasicPlayedThisTurn)
		{
			bFirstBasicPlayedThisTurn = true;
		}
	}

	// 招式卡也触发剑意不绝
	if (CardType == TEXT("zhaoshi") && HasPower(TEXT("sword_art_flow")) && SwordArtFlowTriggeredThisTurn < 3)
	{
		SwordArtFlowTriggeredThisTurn++;
		OneSwordEnhance += 3;
		Log(FString::Printf(TEXT("功法【剑意不绝】: 强化【一剑】+%d (当前强化 %d)"), 3, OneSwordEnhance));
		EnsureOneSwordInHand();
	}

	return true;
}

bool UCombatEngine::UsePill(const FString& PillId)
{
	if (!bCombatActive || !Player.IsAlive()) return false;

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
	// 功法「剑心通明经」: 基础卡效果生效两次
	const int32 Reps = (Card.Data.Type == TEXT("basic") && HasPower(TEXT("heart_clear"))) ? 2 : 1;
	if (Reps > 1) Log(TEXT("功法【剑心通明经】运转: 基础卡效果再生一次"));

	PendingPowerSourceName = Card.GetDisplayName();
	for (int32 Rep = 0; Rep < Reps; ++Rep)
	{
		for (const FCardEffect& E : Card.GetEffects())
		{
			ExecuteEffect(E, TargetEnemyIndex, true, &Card);
		}
	}
	PendingPowerSourceName.Empty();
}

void UCombatEngine::ExecuteEffect(const FCardEffect& Effect, int32 TargetEnemyIndex, bool bFromPlayer, const FCardInstance* Card)
{
	const FString& A = Effect.Action;

	if (A == TEXT("damage"))
	{
		for (int32 t = 0; t < Effect.Times; ++t) DealDamageToEnemy(Effect.Value, TargetEnemyIndex, TEXT("卡牌"));
	}
	else if (A == TEXT("damage_all"))
	{
		for (int32 Idx : GetAliveEnemyIndices()) DealDamageToEnemy(Effect.Value, Idx, TEXT("卡牌"));
	}
	else if (A == TEXT("damage_all_per_repeat"))
	{
		// 计数卡: 对全体敌人造成 Value 点伤害，重复 RepeatCount 次
		const int32 Rpt = (Card && Card->RepeatCount > 0) ? Card->RepeatCount : 1;
		if (Card) Log(FString::Printf(TEXT("【%s】牵引 %d 道剑痕"), *Card->GetDisplayName(), Rpt));
		for (int32 t = 0; t < Rpt; ++t)
		{
			for (int32 Idx : GetAliveEnemyIndices()) DealDamageToEnemy(Effect.Value, Idx, TEXT("卡牌"));
		}
	}
	else if (A == TEXT("damage_random"))
	{
		for (int32 t = 0; t < Effect.Times; ++t)
		{
			TArray<int32> Alive = GetAliveEnemyIndices();
			if (Alive.Num() == 0) break;
			DealDamageToEnemy(Effect.Value, Alive[Rng.RandRange(0, Alive.Num() - 1)], TEXT("卡牌"));
		}
	}
	else if (A == TEXT("block"))
	{
		ApplyBlock(Player, Effect.Value);
	}
	else if (A == TEXT("draw"))
	{
		DrawCards(Effect.Value);
		Log(FString::Printf(TEXT("抽 %d 张牌"), Effect.Value));
	}
	else if (A == TEXT("gain_spirit"))
	{
		Spirit += Effect.Value;
		Log(FString::Printf(TEXT("获得 %d 点灵力 (当前 %d)"), Effect.Value, Spirit));
	}
	else if (A == TEXT("heal"))
	{
		int32 Healed = FMath::Min(Effect.Value, Player.MaxHP - Player.HP);
		Player.HP += Healed;
		Log(FString::Printf(TEXT("恢复 %d 点气血 (HP %d/%d)"), Healed, Player.HP, Player.MaxHP));
	}
	else if (A == TEXT("apply_status"))
	{
		if (Effect.Target == TEXT("self"))
			ApplyStatusTo(Player, Effect.StatusId, Effect.StatusStacks, TEXT("卡牌"));
		else if (Enemies.IsValidIndex(TargetEnemyIndex))
			ApplyStatusTo(Enemies[TargetEnemyIndex].State, Effect.StatusId, Effect.StatusStacks, TEXT("卡牌"));
	}
	else if (A == TEXT("apply_temp_strength"))
	{
		ApplyTempStrength(Player, Effect.StatusStacks, TEXT("卡牌"));
	}
	else if (A == TEXT("gain_gold"))
	{
		Gold += Effect.Value;
		Log(FString::Printf(TEXT("获得 %d 枚灵石 (共 %d)"), Effect.Value, Gold));
	}
	else if (A == TEXT("cleanse_toxicity"))
	{
		Toxicity = 0;
		Log(TEXT("丹毒已清除"));
	}
	else if (A == TEXT("damage_per_block"))
	{
		// 罡气共鸣: 造成 Value + 当前护体罡气 的伤害
		DealDamageToEnemy(Effect.Value + Player.Block, TargetEnemyIndex, TEXT("卡牌"));
	}
	else if (A == TEXT("discard_random"))
	{
		DiscardRandomCards(Effect.Value);
	}
	else if (A == TEXT("spirit_next_turn"))
	{
		SpiritCarryOver += Effect.Value;
		Log(FString::Printf(TEXT("封存灵气: 下回合额外获得 %d 点灵力"), Effect.Value));
	}
	else if (A == TEXT("self_damage"))
	{
		// 以血为引: 失去气血（不致死，无视罡气）
		const int32 Loss = FMath::Min(Effect.Value, Player.HP - 1);
		if (Loss > 0)
		{
			Player.HP -= Loss;
			Log(FString::Printf(TEXT("你失去 %d 点气血 (HP %d/%d)"), Loss, Player.HP, Player.MaxHP));
		}
	}
	else if (A == TEXT("power"))
	{
		// 功法卡: 注册整场战斗生效的功法（StatusId = 功法ID）
		// 注: ExecuteCardEffects 中调用时附带卡名，这里由 ExecuteCardEffects 保证顺序
		AddPower(Effect.StatusId, PendingPowerSourceName);
		if (Effect.StatusId == TEXT("counter_stance") || Effect.StatusId == TEXT("thorns_aura"))
		{
			CounterDamage = FMath::Max(CounterDamage, Effect.Value);
		}
	}
	else if (A == TEXT("damage_per_status"))
	{
		// 剑意爆发: 造成 Value + 目标状态层数 × StatusStacks 的伤害（读取自身剑意）
		const int32 Stacks = Player.GetStatusStacks(Effect.StatusId);
		DealDamageToEnemy(Effect.Value + Stacks * Effect.StatusStacks, TargetEnemyIndex, TEXT("卡牌"));
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
				for (int32 Idx : GetAliveEnemyIndices()) DealDamageToEnemy(Effect.Value, Idx, TEXT("卡牌"));
			}
		}
	}
	else if (A == TEXT("damage_all_per_status"))
	{
		// 群体剑意爆发: 对所有敌人造成 Value + 自身状态层数 × StatusStacks 的伤害
		const int32 Stacks = Player.GetStatusStacks(Effect.StatusId);
		for (int32 Idx : GetAliveEnemyIndices())
		{
			DealDamageToEnemy(Effect.Value + Stacks * Effect.StatusStacks, Idx, TEXT("卡牌"));
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
		ApplyBlock(Player, Effect.Value + Stacks * Effect.StatusStacks);
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
				ApplyStatusTo(*Tgt, Effect.StatusId, Cur * FMath::Max(1, Effect.Value - 1), TEXT("卡牌"));
			}
			else if (Effect.StatusStacks > 0)
			{
				ApplyStatusTo(*Tgt, Effect.StatusId, Effect.StatusStacks, TEXT("卡牌"));
			}
		}
	}
	else if (A == TEXT("enhance_one_sword"))
	{
		OneSwordEnhance += Effect.Value;
		Log(FString::Printf(TEXT("强化【一剑】+%d (当前强化 %d)"), Effect.Value, OneSwordEnhance));
		EnsureOneSwordInHand();
	}
	else if (A == TEXT("one_sword_strike"))
	{
		const int32 BaseDmg = Effect.Value;
		const int32 TotalDmg = BaseDmg + OneSwordEnhance * 6;
		const int32 Reps = bOneSwordMultipliedThisTurn ? 2 : 1;
		Log(FString::Printf(TEXT("【一剑】! 基础 %d + 强化 %d×6 = %d 伤害%s"),
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
		NextZhaoshiCostReduce = FMath::Max(NextZhaoshiCostReduce, Effect.Value);
		Log(FString::Printf(TEXT("本回合下一张招式费用-%d"), Effect.Value));
	}
	else if (A == TEXT("wan_jian_damage"))
	{
		const int32 Base = Effect.Value;
		const int32 Bonus = Card ? Card->RepeatCount * Effect.StatusStacks : 0;
		const int32 Total = Base + Bonus;
		for (int32 Idx : GetAliveEnemyIndices())
			DealDamageToEnemy(Total, Idx, TEXT("万剑归宗"));
		Log(FString::Printf(TEXT("万剑归宗: %d+%d=%d 伤害 (重复%d次)"), Base, Bonus, Total, Card ? Card->RepeatCount : 0));
	}
	else if (A == TEXT("defense_to_strength"))
	{
		// 以守为攻: 若本回合获得过护甲则获得双倍力量
		const int32 BaseStr = Effect.Value; // 3
		const int32 BonusStr = Effect.StatusStacks; // 3
		const int32 TotalStr = bGotBlockThisTurn ? (BaseStr + BonusStr) : BaseStr;
		Log(FString::Printf(TEXT("以守为攻: 获得 %d 层力量%s"), TotalStr, bGotBlockThisTurn ? TEXT(" (守转攻!)") : TEXT("")));
		ApplyStatusTo(Player, TEXT("strength"), TotalStr, TEXT("以守为攻"));
	}
}

// -----------------------------------------------------------
// 伤害与状态
// -----------------------------------------------------------

int32 UCombatEngine::CalcAttackDamage(int32 Base, FCombatantState& Attacker, FCombatantState& Defender) const
{
	float Dmg = static_cast<float>(Base + Attacker.GetStatusStacks(TEXT("strength")));
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

	const int32 Dmg = CalcAttackDamage(Amount, Player, E.State);
	const int32 Absorbed = FMath::Min(E.State.Block, Dmg);
	E.State.Block -= Absorbed;
	E.State.HP -= (Dmg - Absorbed);

	Log(FString::Printf(TEXT("  %s 受到 %d 点伤害 (罡气抵挡 %d, HP %d/%d)"),
		*E.State.Name, Dmg, Absorbed, FMath::Max(0, E.State.HP), E.State.MaxHP));

	TriggerRelicsWithValue(TEXT("on_damage_dealt"), Dmg);

	if (E.State.HP <= 0)
	{
		E.State.HP = 0;
		Log(FString::Printf(TEXT("  %s 被击杀!"), *E.State.Name));
		TriggerRelics(TEXT("on_kill"));
		RefreshHandLimit();
		return;
	}

	// ---- 受击机制 ----
	// 反噬（thorns）: 受到攻击时对攻击者反噬
	const int32 ThornsN = GetEnemyAbilityValue(E.Data, TEXT("thorns"));
	if (ThornsN > 0 && Player.IsAlive())
	{
		Player.HP = FMath::Max(0, Player.HP - ThornsN);
		Log(FString::Printf(TEXT("  %s 的尸毒反噬: 你受到 %d 点反噬伤害 (HP %d/%d)"),
			*E.State.Name, ThornsN, FMath::Max(0, Player.HP), Player.MaxHP));
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
	int32 Dmg = CalcAttackDamage(Amount, Attacker, Player);

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

	// 功法「听劲反击诀」/「罡气反震」: 受到攻击后反击攻击者
	if (CounterDamage > 0 && bCombatActive && Player.IsAlive())
	{
		FString PowerName;
		if (HasPower(TEXT("counter_stance"))) PowerName = TEXT("听劲反击诀");
		else if (HasPower(TEXT("thorns_aura"))) PowerName = TEXT("罡气反震");
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
	if (&Target == &Player && Amount > 0) bGotBlockThisTurn = true;
	Target.Block += Amount;
	Log(FString::Printf(TEXT("  %s 获得 %d 点护体罡气 (共 %d)"), *Target.Name, Amount, Target.Block));
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

	// 功法「双刃剑意诀」: 力量获取翻倍
	if (Stacks > 0 && StatusId == TEXT("strength") && &Target == &Player && HasPower(TEXT("double_strength")))
	{
		Stacks *= 2;
		Log(TEXT("功法【双刃剑意诀】运转: 力量获取翻倍"));
	}

	// 记录本集是否获得过力量/护甲（供双刃剑意诀/以守为攻检测）
	if (StatusId == TEXT("strength") && &Target == &Player && Stacks > 0)
	{
		bGotStrengthThisTurn = true;
	}

	Target.AddStatus(StatusId, Stacks);

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
