#pragma once

#include "CoreMinimal.h"
#include "GameDataTypes.h"
#include "Combat/CombatTypes.h"
#include "Map/MapTypes.h"
#include "NarrativeSystem.h"
#include "RunManager.generated.h"

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
	bool StartNewRun(int32 Seed);

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

	/** 购买商品 */
	UFUNCTION(BlueprintCallable)
	bool BuyShopItem(int32 ItemIndex);

	/** 获取随机初始法器（排除已拥有的） */
	UFUNCTION(BlueprintCallable)
	TArray<FString> RollInitialRelicChoices(int32 Count = 3);

	/** 获取所有修道路径（预留） */
	TArray<FSectPath> GetAllPaths() const { return {}; }

	/** 选择修道路径（预留） */
	void SetCultivatorPath(const FString& PathId) { State.CultivatorPathId = PathId; }

	const FEventData* GetCurrentEvent() const;

	/** 查卡牌数据 */
	const FCardData* GetCardData(const FString& CardId) const { return CardTable.Find(CardId); }

	/** 查法宝数据 */
	const FRelicData* GetRelicData(const FString& RelicId) const { return RelicTable.Find(RelicId); }

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

	mutable FRandomStream Rng;
	int32 RunSeed = 0;
	FString CurrentEventId;
	TArray<FShopItem> CurrentShopStock;
	FCombatReward PendingReward;

	void Log(const FString& Msg) const;
	bool LoadAllData(FString& OutError);

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

	// ---------- 迷雾探索内部 ----------
	/** 由层数推导敌人强化等级: 0-2→0, 3-5→1, 6-8→2, 9-11→3 */
	static int32 EnemyLevelForFloor(int32 Floor);
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
