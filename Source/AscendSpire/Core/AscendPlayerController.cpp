#include "AscendPlayerController.h"
#include "UI/ClickProxy.h"
#include "UI/AscendUIStyle.h"
#include "UI/AscendRootWidget.h"
#include "UI/AscendArt.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "TimerManager.h"
#include "Engine/GameViewportClient.h"
#include "Framework/Application/SlateApplication.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/Spacer.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/ProgressBar.h"
#include "Components/ScrollBox.h"
#include "Components/Image.h"
#include "Components/ScaleBox.h"

void AAscendPlayerController::BeginPlay()
{
	Super::BeginPlay();

	bShowMouseCursor = true;
	bEnableClickEvents = true;
	bEnableMouseOverEvents = true;

	FInputModeGameAndUI InputMode;
	InputMode.SetHideCursorDuringCapture(false);
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	SetInputMode(InputMode);

	// 主动激活窗口，防止 macOS 下窗口拿不到 key 焦点导致点击被系统吃掉
	ActivateGameWindow();
	FTimerHandle ActivateTimer;
	GetWorld()->GetTimerManager().SetTimer(ActivateTimer, FTimerDelegate::CreateWeakLambda(this, [this]()
	{
		ActivateGameWindow();
	}), 1.0f, false);

	UE_LOG(LogTemp, Display, TEXT("[AscendUI] BeginPlay 完成，输入模式已设置"));
	UE_LOG(LogTemp, Display, TEXT("[DIAG] BeginPlay creating RunManager"));

	Run = NewObject<URunManager>(this);

	// 根界面：Overlay（底层ScreenHost + 上层AnimCanvas）
	RootWidget = CreateWidget<UAscendRootWidget>(this, UAscendRootWidget::StaticClass());
	RootOverlay = NewObject<UOverlay>(RootWidget);
	RootWidget->WidgetTree->RootWidget = RootOverlay;

	ScreenHost = NewObject<UBorder>(RootOverlay);
	ScreenHost->SetBrushColor(FLinearColor(0.06f, 0.06f, 0.08f, 1.f));
	UOverlaySlot* HostSlot = RootOverlay->AddChildToOverlay(ScreenHost);
	HostSlot->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Fill);
	HostSlot->SetVerticalAlignment(EVerticalAlignment::VAlign_Fill);

	AnimCanvas = NewObject<UCanvasPanel>(RootOverlay);
	AnimCanvas->SetVisibility(ESlateVisibility::HitTestInvisible);
	RootWidget->AnimCanvas = AnimCanvas;
	UOverlaySlot* AnimSlot = RootOverlay->AddChildToOverlay(AnimCanvas);
	AnimSlot->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Fill);
	AnimSlot->SetVerticalAlignment(EVerticalAlignment::VAlign_Fill);

	RootWidget->AddToViewport(0);

	ShowTitle();
}

// -----------------------------------------------------------
// 工具
// -----------------------------------------------------------

UClickProxy* AAscendPlayerController::MakeProxy(const FString& Tag, int32 Index)
{
	UClickProxy* P = NewObject<UClickProxy>(this);
	P->Tag = Tag;
	P->Index = Index;
	P->Owner = this;
	Proxies.Add(P);
	return P;
}

UButton* AAscendPlayerController::MakeLinkedButton(UObject* Outer, const FString& Label, const FString& Tag, int32 Index, int32 FontSize)
{
	UButton* Btn = FAscendUIStyle::MakeStyledButton(Outer, Label, FontSize, FAscendUIStyle::PaperWhite());
	// 代理挂在按钮上，与按钮同生命周期（否则被 GC 回收导致点击失效）
	UClickProxy* P = NewObject<UClickProxy>(Btn);
	P->Tag = Tag;
	P->Index = Index;
	P->Owner = this;
	Btn->OnClicked.AddDynamic(P, &UClickProxy::HandleClick);
	Proxies.Add(P);
	return Btn;
}

UTextBlock* AAscendPlayerController::MakeLogText(const FString& Text, FSlateColor Color)
{
	return FAscendUIStyle::MakeText(RootWidget, Text, 15, Color);
}

void AAscendPlayerController::SetScreen(UWidget* Content, EGameScreen Screen)
{
	UE_LOG(LogTemp, Display, TEXT("[DIAG] SetScreen begin screen=%d"), static_cast<int32>(Screen));
	ClearAnimations();
	UE_LOG(LogTemp, Display, TEXT("[DIAG] SetScreen after ClearAnimations"));
	CurrentScreen = Screen;

	// 各界面微调底色，营造层次（战斗偏墨蓝、地图偏黛绿、标题偏暖墨）
	FLinearColor Bg(0.06f, 0.06f, 0.08f);
	FString BgArt;
	float DimAlpha = 0.62f;
	switch (Screen)
	{
	case EGameScreen::Title:    Bg = FLinearColor(0.075f, 0.06f, 0.055f); BgArt = TEXT("Art/bg/title.png"); DimAlpha = 0.45f; break;
	case EGameScreen::Map:      Bg = FLinearColor(0.05f, 0.062f, 0.055f); BgArt = TEXT("Art/bg/map.png"); DimAlpha = 0.58f; break;
	case EGameScreen::Combat:   Bg = FLinearColor(0.052f, 0.048f, 0.072f); BgArt = TEXT("Art/bg/combat.png"); DimAlpha = 0.68f; break;
	case EGameScreen::Reward:   Bg = FLinearColor(0.06f, 0.055f, 0.05f); BgArt = TEXT("Art/bg/reward.png"); DimAlpha = 0.60f; break;
	case EGameScreen::Event:    Bg = FLinearColor(0.06f, 0.055f, 0.075f); break;
	case EGameScreen::Shop:     Bg = FLinearColor(0.07f, 0.06f, 0.045f); break;
	case EGameScreen::Rest:     Bg = FLinearColor(0.045f, 0.06f, 0.06f); break;
	default: break;
	}
	ScreenHost->SetBrushColor(Bg);

	// 背景水墨图 + 暗化遮罩（保证文字可读）
	if (!BgArt.IsEmpty())
	{
		if (UImage* BgImg = FAscendArt::MakeImage(RootWidget, BgArt))
		{
			UOverlay* Wrap = NewObject<UOverlay>(RootWidget);

			UScaleBox* BgScale = NewObject<UScaleBox>(Wrap);
			BgScale->SetStretch(EStretch::ScaleToFill);
			BgScale->SetContent(BgImg);
			UOverlaySlot* BgSlot = Wrap->AddChildToOverlay(BgScale);
			BgSlot->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Fill);
			BgSlot->SetVerticalAlignment(EVerticalAlignment::VAlign_Fill);

			UBorder* Dim = NewObject<UBorder>(Wrap);
			Dim->SetBrushColor(FLinearColor(0.03f, 0.03f, 0.05f, DimAlpha));
			UOverlaySlot* DimSlot = Wrap->AddChildToOverlay(Dim);
			DimSlot->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Fill);
			DimSlot->SetVerticalAlignment(EVerticalAlignment::VAlign_Fill);

			UOverlaySlot* ContentSlot = Wrap->AddChildToOverlay(Content);
			ContentSlot->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Fill);
			ContentSlot->SetVerticalAlignment(EVerticalAlignment::VAlign_Fill);

			ScreenHost->SetContent(Wrap);
			return;
		}
	}
	ScreenHost->SetContent(Content);
}

void AAscendPlayerController::OnCombatLogDynamic(const FString& Msg)
{
	OnCombatLog(Msg);
}

void AAscendPlayerController::OnCombatLog(const FString& Msg)
{
	CombatLogLines.Add(Msg);
	if (CombatLogLines.Num() > 60) CombatLogLines.RemoveAt(0);
}

// -----------------------------------------------------------
// 统一点击派发
// -----------------------------------------------------------

void AAscendPlayerController::DispatchClick(const FString& Tag, int32 Index)
{
	UE_LOG(LogTemp, Display, TEXT("[CLICK] 派发: %s [%d]"), *Tag, Index);

	// UMG 陷阱: 不能在按钮点击回调里同步重建控件树（被点按钮自身会被销毁，
	// 导致 Slate 鼠标捕获挂在幽灵控件上，后续点击全部丢失）。
	// 统一延迟到下一帧处理。
	if (UWorld* World = GetWorld())
	{
		FTimerHandle TempHandle;
		World->GetTimerManager().SetTimerForNextTick(
			FTimerDelegate::CreateWeakLambda(this, [this, Tag, Index]()
			{
				HandleClickAction(Tag, Index);
			}));
	}
}

