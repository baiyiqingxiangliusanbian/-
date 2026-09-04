#pragma once

#include "CoreMinimal.h"

/** A single Chat Completions message after prompt assembly. */
struct FNarrativePromptMessage
{
	FString Role = TEXT("system");
	FString Content;
	/** Provider-specific hidden continuation channel. It is request-local and never history. */
	FString ReasoningContent;
	/** Marks an injected continuation so diagnostics and future adapters can omit it from saves. */
	bool bTemporary = false;
	/** Engine facts and the output contract are never candidates for context trimming. */
	bool bProtected = false;
};

/** SillyTavern-compatible prompt entry subset used by AscendSpire. */
struct FNarrativePromptEntry
{
	FString Identifier;
	FString Name;
	FString Role = TEXT("system");
	FString Content;
	bool bEnabled = true;
	bool bMarker = false;
	bool bInChat = false;
	int32 Depth = 4;
	int32 Order = 100;
	TArray<FString> Triggers;
};

/** Generation controls imported from an ST OpenAI preset or the built-in preset. */
struct FNarrativeGenerationPreset
{
	FString Name;
	TArray<FNarrativePromptEntry> Prompts;
	TArray<FString> PromptOrder;
	float Temperature = 0.55f;
	float TopP = 1.f;
	float TopK = 0.f;
	float TopA = 0.f;
	float MinP = 0.f;
	float FrequencyPenalty = 0.f;
	float PresencePenalty = 0.f;
	float RepetitionPenalty = 1.f;
	int32 Seed = -1;
	int32 MaxContextTokens = 131072;
	int32 MaxOutputTokens = 65535;
	bool bStream = false;
	bool bContinuePrefill = false;
	FString AssistantPrefill;
	FString ContinueNudge;
	FString ContinuePostfix = TEXT(" ");
	FString ReasoningEffort = TEXT("auto");
	bool bShowThoughts = false;
	TArray<FString> StopStrings;
};

/** Runtime material inserted into a configurable prompt stack. */
struct FNarrativePromptBuildContext
{
	FString GenerationType = TEXT("normal");
	TMap<FString, FString> Macros;
	TArray<FNarrativePromptMessage> ChatHistory;
	FString WorldBookJson;
	/** Optional SillyTavern character_book merged only for this request. */
	FString EmbeddedWorldBookJson;
	FString CharacterRegistryJson;
	/** Imported character-card fields are kept separate from the portrait registry. */
	FString CharacterCardPrompt;
	/** User-authored direction. This is a permanent system block and survives trimming. */
	FString StoryDirection;
	bool bStoryDirectionEnabled = true;
	/** Writer format repair appends the one authoritative direction block after its user suffix. */
	bool bWriterFormatRetry = false;
	/** Explicit UI consent gate for imported card/worldbook/reasoning content. */
	bool bAllowExternalNarrativeContent = false;
	/** Local engine facts are higher priority than story direction and external assets. */
	FString EngineFacts;
	/** A provider-returned reasoning continuation may be pre-injected for this request only. */
	FString TemporaryReasoningContent;
	bool bAllowTemporaryReasoningPreInjection = false;
	/** User-authored reasoning prefill for the writer only; it is never added to history. */
	FString ReasoningPrefill;
	bool bReasoningPrefillEnabled = false;
	/** Prepended to each copied user history message; never persisted to the real history. */
	FString UserHistoryMarker = TEXT("[继续遵循既定角色,关系与写作要求,直接回应本条消息]");
	bool bUserHistoryMarkerEnabled = true;
	FString AuthorNote;
	FString GameState;
	FString CombatContext;
	FString OutputContract;
	int32 TokenBudget = 131072;
	/** 0 scans every retained chat message. */
	int32 WorldInfoScanDepth = 20;
};

/**
 * Data-driven prompt assembler modelled after SillyTavern's Prompt Manager.
 * It intentionally reimplements observable behavior instead of copying AGPL code.
 */
class FNarrativePromptManager
{
public:
	/** Loads built-in or user/ST preset JSON. Path takes precedence over inline JSON. */
	static bool LoadPreset(const FString& DefaultRelativePath, const FString& UserPath,
		const FString& InlineJson, FNarrativeGenerationPreset& OutPreset, FString& OutError);

	/** Builds role messages, markers, World Info and in-chat depth injections. */
	static TArray<FNarrativePromptMessage> BuildMessages(const FNarrativeGenerationPreset& Preset,
		const FNarrativePromptBuildContext& Context, FString& OutDiagnostic);

	static FString ExpandMacros(const FString& Source, const TMap<FString, FString>& Macros);
	static int32 ApproxTokens(const FString& Text);

private:
	struct FWorldInfoEntry
	{
		FString Name;
		FString Content;
		TArray<FString> Keys;
		TArray<FString> SecondaryKeys;
		FString SelectiveLogic = TEXT("and_any");
		FString Position = TEXT("after_char");
		FString Role = TEXT("system");
		bool bEnabled = true;
		bool bConstant = false;
		bool bUseRegex = false;
		bool bCaseSensitive = false;
		bool bRecursive = true;
		bool bPreventRecursion = false;
		int32 Depth = 4;
		int32 Order = 100;
		int32 Probability = 100;
	};

	static bool ParsePreset(const FString& Json, FNarrativeGenerationPreset& OutPreset, FString& OutError);
	static void ParseWorldInfo(const FString& Json, TArray<FWorldInfoEntry>& OutEntries);
	static bool IsWorldInfoActive(const FWorldInfoEntry& Entry, const FString& ScanText);
	static FString JoinWorldInfoAtPosition(const TArray<FWorldInfoEntry>& Entries, const FString& Position);
	static FString ResolveMarker(const FString& Identifier, const FNarrativePromptBuildContext& Context,
		const TArray<FWorldInfoEntry>& WorldInfo);
	static FString BuildWriterFormattingGuidance();
	static void InsertInChat(TArray<FNarrativePromptMessage>& History,
		const FNarrativePromptMessage& Message, int32 Depth);
};
