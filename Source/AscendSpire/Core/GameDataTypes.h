#pragma once

#include "CoreMinimal.h"
#include "GameDataTypes.generated.h"

/** 卡牌效果动作 */
USTRUCT(BlueprintType)
struct FCardEffect
{
	GENERATED_BODY()

	/**
	 * 事件触发点。on_play 立即结算；其余触发点会在打出卡牌后注册为本场战斗规则。
	 * 当前通用事件包括 before_gain_block / after_gain_block / on_turn_start /
	 * on_turn_end / on_card_played / on_damage_dealt / on_damage_taken。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString Trigger = TEXT("on_play");

	/** instant / turn / combat。非 on_play 规则默认持续至战斗结束。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString Duration = TEXT("instant");

	/** 通用脚本读取源：event_value / self_block / self_hp / missing_hp /
	 *  self_spirit / self_status:<id> / target_status:<id>。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString Source;

	/** 通用脚本写入目标：self_block / self_hp / self_spirit / self_status:<id>。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString Destination;

	/** transfer 等动作完成后是否清空来源；event_value 表示拦截本次事件。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bConsumeSource = false;

	/** transfer 写入方式：add / set / min / max / multiply。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString WriteMode = TEXT("add");

	/** 持续规则最多触发次数；0 表示在持续期内不限次数。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 MaxTriggers = 0;

	/** 动作类型: damage / damage_all / damage_random / damage_per_block / block / draw / discover_draw /
	 *  gain_spirit / spirit_next_turn / discard_random / self_damage / amplify_status /
	 *  heal / apply_status / gain_gold / cleanse_toxicity /
	 *  power(功法注册) / damage_per_status / damage_all_per_basic_gongfa /
	 *  cost_free_basic_gongfa / block_per_status */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString Action;

	/** 数值（伤害/护甲/抽牌数等） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 Value = 0;

	/** 目标: enemy / self / all_enemies / random_enemy */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString Target = TEXT("enemy");

	/** 重复次数（多段攻击） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 Times = 1;

	/** 状态效果ID（apply_status 时使用）: burn / poison / weak / vulnerable / strength / dexterity */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString StatusId;

	/** 状态层数 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 StatusStacks = 0;

	/** 通用字符串参数（卡牌引用或区域选择器 random/highest_cost/lowest_cost/type:<type>）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString Param;

	/** 可选执行条件（例如 self_hp_below:50 / counter_at_least:3） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString Condition;

	/** 动态数值来源（例如 counter / self_block / self_status:strength） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString ScaleBy;

	/** 每组动态变量增加的数值 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 ScaleFactor = 0;

	/** 动态变量每多少点算一组 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 ScaleDivisor = 1;

	/** 执行概率，0~1 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float Chance = 1.f;
};

/** 卡牌生效时的表现层定义。动画由 UI 解释，未知值会安全回退到通用命中特效。 */
USTRUCT(BlueprintType)
struct FCardVisualData
{
	GENERATED_BODY()

	/** none / slash / fireball / impact / block / heal / draw */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString Animation = TEXT("none");

	/** none / sword_slash / fireball / block / heal / draw */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString Sound = TEXT("none");

	/** 十六进制颜色，例如 #EAF7FF */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString Accent = TEXT("#FFFFFF");

	/** 动画时长（秒） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float Duration = 0.42f;

	/** 受击时屏幕震动强度；0 表示不震动 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float Intensity = 4.f;

	/** 多段视觉次数（剑气纵横等） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 Count = 1;
};

/** 卡牌数据（JSON 驱动） */
USTRUCT(BlueprintType)
struct FCardData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString Id;

	/** 中文名，如 火球术 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString Name;

	/** 类型: spell(法术) / sword(剑诀) / body(体术) / talisman(符箓) / skill(功法) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString Type;

	/** 稀有度: common / uncommon / rare */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString Rarity = TEXT("common");

