#include "CultivationSystem.h"

namespace
{
	// Absolute totals.  Each step takes longer than the previous one, while a
	// normal run can still reach layer nine from a reasonable number of choices.
	static const int32 QiThresholds[] = {
		0,    // unused index 0
		0,    // layer 1 starts at zero
		100,  // layer 2
		225,  // layer 3
		375,  // layer 4
		550,  // layer 5
		750,  // layer 6
		975,  // layer 7
		1225, // layer 8
		1500  // layer 9 / bottleneck
	};

	static const TCHAR* QiNames[] = {
		TEXT("未入道"), TEXT("炼气一层"), TEXT("炼气二层"), TEXT("炼气三层"),
		TEXT("炼气四层"), TEXT("炼气五层"), TEXT("炼气六层"), TEXT("炼气七层"),
		TEXT("炼气八层"), TEXT("炼气九层"), TEXT("筑基期")
	};

	static void RefreshDerivedFields(FCultivationState& State)
	{
		State.RealmIndex = FMath::Clamp(State.RealmIndex,
			FCultivationSystem::MinQiRealm, FCultivationSystem::FoundationRealm);
		if (State.RealmIndex >= FCultivationSystem::FoundationRealm || State.bFoundationEstablished)
		{
			State.RealmIndex = FCultivationSystem::FoundationRealm;
			State.bFoundationEstablished = true;
			State.bAtBottleneck = false;
			State.NextThreshold = 0;
		}
		else
		{
			State.RealmName = FCultivationSystem::GetRealmName(State.RealmIndex);
			if (State.RealmIndex == FCultivationSystem::MaxQiRealm && State.CultivationPoints >= FCultivationSystem::GetRealmThreshold(FCultivationSystem::MaxQiRealm))
			{
				State.CultivationPoints = FMath::Max(State.CultivationPoints,
					FCultivationSystem::GetRealmThreshold(FCultivationSystem::MaxQiRealm));
				State.bAtBottleneck = true;
				State.NextThreshold = 0;
			}
			else
			{
				State.bAtBottleneck = false;
				State.NextThreshold = FCultivationSystem::GetRealmThreshold(State.RealmIndex + 1);
			}
		}
		if (State.RealmIndex == FCultivationSystem::FoundationRealm)
			State.RealmName = FCultivationSystem::GetRealmName(FCultivationSystem::FoundationRealm);
		State.MaxHPBonus = FMath::Clamp(State.MaxHPBonus, 0, 18);
		State.QiReserve = FMath::Clamp(State.QiReserve, 0, 3);
		State.InsightCharges = FMath::Clamp(State.InsightCharges, 0, 3);
	}
}

void FCultivationSystem::Initialize(FCultivationState& State)
{
	State = FCultivationState();
	RefreshDerivedFields(State);
}

int32 FCultivationSystem::GetRealmThreshold(int32 RealmIndex)
{
	if (RealmIndex <= MinQiRealm) return QiThresholds[MinQiRealm];
	if (RealmIndex >= MaxQiRealm) return QiThresholds[MaxQiRealm];
	return QiThresholds[RealmIndex];
}

FString FCultivationSystem::GetRealmName(int32 RealmIndex)
{
	const int32 Index = FMath::Clamp(RealmIndex, 0, FoundationRealm);
	return QiNames[Index];
}

bool FCultivationSystem::IsFoundation(const FCultivationState& State)
{
	return State.bFoundationEstablished || State.RealmIndex >= FoundationRealm;
}

bool FCultivationSystem::IsAtBottleneck(const FCultivationState& State)
{
	return !IsFoundation(State) && State.bAtBottleneck && State.RealmIndex == MaxQiRealm;
}

