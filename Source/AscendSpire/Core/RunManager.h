#pragma once

#include "CoreMinimal.h"
#include "GameDataTypes.h"
#include "Combat/CombatTypes.h"
#include "Combat/CombatEngine.h"
#include "CultivationSystem.h"
#include "Map/MapTypes.h"
#include "NarrativeSystem.h"
#include "RunManager.generated.h"

struct FInfiniteNarrativeReward;
struct FInfiniteEnemySpec;
struct FInfiniteVariableUpdate;

USTRUCT(BlueprintType)
struct FRPBodyCondition
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly) FString Name;
	UPROPERTY(BlueprintReadOnly) int32 Severity = 1;
	/** 0 表示无固定截止回合，由后续剧情治愈或恶化。 */
	UPROPERTY(BlueprintReadOnly) int32 RemainingRPTurns = 0;
};

USTRUCT(BlueprintType)
struct FRPNarrativeSkillState
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly) FString Name;
	UPROPERTY(BlueprintReadOnly) int32 Level = 1;
	UPROPERTY(BlueprintReadOnly) bool bKnown = true;
};

USTRUCT(BlueprintType)
struct FRPRelationshipState
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly) FString CharacterId;
	UPROPERTY(BlueprintReadOnly) FString DisplayName;
	UPROPERTY(BlueprintReadOnly) int32 Affinity = 0;
	UPROPERTY(BlueprintReadOnly) FString Bond = TEXT("ally");
};

USTRUCT(BlueprintType)
struct FRPFactionAlertState
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly) FString FactionId;
	UPROPERTY(BlueprintReadOnly) int32 AlertLevel = 0;
	UPROPERTY(BlueprintReadOnly) int32 RemainingRPTurns = 0;
	UPROPERTY(BlueprintReadOnly) TArray<FString> Traits;
};

/** 一轮仍保留原文的 RP 记录；旧记录会在达到预算后压缩为 FRPMemoryEvent。 */
USTRUCT(BlueprintType)
struct FRPNarrativeTurn
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	int32 TurnId = 0;

	UPROPERTY(BlueprintReadOnly)
	int32 Cycle = 0;

	UPROPERTY(BlueprintReadOnly)
	FString Title;

	UPROPERTY(BlueprintReadOnly)
	FString Speaker;

	UPROPERTY(BlueprintReadOnly)
	FString Narration;

	UPROPERTY(BlueprintReadOnly)
	FString Dialogue;

	UPROPERTY(BlueprintReadOnly)
	FString ChoiceText;

	UPROPERTY(BlueprintReadOnly)
	FString ResultSummary;

	UPROPERTY(BlueprintReadOnly)
	FString Next;

	/** 主模型同一份结构化回复中给出的记忆候选，未提供时由本地确定性生成。 */
	UPROPERTY(BlueprintReadOnly)
	FString MemoryJson;
};

/** 可检索的长期剧情记忆。它替代无限增长的一整段散文摘要。 */
USTRUCT(BlueprintType)
struct FRPMemoryEvent
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	FString EventId;

	UPROPERTY(BlueprintReadOnly)
	int32 SourceTurn = 0;

	UPROPERTY(BlueprintReadOnly)
	FString Title;

	UPROPERTY(BlueprintReadOnly)
	FString Summary;

	UPROPERTY(BlueprintReadOnly)
	TArray<FString> Participants;

	UPROPERTY(BlueprintReadOnly)
	TArray<FString> Facts;

	UPROPERTY(BlueprintReadOnly)
	TArray<FString> Unresolved;

	UPROPERTY(BlueprintReadOnly)
	TArray<FString> Keywords;

	UPROPERTY(BlueprintReadOnly)
	float Importance = 0.5f;
};

