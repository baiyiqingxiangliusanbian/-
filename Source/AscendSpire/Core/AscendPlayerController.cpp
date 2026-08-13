#include "AscendPlayerController.h"
#include "UI/ClickProxy.h"
#include "UI/AscendUIStyle.h"
#include "UI/AscendRootWidget.h"
#include "UI/AscendArt.h"
#include "UI/AscendCardLayout.h"
#include "UI/CombatSlashWidget.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "TimerManager.h"
#include "Engine/Engine.h"
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
#include "Components/EditableTextBox.h"
#include "Components/CheckBox.h"
#include "Components/InputComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundWaveProcedural.h"
#include "Math/UnrealMathUtility.h"
#include "Misc/ConfigCacheIni.h"

namespace
{
	constexpr float HandHoverScale = 1.4f;
	constexpr float HandHoverLift = 36.f;
	constexpr float TouchHandHoverScale = 1.82f;
	constexpr float TouchHandHoverLift = 62.f;
	constexpr float TouchDragScale = 1.42f;
	constexpr float MouseDragScale = 1.12f;
	constexpr float TouchDragThreshold = 22.f;
	constexpr int32 HandHoverZOrder = 1000;

	void PlaceCardWidget(UCanvasPanel* Canvas, UWidget* Widget, float X, float Y, float W, float H, float Scale, int32 ZOrder)
	{
		if (UCanvasPanelSlot* Slot = Canvas->AddChildToCanvas(Widget))
		{
			Slot->SetPosition(FVector2D(X * Scale, Y * Scale));
			Slot->SetSize(FVector2D(W * Scale, H * Scale));
			Slot->SetZOrder(ZOrder);
		}
	}

	UWidget* BuildFixedCardFace(
		UObject* Outer,
		const FString& ArtPath,
		const FLinearColor& TypeColor,
		const FString& DisplayName,
		const FString& DisplayDescription,
		int32 DisplayCost,
		const FLinearColor& CostColor,
		bool bRetain,
		bool bExhaust,
		float Scale)
	{
		using namespace AscendCardLayout;
		const float S = FMath::Max(0.1f, Scale);

		USizeBox* CardSizer = NewObject<USizeBox>(Outer);
		CardSizer->SetWidthOverride(Width * S);
		CardSizer->SetHeightOverride(Height * S);

		UCanvasPanel* Canvas = NewObject<UCanvasPanel>(CardSizer);

		// A solid inner backplate owns every pixel beneath the frame.  It is inset
		// from the transparent outer corners, so no old grey placeholder or card
		// art can leak beyond the new border.
		UBorder* Backplate = NewObject<UBorder>(Canvas);
		Backplate->SetBrushColor(FLinearColor(0.018f, 0.045f, 0.038f, 1.f));
		PlaceCardWidget(Canvas, Backplate, InnerX, InnerY, InnerW, InnerH, S, 0);

		UBorder* ArtClip = NewObject<UBorder>(Canvas);
		ArtClip->SetClipping(EWidgetClipping::ClipToBounds);
		ArtClip->SetBrushColor(FAscendArt::Exists(ArtPath)
			? FLinearColor(0.015f, 0.025f, 0.024f, 1.f)
			: (TypeColor * 0.20f + FLinearColor(0.04f, 0.065f, 0.055f) * 0.80f));
		if (UImage* ArtImage = FAscendArt::MakeImage(ArtClip, ArtPath))
		{
			UScaleBox* ArtScale = NewObject<UScaleBox>(ArtClip);
			ArtScale->SetStretch(EStretch::ScaleToFill);
			ArtScale->SetContent(ArtImage);
			ArtClip->SetContent(ArtScale);
		}
		PlaceCardWidget(Canvas, ArtClip, ArtX, ArtY, ArtW, ArtH, S, 5);

		UOverlay* TitlePanel = NewObject<UOverlay>(Canvas);
		if (UImage* TitleArt = FAscendArt::MakeImage(TitlePanel, TEXT("Art/ui/card_title_bar_v2.png")))
		{
			TitleArt->SetVisibility(ESlateVisibility::HitTestInvisible);
			UOverlaySlot* ArtSlot = TitlePanel->AddChildToOverlay(TitleArt);
			ArtSlot->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Fill);
			ArtSlot->SetVerticalAlignment(EVerticalAlignment::VAlign_Fill);
		}
		UTextBlock* NameText = FAscendUIStyle::MakeText(TitlePanel, DisplayName,
			FMath::Clamp(FMath::RoundToInt(14.f * S), 11, 17), FAscendUIStyle::GoldYellow());
		NameText->SetJustification(ETextJustify::Center);
		NameText->SetShadowOffset(FVector2D(1.f, 1.f));
		UScaleBox* NameScale = NewObject<UScaleBox>(TitlePanel);
		NameScale->SetStretch(EStretch::ScaleToFit);
		NameScale->SetStretchDirection(EStretchDirection::DownOnly);
		NameScale->SetContent(NameText);
		UOverlaySlot* NameSlot = TitlePanel->AddChildToOverlay(NameScale);
		NameSlot->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Fill);
		NameSlot->SetVerticalAlignment(EVerticalAlignment::VAlign_Fill);
		NameSlot->SetPadding(FMargin(17.f * S, 5.f * S, 17.f * S, 5.f * S));
		PlaceCardWidget(Canvas, TitlePanel, TitleX, TitleY, TitleW, TitleH, S, 15);

		UOverlay* RulesPanel = NewObject<UOverlay>(Canvas);
		RulesPanel->SetClipping(EWidgetClipping::ClipToBounds);
		if (UImage* RulesArt = FAscendArt::MakeImage(RulesPanel, TEXT("Art/ui/card_rules_panel_v2.png")))
		{
			RulesArt->SetVisibility(ESlateVisibility::HitTestInvisible);
			UOverlaySlot* ArtSlot = RulesPanel->AddChildToOverlay(RulesArt);
			ArtSlot->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Fill);
			ArtSlot->SetVerticalAlignment(EVerticalAlignment::VAlign_Fill);
		}
		FString RulesText = DisplayDescription;
		if (bRetain && !RulesText.Contains(TEXT("保留")))
		{
			RulesText += RulesText.IsEmpty() ? TEXT("【保留】") : TEXT("\n【保留】");
		}
		if (bExhaust && !RulesText.Contains(TEXT("消耗")) && !RulesText.Contains(TEXT("消失")))
		{
			RulesText += RulesText.IsEmpty() ? TEXT("【消耗】") : TEXT("\n【消耗】");
		}
		UTextBlock* DescriptionText = FAscendUIStyle::MakeText(RulesPanel, RulesText,
			FMath::Clamp(FMath::RoundToInt(10.f * S), 8, 12), FAscendUIStyle::PaperWhite());
		DescriptionText->SetAutoWrapText(true);
		DescriptionText->SetWrapTextAt(120.f * S);
		DescriptionText->SetJustification(ETextJustify::Center);
		UScaleBox* DescriptionScale = NewObject<UScaleBox>(RulesPanel);
		DescriptionScale->SetStretch(EStretch::ScaleToFit);
		DescriptionScale->SetStretchDirection(EStretchDirection::DownOnly);
		DescriptionScale->SetContent(DescriptionText);
		UOverlaySlot* DescriptionSlot = RulesPanel->AddChildToOverlay(DescriptionScale);
		DescriptionSlot->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Fill);
		DescriptionSlot->SetVerticalAlignment(EVerticalAlignment::VAlign_Fill);
		DescriptionSlot->SetPadding(FMargin(10.f * S, 9.f * S, 10.f * S, 9.f * S));
		PlaceCardWidget(Canvas, RulesPanel, RulesX, RulesY, RulesW, RulesH, S, 15);

		// The frame is a true transparent overlay and is deliberately last, so it
		// masks the joins without ever reserving or guessing a content area.
		if (UImage* Frame = FAscendArt::MakeImage(Canvas, TEXT("Art/ui/card_border_v2.png")))
		{
			Frame->SetVisibility(ESlateVisibility::HitTestInvisible);
			PlaceCardWidget(Canvas, Frame, 0.f, 0.f, Width, Height, S, 40);
		}

		UOverlay* CostGem = NewObject<UOverlay>(Canvas);
		if (UImage* GemArt = FAscendArt::MakeImage(CostGem, TEXT("Art/ui/spirit_gem.png")))
		{
			GemArt->SetVisibility(ESlateVisibility::HitTestInvisible);
			UOverlaySlot* GemSlot = CostGem->AddChildToOverlay(GemArt);
			GemSlot->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Fill);
			GemSlot->SetVerticalAlignment(EVerticalAlignment::VAlign_Fill);
		}
		UTextBlock* CostText = FAscendUIStyle::MakeText(CostGem, FString::FromInt(DisplayCost),
			FMath::Clamp(FMath::RoundToInt(22.f * S), 18, 27), CostColor);
		CostText->SetJustification(ETextJustify::Center);
		CostText->SetShadowOffset(FVector2D(2.f, 2.f));
		CostText->SetShadowColorAndOpacity(FLinearColor(0.f, 0.02f, 0.04f, 1.f));
		FSlateFontInfo CostFont = CostText->GetFont();
		CostFont.OutlineSettings.OutlineSize = 2;
		CostFont.OutlineSettings.OutlineColor = FLinearColor(0.f, 0.02f, 0.04f, 1.f);
		CostText->SetFont(CostFont);
		UOverlaySlot* CostTextSlot = CostGem->AddChildToOverlay(CostText);
		CostTextSlot->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Fill);
		CostTextSlot->SetVerticalAlignment(EVerticalAlignment::VAlign_Center);
		PlaceCardWidget(Canvas, CostGem, CostX, CostY, CostW, CostH, S, 55);

		CardSizer->SetContent(Canvas);
		return CardSizer;
	}

}

void AAscendPlayerController::BeginPlay()
{
	Super::BeginPlay();
	// The game is entirely turn-based UMG; rendering above 60 FPS only increases
	// Metal/Slate CPU and GPU pressure without improving input or animation timing.
	if (GEngine) GEngine->SetMaxFPS(60.f);

	bShowMouseCursor = true;
	bEnableClickEvents = true;
	bEnableMouseOverEvents = true;

#if PLATFORM_ANDROID
	bEnableTouchEvents = true;
	bEnableTouchOverEvents = true;
#else
	// 桌面端只走原生鼠标路径。Mac 触控板会被系统转换为鼠标事件，
	// 不应再同时启用 UE 的触摸兼容事件，否则释放可能被派发两次。
	bEnableTouchEvents = false;
	bEnableTouchOverEvents = false;
#endif

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
	InfiniteNarrativeService = NewObject<UInfiniteNarrativeService>(this);
	LoadInfiniteNarrativeSettings();

	// 根界面：Overlay（底层ScreenHost + 上层AnimCanvas）
	RootWidget = CreateWidget<UAscendRootWidget>(this, UAscendRootWidget::StaticClass());
	RootWidget->OwnerController = this;
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

	// 菜单层独立于 ScreenHost，因此战斗、RP、奖励等任意界面都可打开。
	MenuButtonLayer = NewObject<UCanvasPanel>(RootOverlay);
	MenuButtonLayer->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	UOverlaySlot* MenuLayerSlot = RootOverlay->AddChildToOverlay(MenuButtonLayer);
	MenuLayerSlot->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Fill);
	MenuLayerSlot->SetVerticalAlignment(EVerticalAlignment::VAlign_Fill);
	UButton* MenuButton = MakeLinkedButton(MenuButtonLayer, TEXT(""), TEXT("menu_open"), 0, 16);
	MenuButton->SetBackgroundColor(FLinearColor::Transparent);
	UOverlay* MenuVisual = NewObject<UOverlay>(MenuButton);
	if (UImage* MenuFrame = FAscendArt::MakeImage(MenuVisual, TEXT("Art/ui/hud_menu.png")))
	{
		MenuFrame->SetVisibility(ESlateVisibility::HitTestInvisible);
		UOverlaySlot* FrameSlot = MenuVisual->AddChildToOverlay(MenuFrame);
		FrameSlot->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Fill);
		FrameSlot->SetVerticalAlignment(EVerticalAlignment::VAlign_Fill);
	}
	UTextBlock* MenuText = FAscendUIStyle::MakeText(MenuVisual, TEXT("菜单"), 21, FAscendUIStyle::GoldYellow());
	MenuText->SetJustification(ETextJustify::Center);
	MenuText->SetShadowOffset(FVector2D(1.f, 1.f));
	UOverlaySlot* MenuTextSlot = MenuVisual->AddChildToOverlay(MenuText);
	MenuTextSlot->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Fill);
	MenuTextSlot->SetVerticalAlignment(EVerticalAlignment::VAlign_Center);
	MenuButton->SetContent(MenuVisual);
	PersistentProxies.Append(PendingScreenProxies);
	PendingScreenProxies.Reset();
	if (UCanvasPanelSlot* MenuSlot = MenuButtonLayer->AddChildToCanvas(MenuButton))
	{
		MenuSlot->SetAnchors(FAnchors(1.f, 0.f));
		MenuSlot->SetAlignment(FVector2D(1.f, 0.f));
		MenuSlot->SetPosition(FVector2D(-20.f, 14.f));
		MenuSlot->SetSize(FVector2D(190.f, 64.f));
		MenuSlot->SetZOrder(20000);
	}

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
	PendingScreenProxies.Add(P);
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
	PendingScreenProxies.Add(P);
	return Btn;
}

