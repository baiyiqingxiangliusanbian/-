#include "OpeningRelicRandomRegressionTests.h"

#include "RunManager.h"

namespace
{
	constexpr int32 OpeningRelicSampleCount = 128;
	constexpr int32 OpeningRelicSeedBase = 0x5A170001;
	constexpr int32 OpeningRelicSeedStride = 7919;

	URunManager* MakeIsolatedOpeningRun(int32 Seed)
	{
		URunManager* Run = NewObject<URunManager>();
		if (!Run) return nullptr;
		Run->SetPersistentAuthoredContentEnabledForAutomationTest(false);
		return Run->StartNewRun(Seed, true) ? Run : nullptr;
	}

	TArray<FString> GetCommonFixedRelicIds(const URunManager* Run)
	{
		TArray<FString> Result;
		if (!Run) return Result;
		for (const FString& RelicId : Run->GetAvailableFixedNarrativeRelicIds())
		{
			const FRelicData* Relic = Run->GetRelicData(RelicId);
			if (Relic && Relic->Rarity == TEXT("common")) Result.Add(RelicId);
		}
		Result.Sort();
		return Result;
	}

	bool AreThreeValidUnownedRelics(const URunManager* Run,
		const TArray<FString>& RelicIds, bool bRequireCommon)
	{
		if (!Run || RelicIds.Num() != 3) return false;
		TSet<FString> Seen;
		for (const FString& RelicId : RelicIds)
		{
			const FRelicData* Relic = Run->GetRelicData(RelicId);
			if (RelicId.IsEmpty() || Seen.Contains(RelicId)
				|| Run->State.RelicIds.Contains(RelicId) || !Relic || Relic->Id != RelicId
				|| (bRequireCommon && Relic->Rarity != TEXT("common")))
			{
				return false;
			}
			Seen.Add(RelicId);
		}
		return Seen.Num() == 3;
	}

	FString MakeCombinationKey(const TArray<FString>& RelicIds)
	{
		TArray<FString> SortedIds = RelicIds;
		SortedIds.Sort();
		return FString::Join(SortedIds, TEXT("|"));
	}
}

