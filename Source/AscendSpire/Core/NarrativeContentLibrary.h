#pragma once

#include "CoreMinimal.h"

/**
 * A world book imported from the SillyTavern ecosystem.
 *
 * The raw source is intentionally kept alongside the normalized metadata.  The
 * narrative runtime never sends this struct to an LLM to "clean it up"; parsing
 * and placeholder substitution are deterministic local operations.
 */
struct FNarrativeWorldBookAsset
{
	FString Id;
	FString Name;
	FString SourcePath;
	FString RawJson;
	FString NormalizedJson;
	int32 EntryCount = 0;
	int32 ConstantEntryCount = 0;
	bool bValid = false;
	bool bHasWarnings = false;
	FString Diagnostic;
};
/** A parsed SillyTavern character card (V1, V2 or V3). */
struct FNarrativeCharacterCardAsset
{
	FString Id;
	FString Name;
	FString SourceFormat;
	FString SourcePath;
	FString RawJson;
	FString NormalizedJson;
	FString Description;
	FString Personality;
	FString Scenario;
	FString FirstMessage;
	TArray<FString> AlternateGreetings;
	FString MessageExamples;
	FString SystemPrompt;
	FString PostHistoryInstructions;
	FString Avatar;
	FString CharacterBookJson;
	/** Registry-shaped JSON used by the portrait UI, never a source-of-truth replacement. */
	FString RegistryJson;
	bool bHasEmbeddedAvatar = false;
	bool bHasEmbeddedWorldBook = false;
	bool bValid = false;
	bool bHasWarnings = false;
	FString Diagnostic;
};

/** Small status summary used by settings and prompt-preview surfaces. */
struct FNarrativeAssetStatus
{
	FString Id;
	FString Name;
	int32 EntryCount = 0;
	bool bEnabled = false;
	bool bValid = false;
	bool bHasWarnings = false;
	FString Diagnostic;
};

/**
 * Deterministic local importer and persistence layer for narrative assets.
 *
 * It deliberately has no UObject/Agent dependency.  The only inputs that can
 * affect imported data are the selected local files and the explicit user name
 * used for {{user}} expansion.  Imported files are copied to Saved/Narrative so
 * future sessions do not depend on a removable source directory.
 */
class ASCENDSPIRE_API FNarrativeContentLibrary
{
public:
	static FString GetNarrativeRootDirectory();
	static FString GetWorldBooksDirectory();
	static FString GetCharacterCardsDirectory();

	/** Import a JSON world book and keep the source bytes verbatim. */
	static bool ImportWorldBook(const FString& SourcePath, FNarrativeWorldBookAsset& OutAsset,
		FString& OutError);
	static bool ImportWorldBookText(const FString& Json, const FString& SourcePath,
		FNarrativeWorldBookAsset& OutAsset, FString& OutError);
	static bool LoadWorldBook(const FString& Id, FNarrativeWorldBookAsset& OutAsset,
		FString& OutError);
	static void ListWorldBooks(TArray<FNarrativeWorldBookAsset>& OutAssets);

	/** Import a V1/V2/V3 JSON card or a PNG containing chara/ccv3 metadata. */
	static bool ImportCharacterCard(const FString& SourcePath, FNarrativeCharacterCardAsset& OutAsset,
		FString& OutError, const FString& UserName = TEXT("玩家"));
	static bool ImportCharacterCardJsonText(const FString& Json, const FString& SourcePath,
		FNarrativeCharacterCardAsset& OutAsset, FString& OutError,
		const FString& UserName = TEXT("玩家"));
	static bool ImportCharacterCardPng(const FString& SourcePath, FNarrativeCharacterCardAsset& OutAsset,
		FString& OutError, const FString& UserName = TEXT("玩家"));
	static bool LoadCharacterCard(const FString& Id, FNarrativeCharacterCardAsset& OutAsset,
		FString& OutError, const FString& UserName = TEXT("玩家"));
	static void ListCharacterCards(TArray<FNarrativeCharacterCardAsset>& OutAssets,
		const FString& UserName = TEXT("玩家"));

	/** Parse without persistence; useful to import previews and automation tests. */
	static bool ParseCharacterCardJson(const FString& Json, FNarrativeCharacterCardAsset& OutAsset,
		FString& OutError, const FString& SourcePath = TEXT("character-card.json"),
		const FString& UserName = TEXT("玩家"));
	static bool ExtractCharacterCardJsonFromPng(const TArray<uint8>& PngBytes,
		FString& OutJson, FString& OutError);

	/** SillyTavern-compatible substitutions; never invokes a model. */
	static FString ReplaceMacros(const FString& Source, const FString& CharacterName,
		const FString& UserName = TEXT("玩家"));

	/** Build a safe portrait registry entry from an imported card. */
	static FString BuildRegistryJson(const FNarrativeCharacterCardAsset& Asset);

	/** Stable IDs are content-addressed and cannot escape the Saved directory. */
	static FString MakeWorldBookId(const FString& RawJson, const FString& SourcePath);
	static FString MakeCharacterCardId(const FString& RawJson, const FString& SourcePath);

private:
	static bool ParseWorldBookJson(const FString& Json, FNarrativeWorldBookAsset& OutAsset,
		FString& OutError, const FString& SourcePath);
	static bool ParseCharacterCardObject(const TSharedPtr<class FJsonObject>& Root,
		const FString& RawJson, const FString& SourcePath, FNarrativeCharacterCardAsset& OutAsset,
		FString& OutError, const FString& UserName);
	static bool SaveWorldBookAsset(const FNarrativeWorldBookAsset& Asset, FString& OutError);
	static bool SaveCharacterCardAsset(const FNarrativeCharacterCardAsset& Asset,
		const TArray<uint8>* OptionalSourceBytes, FString& OutError);
	static bool ReadStoredCharacterCard(const FString& Path, FNarrativeCharacterCardAsset& OutAsset,
		FString& OutError, const FString& UserName);
	static FString SanitizeId(const FString& Candidate, const FString& Prefix);
	static FString JsonObjectToString(const TSharedPtr<class FJsonObject>& Object);
};
