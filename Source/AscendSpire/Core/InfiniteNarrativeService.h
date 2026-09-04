#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "GameDataTypes.h"
#include "NarrativePromptManager.h"
#include "InfiniteNarrativeService.generated.h"

struct FNarrativeCharacterCardAsset;

/** OpenAI-compatible LLM connection and presentation settings. */
struct FInfiniteNarrativeSettings
{
	FString Endpoint;
	FString ApiKey;
	FString Model;
	/** 兼容旧配置名：现在仅作为独立卡牌工坊模型；空值表示沿用剧情模型。 */
	FString MvuVerifierModel;
	bool bShowFreeformInput = false;
	bool bShowSpeakerPortrait = true;
	bool bEnableScreenShake = true;
	/** Accessibility: keep impact color cues but substantially reduce full-screen flashes. */
	bool bReduceFlashing = false;
	bool bEnableStructuredMemory = true;
	bool bEnableContinuityChecklist = true;
	/** A=true: 从战斗胜利后携带完整日志推演；B=false: 战斗开始即假定胜利预演。首战始终按 B。 */
	bool bGenerateAfterCombatWithLog = true;
	float Temperature = 1.05f;
	float TopP = 0.99f;
	float TopK = 0.f;
	float TopA = 0.f;
	float MinP = 0.f;
	float FrequencyPenalty = 0.1f;
	float PresencePenalty = 0.15f;
	float RepetitionPenalty = 1.f;
	int32 Seed = -1;
	/** auto omits provider-specific thinking fields; other values map to reasoning_effort. */
	FString ReasoningEffort = TEXT("auto");
	FString MvuReasoningEffort = TEXT("auto");
	/** Separate from visibility: explicitly request model thinking when the provider supports it. */
	bool bRequestReasoning = false;
	bool bRequestMvuReasoning = false;
	bool bShowReasoning = false;
	/** User-authored request-local reasoning prefill; never persisted in chat history. */
	bool bEnableReasoningPrefill = false;
	/** Empty in the visible settings surface; an explicitly configured private loader may supply a hidden default. */
	FString ReasoningPrefill;
	/** Internal-only copied resource location; never surfaced in the settings widget or prompt diagnostics. */
	FString HiddenReasoningPrefillPath;
	/** Internal-only dotted JSON field path for the copied resource. */
	FString HiddenReasoningPrefillFieldPath;
	/** Separate opt-in for returning provider reasoning as a request-local prefill on repair. */
	bool bEnableTemporaryReasoningPreInjection = false;
	/** Per-copied-user-message maintenance marker; disabled copies remain byte-for-byte content. */
	bool bMaintenanceMarkerEnabled = true;
	FString MaintenanceMarker = TEXT("[继续遵循既定角色,关系与写作要求,直接回应本条消息]");
	/** Independent card-forge thinking level; fast non-thinking generation is the default. */
	FString CardForgeReasoningEffort = TEXT("disabled");
	/** Stream the single narrative-director response and expose provisional scene text. */
	bool bStreamResponse = true;
	FString StopStrings;
	float TimeoutSeconds = 180.f;
	float SfxVolume = 0.65f;
	float MusicVolume = 0.35f;
	int32 InputContextTokens = 131072;
	int32 MaxOutputTokens = 65535;
	int32 MvuMaxOutputTokens = 32768;
	int32 NarrativeMinChars = 600;
	int32 NarrativeMaxChars = 1200;
	/** 0 keeps every RP turn until the configured context token capacity is reached. */
	int32 RecentRawRounds = 0;
	/** 0 disables destructive raw-history compression. */
	int32 CompressAfterRounds = 0;
	int32 UnsummarizedTokenThreshold = 18000;
	int32 MemoryTokenBudget = 12000;
	int32 WorldBookTokenBudget = 10000;
	/** 0 scans every retained history message for lorebook keys. */
	int32 WorldInfoScanDepth = 20;
	FString NarrativePromptPresetPath;
	FString MvuPromptPresetPath;
	FString NarrativePromptPresetOverride;
	FString MvuPromptPresetOverride;
	FString PersonaDescription;
	FString CharacterDescription;
	FString CharacterPersonality;
	FString Scenario;
	FString DialogueExamples;
	FString AuthorNote;
	/** Highest-priority user-authored story direction; it never overrides engine facts/output contract. */
	FString StoryDirection;
	bool bStoryDirectionEnabled = true;
	/** IDs only; imported bytes live under Saved/Narrative and are loaded by the local library. */
	FString WorldBookId;
	bool bWorldBookEnabled = true;
	FString CharacterCardId;
	bool bCharacterCardEnabled = true;
	bool bUseEmbeddedCharacterBook = true;
	/** Explicit consent gate before imported local files may leave the game process. */
	bool bAllowImportedContentToNarrativeModel = false;
	/** 完整世界书覆盖；为空时读取随游戏提供的默认世界书。 */
	FString WorldBookOverride;
	/** 完整角色/头像注册表覆盖；为空时读取默认注册表。 */
	FString CharacterRegistryOverride;
	/** 开场白的便捷覆盖项；为空时使用世界书 opening 节点。 */
	FString OpeningTitleOverride;
	FString OpeningSpeakerOverride;
	FString OpeningPortraitOverride;
	FString OpeningExpressionOverride;
	FString OpeningNarrationOverride;
	FString OpeningDialogueOverride;
	/** 兼容旧版“追加世界书”设置。 */
	FString CustomWorldBook;
	FString CustomContinuityChecklist;
};