void AAscendPlayerController::HandleClickAction(const FString& Tag, int32 Index)
{
	if (bInputLocked) return;
	if (Tag == TEXT("title_new"))
	{
		Run->StartNewRun(FMath::RandRange(1, 999999));
		ShowStartRelicChoice();
	}
	else if (Tag == TEXT("title_continue"))
	{
		if (Run->LoadRun()) ShowMap();
	}
	else if (Tag == TEXT("start_relic"))
	{
		if (StartRelicChoices.IsValidIndex(Index))
		{
			Run->State.RelicIds.Add(StartRelicChoices[Index]);
		}
		ShowMap();
	}
	else if (Tag == TEXT("restart"))
	{
		ShowTitle();
	}
	else if (Tag == TEXT("node"))
	{
		EnterMapNode(Index);
	}
	else if (Tag == TEXT("lock_target"))
	{
		if (Combat && Combat->Enemies.IsValidIndex(Index))
		{
			SetLockedTarget(Index);
			RefreshCombatPanel();
		}
	}
	else if (Tag == TEXT("toggle_log"))
	{
		bLogExpanded = !bLogExpanded;
		RefreshCombatPanel();
	}
	else if (Tag == TEXT("pile_draw"))
	{
		PileViewerMode = (PileViewerMode == 1) ? 0 : 1;
		RefreshCombatPanel();
	}
	else if (Tag == TEXT("pile_discard"))
	{
		PileViewerMode = (PileViewerMode == 2) ? 0 : 2;
		RefreshCombatPanel();
	}
	else if (Tag == TEXT("pile_close"))
	{
		PileViewerMode = 0;
		RefreshCombatPanel();
	}
	else if (Tag == TEXT("endturn"))
	{
		if (!Combat || !Combat->bCombatActive) return;
		UE_LOG(LogTemp, Display, TEXT("[DIAG] endturn begin turn=%d"), Combat->TurnCount);
		Combat->EndPlayerTurn();
		UE_LOG(LogTemp, Display, TEXT("[DIAG] endturn after EndPlayerTurn turn=%d over=%d"), Combat->TurnCount, Combat->IsCombatOver());
		if (Combat->IsCombatOver())
		{
			bCombatEndPending = true;
			bInputLocked = true;
			TriggerCombatAnimations(TEXT("enemy_turn"));
			if (GetWorld())
				GetWorld()->GetTimerManager().SetTimer(CombatEndTimer, this,
					&AAscendPlayerController::FinishCombatDelayed, 0.6f, false);
			return;
		}
		RefreshCombatPanel();
		UE_LOG(LogTemp, Display, TEXT("[DIAG] endturn after RefreshCombatPanel"));
		TriggerCombatAnimations(TEXT("enemy_turn"));
		UE_LOG(LogTemp, Display, TEXT("[DIAG] endturn done"));
	}
	else if (Tag == TEXT("pill"))
	{
		if (!Combat || !Combat->bCombatActive) return;
		if (Run->State.PillIds.IsValidIndex(Index))
		{
			Combat->UsePill(Run->State.PillIds[Index]);
			Run->State.PillIds.RemoveAt(Index);
			if (Combat->IsCombatOver())
			{
				bCombatEndPending = true;
				bInputLocked = true;
				TriggerCombatAnimations(TEXT("pill"));
				if (GetWorld())
					GetWorld()->GetTimerManager().SetTimer(CombatEndTimer, this,
						&AAscendPlayerController::FinishCombatDelayed, 0.6f, false);
				return;
			}
			RefreshCombatPanel();
			TriggerCombatAnimations(TEXT("pill"));
		}
	}
	else if (Tag == TEXT("loot_yes"))
	{
		PendingReward = Run->ResolveCombatVictory(true, Combat->Player.HP, Combat->Gold);
		ShowReward();
	}
	else if (Tag == TEXT("loot_no"))
	{
		PendingReward = Run->ResolveCombatVictory(false, Combat->Player.HP, Combat->Gold);
		ShowReward();
	}
	else if (Tag == TEXT("reward"))
	{
		if (PendingReward.CardChoices.IsValidIndex(Index))
		{
			Run->PickRewardCard(PendingReward.CardChoices[Index]);
		}
		if (Run->State.bRunVictory) ShowBreakthrough();
		else { Run->SaveRun(); ShowMap(); }
	}
	else if (Tag == TEXT("reward_skip"))
	{
		Run->PickRewardCard(FDeckCard());
		if (Run->State.bRunVictory) ShowBreakthrough();
		else { Run->SaveRun(); ShowMap(); }
	}
	else if (Tag == TEXT("reward_to_narrative"))
	{
		if (PendingReward.CardChoices.IsValidIndex(Index))
		{
			Run->PickRewardCard(PendingReward.CardChoices[Index]);
		}
		// 叙事战斗结束后返回剧情
		Run->bNarrativeMode = true;
		Run->CurrentBeatId = TEXT("");
		ShowNarrative();
	}
	else if (Tag == TEXT("event_choice"))
	{
		Run->ResolveEventChoice(Index);
		Run->SaveRun();
		ShowMap();
	}
	else if (Tag == TEXT("shop_buy"))
	{
		UE_LOG(LogTemp, Display, TEXT("[DIAG] shop_buy begin idx=%d"), Index);
		Run->BuyShopItem(Index);
		UE_LOG(LogTemp, Display, TEXT("[DIAG] shop_buy ShowShop begin"));
		ShowShop();
		UE_LOG(LogTemp, Display, TEXT("[DIAG] shop_buy done"));
	}
	else if (Tag == TEXT("shop_leave"))
	{
		UE_LOG(LogTemp, Display, TEXT("[DIAG] shop_leave begin"));
		Run->SaveRun();
		UE_LOG(LogTemp, Display, TEXT("[DIAG] shop_leave ShowMap begin"));
		ShowMap();
		UE_LOG(LogTemp, Display, TEXT("[DIAG] shop_leave done"));
	}
	else if (Tag == TEXT("rest_heal"))
	{
		Run->ResolveRest(true);
		Run->SaveRun();
		ShowMap();
	}
	else if (Tag == TEXT("rest_upgrade"))
	{
		Run->ResolveRest(false);
		Run->SaveRun();
		ShowMap();
	}
	else if (Tag == TEXT("narrative_proceed"))
	{
		// 进入由叙事结果决定的游戏节点
		switch (PendingNarrativeOutcomeType)
		{
		case ENarrativeOutcomeType::Combat:
		case ENarrativeOutcomeType::Elite:
		case ENarrativeOutcomeType::Boss:
		{
			FNodeEncounter Enc;
			Enc.Type = PendingNarrativeOutcomeType == ENarrativeOutcomeType::Elite ? EMapNodeType::Elite
				: PendingNarrativeOutcomeType == ENarrativeOutcomeType::Boss ? EMapNodeType::Boss
				: EMapNodeType::Combat;
			Enc.EnemyIds.Add(PendingNarrativeOutcomeParam);
			Enc.EnemyLevel = Run->State.CurrentFloor / 3;
			Enc.EnemyHPBonus = Run->State.KillStreak * 5;
			CurrentEncounter = Enc;

			Combat = NewObject<UCombatEngine>(this);
			Combat->OnLog.AddDynamic(this, &AAscendPlayerController::OnCombatLogDynamic);
			CombatLogLines.Reset();
			LastProcessedLogIndex = 0;
			if (Combat->StartCombat(Run->State.Deck, Enc.EnemyIds, Run->State.RelicIds,
				Run->State.MaxHP, Run->State.HP, Enc.EnemyHPBonus,
				FMath::RandRange(1, 999999), Enc.EnemyLevel))
			{
				ShowCombat();
			}
			break;
		}
		case ENarrativeOutcomeType::Rest:
			ShowRest();
			break;
		case ENarrativeOutcomeType::Shop:
			ShowShop();
			break;
		case ENarrativeOutcomeType::Event:
			ShowEvent();
			break;
		case ENarrativeOutcomeType::GameOver:
			Run->State.HP = 0;
			Run->ResolveDefeat();
			ShowGameOver();
			break;
		default:
			// 回退到地图
			Run->SaveRun();
			ShowMap();
			break;
		}
	}
	else if (Tag == TEXT("continue_map"))
	{
		ShowMap();
	}
}

// -----------------------------------------------------------
// 流程
// -----------------------------------------------------------

void AAscendPlayerController::EnterMapNode(int32 ChoiceIndex)
{
	CurrentEncounter = Run->ChooseOption(ChoiceIndex);

	// 叙事选项处理
	if (CurrentEncounter.bIsNarrative)
	{
		const FNarrativeBeat Beat = Run->GetCurrentNarrativeBeat();
		if (Beat.Choices.IsValidIndex(CurrentEncounter.NarrativeChoiceIndex))
		{
			const FNarrativeOutcome& Outcome = Beat.Choices[CurrentEncounter.NarrativeChoiceIndex].Outcome;
			PendingNarrativeOutcomeType = Outcome.Type;
			PendingNarrativeOutcomeParam = Outcome.Param;
		}
		const bool bEntersGameNode = Run->AdvanceNarrative(CurrentEncounter.NarrativeChoiceIndex);
		if (bEntersGameNode)
		{
			ShowNarrativeOutcome();
		}
		else
		{
			ShowNarrative();
		}
		return;
	}

	switch (CurrentEncounter.Type)
	{
	case EMapNodeType::Combat:
	case EMapNodeType::Elite:
	case EMapNodeType::Boss:
	{
		Combat = NewObject<UCombatEngine>(this);
		Combat->OnLog.AddDynamic(this, &AAscendPlayerController::OnCombatLogDynamic);
		CombatLogLines.Reset();
		LastProcessedLogIndex = 0;

		if (Combat->StartCombat(Run->State.Deck, CurrentEncounter.EnemyIds, Run->State.RelicIds,
			Run->State.MaxHP, Run->State.HP, CurrentEncounter.EnemyHPBonus,
			FMath::RandRange(1, 999999), CurrentEncounter.EnemyLevel))
		{
			ShowCombat();
		}
		break;
	}
	case EMapNodeType::Event:
		ShowEvent();
		break;
	case EMapNodeType::Shop:
		ShowShop();
		break;
	case EMapNodeType::Rest:
		ShowRest();
		break;
	}
}

void AAscendPlayerController::FinishCombatDelayed()
{
	if (!bCombatEndPending) return;
	bCombatEndPending = false;
	bInputLocked = false;
	CombatEndTimer.Invalidate();
	if (Combat)
		FinishCombat();
}

void AAscendPlayerController::FinishCombat()
{
	if (Combat->IsVictory())
	{
		const bool bIsElite = (CurrentEncounter.Type == EMapNodeType::Elite);

		if (bIsElite)
		{
			// 杀人夺宝抉择界面
			bPendingKillLootChoice = true;
			ShowReward(); // ShowReward 内部根据 bPendingKillLootChoice 显示抉择
		}
		else
		{
			PendingReward = Run->ResolveCombatVictory(false, Combat->Player.HP, Combat->Gold);
			ShowReward();
		}
	}
	else
	{
		Run->ResolveDefeat();
		ShowGameOver();
	}
}

void AAscendPlayerController::RestartRun()
{
	ShowTitle();
}

void AAscendPlayerController::ActivateGameWindow()
{
	if (UGameViewportClient* VC = GetWorld() ? GetWorld()->GetGameViewport() : nullptr)
	{
		TSharedPtr<SWindow> Window = VC->GetWindow();
		if (Window.IsValid())
		{
			Window->BringToFront(true);
			FSlateApplication::Get().SetKeyboardFocus(Window, EFocusCause::SetDirectly);
			UE_LOG(LogTemp, Display, TEXT("[AscendUI] 已请求窗口前台激活"));
		}
	}
}

void AAscendPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();
	InputComponent->BindKey(EKeys::Enter, IE_Pressed, this, &AAscendPlayerController::OnConfirmKey);
	InputComponent->BindKey(EKeys::SpaceBar, IE_Pressed, this, &AAscendPlayerController::OnConfirmKey);
	InputComponent->BindKey(EKeys::LeftMouseButton, IE_Released, this, &AAscendPlayerController::OnMouseLeftReleased);
}

void AAscendPlayerController::OnConfirmKey()
{
	UE_LOG(LogTemp, Display, TEXT("[KEY] 确认键按下，当前界面: %d"), (int32)CurrentScreen);
	if (CurrentScreen == EGameScreen::Title)
	{
		HandleClickAction(TEXT("title_new"), 0);
	}
}

// -----------------------------------------------------------
// 拖拽出牌系统
// -----------------------------------------------------------

void AAscendPlayerController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);
	if (bIsDraggingCard)
	{
		UpdateCardDrag();
	}
}

void AAscendPlayerController::OnMouseLeftReleased()
{
	UE_LOG(LogTemp, Display, TEXT("[MOUSEUP] bIsDragging=%d"), bIsDraggingCard);
	if (bIsDraggingCard)
	{
		EndCardDrag();
	}
}

FVector2D AAscendPlayerController::GetViewportSize() const
{
	FVector2D VP(1200.f, 720.f);
	if (UGameViewportClient* VC = GetWorld() ? GetWorld()->GetGameViewport() : nullptr)
		VC->GetViewportSize(VP);
	return VP;
}

FVector2D AAscendPlayerController::GetEnemyScreenPos(int32 EnemyIndex) const
{
	if (!Combat || !Combat->Enemies.IsValidIndex(EnemyIndex)) return FVector2D(600.f, 100.f);
	FVector2D VP = GetViewportSize();
	int32 N = Combat->Enemies.Num();
	float CardW = 150.f;
	float Gap = 16.f;
	float TotalW = N * CardW + (N - 1) * Gap;
	float StartX = (VP.X - TotalW) * 0.5f;
	float X = StartX + EnemyIndex * (CardW + Gap) + CardW * 0.5f;
	FVector2D ViewportPos(X, VP.Y * 0.15f);

	// Compute directly in CanvasPanel logical space (matched to ScreenHost layout)
	FGeometry AG = AnimCanvas->GetTickSpaceGeometry();
	float VW = AG.GetLocalSize().X;
	float VH = AG.GetLocalSize().Y;
	float HPad = 12.f; // VerticalBox slot padding (6 each side)
	float HBoxLeft = HPad * 0.5f; // 6
	float CardSlotW = CardW + Gap; // 166
	float TotalLog = N * CardSlotW;
	float FillW = (VW - HPad - TotalLog) * 0.5f;
	float XLog = HBoxLeft + FillW + EnemyIndex * CardSlotW + 8.f + CardW * 0.5f;
	FVector2D LogicalPos(XLog, VH * 0.18f);

	UE_LOG(LogTemp, Display, TEXT("[ENEMYPOS] idx=%d VP=%.0f,%.0f canvasLocal=%.0f,%.0f logical=%.0f,%.0f"),
		EnemyIndex, ViewportPos.X, ViewportPos.Y,
		AG.AbsoluteToLocal(ViewportPos + AG.GetAbsolutePosition()).X,
		AG.AbsoluteToLocal(ViewportPos + AG.GetAbsolutePosition()).Y,
		LogicalPos.X, LogicalPos.Y);

	return LogicalPos;
}

void AAscendPlayerController::BuildCardWidget(UButton* Btn, int32 CardIndex, bool bPlayable)
{
	Btn->SetBackgroundColor(FLinearColor::Transparent);
	Btn->SetContent(MakeCardContent(Btn, CardIndex, bPlayable, 1.f));
	if (!bPlayable) Btn->SetRenderOpacity(0.45f);
}

