#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "RunManager.h"
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

UENUM()
enum class EGameScreen : uint8
{
	Title, Map, Combat, Reward, Event, Shop, Rest, GameOver, Victory
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

	// ---------- 拖拽出牌 ----------
	void HandleCardPressed(int32 CardIndex);
	void OnMouseLeftReleased();
	void BuildCardWidget(UButton* Btn, int32 CardIndex, bool bPlayable);
	/** 卡面内容构建（Scale 缩放全部尺寸/字号，原生绘制不模糊） */
	UWidget* MakeCardContent(UObject* Outer, int32 CardIndex, bool bPlayable, float Scale);
	/** 从卡牌数据构建完整卡面（用于奖励/坊市等非战斗场景） */
	UWidget* MakeCardContentFromData(UObject* Outer, const FCardData& CardData, bool bUpgraded, float Scale);
	UWidget* MakeRelicCardContentFromData(UObject* Outer, const FRelicData& RelicData, float Scale);
	void BuildEnemyCardWidget(UVerticalBox* EBox, int32 EnemyIndex, bool bIsLocked);
	UHorizontalBox* BuildStatusRow(UObject* Outer, const FCombatantState& State, int32 FontSize = 10);

	// ---------- 悬停大卡预览 ----------
	void ShowCardPreview(int32 CardIndex);
	void HideCardPreview();

	UPROPERTY()
	UBorder* CardPreviewWidget;
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
	void SpawnFloatingText(const FString& Text, FLinearColor Color, float X, float Y, float Duration = 1.2f);
	void SpawnSlashEffect(float X, float Y);
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

	// ---------- 工具 ----------
	UClickProxy* MakeProxy(const FString& Tag, int32 Index);
	UButton* MakeLinkedButton(UObject* Outer, const FString& Label, const FString& Tag, int32 Index, int32 FontSize = 20);
	UTextBlock* MakeLogText(const FString& Text, FSlateColor Color);

	UPROPERTY()
	TArray<UClickProxy*> Proxies;

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

private:
	UFUNCTION()
	void OnCombatLogDynamic(const FString& Msg);
};