UTextBlock* AAscendPlayerController::MakeLogText(const FString& Text, FSlateColor Color)
{
	return FAscendUIStyle::MakeText(RootWidget, Text, 15, Color);
}

void AAscendPlayerController::SetScreen(UWidget* Content, EGameScreen Screen)
{
	UE_LOG(LogTemp, Display, TEXT("[DIAG] SetScreen begin screen=%d"), static_cast<int32>(Screen));
	// Screen builders create their controls before this call. Retain only this generation so
	// buttons from every previous combat/RP redraw do not keep entire widget trees alive.
	Proxies = MoveTemp(PendingScreenProxies);
	PendingScreenProxies.Reset();
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
	const bool bMenuAction = Tag.StartsWith(TEXT("menu_"));
	if (bInputLocked && !bMenuAction) return;
	if (Tag == TEXT("menu_open"))
	{
		TogglePauseMenu();
		return;
	}
	if (Tag == TEXT("menu_resume"))
	{
		HidePauseMenu();
		return;
	}
	if (Tag == TEXT("menu_save_settings"))
	{
		const bool bRefreshRP = CurrentScreen == EGameScreen::InfiniteNarrative;
		SaveInfiniteNarrativeSettings();
		HidePauseMenu();
		if (bRefreshRP) ShowInfiniteNarrative();
		return;
	}
	if (Tag == TEXT("menu_full_settings"))
	{
		SettingsReturnScreen = CurrentScreen;
		HidePauseMenu();
		ShowSettings(SettingsCategory);
		return;
	}
	if (Tag == TEXT("menu_title"))
	{
		SaveAndReturnToTitle();
		return;
	}
	if (Tag == TEXT("menu_quit"))
	{
		SaveAndQuitGame();
		return;
	}
	if (Tag == TEXT("title_new") || Tag == TEXT("title_infinite"))
	{
		// 无尽叙事现为标准新游戏流程；title_new 作为旧入口别名保留，避免遗留调用落回传统模式。
		StartInfiniteNarrativeRun();
	}
	else if (Tag == TEXT("title_settings"))
	{
		SettingsReturnScreen = EGameScreen::Title;
		ShowSettings(SettingsCategory);
	}
	else if (Tag == TEXT("title_continue"))
	{
		if (Run->LoadRun())
		{
			if (Run->State.bInfiniteNarrativeMode)
			{
				FString ResultSummary;
				if (Run->RestorePendingInfiniteCombat(PendingInfiniteEncounter, ResultSummary,
					PendingNarrativeCards, PendingNarrativeRelics))
				{
					PendingInfiniteChoice = FInfiniteNarrativeChoice();
					PendingInfiniteChoice.ResultSummary = ResultSummary;
					if (PendingNarrativeCards.Num() + PendingNarrativeRelics.Num() > 0) ShowAcquiredItems();
					else BeginPendingInfiniteCombat();
				}
				else RequestNextInfiniteNarrative();
			}
			else ShowMap();
		}
	}
	else if (Tag == TEXT("settings_save"))
	{
		SaveInfiniteNarrativeSettings();
		ReturnFromSettings();
	}
	else if (Tag == TEXT("settings_back"))
	{
		ReturnFromSettings();
	}
	else if (Tag == TEXT("settings_tab"))
	{
		SaveInfiniteNarrativeSettings();
		ShowSettings(Index);
	}
	else if (Tag == TEXT("rp_choice"))
	{
		SelectInfiniteNarrativeChoice(Index);
	}
	else if (Tag == TEXT("rp_freeform"))
	{
		const FString Action = RPFreeformInput ? RPFreeformInput->GetText().ToString().TrimStartAndEnd() : TEXT("");
		if (!Action.IsEmpty()) RequestNextInfiniteNarrative(Action);
	}
	else if (Tag == TEXT("rp_retry"))
	{
		if (bWaitingForCombatNarrativeAfterReward || bCombatNarrativePrefetchFailed)
		{
			bWaitingForCombatNarrativeAfterReward = true;
			bCombatNarrativePrefetchFailed = false;
			StartCombatNarrativePrefetch(bCombatPrefetchIncludedLog);
			ShowInfiniteNarrativeLoading(TEXT("正在重新生成战后剧情……"));
		}
		else RequestNextInfiniteNarrative();
	}
	else if (Tag == TEXT("rp_error_title"))
	{
		SaveAndReturnToTitle();
	}
	else if (Tag == TEXT("acquisition_next"))
	{
		bInfiniteChoiceResolved = false;
		if (PendingInfiniteEncounter.EnemyIds.Num() > 0) BeginPendingInfiniteCombat();
		else RequestNextInfiniteNarrative();
	}
	else if (Tag == TEXT("rp_resolution_next"))
	{
		if (PendingInfiniteEncounter.EnemyIds.Num() > 0)
		{
			bInfiniteChoiceResolved = false;
			BeginPendingInfiniteCombat();
		}
		else if (!BeginInfiniteGameFunction()) RequestNextInfiniteNarrative();
	}
	else if (Tag == TEXT("rp_paid_remove_open"))
	{
		ShowInfinitePaidDeckRemoval();
	}
	else if (Tag == TEXT("rp_paid_remove_card"))
	{
		FString RemovedName;
		if (Run->RemoveDeckCardForGold(Index, 50, RemovedName))
		{
			Run->AddRPHistory(FString::Printf(TEXT("牌组整理：花费50灵石删除【%s】。"), *RemovedName));
			ShowInfinitePaidDeckRemoval();
		}
	}
	else if (Tag == TEXT("rp_paid_remove_back"))
	{
		ShowInfiniteNarrative();
	}
	else if (Tag == TEXT("rp_remove_card"))
	{
		if (bInfiniteFunctionFlowActive && Run->RemoveDeckCardAt(Index, ActiveInfiniteGameOperation.bAllowCurse))
		{
			--ActiveInfiniteGameOperation.Count;
			Run->SaveRun();
			if (ActiveInfiniteGameOperation.Count > 0)
				ShowInfiniteCardOperation(false, ActiveInfiniteGameOperation);
			else CompleteInfiniteGameFunction();
		}
	}
	else if (Tag == TEXT("rp_upgrade_card"))
	{
		if (bInfiniteFunctionFlowActive && Run->UpgradeDeckCardAt(Index))
		{
			--ActiveInfiniteGameOperation.Count;
			Run->SaveRun();
			if (ActiveInfiniteGameOperation.Count > 0)
				ShowInfiniteCardOperation(true, ActiveInfiniteGameOperation);
			else CompleteInfiniteGameFunction();
		}
	}
	else if (Tag == TEXT("rp_function_complete"))
	{
		CompleteInfiniteGameFunction();
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
	else if (Tag == TEXT("title_authored_library"))
	{
		ShowAuthoredLibrary();
	}
	else if (Tag == TEXT("authored_library_back"))
	{
		ShowTitle();
	}
	else if (Tag == TEXT("authored_delete_card"))
	{
		const TArray<FCardData>& Cards = Run->GetPersistentAuthoredCards();
		if (Cards.IsValidIndex(Index)) Run->DeletePersistentAuthoredCard(Cards[Index].Id);
		ShowAuthoredLibrary();
	}
	else if (Tag == TEXT("authored_delete_relic"))
	{
		const TArray<FRelicData>& Relics = Run->GetPersistentAuthoredRelics();
		if (Relics.IsValidIndex(Index)) Run->DeletePersistentAuthoredRelic(Relics[Index].Id);
		ShowAuthoredLibrary();
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
	else if (Tag == TEXT("discover_choice"))
	{
		if (Combat && Combat->ResolveDiscoverChoice(Index))
		{
			RefreshCombatPanel();
			TriggerCombatAnimations(TEXT("draw"));
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

		// 在引擎结算前记录当前手牌位置；结算后按实际弃掉的 UID 播放动画。
		TMap<int32, FVector2D> HandPositionsByUID;
		if (AnimCanvas)
		{
			const FGeometry CanvasGeometry = AnimCanvas->GetCachedGeometry();
			for (int32 CardIndex = 0; CardIndex < Combat->Hand.Num(); ++CardIndex)
			{
				if (!HandCardButtons.IsValidIndex(CardIndex) || !HandCardButtons[CardIndex].IsValid()) continue;
				const FGeometry CardGeometry = HandCardButtons[CardIndex]->GetCachedGeometry();
				const FVector2D AbsoluteCenter = CardGeometry.LocalToAbsolute(CardGeometry.GetLocalSize() * 0.5f);
				const FVector2D LocalMiniCardPos = CanvasGeometry.AbsoluteToLocal(AbsoluteCenter) - FVector2D(17.f, 24.f);
				HandPositionsByUID.Add(Combat->Hand[CardIndex].UID, LocalMiniCardPos);
			}
		}

		UE_LOG(LogTemp, Display, TEXT("[DIAG] endturn begin turn=%d"), Combat->TurnCount);
		Combat->EndPlayerTurn();
		UE_LOG(LogTemp, Display, TEXT("[DIAG] endturn after EndPlayerTurn turn=%d over=%d"), Combat->TurnCount, Combat->IsCombatOver());

		TArray<FVector2D> DiscardAnimStarts;
		for (int32 DiscardedUID : Combat->PendingTurnEndDiscardUIDs)
		{
			if (const FVector2D* StartPos = HandPositionsByUID.Find(DiscardedUID))
			{
				DiscardAnimStarts.Add(*StartPos);
			}
		}
		Combat->PendingTurnEndDiscardUIDs.Reset();

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
		PlayEndTurnDiscardAnimation(DiscardAnimStarts);
		UE_LOG(LogTemp, Display, TEXT("[DIAG] endturn after RefreshCombatPanel"));
		TriggerCombatAnimations(TEXT("enemy_turn"));
		UE_LOG(LogTemp, Display, TEXT("[DIAG] endturn done"));
	}
	else if (Tag == TEXT("pill"))
	{
		if (!Combat || !Combat->bCombatActive) return;
		if (Combat->bPlayerTurnSkipped) return;
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
		if (bInfiniteFunctionFlowActive) CompleteInfiniteGameFunction();
		else ContinueAfterReward();
	}
	else if (Tag == TEXT("reward_skip"))
	{
		Run->PickRewardCard(FDeckCard());
		if (bInfiniteFunctionFlowActive) CompleteInfiniteGameFunction();
		else ContinueAfterReward();
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
		UE_LOG(LogTemp, Display, TEXT("[DIAG] shop_leave return begin"));
		if (bInfiniteFunctionFlowActive) CompleteInfiniteGameFunction();
		else ShowMap();
		UE_LOG(LogTemp, Display, TEXT("[DIAG] shop_leave done"));
	}
	else if (Tag == TEXT("rest_heal"))
	{
		Run->ResolveRest(true);
		Run->SaveRun();
		if (bInfiniteFunctionFlowActive) CompleteInfiniteGameFunction();
		else ShowMap();
	}
	else if (Tag == TEXT("rest_upgrade"))
	{
		Run->ResolveRest(false);
		Run->SaveRun();
		if (bInfiniteFunctionFlowActive) CompleteInfiniteGameFunction();
		else ShowMap();
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
			RegisterEncounterRuntimeEnemies();
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
			Run->SaveRun();
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
		RegisterEncounterRuntimeEnemies();
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

void AAscendPlayerController::RegisterEncounterRuntimeEnemies()
{
	if (!Combat || !Run) return;
	Combat->RegisterRuntimePlayerContent(Run->GetDynamicCards(), Run->GetDynamicRelics());

	TArray<FEnemyData> RuntimeEnemies;
	for (const FString& EnemyId : CurrentEncounter.EnemyIds)
	{
		if (!EnemyId.StartsWith(TEXT("proc_")) && !EnemyId.StartsWith(TEXT("llm_"))) continue;
		if (const FEnemyData* Data = Run->GetEnemyData(EnemyId)) RuntimeEnemies.Add(*Data);
	}
	if (RuntimeEnemies.Num() > 0)
	{
		Combat->RegisterRuntimeEnemies(RuntimeEnemies);
		UE_LOG(LogTemp, Display, TEXT("[Encounter] registered %d runtime enemy variants"), RuntimeEnemies.Num());
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
		if (Run->State.bInfiniteNarrativeMode)
		{
			Run->RecordInfiniteCombatDigest(CurrentEncounter.EnemyIds, Combat->TurnCount,
				Run->State.HP, Combat->Player.HP, CombatLogLines);
			// 模式 A 从第二场战斗起，在完整日志产生后立即连续执行两轮 LLM。
			if (Run->State.InfiniteCycle > 1 && InfiniteNarrativeSettings.bGenerateAfterCombatWithLog
				&& !bCombatNarrativePrefetchReady && !bInfiniteNarrativeRequestInFlight)
			{
				StartCombatNarrativePrefetch(true);
			}
		}
		if (Run->State.bInfiniteNarrativeMode) Run->ClearPendingInfiniteCombat();
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
		if (Run->State.bInfiniteNarrativeMode)
		{
			bDiscardCombatNarrativePrefetch = true;
			bWaitingForCombatNarrativeAfterReward = false;
		}
		Run->ResolveDefeat();
		Run->SaveRun();
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

#if PLATFORM_ANDROID
	// Android 使用触摸释放；卡牌按钮自身的 OnReleased 还会提供捕获范围外的兜底。
	InputComponent->BindKey(EKeys::TouchKeys[ETouchIndex::Touch1], IE_Released, this,
		&AAscendPlayerController::OnMouseLeftReleased);
#else
	// Mac/桌面端保持原有的单一鼠标释放路径。
	InputComponent->BindKey(EKeys::LeftMouseButton, IE_Released, this, &AAscendPlayerController::OnMouseLeftReleased);
#endif
	FInputKeyBinding& EscapeBinding = InputComponent->BindKey(EKeys::Escape, IE_Pressed, this,
		&AAscendPlayerController::OnEscapeKey);
	EscapeBinding.bExecuteWhenPaused = true;
}

void AAscendPlayerController::OnConfirmKey()
{
	UE_LOG(LogTemp, Display, TEXT("[KEY] 确认键按下，当前界面: %d"), (int32)CurrentScreen);
	if (CurrentScreen == EGameScreen::Title)
	{
		HandleClickAction(TEXT("title_infinite"), 0);
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

bool AAscendPlayerController::GetPointerCanvasPosition(FVector2D& OutPosition, bool& bOutTouchPressed) const
{
	bOutTouchPressed = false;
	if (!AnimCanvas) return false;

#if !PLATFORM_ANDROID
	// 桌面 Slate 的游标是绝对坐标，直接转换到 Canvas 局部坐标。
	// 不经过 Android 所需的 Viewport/Canvas 比例换算，避免 Retina/DPI 下二次缩放。
	const FGeometry CanvasGeometry = AnimCanvas->GetTickSpaceGeometry();
	const FVector2D CanvasSize = CanvasGeometry.GetLocalSize();
	if (CanvasSize.X <= 1.f || CanvasSize.Y <= 1.f) return false;
	OutPosition = CanvasGeometry.AbsoluteToLocal(FSlateApplication::Get().GetCursorPos());
	return FMath::IsFinite(OutPosition.X) && FMath::IsFinite(OutPosition.Y);
#else
	float ScreenX = 0.f;
	float ScreenY = 0.f;
	const FVector2D ViewportSize = GetViewportSize();
	// 不假定活跃手指一定是 Touch1；部分 Android/厂商触控层会保留一个
	// 位于 (0,0) 的 Touch1，同时把真实手指分配给后续索引。
	for (int32 TouchIndex = 0; TouchIndex < EKeys::NUM_TOUCH_KEYS; ++TouchIndex)
	{
		float CandidateX = 0.f;
		float CandidateY = 0.f;
		bool bCandidatePressed = false;
		GetInputTouchState(static_cast<ETouchIndex::Type>(TouchIndex),
			CandidateX, CandidateY, bCandidatePressed);
		if (!bCandidatePressed) continue;
		bOutTouchPressed = true;
		const bool bInsideViewport = CandidateX >= 0.f && CandidateY >= 0.f
			&& CandidateX <= ViewportSize.X && CandidateY <= ViewportSize.Y;
		const bool bNotBogusOrigin = CandidateX > 2.f || CandidateY > 2.f;
		if (bInsideViewport && bNotBogusOrigin)
		{
			ScreenX = CandidateX;
			ScreenY = CandidateY;
			break;
		}
	}

	const bool bHasValidTouchPosition = bOutTouchPressed && (ScreenX > 2.f || ScreenY > 2.f);
	if (!bHasValidTouchPosition && !GetMousePosition(ScreenX, ScreenY))
	{
		return false;
	}

	const FVector2D CanvasSize = AnimCanvas->GetTickSpaceGeometry().GetLocalSize();
	if (ViewportSize.X <= 1.f || ViewportSize.Y <= 1.f || CanvasSize.X <= 1.f || CanvasSize.Y <= 1.f)
	{
		return false;
	}

	// GetInputTouchState/GetMousePosition 返回游戏视口像素；Canvas Slot 使用 DPI 缩放后的
	// Slate 局部坐标。按两者尺寸比例换算，不依赖 Android 上不存在的系统鼠标游标。
	OutPosition.X = ScreenX * CanvasSize.X / ViewportSize.X;
	OutPosition.Y = ScreenY * CanvasSize.Y / ViewportSize.Y;
	return true;
#endif
}

void AAscendPlayerController::HandleRootPointerMoved(const FVector2D& ScreenSpacePosition)
{
	if (!bIsDraggingCard || !bDragUsingTouch || !AnimCanvas) return;
	const FGeometry CanvasGeometry = AnimCanvas->GetTickSpaceGeometry();
	const FVector2D CanvasSize = CanvasGeometry.GetLocalSize();
	const FVector2D LocalPosition = CanvasGeometry.AbsoluteToLocal(ScreenSpacePosition);
	if (!FMath::IsFinite(LocalPosition.X) || !FMath::IsFinite(LocalPosition.Y)) return;
	if (LocalPosition.X < -32.f || LocalPosition.Y < -32.f
		|| LocalPosition.X > CanvasSize.X + 32.f || LocalPosition.Y > CanvasSize.Y + 32.f)
	{
		return;
	}
	WidgetTouchCanvasPosition = LocalPosition;
	bHasWidgetTouchPosition = true;
}

float AAscendPlayerController::GetResponsiveHandScale(const FVector2D& CanvasSize) const
{
#if PLATFORM_ANDROID
	// 横屏手机的 Slate 逻辑高度通常在 600~800；略缩小静止手牌，为战场和状态栏让位。
	return CanvasSize.Y < 820.f ? 0.86f : 0.94f;
#else
	return (CanvasSize.Y < 680.f || CanvasSize.X < 1100.f) ? 0.90f : 1.f;
#endif
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

void AAscendPlayerController::BuildCardWidget(UButton* Btn, int32 CardIndex, bool bPlayable, float CardScale)
{
	Btn->SetBackgroundColor(FLinearColor::Transparent);
	Btn->SetContent(MakeCardContent(Btn, CardIndex, bPlayable, CardScale));
	if (!bPlayable) Btn->SetRenderOpacity(0.45f);
}

UWidget* AAscendPlayerController::MakeCardContent(UObject* Outer, int32 CardIndex, bool bPlayable, float Scale)
{
	if (!Combat || !Combat->Hand.IsValidIndex(CardIndex)) return NewObject<USpacer>(Outer);
	const FCardInstance& Card = Combat->Hand[CardIndex];
	const float S = Scale;
	{
		FString FixedDescription = (Card.bUpgraded && !Card.Data.UpgradedDescription.IsEmpty())
			? Card.Data.UpgradedDescription : Card.Data.Description;
		if (Card.Data.Id == TEXT("one_sword"))
		{
			FixedDescription = FString::Printf(TEXT("对所有敌人造成 %d 点伤害（每层强化+6）。保留，消失"),
				Combat->GetOneSwordDamage());
		}
		else if (Card.Data.Id == TEXT("wan_jian_gui_zong"))
		{
			const int32 Base = Card.bUpgraded ? 5 : 3;
			const int32 Increment = Card.bUpgraded ? 4 : 3;
			FixedDescription = FString::Printf(TEXT("对所有敌人造成 %d 点伤害（每用一次+%d）已用%d次。保留"),
				Base + Card.RepeatCount * Increment, Increment, Card.RepeatCount);
		}
		else if (Card.RepeatCount > 0)
		{
			FixedDescription = FString::Printf(TEXT("%s (当前重复%d次)"), *Card.Data.Description, Card.RepeatCount);
		}

		const TArray<FCardEffect>& Effects = Card.GetEffects();
		for (int32 Index = 0; Index < Effects.Num(); ++Index)
		{
			FixedDescription.ReplaceInline(*FString::Printf(TEXT("{effect%d}"), Index),
				*FString::FromInt(Combat->ResolveEffectValue(Effects[Index], &Card)));
		}
		FixedDescription.ReplaceInline(TEXT("{counter}"), *FString::FromInt(Card.RepeatCount));
		FixedDescription.ReplaceInline(TEXT("{hand_size}"), *FString::FromInt(Combat->Hand.Num()));
		FixedDescription.ReplaceInline(TEXT("{draw_pile}"), *FString::FromInt(Combat->DrawPile.Num()));
		FixedDescription.ReplaceInline(TEXT("{discard_pile}"), *FString::FromInt(Combat->DiscardPile.Num()));
		const TArray<FString> DynamicStatuses = {
			TEXT("strength"), TEXT("dexterity"), TEXT("weak"), TEXT("vulnerable"),
			TEXT("burn"), TEXT("poison"), TEXT("nightmare"), TEXT("temp_strength")
		};
		for (const FString& StatusId : DynamicStatuses)
		{
			FixedDescription.ReplaceInline(*FString::Printf(TEXT("{stacks:%s}"), *StatusId),
				*FString::FromInt(Combat->Player.GetStatusStacks(StatusId)));
		}

		const int32 EffectiveCost = Combat->GetEffectiveCost(Card);
		const FLinearColor CostColor = EffectiveCost < Card.GetCost()
			? FLinearColor(0.72f, 1.f, 0.72f, 1.f)
			: (EffectiveCost > Card.GetCost()
				? FLinearColor(1.f, 0.72f, 0.65f, 1.f)
				: FLinearColor(1.f, 0.94f, 0.68f, 1.f));
		return BuildFixedCardFace(Outer, Card.Data.ArtPath, FAscendUIStyle::CardTypeColor(Card.Data.Type),
			Card.GetDisplayName(), FixedDescription, EffectiveCost, CostColor,
			Card.Data.bRetain, Card.Data.bExhaust, S);
	}

#if 0 // Legacy adaptive 162x203 layout retained only for reference during the visual migration.
	// Size box for fixed card dimensions
	USizeBox* CardSizer = NewObject<USizeBox>(Outer);
	CardSizer->SetWidthOverride(162.f * S);
	CardSizer->SetHeightOverride(203.f * S);

	const FLinearColor TypeCol = FAscendUIStyle::CardTypeColor(Card.Data.Type);

	// 新卡框作为独立底层，内容留出玉石边框宽度。它不参与命中测试，避免触控被装饰图截获。
	UOverlay* CardRoot = NewObject<UOverlay>(CardSizer);
	UBorder* CardBase = NewObject<UBorder>(CardRoot);
	CardBase->SetBrushColor(FLinearColor(0.025f, 0.070f, 0.060f, 1.f));
	UOverlaySlot* BaseSlot = CardRoot->AddChildToOverlay(CardBase);
	BaseSlot->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Fill);
	BaseSlot->SetVerticalAlignment(EVerticalAlignment::VAlign_Fill);
	UImage* FrameArt = FAscendArt::MakeImage(CardRoot, TEXT("Art/ui/card_frame.png"));
	if (FrameArt)
	{
		FrameArt->SetVisibility(ESlateVisibility::HitTestInvisible);
		UOverlaySlot* FrameArtSlot = CardRoot->AddChildToOverlay(FrameArt);
		FrameArtSlot->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Fill);
		FrameArtSlot->SetVerticalAlignment(EVerticalAlignment::VAlign_Fill);
	}

	// Card face
	UBorder* Face = NewObject<UBorder>(CardRoot);
	// 新卡框本身已经带有完整的底板与文字区，旧灰色 Face 会把美术底板整个盖住。
	Face->SetBrushColor(FLinearColor::Transparent);
	UOverlaySlot* FaceSlot = CardRoot->AddChildToOverlay(Face);
	FaceSlot->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Fill);
	FaceSlot->SetVerticalAlignment(EVerticalAlignment::VAlign_Fill);
	FaceSlot->SetPadding(FMargin(16.f * S, 16.f * S, 16.f * S, 14.f * S));

	UVerticalBox* VBox = NewObject<UVerticalBox>(Face);

	// --- Art area (fill remaining) ---
	UBorder* ArtArea = NewObject<UBorder>(VBox);
	ArtArea->SetClipping(EWidgetClipping::ClipToBounds);
	ArtArea->SetBrushColor(FAscendArt::Exists(Card.Data.ArtPath)
		? FLinearColor::Transparent
		: (TypeCol * 0.22f + FLinearColor(0.07f, 0.09f, 0.08f) * 0.78f));
	UVerticalBoxSlot* ArtSlot = VBox->AddChildToVerticalBox(ArtArea);
	FSlateChildSize FillAll(ESlateSizeRule::Fill);
	FillAll.Value = 0.54f;
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

	// 灵气费用独立放在卡图左上角，不再跟在卡名后面。
	const int32 EffCost = Combat ? Combat->GetEffectiveCost(Card) : Card.GetCost();
	const float CostUIScale = FMath::Max(S, 0.82f); // 小屏也保持足够大的费用读数
	USizeBox* GemSizer = NewObject<USizeBox>(ArtOvl);
	GemSizer->SetWidthOverride(40.f * CostUIScale);
	GemSizer->SetHeightOverride(48.f * CostUIScale);
	UOverlay* CostGem = NewObject<UOverlay>(GemSizer);
	if (UImage* GemArt = FAscendArt::MakeImage(CostGem, TEXT("Art/ui/spirit_gem.png")))
	{
		GemArt->SetVisibility(ESlateVisibility::HitTestInvisible);
		UOverlaySlot* GemArtSlot = CostGem->AddChildToOverlay(GemArt);
		GemArtSlot->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Fill);
		GemArtSlot->SetVerticalAlignment(EVerticalAlignment::VAlign_Fill);
	}
	const FLinearColor CostColor = EffCost < Card.GetCost()
		? FLinearColor(0.72f, 1.f, 0.72f, 1.f)
		: (EffCost > Card.GetCost() ? FLinearColor(1.f, 0.72f, 0.65f, 1.f) : FLinearColor(1.f, 0.94f, 0.68f, 1.f));
	UTextBlock* CostT = FAscendUIStyle::MakeText(CostGem, FString::FromInt(EffCost),
		FMath::Clamp(FMath::RoundToInt(21.f * S), 18, 25), CostColor);
	CostT->SetJustification(ETextJustify::Center);
	CostT->SetShadowOffset(FVector2D(2.f, 2.f));
	CostT->SetShadowColorAndOpacity(FLinearColor(0.f, 0.025f, 0.06f, 1.f));
	FSlateFontInfo CostFont = CostT->GetFont();
	CostFont.OutlineSettings.OutlineSize = 2;
	CostFont.OutlineSettings.OutlineColor = FLinearColor(0.f, 0.025f, 0.06f, 1.f);
	CostT->SetFont(CostFont);
	UOverlaySlot* CostTextSlot = CostGem->AddChildToOverlay(CostT);
	CostTextSlot->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Fill);
	CostTextSlot->SetVerticalAlignment(EVerticalAlignment::VAlign_Center);
	GemSizer->SetContent(CostGem);
	UOverlaySlot* CostOuterSlot = ArtOvl->AddChildToOverlay(GemSizer);
	CostOuterSlot->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Left);
	CostOuterSlot->SetVerticalAlignment(EVerticalAlignment::VAlign_Top);
	CostOuterSlot->SetPadding(FMargin(2.f * S));

	// 保留/消耗徽标（左上角小字，叠在 ArtArea 内）
	if (Card.Data.bRetain || Card.Data.bExhaust)
	{
		UTextBlock* Badge = FAscendUIStyle::MakeText(ArtOvl,
			Card.Data.bRetain ? TEXT("留") : TEXT("耗"), FMath::RoundToInt(11.f * S),
			Card.Data.bRetain ? FAscendUIStyle::JadeGreen() : FAscendUIStyle::GoldYellow());
		Badge->SetJustification(ETextJustify::Left);
		Badge->SetMargin(FMargin(3.f * S, 2.f * S, 0.f, 0.f));
		UOverlaySlot* BadgeSlot = ArtOvl->AddChildToOverlay(Badge);
		BadgeSlot->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Right);
		BadgeSlot->SetVerticalAlignment(EVerticalAlignment::VAlign_Top);
		BadgeSlot->SetPadding(FMargin(0.f, 2.f * S, 3.f * S, 0.f));
	}
	ArtArea->SetContent(ArtOvl);

	// --- Text area (bottom, fixed) ---
	UBorder* TextBg = NewObject<UBorder>(VBox);
	TextBg->SetClipping(EWidgetClipping::ClipToBounds);
	TextBg->SetPadding(FMargin(7.f * S, 6.f * S, 7.f * S, 6.f * S));
	TextBg->SetBrushColor(FLinearColor(0.035f, 0.085f, 0.070f, 0.99f));

	UVerticalBox* TextBox = NewObject<UVerticalBox>(TextBg);

	// 卡名独占一行，居中显示。
	UHorizontalBox* NameRow = NewObject<UHorizontalBox>(TextBox);
	UTextBlock* NameT = FAscendUIStyle::MakeText(NameRow, Card.GetDisplayName(),
		FMath::RoundToInt(13.f * S), FAscendUIStyle::GoldYellow());
	NameT->SetJustification(ETextJustify::Center);
	UHorizontalBoxSlot* NameSlot = NameRow->AddChildToHorizontalBox(NameT);
	NameSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	NameSlot->SetPadding(FMargin(4.f * S, 1.f * S));

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
	if (Combat)
	{
		const TArray<FCardEffect>& Effects = Card.GetEffects();
		for (int32 Index = 0; Index < Effects.Num(); ++Index)
		{
			DisplayDesc.ReplaceInline(*FString::Printf(TEXT("{effect%d}"), Index),
				*FString::FromInt(Combat->ResolveEffectValue(Effects[Index], &Card)));
		}
		DisplayDesc.ReplaceInline(TEXT("{counter}"), *FString::FromInt(Card.RepeatCount));
		DisplayDesc.ReplaceInline(TEXT("{hand_size}"), *FString::FromInt(Combat->Hand.Num()));
		DisplayDesc.ReplaceInline(TEXT("{draw_pile}"), *FString::FromInt(Combat->DrawPile.Num()));
		DisplayDesc.ReplaceInline(TEXT("{discard_pile}"), *FString::FromInt(Combat->DiscardPile.Num()));
		const TArray<FString> DynamicStatuses = {
			TEXT("strength"), TEXT("dexterity"), TEXT("weak"), TEXT("vulnerable"),
			TEXT("burn"), TEXT("poison"), TEXT("nightmare"), TEXT("temp_strength")
		};
		for (const FString& StatusId : DynamicStatuses)
		{
			DisplayDesc.ReplaceInline(*FString::Printf(TEXT("{stacks:%s}"), *StatusId),
				*FString::FromInt(Combat->Player.GetStatusStacks(StatusId)));
		}
	}
	UTextBlock* DescT = FAscendUIStyle::MakeText(TextBox, DisplayDesc,
		FMath::RoundToInt(10.f * S), FAscendUIStyle::PaperWhite());
	DescT->SetAutoWrapText(true);
	DescT->SetWrapTextAt(122.f * S);
	DescT->SetJustification(ETextJustify::Center);
	UScaleBox* DescScale = NewObject<UScaleBox>(TextBox);
	DescScale->SetStretch(EStretch::ScaleToFit);
	DescScale->SetStretchDirection(EStretchDirection::DownOnly);
	DescScale->SetContent(DescT);
	UVerticalBoxSlot* DescSlot = TextBox->AddChildToVerticalBox(DescScale);
	DescSlot->SetPadding(FMargin(3.f * S, 2.f * S));
	DescSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));

	TextBg->SetContent(TextBox);
	UVerticalBoxSlot* TextSlot = VBox->AddChildToVerticalBox(TextBg);
	FSlateChildSize TextFill(ESlateSizeRule::Fill);
	TextFill.Value = 0.46f;
	TextSlot->SetSize(TextFill);

	Face->SetContent(VBox);
	CardSizer->SetContent(CardRoot);
	return CardSizer;
#endif
}