UWidget* AAscendPlayerController::MakeCardContent(UObject* Outer, int32 CardIndex, bool bPlayable, float Scale)
{
	if (!Combat || !Combat->Hand.IsValidIndex(CardIndex)) return NewObject<USpacer>(Outer);
	const FCardInstance& Card = Combat->Hand[CardIndex];
	const float S = Scale;

	// Size box for fixed card dimensions
	USizeBox* CardSizer = NewObject<USizeBox>(Outer);
	CardSizer->SetWidthOverride(162.f * S);
	CardSizer->SetHeightOverride(194.f * S);

	const FLinearColor TypeCol = FAscendUIStyle::CardTypeColor(Card.Data.Type);

	// Card frame border（类型色描边）
	UBorder* Frame = NewObject<UBorder>(CardSizer);
	Frame->SetBrushColor(TypeCol * 0.55f + FLinearColor(0.08f, 0.07f, 0.05f) * 0.45f);
	Frame->SetPadding(FMargin(2.f * S));

	// Card face
	UBorder* Face = NewObject<UBorder>(Frame);
	const FLinearColor FaceColor = bPlayable
		? FLinearColor(0.30f, 0.26f, 0.22f)
		: FLinearColor(0.18f, 0.16f, 0.14f);
	Face->SetBrushColor(FaceColor);

	UVerticalBox* VBox = NewObject<UVerticalBox>(Face);

	// --- Art area (fill remaining) ---
	UBorder* ArtArea = NewObject<UBorder>(VBox);
	ArtArea->SetBrushColor(TypeCol * 0.35f + FLinearColor(0.13f, 0.11f, 0.09f) * 0.65f);
	UVerticalBoxSlot* ArtSlot = VBox->AddChildToVerticalBox(ArtArea);
	FSlateChildSize FillAll(ESlateSizeRule::Fill);
	FillAll.Value = 1.f;
	ArtSlot->SetSize(FillAll);

	// 卡面图（水墨贴图，缺失时回退纯色）+ 徽标叠加
	UOverlay* ArtOvl = NewObject<UOverlay>(ArtArea);
	if (UImage* Img = FAscendArt::MakeImage(ArtOvl, Card.Data.ArtPath))
	{
		UScaleBox* ImgScale = NewObject<UScaleBox>(ArtOvl);
		ImgScale->SetStretch(EStretch::ScaleToFill);
		ImgScale->SetContent(Img);
		UOverlaySlot* ImgSlot = ArtOvl->AddChildToOverlay(ImgScale);
		ImgSlot->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Fill);
		ImgSlot->SetVerticalAlignment(EVerticalAlignment::VAlign_Fill);
	}

	// 保留/消耗徽标（左上角小字，叠在 ArtArea 内）
	if (Card.Data.bRetain || Card.Data.bExhaust)
	{
		UTextBlock* Badge = FAscendUIStyle::MakeText(ArtOvl,
			Card.Data.bRetain ? TEXT("留") : TEXT("耗"), FMath::RoundToInt(11.f * S),
			Card.Data.bRetain ? FAscendUIStyle::JadeGreen() : FAscendUIStyle::GoldYellow());
		Badge->SetJustification(ETextJustify::Left);
		Badge->SetMargin(FMargin(3.f * S, 2.f * S, 0.f, 0.f));
		UOverlaySlot* BadgeSlot = ArtOvl->AddChildToOverlay(Badge);
		BadgeSlot->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Left);
		BadgeSlot->SetVerticalAlignment(EVerticalAlignment::VAlign_Top);
	}
	ArtArea->SetContent(ArtOvl);

	// --- Text area (bottom, fixed) ---
	UBorder* TextBg = NewObject<UBorder>(VBox);
	TextBg->SetBrushColor(FLinearColor(0.22f, 0.19f, 0.16f, 0.92f));

	UVerticalBox* TextBox = NewObject<UVerticalBox>(TextBg);

	// Name + cost row
	UHorizontalBox* NameRow = NewObject<UHorizontalBox>(TextBox);
	UTextBlock* NameT = FAscendUIStyle::MakeText(NameRow, Card.GetDisplayName(),
		FMath::RoundToInt(13.f * S), FAscendUIStyle::PaperWhite());
	NameT->SetJustification(ETextJustify::Left);
	UHorizontalBoxSlot* NameSlot = NameRow->AddChildToHorizontalBox(NameT);
	NameSlot->SetPadding(FMargin(4.f * S, 1.f * S, 0.f, 0.f));

	NameRow->AddChildToHorizontalBox(NewObject<USpacer>(NameRow))->SetSize(FSlateChildSize(ESlateSizeRule::Fill));

	USizeBox* GemSizer = NewObject<USizeBox>(NameRow);
	GemSizer->SetWidthOverride(22.f * S);
	GemSizer->SetHeightOverride(22.f * S);
	UBorder* CostGem = NewObject<UBorder>(GemSizer);
	CostGem->SetBrushColor(FLinearColor(0.25f, 0.45f, 0.80f));
	const int32 EffCost = Combat ? Combat->GetEffectiveCost(Card) : Card.GetCost();
	UTextBlock* CostT = FAscendUIStyle::MakeText(CostGem, FString::FromInt(EffCost),
		FMath::RoundToInt(13.f * S),
		EffCost < Card.GetCost() ? FAscendUIStyle::JadeGreen() : FAscendUIStyle::PaperWhite());
	CostT->SetJustification(ETextJustify::Center);
	CostGem->SetContent(CostT);
	GemSizer->SetContent(CostGem);
	UHorizontalBoxSlot* CostSlot = NameRow->AddChildToHorizontalBox(GemSizer);
	CostSlot->SetPadding(FMargin(0.f, 1.f * S, 4.f * S, 0.f));

	TextBox->AddChildToVerticalBox(NameRow);

	// Description（纯机制文本，纸白色易读）
	FString DisplayDesc = (Card.bUpgraded && !Card.Data.UpgradedDescription.IsEmpty())
		? Card.Data.UpgradedDescription : Card.Data.Description;
	if (Card.Data.Id == TEXT("one_sword") && Combat)
	{
		const int32 TotalDmg = Combat->GetOneSwordDamage();
		DisplayDesc = FString::Printf(TEXT("对所有敌人造成 %d 点伤害（每层强化+6）。保留，消失"), TotalDmg);
	}
	else if (Card.Data.Id == TEXT("wan_jian_gui_zong") && Combat)
	{
		const int32 Base = Card.bUpgraded ? 5 : 3;
		const int32 Inc = Card.bUpgraded ? 4 : 3;
		const int32 Next = Base + Card.RepeatCount * Inc;
		DisplayDesc = FString::Printf(TEXT("对所有敌人造成 %d 点伤害（每用一次+%d）已用%d次。保留"), Next, Inc, Card.RepeatCount);
	}
	else if (Card.RepeatCount > 0)
	{
		DisplayDesc = FString::Printf(TEXT("%s (当前重复%d次)"), *Card.Data.Description, Card.RepeatCount);
	}
	UTextBlock* DescT = FAscendUIStyle::MakeText(TextBox, DisplayDesc,
		FMath::RoundToInt(10.f * S), FAscendUIStyle::PaperWhite());
	DescT->SetAutoWrapText(true);
	DescT->SetJustification(ETextJustify::Center);
	UVerticalBoxSlot* DescSlot = TextBox->AddChildToVerticalBox(DescT);
	DescSlot->SetPadding(FMargin(4.f * S, 2.f * S));

	// Flavor（剧情小字，底部不起眼的点缀）
	if (!Card.Data.Flavor.IsEmpty())
	{
		UTextBlock* FlavorT = FAscendUIStyle::MakeText(TextBox, Card.Data.Flavor,
			FMath::RoundToInt(8.f * S),
			FSlateColor(FLinearColor(0.40f, 0.38f, 0.34f)));
		FlavorT->SetAutoWrapText(true);
		FlavorT->SetJustification(ETextJustify::Center);
		UVerticalBoxSlot* FlavorSlot = TextBox->AddChildToVerticalBox(FlavorT);
		FlavorSlot->SetPadding(FMargin(4.f * S, 1.f * S, 4.f * S, 2.f * S));
	}

	TextBg->SetContent(TextBox);
	UVerticalBoxSlot* TextSlot = VBox->AddChildToVerticalBox(TextBg);
	FSlateChildSize Auto(ESlateSizeRule::Automatic);
	TextSlot->SetSize(Auto);

	Face->SetContent(VBox);
	Frame->SetContent(Face);
	CardSizer->SetContent(Frame);
	return CardSizer;
}

// -----------------------------------------------------------
// 从卡牌数据构建完整卡面（奖励/坊市等）
// -----------------------------------------------------------

UWidget* AAscendPlayerController::MakeCardContentFromData(UObject* Outer, const FCardData& CardData, bool bUpgraded, float Scale)
{
	const float S = Scale;

	USizeBox* CardSizer = NewObject<USizeBox>(Outer);
	CardSizer->SetWidthOverride(162.f * S);
	CardSizer->SetHeightOverride(194.f * S);

	const FLinearColor TypeCol = FAscendUIStyle::CardTypeColor(CardData.Type);

	UBorder* Frame = NewObject<UBorder>(CardSizer);
	Frame->SetBrushColor(TypeCol * 0.55f + FLinearColor(0.08f, 0.07f, 0.05f) * 0.45f);
	Frame->SetPadding(FMargin(2.f * S));

	UBorder* Face = NewObject<UBorder>(Frame);
	Face->SetBrushColor(FLinearColor(0.30f, 0.26f, 0.22f));

	UVerticalBox* VBox = NewObject<UVerticalBox>(Face);

	UBorder* ArtArea = NewObject<UBorder>(VBox);
	ArtArea->SetBrushColor(TypeCol * 0.35f + FLinearColor(0.13f, 0.11f, 0.09f) * 0.65f);
	UVerticalBoxSlot* ArtSlot = VBox->AddChildToVerticalBox(ArtArea);
	FSlateChildSize FillAll(ESlateSizeRule::Fill);
	FillAll.Value = 1.f;
	ArtSlot->SetSize(FillAll);

	UOverlay* ArtOvl = NewObject<UOverlay>(ArtArea);
	if (UImage* Img = FAscendArt::MakeImage(ArtOvl, CardData.ArtPath))
	{
		UScaleBox* ImgScale = NewObject<UScaleBox>(ArtOvl);
		ImgScale->SetStretch(EStretch::ScaleToFill);
		ImgScale->SetContent(Img);
		UOverlaySlot* ImgSlot = ArtOvl->AddChildToOverlay(ImgScale);
		ImgSlot->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Fill);
		ImgSlot->SetVerticalAlignment(EVerticalAlignment::VAlign_Fill);
	}

	if (CardData.bRetain || CardData.bExhaust)
	{
		UTextBlock* Badge = FAscendUIStyle::MakeText(ArtOvl,
			CardData.bRetain ? TEXT("留") : TEXT("耗"), FMath::RoundToInt(11.f * S),
			CardData.bRetain ? FAscendUIStyle::JadeGreen() : FAscendUIStyle::GoldYellow());
		Badge->SetJustification(ETextJustify::Left);
		Badge->SetMargin(FMargin(3.f * S, 2.f * S, 0.f, 0.f));
		UOverlaySlot* BadgeSlot = ArtOvl->AddChildToOverlay(Badge);
		BadgeSlot->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Left);
		BadgeSlot->SetVerticalAlignment(EVerticalAlignment::VAlign_Top);
	}
	ArtArea->SetContent(ArtOvl);

	UBorder* TextBg = NewObject<UBorder>(VBox);
	TextBg->SetBrushColor(FLinearColor(0.22f, 0.19f, 0.16f, 0.92f));

	UVerticalBox* TextBox = NewObject<UVerticalBox>(TextBg);

	UHorizontalBox* NameRow = NewObject<UHorizontalBox>(TextBox);
	FString DisplayName = bUpgraded ? (CardData.Name + TEXT("+")) : CardData.Name;
	UTextBlock* NameT = FAscendUIStyle::MakeText(NameRow, DisplayName,
		FMath::RoundToInt(13.f * S), FAscendUIStyle::PaperWhite());
	NameT->SetJustification(ETextJustify::Left);
	NameRow->AddChildToHorizontalBox(NameT)->SetPadding(FMargin(4.f * S, 1.f * S, 0.f, 0.f));

	NameRow->AddChildToHorizontalBox(NewObject<USpacer>(NameRow))->SetSize(FSlateChildSize(ESlateSizeRule::Fill));

	USizeBox* GemSizer = NewObject<USizeBox>(NameRow);
	GemSizer->SetWidthOverride(22.f * S);
	GemSizer->SetHeightOverride(22.f * S);
	UBorder* CostGem = NewObject<UBorder>(GemSizer);
	CostGem->SetBrushColor(FLinearColor(0.25f, 0.45f, 0.80f));
	UTextBlock* CostT = FAscendUIStyle::MakeText(CostGem, FString::FromInt(CardData.Cost),
		FMath::RoundToInt(13.f * S), FAscendUIStyle::PaperWhite());
	CostT->SetJustification(ETextJustify::Center);
	CostGem->SetContent(CostT);
	GemSizer->SetContent(CostGem);
	NameRow->AddChildToHorizontalBox(GemSizer)->SetPadding(FMargin(0.f, 1.f * S, 4.f * S, 0.f));

	TextBox->AddChildToVerticalBox(NameRow);

	FString DisplayDesc = bUpgraded && !CardData.UpgradedDescription.IsEmpty()
		? CardData.UpgradedDescription : CardData.Description;
	UTextBlock* DescT = FAscendUIStyle::MakeText(TextBox, DisplayDesc,
		FMath::RoundToInt(10.f * S), FAscendUIStyle::PaperWhite());
	DescT->SetAutoWrapText(true);
	DescT->SetJustification(ETextJustify::Center);
	TextBox->AddChildToVerticalBox(DescT)->SetPadding(FMargin(4.f * S, 2.f * S));

	if (!CardData.Flavor.IsEmpty())
	{
		UTextBlock* FlavorT = FAscendUIStyle::MakeText(TextBox, CardData.Flavor,
			FMath::RoundToInt(8.f * S), FSlateColor(FLinearColor(0.40f, 0.38f, 0.34f)));
		FlavorT->SetAutoWrapText(true);
		FlavorT->SetJustification(ETextJustify::Center);
		TextBox->AddChildToVerticalBox(FlavorT)->SetPadding(FMargin(4.f * S, 1.f * S, 4.f * S, 2.f * S));
	}

	TextBg->SetContent(TextBox);
	VBox->AddChildToVerticalBox(TextBg)->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));

	Face->SetContent(VBox);
	Frame->SetContent(Face);
	CardSizer->SetContent(Frame);
	return CardSizer;
}

