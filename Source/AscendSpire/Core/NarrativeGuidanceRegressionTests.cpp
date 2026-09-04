#include "NarrativeGuidanceRegressionTests.h"

#include "InfiniteNarrativeService.h"
#include "NarrativePromptManager.h"

namespace
{
	constexpr TCHAR DirectionToken[] = TEXT("SYNTHETIC_DIRECTION_KEEP");
	constexpr TCHAR DisabledDirectionToken[] = TEXT("SYNTHETIC_DIRECTION_DISABLED");
	constexpr TCHAR EngineFactToken[] = TEXT("SYNTHETIC_ENGINE_FACT");
	constexpr TCHAR OutputContractToken[] = TEXT("SYNTHETIC_OUTPUT_CONTRACT");
	constexpr TCHAR PresetConflictToken[] = TEXT("SYNTHETIC_PRESET_CONFLICT");
	constexpr TCHAR WorldBookConflictToken[] = TEXT("SYNTHETIC_WORLDBOOK_CONFLICT");
	constexpr TCHAR CardConflictToken[] = TEXT("SYNTHETIC_CARD_CONFLICT");

	FNarrativeGenerationPreset MakeSyntheticPreset()
	{
		FNarrativeGenerationPreset Preset;
		Preset.Name = TEXT("synthetic-story-direction");

		FNarrativePromptEntry Main;
		Main.Identifier = TEXT("main");
		Main.Role = TEXT("system");
		Main.Content = FString::Printf(TEXT("普通预设：%s"), PresetConflictToken);

		FNarrativePromptEntry WorldInfo;
		WorldInfo.Identifier = TEXT("worldInfoBefore");
		WorldInfo.Role = TEXT("system");
		WorldInfo.bMarker = true;

		FNarrativePromptEntry DirectionMarker;
		DirectionMarker.Identifier = TEXT("storyDirection");
		DirectionMarker.Role = TEXT("system");
		DirectionMarker.bMarker = true;

		FNarrativePromptEntry ChatHistory;
		ChatHistory.Identifier = TEXT("chatHistory");
		ChatHistory.Role = TEXT("system");
		ChatHistory.bMarker = true;

		FNarrativePromptEntry OutputContract;
		OutputContract.Identifier = TEXT("outputContract");
		OutputContract.Role = TEXT("system");
		OutputContract.bMarker = true;

		Preset.Prompts = {Main, WorldInfo, DirectionMarker, ChatHistory, OutputContract};
		Preset.PromptOrder = {TEXT("main"), TEXT("worldInfoBefore"), TEXT("storyDirection"),
			TEXT("chatHistory"), TEXT("outputContract")};
		return Preset;
	}

	FNarrativePromptBuildContext MakeSyntheticContext(const FString& GenerationType,
		bool bDirectionEnabled, const FString& Direction)
	{
		FNarrativePromptBuildContext Context;
		Context.GenerationType = GenerationType;
		Context.StoryDirection = Direction;
		Context.bStoryDirectionEnabled = bDirectionEnabled;
		Context.EngineFacts = EngineFactToken;
		Context.OutputContract = OutputContractToken;
		Context.bAllowExternalNarrativeContent = true;
		Context.CharacterCardPrompt = FString::Printf(TEXT("角色卡普通指引：%s"), CardConflictToken);
		Context.WorldBookJson = FString::Printf(TEXT(
			"{\"entries\":[{\"comment\":\"synthetic\",\"content\":\"%s\","
			"\"constant\":true,\"enabled\":true,\"position\":\"before_char\"}]}"), WorldBookConflictToken);
		return Context;
	}

	int32 FindMessageIndex(const TArray<FNarrativePromptMessage>& Messages, const FString& Needle)
	{
		for (int32 Index = 0; Index < Messages.Num(); ++Index)
			if (Messages[Index].Content.Contains(Needle)) return Index;
		return INDEX_NONE;
	}

	int32 FindLastMessageIndex(const TArray<FNarrativePromptMessage>& Messages, const FString& Needle)
	{
		for (int32 Index = Messages.Num() - 1; Index >= 0; --Index)
			if (Messages[Index].Content.Contains(Needle)) return Index;
		return INDEX_NONE;
	}