// -----------------------------------------------------------
// 从卡牌数据构建完整卡面（奖励/坊市等）
// -----------------------------------------------------------

UWidget* AAscendPlayerController::MakeCardContentFromData(UObject* Outer, const FCardData& CardData, bool bUpgraded, float Scale)
{
	const float S = Scale;
	{
		FString FixedDescription = bUpgraded && !CardData.UpgradedDescription.IsEmpty()
			? CardData.UpgradedDescription : CardData.Description;
		const TArray<FCardEffect>& PreviewEffects = bUpgraded && CardData.UpgradedEffects.Num() > 0
			? CardData.UpgradedEffects : CardData.Effects;
		for (int32 Index = 0; Index < PreviewEffects.Num(); ++Index)
		{
			FixedDescription.ReplaceInline(*FString::Printf(TEXT("{effect%d}"), Index),
				*FString::FromInt(PreviewEffects[Index].Value));
		}
		FixedDescription.ReplaceInline(TEXT("{counter}"), TEXT("0"));
		return BuildFixedCardFace(Outer, CardData.ArtPath, FAscendUIStyle::CardTypeColor(CardData.Type),
			bUpgraded ? (CardData.Name + TEXT("+")) : CardData.Name,
			FixedDescription, CardData.Cost, FLinearColor(1.f, 0.94f, 0.68f, 1.f),
			CardData.bRetain, CardData.bExhaust, S);
	}

#if 0 // Legacy adaptive 162x203 layout retained only for reference during the visual migration.
	USizeBox* CardSizer = NewObject<USizeBox>(Outer);
	CardSizer->SetWidthOverride(162.f * S);
	CardSizer->SetHeightOverride(203.f * S);

	const FLinearColor TypeCol = FAscendUIStyle::CardTypeColor(CardData.Type);

	UOverlay* CardRoot = NewObject<UOverlay>(CardSizer);
	UBorder* CardBase = NewObject<UBorder>(CardRoot);
	CardBase->SetBrushColor(FLinearColor(0.025f, 0.070f, 0.060f, 1.f));
	UOverlaySlot* BaseSlot = CardRoot->AddChildToOverlay(CardBase);
	BaseSlot->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Fill);
	BaseSlot->SetVerticalAlignment(EVerticalAlignment::VAlign_Fill);
	UImage* FrameArt = FAscendArt::MakeImage(CardRoot, TEXT("Art/ui/card_frame.png"));
	if (FrameArt)
	{
		FrameArt->SetVisibility(ESlateVisibility::HitTestInvisible);
		UOverlaySlot* FrameArtSlot = CardRoot->AddChildToOverlay(FrameArt);
		FrameArtSlot->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Fill);
		FrameArtSlot->SetVerticalAlignment(EVerticalAlignment::VAlign_Fill);
	}

	UBorder* Face = NewObject<UBorder>(CardRoot);
	Face->SetBrushColor(FLinearColor::Transparent);
	UOverlaySlot* FaceSlot = CardRoot->AddChildToOverlay(Face);
	FaceSlot->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Fill);
	FaceSlot->SetVerticalAlignment(EVerticalAlignment::VAlign_Fill);
	FaceSlot->SetPadding(FMargin(16.f * S, 16.f * S, 16.f * S, 14.f * S));

	UVerticalBox* VBox = NewObject<UVerticalBox>(Face);

	UBorder* ArtArea = NewObject<UBorder>(VBox);
	ArtArea->SetClipping(EWidgetClipping::ClipToBounds);
	ArtArea->SetBrushColor(FAscendArt::Exists(CardData.ArtPath)
		? FLinearColor::Transparent
		: (TypeCol * 0.22f + FLinearColor(0.07f, 0.09f, 0.08f) * 0.78f));
	UVerticalBoxSlot* ArtSlot = VBox->AddChildToVerticalBox(ArtArea);
	FSlateChildSize FillAll(ESlateSizeRule::Fill);
	FillAll.Value = 0.54f;
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

	const float CostUIScale = FMath::Max(S, 0.82f);
	USizeBox* GemSizer = NewObject<USizeBox>(ArtOvl);
	GemSizer->SetWidthOverride(40.f * CostUIScale);
	GemSizer->SetHeightOverride(48.f * CostUIScale);
	UOverlay* CostGem = NewObject<UOverlay>(GemSizer);
	if (UImage* GemArt = FAscendArt::MakeImage(CostGem, TEXT("Art/ui/spirit_gem.png")))
	{
		GemArt->SetVisibility(ESlateVisibility::HitTestInvisible);
		UOverlaySlot* GemArtSlot = CostGem->AddChildToOverlay(GemArt);
		GemArtSlot->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Fill);
		GemArtSlot->SetVerticalAlignment(EVerticalAlignment::VAlign_Fill);
	}
	UTextBlock* CostT = FAscendUIStyle::MakeText(CostGem, FString::FromInt(CardData.Cost),
		FMath::Clamp(FMath::RoundToInt(21.f * S), 18, 25), FLinearColor(1.f, 0.94f, 0.68f, 1.f));
	CostT->SetJustification(ETextJustify::Center);
	CostT->SetShadowOffset(FVector2D(2.f, 2.f));
	CostT->SetShadowColorAndOpacity(FLinearColor(0.f, 0.025f, 0.06f, 1.f));
	FSlateFontInfo CostFont = CostT->GetFont();
	CostFont.OutlineSettings.OutlineSize = 2;
	CostFont.OutlineSettings.OutlineColor = FLinearColor(0.f, 0.025f, 0.06f, 1.f);
	CostT->SetFont(CostFont);
	UOverlaySlot* CostTextSlot = CostGem->AddChildToOverlay(CostT);
	CostTextSlot->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Fill);
	CostTextSlot->SetVerticalAlignment(EVerticalAlignment::VAlign_Center);
	GemSizer->SetContent(CostGem);
	UOverlaySlot* CostOuterSlot = ArtOvl->AddChildToOverlay(GemSizer);
	CostOuterSlot->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Left);
	CostOuterSlot->SetVerticalAlignment(EVerticalAlignment::VAlign_Top);
	CostOuterSlot->SetPadding(FMargin(2.f * S));

	if (CardData.bRetain || CardData.bExhaust)
	{
		UTextBlock* Badge = FAscendUIStyle::MakeText(ArtOvl,
			CardData.bRetain ? TEXT("留") : TEXT("耗"), FMath::RoundToInt(11.f * S),
			CardData.bRetain ? FAscendUIStyle::JadeGreen() : FAscendUIStyle::GoldYellow());
		Badge->SetJustification(ETextJustify::Left);
		Badge->SetMargin(FMargin(3.f * S, 2.f * S, 0.f, 0.f));
		UOverlaySlot* BadgeSlot = ArtOvl->AddChildToOverlay(Badge);
		BadgeSlot->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Right);
		BadgeSlot->SetVerticalAlignment(EVerticalAlignment::VAlign_Top);
		BadgeSlot->SetPadding(FMargin(0.f, 2.f * S, 3.f * S, 0.f));
	}
	ArtArea->SetContent(ArtOvl);

	UBorder* TextBg = NewObject<UBorder>(VBox);
	TextBg->SetClipping(EWidgetClipping::ClipToBounds);
	TextBg->SetPadding(FMargin(7.f * S, 6.f * S, 7.f * S, 6.f * S));
	TextBg->SetBrushColor(FLinearColor(0.035f, 0.085f, 0.070f, 0.99f));

	UVerticalBox* TextBox = NewObject<UVerticalBox>(TextBg);

	UHorizontalBox* NameRow = NewObject<UHorizontalBox>(TextBox);
	FString DisplayName = bUpgraded ? (CardData.Name + TEXT("+")) : CardData.Name;
	UTextBlock* NameT = FAscendUIStyle::MakeText(NameRow, DisplayName,
		FMath::RoundToInt(13.f * S), FAscendUIStyle::GoldYellow());
	NameT->SetJustification(ETextJustify::Center);
	UHorizontalBoxSlot* NameSlot = NameRow->AddChildToHorizontalBox(NameT);
	NameSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	NameSlot->SetPadding(FMargin(4.f * S, 1.f * S));

	TextBox->AddChildToVerticalBox(NameRow);

	FString DisplayDesc = bUpgraded && !CardData.UpgradedDescription.IsEmpty()
		? CardData.UpgradedDescription : CardData.Description;
	const TArray<FCardEffect>& PreviewEffects = bUpgraded && CardData.UpgradedEffects.Num() > 0
		? CardData.UpgradedEffects : CardData.Effects;
	for (int32 Index = 0; Index < PreviewEffects.Num(); ++Index)
	{
		DisplayDesc.ReplaceInline(*FString::Printf(TEXT("{effect%d}"), Index),
			*FString::FromInt(PreviewEffects[Index].Value));
	}
	DisplayDesc.ReplaceInline(TEXT("{counter}"), TEXT("0"));
	UTextBlock* DescT = FAscendUIStyle::MakeText(TextBox, DisplayDesc,
		FMath::RoundToInt(10.f * S), FAscendUIStyle::PaperWhite());
	DescT->SetAutoWrapText(true);
	DescT->SetWrapTextAt(122.f * S);
	DescT->SetJustification(ETextJustify::Center);
	UScaleBox* DescScale = NewObject<UScaleBox>(TextBox);
	DescScale->SetStretch(EStretch::ScaleToFit);
	DescScale->SetStretchDirection(EStretchDirection::DownOnly);
	DescScale->SetContent(DescT);
	UVerticalBoxSlot* DescSlot = TextBox->AddChildToVerticalBox(DescScale);
	DescSlot->SetPadding(FMargin(3.f * S, 2.f * S));
	DescSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));

	TextBg->SetContent(TextBox);
	UVerticalBoxSlot* TextSlot = VBox->AddChildToVerticalBox(TextBg);
	FSlateChildSize TextFill(ESlateSizeRule::Fill);
	TextFill.Value = 0.46f;
	TextSlot->SetSize(TextFill);

	Face->SetContent(VBox);
	CardSizer->SetContent(CardRoot);
	return CardSizer;