// -----------------------------------------------------------
// 悬停大卡预览（原生尺寸绘制，字体不模糊）
// -----------------------------------------------------------

void AAscendPlayerController::ShowCardPreview(int32 CardIndex)
{
	if (!AnimCanvas || !Combat || !Combat->Hand.IsValidIndex(CardIndex)) return;
	if (bIsDraggingCard) return; // 拖拽时不弹预览

	HideCardPreview();

	const float PreviewScale = 1.6f;
	const float CardW = 132.f * PreviewScale;
	const float CardH = 194.f * PreviewScale;

	CardPreviewWidget = NewObject<UBorder>(AnimCanvas);
	CardPreviewWidget->SetBrushColor(FLinearColor(0.f, 0.f, 0.f, 0.55f));
	CardPreviewWidget->SetPadding(FMargin(6.f));
	CardPreviewWidget->SetContent(MakeCardContent(CardPreviewWidget, CardIndex, true, PreviewScale));
	CardPreviewWidget->SetVisibility(ESlateVisibility::HitTestInvisible);

	const FVector2D VP = GetViewportSize();
	const float X = (VP.X - CardW) * 0.5f - 12.f;
	const float Y = FMath::Max(16.f, VP.Y - 230.f - CardH - 16.f);
	if (UCanvasPanelSlot* Slot = AnimCanvas->AddChildToCanvas(CardPreviewWidget))
	{
		Slot->SetPosition(FVector2D(X, Y));
		Slot->SetAutoSize(true);
		Slot->SetZOrder(9500);
	}
}

void AAscendPlayerController::HideCardPreview()
{
	if (CardPreviewWidget)
	{
		CardPreviewWidget->RemoveFromParent();
		CardPreviewWidget = nullptr;
	}
}

void AAscendPlayerController::BuildEnemyCardWidget(UVerticalBox* EBox, int32 EnemyIndex, bool bIsLocked)
{
	if (!Combat || !Combat->Enemies.IsValidIndex(EnemyIndex)) return;
	const FEnemyCombatant& E = Combat->Enemies[EnemyIndex];
	const bool bAlive = E.State.IsAlive();

	USizeBox* Sizer = NewObject<USizeBox>(EBox);
	Sizer->SetWidthOverride(150.f);
	Sizer->SetHeightOverride(210.f);

	UBorder* Frame = NewObject<UBorder>(Sizer);
	Frame->SetBrushColor(bIsLocked ? FLinearColor(0.90f, 0.40f, 0.20f) : FLinearColor(0.14f, 0.11f, 0.08f));
	Frame->SetPadding(FMargin(2.f));

	UBorder* Face = NewObject<UBorder>(Frame);
	Face->SetBrushColor(bAlive ? FLinearColor(0.28f, 0.20f, 0.18f) : FLinearColor(0.15f, 0.13f, 0.12f));

	UVerticalBox* V = NewObject<UVerticalBox>(Face);

	// Art area (fill)——立绘 + 死亡时显示「已击杀」
	UBorder* Art = NewObject<UBorder>(V);
	Art->SetBrushColor(FLinearColor(0.22f, 0.18f, 0.15f));
	UVerticalBoxSlot* ArtS = V->AddChildToVerticalBox(Art);
	FSlateChildSize Fill(ESlateSizeRule::Fill);
	Fill.Value = 1.f;
	ArtS->SetSize(Fill);

	UOverlay* EArtOvl = NewObject<UOverlay>(Art);
	if (UImage* EImg = FAscendArt::MakeImage(EArtOvl, E.Data.ArtPath))
	{
		UScaleBox* EScale = NewObject<UScaleBox>(EArtOvl);
		EScale->SetStretch(EStretch::ScaleToFill);
		EScale->SetContent(EImg);
		UOverlaySlot* EImgSlot = EArtOvl->AddChildToOverlay(EScale);
		EImgSlot->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Fill);
		EImgSlot->SetVerticalAlignment(EVerticalAlignment::VAlign_Fill);
		// 死亡时立绘压暗
		if (!bAlive) EImg->SetRenderOpacity(0.25f);
	}
	if (!bAlive)
	{
		UTextBlock* DeadT = FAscendUIStyle::MakeText(EArtOvl, TEXT("已击杀"), 15, FAscendUIStyle::DimGray());
		DeadT->SetJustification(ETextJustify::Center);
		UOverlaySlot* DeadSlot = EArtOvl->AddChildToOverlay(DeadT);
		DeadSlot->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Center);
		DeadSlot->SetVerticalAlignment(EVerticalAlignment::VAlign_Center);
	}
	Art->SetContent(EArtOvl);

	// Text area
	UBorder* TextBg = NewObject<UBorder>(V);
	TextBg->SetBrushColor(FLinearColor(0.20f, 0.17f, 0.14f, 0.92f));
	UVerticalBox* TV = NewObject<UVerticalBox>(TextBg);

	UTextBlock* NameT = FAscendUIStyle::MakeText(TV, E.State.Name, 14,
		bAlive ? FAscendUIStyle::BloodRed() : FAscendUIStyle::DimGray());
	NameT->SetJustification(ETextJustify::Center);
	TV->AddChildToVerticalBox(NameT);

	UProgressBar* HPBar = NewObject<UProgressBar>(TV);
	HPBar->SetPercent(bAlive ? (float)E.State.HP / E.State.MaxHP : 0.f);
	HPBar->SetFillColorAndOpacity(FLinearColor(0.75f, 0.22f, 0.18f));
	TV->AddChildToVerticalBox(HPBar);

	UTextBlock* HPT = FAscendUIStyle::MakeText(TV,
		FString::Printf(TEXT("%d/%d  罡气 %d"), E.State.HP, E.State.MaxHP, E.State.Block),
		12, FAscendUIStyle::PaperWhite());
	HPT->SetJustification(ETextJustify::Center);
	TV->AddChildToVerticalBox(HPT);

	if (bAlive)
	{
		// 意图徽标：色块 + 单字图标 + 数值
		FLinearColor BadgeCol;
		FString Glyph = TEXT("?");
		FString ValStr;
		if (E.CurrentIntent.Action == TEXT("attack"))
		{
			BadgeCol = FLinearColor(0.62f, 0.16f, 0.12f); Glyph = TEXT("攻");
			ValStr = FString::FromInt(E.CurrentIntent.Value);
		}
		else if (E.CurrentIntent.Action == TEXT("attack_multi"))
		{
			BadgeCol = FLinearColor(0.72f, 0.32f, 0.10f); Glyph = TEXT("连");
			ValStr = FString::Printf(TEXT("%dx%d"), E.CurrentIntent.Value, E.CurrentIntent.Times);
		}
		else if (E.CurrentIntent.Action == TEXT("defend"))
		{
			BadgeCol = FLinearColor(0.22f, 0.42f, 0.65f); Glyph = TEXT("御");
			ValStr = FString::FromInt(E.CurrentIntent.Value);
		}
		else if (E.CurrentIntent.Action == TEXT("buff"))
		{
			BadgeCol = FLinearColor(0.25f, 0.55f, 0.35f); Glyph = TEXT("强");
		}
		else if (E.CurrentIntent.Action == TEXT("debuff"))
		{
			BadgeCol = FLinearColor(0.48f, 0.30f, 0.62f); Glyph = TEXT("弱");
		}
		else
		{
			BadgeCol = FLinearColor(0.35f, 0.35f, 0.35f);
		}

		UHorizontalBox* IntentRow = NewObject<UHorizontalBox>(TV);
		IntentRow->AddChildToHorizontalBox(NewObject<USpacer>(TV))->SetSize(FSlateChildSize(ESlateSizeRule::Fill));

		USizeBox* BadgeSize = NewObject<USizeBox>(IntentRow);
		BadgeSize->SetWidthOverride(18.f);
		BadgeSize->SetHeightOverride(18.f);
		UBorder* Badge = NewObject<UBorder>(BadgeSize);
		Badge->SetBrushColor(BadgeCol);
		UTextBlock* GlyphT = FAscendUIStyle::MakeText(Badge, Glyph, 11, FAscendUIStyle::PaperWhite());
		GlyphT->SetJustification(ETextJustify::Center);
		Badge->SetContent(GlyphT);
		BadgeSize->SetContent(Badge);
		IntentRow->AddChildToHorizontalBox(BadgeSize);

		if (!ValStr.IsEmpty())
		{
			UTextBlock* ValT = FAscendUIStyle::MakeText(IntentRow, ValStr, 12, FAscendUIStyle::GoldYellow());
			UHorizontalBoxSlot* ValSlot = IntentRow->AddChildToHorizontalBox(ValT);
			ValSlot->SetPadding(FMargin(4.f, 1.f, 0.f, 0.f));
		}
		IntentRow->AddChildToHorizontalBox(NewObject<USpacer>(TV))->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		TV->AddChildToVerticalBox(IntentRow)->SetPadding(FMargin(0.f, 2.f, 0.f, 1.f));
	}

	if (UHorizontalBox* StatusRow = BuildStatusRow(TV, E.State))
	{
		TV->AddChildToVerticalBox(StatusRow)->SetPadding(FMargin(0.f, 2.f));
	}

	// 特殊机制说明（小字，灰金色）
	if (!E.Data.AbilityDesc.IsEmpty())
	{
		UTextBlock* AbilT = FAscendUIStyle::MakeText(TV, E.Data.AbilityDesc, 8,
			FSlateColor(FLinearColor(0.55f, 0.48f, 0.36f)));
		AbilT->SetAutoWrapText(true);
		AbilT->SetJustification(ETextJustify::Center);
		UVerticalBoxSlot* AbilSlot = TV->AddChildToVerticalBox(AbilT);
		AbilSlot->SetPadding(FMargin(2.f, 1.f, 2.f, 1.f));
	}

	TextBg->SetContent(TV);
	V->AddChildToVerticalBox(TextBg);
	Face->SetContent(V);
	Frame->SetContent(Face);
	Sizer->SetContent(Frame);
	UVerticalBoxSlot* ESlot = EBox->AddChildToVerticalBox(Sizer);
	ESlot->SetPadding(FMargin(4.f));
}