/** Run 持久状态 */
USTRUCT(BlueprintType)
struct FRunState
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	TArray<FDeckCard> Deck;

	UPROPERTY(BlueprintReadOnly)
	int32 HP = 74;

	UPROPERTY(BlueprintReadOnly)
	int32 MaxHP = 74;

	UPROPERTY(BlueprintReadOnly)
	int32 Gold = 15;

	UPROPERTY(BlueprintReadOnly)
	TArray<FString> RelicIds;

	UPROPERTY(BlueprintReadOnly)
	TArray<FString> PillIds;

	/** 魔道因果（杀人夺宝累积，敌人变强） */
	UPROPERTY(BlueprintReadOnly)
	int32 KillStreak = 0;

	/**
	 * Persistent encounter difficulty ledger.  BattleSerial counts attempted
	 * encounters, not victories; therefore a defeat can never rewind the next
	 * profile.  The three Previous* values are the last authoritative profile.
	 */
	UPROPERTY(BlueprintReadOnly)
	int32 BattleSerial = 0;

	UPROPERTY(BlueprintReadOnly)
	float PreviousBattleThreatScore = 0.f;

	UPROPERTY(BlueprintReadOnly)
	float PreviousBattleHPScale = 0.f;

	UPROPERTY(BlueprintReadOnly)
	float PreviousBattleIntentScale = 0.f;

	UPROPERTY(BlueprintReadOnly)
	FString PreviousBattleTier = TEXT("none");

	UPROPERTY(BlueprintReadOnly)
	int32 PreviousBattleDifficultyStep = 0;

	/** Idempotency guard: a combat victory can grant cultivation only once. */
	UPROPERTY(BlueprintReadOnly)
	int32 LastCultivationRewardBattleSerial = 0;

	/** Per-run cultivation; no cross-run progression is implied. */
	UPROPERTY(BlueprintReadOnly)
	FCultivationState Cultivation;

	/** 当前修道路径ID */
	UPROPERTY(BlueprintReadOnly)
	FString CultivatorPathId = TEXT("sword");

	/** 已完成的修道路径ID列表（跨局永久） */
	UPROPERTY(BlueprintReadOnly)
	TArray<FString> CompletedPaths;

	UPROPERTY(BlueprintReadOnly)
	int32 CurrentNodeId = -1;

	/** 当前所在层（0-based，迷雾探索推进） */
	UPROPERTY(BlueprintReadOnly)
	int32 CurrentFloor = 0;

	/** 精英软保底计数（自第5层起每无精英一层+1，出现精英清零） */
	UPROPERTY(BlueprintReadOnly)
	int32 ElitePity = 0;

	UPROPERTY(BlueprintReadOnly)
	FString Realm = TEXT("炼气期");

	UPROPERTY(BlueprintReadOnly)
	bool bRunActive = false;

	UPROPERTY(BlueprintReadOnly)
	bool bRunVictory = false;

	/** 本局历史最高到达层数（1-based，用于成就结算） */
	UPROPERTY(BlueprintReadOnly)
	int32 HighestFloorThisRun = 0;

	/** 无尽叙事模式：地图节点由 LLM/RP 场景替代，直到战败才结束。 */
	UPROPERTY(BlueprintReadOnly)
	bool bInfiniteNarrativeMode = false;

	/** 已完成的叙事战斗轮数。 */
	UPROPERTY(BlueprintReadOnly)
	int32 InfiniteCycle = 0;

	/** 压缩后的最近 RP 历史，随存档持久化并回注到提示词。 */
	UPROPERTY(BlueprintReadOnly)
	TArray<FString> RPHistory;

	/** 尚未压缩、可逐字回注的最近完整 RP 轮次。 */
	UPROPERTY(BlueprintReadOnly)
	TArray<FRPNarrativeTurn> RPRecentTurns;

	/** 从旧轮次提取的结构化长期记忆。 */
	UPROPERTY(BlueprintReadOnly)
	TArray<FRPMemoryEvent> RPMemoryEvents;

	/** MVU 提交前的状态快照，用于校验失败、读档或以后实现回滚。 */
	UPROPERTY(BlueprintReadOnly)
	TArray<FString> RPStateSnapshots;

	UPROPERTY(BlueprintReadOnly)
	int32 RPTurnSerial = 0;

	/** 上一次实际选中偶发弱事件的 RP 轮次，用于防止连续滞用。 */
	UPROPERTY(BlueprintReadOnly)
	int32 LastRPIncidentalTurn = -100000;

	/** 自由 RP 提交后必须完成的一轮自动衔接；方向已锁定并可跨读档恢复。 */
	UPROPERTY(BlueprintReadOnly)
	bool bInfiniteFreeRPForcedJumpPending = false;

	/** 自由 RP 回应已完成；等待玩家读完并点击继续后才启动自动衔接。 */
	UPROPERTY(BlueprintReadOnly)
	bool bInfiniteFreeRPForcedJumpAwaitingContinue = false;

	UPROPERTY(BlueprintReadOnly)
	FString InfiniteFreeRPForcedDirection;

	/** 最近一场战斗的确定性摘要；不把整份逐伤害日志塞进提示词。 */
	UPROPERTY(BlueprintReadOnly)
	FString LastCombatDigest;

	/**
	 * 最近一场已获胜遭遇的引擎权威结论。它独立于 LLM 的 MVU 状态，
	 * 防止旧的“受伤/回防/战斗未定”等描述让同一敌人反复复活。
	 */
	UPROPERTY(BlueprintReadOnly)
	FString LastResolvedEncounterFact;

	/** MVU 风格世界状态 JSON。 */
	UPROPERTY(BlueprintReadOnly)
	FString RPWorldStateJson = TEXT("{}");

	/** 引擎可直接消费的类型化 RP 状态。 */
	UPROPERTY(BlueprintReadOnly) FString RPCurrentLocation;
	UPROPERTY(BlueprintReadOnly) FString RPCurrentAct = TEXT("opening");
	UPROPERTY(BlueprintReadOnly) TArray<FString> RPEnvironmentTraits;
	/** -3..+3，正数利于玩家、负数利于敌人；进入下一场无限剧情战斗后消耗。 */
	UPROPERTY(BlueprintReadOnly) int32 RPCombatEdge = 0;
	UPROPERTY(BlueprintReadOnly) FString RPCombatEdgeSource;
	UPROPERTY(BlueprintReadOnly) TArray<FRPBodyCondition> RPBodyConditions;
	/** 只参与剧情所有权与后续选择，不强迫每件信件、地图、丹药都变成实体卡。 */
	UPROPERTY(BlueprintReadOnly) TArray<FString> RPNarrativeItems;
	UPROPERTY(BlueprintReadOnly) TArray<FRPNarrativeSkillState> RPSkills;
	UPROPERTY(BlueprintReadOnly) TArray<FRPRelationshipState> RPRelationships;
	UPROPERTY(BlueprintReadOnly) TArray<FRPFactionAlertState> RPFactionAlerts;

	/** 已经提交过的剧情事实键；防止同一取得/损失/奖励在连续轮次重复结算。 */
	UPROPERTY(BlueprintReadOnly)
	TArray<FString> RPSettledFactKeys;

	/** 本局由 LLM 创作并通过本地规则校验的卡牌与法宝；随存档持久化。 */
	UPROPERTY(BlueprintReadOnly)
	TArray<FCardData> DynamicCards;

	UPROPERTY(BlueprintReadOnly)
	TArray<FRelicData> DynamicRelics;

	/** 选择剧情后、战斗结算前的遭遇快照，避免在获得物界面退出后跳过战斗。 */
	UPROPERTY(BlueprintReadOnly)
	bool bInfiniteCombatPending = false;

	/**
	 * 新局固定开场的最小恢复记录。正文由本地 opening_id/index 重建；法器 ID
	 * 与已完成按钮文案在此锁定，读档不会重新 roll 或重新等待旧请求。
	 */
	UPROPERTY(BlueprintReadOnly)
	bool bInfiniteOpeningPending = false;

	UPROPERTY(BlueprintReadOnly)
	int32 PendingInfiniteOpeningIndex = INDEX_NONE;

	UPROPERTY(BlueprintReadOnly)
	FString PendingInfiniteOpeningId;

	UPROPERTY(BlueprintReadOnly)
	TArray<FString> PendingInfiniteOpeningRelicIds;

	/** 三条已完成按钮前缀；法器名称/说明仍由当前真实数据重建。 */
	UPROPERTY(BlueprintReadOnly)
	TArray<FString> PendingInfiniteOpeningChoiceTexts;

	UPROPERTY(BlueprintReadOnly)
	bool bInfiniteOpeningWordingReady = false;

	UPROPERTY(BlueprintReadOnly)
	EMapNodeType PendingInfiniteNodeType = EMapNodeType::Combat;

	UPROPERTY(BlueprintReadOnly)
	FEnemyData PendingInfiniteEnemy;

	/** Complete pending encounter. Empty in legacy single-enemy saves. */
	UPROPERTY(BlueprintReadOnly)
	TArray<FEnemyData> PendingInfiniteEnemies;

	UPROPERTY(BlueprintReadOnly)
	FString PendingInfiniteResultSummary;

	UPROPERTY(BlueprintReadOnly)
	int32 PendingInfiniteEnemyHPBonus = 0;

	UPROPERTY(BlueprintReadOnly)
	TArray<FDeckCard> PendingInfiniteRewardCards;

	UPROPERTY(BlueprintReadOnly)
	TArray<FString> PendingInfiniteRewardRelics;
};

