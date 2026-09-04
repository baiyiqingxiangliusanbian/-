#pragma once

#include "CoreMinimal.h"
#include "CombatTypes.h"
#include "Core/GameDataTypes.h"
#include "CombatEngine.generated.h"

/** 敌方战斗单位（状态 + 数据 + 当前意图） */
USTRUCT(BlueprintType)
struct FEnemyCombatant
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	FCombatantState State;

	UPROPERTY(BlueprintReadOnly)
	FEnemyData Data;

	/** 当前意图（玩家可见） */
	UPROPERTY(BlueprintReadOnly)
	FEnemyIntent CurrentIntent;

	/** 狂暴机制已触发过（enrage_half 一次性） */
	bool bEnraged = false;

	/** 强化等级（凶/厉/煞）：意图数值加成 */
	int32 LevelBonus = 0;
};


/**
 * Structured enemy hit record for presentation and telemetry.
 */
USTRUCT(BlueprintType)
struct FEnemyDamageEvent
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly)
    int32 EnemyIndex = INDEX_NONE;

    UPROPERTY(BlueprintReadOnly)
    int32 Damage = 0;
};

/**
 * Authoritative per-encounter difficulty result.  The profile is generated
 * once by the engine and can be persisted by URunManager before an encounter
 * starts.  HPScale, IntentScale and ThreatScore are all strictly greater than
 * the previous profile, even when a low-HP template follows an elite/Boss.
 */
USTRUCT(BlueprintType)
struct FCombatDifficultyProfile
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	int32 BattleSerial = 0;

	UPROPERTY(BlueprintReadOnly)
	FString Tier = TEXT("normal");

	UPROPERTY(BlueprintReadOnly)
	int32 DifficultyStep = 0;

	UPROPERTY(BlueprintReadOnly)
	bool bElite = false;

	UPROPERTY(BlueprintReadOnly)
	bool bBoss = false;

	UPROPERTY(BlueprintReadOnly)
	float HPScale = 1.f;

	UPROPERTY(BlueprintReadOnly)
	float IntentScale = 1.f;

	UPROPERTY(BlueprintReadOnly)
	float ThreatScore = 0.f;

	/** Template-only score retained for diagnostics; not a progression floor. */
	UPROPERTY(BlueprintReadOnly)
	float TemplateThreat = 0.f;

	UPROPERTY(BlueprintReadOnly)
	float PreviousHPScale = 0.f;

	UPROPERTY(BlueprintReadOnly)
	float PreviousIntentScale = 0.f;

	UPROPERTY(BlueprintReadOnly)
	float PreviousThreatScore = 0.f;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnCombatLog, const FString&, Message);

/**
 * 战斗引擎 —— 纯逻辑，无 Actor 依赖，可无头运行
 * 回合流程: StartCombat -> [StartPlayerTurn -> 出牌 -> EndPlayerTurn -> 敌方回合] 循环
 */
UCLASS(BlueprintType)
class ASCENDSPIRE_API UCombatEngine : public UObject
{
	GENERATED_BODY()

public:
	/** 战斗日志事件（UI / 测试均可绑定） */
	UPROPERTY(BlueprintAssignable)
	FOnCombatLog OnLog;

	/** 测试钩子：战斗开始后由模拟器使用的丹药ID列表 */
	UPROPERTY()
	TArray<FString> TestPills;

	// ---------- 玩家状态 ----------
	UPROPERTY(BlueprintReadOnly)
	FCombatantState Player;

	UPROPERTY(BlueprintReadOnly)
	int32 Spirit = 0;

	UPROPERTY(BlueprintReadOnly)
	int32 MaxSpirit = 3;

	/** 储灵：下回合额外获得的灵力（spirit_next_turn 卡牌效果累积） */
	UPROPERTY(BlueprintReadOnly)
	int32 SpiritCarryOver = 0;

	/** 丹毒值（M2 丹药系统使用） */
	UPROPERTY(BlueprintReadOnly)
	int32 Toxicity = 0;

