#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "RunManager.h"
#include "InfiniteNarrativeService.h"
#include "Combat/CombatEngine.h"
#include "AscendPlayerController.generated.h"

class UAscendRootWidget;
class UBorder;
class UVerticalBox;
class UClickProxy;
class UTextBlock;
class UButton;
class UWidget;
class UCanvasPanel;
class UOverlay;
class USizeBox;
class UHorizontalBox;
class USoundWaveProcedural;
class UEditableTextBox;
class UCheckBox;
class UComboBoxString;
class UMultiLineEditableTextBox;
class UScrollBox;

UENUM()
enum class EGameScreen : uint8
{
	Title, Map, Combat, Reward, Event, Shop, Rest, GameOver, Victory,
	Settings, InfiniteNarrative, Acquisition, AuthoredLibrary
};

/**
 * 游戏主控制器 —— 持有 Run/战斗引擎，驱动全部界面流程
 */
UCLASS()
class ASCENDSPIRE_API AAscendPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	virtual void BeginPlay() override;

	/** 统一点击派发（ClickProxy 回调）——延迟到下一帧执行，避免在点击回调中同步重建 UI */
	void DispatchClick(const FString& Tag, int32 Index);

	/** 实际的点击处理逻辑（下一帧调用） */
	void HandleClickAction(const FString& Tag, int32 Index);

	/** 激活游戏窗口（提到前台 + 请求焦点） */
	void ActivateGameWindow();

	virtual void SetupInputComponent() override;
	virtual void PlayerTick(float DeltaTime) override;

	UFUNCTION()
	void OnConfirmKey();

	UFUNCTION()
	void OnEscapeKey();

	// ---------- 拖拽出牌 ----------
	void HandleCardPressed(int32 CardIndex);
	void OnMouseLeftReleased();
	/** 根 UMG 从真实 PointerEvent 上报触摸移动位置（Android 拖牌坐标权威来源）。 */
	void HandleRootPointerMoved(const FVector2D& ScreenSpacePosition);
	void BuildCardWidget(UButton* Btn, int32 CardIndex, bool bPlayable, float CardScale = 1.f);
	/** 卡面内容构建（Scale 缩放全部尺寸/字号，原生绘制不模糊） */
	UWidget* MakeCardContent(UObject* Outer, int32 CardIndex, bool bPlayable, float Scale);
	/** 从卡牌数据构建完整卡面（用于奖励/坊市等非战斗场景） */
	UWidget* MakeCardContentFromData(UObject* Outer, const FCardData& CardData, bool bUpgraded, float Scale);
	UWidget* MakeRelicCardContentFromData(UObject* Outer, const FRelicData& RelicData, float Scale);
	void BuildEnemyCardWidget(UVerticalBox* EBox, int32 EnemyIndex, bool bIsLocked);
	UHorizontalBox* BuildStatusRow(UObject* Outer, const FCombatantState& State, int32 FontSize = 10);

	// ---------- 手牌悬停放大 ----------
	void ShowCardPreview(int32 CardIndex);
	void HideCardPreview(int32 CardIndex = INDEX_NONE);

	int32 HoveredCardIndex = INDEX_NONE;
	float HoveredCardOriginalAngle = 0.f;
	void UpdateCardDrag();
	void EndCardDrag();
	void ShowAttackLine(int32 EnemyIndex);
	void HideAttackLine();
	int32 GetDragTargetEnemy();
	void SetLockedTarget(int32 EnemyIndex);
	FVector2D GetEnemyScreenPos(int32 EnemyIndex) const;
	FVector2D GetViewportSize() const;

	void ShowRelicTooltip(const FRelicData& Relic, UWidget* Anchor);
	void ShowRelicTooltipByIndex(int32 Index);
	void HideRelicTooltip();

	bool bIsDraggingCard = false;
	int32 DragCardIndex = -1;
	bool bDragNeedsTarget = false;
	int32 DragTargetEnemy = -1;
	FVector2D DragStartMousePos;
	FVector2D DragLastCardCenter;
	bool bDragUsingTouch = false;
	bool bHasWidgetTouchPosition = false;
	FVector2D WidgetTouchCanvasPosition = FVector2D::ZeroVector;
	float CurrentHandCardScale = 1.f;
	/** 将触摸/鼠标的视口像素坐标稳定换算成动画 Canvas 的局部坐标。 */
	bool GetPointerCanvasPosition(FVector2D& OutPosition, bool& bOutTouchPressed) const;
	float GetResponsiveHandScale(const FVector2D& CanvasSize) const;
	int32 LockedTargetIndex = -1;

	UPROPERTY()
	UWidget* DraggedCardWidget;

	UPROPERTY()
	UBorder* GhostHighlightBorder;

	UPROPERTY()
	TArray<UBorder*> AttackLineSegments;

	/** 定时器句柄清理：每次构建新界面时统一清空，杜绝定时器泄漏 */
	TArray<FTimerHandle> AnimTimerHandles;

	/** 攻击线弹性动画定时器 */
	TSharedPtr<FTimerHandle> AttackLineTimerHandle;

	UPROPERTY()
	TArray<TWeakObjectPtr<UButton>> HandCardButtons;

	// ---------- 战斗动画 ----------
	void TriggerCombatAnimations(const FString& ActionType);
	/** 按卡牌 visual 定义播放投射物、刀光、命中和音效。 */
	void PlayCardVisual(const FCardInstance& Card, int32 TargetEnemyIndex);
	void SpawnFloatingText(const FString& Text, FLinearColor Color, float X, float Y, float Duration = 1.2f);
	void SpawnSlashEffect(float X, float Y, FLinearColor Color = FLinearColor(1.f, 1.f, 0.9f, 0.9f));
	void SpawnProjectileEffect(const FVector2D& From, const FVector2D& To, FLinearColor Color, float Duration);
	void SpawnImpactBurst(const FVector2D& Center, FLinearColor Color, float Duration);
	void SpawnHealBurst(const FVector2D& Center, FLinearColor Color, float Duration);
	void PlayVisualSound(const FString& SoundId);
	void AnimateScreenShake(float Intensity = 6.f, float Duration = 0.3f);
	void AnimateColorFlash(FLinearColor Color, float Duration = 0.3f);
	void ClearAnimations();

	// ---------- 牌堆检视与抽卡动画 ----------
	/** 牌堆内容查看面板（0=关闭 1=抽牌堆 2=弃牌堆） */
	int32 PileViewerMode = 0;

	/** 已播放抽卡动画的回合数（防止重复触发） */
	int32 LastSeenTurnCount = 0;

	/** 构建牌堆检视浮层 */
	void BuildPileViewer(UOverlay* ParentOverlay);

	/** 弹出奖励通知浮窗（获得卡牌/法宝/首次斩杀等），3秒淡出 */
	void ShowGainToasts();

	/** 抽卡动画：真实手牌一张张从牌堆飞入（位移+放大），与牌堆位置对应 */
	/** 手牌登场动画（Count 指定只动画后 Count 张，0=全部） */
	void PlayHandEntranceAnimation(int32 Count = 0);
	/** 回合结束弃牌动画：从各手牌位置逐张飞入弃牌堆 */
	void PlayEndTurnDiscardAnimation(const TArray<FVector2D>& FromPositions);

	/** 弃牌回洗动画：迷你卡快速一张张洗入牌堆 */
	void SpawnReshuffleEffect();

	/** 生成一张动画迷你卡（位移/缩放/弧度/透明度 三段式时间线） */
	void SpawnMiniCardAnim(const FVector2D& From, const FVector2D& To, float Delay, float Duration,
		float ScaleFrom, float ScaleMid, float ScaleTo, float ArcHeight,
		float FadeInEnd, float FadeOutStart);

	/** 牌堆按钮的 Canvas 逻辑坐标（bDrawPile: 抽牌堆/弃牌堆） */
	FVector2D GetPileAnchor(bool bDrawPile) const;

	UPROPERTY()
	TArray<UWidget*> ActiveAnimations;

	/** 程序化短音效的 UObject 持有列表，避免播放期间被 GC。 */
	UPROPERTY()
	TArray<USoundWaveProcedural*> ActiveSoundWaves;

	/** 本次卡牌结算是否已有专用表现，避免再叠加旧的通用刀光。 */
	FString LastPlayedVisualAnimation;