UHorizontalBox* AAscendPlayerController::BuildStatusRow(UObject* Outer, const FCombatantState& State, int32 FontSize)
{
	static const TMap<FString, TPair<FString, FLinearColor>> StatusStyle = {
		{TEXT("strength"),   {TEXT("力"), FLinearColor(0.25f, 0.60f, 0.30f)}},
		{TEXT("burn"),       {TEXT("灼"), FLinearColor(0.75f, 0.20f, 0.12f)}},
		{TEXT("poison"),     {TEXT("毒"), FLinearColor(0.55f, 0.15f, 0.55f)}},
		{TEXT("weak"),       {TEXT("弱"), FLinearColor(0.45f, 0.40f, 0.35f)}},
		{TEXT("vulnerable"), {TEXT("伤"), FLinearColor(0.70f, 0.50f, 0.10f)}},
	};
	if (State.Statuses.IsEmpty()) return nullptr;
	UHorizontalBox* Row = NewObject<UHorizontalBox>(Outer);
	for (const FStatusInstance& S : State.Statuses)
	{
		const auto* Style = StatusStyle.Find(S.Id);
		FString Label = Style ? FString::Printf(TEXT("%s%d"), *Style->Key, S.Stacks) : FString::Printf(TEXT("%s%d"), *S.Id, S.Stacks);
		FLinearColor Col = Style ? Style->Value : FLinearColor(0.50f, 0.50f, 0.50f);
		UBorder* Badge = NewObject<UBorder>(Row);
		Badge->SetBrushColor(Col);
		Badge->SetPadding(FMargin(6.f, 2.f));
		UTextBlock* T = FAscendUIStyle::MakeText(Badge, Label, FontSize, FAscendUIStyle::PaperWhite());
		Badge->SetContent(T);
		UHorizontalBoxSlot* Slot = Row->AddChildToHorizontalBox(Badge);
		Slot->SetPadding(FMargin(0.f, 0.f, 6.f, 0.f));
		Slot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
	}
	return Row;
}

void AAscendPlayerController::HandleCardPressed(int32 CardIndex)
{
	if (!Combat || !Combat->bCombatActive) return;
	if (!Combat->Hand.IsValidIndex(CardIndex)) return;

	UE_LOG(LogTemp, Display, TEXT("[PRESS] card=%d bIsDragging=%d"), CardIndex, bIsDraggingCard);

	// Already dragging → cancel current drag (snap back)
	if (bIsDraggingCard)
	{
		EndCardDrag();
		return;
	}

	HideCardPreview();

	if (Combat->GetEffectiveCost(Combat->Hand[CardIndex]) > Combat->Spirit) return;

	bool bNeedsTarget = false;
	for (const FCardEffect& E : Combat->Hand[CardIndex].GetEffects())
	{
		if (E.Target == TEXT("enemy")) { bNeedsTarget = true; break; }
	}

	bIsDraggingCard = true;
	DragCardIndex = CardIndex;
	bDragNeedsTarget = bNeedsTarget;
	DragTargetEnemy = -1;
	DraggedCardWidget = nullptr;
	GetMousePosition(DragStartMousePos.X, DragStartMousePos.Y);
}

void AAscendPlayerController::UpdateCardDrag()
{
	if (!AnimCanvas || !GetWorld()) return;

	float MxPhys, MyPhys;
	GetMousePosition(MxPhys, MyPhys);
	FVector2D MousePos = AnimCanvas->GetTickSpaceGeometry().AbsoluteToLocal(
		FSlateApplication::Get().GetCursorPos());

	if (!DraggedCardWidget)
	{
		int32 NCards = Combat->Hand.Num();
		FVector2D VP = GetViewportSize();
		FGeometry AG = AnimCanvas->GetTickSpaceGeometry();
		FVector2D CAbs = AG.GetAbsolutePosition();

		UE_LOG(LogTemp, Display, TEXT("[DRAG] Canvas local=%.0fx%.0f abs=%.0f,%.0f absSize=%.0fx%.0f"),
			AG.GetLocalSize().X, AG.GetLocalSize().Y,
			CAbs.X, CAbs.Y,
			AG.GetAbsoluteSize().X, AG.GetAbsoluteSize().Y);

		float SlotW = 140.f;
		float TotalW = NCards * SlotW;
		FVector2D HandVP((VP.X - TotalW) * 0.5f + DragCardIndex * SlotW, VP.Y - 160.f);
		FVector2D HandLocal = AG.AbsoluteToLocal(HandVP + CAbs);

		UE_LOG(LogTemp, Display, TEXT("[DRAG] VP=%.0fx%.0f NCards=%d HandVP=%.0f,%.0f HandLocal=%.0f,%.0f"),
			VP.X, VP.Y, NCards, HandVP.X, HandVP.Y, HandLocal.X, HandLocal.Y);

		UButton* Ghost = NewObject<UButton>(AnimCanvas);
		BuildCardWidget(Ghost, DragCardIndex, true);

		// Wrap in highlight border
		UBorder* HBorder = NewObject<UBorder>(AnimCanvas);
		HBorder->SetBrushColor(FLinearColor(0.f, 0.f, 0.f, 0.f));
		HBorder->SetPadding(FMargin(3.f));
		HBorder->SetContent(Ghost);
		GhostHighlightBorder = HBorder;
		DraggedCardWidget = HBorder;

		UCanvasPanelSlot* Slot = AnimCanvas->AddChildToCanvas(HBorder);
		Slot->SetPosition(HandLocal);
		Slot->SetAutoSize(true);
		HBorder->SetRenderScale(FVector2D(1.12f, 1.12f));
		HBorder->SetRenderTranslation(FVector2D(0.f, -35.f));
		HBorder->SetRenderOpacity(0.92f);

		// Hide original card
		if (HandCardButtons.IsValidIndex(DragCardIndex) && HandCardButtons[DragCardIndex].IsValid())
			HandCardButtons[DragCardIndex]->SetRenderOpacity(0.f);
	}
	else
	{
		if (UCanvasPanelSlot* Slot = Cast<UCanvasPanelSlot>(DraggedCardWidget->Slot))
		{
			Slot->SetPosition(MousePos - FVector2D(69.f, 100.f));
		}
		DraggedCardWidget->SetRenderScale(FVector2D(1.12f, 1.12f));
		DraggedCardWidget->SetRenderTranslation(FVector2D(0.f, -35.f));

		// Play zone detection
		FGeometry AG = AnimCanvas->GetTickSpaceGeometry();
		FVector2D VP = GetViewportSize();
		float HandRowCenterVY = VP.Y - 160.f;
		FVector2D HandZoneVP(0.f, HandRowCenterVY - 10.f);
		float HandZoneLocal = AG.AbsoluteToLocal(HandZoneVP + AG.GetAbsolutePosition()).Y;
		float CardCenterY = MousePos.Y;
		bool bInPlayZone = (CardCenterY < HandZoneLocal);

		// Highlight border
		if (GhostHighlightBorder)
		{
			GhostHighlightBorder->SetBrushColor(
				bInPlayZone ? FLinearColor(0.95f, 0.70f, 0.10f, 0.50f) : FLinearColor(0.f, 0.f, 0.f, 0.f));
		}

		// Attack line only when in play zone
		if (bDragNeedsTarget)
		{
			if (bInPlayZone)
			{
				int32 Target = GetDragTargetEnemy();
				DragTargetEnemy = Target;
				if (Target >= 0) ShowAttackLine(Target);
				else HideAttackLine();
			}
			else
			{
				HideAttackLine();
				DragTargetEnemy = -1;
			}
		}
		else
		{
			DragTargetEnemy = 0;
		}
	}
}

void AAscendPlayerController::EndCardDrag()
{
	if (!bIsDraggingCard) return;
	bIsDraggingCard = false;
	HideAttackLine();

	// Restore original card button opacity
	if (HandCardButtons.IsValidIndex(DragCardIndex) && HandCardButtons[DragCardIndex].IsValid())
	{
		HandCardButtons[DragCardIndex]->SetRenderOpacity(1.f);
	}

	bool bPlayCard = false;

	if (DraggedCardWidget)
	{
		float CardCenterY = 0.f;
		if (UCanvasPanelSlot* S = Cast<UCanvasPanelSlot>(DraggedCardWidget->Slot))
		{
			CardCenterY = S->GetPosition().Y + 100.f;
		}
		// Outside hand zone → play
		FVector2D VP = GetViewportSize();
		FGeometry AG = AnimCanvas->GetTickSpaceGeometry();
		float HandRowCenterVY = VP.Y - 160.f;
		FVector2D HandZoneVP(0.f, HandRowCenterVY - 10.f);
		float HandZoneLocal = AG.AbsoluteToLocal(HandZoneVP + AG.GetAbsolutePosition()).Y;
		bPlayCard = (CardCenterY < HandZoneLocal);

		DraggedCardWidget->RemoveFromParent();
		DraggedCardWidget = nullptr;
		GhostHighlightBorder = nullptr;
	}
	else
	{
		bPlayCard = false;
	}

	if (bPlayCard && DragCardIndex >= 0 && Combat && Combat->Hand.IsValidIndex(DragCardIndex))
	{
		if (bDragNeedsTarget && DragTargetEnemy < 0) bPlayCard = false;

		if (bPlayCard)
		{
			int32 PlayTarget = bDragNeedsTarget ? DragTargetEnemy : 0;
			int32 LogN_before = CombatLogLines.Num();
			UE_LOG(LogTemp, Display, TEXT("[PLAY] card=%d target=%d bNeedsTarget=%d logN=%d"),
				DragCardIndex, PlayTarget, bDragNeedsTarget, LogN_before);
			Combat->PlayCard(DragCardIndex, PlayTarget);
			UE_LOG(LogTemp, Display, TEXT("[PLAY] done logN=%d"), CombatLogLines.Num());
			int32 LogN_after = CombatLogLines.Num();
			for (int32 li = LogN_before; li < LogN_after; ++li)
				UE_LOG(LogTemp, Display, TEXT("[PLAYLOG] %s"), *CombatLogLines[li]);
			DragCardIndex = -1;
			DragTargetEnemy = -1;
			if (Combat->IsCombatOver())
			{
				bCombatEndPending = true;
				bInputLocked = true;
				TriggerCombatAnimations(TEXT("card"));
				if (GetWorld())
				{
					GetWorld()->GetTimerManager().SetTimer(CombatEndTimer, this,
						&AAscendPlayerController::FinishCombatDelayed, 0.6f, false);
				}
				return;
			}
			RefreshCombatPanel();
			TriggerCombatAnimations(TEXT("card"));
			return;
		}
	}

	// Snap back animation: brief translate to hand position then show panel
	DragCardIndex = -1;
	DragTargetEnemy = -1;
	RefreshCombatPanel();
}

int32 AAscendPlayerController::GetDragTargetEnemy()
{
	if (LockedTargetIndex >= 0 &&
		Combat->Enemies.IsValidIndex(LockedTargetIndex) &&
		Combat->Enemies[LockedTargetIndex].State.IsAlive())
		return LockedTargetIndex;
	for (int32 i = 0; i < Combat->Enemies.Num(); ++i)
		if (Combat->Enemies[i].State.IsAlive()) return i;
	return -1;
}

void AAscendPlayerController::SetLockedTarget(int32 EnemyIndex)
{
	if (LockedTargetIndex == EnemyIndex)
		LockedTargetIndex = -1; // click again to unlock
	else
		LockedTargetIndex = EnemyIndex;
}