FCultivationVictoryResult FCultivationSystem::GrantCultivationFromVictory(
	FCultivationState& State, float ThreatScore)
{
	FString NormalizationWarning;
	Normalize(State, NormalizationWarning);
	FCultivationVictoryResult Result;
	Result.ThreatScore = FMath::Max(0.f, FMath::IsFinite(ThreatScore) ? ThreatScore : 0.f);
	Result.CultivationBefore = State.CultivationPoints;
	Result.CultivationAfter = State.CultivationPoints;
	Result.RealmBefore = State.RealmIndex;
	Result.RealmAfter = State.RealmIndex;
	if (IsFoundation(State))
	{
		Result.Summary = TEXT("道基已成，战斗胜利不再增加炼气修为。");
		return Result;
	}
	if (Result.ThreatScore <= 0.f)
	{
		Result.Summary = TEXT("本场没有可确认的权威威胁值，修为未结算。");
		return Result;
	}
	if (IsAtBottleneck(State))
	{
		Result.bReachedBottleneck = true;
		Result.Summary = TEXT("炼气九层已至瓶颈，须先完成首领门槛。");
		return Result;
	}

	// Threat contributes at a deliberately bounded rate.  A strong card still
	// matters more than a single victory's cultivation payout, while a long run
	// can reach layer nine through meaningful encounters rather than menu spam.
	const int32 Gain = FMath::Clamp(FMath::RoundToInt(20.f + Result.ThreatScore * 12.f), 20, 180);
	const int32 BeforePending = State.PendingChoiceCount;
	State.LastVictoryThreatScore = Result.ThreatScore;
	State.TotalCultivationFromVictories = FMath::Max(0,
		State.TotalCultivationFromVictories + Gain);
	State.CultivationPoints = FMath::Max(0, State.CultivationPoints + Gain);
	while (State.RealmIndex < MaxQiRealm
		&& State.CultivationPoints >= GetRealmThreshold(State.RealmIndex + 1))
	{
		++State.RealmIndex;
		++State.PendingChoiceCount;
	}
	RefreshDerivedFields(State);
	Result.bApplied = true;
	Result.CultivationGained = Gain;
	Result.CultivationAfter = State.CultivationPoints;
	Result.RealmAfter = State.RealmIndex;
	Result.NewChoicesQueued = State.PendingChoiceCount - BeforePending;
	Result.bReachedBottleneck = IsAtBottleneck(State);
	Result.Summary = FString::Printf(TEXT("胜利结算：修为 +%d，当前%s%s。"), Gain,
		*State.RealmName,
		Result.bReachedBottleneck ? TEXT("，已至瓶颈") : TEXT(""));
	return Result;
}

FCultivationChoiceResult FCultivationSystem::ApplyChoice(FCultivationState& State,
	ECultivationChoice Choice)
{
	FString NormalizationWarning;
	Normalize(State, NormalizationWarning);
	FCultivationChoiceResult Result;
	Result.Choice = Choice;
	Result.RealmBefore = State.RealmIndex;
	Result.RealmAfter = State.RealmIndex;
	if (IsFoundation(State))
	{
		Result.Summary = TEXT("道基已成，暂不需要重复修炼。");
		return Result;
	}
	if (State.PendingChoiceCount <= 0)
	{
		Result.bReachedBottleneck = IsAtBottleneck(State);
		Result.Summary = IsAtBottleneck(State)
			? TEXT("炼气九层已至瓶颈，须待击败首领后筑基。")
			: TEXT("当前没有待领取的修炼升级奖励；修为由战斗胜利结算。");
		return Result;
	}

	// The side benefits are intentionally capped and orthogonal to card damage.
	// They make a choice felt without replacing a good card, a deck decision, or
	// an encounter reward.
	switch (Choice)
	{
	case ECultivationChoice::BodyTempering:
		Result.CultivationGained = 0;
		Result.MaxHPGained = FMath::Min(2, FMath::Max(0, 18 - State.MaxHPBonus));
		--State.PendingChoiceCount;
		State.BodyTemperingCount++;
		State.MaxHPBonus += Result.MaxHPGained;
		break;
	case ECultivationChoice::QiAbsorption:
		Result.CultivationGained = 0;
		Result.QiReserveGained = State.QiReserve < 3 ? 1 : 0;
		--State.PendingChoiceCount;
		State.QiAbsorptionCount++;
		State.QiReserve += Result.QiReserveGained;
		break;
	case ECultivationChoice::Insight:
		Result.CultivationGained = 0;
		Result.InsightChargeGained = State.InsightCharges < 3 ? 1 : 0;
		--State.PendingChoiceCount;
		State.InsightCount++;
		State.InsightCharges += Result.InsightChargeGained;
		break;
	default:
		Result.Summary = TEXT("未知的修炼方式，未发生变化。");
		return Result;
	}

	RefreshDerivedFields(State);
	Result.bApplied = true;
	Result.RealmAfter = State.RealmIndex;
	Result.bReachedBottleneck = IsAtBottleneck(State);
	Result.Summary = FString::Printf(TEXT("%s：领取本次境界升级奖励，当前%s（待选 %d）。"),
		*ChoiceName(Choice), *State.RealmName, State.PendingChoiceCount);
	return Result;
}