	UPROPERTY(BlueprintReadOnly)
	int32 Gold = 0;

	// ---------- 牌堆 ----------
	UPROPERTY(BlueprintReadOnly)
	TArray<FCardInstance> DrawPile;

	UPROPERTY(BlueprintReadOnly)
	TArray<FCardInstance> Hand;

	UPROPERTY(BlueprintReadOnly)
	TArray<FCardInstance> DiscardPile;

	UPROPERTY(BlueprintReadOnly)
	TArray<FCardInstance> ExhaustPile;

	// ---------- 敌人 ----------
	UPROPERTY(BlueprintReadOnly)
	TArray<FEnemyCombatant> Enemies;

	/** Difficulty actually used to instantiate this combat. */
	UPROPERTY(BlueprintReadOnly)
	FCombatDifficultyProfile DifficultyProfile;

	/** Ordered hit records for the current combat. Reset by StartCombat. */
	TArray<FEnemyDamageEvent> EnemyDamageEvents;

	UPROPERTY(BlueprintReadOnly)
	int32 TurnCount = 0;

	UPROPERTY(BlueprintReadOnly)
	bool bCombatActive = false;

	UPROPERTY(BlueprintReadOnly)
	bool bVictory = false;

	/** 梦魇跳过行动的当前回合：仍然抽牌，但不能出牌/服丹，结束回合后继续。 */
	UPROPERTY(BlueprintReadOnly)
	bool bPlayerTurnSkipped = false;

	// ---------- API ----------

	/** 初始化数据表（卡牌/敌人/法宝），返回是否成功 */
	bool InitData(FString& OutError);

	/** 开始战斗。
	 * Deck: 卡组（含升级标记）; EnemyIds: 敌人ID列表; OwnedRelicIds: 持有法宝
	 * PlayerMaxHP: 最大气血; PlayerCurrentHP: 当前气血(-1=满血); EnemyHPBonus: 敌人HP加成(魔道因果)
	 * EnemyLevel: 敌人强化等级(0=普通 1=凶 2=厉 3=煞); Seed: 随机种子。
	 * BattleSerial/Previous* 为可选的权威递增难度输入；省略时保留旧
	 * 调用行为，使用 StartCombatWithDifficulty 可显式启用统一曲线。
	 */
	UFUNCTION(BlueprintCallable)
	bool StartCombat(const TArray<FDeckCard>& Deck, const TArray<FString>& EnemyIds,
		const TArray<FString>& OwnedRelicIds, int32 PlayerMaxHP, int32 PlayerCurrentHP,
		int32 EnemyHPBonus, int32 Seed, int32 EnemyLevel = 0,
		int32 BattleSerial = 0, float PreviousBattleThreatScore = 0.f,
		float PreviousBattleHPScale = 0.f, float PreviousBattleIntentScale = 0.f,
		bool bEliteEncounter = false, bool bBossEncounter = false);

	/** StartCombat variant that consumes a profile previously produced by the manager. */
	bool StartCombatWithDifficulty(const TArray<FDeckCard>& Deck, const TArray<FString>& EnemyIds,
		const TArray<FString>& OwnedRelicIds, int32 PlayerMaxHP, int32 PlayerCurrentHP,
		int32 EnemyHPBonus, int32 Seed, const FCombatDifficultyProfile& Difficulty);

	/** Build the only supported combat difficulty curve. */
	static float ComputeTemplateThreat(const TArray<FEnemyData>& EnemyTemplates);
	static FCombatDifficultyProfile BuildDifficultyProfile(int32 BattleSerial,
		float PreviousThreatScore, float PreviousHPScale, float PreviousIntentScale,
		const TArray<FEnemyData>& EnemyTemplates, bool bElite, bool bBoss);
	static bool ValidateDifficultyProgression(const FCombatDifficultyProfile& Previous,
		const FCombatDifficultyProfile& Current, FString& OutError);

