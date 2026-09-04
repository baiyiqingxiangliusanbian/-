#pragma once

#include "CoreMinimal.h"
#include "CultivationSystem.generated.h"

/**
 * The three deliberately small, run-local cultivation choices.  They are
 * data, rather than narrative labels, so a choice made by an LLM can never
 * silently invent a fourth progression path.
 */
UENUM(BlueprintType)
enum class ECultivationChoice : uint8
{
	BodyTempering UMETA(DisplayName = "锻体"),
	QiAbsorption UMETA(DisplayName = "纳气"),
	Insight UMETA(DisplayName = "悟法")
};

/** Result of one run-local cultivation choice. */
USTRUCT(BlueprintType)
struct FCultivationChoiceResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	bool bApplied = false;

	UPROPERTY(BlueprintReadOnly)
	ECultivationChoice Choice = ECultivationChoice::BodyTempering;

	UPROPERTY(BlueprintReadOnly)
	int32 CultivationGained = 0;

	UPROPERTY(BlueprintReadOnly)
	int32 MaxHPGained = 0;

	UPROPERTY(BlueprintReadOnly)
	int32 QiReserveGained = 0;

	UPROPERTY(BlueprintReadOnly)
	int32 InsightChargeGained = 0;

	UPROPERTY(BlueprintReadOnly)
	int32 RealmBefore = 1;

	UPROPERTY(BlueprintReadOnly)
	int32 RealmAfter = 1;

	UPROPERTY(BlueprintReadOnly)
	bool bReachedBottleneck = false;

	UPROPERTY(BlueprintReadOnly)
	FString Summary;
};

/** Result of authority-granted cultivation earned by a completed combat. */
USTRUCT(BlueprintType)
struct FCultivationVictoryResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	bool bApplied = false;

	UPROPERTY(BlueprintReadOnly)
	float ThreatScore = 0.f;

	UPROPERTY(BlueprintReadOnly)
	int32 CultivationGained = 0;

	UPROPERTY(BlueprintReadOnly)
	int32 CultivationBefore = 0;

	UPROPERTY(BlueprintReadOnly)
	int32 CultivationAfter = 0;

	UPROPERTY(BlueprintReadOnly)
	int32 RealmBefore = 1;

	UPROPERTY(BlueprintReadOnly)
	int32 RealmAfter = 1;

	UPROPERTY(BlueprintReadOnly)
	int32 NewChoicesQueued = 0;

	UPROPERTY(BlueprintReadOnly)
	bool bReachedBottleneck = false;

	UPROPERTY(BlueprintReadOnly)
	FString Summary;
};

/**
 * Persistent cultivation state for one run.
 *
 * CultivationPoints is an absolute run-local total.  The thresholds are
 * intentionally modest and fixed in code/data so that progress is legible
 * and cannot scale with card damage.  RealmIndex 1..9 are Qi Condensation
 * layers; 10 is Foundation Establishment.
 */
USTRUCT(BlueprintType)
struct FCultivationState
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	int32 RealmIndex = 1;

	UPROPERTY(BlueprintReadOnly)
	FString RealmName = TEXT("炼气一层");

	UPROPERTY(BlueprintReadOnly)
	int32 CultivationPoints = 0;

	/** Absolute threshold for the next Qi layer; 0 means a bottleneck or Foundation. */
	UPROPERTY(BlueprintReadOnly)
	int32 NextThreshold = 100;

	/** Layer nine has reached its threshold but still needs the Boss gate. */
	UPROPERTY(BlueprintReadOnly)
	bool bAtBottleneck = false;

	/** Number of three-way choices waiting from realm breakthroughs. */
	UPROPERTY(BlueprintReadOnly)
	int32 PendingChoiceCount = 0;

	/** Last authority-approved threat contribution; useful for save diagnostics. */
	UPROPERTY(BlueprintReadOnly)
	float LastVictoryThreatScore = 0.f;

	UPROPERTY(BlueprintReadOnly)
	int32 TotalCultivationFromVictories = 0;

	/** A Boss has been defeated while the bottleneck was ready. */
	UPROPERTY(BlueprintReadOnly)
	bool bBossBreakthroughReady = false;

	UPROPERTY(BlueprintReadOnly)
	bool bBossDefeated = false;

	UPROPERTY(BlueprintReadOnly)
	bool bFoundationEstablished = false;

	/** Small side benefits; none directly adds card damage. */
	UPROPERTY(BlueprintReadOnly)
	int32 BodyTemperingCount = 0;

	UPROPERTY(BlueprintReadOnly)
	int32 QiAbsorptionCount = 0;

	UPROPERTY(BlueprintReadOnly)
	int32 InsightCount = 0;

	UPROPERTY(BlueprintReadOnly)
	int32 MaxHPBonus = 0;

	UPROPERTY(BlueprintReadOnly)
	int32 QiReserve = 0;

	UPROPERTY(BlueprintReadOnly)
	int32 InsightCharges = 0;
};

/**
 * Deterministic, engine-owned cultivation rules.  Narrative code may choose
 * a choice, but it cannot alter thresholds, bypass the bottleneck, or grant a
 * combat-sized stat multiplier.
 */
class ASCENDSPIRE_API FCultivationSystem
{
public:
	static constexpr int32 MinQiRealm = 1;
	static constexpr int32 MaxQiRealm = 9;
	static constexpr int32 FoundationRealm = 10;

	static void Initialize(FCultivationState& State);
	static int32 GetRealmThreshold(int32 RealmIndex);
	static FString GetRealmName(int32 RealmIndex);
	static bool IsFoundation(const FCultivationState& State);
	static bool IsAtBottleneck(const FCultivationState& State);

	/** Add run-local cultivation from one completed combat's authoritative threat. */
	static FCultivationVictoryResult GrantCultivationFromVictory(FCultivationState& State,
		float ThreatScore);

	/** Claim one queued realm-upgrade choice; choices never create cultivation. */
	static FCultivationChoiceResult ApplyChoice(FCultivationState& State, ECultivationChoice Choice);

	/** Record a Boss gate and establish Foundation when the ninth-layer bottleneck is ready. */
	static bool MarkBossDefeated(FCultivationState& State, FString& OutMessage);
	static bool TryBreakthroughAfterBoss(FCultivationState& State, FString& OutError);

	/** Normalize old/malformed save data without inventing progress. */
	static bool Normalize(FCultivationState& State, FString& OutWarning);
	static bool Validate(const FCultivationState& State, FString& OutError);
	static FString ChoiceName(ECultivationChoice Choice);
};