struct FInfiniteRewardCard
{
	FString CardId;
	bool bUpgraded = false;
};

struct FInfiniteNarrativeReward
{
	TArray<FInfiniteRewardCard> Cards;
	TArray<FString> RelicIds;
	/** New, LLM-authored content that has passed the local allowlist and balance clamps. */
	TArray<FCardData> CreatedCards;
	TArray<FRelicData> CreatedRelics;
	TArray<FString> RemovedCardIds;
	TArray<FString> RemovedRelicIds;
	int32 GoldChange = 0;
	int32 HPChange = 0;
};

/** A declarative, allow-listed game function requested by the event interpreter. */
struct FInfiniteGameOperation
{
	FString Op;
	FString Name;
	FString Rarity;
	int32 Value = 0;
	int32 Count = 1;
	float PriceMultiplier = 1.f;
	bool bAllowStarter = true;
	bool bAllowCurse = false;
};

/** GM 输出的类型化剧情变量更新；只有白名单领域和操作会进入存档。 */
struct FInfiniteVariableUpdate
{
	FString Domain;
	FString Target;
	FString Field;
	FString Op;
	FString Value;
	int32 Amount = 0;
	int32 Duration = 0;
	FString Causality;
	FString Valence;
	FString Magnitude;
};

/** Local precondition for a branch operation. Failed requirements disable the choice. */
struct FInfiniteChoiceRequirement
{
	FString Type;
	int32 Value = 0;
};

/**
 * A compact concept seed emitted by the narrative director. It is not a card script;
 * only the selected branch is sent to the independent, narration-free card forge.
 */
struct FInfiniteCardForgeJob
{
	FString SourceFact;
	FString Concept;
	FString MechanicIntent;
	FString Acquisition = TEXT("gain");
};

/** Encounter specification; identity/count/scaling are engine-owned, while prose may supply origin/faction. */
struct FInfiniteEnemySpec
{
	FString TemplateId;
	/** Engine-owned encounter size. The narrative model may explain it but cannot change it. */
	int32 Count = 1;
	/** 剧情势力稳定 ID，用于匹配临时警戒度。 */
	FString FactionId;
	FString Name;
	FString Story;
	FString Tier = TEXT("normal");
	float HPScale = 1.f;
	float IntentScale = 1.f;
	TArray<FString> Abilities;
	FString AbilityDesc;
};