/** 节点遭遇描述 */
USTRUCT(BlueprintType)
struct FNodeEncounter
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	EMapNodeType Type = EMapNodeType::Combat;

	UPROPERTY(BlueprintReadOnly)
	TArray<FString> EnemyIds;

	UPROPERTY(BlueprintReadOnly)
	int32 EnemyHPBonus = 0;

	UPROPERTY(BlueprintReadOnly)
	FString EventId;

	UPROPERTY(BlueprintReadOnly)
	FString StoryText;

	/** 敌人强化等级（0=普通 1=凶 2=厉 3=煞） */
	UPROPERTY(BlueprintReadOnly)
	int32 EnemyLevel = 0;

	/** Engine-owned difficulty ledger captured when this encounter was chosen. */
	UPROPERTY(BlueprintReadOnly)
	int32 BattleSerial = 0;

	UPROPERTY(BlueprintReadOnly)
	float HPScale = 1.f;

	UPROPERTY(BlueprintReadOnly)
	float IntentScale = 1.f;

	UPROPERTY(BlueprintReadOnly)
	float ThreatScore = 0.f;

	UPROPERTY(BlueprintReadOnly)
	FString DifficultyTier = TEXT("normal");

	/** 是否叙事选项（非标准地图节点） */
	bool bIsNarrative = false;
	int32 NarrativeChoiceIndex = -1;
};