	/** 灵力消耗 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 Cost = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString Description;

	/** 打出后是否消耗（本场战斗移除） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bExhaust = false;

	/** 保留：回合结束时留在手牌中，不被弃掉 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bRetain = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TArray<FCardEffect> Effects;

	/** 升级后效果（为空则用基础效果） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TArray<FCardEffect> UpgradedEffects;

	/** 升级后费用（-1 = 不变） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 UpgradedCost = -1;

	/** 升级后描述 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString UpgradedDescription;

	/** 升级后计数条件 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString UpgradedCounterCondition;

	/** 剧情小字（卡面底部不起眼的点缀文本） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString Flavor;

	/** 卡面图路径（Content/Art 下相对路径） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString ArtPath;

	/** 职业归属: sword / danxiu / fuxiu（空=全职业通用） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString Class;

	/** 计数条件: on_basic_play / on_basic_gongfa_play（满足条件时手牌中此卡 RepeatCount+1） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString CounterCondition;

	/** 卡牌打出后的动画与音效定义 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FCardVisualData Visual;
};

/** 敌人意图（AI 行为） */
USTRUCT(BlueprintType)
struct FEnemyIntent
{
	GENERATED_BODY()

	/** attack / attack_multi / defend / buff / debuff */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString Action;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 Value = 0;

	/** 多段攻击次数 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 Times = 1;

	/** 选取权重 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 Weight = 1;

	/** buff/debuff 时施加的状态ID */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString StatusId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 StatusStacks = 0;
};

/** 敌人数据 */
USTRUCT(BlueprintType)
struct FEnemyData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString Id;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString Name;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 MaxHP = 10;

	/** 阶级: normal / elite / boss */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString Tier = TEXT("normal");

	/** 剧情文本（Boss 出场等） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString Story;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TArray<FEnemyIntent> Intents;

	/** 特殊机制列表，"id:value" 格式（无数值时省略:value）。
	 *  可用id: first_strike(抢先出手) / enrage_half(半血力量+N) / thorns(受击反噬N) /
	 *  regen(每回合回血N) / block_aura(每回合罡气+N) / poison_aura(每回合你中毒+N) /
	 *  drain(攻击吸血N) / start_weak(开场虚弱N) / start_vulnerable(开场易伤N) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TArray<FString> Abilities;

	/** 机制描述（敌人卡面 UI 展示） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString AbilityDesc;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString ArtPath;
};

/** 法宝（遗物）数据 */
USTRUCT(BlueprintType)
struct FRelicData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString Id;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString Name;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString Rarity = TEXT("common");

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString Description;

	/** 触发时机: combat_start / turn_start / on_kill / on_victory / on_loot / pill_modifier /
	 *  on_card_played / on_player_turn_end / on_hp_changed / on_damage_dealt /
	 *  on_cards_discarded / on_reshuffle */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString Trigger;

	/** 条件类型（被动修饰器使用）: hp_missing_5pct / unused_hand / first_damage_halve /
	 *  per_discarded_card / card_type_filter */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString Condition;

	/** 计数阈值（如每N张牌触发） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 Counter = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FCardEffect Effect;

	/** 通用数值系数（如丹药效果+50% 存 0.5，被动修饰值） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float Modifier = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString ArtPath;
};

/** 事件选项效果 */
USTRUCT(BlueprintType)
struct FEventEffect
{
	GENERATED_BODY()

	/** gold / hp / max_hp / add_relic / add_pill / upgrade_random / add_card / kill_streak / remove_card */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString Action;

	/** 数值或卡牌/法宝ID（random = 随机） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString Param;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 Value = 0;
};

/** 事件选项 */
USTRUCT(BlueprintType)
struct FEventChoice
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString Text;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TArray<FEventEffect> Effects;

	/** 结果描述文本 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString ResultText;
};

/** 随机事件数据 */
USTRUCT(BlueprintType)
struct FEventData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString Id;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString Title;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString Text;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TArray<FEventChoice> Choices;
};

/** 丹药数据 */
USTRUCT(BlueprintType)
struct FPillData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString Id;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString Name;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString Description;

	/** 服用效果 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FCardEffect Effect;

	/** 丹毒值（累积，过高产生负面效果） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 Toxicity = 1;
};

/** 修道路径（剧情系统预留） */
USTRUCT(BlueprintType)
struct FSectPath
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString Id;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString Name;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString Description;

	/** 起始卡组ID列表 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TArray<FString> StartingDeck;

	/** 路径专属卡牌ID列表 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TArray<FString> ExclusiveCardIds;

	/** 路径专属法器ID列表 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TArray<FString> ExclusiveRelicIds;

	/** 剧情节点ID序列（预留） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TArray<FString> StoryNodeIds;
};