struct FInfiniteNarrativeChoice
{
	FString Text;
	FString ResultSummary;
	/** Hidden phase-one statement of the concrete branch consequence; never shown as prose. */
	FString ConsequenceIntent;
	/** Phase-one semantic fact bridge. It classifies the consequence without choosing an engine tool. */
	FString ResolvedImpactKind;
	FString ResolvedImpactSubject;
	FString ResolvedImpactObject;
	bool bResolvedImpactCompleted = false;
	bool bResolvedImpactPersistent = false;
	/** 本地结算说明，仅用于日志与回归测试。 */
	FString GmJudgement;
	/** 同一世界事实的稳定结算键；已提交过的键不会再次授予资源或内容。 */
	FString SettlementKey;
	/** Narrative-only state delta committed only after this branch is selected. */
	FString StatePatchJson;
	/** Planned destination: combat/card_forge/relic_reward/shop/reward/rest/upgrade/remove/continue_rp. */
	FString Next = TEXT("combat");
	/** Opening choices and explicitly immediate pre-combat acquisitions are committed before combat. */
	bool bGrantRewardBeforeCombat = false;
	FInfiniteNarrativeReward Reward;
	FInfiniteEnemySpec Enemy;
	TArray<FInfiniteChoiceRequirement> Requirements;
	TArray<FInfiniteGameOperation> Operations;
	TArray<FInfiniteVariableUpdate> VariableUpdates;
	TArray<FInfiniteCardForgeJob> CardForgeJobs;
};

/** 一条可独立显示头像、名字和表情差分的聊天消息。 */
struct FInfiniteDialogueLine
{
	FString Speaker;
	FString PortraitId;
	FString Expression = TEXT("neutral");
	FString Text;
};

struct FInfiniteNarrativeBeat
{
	FString Title;
	/** Non-secret settings fingerprint used by the controller to reject stale prefetch beats. */
	FString StoryDirectionCacheKey;
	FString Speaker;
	/** 角色头像注册表 ID；为空时由 Speaker 名称或别名匹配。 */
	FString PortraitId;
	/** 兼容单句旧格式的表情；新格式使用 DialogueLines 中的逐句表情。 */
	FString Expression = TEXT("neutral");
	FString Narration;
	FString Dialogue;
	TArray<FInfiniteDialogueLine> DialogueLines;
	FString StatePatchJson;
	FString MemoryJson;
	TArray<FInfiniteNarrativeChoice> Choices;
	bool bError = false;
	FString Diagnostic;
};

/** The three user-visible RP request phases. Freeform is followed by exactly
 * one engine-forced jump before the input is offered again. */
enum class EInfiniteNarrativeRequestKind : uint8
{
	Normal,
	Freeform,
	ForcedFreeRPJump
};