/** 迷雾探索选项（一层中的可选方向/遭遇） */
USTRUCT(BlueprintType)
struct FMysteryChoice
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	EMapNodeType Type = EMapNodeType::Combat;

	/** 是否已揭示内容（未揭示只显示方向） */
	UPROPERTY(BlueprintReadOnly)
	bool bRevealed = false;

	/** 战斗类选项的敌人（已揭示时 UI 展示名称） */
	UPROPERTY(BlueprintReadOnly)
	TArray<FString> EnemyIds;

	UPROPERTY(BlueprintReadOnly)
	FString EventId;

	UPROPERTY(BlueprintReadOnly)
	int32 EnemyLevel = 0;

	/** 叙事选项索引（非叙事模式默认为 -1） */
	int32 StoryChoiceIndex = -1;
};

/** 战斗胜利奖励 */
USTRUCT(BlueprintType)
struct FCombatReward
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	int32 Gold = 0;

	UPROPERTY(BlueprintReadOnly)
	TArray<FDeckCard> CardChoices;

	UPROPERTY(BlueprintReadOnly)
	FString RelicId;

	/** 精英/Boss 战可选杀人夺宝 */
	UPROPERTY(BlueprintReadOnly)
	bool bKillLootAvailable = false;

	UPROPERTY(BlueprintReadOnly)
	int32 KillLootGold = 0;
};

