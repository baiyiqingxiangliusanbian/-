#pragma once

#include "CoreMinimal.h"
#include "Core/GameDataTypes.h"
#include "CombatTypes.generated.h"

/** 手牌中的一张卡（实例） */
USTRUCT(BlueprintType)
struct FCardInstance
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	FCardData Data;

	UPROPERTY(BlueprintReadOnly)
	bool bUpgraded = false;

	/** 实例唯一ID */
	UPROPERTY(BlueprintReadOnly)
	int32 UID = 0;

	/** 重复计数（万剑朝宗式等计数卡使用） */
	UPROPERTY(BlueprintReadOnly)
	int32 RepeatCount = 0;

	int32 GetCost() const
	{
		return (bUpgraded && Data.UpgradedCost >= 0) ? Data.UpgradedCost : Data.Cost;
	}

	const TArray<FCardEffect>& GetEffects() const
	{
		return (bUpgraded && Data.UpgradedEffects.Num() > 0) ? Data.UpgradedEffects : Data.Effects;
	}

	FString GetDisplayName() const
	{
		return bUpgraded ? Data.Name + TEXT("+") : Data.Name;
	}

	FString GetCounterCondition() const
	{
		return (bUpgraded && !Data.UpgradedCounterCondition.IsEmpty()) ? Data.UpgradedCounterCondition : Data.CounterCondition;
	}
};

/** 卡组中的一张牌（ID + 升级标记），用于跨战斗持久化 */
USTRUCT(BlueprintType)
struct FDeckCard
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite)
	FString CardId;

	UPROPERTY(BlueprintReadWrite)
	bool bUpgraded = false;
};

/** 状态效果实例（挂在战斗单位身上） */
USTRUCT(BlueprintType)
struct FStatusInstance
{
	GENERATED_BODY()

	/** burn(灼烧) / poison(中毒) / weak(虚弱) / vulnerable(易伤) / strength(力量) */
	UPROPERTY(BlueprintReadOnly)
	FString Id;

	UPROPERTY(BlueprintReadOnly)
	int32 Stacks = 0;
};

/** 法器运行时计数状态 */
USTRUCT(BlueprintType)
struct FRelicRuntimeState
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	FString RelicId;

	UPROPERTY(BlueprintReadOnly)
	int32 CurrentCount = 0;
};

/** 战斗单位状态（玩家或敌人共用） */
USTRUCT(BlueprintType)
struct FCombatantState
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	FString Name;

	UPROPERTY(BlueprintReadOnly)
	FString EnemyId;

	UPROPERTY(BlueprintReadOnly)
	int32 HP = 1;

	UPROPERTY(BlueprintReadOnly)
	int32 MaxHP = 1;

	/** 护体罡气（护甲） */
	UPROPERTY(BlueprintReadOnly)
	int32 Block = 0;

	UPROPERTY(BlueprintReadOnly)
	TArray<FStatusInstance> Statuses;

	bool IsAlive() const { return HP > 0; }

	int32 GetStatusStacks(const FString& StatusId) const
	{
		for (const FStatusInstance& S : Statuses)
		{
			if (S.Id == StatusId) return S.Stacks;
		}
		return 0;
	}

	void AddStatus(const FString& StatusId, int32 Delta)
	{
		for (FStatusInstance& S : Statuses)
		{
			if (S.Id == StatusId)
			{
				S.Stacks += Delta;
				if (S.Stacks <= 0) Statuses.RemoveAll([&](const FStatusInstance& X) { return X.Id == StatusId; });
				return;
			}
		}
		if (Delta > 0)
		{
			FStatusInstance NewS;
			NewS.Id = StatusId;
			NewS.Stacks = Delta;
			Statuses.Add(NewS);
		}
	}
};