	int32 CountMessagesContaining(const TArray<FNarrativePromptMessage>& Messages, const FString& Needle)
	{
		int32 Count = 0;
		for (const FNarrativePromptMessage& Message : Messages)
			if (Message.Content.Contains(Needle)) ++Count;
		return Count;
	}

	bool HasMessage(const TArray<FNarrativePromptMessage>& Messages, const FString& Needle)
	{
		return FindMessageIndex(Messages, Needle) != INDEX_NONE;
	}

	TArray<FNarrativePromptMessage> BuildSyntheticMessages(const FString& GenerationType,
		bool bDirectionEnabled, const FString& Direction, int32 TokenBudget = 131072,
		bool bWriterFormatRetry = false)
	{
		FNarrativePromptBuildContext Context = MakeSyntheticContext(
			GenerationType, bDirectionEnabled, Direction);
		Context.TokenBudget = TokenBudget;
		Context.bWriterFormatRetry = bWriterFormatRetry;
		Context.ChatHistory.Add({TEXT("assistant"), TEXT("SYNTHETIC_HISTORY_OLD_1")});
		Context.ChatHistory.Add({TEXT("user"), TEXT("SYNTHETIC_HISTORY_OLD_2")});
		Context.ChatHistory.Add({TEXT("assistant"), TEXT("SYNTHETIC_HISTORY_OLD_3")});
		FString Diagnostic;
		return FNarrativePromptManager::BuildMessages(MakeSyntheticPreset(), Context, Diagnostic);
	}
}