/** 剧情推动结果 */
USTRUCT(BlueprintType)
struct FNarrativeResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	FString SummaryText;

	UPROPERTY(BlueprintReadOnly)
	ENarrativeOutcomeType OutcomeType = ENarrativeOutcomeType::NextBeat;

	UPROPERTY(BlueprintReadOnly)
	FString Param;

	UPROPERTY(BlueprintReadOnly)
	int32 HPChange = 0;

	UPROPERTY(BlueprintReadOnly)
	int32 GoldChange = 0;

	UPROPERTY(BlueprintReadOnly)
	FString GainRelicId;

	UPROPERTY(BlueprintReadOnly)
	FString GainCardId;

	UPROPERTY(BlueprintReadOnly)
	bool bUpgraded = false;
};

/** 坊市商品 */
USTRUCT(BlueprintType)
struct FShopItem
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	FString ItemType; // card / relic / pill

	UPROPERTY(BlueprintReadOnly)
	FString ItemId;

	UPROPERTY(BlueprintReadOnly)
	int32 Price = 0;

	UPROPERTY(BlueprintReadOnly)
	bool bSold = false;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnRunLog, const FString&, Message);

/** 元进程（跨局永久成就） */
USTRUCT(BlueprintType)
struct FMetaProgression
{
	GENERATED_BODY()

	/** 历史最高到达层数（1-based，仅展示，不发奖励） */
	UPROPERTY(BlueprintReadOnly)
	int32 HighestFloor = 0;

	/** 已解锁品级: 0=凡品 1=中品 2=上品 3=仙品（由斩妖图鉴数决定） */
	UPROPERTY(BlueprintReadOnly)
	int32 UnlockLevel = 0;

	/** 斩妖图鉴：曾斩杀过的敌人种类（跨局永久，每新增一种计一次成就） */
	UPROPERTY(BlueprintReadOnly)
	TArray<FString> DefeatedEnemyIds;
};

/**
 * Run 管理器 —— 一局游戏的完整元循环
 */