struct FInfiniteNarrativeRequestContext
{
	int32 Cycle = 0;
	int32 HP = 0;
	int32 MaxHP = 0;
	int32 Gold = 0;
	int32 DeckSize = 0;
	int32 UpgradeableCardCount = 0;
	/** Unique display names only. The writer never sees IDs, counts, upgrades or definitions. */
	TArray<FString> AbilityNames;
	TArray<FString> RelicNames;
	/** Forge-only compact definitions of the cards and relics the player actually owns. */
	FString CardForgeBuildSummary;
	/** Internal-only resolvers used after the model refers to existing content by name. */
	TMap<FString, FString> CardNameToId;
	TMap<FString, FString> RelicNameToId;
	/** Unowned built-in relics eligible for the local fixed-relic route. */
	TArray<FString> AvailableFixedRelicIds;
	TMap<FString, FString> FixedRelicIdToName;
	/** Positive local modifier supplied by owned relic affixes; never decided by the LLM. */
	float RouteRewardBias = 0.f;
	TArray<FString> RecentHistory;
	/** Full role-aware history; locally unbounded and packed only by provider token budget. */
	TArray<FNarrativePromptMessage> ChatHistory;
	FString RecentRawContext;
	FString RecalledMemoryContext;
	FString CombatDigest;
	/** 当前或刚结束的战斗对象与场景，供战后剧情预演使用。 */
	FString CombatSetup;
	/**
	 * 引擎生成的权威战斗结算事实。模式 B 可在战斗开始时预先声明，
	 * 实际胜利后会写入存档并持续覆盖旧 RP/MVU 中的战斗中状态。
	 */
	FString CombatResolutionFact;
	/** 战斗后台预演请求；区别于普通 RP 续写。 */
	bool bCombatPrefetch = false;
	/** 模式 B：战斗尚未结束，但必须把最终胜利作为既成事实，且不得假定具体战报。 */
	bool bAssumeCombatVictoryWithoutLog = false;
	FString WorldStateJson;
	/** 本地引擎权威的身体、环境、关系与势力临时变量。 */
	FString EngineVariableContext;
	/** 最近连续没有进入战斗的RP轮数；用于把长期停滞显式反馈给关卡导演。 */
	int32 NarrativeOnlyStreak = 0;
	bool bAllowIncidentalEffect = true;
	/** This request carries a player-authored free RP action. */
	bool bFreeRPAction = false;
	/** This request is the one automatic turn immediately after free RP. */
	bool bFreeRPForcedJump = false;
	/** Exactly one direction selected by the engine from the previous beat's choices. */
	FString ForcedRPDirection;
	FString FreeformAction;
	/** A deterministic low-tier starting situation chosen locally; the writer fills in names and details. */
	FString OpeningSeed;
	/** Provider-returned reasoning is request-local and may be explicitly pre-injected on retry. */
	FString TemporaryReasoningContent;
	/** One-shot opening copy request. Only choice text may be returned; all engine facts stay local. */
	bool bOpeningChoiceWording = false;
	/** Opaque-to-engine prompt containing the fixed opening prose and the three bound relic slots. */
	FString OpeningChoicePrompt;
	/** Expected relic IDs; the opening wording response must address these IDs exactly once. */
	TArray<FString> OpeningChoiceRelicIds;
	/** Engine-owned metadata used only to reject accidental relic/effect leakage in copy. */
	TArray<FString> OpeningChoiceRelicNames;
	TArray<FString> OpeningChoiceRelicDescriptions;
	TArray<FString> OpeningChoiceRelicRarities;
};

DECLARE_DELEGATE_TwoParams(FOnInfiniteNarrativeReady, bool, const FInfiniteNarrativeBeat&);
DECLARE_DELEGATE_ThreeParams(FOnInfiniteOpeningWordingReady, bool, const TArray<FString>&, const FString&);

enum class EInfiniteNarrativeStreamStage : uint8
{
	Thinking,
	Writing,
	Compiling
};

/** Provisional, display-only writer snapshot. It never mutates game state. */
struct FInfiniteNarrativeStreamUpdate
{
	EInfiniteNarrativeStreamStage Stage = EInfiniteNarrativeStreamStage::Thinking;
	FInfiniteNarrativeBeat Preview;
	int32 ReasoningChars = 0;
	bool bHasVisibleContent = false;
};

DECLARE_DELEGATE_OneParam(FOnInfiniteNarrativeStreamUpdate, const FInfiniteNarrativeStreamUpdate&);
DECLARE_DELEGATE_ThreeParams(FOnInfiniteCardForgeReady, bool, const FCardData&, const FString&);

/**
 * Async OpenAI-compatible client. One narrative call writes the scene and three choices;
 * the bounded card-design agent is invoked only after a card-forge choice is selected.
 */
UCLASS()
class ASCENDSPIRE_API UInfiniteNarrativeService : public UObject
{
	GENERATED_BODY()

public:
	/** Returns a non-secret fingerprint for the literary-direction settings only. */
	static FString BuildStoryDirectionCacheKey(const FInfiniteNarrativeSettings& Settings);
	/** Conservative display-safety gate for fixed-opening copy; it never changes engine facts. */
	static bool IsOpeningChoiceWordingSafe(const FString& Text, const FString& RelicId,
		const FString& RelicName, const FString& RelicDescription, const FString& RelicRarity);