#endif
}

// -----------------------------------------------------------
// 手牌悬停放大（原牌立正、竖直上抬，不创建额外预览牌）
// -----------------------------------------------------------

void AAscendPlayerController::ShowCardPreview(int32 CardIndex)
{
	if (!Combat || !Combat->Hand.IsValidIndex(CardIndex)) return;
	if (!HandCardButtons.IsValidIndex(CardIndex) || !HandCardButtons[CardIndex].IsValid()) return;
	if (bIsDraggingCard) return; // 拖拽时不弹预览

	HideCardPreview();

	UButton* HoveredCard = HandCardButtons[CardIndex].Get();
	UWidget* CardVisual = HoveredCard->GetContent();
	if (!CardVisual) return;

	HoveredCardIndex = CardIndex;
	HoveredCardOriginalAngle = CardVisual->GetRenderTransform().Angle;
#if PLATFORM_ANDROID
	const float PreviewScale = TouchHandHoverScale;
	const float PreviewLift = TouchHandHoverLift;
#else
	const float PreviewScale = HandHoverScale;
	const float PreviewLift = HandHoverLift;
#endif
	// 若鼠标在入场动画尚未结束时进入，立即固定按钮命中区域到最终位置。
	HoveredCard->SetRenderTranslation(FVector2D::ZeroVector);
	HoveredCard->SetRenderScale(FVector2D(1.f, 1.f));
	HoveredCard->SetRenderOpacity(1.f);
	CardVisual->SetRenderTransformAngle(0.f);
	CardVisual->SetRenderTranslation(FVector2D(0.f, -PreviewLift));
	CardVisual->SetRenderScale(FVector2D(PreviewScale, PreviewScale));
	if (UCanvasPanelSlot* Slot = Cast<UCanvasPanelSlot>(HoveredCard->Slot))
	{
		Slot->SetZOrder(HandHoverZOrder);
	}
}

