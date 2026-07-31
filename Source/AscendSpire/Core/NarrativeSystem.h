#pragma once

#include "CoreMinimal.h"
#include "NarrativeSystem.generated.h"

UENUM(BlueprintType)
enum class ENarrativeOutcomeType : uint8
{
	NextBeat,
	Combat,
	Elite,
	Boss,
	Rest,
	Shop,
	Event,
	GameOver
};

USTRUCT(BlueprintType)
struct FNarrativeOutcome
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	FString SummaryText;

	UPROPERTY(BlueprintReadOnly)
	ENarrativeOutcomeType Type = ENarrativeOutcomeType::NextBeat;

	/** 下一个 Beat ID / 敌人 ID / 事件 ID */
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

USTRUCT(BlueprintType)
struct FNarrativeChoice
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	FString Text;

	UPROPERTY(BlueprintReadOnly)
	FNarrativeOutcome Outcome;
};

USTRUCT(BlueprintType)
struct FNarrativeBeat
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	FString BeatId;

	UPROPERTY(BlueprintReadOnly)
	FString NarratorText;

	UPROPERTY(BlueprintReadOnly)
	TArray<FNarrativeChoice> Choices;
};

/**
 * LLM 剧情生成器接口 —— 预留替换 JSON 驱动
 */
class INarrativeGenerator
{
public:
	virtual ~INarrativeGenerator() {}
	virtual FNarrativeBeat GenerateBeat(const FString& ActId, const FString& CurrentBeatId,
		const TArray<FString>& ChoiceHistory) = 0;
};

/**
 * 叙事系统 —— 管理剧情推进、beat 查找、LLM 接口
 */
UCLASS()
class ASCENDSPIRE_API UNarrativeSystem : public UObject
{
	GENERATED_BODY()

public:
	void LoadFromJSON(const FString& JSONPath);

	/** 获取指定 Act 的起始 Beat */
	FNarrativeBeat GetStartBeat(const FString& ActId) const;

	/** 按 ID 查找 Beat */
	const FNarrativeBeat* FindBeat(const FString& BeatId) const;

	/** 设置 LLM 生成器（为空则使用 JSON 驱动） */
	void SetGenerator(TSharedPtr<INarrativeGenerator> Gen) { Generator = Gen; }

	/** 是否有 LLM 生成器 */
	bool HasGenerator() const { return Generator.IsValid(); }

private:
	struct FActData
	{
		FString ActId;
		FString StartBeatId;
		TMap<FString, FNarrativeBeat> Beats;
	};

	TArray<FActData> Acts;
	TSharedPtr<INarrativeGenerator> Generator;
};