protected:
	UPROPERTY()
	URunManager* Run;

	UPROPERTY()
	UCombatEngine* Combat;

	UPROPERTY()
	UAscendRootWidget* RootWidget;

	UPROPERTY()
	UOverlay* RootOverlay;

	UPROPERTY()
	UBorder* ScreenHost;

	UPROPERTY()
	UCanvasPanel* AnimCanvas;

	/** 常驻右上角菜单按钮层，不随界面重建而销毁。 */
	UPROPERTY()
	UCanvasPanel* MenuButtonLayer;

	/** Esc/菜单按钮打开的模态菜单。 */
	UPROPERTY()
	UBorder* PauseMenuOverlay;

	bool bPauseMenuOpen = false;

	EGameScreen CurrentScreen = EGameScreen::Title;

	// 战斗动画
	TArray<int32> PreEnemyHP;
	TArray<int32> PreEnemyBlock;
	int32 PrePlayerHP = 0;
	int32 PrePlayerBlock = 0;

	// ---------- 战斗界面状态 ----------
	FCombatReward PendingReward;
	FNodeEncounter CurrentEncounter;
	bool bPendingKillLootChoice = false;

	// ---------- 界面构建 ----------
	void SetScreen(UWidget* Content, EGameScreen Screen);
	void ShowTitle();
	void ShowAuthoredLibrary();
	void ShowMap();
	void ShowCombat();
	void ShowStartRelicChoice();
	void ShowReward();
	void ShowEvent();
	void ShowNarrative();
	void ShowNarrativeOutcome();
	void ShowShop();
	void ShowRest();
	void ShowGameOver();
	void ShowVictory();
	void ShowBreakthrough();
	void ShowSettings(int32 Category = -1);
	void ShowInfiniteNarrative();
	void ShowInfiniteNarrativeLoading(const FString& Message = TEXT("正在推演下一幕……"));
	void ShowInfiniteNarrativeError(const FString& Diagnostic);
	void ShowAcquiredItems();
	void ShowPauseMenu();
	void HidePauseMenu();
	void TogglePauseMenu();
	void SaveAndReturnToTitle();
	void SaveAndQuitGame();

	/** 叙事结果后待进入的节点类型 */
	ENarrativeOutcomeType PendingNarrativeOutcomeType = ENarrativeOutcomeType::NextBeat;
	FString PendingNarrativeOutcomeParam;

	/** 战斗结束后延迟结算，等动画播完 */
	bool bCombatEndPending = false;
	FTimerHandle CombatEndTimer;
	void FinishCombatDelayed();
	/** 击退输入（战斗结束延迟期间禁用操作） */
	bool bInputLocked = false;

	/** 战斗界面刷新（每次操作后重建面板） */
	void RefreshCombatPanel();
	void BuildRelicSidebar(UHorizontalBox* HBox);
	void OnCombatLog(const FString& Msg);

	// ---------- 流程 ----------
	void EnterMapNode(int32 NodeId);
	void FinishCombat();
	void RestartRun();
	void RegisterEncounterRuntimeEnemies();
	void StartInfiniteNarrativeRun();
	void BuildInfiniteOpening();
	FInfiniteNarrativeRequestContext BuildInfiniteNarrativeContext(const FString& FreeformAction = TEXT(""),
		bool bCombatPrefetch = false, bool bAssumeVictoryWithoutLog = false) const;
	void RequestNextInfiniteNarrative(const FString& FreeformAction = TEXT(""));
	void HandleInfiniteNarrativeReady(bool bFromLLM, const FInfiniteNarrativeBeat& Beat);
	void HandleInfiniteNarrativeStreamUpdate(const FInfiniteNarrativeStreamUpdate& Update,
		bool bCombatPrefetch);
	void ResetInfiniteNarrativeStreamPreview();
	void RenderInfiniteNarrativeStreamPreview(const FInfiniteNarrativeStreamUpdate& Update);
	void ResetCombatNarrativePrefetch();
	void StartCombatNarrativePrefetch(bool bIncludeCombatLog);
	void HandleCombatNarrativePrefetchReady(bool bFromLLM, const FInfiniteNarrativeBeat& Beat);
	void ConsumeCombatNarrativePrefetch();
	void SelectInfiniteNarrativeChoice(int32 ChoiceIndex);
	void HandleInfiniteCardForgeReady(bool bSuccess, const FCardData& Card, const FString& Diagnostic);
	void BeginPendingInfiniteCombat();
	bool IsInfiniteChoiceAvailable(const FInfiniteNarrativeChoice& Choice, FString& OutReason) const;
	bool BeginInfiniteGameFunction();
	void CompleteInfiniteGameFunction();
	void ShowInfiniteCardOperation(bool bUpgrade, const FInfiniteGameOperation& Operation);
	void ShowInfinitePaidDeckRemoval();
	void ContinueAfterReward();
	void LoadInfiniteNarrativeSettings();
	void SaveInfiniteNarrativeSettings();
	void ResetSettingsWidgetRefs();
	void ReturnFromSettings();

	// ---------- 工具 ----------
	UClickProxy* MakeProxy(const FString& Tag, int32 Index);
	UButton* MakeLinkedButton(UObject* Outer, const FString& Label, const FString& Tag, int32 Index, int32 FontSize = 20);
	UTextBlock* MakeLogText(const FString& Text, FSlateColor Color);

	UPROPERTY()
	TArray<UClickProxy*> Proxies;

	/** Proxies created while constructing the next screen; promoted atomically by SetScreen. */
	UPROPERTY()
	TArray<UClickProxy*> PendingScreenProxies;

	/** Controls outside ScreenHost (the permanent menu button) must survive screen replacement. */
	UPROPERTY()
	TArray<UClickProxy*> PersistentProxies;

	/** Temporary pause-menu controls, released as soon as the modal closes. */
	UPROPERTY()
	TArray<UClickProxy*> PauseMenuProxies;

	UPROPERTY()
	TArray<FString> CombatLogLines;

	int32 LastProcessedLogIndex = 0;

	bool bLogExpanded = false;

	/** 初始法器选择暂存 */
	TArray<FString> StartRelicChoices;

	UPROPERTY()
	UBorder* RelicTooltipWidget;

	/** 法器栏 tooltip 定时器 */
	FTimerHandle RelicTooltipTimer;

	// ---------- 无尽叙事模式 ----------
	UPROPERTY()
	UInfiniteNarrativeService* InfiniteNarrativeService;

	FInfiniteNarrativeSettings InfiniteNarrativeSettings;
	FInfiniteNarrativeBeat CurrentInfiniteBeat;
	FInfiniteNarrativeChoice PendingInfiniteChoice;
	FNodeEncounter PendingInfiniteEncounter;
	TArray<FDeckCard> PendingNarrativeCards;
	TArray<FString> PendingNarrativeRelics;
	TArray<FString> PendingNarrativeLostCards;
	TArray<FString> PendingNarrativeLostRelics;
	TArray<FString> PendingNarrativeVariableReceipts;
	bool bInfiniteNarrativeRequestInFlight = false;
	bool bInfiniteChoiceResolved = false;
	bool bInfiniteFunctionFlowActive = false;
	bool bInfiniteCardOperationUpgrade = false;
	int32 PendingCardForgeChoiceIndex = INDEX_NONE;
	int32 NextInfiniteGameOperationIndex = 0;
	FInfiniteGameOperation ActiveInfiniteGameOperation;
	bool bCombatNarrativePrefetchReady = false;
	bool bCombatNarrativePrefetchFailed = false;
	bool bDiscardCombatNarrativePrefetch = false;
	bool bWaitingForCombatNarrativeAfterReward = false;
	bool bCombatPrefetchIncludedLog = false;
	FString CombatNarrativePrefetchDiagnostic;
	FInfiniteNarrativeBeat CombatNarrativePrefetchedBeat;
	FInfiniteNarrativeStreamUpdate CombatNarrativePrefetchStreamUpdate;
	bool bHasCombatNarrativePrefetchStreamUpdate = false;
	int32 SettingsCategory = 3;
	EGameScreen SettingsReturnScreen = EGameScreen::Title;

	UPROPERTY()
	UEditableTextBox* RPFreeformInput;

	UPROPERTY()
	UScrollBox* RPScrollBox;

	UPROPERTY()
	UVerticalBox* RPContentBox;

	UPROPERTY()
	UTextBlock* RPGenerationStatusText;

	UPROPERTY()
	UVerticalBox* RPStreamingContainer;

	UPROPERTY()
	UVerticalBox* RPStreamingDialogueBox;

	UPROPERTY()
	UTextBlock* RPStreamingTitleText;

	UPROPERTY()
	UTextBlock* RPStreamingNarrationText;

	UPROPERTY()
	TArray<UTextBlock*> RPStreamingDialogueTexts;

	TArray<FString> RPStreamingDialogueKeys;

	UPROPERTY()
	UEditableTextBox* SettingsEndpointInput;

	UPROPERTY()
	UEditableTextBox* SettingsApiKeyInput;

	UPROPERTY()
	UEditableTextBox* SettingsModelInput;

	UPROPERTY()
	UEditableTextBox* SettingsMvuModelInput;

	UPROPERTY()
	UEditableTextBox* SettingsTemperatureInput;

	UPROPERTY()
	UEditableTextBox* SettingsTopPInput;

	UPROPERTY()
	UEditableTextBox* SettingsTopKInput;

	UPROPERTY()
	UEditableTextBox* SettingsTopAInput;

	UPROPERTY()
	UEditableTextBox* SettingsMinPInput;

	UPROPERTY()
	UEditableTextBox* SettingsFrequencyPenaltyInput;

	UPROPERTY()
	UEditableTextBox* SettingsPresencePenaltyInput;

	UPROPERTY()
	UEditableTextBox* SettingsRepetitionPenaltyInput;

	UPROPERTY()
	UEditableTextBox* SettingsSeedInput;

	UPROPERTY()
	UMultiLineEditableTextBox* SettingsStopStringsInput;

	UPROPERTY()
	UComboBoxString* SettingsReasoningEffortCombo;

	UPROPERTY()
	UComboBoxString* SettingsMvuReasoningEffortCombo;

	UPROPERTY()
	UCheckBox* SettingsStreamResponseCheckBox;

	UPROPERTY()
	UCheckBox* SettingsRequestReasoningCheckBox;

	UPROPERTY()
	UCheckBox* SettingsRequestMvuReasoningCheckBox;

	UPROPERTY()
	UEditableTextBox* SettingsTimeoutInput;

	UPROPERTY()
	UEditableTextBox* SettingsSfxVolumeInput;

	UPROPERTY()
	UEditableTextBox* SettingsInputContextInput;

	UPROPERTY()
	UEditableTextBox* SettingsMaxOutputInput;

	UPROPERTY()
	UEditableTextBox* SettingsMvuMaxOutputInput;

	UPROPERTY()
	UEditableTextBox* SettingsNarrativeMinInput;

	UPROPERTY()
	UEditableTextBox* SettingsNarrativeMaxInput;

	UPROPERTY()
	UEditableTextBox* SettingsRecentRoundsInput;

	UPROPERTY()
	UEditableTextBox* SettingsCompressRoundsInput;

	UPROPERTY()
	UEditableTextBox* SettingsUnsummarizedTokensInput;

	UPROPERTY()
	UEditableTextBox* SettingsMemoryBudgetInput;

	UPROPERTY()
	UEditableTextBox* SettingsWorldBookBudgetInput;

	UPROPERTY()
	UEditableTextBox* SettingsWorldInfoScanDepthInput;

	UPROPERTY()
	UEditableTextBox* SettingsNarrativePresetPathInput;

	UPROPERTY()
	UEditableTextBox* SettingsMvuPresetPathInput;

	UPROPERTY()
	UMultiLineEditableTextBox* SettingsPersonaInput;

	UPROPERTY()
	UMultiLineEditableTextBox* SettingsCharacterDescriptionInput;

	UPROPERTY()
	UMultiLineEditableTextBox* SettingsCharacterPersonalityInput;

	UPROPERTY()
	UMultiLineEditableTextBox* SettingsScenarioInput;

	UPROPERTY()
	UMultiLineEditableTextBox* SettingsDialogueExamplesInput;

	UPROPERTY()
	UMultiLineEditableTextBox* SettingsAuthorNoteInput;

	UPROPERTY()
	UMultiLineEditableTextBox* SettingsCustomWorldBookInput;

	UPROPERTY()
	UMultiLineEditableTextBox* SettingsCharacterRegistryInput;

	UPROPERTY()
	UEditableTextBox* SettingsOpeningTitleInput;

	UPROPERTY()
	UEditableTextBox* SettingsOpeningSpeakerInput;

	UPROPERTY()
	UEditableTextBox* SettingsOpeningPortraitInput;

	UPROPERTY()
	UEditableTextBox* SettingsOpeningExpressionInput;

	UPROPERTY()
	UMultiLineEditableTextBox* SettingsOpeningNarrationInput;

	UPROPERTY()
	UMultiLineEditableTextBox* SettingsOpeningDialogueInput;

	UPROPERTY()
	UMultiLineEditableTextBox* SettingsContinuityInput;

	UPROPERTY()
	UCheckBox* SettingsShowInputCheckBox;

	UPROPERTY()
	UCheckBox* SettingsShowPortraitCheckBox;

	UPROPERTY()
	UCheckBox* SettingsScreenShakeCheckBox;

	UPROPERTY()
	UComboBoxString* SettingsCombatGenerationTimingCombo;

	UPROPERTY()
	UCheckBox* SettingsStructuredMemoryCheckBox;

	UPROPERTY()
	UCheckBox* SettingsContinuityCheckBox;

private:
	UFUNCTION()
	void OnCombatLogDynamic(const FString& Msg);
};