int32 RunOpeningRelicRandomRegressionTests(FOpeningRelicRandomRegressionCheck Check)
{
	int32 Emitted = 0;
	auto Emit = [&Check, &Emitted](bool bPass, const FString& Label)
	{
		++Emitted;
		Check(bPass, Label);
	};

	URunManager* PoolProbe = MakeIsolatedOpeningRun(OpeningRelicSeedBase);
	const TArray<FString> CommonRelicIds = GetCommonFixedRelicIds(PoolProbe);
	const bool bCommonPoolReady = PoolProbe && CommonRelicIds.Num() >= 3;

	TSet<FString> SeenRelicIds;
	TSet<FString> CombinationKeys;
	bool bAllSamplesValid = bCommonPoolReady;
	for (int32 SampleIndex = 0; SampleIndex < OpeningRelicSampleCount; ++SampleIndex)
	{
		const int32 Seed = OpeningRelicSeedBase + SampleIndex * OpeningRelicSeedStride;
		URunManager* Run = MakeIsolatedOpeningRun(Seed);
		const TArray<FString> Choices = Run ? Run->RollInitialRelicChoices(3) : TArray<FString>();
		bAllSamplesValid = bAllSamplesValid
			&& AreThreeValidUnownedRelics(Run, Choices, true);
		if (Run && Choices.Num() == 3)
		{
			CombinationKeys.Add(MakeCombinationKey(Choices));
			for (const FString& RelicId : Choices) SeenRelicIds.Add(RelicId);
		}
	}
	Emit(bAllSamplesValid,
		FString::Printf(TEXT("initial relic rolls: %d deterministic seeds each return three distinct valid unowned common relics"),
			OpeningRelicSampleCount));

	const int32 StableSeed = OpeningRelicSeedBase + 63 * OpeningRelicSeedStride;
	URunManager* StableRunA = MakeIsolatedOpeningRun(StableSeed);
	URunManager* StableRunB = MakeIsolatedOpeningRun(StableSeed);
	const TArray<FString> StableChoicesA = StableRunA ? StableRunA->RollInitialRelicChoices(3) : TArray<FString>();
	const TArray<FString> StableChoicesB = StableRunB ? StableRunB->RollInitialRelicChoices(3) : TArray<FString>();
	Emit(StableRunA && StableRunB && StableChoicesA == StableChoicesB
		&& AreThreeValidUnownedRelics(StableRunA, StableChoicesA, true)
		&& AreThreeValidUnownedRelics(StableRunB, StableChoicesB, true),
		TEXT("rebuilding an isolated run with the same seed preserves initial relic ID order"));

	Emit(CombinationKeys.Num() > 1,
		FString::Printf(TEXT("initial relic randomization varies unordered combinations across %d seeds (%d combinations)"),
			OpeningRelicSampleCount, CombinationKeys.Num()));

	URunManager* OwnedRun = MakeIsolatedOpeningRun(OpeningRelicSeedBase + 1000 * OpeningRelicSeedStride);
	const TArray<FString> OwnedPool = GetCommonFixedRelicIds(OwnedRun);
	const FString OwnedRelicId = OwnedPool.Num() > 0 ? OwnedPool[0] : FString();
	if (OwnedRun && !OwnedRelicId.IsEmpty()) OwnedRun->State.RelicIds.Add(OwnedRelicId);
	const TArray<FString> OwnedChoices = OwnedRun ? OwnedRun->RollInitialRelicChoices(3) : TArray<FString>();
	Emit(OwnedRun && !OwnedRelicId.IsEmpty() && AreThreeValidUnownedRelics(OwnedRun, OwnedChoices, true)
		&& !OwnedChoices.Contains(OwnedRelicId),
		TEXT("initial relic roll excludes a relic already present in the run"));

	TArray<FString> MissingCommonIds;
	for (const FString& RelicId : CommonRelicIds)
		if (!SeenRelicIds.Contains(RelicId)) MissingCommonIds.Add(RelicId);
	Emit(CommonRelicIds.Num() >= 3 && MissingCommonIds.Num() == 0,
		FString::Printf(TEXT("all data-defined common fixed relics appear across %d rolls (%d/%d covered)"),
			OpeningRelicSampleCount, SeenRelicIds.Num(), CommonRelicIds.Num()));

	URunManager* PendingRun = MakeIsolatedOpeningRun(OpeningRelicSeedBase + 2000 * OpeningRelicSeedStride);
	TArray<FString> PendingIds = PendingRun
		? PendingRun->RollInitialRelicChoices(3) : TArray<FString>();
	const TArray<FString> OriginalPendingIds = PendingIds;
	const TArray<FString> PendingChoiceTexts = {
		TEXT("沿河湾收好第一件法器。"), TEXT("在旧药圃辨认法器的灵息。"), TEXT("带着法器离开祖祠。")};
	const bool bPendingInputValid = PendingRun
		&& AreThreeValidUnownedRelics(PendingRun, OriginalPendingIds, true);
	if (bPendingInputValid)
	{
		PendingRun->SavePendingInfiniteOpening(3, TEXT("third_furnace_watch"),
			PendingIds, PendingChoiceTexts, true);
	}
	if (PendingIds.Num() > 0) PendingIds[0] = TEXT("caller_mutation_must_not_rewrite_saved_id");
	int32 RestoredOpeningIndex = INDEX_NONE;
	FString RestoredOpeningId;
	TArray<FString> RestoredIds;
	TArray<FString> RestoredChoiceTexts;
	bool bRestoredWordingReady = false;
	const bool bRestored = PendingRun
		&& PendingRun->RestorePendingInfiniteOpening(RestoredOpeningIndex, RestoredOpeningId,
			RestoredIds, RestoredChoiceTexts, bRestoredWordingReady);
	Emit(bPendingInputValid && bRestored && PendingRun->HasPendingInfiniteOpening()
		&& RestoredOpeningIndex == 3 && RestoredOpeningId == TEXT("third_furnace_watch")
		&& RestoredIds == OriginalPendingIds && RestoredChoiceTexts == PendingChoiceTexts
		&& bRestoredWordingReady,
		TEXT("in-memory pending opening restore preserves the locked relic ID order and wording"));

	return Emitted;
}