	void Generate(const FInfiniteNarrativeSettings& Settings,
		const FInfiniteNarrativeRequestContext& Context,
		FOnInfiniteNarrativeReady Completion,
		FOnInfiniteNarrativeStreamUpdate StreamUpdate = FOnInfiniteNarrativeStreamUpdate());

	/** One-shot copy pass for a fixed opening. It cannot modify routes, rewards, relics or enemies. */
	void GenerateOpeningChoiceWording(const FInfiniteNarrativeSettings& Settings,
		const FInfiniteNarrativeRequestContext& Context,
		FOnInfiniteOpeningWordingReady Completion);

	/** Cancel the current request and invalidate every queued callback or stream chunk. */
	void CancelGeneration();

	/** Forge only the card requested by the branch the player actually selected. */
	void ForgeCard(const FInfiniteNarrativeSettings& Settings,
		const FInfiniteNarrativeRequestContext& Context, const FInfiniteCardForgeJob& Job,
		FOnInfiniteCardForgeReady Completion);

	/** Headless regression hook: exercises the exact production response parser without issuing HTTP. */
	bool ParseResponseForAutomationTest(const FString& ResponseBody, FInfiniteNarrativeBeat& OutBeat,
		FString& OutError) const;
	/** Headless hook for verifying partial JSON scene extraction used by SSE previews. */
	FInfiniteNarrativeBeat ParseStreamingPreviewForAutomationTest(const FString& PartialContent) const;
	/** Headless hook for verifying opening copy stays bound to the three locked relic IDs. */
	bool ParseOpeningWordingForAutomationTest(const FString& ResponseBody,
		const TArray<FString>& ExpectedRelicIds, TArray<FString>& OutTexts, FString& OutError) const;
	/** Headless hook with public relic metadata to exercise the no-leak safety gate. */
	bool ParseOpeningWordingForAutomationTest(const FString& ResponseBody,
		const TArray<FString>& ExpectedRelicIds, const TArray<FString>& ExpectedRelicNames,
		const TArray<FString>& ExpectedRelicDescriptions, const TArray<FString>& ExpectedRelicRarities,
		TArray<FString>& OutTexts, FString& OutError) const;
	/** Headless hook for verifying writer/MVU isolation and branch overlay. */
	bool MergeCompilerResponseForAutomationTest(const FString& DraftJson, const FString& CompilerResponse,
		FInfiniteNarrativeBeat& OutBeat, FString& OutError);
	bool ParseForgedCardForAutomationTest(const FString& ResponseBody, int32 Cycle,
		FCardData& OutCard, FString& OutError) const;
	/** Headless hook for the same lightweight dry-run tool used by the card agent. */
	bool TestCardForgeCandidateForAutomationTest(const FString& Script, int32 Cycle,
		FCardData& OutCard, FString& OutFeedback) const;
	/** Headless hook for the bounded agent's JSON tool protocol. */
	bool ParseCardForgeAgentStepForAutomationTest(const FString& ResponseBody, FString& OutAction,
		FString& OutIntendedText, FString& OutScript, FString& OutImplementationCheck,
		FString& OutPowerCheck, FString& OutContent, FString& OutError) const;
	/** Deterministic local route planner hook; no model request and no game-state mutation. */
	TArray<FString> PlanRoutesForAutomationTest(const FInfiniteNarrativeRequestContext& Context,
		int32 Seed, TArray<FString>* OutPayloads = nullptr,
		TArray<FInfiniteEnemySpec>* OutCombatPlans = nullptr);
	/** Headless hook for verifying regex/recursive authoring-worldbook routing. */
	FString ResolveAuthoringKnowledgeForAutomationTest(const FString& DraftJson);
	/** Compatibility hook retained for existing automation callers; narrative repetition is advisory only. */
	void SetRecentNarrativeContextForAutomationTest(const FString& RecentRawContext);

private:
	enum class ERequestPhase : uint8
	{
		Generation,
		StateCompilation,
		CardForge,
		OpeningWording
	};