void AAscendPlayerController::HideCardPreview(int32 CardIndex)
{
	// 快速扫过重叠手牌时，旧卡的 Unhover 不应取消新卡的悬停状态。
	if (CardIndex != INDEX_NONE && HoveredCardIndex != CardIndex) return;

	if (HandCardButtons.IsValidIndex(HoveredCardIndex) && HandCardButtons[HoveredCardIndex].IsValid())
	{
		UButton* HoveredCard = HandCardButtons[HoveredCardIndex].Get();
		if (UWidget* CardVisual = HoveredCard->GetContent())
		{
			CardVisual->SetRenderTransformAngle(HoveredCardOriginalAngle);
			CardVisual->SetRenderTranslation(FVector2D::ZeroVector);
			CardVisual->SetRenderScale(FVector2D(1.f, 1.f));
		}
		if (UCanvasPanelSlot* Slot = Cast<UCanvasPanelSlot>(HoveredCard->Slot))
		{
			Slot->SetZOrder(HoveredCardIndex);
		}
	}
	HoveredCardIndex = INDEX_NONE;
	HoveredCardOriginalAngle = 0.f;
}

void AAscendPlayerController::BuildEnemyCardWidget(UVerticalBox* EBox, int32 EnemyIndex, bool bIsLocked)
{
	if (!Combat || !Combat->Enemies.IsValidIndex(EnemyIndex)) return;
	const FEnemyCombatant& E = Combat->Enemies[EnemyIndex];
	const bool bAlive = E.State.IsAlive();

	USizeBox* Sizer = NewObject<USizeBox>(EBox);
	Sizer->SetWidthOverride(150.f);
	Sizer->SetHeightOverride(210.f);

	UOverlay* EnemyRoot = NewObject<UOverlay>(Sizer);
	if (UImage* FrameArt = FAscendArt::MakeImage(EnemyRoot, TEXT("Art/ui/card_frame.png")))
	{
		FrameArt->SetVisibility(ESlateVisibility::HitTestInvisible);
		FrameArt->SetColorAndOpacity(bIsLocked
			? FLinearColor(1.f, 0.54f, 0.42f, 1.f)
			: (bAlive ? FLinearColor(0.78f, 0.66f, 0.58f, 1.f) : FLinearColor(0.38f, 0.38f, 0.38f, 1.f)));
		UOverlaySlot* FrameSlot = EnemyRoot->AddChildToOverlay(FrameArt);
		FrameSlot->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Fill);
		FrameSlot->SetVerticalAlignment(EVerticalAlignment::VAlign_Fill);
	}

	UBorder* Face = NewObject<UBorder>(EnemyRoot);
	Face->SetBrushColor(FLinearColor::Transparent);
	UOverlaySlot* FaceSlot = EnemyRoot->AddChildToOverlay(Face);
	FaceSlot->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Fill);
	FaceSlot->SetVerticalAlignment(EVerticalAlignment::VAlign_Fill);
	FaceSlot->SetPadding(FMargin(10.f, 12.f, 10.f, 10.f));

	UVerticalBox* V = NewObject<UVerticalBox>(Face);

	// Art area (fill)——立绘 + 死亡时显示「已击杀」
	UBorder* Art = NewObject<UBorder>(V);
	Art->SetBrushColor(FAscendArt::Exists(E.Data.ArtPath)
		? FLinearColor::Transparent : FLinearColor(0.07f, 0.06f, 0.055f, 0.92f));
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
	TextBg->SetBrushColor(FLinearColor::Transparent);
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
	Sizer->SetContent(EnemyRoot);
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
		FString Label = Style ? FString::Printf(TEXT("%s %d"), *Style->Key, S.Stacks) : FString::Printf(TEXT("%s %d"), *S.Id, S.Stacks);
		FLinearColor Col = Style ? Style->Value : FLinearColor(0.50f, 0.50f, 0.50f);
		const bool bLargeHudBadge = FontSize >= 20;
		USizeBox* BadgeSize = NewObject<USizeBox>(Row);
		BadgeSize->SetWidthOverride(bLargeHudBadge ? 64.f : 48.f);
		BadgeSize->SetHeightOverride(bLargeHudBadge ? 38.f : 23.f);
		UBorder* BadgeFrame = NewObject<UBorder>(BadgeSize);
		BadgeFrame->SetBrushColor(Col * 0.82f);
		BadgeFrame->SetPadding(FMargin(2.f));
		UBorder* BadgeInner = NewObject<UBorder>(BadgeFrame);
		BadgeInner->SetBrushColor(FLinearColor(0.045f, 0.055f, 0.055f, 0.94f));
		BadgeInner->SetPadding(FMargin(4.f, 1.f));
		UScaleBox* BadgeTextScale = NewObject<UScaleBox>(BadgeInner);
		BadgeTextScale->SetStretch(EStretch::ScaleToFit);
		BadgeTextScale->SetStretchDirection(EStretchDirection::DownOnly);
		UTextBlock* T = FAscendUIStyle::MakeText(BadgeTextScale, Label,
			bLargeHudBadge ? FMath::Min(FontSize, 21) : FontSize, FAscendUIStyle::PaperWhite());
		T->SetJustification(ETextJustify::Center);
		T->SetShadowOffset(FVector2D(1.f, 1.f));
		BadgeTextScale->SetContent(T);
		BadgeInner->SetContent(BadgeTextScale);
		BadgeFrame->SetContent(BadgeInner);
		BadgeSize->SetContent(BadgeFrame);
		UHorizontalBoxSlot* Slot = Row->AddChildToHorizontalBox(BadgeSize);
		Slot->SetPadding(FMargin(0.f, 0.f, 6.f, 0.f));
		Slot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
	}
	return Row;
}