bool FCultivationSystem::MarkBossDefeated(FCultivationState& State, FString& OutMessage)
{
	Normalize(State, OutMessage);
	State.bBossDefeated = true;
	State.bBossBreakthroughReady = IsAtBottleneck(State);
	if (State.bBossBreakthroughReady)
		return TryBreakthroughAfterBoss(State, OutMessage);

	OutMessage = FString::Printf(TEXT("首领已败，但当前为%s，尚未满足炼气九层瓶颈。"), *State.RealmName);
	return false;
}

bool FCultivationSystem::TryBreakthroughAfterBoss(FCultivationState& State, FString& OutError)
{
	Normalize(State, OutError);
	if (IsFoundation(State))
	{
		OutError.Reset();
		return true;
	}
	if (!State.bBossDefeated)
	{
		OutError = TEXT("尚未击败首领，不能筑基。");
		return false;
	}
	if (!IsAtBottleneck(State))
	{
		OutError = FString::Printf(TEXT("修为不足：当前%s，必须先到达炼气九层瓶颈。"), *State.RealmName);
		return false;
	}
	State.bBossBreakthroughReady = true;
	State.bFoundationEstablished = true;
	State.RealmIndex = FoundationRealm;
	State.RealmName = GetRealmName(FoundationRealm);
	State.NextThreshold = 0;
	State.bAtBottleneck = false;
	OutError = TEXT("天雷淬体，道基初成。");
	return true;
}

bool FCultivationSystem::Normalize(FCultivationState& State, FString& OutWarning)
{
	OutWarning.Reset();
	const FCultivationState Before = State;
	State.CultivationPoints = FMath::Max(0, State.CultivationPoints);
	State.RealmIndex = FMath::Clamp(State.RealmIndex, MinQiRealm, FoundationRealm);
	if (State.bFoundationEstablished || State.RealmIndex >= FoundationRealm)
	{
		State.bFoundationEstablished = true;
		State.RealmIndex = FoundationRealm;
		State.RealmName = GetRealmName(FoundationRealm);
		State.NextThreshold = 0;
		State.bAtBottleneck = false;
	}
	else
	{
		// Do not fabricate a layer from an inconsistent old save.  Only progress
		// that is already represented by points can be recovered.
		while (State.RealmIndex < MaxQiRealm
			&& State.CultivationPoints >= GetRealmThreshold(State.RealmIndex + 1)) ++State.RealmIndex;
		RefreshDerivedFields(State);
	}
	State.MaxHPBonus = FMath::Clamp(State.MaxHPBonus, 0, 18);
	State.QiReserve = FMath::Clamp(State.QiReserve, 0, 3);
	State.InsightCharges = FMath::Clamp(State.InsightCharges, 0, 3);
	if (Before.RealmIndex != State.RealmIndex || Before.RealmName != State.RealmName
		|| Before.NextThreshold != State.NextThreshold || Before.bAtBottleneck != State.bAtBottleneck)
	{
		OutWarning = FString::Printf(TEXT("修炼状态已规范化为%s。"), *State.RealmName);
	}
	FString ValidationError;
	if (!Validate(State, ValidationError))
	{
		OutWarning = ValidationError;
		return false;
	}
	return true;
}

bool FCultivationSystem::Validate(const FCultivationState& State, FString& OutError)
{
	OutError.Reset();
	if (State.RealmIndex < MinQiRealm || State.RealmIndex > FoundationRealm)
	{
		OutError = TEXT("修炼境界超出 1..10 范围");
		return false;
	}
	if (State.CultivationPoints < 0)
	{
		OutError = TEXT("修为不能为负数");
		return false;
	}
	if (State.bFoundationEstablished != (State.RealmIndex == FoundationRealm))
	{
		OutError = TEXT("筑基标志与境界不一致");
		return false;
	}
	if (State.bAtBottleneck && (State.RealmIndex != MaxQiRealm
		|| State.CultivationPoints < GetRealmThreshold(MaxQiRealm)
		|| State.bFoundationEstablished))
	{
		OutError = TEXT("瓶颈标志与炼气九层阈值不一致");
		return false;
	}
	if (State.MaxHPBonus < 0 || State.MaxHPBonus > 18
		|| State.QiReserve < 0 || State.QiReserve > 3
		|| State.InsightCharges < 0 || State.InsightCharges > 3)
	{
		OutError = TEXT("修炼附带收益超出安全上限");
		return false;
	}
	return true;
}

FString FCultivationSystem::ChoiceName(ECultivationChoice Choice)
{
	switch (Choice)
	{
	case ECultivationChoice::BodyTempering: return TEXT("锻体");
	case ECultivationChoice::QiAbsorption: return TEXT("纳气");
	case ECultivationChoice::Insight: return TEXT("悟法");
	default: return TEXT("未知修炼");
	}
}