	/** 注册本局运行时生成的敌人数据。StartCombat 会在加载静态 JSON 后合并这些覆盖项。 */
	void RegisterRuntimeEnemies(const TArray<FEnemyData>& RuntimeEnemies);
	/** 注册本局 LLM 创作并已校验的卡牌与法宝。 */
	void RegisterRuntimePlayerContent(const TArray<FCardData>& RuntimeCards, const TArray<FRelicData>& RuntimeRelics);

	/** 出牌。HandIndex 手牌下标, TargetEnemyIndex 目标敌人下标 */
	UFUNCTION(BlueprintCallable)
	bool PlayCard(int32 HandIndex, int32 TargetEnemyIndex);

	/** 结束玩家回合（触发敌方回合，随后自动开始下一玩家回合） */
	UFUNCTION(BlueprintCallable)
	void EndPlayerTurn();

	/** discover_draw 等选择效果生成的候选；选择完成前暂停其他战斗输入。 */
	UPROPERTY(BlueprintReadOnly)
	TArray<FCardInstance> PendingDiscoverChoices;

	/** 将一个候选从抽牌堆移动到手牌。 */
	bool ResolveDiscoverChoice(int32 ChoiceIndex);

	/** 服用丹药（累积丹毒；持有九转丹鼎时效果增强但丹毒+1） */
	UFUNCTION(BlueprintCallable)
	bool UsePill(const FString& PillId);

	UFUNCTION(BlueprintPure)
	bool IsCombatOver() const { return !bCombatActive; }

	UFUNCTION(BlueprintPure)
	bool IsVictory() const { return bVictory; }

	/** 手牌中可用（费用够）的卡牌下标 */
	TArray<int32> GetPlayableCardIndices() const;

	/** 卡牌实际费用（计入本回合费用修正，如「无妄剑境」基础/功法0费） */
	int32 GetEffectiveCost(const FCardInstance& Card) const;

	// ---------- 功法系统（打出后整场战斗生效） ----------
	/** 已激活的功法ID（power 效果注入；允许重复，每个元素代表一个独立实例） */
	UPROPERTY(BlueprintReadOnly)
	TArray<FString> ActivePowerIds;

	/** 已激活的功法名（UI 展示；与 ActivePowerIds 一一对应） */
	UPROPERTY(BlueprintReadOnly)
	TArray<FString> ActivePowerNames;

	bool HasPower(const FString& PowerId) const { return ActivePowerIds.Contains(PowerId); }
	/** 返回同一功法当前已运转的独立实例数量；同名功法不会互相覆盖。 */
	int32 GetPowerCount(const FString& PowerId) const;
	/** Current One Sword base damage before the ordinary per-hit Strength/status rules. */
	int32 GetOneSwordDamage() const { return 8 + OneSwordEnhance * 3; }
	int32 GetOneSwordEnhance() const { return OneSwordEnhance; }

	/** 解析 JSON 效果的动态数值，供战斗结算和卡面变量共用。 */
	int32 ResolveEffectValue(const FCardEffect& Effect, const FCardInstance* Card = nullptr, int32 TargetEnemyIndex = -1) const;

private:
	struct FActiveCardRule
	{
		FCardEffect Effect;
		FString SourceName;
		int32 TriggerCount = 0;
	};

	TMap<FString, FCardData> CardTable;
	TMap<FString, FEnemyData> EnemyTable;
	TMap<FString, FRelicData> RelicTable;
	TMap<FString, FPillData> PillTable;
	/** 地图/剧情临时生成的敌人，不能写回静态 JSON。 */
	TArray<FEnemyData> RuntimeEnemyOverrides;
	TArray<FCardData> RuntimeCardOverrides;
	TArray<FRelicData> RuntimeRelicOverrides;

	/** Temporary profile supplied by StartCombatWithDifficulty. */
	FCombatDifficultyProfile PendingDifficultyProfile;
	bool bHasPendingDifficultyProfile = false;