void AAscendPlayerController::HandleCardPressed(int32 CardIndex)
{
	if (!Combat || !Combat->bCombatActive) return;
	if (Combat->PendingDiscoverChoices.Num() > 0) return;
	if (Combat->bPlayerTurnSkipped) return;
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

	FVector2D PointerPosition = FVector2D::ZeroVector;
	bool bTouchPressed = false;
	const bool bHasPointer = GetPointerCanvasPosition(PointerPosition, bTouchPressed);
#if PLATFORM_ANDROID
	bTouchPressed = true;
#else
	// Mac 鼠标永远使用即时拖拽，不应用移动端的轻触预览阈值。
	bTouchPressed = false;
#endif
	if (bTouchPressed)
	{
		// 轻触先在原位展示大卡；只有手指移动超过阈值后才切换为拖拽幽灵牌。
		ShowCardPreview(CardIndex);
	}

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
	bDragUsingTouch = bTouchPressed;
	bHasWidgetTouchPosition = false;
	DragStartMousePos = bHasPointer ? PointerPosition : FVector2D::ZeroVector;
	if (bDragUsingTouch && AnimCanvas && HandCardButtons.IsValidIndex(CardIndex)
		&& HandCardButtons[CardIndex].IsValid())
	{
		const FGeometry CardGeometry = HandCardButtons[CardIndex]->GetCachedGeometry();
		const FVector2D CardAbsoluteCenter = CardGeometry.LocalToAbsolute(CardGeometry.GetLocalSize() * 0.5f);
		DragStartMousePos = AnimCanvas->GetTickSpaceGeometry().AbsoluteToLocal(CardAbsoluteCenter);
	}
}

void AAscendPlayerController::UpdateCardDrag()
{
	if (!AnimCanvas || !GetWorld()) return;

	FVector2D MousePos;
	bool bTouchPressed = false;
	if (bDragUsingTouch)
	{
		// 触摸拖牌只信任 UMG PointerEvent。未收到真实移动事件时保持原牌放大，
		// 绝不根据 Android 的兼容鼠标/伪 Touch(0,0) 创建幽灵牌。
		if (!bHasWidgetTouchPosition) return;
		MousePos = WidgetTouchCanvasPosition;
		bTouchPressed = true;
	}
	else if (!GetPointerCanvasPosition(MousePos, bTouchPressed))
	{
		return;
	}
	bDragUsingTouch = bDragUsingTouch || bTouchPressed;

	if (!DraggedCardWidget)
	{
		if (bDragUsingTouch && FVector2D::Distance(MousePos, DragStartMousePos) < TouchDragThreshold)
		{
			return;
		}
		HideCardPreview();

		int32 NCards = Combat->Hand.Num();
		FVector2D VP = GetViewportSize();
		FGeometry AG = AnimCanvas->GetTickSpaceGeometry();
		FVector2D CAbs = AG.GetAbsolutePosition();

		UE_LOG(LogTemp, Display, TEXT("[DRAG] Canvas local=%.0fx%.0f abs=%.0f,%.0f absSize=%.0fx%.0f"),
			AG.GetLocalSize().X, AG.GetLocalSize().Y,
			CAbs.X, CAbs.Y,
			AG.GetAbsoluteSize().X, AG.GetAbsoluteSize().Y);

		// 与 RefreshCombatPanel 的底部扇形牌列使用同一组局部坐标，
		// 这样拖拽幽灵牌的初始位置不会跳到旧的 1280x720 绝对坐标。
		const float CardW = AscendCardLayout::Width * CurrentHandCardScale;
		const float CardH = AscendCardLayout::Height * CurrentHandCardScale;
		const bool bCompactHand = CurrentHandCardScale < 0.99f;
		const float HandBottomMargin = bCompactHand
			? AscendCardLayout::CompactHandBottomMargin : AscendCardLayout::HandBottomMargin;
		FVector2D CanvasSize = AG.GetLocalSize();
		if (CanvasSize.X < 640.f || CanvasSize.Y < 360.f)
			CanvasSize = FVector2D(1920.f, 1080.f);
		const float FanAngle = bCompactHand ? 46.f : 40.f;
		const float FanRadius = CanvasSize.Y * (bCompactHand ? 0.30f : 0.35f);
		const float FanCenterX = CanvasSize.X * 0.5f;
		const float FanCenterY = CanvasSize.Y - HandBottomMargin + FanRadius;
		const float AngleDeg = (NCards > 1)
			? (static_cast<float>(DragCardIndex) / (NCards - 1) - 0.5f) * FanAngle : 0.f;
		const float AngleRad = FMath::DegreesToRadians(AngleDeg);
		const float Bx = FanCenterX + FanRadius * FMath::Sin(AngleRad);
		const float By = FanCenterY - FanRadius * FMath::Cos(AngleRad);
		FVector2D HandLocal(Bx - CardW * 0.5f, By - CardH);
		if (HandCardButtons.IsValidIndex(DragCardIndex) && HandCardButtons[DragCardIndex].IsValid())
		{
			// 以原牌真实几何位置为准，避免安全区、DPI 或屏幕比例改变时重新计算发生漂移。
			HandLocal = AG.AbsoluteToLocal(HandCardButtons[DragCardIndex]->GetCachedGeometry().GetAbsolutePosition());
		}

		UE_LOG(LogTemp, Display, TEXT("[DRAG] VP=%.0fx%.0f NCards=%d HandLocal=%.0f,%.0f"),
			VP.X, VP.Y, NCards, HandLocal.X, HandLocal.Y);

		UButton* Ghost = NewObject<UButton>(AnimCanvas);
		BuildCardWidget(Ghost, DragCardIndex, true, CurrentHandCardScale);

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
		const float DragScale = bDragUsingTouch ? TouchDragScale : MouseDragScale;
		const float DragLift = bDragUsingTouch ? 54.f : 35.f;
		HBorder->SetRenderTransformPivot(FVector2D(0.5f, 0.5f));
		HBorder->SetRenderScale(FVector2D(DragScale, DragScale));
		HBorder->SetRenderTranslation(FVector2D(0.f, -DragLift));
		HBorder->SetRenderOpacity(0.92f);

		// Hide original card
		if (HandCardButtons.IsValidIndex(DragCardIndex) && HandCardButtons[DragCardIndex].IsValid())
			HandCardButtons[DragCardIndex]->SetRenderOpacity(0.f);
	}
	else
	{
		if (UCanvasPanelSlot* Slot = Cast<UCanvasPanelSlot>(DraggedCardWidget->Slot))
		{
			const FVector2D CardHalfSize(
				AscendCardLayout::HalfWidth * CurrentHandCardScale,
				AscendCardLayout::HalfHeight * CurrentHandCardScale);
			Slot->SetPosition(MousePos - CardHalfSize);
		}
		const float DragScale = bDragUsingTouch ? TouchDragScale : MouseDragScale;
		const float DragLift = bDragUsingTouch ? 54.f : 35.f;
		DraggedCardWidget->SetRenderScale(FVector2D(DragScale, DragScale));
		DraggedCardWidget->SetRenderTranslation(FVector2D(0.f, -DragLift));
		DragLastCardCenter = MousePos;

		// Play zone detection
		FGeometry AG = AnimCanvas->GetTickSpaceGeometry();
		FVector2D CanvasSize = AG.GetLocalSize();
		if (CanvasSize.X < 640.f || CanvasSize.Y < 360.f)
			CanvasSize = FVector2D(1920.f, 1080.f);
		const float HandBottomMargin = CurrentHandCardScale < 0.99f
			? AscendCardLayout::CompactHandBottomMargin : AscendCardLayout::HandBottomMargin;
		const float HandRowTopVY = CanvasSize.Y - AscendCardLayout::Height * CurrentHandCardScale - HandBottomMargin;
		const float HandZoneLocal = HandRowTopVY - 10.f;
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
			CardCenterY = S->GetPosition().Y + AscendCardLayout::HalfHeight * CurrentHandCardScale;
		}
		// Outside hand zone → play。DraggedCardWidget 的位置也是 Canvas 局部坐标。
		FGeometry AG = AnimCanvas->GetTickSpaceGeometry();
		FVector2D CanvasSize = AG.GetLocalSize();
		if (CanvasSize.X < 640.f || CanvasSize.Y < 360.f)
			CanvasSize = FVector2D(1920.f, 1080.f);
		const float HandBottomMargin = CurrentHandCardScale < 0.99f
			? AscendCardLayout::CompactHandBottomMargin : AscendCardLayout::HandBottomMargin;
		const float HandZoneLocal = CanvasSize.Y - AscendCardLayout::Height * CurrentHandCardScale - HandBottomMargin - 10.f;
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
			const FCardInstance PlayedCard = Combat->Hand[DragCardIndex];
			int32 LogN_before = CombatLogLines.Num();
			UE_LOG(LogTemp, Display, TEXT("[PLAY] card=%d target=%d bNeedsTarget=%d logN=%d"),
				DragCardIndex, PlayTarget, bDragNeedsTarget, LogN_before);
			Combat->PlayCard(DragCardIndex, PlayTarget);
			UE_LOG(LogTemp, Display, TEXT("[PLAY] done logN=%d"), CombatLogLines.Num());
			int32 LogN_after = CombatLogLines.Num();
			for (int32 li = LogN_before; li < LogN_after; ++li)
				UE_LOG(LogTemp, Display, TEXT("[PLAYLOG] %s"), *CombatLogLines[li]);
			bDragUsingTouch = false;
			bHasWidgetTouchPosition = false;
			DragCardIndex = -1;
			DragTargetEnemy = -1;
			if (Combat->IsCombatOver())
			{
				bCombatEndPending = true;
				bInputLocked = true;
				TriggerCombatAnimations(TEXT("card"));
				if (GetWorld())
				{
					PlayCardVisual(PlayedCard, PlayTarget);
					TriggerCombatAnimations(TEXT("card"));
					GetWorld()->GetTimerManager().SetTimer(CombatEndTimer, this,
						&AAscendPlayerController::FinishCombatDelayed, 0.6f, false);
				}
				return;
			}
			RefreshCombatPanel();
			PlayCardVisual(PlayedCard, PlayTarget);
			TriggerCombatAnimations(TEXT("card"));
			return;
		}
	}

	// Snap back animation: brief translate to hand position then show panel
	bDragUsingTouch = false;
	bHasWidgetTouchPosition = false;
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
		CardPos = CS->GetPosition() + FVector2D(
			AscendCardLayout::HalfWidth * CurrentHandCardScale,
			AscendCardLayout::HalfHeight * CurrentHandCardScale);
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