UCLASS(BlueprintType)
class ASCENDSPIRE_API URunManager : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintAssignable)
	FOnRunLog OnLog;

	UPROPERTY(BlueprintReadOnly)
	FRunState State;

	UPROPERTY(BlueprintReadOnly)
	FMapData Map;

	// ---------- 迷雾探索 ----------
	/** 总局数（含 Boss 层） */
	static const int32 TotalFloors = 12;

	/** 当前层的可选方向/遭遇（ChooseOption 后清空，EnsureFloorChoices 重新生成） */
	UPROPERTY(BlueprintReadOnly)
	TArray<FMysteryChoice> CurrentChoices;

	/** 若当前无选项且 Run 未结束，则生成本层选项 */
	void EnsureFloorChoices();

	/** 选择一个方向/遭遇，返回遭遇内容 */
	UFUNCTION(BlueprintCallable)
	FNodeEncounter ChooseOption(int32 Index);

	// ---------- 元进程（跨局成就） ----------
	UPROPERTY(BlueprintReadOnly)
	FMetaProgression Meta;

	/** 本局结束时产生的解锁提示（破纪录才有内容，UI 展示用） */
	UPROPERTY(BlueprintReadOnly)
	TArray<FString> LastUnlockMessages;

	/** 奖励通知（获得卡牌/法宝/丹药/首次斩杀等），UI 弹出后消费 */
	UPROPERTY(BlueprintReadOnly)
	TArray<FString> GainNotifications;

	/** 取出并清空奖励通知 */
	TArray<FString> ConsumeGainNotifications();

	/** 确保元数据已从磁盘加载 */
	void EnsureMetaLoaded();

	/** 稀有度 → 品级: common 0 / uncommon 1 / rare 2 / legendary 3 */
	static int32 RarityToTier(const FString& Rarity);

	/** 开始新的一局 */
	UFUNCTION(BlueprintCallable)
	bool StartNewRun(int32 Seed, bool bInfiniteNarrative = false);
	/** Seed used to initialize the current run's deterministic random stream. */
	int32 GetRunSeed() const { return RunSeed; }

	/**
	 * Advance the persistent encounter ledger and return one authoritative
	 * profile. Call exactly once when a combat encounter is committed.
	 */
	FCombatDifficultyProfile BeginCombatDifficulty(const TArray<FString>& EnemyIds,
		EMapNodeType NodeType);
	const FCombatDifficultyProfile& GetLastCombatDifficulty() const { return LastCombatDifficulty; }

	/** Run-local cultivation choices and the Boss-gated Foundation transition. */
	FCultivationVictoryResult GrantCultivationFromVictory(float AuthoritativeThreatScore);
	FCultivationChoiceResult ApplyCultivationChoice(ECultivationChoice Choice);
	bool TryBreakthroughAfterBoss(FString& OutMessage);
	bool IsCultivationAtBottleneck() const { return FCultivationSystem::IsAtBottleneck(State.Cultivation); }
	bool IsFoundationEstablished() const { return FCultivationSystem::IsFoundation(State.Cultivation); }

	/** 应用经白名单验证的剧情奖励。 */
	void ApplyInfiniteNarrativeReward(const FInfiniteNarrativeReward& Reward);

	/** 首次提交返回 true；同一稳定键再次出现返回 false。空键视为无需去重。 */
	bool TryCommitNarrativeSettlement(const FString& SettlementKey);

	/** 合并 LLM 返回的 MVU 状态补丁；旧接口保留给开场等受信任本地内容。 */
	void ApplyRPWorldStatePatch(const FString& PatchJson);
	bool ApplyRPWorldStatePatchTransactional(const FString& PatchJson, FString& OutError);
	bool RestorePreviousRPWorldState(FString& OutError);
	/** 一次 RP 选择时先衰减旧的临时状态，再原子应用新更新。 */
	void AdvanceRPVariableDurations();
	void ApplyInfiniteVariableUpdates(const TArray<FInfiniteVariableUpdate>& Updates, TArray<FString>& OutReceipts);
	FString BuildRPVariableContext() const;
	int32 GetFactionAlertLevel(const FString& FactionId) const;
	int32 GetInfiniteEnemyHPBonus(const FString& FactionId) const;

	/** 兼容旧存档的简短历史记录。 */
	void AddRPHistory(const FString& Entry);
	void AddRPNarrativeTurn(const FString& Title, const FString& Speaker, const FString& Narration,
		const FString& Dialogue, const FString& ChoiceText, const FString& ResultSummary,
		const FString& Next, const FString& MemoryJson, int32 CompressThreshold, int32 KeepRawRounds,
		int32 UnsummarizedTokenThreshold);
	FString BuildRPRecentContext(int32 MaxRounds, int32 TokenBudget) const;
	FString BuildRPMemoryContext(const FString& Query, int32 TokenBudget) const;
	void RecordInfiniteCombatDigest(const TArray<FString>& EnemyIds, int32 TurnCount,
		int32 HPBefore, int32 HPAfter, const TArray<FString>& CombatLog);

	/** 从现有模板生成受限的 LLM 运行时敌人。 */
	FString RegisterInfiniteEnemy(const FInfiniteEnemySpec& Spec, int32 ChoiceIndex);

	/** 登记即将进入的无尽叙事战斗，供奖励和斩妖图鉴结算。 */
	void PrepareInfiniteCombat(EMapNodeType NodeType, const TArray<FString>& EnemyIds);

	/** 保存/恢复剧情选择后的获得物界面和运行时敌人。 */
	void SavePendingInfiniteCombat(EMapNodeType NodeType, const TArray<FEnemyData>& Enemies, int32 EnemyHPBonus,
		const FString& ResultSummary,
		const TArray<FDeckCard>& RewardCards, const TArray<FString>& RewardRelics);
	bool RestorePendingInfiniteCombat(FNodeEncounter& OutEncounter, FString& OutResultSummary,
		TArray<FDeckCard>& OutRewardCards, TArray<FString>& OutRewardRelics);
	void ClearPendingInfiniteCombat();

	/** 保存/恢复新局固定开场；只接受三个互异真实法器 ID 的已锁定记录。 */
	void SavePendingInfiniteOpening(int32 OpeningIndex, const FString& OpeningId,
		const TArray<FString>& RelicIds, const TArray<FString>& ChoiceTexts,
		bool bWordingReady);
	bool HasPendingInfiniteOpening() const;
	bool RestorePendingInfiniteOpening(int32& OutOpeningIndex, FString& OutOpeningId,
		TArray<FString>& OutRelicIds, TArray<FString>& OutChoiceTexts,
		bool& bOutWordingReady) const;
	void ClearPendingInfiniteOpening();

	/** 战斗胜利结算（返回奖励）。bChoseKillLoot: 是否选择杀人夺宝 */
	UFUNCTION(BlueprintCallable)
	FCombatReward ResolveCombatVictory(bool bChoseKillLoot, int32 PlayerRemainingHP, int32 GoldFromCombat);

	/** 选取奖励卡牌 */
	UFUNCTION(BlueprintCallable)
	void PickRewardCard(const FDeckCard& Card);

	/** 随机一张奖励卡（含升级概率+敌人强度调整+排除池过滤） */
	FDeckCard RollRandomRewardCard(EMapNodeType NodeType) const;

	/** 战斗失败结算 */
	UFUNCTION(BlueprintCallable)
	void ResolveDefeat();

	/** 打坐调息: true=恢复30%气血, false=随机升级一张牌 */
	UFUNCTION(BlueprintCallable)
	void ResolveRest(bool bHeal);

	/** 事件选项结算，返回结果文本 */
	UFUNCTION(BlueprintCallable)
	FString ResolveEventChoice(int32 ChoiceIndex);

	/** 获取坊市商品 */
	UFUNCTION(BlueprintCallable)
	TArray<FShopItem> GetShopStock();
	/** RP 游戏功能网关：生成指定品级、指定价格倍率的纯卡牌商店。 */
	void GenerateNarrativeShopStock(const FString& Rarity, float PriceMultiplier, int32 Count = 6);
	/** RP 选择界面使用的确定性卡组操作。 */
	bool RemoveDeckCardAt(int32 DeckIndex, bool bAllowCurse = false);
	/** RP 常驻整理：花费固定灵石删除单个卡组实例，允许删至空卡组。成功时已立即存档。 */
	bool RemoveDeckCardForGold(int32 DeckIndex, int32 GoldCost, FString& OutCardName);
	bool UpgradeDeckCardAt(int32 DeckIndex);
	/** Prevent commandlet fixtures from reading or modifying the player's permanent authored library. */
	void SetPersistentAuthoredContentEnabledForAutomationTest(bool bEnabled)
	{
		bPersistentAuthoredContentEnabled = bEnabled;
	}

	/** 购买商品 */
	UFUNCTION(BlueprintCallable)
	bool BuyShopItem(int32 ItemIndex);

	/** 获取随机初始法器（排除已拥有的） */
	UFUNCTION(BlueprintCallable)
	TArray<FString> RollInitialRelicChoices(int32 Count = 3);
	/** Unowned built-in relics available to the infinite-narrative weighted route pool. */
	TArray<FString> GetAvailableFixedNarrativeRelicIds() const;

	/** 获取所有修道路径（预留） */
	TArray<FSectPath> GetAllPaths() const { return {}; }

	/** 选择修道路径（预留） */
	void SetCultivatorPath(const FString& PathId) { State.CultivatorPathId = PathId; }

	const FEventData* GetCurrentEvent() const;

	/** 查卡牌数据 */
	const FCardData* GetCardData(const FString& CardId) const { return CardTable.Find(CardId); }

	/** 查法宝数据 */
	const FRelicData* GetRelicData(const FString& RelicId) const { return RelicTable.Find(RelicId); }
	const TArray<FCardData>& GetDynamicCards() const { return State.DynamicCards; }
	const TArray<FRelicData>& GetDynamicRelics() const { return State.DynamicRelics; }
	/** Cross-save LLM content library, available from the title-screen collection. */
	void RefreshPersistentAuthoredContent() { LoadPersistentAuthoredContent(); }
	const TArray<FCardData>& GetPersistentAuthoredCards() const { return PersistentAuthoredCards; }
	const TArray<FRelicData>& GetPersistentAuthoredRelics() const { return PersistentAuthoredRelics; }
	bool DeletePersistentAuthoredCard(const FString& CardId);
	bool DeletePersistentAuthoredRelic(const FString& RelicId);

	/** 查敌人数据 */
	const FEnemyData* GetEnemyData(const FString& EnemyId) const { return EnemyTable.Find(EnemyId); }

	/** 初始化叙事系统（数据 + 流程） */
	void InitNarrative(UNarrativeSystem* Sys, const FString& ActId);

	/** 保存当前 Run 到存档 */
	UFUNCTION(BlueprintCallable)
	bool SaveRun() const;

	/** 从存档恢复 Run（存在存档则返回 true） */
	UFUNCTION(BlueprintCallable)
	bool LoadRun();

	/** 是否有存档 */
	static bool HasSaveFile();