	TArray<FRelicData> ActiveRelics;
	TArray<FRelicRuntimeState> RelicCounters;
	/** 由原创卡注册的沙箱化事件规则；只保存数据，不执行任意原生代码。 */
	TArray<FActiveCardRule> ActiveCardRules;
	/** 任意 var:<id> 战斗变量，让原创规则可以保存充能、印记与阶段计数。 */
	TMap<FString, int32> ScriptVariables;
	int32 CurrentScriptEventValue = 0;
	FString CurrentScriptEventTag;
	int32 ScriptEventDepth = 0;
	FCardInstance LastPlayedCard;
	int32 CardsPlayedThisTurn = 0;
	int32 LastTurnHandSize = 0;
	FRandomStream Rng;
	int32 NextCardUID = 1;

	/** 本回合玩家是否已受到过攻击（首次受伤减免法器用） */
	bool bPlayerAttackedThisTurn = false;

	/** 正在打出的卡牌UID（discard_random 等效果需排除自身） */
	int32 PlayingCardUID = -1;

	/** 本回合基础卡/功法卡灵力消耗为0（无妄剑境） */
	bool bFreeBasicGongfaThisTurn = false;

	/** 本场战斗已打出的基础卡+功法卡总数（万剑朝宗式计数） */
	int32 BasicGongfaPlayedThisCombat = 0;

	/** 本回合流水剑经已触发的抽牌次数（上限2次/回合） */
	int32 FlowDrawsThisTurn = 0;

	/** 正在结算的卡牌名（power 效果注册功法时用作来源名） */
	FString PendingPowerSourceName;

	// ---- 一剑系统 ----
	/** 【一剑】累计强化层数（当前卡牌契约为每层+3基础伤害） */
	int32 OneSwordEnhance = 0;

	/** 本回合一剑是否已触发额外一次（剑意共鸣） */
	bool bOneSwordMultipliedThisTurn = false;

	/** 本回合一剑是否费用为0（化剑为气） */
	bool bOneSwordFreeThisTurn = false;

	/** 本回合下一张招式费用减少量（万剑归宗） */
	int32 NextZhaoshiCostReduce = 0;

	/** 本回合剑意不绝已触发次数（上限3次/回合） */
	int32 SwordArtFlowTriggeredThisTurn = 0;

public:
	/** 本轮刷新至今实际抽到的牌数（用于动画） */
	int32 PendingDrawCount = 0;

	/** 最近一次回合结束时进入弃牌堆的卡牌实例（用于 UI 弃牌动画） */
	TArray<int32> PendingTurnEndDiscardUIDs;

	/** 本回合第一张基础卡已打过（剑墟遗刻） */
	bool bFirstBasicPlayedThisTurn = false;

	/** 本回合是否获得过力量（双刃剑意诀检测） */
	bool bGotStrengthThisTurn = false;

	/** 本回合是否获得过护甲（以守为攻检测） */
	bool bGotBlockThisTurn = false;

	/** 本回合一剑归真诀是否已生成过复制（每回合一次） */
	bool bFirstOneSwordReturnedThisTurn = false;

	/** 注册功法（按来源卡名记录，用于 UI 展示） */
	void AddPower(const FString& PowerId, const FString& SourceCardName);

	void Log(const FString& Msg) const;

	FCardInstance MakeCard(const FString& CardId) const;
	void DrawCards(int32 Count);
	void BeginDiscoverFromDrawPile(int32 SampleCount);
	void StartPlayerTurn();
	void RunEnemyPhase();
	void CheckCombatEnd();