void AAscendPlayerController::SpawnSlashEffect(float X, float Y, FLinearColor Color)
{
	if (!AnimCanvas || !GetWorld()) return;

	UCombatSlashWidget* Slash = CreateWidget<UCombatSlashWidget>(this, UCombatSlashWidget::StaticClass());
	if (!Slash) return;
	Slash->SetVisibility(ESlateVisibility::HitTestInvisible);
	Slash->SlashColor = Color;
	Slash->Rotation = FMath::FRandRange(-7.f, 7.f);
	Slash->SetSlashProgress(0.f);
	UCanvasPanelSlot* Slot = AnimCanvas->AddChildToCanvas(Slash);
	Slot->SetPosition(FVector2D(X - 130.f, Y - 106.f));
	Slot->SetSize(FVector2D(260.f, 212.f));
	Slot->SetZOrder(1100);
	ActiveAnimations.Add(Slash);

	const float StartTime = GetWorld()->GetTimeSeconds();
	const float Duration = 0.42f;
	TWeakObjectPtr<UCombatSlashWidget> WeakSlash = Slash;
	TSharedPtr<FTimerHandle> TimerHandle = MakeShared<FTimerHandle>();
	GetWorld()->GetTimerManager().SetTimer(*TimerHandle,
		FTimerDelegate::CreateWeakLambda(this, [this, WeakSlash, TimerHandle, StartTime, Duration]()
		{
			if (!WeakSlash.IsValid())
			{
				if (UWorld* W = GetWorld()) W->GetTimerManager().ClearTimer(*TimerHandle);
				return;
			}
			const float T = FMath::Clamp((GetWorld()->GetTimeSeconds() - StartTime) / Duration, 0.f, 1.f);
			const float Ease = FMath::InterpEaseOut(0.f, 1.f, T, 2.2f);
			WeakSlash->SetSlashProgress(Ease);
			WeakSlash->SetRenderOpacity(1.f - T * T);
			if (T >= 1.f)
			{
				WeakSlash->RemoveFromParent();
				ActiveAnimations.Remove(WeakSlash.Get());
				if (UWorld* W = GetWorld()) W->GetTimerManager().ClearTimer(*TimerHandle);
			}
		}), 0.033f, true);
	AnimTimerHandles.Add(*TimerHandle);
}

void AAscendPlayerController::SpawnImpactBurst(const FVector2D& Center, FLinearColor Color, float Duration)
{
	if (!AnimCanvas || !GetWorld()) return;

	UTextBlock* Burst = FAscendUIStyle::MakeText(AnimCanvas, TEXT("✹"), 66, Color);
	Burst->SetRenderTransformPivot(FVector2D(0.5f, 0.5f));
	UCanvasPanelSlot* Slot = AnimCanvas->AddChildToCanvas(Burst);
	Slot->SetPosition(Center - FVector2D(33.f, 33.f));
	Slot->SetAutoSize(true);
	Burst->SetRenderScale(FVector2D(0.15f, 0.15f));
	Burst->SetRenderOpacity(0.95f);
	ActiveAnimations.Add(Burst);

	const float StartTime = GetWorld()->GetTimeSeconds();
	const float SafeDuration = FMath::Max(0.12f, Duration);
	TWeakObjectPtr<UTextBlock> WeakBurst = Burst;
	TSharedPtr<FTimerHandle> TimerHandle = MakeShared<FTimerHandle>();
	GetWorld()->GetTimerManager().SetTimer(*TimerHandle,
		FTimerDelegate::CreateWeakLambda(this, [this, WeakBurst, TimerHandle, StartTime, SafeDuration]()
		{
			if (!WeakBurst.IsValid())
			{
				if (UWorld* W = GetWorld()) W->GetTimerManager().ClearTimer(*TimerHandle);
				return;
			}
			const float T = FMath::Clamp((GetWorld()->GetTimeSeconds() - StartTime) / SafeDuration, 0.f, 1.f);
			const float Scale = FMath::InterpEaseOut(0.15f, 1.55f, T, 2.f) * (1.f - 0.35f * T);
			WeakBurst->SetRenderScale(FVector2D(Scale, Scale));
			WeakBurst->SetRenderOpacity(0.95f * (1.f - T));
			if (T >= 1.f)
			{
				WeakBurst->RemoveFromParent();
				ActiveAnimations.Remove(WeakBurst.Get());
				if (UWorld* W = GetWorld()) W->GetTimerManager().ClearTimer(*TimerHandle);
			}
		}), 0.033f, true);
	AnimTimerHandles.Add(*TimerHandle);
}

void AAscendPlayerController::SpawnProjectileEffect(const FVector2D& From, const FVector2D& To,
	FLinearColor Color, float Duration)
{
	if (!AnimCanvas || !GetWorld()) return;

	UTextBlock* Trail = FAscendUIStyle::MakeText(AnimCanvas, TEXT("·"), 30, Color * 0.55f);
	UTextBlock* Orb = FAscendUIStyle::MakeText(AnimCanvas, TEXT("◆"), 38, Color);
	Orb->SetRenderTransformPivot(FVector2D(0.5f, 0.5f));
	UCanvasPanelSlot* TrailSlot = AnimCanvas->AddChildToCanvas(Trail);
	TrailSlot->SetPosition(From - FVector2D(15.f, 15.f));
	TrailSlot->SetAutoSize(true);
	UCanvasPanelSlot* OrbSlot = AnimCanvas->AddChildToCanvas(Orb);
	OrbSlot->SetPosition(From - FVector2D(19.f, 19.f));
	OrbSlot->SetAutoSize(true);
	ActiveAnimations.Add(Trail);
	ActiveAnimations.Add(Orb);

	const float StartTime = GetWorld()->GetTimeSeconds();
	const float SafeDuration = FMath::Max(0.12f, Duration);
	TWeakObjectPtr<UTextBlock> WeakTrail = Trail;
	TWeakObjectPtr<UTextBlock> WeakOrb = Orb;
	TSharedPtr<FTimerHandle> TimerHandle = MakeShared<FTimerHandle>();
	GetWorld()->GetTimerManager().SetTimer(*TimerHandle,
		FTimerDelegate::CreateWeakLambda(this, [this, WeakTrail, WeakOrb, TimerHandle, From, To, Color, StartTime, SafeDuration]()
		{
			if (!WeakOrb.IsValid())
			{
				if (UWorld* W = GetWorld()) W->GetTimerManager().ClearTimer(*TimerHandle);
				return;
			}
			const float T = FMath::Clamp((GetWorld()->GetTimeSeconds() - StartTime) / SafeDuration, 0.f, 1.f);
			const FVector2D Delta = To - From;
			const FVector2D Pos = From + Delta * T + FVector2D(0.f, -FMath::Sin(T * PI) * 34.f);
			const FVector2D Relative = Pos - From;
			WeakOrb->SetRenderTranslation(Relative);
			WeakOrb->SetRenderScale(FVector2D(0.75f + 0.35f * FMath::Sin(T * PI), 0.75f + 0.35f * FMath::Sin(T * PI)));
			WeakOrb->SetRenderOpacity(1.f - 0.15f * T);
			if (WeakTrail.IsValid())
			{
				WeakTrail->SetRenderTranslation(Relative - Delta.GetSafeNormal() * 18.f);
				WeakTrail->SetRenderOpacity(0.45f * (1.f - 0.35f * T));
			}
			if (T >= 1.f)
			{
				SpawnImpactBurst(To, Color, 0.24f);
				WeakOrb->RemoveFromParent();
				ActiveAnimations.Remove(WeakOrb.Get());
				if (WeakTrail.IsValid())
				{
					WeakTrail->RemoveFromParent();
					ActiveAnimations.Remove(WeakTrail.Get());
				}
				if (UWorld* W = GetWorld()) W->GetTimerManager().ClearTimer(*TimerHandle);
			}
		}), 0.033f, true);
	AnimTimerHandles.Add(*TimerHandle);
}

void AAscendPlayerController::SpawnHealBurst(const FVector2D& Center, FLinearColor Color, float Duration)
{
	if (!AnimCanvas || !GetWorld()) return;

	UTextBlock* Heal = FAscendUIStyle::MakeText(AnimCanvas, TEXT("✦"), 58, Color);
	Heal->SetRenderTransformPivot(FVector2D(0.5f, 0.5f));
	UCanvasPanelSlot* Slot = AnimCanvas->AddChildToCanvas(Heal);
	Slot->SetPosition(Center - FVector2D(29.f, 29.f));
	Slot->SetAutoSize(true);
	ActiveAnimations.Add(Heal);

	const float StartTime = GetWorld()->GetTimeSeconds();
	const float SafeDuration = FMath::Max(0.18f, Duration);
	TWeakObjectPtr<UTextBlock> WeakHeal = Heal;
	TSharedPtr<FTimerHandle> TimerHandle = MakeShared<FTimerHandle>();
	GetWorld()->GetTimerManager().SetTimer(*TimerHandle,
		FTimerDelegate::CreateWeakLambda(this, [this, WeakHeal, TimerHandle, StartTime, SafeDuration]()
		{
			if (!WeakHeal.IsValid())
			{
				if (UWorld* W = GetWorld()) W->GetTimerManager().ClearTimer(*TimerHandle);
				return;
			}
			const float T = FMath::Clamp((GetWorld()->GetTimeSeconds() - StartTime) / SafeDuration, 0.f, 1.f);
			WeakHeal->SetRenderTranslation(FVector2D(0.f, -T * 48.f));
			WeakHeal->SetRenderScale(FVector2D(0.7f + 0.65f * T, 0.7f + 0.65f * T));
			WeakHeal->SetRenderOpacity(1.f - T);
			if (T >= 1.f)
			{
				WeakHeal->RemoveFromParent();
				ActiveAnimations.Remove(WeakHeal.Get());
				if (UWorld* W = GetWorld()) W->GetTimerManager().ClearTimer(*TimerHandle);
			}
		}), 0.033f, true);
	AnimTimerHandles.Add(*TimerHandle);
}