void AAscendPlayerController::ShowAttackLine(int32 EnemyIndex)
{
	HideAttackLine();
	if (!AnimCanvas || !DraggedCardWidget) return;

	FVector2D CardPos(0.f);
	if (UCanvasPanelSlot* CS = Cast<UCanvasPanelSlot>(DraggedCardWidget->Slot))
		CardPos = CS->GetPosition() + FVector2D(69.f, 100.f);
	FVector2D EnemyPos = GetEnemyScreenPos(EnemyIndex);
	FVector2D Delta = EnemyPos - CardPos;
	float Len = Delta.Size();
	if (Len < 1.f) return;

	// Quadratic Bezier control point
	FVector2D Mid = (CardPos + EnemyPos) * 0.5f;
	FVector2D Perp(-Delta.Y, Delta.X);
	Perp.Normalize();
	FVector2D P0 = CardPos;
	FVector2D P1 = Mid + Perp * 40.f;
	FVector2D P2 = EnemyPos;

	auto MakeSeg = [&](FVector2D From, FVector2D To, FLinearColor Color, float Thick, FVector2D Offset)
	{
		FVector2D D = To - From;
		float L = D.Size();
		if (L < 0.5f) return;
		float A = FMath::RadiansToDegrees(FMath::Atan2(D.Y, D.X));
		UBorder* Seg = NewObject<UBorder>(AnimCanvas);
		Seg->SetBrushColor(Color);
		UCanvasPanelSlot* S = AnimCanvas->AddChildToCanvas(Seg);
		S->SetPosition((From + To) * 0.5f - FVector2D(0.f, Thick * 0.5f) + Offset);
		S->SetSize(FVector2D(L, Thick));
		S->SetAutoSize(false);
		S->SetZOrder(9999);
		Seg->SetRenderTransformAngle(A);
		AttackLineSegments.Add(Seg);
		ActiveAnimations.Add(Seg);
	};

	auto Bezier = [&](float t) -> FVector2D
	{
		float u = 1.f - t;
		return u*u * P0 + 2.f*u*t * P1 + t*t * P2;
	};

	const int32 NumSegs = 24;
	FLinearColor ShadowColor(0.35f, 0.05f, 0.02f, 0.40f);
	FLinearColor MainColor(0.95f, 0.30f, 0.12f, 0.85f);
	FVector2D ShadowOff(2.f, 2.f);

	// Shadow curve
	for (int32 i = 0; i < NumSegs; ++i)
	{
		float t0 = (float)i / NumSegs;
		float t1 = (float)(i + 1) / NumSegs;
		MakeSeg(Bezier(t0), Bezier(t1), ShadowColor, 14.f, ShadowOff);
	}

	// Main curve
	for (int32 i = 0; i < NumSegs; ++i)
	{
		float t0 = (float)i / NumSegs;
		float t1 = (float)(i + 1) / NumSegs;
		MakeSeg(Bezier(t0), Bezier(t1), MainColor, 10.f, FVector2D::ZeroVector);
	}

	// Arrowhead: short spine + two wings forming a crisp arrow tip
	FLinearColor ArrowColor(0.95f, 0.30f, 0.12f, 0.95f);
	{
		FVector2D Tangent = (P2 - P1) * 2.f;
		FVector2D AD = Tangent.GetSafeNormal();
		float SpineLen = 18.f;
		float SpineThick = 8.f;
		float WingLen = 30.f;
		float WingThick = 7.f;
		FVector2D ArrowTip = P2;
		// Spine
		MakeSeg(ArrowTip - AD * SpineLen, ArrowTip, ArrowColor, SpineThick, FVector2D::ZeroVector);
		// Wings
		for (float Sign : {-1.f, 1.f})
		{
			FVector2D Wing = AD.GetRotated(180.f + Sign * 30.f).GetSafeNormal();
			MakeSeg(ArrowTip, ArrowTip + Wing * WingLen, ArrowColor, WingThick, FVector2D::ZeroVector);
		}
	}

	// Elastic spring animation
	const float StartTime = GetWorld()->GetTimeSeconds();
	AttackLineTimerHandle = MakeShared<FTimerHandle>();
	GetWorld()->GetTimerManager().SetTimer(*AttackLineTimerHandle,
		FTimerDelegate::CreateWeakLambda(this, [this, StartTime]()
		{
			const float Elapsed = GetWorld()->GetTimeSeconds() - StartTime;
			const float T = FMath::Min(Elapsed / 0.3f, 1.f);
			float Scale = 1.f + 0.15f * FMath::Sin(T * PI * 3.f) * (1.f - T);
			Scale = FMath::Clamp(Scale, 0.1f, 1.3f);
			for (UBorder* Seg : AttackLineSegments)
			{
				if (Seg && Seg->IsValidLowLevel())
					Seg->SetRenderScale(FVector2D(Scale, 1.f));
			}
			if (T >= 1.f)
			{
				for (UBorder* Seg : AttackLineSegments)
				{
					if (Seg && Seg->IsValidLowLevel())
						Seg->SetRenderScale(FVector2D(1.f, 1.f));
				}
				if (UWorld* W = GetWorld()) W->GetTimerManager().ClearTimer(*AttackLineTimerHandle);
			}
		}), 0.033f, true);
}

void AAscendPlayerController::HideAttackLine()
{
	if (AttackLineTimerHandle.IsValid() && GetWorld())
	{
		GetWorld()->GetTimerManager().ClearTimer(*AttackLineTimerHandle);
	}
	for (UBorder* Seg : AttackLineSegments)
	{
		if (Seg && Seg->IsValidLowLevel())
		{
			ActiveAnimations.Remove(Seg);
			Seg->RemoveFromParent();
		}
	}
	AttackLineSegments.Empty();
}

// -----------------------------------------------------------
// 战斗动画
// -----------------------------------------------------------

void AAscendPlayerController::ClearAnimations()
{
	UE_LOG(LogTemp, Display, TEXT("[DIAG] ClearAnimations start N=%d timers=%d"), ActiveAnimations.Num(), AnimTimerHandles.Num());

	// 统一清理所有动画定时器，杜绝泄漏
	if (UWorld* W = GetWorld())
	{
		for (FTimerHandle& H : AnimTimerHandles)
			W->GetTimerManager().ClearTimer(H);
	}
	AnimTimerHandles.Empty();
	if (AttackLineTimerHandle.IsValid() && GetWorld())
	{
		GetWorld()->GetTimerManager().ClearTimer(*AttackLineTimerHandle);
	}
	// 注意：不清理 Proxies —— 当前界面按钮的代理依赖此数组保持存活

	for (UWidget* W : ActiveAnimations)
	{
		if (W && W->IsValidLowLevel()) W->RemoveFromParent();
	}
	ActiveAnimations.Empty();
	if (ScreenHost) ScreenHost->SetRenderTransform(FWidgetTransform());
	HideCardPreview();
	UE_LOG(LogTemp, Display, TEXT("[DIAG] ClearAnimations done"));
}

void AAscendPlayerController::SpawnFloatingText(const FString& Text, FLinearColor Color, float X, float Y, float Duration)
{
	if (!AnimCanvas || !GetWorld()) return;

	UTextBlock* TB = FAscendUIStyle::MakeText(AnimCanvas, Text, 28, Color);
	UCanvasPanelSlot* Slot = AnimCanvas->AddChildToCanvas(TB);
	Slot->SetPosition(FVector2D(X, Y));
	Slot->SetAutoSize(true);
	TB->SetRenderOpacity(1.f);
	ActiveAnimations.Add(TB);

	const float StartTime = GetWorld()->GetTimeSeconds();
	TWeakObjectPtr<UTextBlock> WeakTB = TB;
	FTimerHandle Handle;
	GetWorld()->GetTimerManager().SetTimer(Handle,
		FTimerDelegate::CreateWeakLambda(this, [this, WeakTB, Handle, StartTime, Duration]() mutable
		{
			if (!WeakTB.IsValid())
			{
				if (UWorld* W = GetWorld()) W->GetTimerManager().ClearTimer(Handle);
				return;
			}
			const float Elapsed = GetWorld()->GetTimeSeconds() - StartTime;
			if (Elapsed >= Duration)
			{
				WeakTB->RemoveFromParent();
				ActiveAnimations.Remove(WeakTB.Get());
				if (UWorld* W = GetWorld()) W->GetTimerManager().ClearTimer(Handle);
				return;
			}
			const float T = Elapsed / Duration;
			WeakTB->SetRenderTranslation(FVector2D(0, -T * 50.f));
			WeakTB->SetRenderOpacity(1.f - T);
		}), 0.033f, true);
	AnimTimerHandles.Add(Handle);
}

void AAscendPlayerController::SpawnSlashEffect(float X, float Y)
{
	if (!AnimCanvas || !GetWorld()) return;

	for (float Angle : {35.f, -35.f})
	{
		UBorder* Slash = NewObject<UBorder>(AnimCanvas);
		Slash->SetBrushColor(FLinearColor(1.f, 1.f, 0.9f, 0.9f));
		UCanvasPanelSlot* Slot = AnimCanvas->AddChildToCanvas(Slash);
		Slot->SetPosition(FVector2D(X - 40.f, Y - 15.f));
		Slot->SetSize(FVector2D(100.f, 3.f));
		Slot->SetAutoSize(false);
		Slash->SetRenderTransformAngle(Angle);
		Slash->SetRenderScale(FVector2D(0.f, 1.f));
		ActiveAnimations.Add(Slash);

		const float StartTime = GetWorld()->GetTimeSeconds();
		TWeakObjectPtr<UBorder> WeakSlash = Slash;
		FTimerHandle Handle;
		GetWorld()->GetTimerManager().SetTimer(Handle,
			FTimerDelegate::CreateWeakLambda(this, [this, WeakSlash, Handle, StartTime]() mutable
			{
				if (!WeakSlash.IsValid())
				{
					if (UWorld* W = GetWorld()) W->GetTimerManager().ClearTimer(Handle);
					return;
				}
				const float Elapsed = GetWorld()->GetTimeSeconds() - StartTime;
				const float T = FMath::Min(Elapsed / 0.25f, 1.f);
				float ScaleX = FMath::Sin(T * PI) * 1.4f;
				WeakSlash->SetRenderScale(FVector2D(ScaleX, 1.f));
				WeakSlash->SetRenderTranslation(FVector2D(T * 30.f, 0.f));
				WeakSlash->SetRenderOpacity(1.f - T);
				if (T >= 1.f)
				{
					WeakSlash->RemoveFromParent();
					ActiveAnimations.Remove(WeakSlash.Get());
					if (UWorld* W = GetWorld()) W->GetTimerManager().ClearTimer(Handle);
				}
			}), 0.033f, true);
		AnimTimerHandles.Add(Handle);
	}
}

void AAscendPlayerController::AnimateScreenShake(float Intensity, float Duration)
{
	if (!ScreenHost || !GetWorld()) return;

	const FWidgetTransform OrigTransform = ScreenHost->GetRenderTransform();
	const float StartTime = GetWorld()->GetTimeSeconds();
	TWeakObjectPtr<UBorder> WeakHost = ScreenHost;
	FTimerHandle Handle;
	GetWorld()->GetTimerManager().SetTimer(Handle,
		FTimerDelegate::CreateWeakLambda(this, [this, WeakHost, Handle, OrigTransform, StartTime, Intensity, Duration]() mutable
		{
			if (!WeakHost.IsValid())
			{
				if (UWorld* W = GetWorld()) W->GetTimerManager().ClearTimer(Handle);
				return;
			}
			const float Elapsed = GetWorld()->GetTimeSeconds() - StartTime;
			if (Elapsed >= Duration)
			{
				WeakHost->SetRenderTransform(OrigTransform);
				if (UWorld* W = GetWorld()) W->GetTimerManager().ClearTimer(Handle);
				return;
			}
			const float Amp = Intensity * (1.f - Elapsed / Duration);
			WeakHost->SetRenderTranslation(FVector2D(
				FMath::FRandRange(-Amp, Amp), FMath::FRandRange(-Amp, Amp)));
		}), 0.033f, true);
	AnimTimerHandles.Add(Handle);
}