	void ExecuteCardEffects(const FCardInstance& Card, int32 TargetEnemyIndex);
	void ExecuteEffect(const FCardEffect& Effect, int32 TargetEnemyIndex, bool bFromPlayer, const FCardInstance* Card = nullptr);
	bool ShouldExecuteEffect(const FCardEffect& Effect, const FCardInstance* Card, int32 TargetEnemyIndex);
	void RegisterCardRule(const FCardEffect& Effect, const FString& SourceName);
	void TriggerCardRules(const FString& Trigger, int32& EventValue, int32 TargetEnemyIndex = -1,
		const FString& EventTag = TEXT(""));
	int32 ReadScriptValue(const FString& Source, int32 TargetEnemyIndex) const;
	void WriteScriptValue(const FString& Destination, int32 Value, const FString& WriteMode,
		int32 TargetEnemyIndex, const FString& SourceName);
	TArray<FCardInstance>* ResolveCardZone(const FString& Zone);
	const TArray<FCardInstance>* ResolveCardZone(const FString& Zone) const;
	bool CardMatchesScriptSelector(const FCardInstance& Card, const FString& Selector) const;
	void ExecuteCardZoneEffect(const FCardEffect& Effect);
	/** Generic counters used by authored cards; matching cards in hand gain Amount stacks. */
	void IncrementHandCounters(const FString& Event, const FString& PlayedCardType = TEXT(""), int32 Amount = 1);

	/** 计算攻击伤害（含力量/虚弱/易伤修正） */
	int32 CalcAttackDamage(int32 Base, FCombatantState& Attacker, FCombatantState& Defender) const;
	void DealDamageToEnemy(int32 Amount, int32 EnemyIndex, const FString& SourceName);
	void DealDamageToPlayer(int32 Amount, FCombatantState& Attacker, const FString& SourceName);
	void ApplyBlock(FCombatantState& Target, int32 Amount);
	void ApplyStatusTo(FCombatantState& Target, const FString& StatusId, int32 Stacks, const FString& SourceName);

	/** 施加临时力量（回合结束时消散） */
	void ApplyTempStrength(FCombatantState& Target, int32 Stacks, const FString& SourceName);

	/** 灼烧/中毒 结算（单位回合开始时），返回是否致死 */
	bool TickDotStatuses(FCombatantState& Unit);
	/** 虚弱/易伤 回合结束衰减 */
	void DecayStatuses(FCombatantState& Unit);

	void TriggerRelics(const FString& Trigger);
	void TriggerRelicsWithValue(const FString& Trigger, int32 Value);
	/** 获取被动修饰器总值（遍历法器，根据 Condition 计算） */
	int32 GetRelicPassiveValue(const FString& Condition, int32 ContextValue = 0) const;
	/** 获取被动修饰器浮点系数总和（如首次受伤减免 0.5） */
	float GetRelicPassiveModifierSum(const FString& Condition) const;
	/** 随机弃掉 N 张手牌（触发 on_cards_discarded 法器） */
	void DiscardRandomCards(int32 Count);
	/** 重置法器运行时计数器 */
	void ResetRelicCounters();
	void RollIntent(FEnemyCombatant& Enemy);
	void ExecuteEnemyIntent(FEnemyCombatant& Enemy);
	TArray<int32> GetAliveEnemyIndices() const;

	/** 敌人特殊机制取值（无数值的开关型机制返回1，无此机制返回0） */
	static int32 GetEnemyAbilityValue(const FEnemyData& Data, const FString& AbilityId);
	static FString GetEnemyAbilityValueRaw(const FEnemyData& Data, const FString& AbilityId);

	/** 强化【一剑】+N 或生成一剑 */
	void EnsureOneSwordInHand();

	/** 重置每回合状态量 */
	void ResetTurnState();

	/** 手牌限制相关 */
	int32 HandLimitReduction = 0;

	/** 反震/反击伤害值（功法用，0=未激活） */
	int32 CounterDamage = 0;

	/** 刷新手牌限制（统计存活敌人的 hand_limit 值） */
	void RefreshHandLimit();

	/** 新增召唤敌人 */
	FEnemyCombatant SpawnEnemy(const FString& EnemyId);

	/** 获取敌人除自身外的存活盟友 */
	TArray<int32> GetAliveAlliesExcept(int32 SelfIdx) const;
};