	TSharedPtr<IHttpRequest, ESPMode::ThreadSafe> ActiveRequest;
	uint64 GenerationSerial = 0;
	FOnInfiniteNarrativeReady PendingCompletion;
	FOnInfiniteOpeningWordingReady PendingOpeningWordingCompletion;
	FOnInfiniteNarrativeStreamUpdate PendingStreamUpdate;
	FOnInfiniteCardForgeReady PendingCardForgeCompletion;
	FInfiniteNarrativeRequestContext PendingContext;
	FInfiniteNarrativeSettings PendingSettings;
	FInfiniteNarrativeBeat PendingDraftBeat;
	bool bHasPendingDraftBeat = false;
	FString PendingDraftJson;
	/** Engine destinations are rolled before the narrative call; prose explains these outcomes. */
	TArray<FString> PendingChoiceRoutePlan;
	TArray<int32> PendingChoiceRouteValue;
	/** Optional engine-owned payload, currently the exact built-in relic id for relic_reward. */
	TArray<FString> PendingChoiceRoutePayload;
	/** Engine-owned combat identity, count and scaling rolled together with each combat route. */
	TArray<FInfiniteEnemySpec> PendingChoiceCombatPlan;
	bool bSuppressRoutePlanLog = false;
	ERequestPhase RequestPhase = ERequestPhase::Generation;
	int32 TokenCapAttempt = 0;
	int32 EffectiveMaxOutputTokens = 65535;
	/** Additional writer calls after an otherwise successful HTTP response cannot be parsed. */
	int32 WriterFormatRetryAttempt = 0;
	FString LastWriterFormatError;
	/** Provider compatibility fallback: retry one time after rejecting reasoning_content. */
	bool bReasoningContentFallbackAttempted = false;
	int32 MvuTokenCapAttempt = 0;
	int32 MvuSemanticRetryAttempt = 0;
	int32 TotalModelRequestCount = 0;
	/** Number of model turns spent by the bounded card-design agent. Usually one; repairs use the remainder. */
	int32 CardForgeAttempt = 0;
	TArray<FNarrativePromptMessage> CardForgeMessages;
	FString CardForgeLastToolFeedback;
	/** Short story-to-mechanics target written before implementation; repairs should preserve it. */
	FString CardForgeIntendedText;
	/** A mechanically valid candidate awaiting the same agent's semantic and power review. */
	FCardData CardForgePendingCandidate;
	bool bCardForgeAwaitingReview = false;
	/** Diagnostics only; never used as a workflow cutoff. */
	double CardForgeWorkflowStartedAt = 0.0;
	double CardForgeRequestStartedAt = 0.0;
	FInfiniteCardForgeJob PendingCardForgeJob;
	FString LastCardForgeError;
	FString LastMvuValidationError;
	int32 EffectiveMvuMaxOutputTokens = 32768;
	double WriterRequestStartedAt = 0.0;
	double MvuRequestStartedAt = 0.0;
	FNarrativeGenerationPreset NarrativePreset;
	FNarrativeGenerationPreset MvuPreset;
	FCriticalSection WriterStreamCriticalSection;
	TArray<uint8> WriterStreamPendingBytes;
	TArray<uint8> WriterStreamAllBytes;
	FString WriterStreamContent;
	FString WriterStreamReasoning;
	FString WriterStreamFinishReason;
	bool bWriterStreaming = false;
	bool bWriterStreamSawSse = false;
	bool bWriterStreamDone = false;
	bool bWriterStreamUpdateQueued = false;
	bool bWriterStreamFallbackAttempted = false;
	double LastWriterStreamUpdateAt = 0.0;