void AAscendPlayerController::AnimateColorFlash(FLinearColor Color, float Duration)
{
	if (!AnimCanvas || !GetWorld()) return;

	UBorder* Flash = NewObject<UBorder>(AnimCanvas);
	Flash->SetBrushColor(FLinearColor(Color.R, Color.G, Color.B, 0.45f));

	FVector2D CanvasSize = AnimCanvas->GetTickSpaceGeometry().GetLocalSize();
	UCanvasPanelSlot* Slot = AnimCanvas->AddChildToCanvas(Flash);
	Slot->SetPosition(FVector2D::ZeroVector);
	Slot->SetSize(CanvasSize);
	Flash->SetRenderOpacity(0.45f);
	ActiveAnimations.Add(Flash);

	const float StartTime = GetWorld()->GetTimeSeconds();
	TWeakObjectPtr<UBorder> WeakFlash = Flash;
	FTimerHandle Handle;
	GetWorld()->GetTimerManager().SetTimer(Handle,
		FTimerDelegate::CreateWeakLambda(this, [this, WeakFlash, Handle, StartTime, Duration]() mutable
		{
			if (!WeakFlash.IsValid())
			{
				if (UWorld* W = GetWorld()) W->GetTimerManager().ClearTimer(Handle);
				return;
			}
			const float Elapsed = GetWorld()->GetTimeSeconds() - StartTime;
			if (Elapsed >= Duration)
			{
				WeakFlash->RemoveFromParent();
				ActiveAnimations.Remove(WeakFlash.Get());
				if (UWorld* W = GetWorld()) W->GetTimerManager().ClearTimer(Handle);
				return;
			}
			WeakFlash->SetRenderOpacity(0.45f * (1.f - Elapsed / Duration));
		}), 0.033f, true);
	AnimTimerHandles.Add(Handle);
}

void AAscendPlayerController::TriggerCombatAnimations(const FString& ActionType)
{
	if (!Combat) return;

	const int32 LogN = CombatLogLines.Num();
	if (LogN == 0 || LogN <= LastProcessedLogIndex) return;

	bool bPlayerDamaged = false;
	int32 PlayerDamageValue = 0;
	int32 PlayerBlockValue = 0;
	bool bPlayerBlocked = false;
	bool bPlayerHealed = false;
	bool bReshuffled = false;
	TArray<TPair<int32, int32>> EnemyDamages;

	int32 StartIdx = LastProcessedLogIndex;
	LastProcessedLogIndex = LogN;
	for (int32 i = StartIdx; i < LogN; ++i)
	{
		const FString& L = CombatLogLines[i];

		// 敌人受击: "  血袍老祖 受到 8 点伤害 (罡气抵挡 2, HP 42/80)"
		if (L.Contains(TEXT("受到")) && L.Contains(TEXT("点伤害")) && !L.Contains(TEXT("你受到")))
		{
			for (int32 ei = 0; ei < Combat->Enemies.Num(); ++ei)
			{
				if (L.Contains(Combat->Enemies[ei].State.Name))
				{
					// 从 "受到" 后面取数字
					int32 Pos = L.Find(TEXT("受到 "));
					if (Pos != INDEX_NONE)
					{
						Pos += 3;
						while (Pos < L.Len() && L[Pos] == TEXT(' ')) ++Pos;
						int32 End = Pos;
						while (End < L.Len() && FChar::IsDigit(L[End])) ++End;
						if (End > Pos)
						{
							EnemyDamages.Emplace(ei, FCString::Atoi(*L.Mid(Pos, End - Pos)));
						}
					}
					break;
				}
			}
		}

		// 玩家受击: "  你受到 12 点伤害 (罡气抵挡 3, HP 65/80)"
		if (L.Contains(TEXT("你受到")) && L.Contains(TEXT("点伤害")) && !L.Contains(TEXT("层")))
		{
			bPlayerDamaged = true;
			int32 Pos = L.Find(TEXT("你受到"));
			if (Pos != INDEX_NONE)
			{
				Pos += 3;
				while (Pos < L.Len() && L[Pos] == TEXT(' ')) ++Pos;
				int32 End = Pos;
				while (End < L.Len() && FChar::IsDigit(L[End])) ++End;
				if (End > Pos) PlayerDamageValue = FCString::Atoi(*L.Mid(Pos, End - Pos));
			}
			// 解析 罡气抵挡
			int32 BkPos = L.Find(TEXT("罡气抵挡 "));
			if (BkPos != INDEX_NONE)
			{
				BkPos += 4;
				while (BkPos < L.Len() && L[BkPos] == TEXT(' ')) ++BkPos;
				int32 BkEnd = BkPos;
				while (BkEnd < L.Len() && FChar::IsDigit(L[BkEnd])) ++BkEnd;
				if (BkEnd > BkPos) PlayerBlockValue = FCString::Atoi(*L.Mid(BkPos, BkEnd - BkPos));
			}
		}

		// DoT: "  你受到 2 层中毒伤害 2 点 (HP 67/70)"
		if (L.Contains(TEXT("你受到")) && L.Contains(TEXT("层")) && L.Contains(TEXT("伤害")))
		{
			bPlayerDamaged = true;
			int32 Pos = L.Find(TEXT("伤害"));
			if (Pos != INDEX_NONE)
			{
				Pos += 2;
				while (Pos < L.Len() && L[Pos] == TEXT(' ')) ++Pos;
				int32 End = Pos;
				while (End < L.Len() && FChar::IsDigit(L[End])) ++End;
				if (End > Pos) PlayerDamageValue = FCString::Atoi(*L.Mid(Pos, End - Pos));
			}
		}

		// 玩家获得护体罡气: "  玩家 获得 8 点护体罡气 (共 8)"
		if (L.Contains(TEXT("玩家 获得")) && L.Contains(TEXT("护体罡气")))
		{
			bPlayerBlocked = true;
		}

		// 玩家恢复气血: "  恢复 10 点气血 (HP 65/80)" 或 "  玩家 恢复 10 点气血"
		if (L.Contains(TEXT("恢复")) && L.Contains(TEXT("点气血")))
		{
			bPlayerHealed = true;
		}

		// 弃牌堆回洗
		if (L.Contains(TEXT("弃牌堆洗回抽牌堆")))
		{
			bReshuffled = true;
		}
	}

	// ---- 生成动画 ----

	const FGeometry& AG = AnimCanvas->GetCachedGeometry();
	FVector2D PlayerDmgPos = AG.AbsoluteToLocal(AG.GetAbsolutePosition() + FVector2D(560.f, 280.f));
	FVector2D PlayerStatusPos = AG.AbsoluteToLocal(AG.GetAbsolutePosition() + FVector2D(560.f, 320.f));

	if (bPlayerHealed)
	{
		SpawnFloatingText(TEXT("+回春"), FLinearColor(0.2f, 0.8f, 0.3f), PlayerStatusPos.X, PlayerStatusPos.Y);
	}

	AnimateScreenShake(3.f, 0.2f);
	for (const auto& Pair : EnemyDamages)
	{
		const int32 EnemyCount = Combat->Enemies.Num();
		FVector2D EP = GetEnemyScreenPos(Pair.Key);
		SpawnFloatingText(FString::Printf(TEXT("-%d"), Pair.Value),
			FLinearColor(0.95f, 0.25f, 0.15f), EP.X - 20.f, EP.Y - 20.f);
		SpawnSlashEffect(EP.X, EP.Y);
	}

	if (bPlayerDamaged)
	{
		if (PlayerDamageValue > 0)
		{
			int32 ActualHP = FMath::Max(0, PlayerDamageValue - PlayerBlockValue);
			if (PlayerBlockValue > 0)
			{
				AnimateScreenShake(8.f, 0.35f);
				AnimateColorFlash(FLinearColor(0.8f, 0.1f, 0.05f), 0.4f);
				SpawnFloatingText(FString::Printf(TEXT("-%d (抵消%d)"), ActualHP, PlayerBlockValue),
					FLinearColor(0.85f, 0.2f, 0.1f), PlayerDmgPos.X, PlayerDmgPos.Y);
			}
			else
			{
				AnimateScreenShake(8.f, 0.35f);
				AnimateColorFlash(FLinearColor(0.8f, 0.1f, 0.05f), 0.4f);
				SpawnFloatingText(FString::Printf(TEXT("-%d"), PlayerDamageValue),
					FLinearColor(0.85f, 0.2f, 0.1f), PlayerDmgPos.X, PlayerDmgPos.Y);
			}
		}
		else
		{
			AnimateScreenShake(8.f, 0.35f);
			AnimateColorFlash(FLinearColor(0.8f, 0.1f, 0.05f), 0.4f);
			SpawnFloatingText(TEXT("受击"), FLinearColor(0.85f, 0.2f, 0.1f), PlayerDmgPos.X, PlayerDmgPos.Y);
		}
	}

	if (bPlayerBlocked)
	{
		AnimateColorFlash(FLinearColor(0.25f, 0.65f, 0.85f), 0.25f);
		SpawnFloatingText(TEXT("罡气"), FLinearColor(0.3f, 0.7f, 0.9f), PlayerStatusPos.X, PlayerStatusPos.Y);
	}

	if (bReshuffled)
	{
		SpawnReshuffleEffect();
	}
}

// -----------------------------------------------------------
// 牌堆检视与抽卡动画
// -----------------------------------------------------------

FVector2D AAscendPlayerController::GetPileAnchor(bool bDrawPile) const
{
	FVector2D VP = GetViewportSize();
	FVector2D ViewportPos(VP.X - 70.f, bDrawPile ? VP.Y - 130.f : VP.Y - 40.f);
	if (AnimCanvas)
	{
		FGeometry AG = AnimCanvas->GetTickSpaceGeometry();
		return AG.AbsoluteToLocal(ViewportPos + AG.GetAbsolutePosition());
	}
	return ViewportPos;
}

void AAscendPlayerController::SpawnMiniCardAnim(const FVector2D& From, const FVector2D& To,
	float Delay, float Duration, float ScaleFrom, float ScaleMid, float ScaleTo,
	float ArcHeight, float FadeInEnd, float FadeOutStart)
{
	if (!AnimCanvas || !GetWorld()) return;

	// 卡背（金边墨面小卡）
	UBorder* Frame = NewObject<UBorder>(AnimCanvas);
	Frame->SetBrushColor(FLinearColor(0.55f, 0.42f, 0.18f));
	Frame->SetPadding(FMargin(1.5f));
	UBorder* Face = NewObject<UBorder>(Frame);
	Face->SetBrushColor(FLinearColor(0.13f, 0.11f, 0.09f));
	Frame->SetContent(Face);

	UCanvasPanelSlot* Slot = AnimCanvas->AddChildToCanvas(Frame);
	Slot->SetSize(FVector2D(34.f, 48.f));
	Slot->SetPosition(From);
	Slot->SetZOrder(9000);
	Frame->SetRenderOpacity(0.f);
	ActiveAnimations.Add(Frame);

	const float StartTime = GetWorld()->GetTimeSeconds();
	TWeakObjectPtr<UBorder> WeakCard = Frame;
	FTimerHandle Handle;
	GetWorld()->GetTimerManager().SetTimer(Handle,
		FTimerDelegate::CreateWeakLambda(this,
			[this, WeakCard, Handle, StartTime, Delay, Duration, From, To,
			 ScaleFrom, ScaleMid, ScaleTo, ArcHeight, FadeInEnd, FadeOutStart]() mutable
		{
			if (!WeakCard.IsValid())
			{
				if (UWorld* W = GetWorld()) W->GetTimerManager().ClearTimer(Handle);
				return;
			}
			const float Elapsed = GetWorld()->GetTimeSeconds() - StartTime - Delay;
			if (Elapsed < 0.f) return;
			if (Elapsed >= Duration)
			{
				WeakCard->RemoveFromParent();
				ActiveAnimations.Remove(WeakCard.Get());
				if (UWorld* W = GetWorld()) W->GetTimerManager().ClearTimer(Handle);
				return;
			}

			const float T = Elapsed / Duration;
			const float Ease = 1.f - (1.f - T) * (1.f - T); // easeOutQuad

			// 位移 + 弧线上扬
			FVector2D Pos = FMath::Lerp(From, To, Ease);
			Pos.Y -= FMath::Sin(T * PI) * ArcHeight;
			if (UCanvasPanelSlot* S = Cast<UCanvasPanelSlot>(WeakCard->Slot))
			{
				S->SetPosition(Pos);
			}

			// 三段缩放: 起点 → 中段放大 → 终点回落
			const float Scale = (T < 0.5f)
				? FMath::Lerp(ScaleFrom, ScaleMid, T * 2.f)
				: FMath::Lerp(ScaleMid, ScaleTo, (T - 0.5f) * 2.f);
			WeakCard->SetRenderScale(FVector2D(Scale, Scale));

			// 透明度: 前段淡入 / 后段淡出
			float Op = 1.f;
			if (FadeInEnd > 0.f && T < FadeInEnd) Op = T / FadeInEnd;
			else if (FadeOutStart < 1.f && T > FadeOutStart) Op = 1.f - (T - FadeOutStart) / (1.f - FadeOutStart);
			WeakCard->SetRenderOpacity(FMath::Clamp(Op, 0.f, 1.f) * 0.95f);
		}), 0.033f, true);
	AnimTimerHandles.Add(Handle);
}