public:
	// ---------- 叙事系统 ----------
	UPROPERTY()
	UNarrativeSystem* NarrativeSys;

	FString CurrentActId;
	FString CurrentBeatId;
	FString PendingNarrativeSummary;
	bool bNarrativeMode = false;
	/** 剧情推进（展示结果+应用效果），返回是否进入游戏节点（战斗/事件等） */
	bool AdvanceNarrative(int32 ChoiceIndex);

	/** 获取当前剧情 Beat */
	FNarrativeBeat GetCurrentNarrativeBeat() const;

private:
	TMap<FString, FCardData> CardTable;
	TMap<FString, FEnemyData> EnemyTable;
	TMap<FString, FRelicData> RelicTable;
	TMap<FString, FPillData> PillTable;
	TMap<FString, FEventData> EventTable;

	TArray<FString> NormalEnemyIds;
	TArray<FString> EliteEnemyIds;
	/** Cross-save authored content library. Successful LLM creations are retained here. */
	TArray<FCardData> PersistentAuthoredCards;
	TArray<FRelicData> PersistentAuthoredRelics;
	bool bPersistentAuthoredContentEnabled = true;

	mutable FRandomStream Rng;
	int32 RunSeed = 0;
	FString CurrentEventId;
	TArray<FShopItem> CurrentShopStock;
	FCombatReward PendingReward;
	FCombatDifficultyProfile LastCombatDifficulty;

	void Log(const FString& Msg) const;
	bool LoadAllData(FString& OutError);
	void LoadPersistentAuthoredContent();
	void SavePersistentAuthoredContent() const;
	static FString GetAuthoredContentSavePath();

	TArray<FString> RollEnemyGroup(EMapNodeType Type);
	FString RollRandomCard(const FString& RarityFilter = TEXT("")) const;
	FString RollRandomRelic() const;
	FString RollRandomPill() const;
	FString RollRarity(FRandomStream& Stream) const;
	int32 CardPrice(const FString& Rarity) const;
	int32 RelicPrice(const FString& Rarity) const;

	void ApplyEventEffect(const FEventEffect& Effect);
	void GenerateShopStock();
	void Breakthrough();
	void SyncCultivationToRunRealm();

	// ---------- 迷雾探索内部 ----------
	/** 由层数推导敌人强化等级: 0-2→0, 3-5→1, 6-8→2, 9-11→3 */
	static int32 EnemyLevelForFloor(int32 Floor);
	/**
	 * 从现有敌人模板生成一个确定性的运行时变体。
	 * 变体只存在于本局内，仍然使用 FEnemyData 的同一套字段，便于后续由编辑器/LLM 直接描述。
	 */
	FString MakeProceduralEnemyVariant(const FEnemyData& Template, int32 Floor, int32 ChoiceIndex,
		int32 SlotIndex, bool bElite, FRandomStream& Stream);
	/** 最近一场战斗的节点类型与敌人（胜利结算/斩妖图鉴用） */
	EMapNodeType LastCombatNodeType = EMapNodeType::Combat;
	TArray<FString> LastCombatEnemyIds;
	/** 斩杀图鉴登记（新种类 → 成就提示 + 品级解锁判定） */
	void RegisterEnemyKills(const TArray<FString>& EnemyIds);

	// ---------- 元进程内部 ----------
	bool bMetaLoaded = false;
	static FString GetMetaSavePath();
	void SaveMetaToDisk() const;
	/** 局末结算：只更新最高层展示，奖励由斩妖图鉴发放 */
	void UpdateMetaOnRunEnd();
};