	void IssueRequest();
	FString ResolveEffectiveReasoningPrefill(const FInfiniteNarrativeSettings& Settings) const;
	void IssueOpeningWordingRequest();
	void HandleOpeningWordingComplete(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bSucceeded);
	bool ParseOpeningWordingResponse(const FString& ResponseBody, TArray<FString>& OutTexts,
		FString& OutError) const;
	void CompleteOpeningWording(bool bSuccess, const TArray<FString>& Texts, const FString& Diagnostic);
	void PrepareChoiceRoutePlan();
	FString DescribeChoiceRoutePlanForPrompt() const;
	void ApplyChoiceRoutePlan(FInfiniteNarrativeChoice& Choice, int32 ChoiceSlot) const;
	void IssueStateCompilation(const FString& DraftJson);
	void HandleWriterStreamBytes(void* Ptr, int64& InOutLength);
	void ProcessWriterSseLine(const FString& Line);
	void QueueWriterStreamUpdate();
	void EmitWriterStreamUpdate();
	/** Emit one unthrottled terminal snapshot before the ready callback can unbind it. */
	void FlushWriterStreamUpdate(const FInfiniteNarrativeBeat& FinalBeat);
	void EmitStreamStage(EInfiniteNarrativeStreamStage Stage);
	FString BuildWriterStreamTransportResponse();
	void ResetWriterStreamState();
	void HandleHttpComplete(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bSucceeded);
	void HandleStateCompilationComplete(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bSucceeded);
	void IssueCardForgeRequest();
	void HandleCardForgeComplete(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bSucceeded);
	bool ParseCardForgeAgentStep(const FString& ResponseBody, FString& OutAction,
		FString& OutIntendedText, FString& OutScript, FString& OutImplementationCheck,
		FString& OutPowerCheck, FString& OutContent, FString& OutError) const;
	FString LoadCardForgeWorldBook() const;
	FString BuildCardForgeContext() const;
	bool TestCardForgeCandidate(const FString& Script, FCardData& OutCard, FString& OutFeedback) const;
	void AppendCardForgeMessage(const FString& Role, const FString& Content);
	bool ParseForgedCard(const FString& ResponseBody, FCardData& OutCard, FString& OutError) const;
	void CompleteCardForge(bool bSuccess, const FCardData& Card, const FString& Diagnostic);
	bool MergeCompilerResponse(const FString& ResponseBody, FInfiniteNarrativeBeat& OutBeat, FString& OutError) const;
	bool ParseResponse(const FString& ResponseBody, FInfiniteNarrativeBeat& OutBeat, FString& OutError) const;
	bool ValidateStatePatch(const FString& PatchJson, FString& OutError) const;
	void CompleteSuccess(FInfiniteNarrativeBeat Beat);
	FString BuildSystemPrompt(const FInfiniteNarrativeRequestContext& Context) const;
	FString BuildUserPrompt(const FInfiniteNarrativeRequestContext& Context) const;
	TArray<FNarrativePromptMessage> BuildNarrativeMessages(FString& OutDiagnostic) const;
	TArray<FNarrativePromptMessage> BuildMvuMessages(const FString& DraftJson, FString& OutDiagnostic) const;
	void ApplyGenerationControls(const TSharedPtr<FJsonObject>& Root,
		const FNarrativeGenerationPreset& Preset, bool bMvu) const;
	FString BuildNarrativeOutputContract() const;
	FString BuildMvuOutputContract() const;
	FString BuildContentCatalog() const;
	FString LoadCapabilityManifest() const;
	FString LoadWorldBook() const;
	FString LoadCharacterRegistry() const;
	bool LoadSelectedCharacterCard(FNarrativeCharacterCardAsset& OutAsset) const;
	FString BuildCharacterCardPrompt(const FNarrativeCharacterCardAsset& Asset) const;
	FString LoadTriggeredAuthoringKnowledge(const FInfiniteNarrativeRequestContext& Context,
		bool bForceFullManual = false, const FString& DraftOverride = FString()) const;
	void CompleteWithError(const FString& Diagnostic);
	bool RetryWriterAfterFormatError(const FString& Error);
	bool ReserveModelRequest(const TCHAR* PhaseLabel);
};