void AAscendPlayerController::PlayVisualSound(const FString& SoundId)
{
	if (!GetWorld() || SoundId.IsEmpty() || SoundId == TEXT("none")) return;
	if (InfiniteNarrativeSettings.SfxVolume <= 0.001f) return;

	const int32 SampleRate = 44100;
	const bool bSword = SoundId.Contains(TEXT("sword"));
	const bool bFire = SoundId.Contains(TEXT("fire"));
	const bool bBlock = SoundId.Contains(TEXT("block"));
	const bool bHeal = SoundId.Contains(TEXT("heal"));
	const bool bDraw = SoundId.Contains(TEXT("draw"));
	const float Duration = bFire ? 0.46f : (bSword ? 0.32f : (bHeal || bDraw ? 0.38f : 0.28f));
	const int32 SampleCount = FMath::RoundToInt(Duration * SampleRate);
	TArray<int16> Samples;
	Samples.SetNumUninitialized(SampleCount);
	float SmoothedNoise = 0.f;
	float PreviousNoise = 0.f;
	for (int32 Index = 0; Index < SampleCount; ++Index)
	{
		const float T = static_cast<float>(Index) / SampleRate;
		const float NormalizedT = FMath::Clamp(T / Duration, 0.f, 1.f);
		const float N = static_cast<float>(FMath::RandRange(-1000, 1000)) / 1000.f;
		// 一阶平滑噪声让火焰/剑风有连续的空气感，避免每个采样点独立白噪声的“砂纸声”。
		SmoothedNoise = FMath::Lerp(SmoothedNoise, N, 0.12f);
		const float HighNoise = N - PreviousNoise;
		PreviousNoise = N;
		float Signal = 0.f;
		if (bSword)
		{
			// 剑风：两层反向音高滑落 + 高频刃口噪声 + 很短的低频触击。
			const float Sweep = FMath::Lerp(2300.f, 190.f, FMath::Pow(NormalizedT, 0.72f));
			const float Whoosh = FMath::Exp(-7.6f * T) * (0.62f * FMath::Sin(2.f * PI * Sweep * T)
				+ 0.22f * FMath::Sin(2.f * PI * Sweep * 1.96f * T)
				+ 0.12f * FMath::Sin(2.f * PI * Sweep * 0.51f * T));
			const float Edge = HighNoise * 0.20f * FMath::Exp(-18.f * T);
			const float ImpactT = FMath::Max(0.f, T - 0.055f);
			const float Impact = FMath::Sin(2.f * PI * 132.f * ImpactT) * FMath::Exp(-34.f * ImpactT) * 0.32f;
			Signal = Whoosh + Edge + Impact;
		}
		else if (bFire)
		{
			// 火球：平滑火焰底噪、低频爆裂、稀疏高频火星。
			const float FlameEnv = (0.25f + 0.75f * (1.f - NormalizedT)) * FMath::Exp(-2.8f * T);
			const float RumbleFreq = FMath::Lerp(108.f, 38.f, NormalizedT);
			const float Rumble = FMath::Sin(2.f * PI * RumbleFreq * T) * 0.36f
				+ FMath::Sin(2.f * PI * RumbleFreq * 1.87f * T) * 0.15f;
			const float Crackle = SmoothedNoise * 0.42f * FlameEnv;
			const float Sparks = HighNoise * (0.10f + 0.16f * (1.f - NormalizedT)) * FMath::Exp(-3.8f * T);
			Signal = Crackle + Rumble * FlameEnv + Sparks;
		}
		else if (bBlock)
		{
			// 护体/格挡：短促金属共振，不再是单一 420Hz 正弦波。
			const float Ring = FMath::Exp(-13.f * T);
			Signal = Ring * (0.54f * FMath::Sin(2.f * PI * 460.f * T)
				+ 0.24f * FMath::Sin(2.f * PI * 930.f * T)
				+ 0.12f * FMath::Sin(2.f * PI * 1410.f * T))
				+ HighNoise * 0.14f * FMath::Exp(-30.f * T);
		}
		else if (bHeal || bDraw)
		{
			// 回春/抽牌：两到三层短铃声，给界面反馈一个更柔和的音高轮廓。
			const float Attack = FMath::Clamp(T / 0.018f, 0.f, 1.f);
			const float Release = FMath::Exp(-6.8f * T);
			const float Root = bHeal ? 640.f : 560.f;
			Signal = Attack * Release * (0.42f * FMath::Sin(2.f * PI * Root * T)
				+ 0.24f * FMath::Sin(2.f * PI * Root * 1.5f * T)
				+ 0.14f * FMath::Sin(2.f * PI * Root * 2.01f * T));
		}
		else
		{
			Signal = FMath::Sin(2.f * PI * 560.f * T) * FMath::Exp(-10.f * T);
		}
		// 轻微软削波，避免多层合成在峰值处产生刺耳的数字爆音。
		const float SoftClipped = FMath::Tan(FMath::Clamp(Signal, -1.1f, 1.1f) * 0.78f) / FMath::Tan(0.78f);
		Samples[Index] = static_cast<int16>(FMath::Clamp(SoftClipped, -1.f, 1.f) * 25000.f);
	}

	USoundWaveProcedural* Wave = NewObject<USoundWaveProcedural>(this);
	Wave->NumChannels = 1;
	Wave->SetSampleRate(SampleRate);
	Wave->bLooping = false;
	Wave->QueueAudio(reinterpret_cast<const uint8*>(Samples.GetData()), Samples.Num() * sizeof(int16));
	ActiveSoundWaves.Add(Wave);
	UGameplayStatics::PlaySound2D(this, Wave,
		(bFire ? 0.72f : (bSword ? 0.62f : 0.58f)) * FMath::Clamp(InfiniteNarrativeSettings.SfxVolume, 0.f, 1.f), 1.f);
}

void AAscendPlayerController::PlayCardVisual(const FCardInstance& Card, int32 TargetEnemyIndex)
{
	if (!Combat) return;
	const FCardVisualData& Visual = Card.Data.Visual;
	LastPlayedVisualAnimation = Visual.Animation == TEXT("none") ? TEXT("") : Visual.Animation;
	if (Visual.Sound != TEXT("none")) PlayVisualSound(Visual.Sound);
	if (LastPlayedVisualAnimation.IsEmpty()) return;

	const FLinearColor Accent = FColor::FromHex(Visual.Accent.IsEmpty() ? TEXT("#FFFFFF") : Visual.Accent);
	const FVector2D EnemyPos = GetEnemyScreenPos(TargetEnemyIndex);
	const FVector2D PlayerPos = FVector2D(GetViewportSize().X * 0.5f, GetViewportSize().Y * 0.64f);
	const float Duration = FMath::Max(0.12f, Visual.Duration);

	if (Visual.Animation == TEXT("slash"))
	{
		const int32 SlashCount = FMath::Max(1, Visual.Count);
		for (int32 Index = 0; Index < SlashCount; ++Index)
			SpawnSlashEffect(EnemyPos.X + (Index - (SlashCount - 1) * 0.5f) * 20.f, EnemyPos.Y - Index * 8.f, Accent);
		SpawnImpactBurst(EnemyPos, Accent, Duration * 0.75f);
	}
	else if (Visual.Animation == TEXT("fireball"))
	{
		SpawnProjectileEffect(PlayerPos, EnemyPos, Accent, Duration);
	}
	else if (Visual.Animation == TEXT("impact"))
	{
		SpawnImpactBurst(EnemyPos, Accent, Duration);
	}
	else if (Visual.Animation == TEXT("block"))
	{
		AnimateColorFlash(Accent, Duration * 0.8f);
		SpawnFloatingText(TEXT("罡气"), Accent, PlayerPos.X - 36.f, PlayerPos.Y - 30.f, Duration);
	}
	else if (Visual.Animation == TEXT("heal"))
	{
		SpawnHealBurst(PlayerPos, Accent, Duration);
		SpawnFloatingText(TEXT("回春"), Accent, PlayerPos.X - 36.f, PlayerPos.Y - 30.f, Duration + 0.25f);
	}
	else if (Visual.Animation == TEXT("draw"))
	{
		SpawnFloatingText(TEXT("抽牌"), Accent, GetViewportSize().X - 125.f, GetViewportSize().Y - 175.f, Duration);
	}

	if (Visual.Intensity > 0.f)
		AnimateScreenShake(Visual.Intensity, FMath::Min(0.35f, Duration));
}

void AAscendPlayerController::AnimateScreenShake(float Intensity, float Duration)
{
	if (!InfiniteNarrativeSettings.bEnableScreenShake) return;
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
	const bool bHasCardVisual = ActionType == TEXT("card") && !LastPlayedVisualAnimation.IsEmpty();

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
		if (!bHasCardVisual)
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
	if (ActionType == TEXT("card")) LastPlayedVisualAnimation.Empty();
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
	TSharedPtr<FTimerHandle> Handle = MakeShared<FTimerHandle>();
	GetWorld()->GetTimerManager().SetTimer(*Handle,
		FTimerDelegate::CreateWeakLambda(this,
			[this, WeakCard, Handle, StartTime, Delay, Duration, From, To,
			 ScaleFrom, ScaleMid, ScaleTo, ArcHeight, FadeInEnd, FadeOutStart]()
		{
			if (!WeakCard.IsValid())
			{
				if (UWorld* W = GetWorld())
				{
					FTimerHandle CompletedHandle = *Handle;
					W->GetTimerManager().ClearTimer(CompletedHandle);
				}
				return;
			}
			const float Elapsed = GetWorld()->GetTimeSeconds() - StartTime - Delay;
			if (Elapsed < 0.f) return;
			if (Elapsed >= Duration)
			{
				FTimerHandle CompletedHandle = *Handle;
				WeakCard->RemoveFromParent();
				ActiveAnimations.Remove(WeakCard.Get());
				if (UWorld* W = GetWorld()) W->GetTimerManager().ClearTimer(CompletedHandle);
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
	AnimTimerHandles.Add(*Handle);
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
		TSharedPtr<FTimerHandle> H = MakeShared<FTimerHandle>();

		TWeakObjectPtr<AAscendPlayerController> WeakThis(this);
		GetWorld()->GetTimerManager().SetTimer(*H,
			FTimerDelegate::CreateWeakLambda(this, [Btn, H, i, StartTime, Delay, Dur, PileAnchor, WeakThis]()
			{
				if (!WeakThis.IsValid()) return;
				AAscendPlayerController* PC = WeakThis.Get();
				UWorld* W = PC->GetWorld();
				if (!W) return;
				if (!Btn.IsValid())
				{
					FTimerHandle CompletedHandle = *H;
					W->GetTimerManager().ClearTimer(CompletedHandle);
					return;
				}

				const float Elapsed = W->GetTimeSeconds() - StartTime - Delay;
				if (Elapsed < 0.f) return;
				if (Elapsed >= Dur)
				{
					if (PC->HoveredCardIndex == i)
					{
						Btn->SetRenderTranslation(FVector2D::ZeroVector);
						Btn->SetRenderScale(FVector2D(1.f, 1.f));
					}
					else
					{
						Btn->SetRenderTranslation(FVector2D::ZeroVector);
						Btn->SetRenderScale(FVector2D(1.f, 1.f));
					}
					Btn->SetRenderOpacity(1.f);
					FTimerHandle CompletedHandle = *H;
					W->GetTimerManager().ClearTimer(CompletedHandle);
					return;
				}

				const float T = Elapsed / Dur;
				const float Ease = 1.f - FMath::Pow(1.f - T, 3.f);

				FVector2D Tr = PileAnchor * (1.f - Ease);
				Tr.Y -= FMath::Sin(T * PI) * 40.f;
				const float Scale = (T < 0.5f)
					? FMath::Lerp(0.2f, 1.15f, T * 2.f)
					: FMath::Lerp(1.15f, 1.0f, (T - 0.5f) * 2.f);
				if (PC->HoveredCardIndex == i)
				{
					Btn->SetRenderTranslation(FVector2D::ZeroVector);
					Btn->SetRenderScale(FVector2D(1.f, 1.f));
					Btn->SetRenderOpacity(1.f);
				}
				else
				{
					Btn->SetRenderTranslation(Tr);
					Btn->SetRenderScale(FVector2D(Scale, Scale));
					Btn->SetRenderOpacity(FMath::Clamp(T / 0.15f, 0.f, 1.f));
				}
			}), 0.033f, true);
		AnimTimerHandles.Add(*H);
	}
}

void AAscendPlayerController::PlayEndTurnDiscardAnimation(const TArray<FVector2D>& FromPositions)
{
	if (!AnimCanvas || FromPositions.Num() == 0) return;

	const FVector2D DiscardAnchor = GetPileAnchor(false);
	for (int32 i = 0; i < FromPositions.Num(); ++i)
	{
		const FVector2D TargetJitter = DiscardAnchor + FVector2D(
			FMath::FRandRange(-5.f, 5.f), FMath::FRandRange(-3.f, 3.f));
		SpawnMiniCardAnim(
			FromPositions[i], TargetJitter,
			i * 0.055f, 0.42f,
			2.4f, 1.2f, 0.25f,
			36.f, 0.f, 0.76f);
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
	PendingScreenProxies.Add(CloseProxy);
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