int32 RunNarrativeGuidanceRegressionTests(FNarrativeGuidanceRegressionCheck Check)
{
	int32 Emitted = 0;
	auto Emit = [&Check, &Emitted](bool bPass, const TCHAR* Label)
	{
		++Emitted;
		Check(bPass, FString(Label));
	};

	const TArray<FNarrativePromptMessage> Enabled = BuildSyntheticMessages(
		TEXT("normal"), true, DirectionToken);
	const int32 DirectionIndex = FindLastMessageIndex(Enabled, DirectionToken);
	const int32 PresetIndex = FindMessageIndex(Enabled, PresetConflictToken);
	const int32 WorldBookIndex = FindMessageIndex(Enabled, WorldBookConflictToken);
	const int32 CardIndex = FindMessageIndex(Enabled, CardConflictToken);
	const int32 EngineIndex = FindMessageIndex(Enabled, EngineFactToken);
	const int32 OutputIndex = FindMessageIndex(Enabled, OutputContractToken);
	Emit(DirectionIndex != INDEX_NONE, TEXT("enabled direction is assembled"));
	Emit(CountMessagesContaining(Enabled, DirectionToken) == 1,
		TEXT("direction body is emitted once in the final authoritative block"));
	Emit(PresetIndex != INDEX_NONE && WorldBookIndex != INDEX_NONE && CardIndex != INDEX_NONE
		&& DirectionIndex > PresetIndex && DirectionIndex > WorldBookIndex && DirectionIndex > CardIndex,
		TEXT("direction tail outranks preset, worldbook and character-card guidance"));
	Emit(Enabled.IsValidIndex(Enabled.Num() - 1)
		&& Enabled.Last().bProtected
		&& Enabled.Last().Content.Contains(DirectionToken)
		&& Enabled.Last().Content.Contains(TEXT("不得改写或覆盖")),
		TEXT("final priority block is protected and bounds direction without copying engine payloads"));
	Emit(EngineIndex != INDEX_NONE && OutputIndex != INDEX_NONE
		&& Enabled[EngineIndex].bProtected && Enabled[OutputIndex].bProtected,
		TEXT("engine facts and output contract are protected from trimming"));

	const TArray<FNarrativePromptMessage> Disabled = BuildSyntheticMessages(
		TEXT("normal"), false, DisabledDirectionToken);
	Emit(!HasMessage(Disabled, DisabledDirectionToken), TEXT("disabled direction is absent, including marker resolution"));

	const TArray<FNarrativePromptMessage> Blank = BuildSyntheticMessages(
		TEXT("normal"), true, TEXT(" \t\r\n "));
	Emit(!HasMessage(Blank, TEXT("剧情大纲与走向")), TEXT("blank direction does not synthesize a user direction block"));

	const TArray<FNarrativePromptMessage> Prefetch = BuildSyntheticMessages(
		TEXT("combat_prefetch"), true, DirectionToken);
	Emit(HasMessage(Prefetch, DirectionToken), TEXT("combat prefetch receives the active direction"));

	const TArray<FNarrativePromptMessage> Trimmed = BuildSyntheticMessages(
		TEXT("normal"), true, DirectionToken, 64);
	Emit(HasMessage(Trimmed, DirectionToken)
		&& HasMessage(Trimmed, EngineFactToken)
		&& HasMessage(Trimmed, OutputContractToken),
		TEXT("history trimming retains protected direction, facts and output contract"));
	Emit(!HasMessage(Trimmed, TEXT("SYNTHETIC_HISTORY_OLD_1"))
		&& !HasMessage(Trimmed, TEXT("SYNTHETIC_HISTORY_OLD_2"))
		&& !HasMessage(Trimmed, TEXT("SYNTHETIC_HISTORY_OLD_3")),
		TEXT("small context budget removes old history before protected guidance"));

	FInfiniteNarrativeSettings BaseSettings;
	BaseSettings.bStoryDirectionEnabled = true;
	BaseSettings.StoryDirection = TEXT("  route A\r\nroute B\r\n");
	FInfiniteNarrativeSettings NormalizedSettings = BaseSettings;
	NormalizedSettings.StoryDirection = TEXT("route A\nroute B");
	FInfiniteNarrativeSettings ChangedSettings = BaseSettings;
	ChangedSettings.StoryDirection = TEXT("route B");
	FInfiniteNarrativeSettings DisabledSettings = BaseSettings;
	DisabledSettings.bStoryDirectionEnabled = false;
	FInfiniteNarrativeSettings DisabledTextChangedSettings = DisabledSettings;
	DisabledTextChangedSettings.StoryDirection = TEXT("route B");
	FInfiniteNarrativeSettings DisabledTextClearedSettings = DisabledSettings;
	DisabledTextClearedSettings.StoryDirection.Reset();
	const FString BaseKey = UInfiniteNarrativeService::BuildStoryDirectionCacheKey(BaseSettings);
	const FString NormalizedKey = UInfiniteNarrativeService::BuildStoryDirectionCacheKey(NormalizedSettings);
	const FString ChangedKey = UInfiniteNarrativeService::BuildStoryDirectionCacheKey(ChangedSettings);
	const FString DisabledKey = UInfiniteNarrativeService::BuildStoryDirectionCacheKey(DisabledSettings);
	const FString DisabledTextChangedKey = UInfiniteNarrativeService::BuildStoryDirectionCacheKey(DisabledTextChangedSettings);
	const FString DisabledTextClearedKey = UInfiniteNarrativeService::BuildStoryDirectionCacheKey(DisabledTextClearedSettings);
	Emit(BaseKey == NormalizedKey, TEXT("cache key normalizes surrounding whitespace and line endings"));
	Emit(BaseKey != ChangedKey, TEXT("cache key changes when direction text changes"));
	Emit(BaseKey != DisabledKey, TEXT("cache key changes when direction enable state changes"));
	Emit(DisabledKey == DisabledTextChangedKey && DisabledKey == DisabledTextClearedKey,
		TEXT("editing or clearing stored direction while disabled does not invalidate an effective cache"));
	Emit(ChangedKey != BaseKey, TEXT("new direction key rejects an old unconsumed cache key"));

	// The writer retry suffix is private request plumbing. Its actual HTTP path is
	// intentionally not invoked here. The builder reserves the direction block so
	// the production service can append exactly one current-direction guard after
	// the repair user message.
	const TArray<FNarrativePromptMessage> RetryBase = BuildSyntheticMessages(
		TEXT("normal"), true, DirectionToken, 131072, true);
	Emit(!HasMessage(RetryBase, DirectionToken),
		TEXT("format-retry base reserves direction for the post-repair protected suffix; HTTP remains static-only"));

	return Emitted;
}