void AAscendPlayerController::PlayHandEntranceAnimation(int32 Count)
{
	if (!GetWorld() || !Combat) return;

	const int32 N = HandCardButtons.Num();
	const int32 StartIdx = (Count > 0) ? FMath::Max(0, N - Count) : 0;
	const int32 AnimN = (Count > 0) ? FMath::Min(Count, N) : N;
	const FVector2D PileAnchor(GetViewportSize().X - 100.f, GetViewportSize().Y - 200.f);

	for (int32 ai = 0; ai < AnimN; ++ai)
	{
		const int32 i = StartIdx + ai;
		if (!HandCardButtons.IsValidIndex(i) || !HandCardButtons[i].IsValid()) continue;
		TWeakObjectPtr<UButton> Btn = HandCardButtons[i];

		Btn->SetRenderTranslation(PileAnchor);
		Btn->SetRenderScale(FVector2D(0.2f, 0.2f));
		Btn->SetRenderOpacity(0.f);

		const float Delay = i * 0.08f;
		const float Dur = 0.35f;
		const float StartTime = GetWorld()->GetTimeSeconds();
		FTimerHandle H;

		TWeakObjectPtr<AAscendPlayerController> WeakThis(this);
		GetWorld()->GetTimerManager().SetTimer(H,
			FTimerDelegate::CreateWeakLambda(this, [Btn, H, StartTime, Delay, Dur, PileAnchor, WeakThis]() mutable
			{
				if (!Btn.IsValid() || !WeakThis.IsValid()) return;
				AAscendPlayerController* PC = WeakThis.Get();
				UWorld* W = PC->GetWorld();
				if (!W) return;

				const float Elapsed = W->GetTimeSeconds() - StartTime - Delay;
				if (Elapsed < 0.f) return;
				if (Elapsed >= Dur)
				{
					Btn->SetRenderTranslation(FVector2D::ZeroVector);
					Btn->SetRenderScale(FVector2D(1.f, 1.f));
					Btn->SetRenderOpacity(1.f);
					W->GetTimerManager().ClearTimer(H);
					return;
				}

				const float T = Elapsed / Dur;
				const float Ease = 1.f - FMath::Pow(1.f - T, 3.f);

				FVector2D Tr = PileAnchor * (1.f - Ease);
				Tr.Y -= FMath::Sin(T * PI) * 40.f;
				Btn->SetRenderTranslation(Tr);

				const float Scale = (T < 0.5f)
					? FMath::Lerp(0.2f, 1.15f, T * 2.f)
					: FMath::Lerp(1.15f, 1.0f, (T - 0.5f) * 2.f);
				Btn->SetRenderScale(FVector2D(Scale, Scale));
				Btn->SetRenderOpacity(FMath::Clamp(T / 0.15f, 0.f, 1.f));
			}), 0.033f, true);
		AnimTimerHandles.Add(H);
	}
}

void AAscendPlayerController::SpawnReshuffleEffect()
{
	if (!AnimCanvas || !GetWorld()) return;

	const FVector2D From = GetPileAnchor(false); // 弃牌堆
	const FVector2D To = GetPileAnchor(true);    // 抽牌堆

	// 快速一张张洗入：从弃牌堆飞向抽牌堆，边飞边缩小钻入
	const int32 N = 6;
	for (int32 i = 0; i < N; ++i)
	{
		// 起点微微错开，模拟一摞牌被快速拨入
		const FVector2D FromJitter = From + FVector2D(FMath::FRandRange(-14.f, 14.f), FMath::FRandRange(-8.f, 8.f));
		SpawnMiniCardAnim(FromJitter, To,
			i * 0.045f,  // 快速连发
			0.28f,
			0.8f, 0.85f, 0.25f, // 近大 → 钻入变小
			-24.f,       // 向下微弧（洗入感）
			0.05f, 0.7f);
	}

	SpawnFloatingText(TEXT("洗牌重铸"), FLinearColor(0.85f, 0.68f, 0.25f), To.X - 110.f, To.Y - 30.f, 1.0f);
}

void AAscendPlayerController::ShowGainToasts()
{
	if (!Run || !AnimCanvas || !GetWorld()) return;

	const TArray<FString> Notes = Run->ConsumeGainNotifications();
	if (Notes.Num() == 0) return;

	// 顶部居中浮条：金边墨底，逐条列出
	UBorder* Frame = NewObject<UBorder>(AnimCanvas);
	Frame->SetBrushColor(FLinearColor(0.45f, 0.35f, 0.16f, 0.95f));
	Frame->SetPadding(FMargin(1.5f));
	UBorder* Inner = NewObject<UBorder>(Frame);
	Inner->SetBrushColor(FLinearColor(0.06f, 0.055f, 0.09f, 0.95f));
	Inner->SetPadding(FMargin(14.f, 8.f));

	UVerticalBox* VB = NewObject<UVerticalBox>(Inner);
	for (const FString& N : Notes)
	{
		UTextBlock* T = FAscendUIStyle::MakeText(VB, N, 16, FAscendUIStyle::GoldYellow());
		T->SetJustification(ETextJustify::Center);
		UVerticalBoxSlot* TS = VB->AddChildToVerticalBox(T);
		TS->SetPadding(FMargin(0.f, 2.f));
	}
	Inner->SetContent(VB);
	Frame->SetContent(Inner);

	const FVector2D VP = GetViewportSize();
	UCanvasPanelSlot* Slot = AnimCanvas->AddChildToCanvas(Frame);
	Slot->SetPosition(FVector2D(VP.X * 0.5f - 180.f, 90.f));
	Slot->SetAutoSize(true);
	Slot->SetZOrder(9800);
	ActiveAnimations.Add(Frame);

	// 停留后上浮淡出
	const float StartTime = GetWorld()->GetTimeSeconds();
	const float Duration = 3.2f;
	TWeakObjectPtr<UBorder> WeakFrame = Frame;
	FTimerHandle Handle;
	GetWorld()->GetTimerManager().SetTimer(Handle,
		FTimerDelegate::CreateWeakLambda(this, [this, WeakFrame, Handle, StartTime, Duration]() mutable
		{
			if (!WeakFrame.IsValid())
			{
				if (UWorld* W = GetWorld()) W->GetTimerManager().ClearTimer(Handle);
				return;
			}
			const float Elapsed = GetWorld()->GetTimeSeconds() - StartTime;
			if (Elapsed >= Duration)
			{
				WeakFrame->RemoveFromParent();
				ActiveAnimations.Remove(WeakFrame.Get());
				if (UWorld* W = GetWorld()) W->GetTimerManager().ClearTimer(Handle);
				return;
			}
			const float FadeStart = Duration - 0.8f;
			if (Elapsed > FadeStart)
			{
				const float T = (Elapsed - FadeStart) / 0.8f;
				WeakFrame->SetRenderOpacity(1.f - T);
				WeakFrame->SetRenderTranslation(FVector2D(0.f, -18.f * T));
			}
		}), 0.033f, true);
	AnimTimerHandles.Add(Handle);
}

void AAscendPlayerController::BuildPileViewer(UOverlay* ParentOverlay)
{
	if (!Combat || !ParentOverlay) return;

	const bool bDraw = (PileViewerMode == 1);
	const TArray<FCardInstance>& Pile = bDraw ? Combat->DrawPile : Combat->DiscardPile;

	// 面板尺寸
	USizeBox* SizeWrap = NewObject<USizeBox>(ParentOverlay);
	SizeWrap->SetWidthOverride(250.f);
	SizeWrap->SetHeightOverride(380.f);

	UBorder* Panel = NewObject<UBorder>(SizeWrap);
	Panel->SetBrushColor(FLinearColor(0.07f, 0.065f, 0.10f, 0.97f));
	Panel->SetPadding(FMargin(0.f));

	// 金边
	UBorder* GoldFrame = NewObject<UBorder>(Panel);
	GoldFrame->SetBrushColor(FLinearColor(0.45f, 0.35f, 0.16f));
	GoldFrame->SetPadding(FMargin(1.5f));
	UBorder* Inner = NewObject<UBorder>(GoldFrame);
	Inner->SetBrushColor(FLinearColor(0.07f, 0.065f, 0.10f, 1.f));
	Inner->SetPadding(FMargin(10.f));

	UVerticalBox* VB = NewObject<UVerticalBox>(Inner);

	// ---- 标题行 ----
	UHorizontalBox* TitleRow = NewObject<UHorizontalBox>(VB);
	const FString TitleStr = bDraw
		? FString::Printf(TEXT("抽牌堆 · %d（顺序未知）"), Pile.Num())
		: FString::Printf(TEXT("弃牌堆 · %d"), Pile.Num());
	UTextBlock* TitleT = FAscendUIStyle::MakeText(TitleRow, TitleStr, 16, FAscendUIStyle::GoldYellow());
	UHorizontalBoxSlot* TitleSlot = TitleRow->AddChildToHorizontalBox(TitleT);
	TitleSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	TitleSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 6.f));

	UButton* CloseBtn = FAscendUIStyle::MakeStyledButton(TitleRow, TEXT("×"), 13, FAscendUIStyle::PaperWhite());
	UClickProxy* CloseProxy = NewObject<UClickProxy>(CloseBtn);
	CloseProxy->Tag = TEXT("pile_close");
	CloseProxy->Index = 0;
	CloseProxy->Owner = this;
	CloseBtn->OnClicked.AddDynamic(CloseProxy, &UClickProxy::HandleClick);
	Proxies.Add(CloseProxy);
	TitleRow->AddChildToHorizontalBox(CloseBtn);

	VB->AddChildToVerticalBox(TitleRow);

	// ---- 内容列表（按名称分组计数） ----
	UScrollBox* Scroll = NewObject<UScrollBox>(VB);
	if (Pile.Num() == 0)
	{
		UTextBlock* EmptyT = FAscendUIStyle::MakeText(Scroll, TEXT("（空）"), 13, FAscendUIStyle::DimGray());
		Scroll->AddChild(EmptyT);
	}
	else
	{
		TMap<FString, int32> Counts;
		TArray<FString> Order;
		TMap<FString, FString> RarityByName;
		for (const FCardInstance& C : Pile)
		{
			const FString Key = C.GetDisplayName();
			if (!Counts.Contains(Key)) Order.Add(Key);
			Counts.FindOrAdd(Key)++;
			RarityByName.Add(Key, C.Data.Rarity);
		}
		for (const FString& Name : Order)
		{
			const FString* Rar = RarityByName.Find(Name);
			const FLinearColor C = Rar ? FAscendUIStyle::RarityColor(*Rar) : FLinearColor(0.8f, 0.8f, 0.8f);
			UTextBlock* RowT = FAscendUIStyle::MakeText(Scroll,
				FString::Printf(TEXT("%s ×%d"), *Name, Counts[Name]), 14, FSlateColor(C));
			RowT->SetMargin(FMargin(0.f, 2.f));
			Scroll->AddChild(RowT);
		}
	}
	UVerticalBoxSlot* ScrollSlot = VB->AddChildToVerticalBox(Scroll);
	FSlateChildSize FillSize(ESlateSizeRule::Fill);
	ScrollSlot->SetSize(FillSize);

	Inner->SetContent(VB);
	GoldFrame->SetContent(Inner);
	Panel->SetContent(GoldFrame);
	SizeWrap->SetContent(Panel);

	UOverlaySlot* OvlSlot = ParentOverlay->AddChildToOverlay(SizeWrap);
	OvlSlot->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Right);
	OvlSlot->SetVerticalAlignment(EVerticalAlignment::VAlign_Bottom);
	OvlSlot->SetPadding(FMargin(16.f, 16.f, 16.f, 190.f));
}
