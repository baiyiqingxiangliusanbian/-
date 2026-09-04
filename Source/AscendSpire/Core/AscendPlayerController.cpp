#include "AscendPlayerController.h"
#include "AscendAudioRouter.h"
#include "UI/ClickProxy.h"
#include "UI/AscendUIStyle.h"
#include "UI/AscendRootWidget.h"
#include "UI/AscendArt.h"
#include "UI/AscendCardLayout.h"
#include "UI/CombatSlashWidget.h"
#include "UI/CardVisualResolver.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "TimerManager.h"
#include "Engine/Engine.h"
#include "Engine/Texture2D.h"
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
#include "Math/UnrealMathUtility.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

namespace
{
	static_assert(AscendCardLayout::Width > 0.f && AscendCardLayout::Height > 0.f,
		"Card layout must have a positive final render size");
	static_assert(AscendCardLayout::ArtX >= AscendCardLayout::InnerX
		&& AscendCardLayout::ArtY >= AscendCardLayout::InnerY
		&& AscendCardLayout::ArtX + AscendCardLayout::ArtW <= AscendCardLayout::InnerX + AscendCardLayout::InnerW
		&& AscendCardLayout::ArtY + AscendCardLayout::ArtH <= AscendCardLayout::InnerY + AscendCardLayout::InnerH,
		"Card art must stay inside the card backplate");
	static_assert(AscendCardLayout::RulesX >= AscendCardLayout::InnerX
		&& AscendCardLayout::RulesY >= AscendCardLayout::InnerY
		&& AscendCardLayout::RulesX + AscendCardLayout::RulesW <= AscendCardLayout::InnerX + AscendCardLayout::InnerW
		&& AscendCardLayout::RulesY + AscendCardLayout::RulesH <= AscendCardLayout::InnerY + AscendCardLayout::InnerH,
		"Card rules panel must stay inside the card backplate");

	constexpr float HandHoverScale = 1.4f;
	constexpr float HandHoverLift = 36.f;
	constexpr float TouchHandHoverScale = 1.82f;
	constexpr float TouchHandHoverLift = 62.f;
	constexpr float TouchDragScale = 1.42f;
	constexpr float MouseDragScale = 1.12f;
	constexpr float TouchDragThreshold = 22.f;
	constexpr int32 HandHoverZOrder = 1000;

	// Keep geometry on whole Slate pixels.  Fractional slot edges are especially
	// visible on the thin card frame when a responsive hand scale is active.
	inline float SnapCardPixel(float Value)
	{
		return static_cast<float>(FMath::RoundToInt(Value));
	}

	inline float GetDragCardScale(float HandScale, bool bTouch)
	{
		return FMath::Max(0.1f, HandScale * (bTouch ? TouchDragScale : MouseDragScale));
	}

	void ConfigureCardTexture(UImage* Image)
	{
		if (!Image) return;
		UTexture2D* Texture = Cast<UTexture2D>(Image->GetBrush().GetResourceObject());
		if (!Texture) return;

		// Runtime-imported UI art must not stream in a lower mip between the hand
		// and focus layer.  No mip chain also prevents a transient low-resolution
		// sample while the card is being rebuilt at its final size.
		bool bChanged = !Texture->NeverStream
			|| Texture->LODGroup != TEXTUREGROUP_UI
			|| Texture->Filter != TF_Bilinear;
		Texture->NeverStream = true;
		// MipGenSettings is editor-only in UE 5.8 and is unavailable in the
		// packaged runtime target. NeverStream plus the UI texture group keeps
		// the runtime card face from being replaced by a lower streamed mip.
		#if WITH_EDITORONLY_DATA
		bChanged = bChanged || Texture->MipGenSettings != TMGS_NoMipmaps;
		Texture->MipGenSettings = TMGS_NoMipmaps;
		#endif
		Texture->LODGroup = TEXTUREGROUP_UI;
		Texture->Filter = TF_Bilinear;
		if (bChanged) Texture->UpdateResource();
	}

	void PlaceCardWidget(UCanvasPanel* Canvas, UWidget* Widget, float X, float Y, float W, float H, float Scale, int32 ZOrder)
	{
		if (UCanvasPanelSlot* Slot = Canvas->AddChildToCanvas(Widget))
		{
			Slot->SetPosition(FVector2D(SnapCardPixel(X * Scale), SnapCardPixel(Y * Scale)));
			Slot->SetSize(FVector2D(SnapCardPixel(W * Scale), SnapCardPixel(H * Scale)));
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
		CardSizer->SetWidthOverride(SnapCardPixel(Width * S));
		CardSizer->SetHeightOverride(SnapCardPixel(Height * S));

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
			ConfigureCardTexture(ArtImage);
			UScaleBox* ArtScale = NewObject<UScaleBox>(ArtClip);
			ArtScale->SetStretch(EStretch::ScaleToFill);
			ArtScale->SetContent(ArtImage);
			ArtClip->SetContent(ArtScale);
		}
		PlaceCardWidget(Canvas, ArtClip, ArtX, ArtY, ArtW, ArtH, S, 5);

		UOverlay* TitlePanel = NewObject<UOverlay>(Canvas);
		if (UImage* TitleArt = FAscendArt::MakeImage(TitlePanel, TEXT("Art/ui/card_title_bar_v2.png")))
		{
			ConfigureCardTexture(TitleArt);
			TitleArt->SetVisibility(ESlateVisibility::HitTestInvisible);
			UOverlaySlot* ArtSlot = TitlePanel->AddChildToOverlay(TitleArt);
			ArtSlot->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Fill);
			ArtSlot->SetVerticalAlignment(EVerticalAlignment::VAlign_Fill);
		}
		UTextBlock* NameText = FAscendUIStyle::MakeText(TitlePanel, DisplayName,
			FAscendUIStyle::CardTitleFontSize(S), FAscendUIStyle::GoldYellow());
		NameText->SetJustification(ETextJustify::Center);
		NameText->SetAutoWrapText(true);
		NameText->SetWrapTextAt(SnapCardPixel(130.f * S));
		NameText->SetShadowOffset(FVector2D(1.f, 1.f));
		UOverlaySlot* NameSlot = TitlePanel->AddChildToOverlay(NameText);
		NameSlot->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Fill);
		NameSlot->SetVerticalAlignment(EVerticalAlignment::VAlign_Fill);
		NameSlot->SetPadding(FMargin(SnapCardPixel(17.f * S), SnapCardPixel(5.f * S),
			SnapCardPixel(17.f * S), SnapCardPixel(5.f * S)));
		PlaceCardWidget(Canvas, TitlePanel, TitleX, TitleY, TitleW, TitleH, S, 15);

		UOverlay* RulesPanel = NewObject<UOverlay>(Canvas);
		RulesPanel->SetClipping(EWidgetClipping::ClipToBounds);
		if (UImage* RulesArt = FAscendArt::MakeImage(RulesPanel, TEXT("Art/ui/card_rules_panel_v2.png")))
		{
			ConfigureCardTexture(RulesArt);
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
			FAscendUIStyle::CardDescriptionFontSize(S), FAscendUIStyle::PaperWhite());
		DescriptionText->SetAutoWrapText(true);
		DescriptionText->SetWrapTextAt(SnapCardPixel(120.f * S));
		DescriptionText->SetJustification(ETextJustify::Center);
		UOverlaySlot* DescriptionSlot = RulesPanel->AddChildToOverlay(DescriptionText);
		DescriptionSlot->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Fill);
		DescriptionSlot->SetVerticalAlignment(EVerticalAlignment::VAlign_Fill);
		DescriptionSlot->SetPadding(FMargin(SnapCardPixel(10.f * S), SnapCardPixel(9.f * S),
			SnapCardPixel(10.f * S), SnapCardPixel(9.f * S)));
		PlaceCardWidget(Canvas, RulesPanel, RulesX, RulesY, RulesW, RulesH, S, 15);

		// The frame is a true transparent overlay and is deliberately last, so it
		// masks the joins without ever reserving or guessing a content area.
		if (UImage* Frame = FAscendArt::MakeImage(Canvas, TEXT("Art/ui/card_border_v2.png")))
		{
			ConfigureCardTexture(Frame);
			Frame->SetVisibility(ESlateVisibility::HitTestInvisible);
			PlaceCardWidget(Canvas, Frame, 0.f, 0.f, Width, Height, S, 40);
		}

		UOverlay* CostGem = NewObject<UOverlay>(Canvas);
		if (UImage* GemArt = FAscendArt::MakeImage(CostGem, TEXT("Art/ui/spirit_gem.png")))
		{
			ConfigureCardTexture(GemArt);
			GemArt->SetVisibility(ESlateVisibility::HitTestInvisible);
			UOverlaySlot* GemSlot = CostGem->AddChildToOverlay(GemArt);
			GemSlot->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Fill);
			GemSlot->SetVerticalAlignment(EVerticalAlignment::VAlign_Fill);
		}
		UTextBlock* CostText = FAscendUIStyle::MakeText(CostGem, FString::FromInt(DisplayCost),
			FAscendUIStyle::CardCostFontSize(S), CostColor);
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
	AudioRouter = NewObject<UAscendAudioRouter>(this);
	AudioRouter->Initialize(this, InfiniteNarrativeSettings.SfxVolume, InfiniteNarrativeSettings.MusicVolume);

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

#if !UE_BUILD_SHIPPING
	// Development-only visual acceptance route. It exercises the same card-play,
	// combat-log and animation path as a real run while never touching the save.
	const bool bVFXSignature = FParse::Param(FCommandLine::Get(), TEXT("VFXShowcase"));
	const bool bVFXElements = FParse::Param(FCommandLine::Get(), TEXT("VFXShowcaseElements"));
	const bool bVFXSupport = FParse::Param(FCommandLine::Get(), TEXT("VFXShowcaseSupport"));
	const bool bVFXAutoPlay = FParse::Param(FCommandLine::Get(), TEXT("VFXAutoPlay"));
	const bool bRPVisualTest = FParse::Param(FCommandLine::Get(), TEXT("RPVisualTest"));
	if (bRPVisualTest)
	{
		GetWorld()->GetTimerManager().SetTimerForNextTick(
			FTimerDelegate::CreateWeakLambda(this, [this]() { StartRPNarrativeVisualTest(); }));
	}
	if (bVFXSignature || bVFXElements || bVFXSupport)
	{
		GetWorld()->GetTimerManager().SetTimerForNextTick(
			FTimerDelegate::CreateWeakLambda(this, [this, bVFXElements, bVFXSupport, bVFXAutoPlay]()
			{
				if (!Run || !Run->StartNewRun(20260830, true)) return;

				TArray<FDeckCard> ShowcaseDeck;
				auto AddShowcaseCard = [&ShowcaseDeck](const TCHAR* CardId, bool bUpgraded = false)
				{
					FDeckCard Card;
					Card.CardId = CardId;
					Card.bUpgraded = bUpgraded;
					ShowcaseDeck.Add(Card);
				};
				if (bVFXElements)
				{
					AddShowcaseCard(TEXT("heavenly_thunder"));
					AddShowcaseCard(TEXT("wildfire"));
					AddShowcaseCard(TEXT("poison_palm"));
					AddShowcaseCard(TEXT("venom_burst"));
					AddShowcaseCard(TEXT("mind_sever"));
				}
				else if (bVFXSupport)
				{
					AddShowcaseCard(TEXT("vajra"));
					AddShowcaseCard(TEXT("qi_breath"));
					AddShowcaseCard(TEXT("sword_immortal_gongfa"));
					AddShowcaseCard(TEXT("spirit_gathering"));
					AddShowcaseCard(TEXT("curse_regret"));
				}
				else
				{
					AddShowcaseCard(TEXT("strike"));
					AddShowcaseCard(TEXT("one_sword"));
					AddShowcaseCard(TEXT("wan_jian_gui_zong"), true);
					AddShowcaseCard(TEXT("sword_qi"));
					AddShowcaseCard(TEXT("rejuvenation"));
				}

				CurrentEncounter = FNodeEncounter();
				CurrentEncounter.Type = EMapNodeType::Combat;
				CurrentEncounter.EnemyIds = {TEXT("mountain_imp")};
				CurrentEncounter.EnemyHPBonus = 900;

				Combat = NewObject<UCombatEngine>(this);
				Combat->OnLog.AddDynamic(this, &AAscendPlayerController::OnCombatLogDynamic);
				CombatLogLines.Reset();
				LastProcessedLogIndex = 0;
				LastProcessedEnemyDamageEventIndex = 0;
				if (Combat->StartCombat(ShowcaseDeck, CurrentEncounter.EnemyIds, {},
					Run->State.MaxHP, Run->State.MaxHP, CurrentEncounter.EnemyHPBonus,
					20260830, 0))
				{
					Combat->Spirit = 9;
					Combat->MaxSpirit = 9;
					ShowCombat();
					UE_LOG(LogTemp, Display, TEXT("[AscendVFX] showcase ready family=%s"),
						bVFXElements ? TEXT("elements") : (bVFXSupport ? TEXT("support") : TEXT("signature")));

					if (bVFXAutoPlay && GetWorld())
					{
						TSharedPtr<TArray<FString>> CardIds = MakeShared<TArray<FString>>();
						for (const FDeckCard& Card : ShowcaseDeck) CardIds->Add(Card.CardId);
						TSharedPtr<int32> NextCard = MakeShared<int32>(0);
						TSharedPtr<FTimerHandle> AutoTimer = MakeShared<FTimerHandle>();
						GetWorld()->GetTimerManager().SetTimer(*AutoTimer,
							FTimerDelegate::CreateWeakLambda(this,
								[this, CardIds, NextCard, AutoTimer]()
								{
									if (!Combat || !Combat->bCombatActive || !CardIds->IsValidIndex(*NextCard))
									{
										if (GetWorld()) GetWorld()->GetTimerManager().ClearTimer(*AutoTimer);
										return;
									}
									const FString CardId = (*CardIds)[*NextCard];
									const int32 HandIndex = Combat->Hand.IndexOfByPredicate(
										[&CardId](const FCardInstance& Card) { return Card.Data.Id == CardId; });
									++(*NextCard);
									if (HandIndex == INDEX_NONE) return;
									const FCardInstance PlayedCard = Combat->Hand[HandIndex];
									if (!Combat->PlayCard(HandIndex, 0)) return;
									RefreshCombatPanel();
									PlayCardVisual(PlayedCard, 0);
									TriggerCombatAnimations(TEXT("card"));
									UE_LOG(LogTemp, Display, TEXT("[AscendVFX] autoplay card=%s animation=%s"),
										*CardId, *AscendCardVisual::ResolveAnimation(PlayedCard));
								}), 1.85f, true, 1.2f);
					}
				}
			}));
	}
#endif
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
	UClickProxy* HoverProxy = NewObject<UClickProxy>(Btn);
	HoverProxy->Tag = TEXT("button_hover");
	HoverProxy->Index = Index;
	HoverProxy->Owner = this;
	Btn->OnHovered.AddDynamic(HoverProxy, &UClickProxy::HandleHovered);
	PendingScreenProxies.Add(HoverProxy);
	return Btn;
}

UTextBlock* AAscendPlayerController::MakeLogText(const FString& Text, FSlateColor Color)
{
	return FAscendUIStyle::MakeText(RootWidget, Text, 15, Color);
}

void AAscendPlayerController::SetScreen(UWidget* Content, EGameScreen Screen,
	const FString& MusicStateOverride)
{
	UE_LOG(LogTemp, Display, TEXT("[DIAG] SetScreen begin screen=%d"), static_cast<int32>(Screen));
	const EGameScreen PreviousScreen = CurrentScreen;
	// Screen builders create their controls before this call. Retain only this generation so
	// buttons from every previous combat/RP redraw do not keep entire widget trees alive.
	Proxies = MoveTemp(PendingScreenProxies);
	PendingScreenProxies.Reset();
	ClearAnimations();
	UE_LOG(LogTemp, Display, TEXT("[DIAG] SetScreen after ClearAnimations"));
	CurrentScreen = Screen;
	SetMusicForScreen(Screen, PreviousScreen, MusicStateOverride);
	if (Screen == EGameScreen::Combat && PreviousScreen != EGameScreen::Combat)
		PlayAudioEvent(TEXT("combat_enter"), 0.70f, 0.98f, 1.02f);

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
	case EGameScreen::Event:    Bg = FLinearColor(0.06f, 0.055f, 0.075f); BgArt = TEXT("Art/bg/reward.png"); DimAlpha = 0.79f; break;
	case EGameScreen::InfiniteNarrative: Bg = FLinearColor(0.042f, 0.055f, 0.058f); BgArt = TEXT("Art/bg/map.png"); DimAlpha = 0.80f; break;
	case EGameScreen::Settings: Bg = FLinearColor(0.05f, 0.055f, 0.06f); BgArt = TEXT("Art/bg/title.png"); DimAlpha = 0.84f; break;
	case EGameScreen::Shop:     Bg = FLinearColor(0.07f, 0.06f, 0.045f); BgArt = TEXT("Art/bg/reward.png"); DimAlpha = 0.76f; break;
	case EGameScreen::Rest:     Bg = FLinearColor(0.045f, 0.06f, 0.06f); BgArt = TEXT("Art/bg/map.png"); DimAlpha = 0.78f; break;
	default: break;
	}
	ScreenHost->SetBrushColor(Bg);

	auto AnimateScreenEntrance = [this, PreviousScreen, Screen](UWidget* ScreenContent)
	{
		if (!ScreenContent || !GetWorld() || PreviousScreen == Screen) return;
		const float Duration = Screen == EGameScreen::Combat ? 0.24f : 0.34f;
		const float StartOffset = Screen == EGameScreen::Combat ? 10.f : 20.f;
		ScreenContent->SetRenderTransformPivot(FVector2D(0.5f, 0.5f));
		ScreenContent->SetRenderOpacity(0.f);
		ScreenContent->SetRenderTranslation(FVector2D(0.f, StartOffset));
		const double StartTime = FPlatformTime::Seconds();
		TWeakObjectPtr<UWidget> WeakContent(ScreenContent);
		TSharedPtr<FTimerHandle> TimerHandle = MakeShared<FTimerHandle>();
		GetWorld()->GetTimerManager().SetTimer(*TimerHandle,
			FTimerDelegate::CreateWeakLambda(this,
				[this, WeakContent, TimerHandle, StartTime, Duration, StartOffset]()
				{
					if (!WeakContent.IsValid())
					{
						if (UWorld* World = GetWorld()) World->GetTimerManager().ClearTimer(*TimerHandle);
						return;
					}
					const float T = FMath::Clamp(static_cast<float>((FPlatformTime::Seconds() - StartTime) / Duration), 0.f, 1.f);
					const float Ease = 1.f - FMath::Pow(1.f - T, 3.f);
					WeakContent->SetRenderOpacity(Ease);
					WeakContent->SetRenderTranslation(FVector2D(0.f, StartOffset * (1.f - Ease)));
					if (T >= 1.f)
					{
						WeakContent->SetRenderOpacity(1.f);
						WeakContent->SetRenderTranslation(FVector2D::ZeroVector);
						if (UWorld* World = GetWorld()) World->GetTimerManager().ClearTimer(*TimerHandle);
					}
				}), 0.016f, true);
		AnimTimerHandles.Add(*TimerHandle);
	};

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
			AnimateScreenEntrance(Content);
			return;
		}
	}
	ScreenHost->SetContent(Content);
	AnimateScreenEntrance(Content);
}

void AAscendPlayerController::SetMusicForScreen(EGameScreen Screen, EGameScreen PreviousScreen,
	const FString& MusicStateOverride)
{
	if (!AudioRouter) return;

	FString State = MusicStateOverride;
	if (State.IsEmpty())
	{
		switch (Screen)
		{
		case EGameScreen::Title:
		case EGameScreen::Settings:
			State = TEXT("bgm_title");
			break;
		case EGameScreen::AuthoredLibrary:
			State = TEXT("bgm_deckbuilding");
			break;
		case EGameScreen::Map:
			State = TEXT("bgm_map");
			break;
		case EGameScreen::Combat:
			State = CurrentEncounter.Type == EMapNodeType::Boss ? TEXT("bgm_combat_boss")
				: (CurrentEncounter.Type == EMapNodeType::Elite ? TEXT("bgm_combat_elite") : TEXT("bgm_combat"));
			break;
		case EGameScreen::Event:
			State = CurrentEncounter.bIsNarrative ? TEXT("bgm_narrative") : TEXT("bgm_event");
			break;
		case EGameScreen::InfiniteNarrative:
			if (bInfiniteFunctionFlowActive && ActiveInfiniteGameOperation.Op == TEXT("choose_upgrade_card"))
				State = TEXT("bgm_upgrade");
			else if (bInfiniteFunctionFlowActive && ActiveInfiniteGameOperation.Op == TEXT("choose_remove_card"))
				State = TEXT("bgm_deckbuilding");
			else
				State = TEXT("bgm_narrative");
			break;
		case EGameScreen::Acquisition:
			State = TEXT("bgm_card_discovery");
			break;
		case EGameScreen::Shop:
			State = TEXT("bgm_shop");
			break;
		case EGameScreen::Rest:
			State = TEXT("bgm_rest");
			break;
		case EGameScreen::Reward:
			State = TEXT("bgm_reward");
			break;
		case EGameScreen::Victory:
			State = TEXT("bgm_victory");
			break;
		case EGameScreen::GameOver:
			State = TEXT("bgm_defeat");
			break;
		default:
			State = TEXT("bgm_map");
			break;
		}
	}

	// A same-screen redraw (card selection, combat log, streaming narrative) must
	// not restart the track. A real screen transition advances that state's A/B pair.
	AudioRouter->SetMusicState(State, PreviousScreen != Screen);
}

void AAscendPlayerController::OnCombatLogDynamic(const FString& Msg)
{
	OnCombatLog(Msg);
}

void AAscendPlayerController::OnCombatLog(const FString& Msg)
{
	CombatLogLines.Add(Msg);
	if (CombatLogLines.Num() > 60)
	{
		CombatLogLines.RemoveAt(0);
		// Keep the animation consumer cursor aligned with the rolling log window.
		// Without this, a long fight eventually leaves LastProcessedLogIndex at 60
		// while the capped array also stays at 60, silently stopping all hit feedback.
		LastProcessedLogIndex = FMath::Max(0, LastProcessedLogIndex - 1);
	}
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
	const bool bSettingsNavigation = Tag == TEXT("settings_save")
		|| Tag == TEXT("settings_back") || Tag == TEXT("settings_tab");
	// Settings navigation is safe even while an RP request is pending: it cannot
	// choose a branch or mutate combat. Gameplay actions remain locked.
	if (bInputLocked && !bMenuAction && !bSettingsNavigation) return;
	if (Tag.Contains(TEXT("back")) || Tag == TEXT("menu_resume"))
		PlayAudioEvent(TEXT("ui_back"), 0.75f);
	else if (Tag == TEXT("lock_target"))
		PlayAudioEvent(TEXT("target_lock"), 0.70f);
	else if (Tag == TEXT("endturn"))
		PlayAudioEvent(TEXT("end_turn"), 0.78f);
	else if (Tag == TEXT("discover_choice"))
		PlayAudioEvent(TEXT("card_pick"), 0.65f);
	else if (Tag == TEXT("menu_open") || Tag == TEXT("settings_save") || Tag == TEXT("settings_tab")
		|| Tag == TEXT("rp_choice") || Tag == TEXT("event_choice") || Tag == TEXT("reward")
		|| Tag == TEXT("reward_skip") || Tag == TEXT("cultivation_choice") || Tag == TEXT("shop_buy") || Tag == TEXT("rest_heal")
		|| Tag == TEXT("rest_upgrade") || Tag == TEXT("start_relic") || Tag == TEXT("node")
		|| Tag == TEXT("pile_draw") || Tag == TEXT("pile_discard") || Tag == TEXT("pile_close"))
		PlayAudioEvent(TEXT("ui_confirm"), 0.70f);
	else if (Tag == TEXT("pill"))
		PlayAudioEvent(TEXT("potion"), 0.75f);
	else
		// Cover title, narrative, retry, close and future buttons by default.
		PlayAudioEvent(TEXT("ui_confirm"), 0.62f);
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
	if (Tag == TEXT("menu_full_settings"))
	{
		SettingsReturnScreen = CurrentScreen;
		// Title settings return to the title directly; only an in-run pause menu
		// reopens its overlay after the full settings page.
		bSettingsReturnToPause = CurrentScreen != EGameScreen::Title;
		SettingsReturnScrollOffset = RPScrollBox ? RPScrollBox->GetScrollOffset() : 0.f;
		HidePauseMenu();
		ShowSettings(SettingsCategory);
		return;
	}
	if (Tag == TEXT("menu_title"))
	{
		RequestLeaveConfirmation(false);
		return;
	}
	if (Tag == TEXT("menu_quit"))
	{
		RequestLeaveConfirmation(true);
		return;
	}
	if (Tag == TEXT("menu_leave_confirm"))
	{
		ConfirmLeaveConfirmation();
		return;
	}
	if (Tag == TEXT("menu_leave_cancel"))
	{
		CancelLeaveConfirmation();
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
		bSettingsReturnToPause = false;
		SettingsReturnScrollOffset = 0.f;
		ShowSettings(SettingsCategory);
	}
	else if (Tag == TEXT("title_continue"))
	{
		InvalidateInfiniteNarrativeFlow();
		if (InfiniteNarrativeService) InfiniteNarrativeService->CancelGeneration();
		if (Run->LoadRun())
		{
			if (Run->State.bInfiniteNarrativeMode)
			{
				FString ResultSummary;
				if (Run->RestorePendingInfiniteCombat(PendingInfiniteEncounter, ResultSummary,
					PendingNarrativeCards, PendingNarrativeRelics))
				{
					if (Run->HasPendingInfiniteOpening())
					{
						Run->ClearPendingInfiniteOpening();
						Run->SaveRun();
					}
					PendingInfiniteChoice = FInfiniteNarrativeChoice();
					PendingInfiniteChoice.ResultSummary = ResultSummary;
					if (PendingNarrativeCards.Num() + PendingNarrativeRelics.Num() > 0) ShowAcquiredItems();
					else BeginPendingInfiniteCombat();
				}
					else if (Run->HasPendingInfiniteOpening())
					{
					if (RestoreAuthoredOpeningFromRunState()) ShowInfiniteNarrative();
					else
					{
						Run->ClearPendingInfiniteOpening();
						Run->SaveRun();
						RequestNextInfiniteNarrative();
						}
					}
					else if (Run->State.bInfiniteFreeRPForcedJumpPending)
					{
						// A free-RP bridge is a persisted two-turn transaction. If the app was
						// closed while the result was waiting to be read, restore that result
						// and keep the explicit continue gate. Only a request that had already
						// crossed the gate is resumed automatically after loading.
						if (Run->State.bInfiniteFreeRPForcedJumpAwaitingContinue)
						{
							if (RestoreFreeRPForcedJumpResultFromRunState()) ShowInfiniteNarrative();
							else
							{
								Run->State.bInfiniteFreeRPForcedJumpAwaitingContinue = false;
								Run->SaveRun();
								bInfiniteFreeRPForcedTurnActive = true;
								RequestNextInfiniteNarrative(Run->State.InfiniteFreeRPForcedDirection,
									EInfiniteNarrativeRequestKind::ForcedFreeRPJump,
									Run->State.InfiniteFreeRPForcedDirection);
							}
						}
						else
						{
							bInfiniteFreeRPForcedTurnActive = true;
							RequestNextInfiniteNarrative(Run->State.InfiniteFreeRPForcedDirection,
								EInfiniteNarrativeRequestKind::ForcedFreeRPJump,
								Run->State.InfiniteFreeRPForcedDirection);
						}
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
		if (bSettingsDraftActive)
			ApplySettingsWidgetsTo(SettingsDraft, false);
		ShowSettings(Index);
	}
	else if (Tag == TEXT("rp_choice"))
	{
		SelectInfiniteNarrativeChoice(Index);
	}
	else if (Tag == TEXT("rp_freeform"))
	{
		if (!IsFreeRPInputAvailableForAutomationTest(InfiniteNarrativeSettings.bShowFreeformInput,
			bInfiniteFreeRPForcedTurnActive
				|| (Run && Run->State.bInfiniteFreeRPForcedJumpPending),
			bInfiniteNarrativeRequestInFlight, bOpeningChoiceWordingPending)) return;
		const FString Action = RPFreeformInput ? RPFreeformInput->GetText().ToString().TrimStartAndEnd() : TEXT("");
		if (!Action.IsEmpty())
			RequestNextInfiniteNarrative(Action, EInfiniteNarrativeRequestKind::Freeform);
	}
	else if (Tag == TEXT("rp_free_rp_continue"))
	{
		const bool bForcedJumpPending = Run && Run->State.bInfiniteFreeRPForcedJumpPending;
		const bool bAwaitingContinue = Run && Run->State.bInfiniteFreeRPForcedJumpAwaitingContinue;
		if (!IsFreeRPForcedContinueAvailableForAutomationTest(
			bForcedJumpPending, bAwaitingContinue,
			bInfiniteNarrativeRequestInFlight || bInfiniteFreeRPForcedTurnActive)) return;
		bInfiniteFreeRPForcedTurnActive = true;
		const FString Direction = Run->State.InfiniteFreeRPForcedDirection;
		RequestNextInfiniteNarrative(Direction,
			EInfiniteNarrativeRequestKind::ForcedFreeRPJump, Direction);
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
		else if ((bInfiniteFreeRPForcedTurnActive
			|| (Run && Run->State.bInfiniteFreeRPForcedJumpPending))
			&& !(Run && Run->State.bInfiniteFreeRPForcedJumpAwaitingContinue))
		{
			bInfiniteFreeRPForcedTurnActive = true;
			const FString Direction = Run ? Run->State.InfiniteFreeRPForcedDirection : FString();
			RequestNextInfiniteNarrative(Direction, EInfiniteNarrativeRequestKind::ForcedFreeRPJump, Direction);
		}
		else RequestNextInfiniteNarrative(PendingInfiniteFreeformAction,
			EInfiniteNarrativeRequestKind::Freeform);
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
		if (DiscardAnimStarts.Num() > 0)
			PlayAudioEvent(TEXT("discard"), 0.52f);

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
	else if (Tag == TEXT("cultivation_choice"))
	{
		if (!Run || Run->State.Cultivation.PendingChoiceCount <= 0 || Index < 0 || Index > 2)
		{
			PlayAudioEvent(TEXT("ui_deny"), 0.62f);
			return;
		}
		const ECultivationChoice Choice = Index == 0 ? ECultivationChoice::BodyTempering
			: Index == 1 ? ECultivationChoice::QiAbsorption : ECultivationChoice::Insight;
		const FCultivationChoiceResult ChoiceResult = Run->ApplyCultivationChoice(Choice);
		if (!ChoiceResult.bApplied)
		{
			PlayAudioEvent(TEXT("ui_deny"), 0.62f);
		}
		else
		{
			PlayAudioEvent(TEXT("breakthrough"), 0.72f);
			Run->SaveRun();
		}
		if (CurrentScreen == EGameScreen::Reward) ShowReward();
		else if (CurrentScreen == EGameScreen::Rest) ShowRest();
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
		if (Run && Run->State.Cultivation.PendingChoiceCount > 0)
		{
			PlayAudioEvent(TEXT("ui_deny"), 0.62f);
			ShowReward();
			return;
		}
		if (PendingReward.CardChoices.IsValidIndex(Index))
		{
			Run->PickRewardCard(PendingReward.CardChoices[Index]);
		}
		if (bInfiniteFunctionFlowActive) CompleteInfiniteGameFunction();
		else ContinueAfterReward();
	}
	else if (Tag == TEXT("reward_skip"))
	{
		if (Run && Run->State.Cultivation.PendingChoiceCount > 0)
		{
			PlayAudioEvent(TEXT("ui_deny"), 0.62f);
			ShowReward();
			return;
		}
		Run->PickRewardCard(FDeckCard());
		if (bInfiniteFunctionFlowActive) CompleteInfiniteGameFunction();
		else ContinueAfterReward();
	}
	else if (Tag == TEXT("reward_to_narrative"))
	{
		if (Run && Run->State.Cultivation.PendingChoiceCount > 0)
		{
			PlayAudioEvent(TEXT("ui_deny"), 0.62f);
			ShowReward();
			return;
		}
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
		const bool bBought = Run->BuyShopItem(Index);
		PlayAudioEvent(bBought ? TEXT("gold") : TEXT("ui_deny"), bBought ? 0.58f : 0.62f);
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
		if (Run && Run->State.Cultivation.PendingChoiceCount > 0)
		{
			PlayAudioEvent(TEXT("ui_deny"), 0.62f);
			ShowRest();
			return;
		}
		Run->ResolveRest(true);
		PlayAudioEvent(TEXT("heal_chime"), 0.70f);
		Run->SaveRun();
		if (bInfiniteFunctionFlowActive) CompleteInfiniteGameFunction();
		else ShowMap();
	}
	else if (Tag == TEXT("rest_upgrade"))
	{
		if (Run && Run->State.Cultivation.PendingChoiceCount > 0)
		{
			PlayAudioEvent(TEXT("ui_deny"), 0.62f);
			ShowRest();
			return;
		}
		Run->ResolveRest(false);
		PlayAudioEvent(TEXT("breakthrough"), 0.72f);
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
			LastProcessedEnemyDamageEventIndex = 0;
			const FCombatDifficultyProfile Difficulty = Run->BeginCombatDifficulty(Enc.EnemyIds, Enc.Type);
			Enc.BattleSerial = Difficulty.BattleSerial;
			Enc.HPScale = Difficulty.HPScale;
			Enc.IntentScale = Difficulty.IntentScale;
			Enc.ThreatScore = Difficulty.ThreatScore;
			Enc.DifficultyTier = Difficulty.Tier;
			CurrentEncounter = Enc;
			if (Combat->StartCombatWithDifficulty(Run->State.Deck, Enc.EnemyIds, Run->State.RelicIds,
				Run->State.MaxHP, Run->State.HP, Enc.EnemyHPBonus,
				FMath::RandRange(1, 999999), Difficulty))
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
	if (!Run || !Run->CurrentChoices.IsValidIndex(ChoiceIndex)) return;
	PlayAudioEvent(TEXT("map_node"), 0.58f);
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
		LastProcessedEnemyDamageEventIndex = 0;

		if (Combat->StartCombatWithDifficulty(Run->State.Deck, CurrentEncounter.EnemyIds, Run->State.RelicIds,
			Run->State.MaxHP, Run->State.HP, CurrentEncounter.EnemyHPBonus,
			FMath::RandRange(1, 999999), Run->GetLastCombatDifficulty()))
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
		PlayAudioEvent(TEXT("victory_stinger"), 1.0f, 0.98f, 1.02f);
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
		PlayAudioEvent(TEXT("defeat_stinger"), 1.0f, 0.98f, 1.02f);
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
	InvalidateInfiniteNarrativeFlow();
	if (InfiniteNarrativeService) InfiniteNarrativeService->CancelGeneration();
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
	// Android 真触摸由 InputTouch 统一处理；部分设备将触摸转成鼠标事件，
	// 额外保留鼠标释放兜底。卡牌按钮使用 MouseDown/Down，不会捕获移动。
	InputComponent->BindKey(EKeys::LeftMouseButton, IE_Released, this,
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
	if (ScreenHost && ScreenShakeTrauma > KINDA_SMALL_NUMBER)
	{
		ScreenShakeTrauma = FMath::Max(0.f, ScreenShakeTrauma - ScreenShakeDecayRate * DeltaTime);
		ScreenShakePhase += DeltaTime * 30.f;
		const float Strength = ScreenShakeTrauma * ScreenShakeTrauma;
		const float XWave = 0.62f * FMath::Sin(ScreenShakePhase * 1.73f)
			+ 0.38f * FMath::Sin(ScreenShakePhase * 2.91f + 0.7f);
		const float YWave = 0.58f * FMath::Sin(ScreenShakePhase * 2.17f + 1.2f)
			+ 0.42f * FMath::Sin(ScreenShakePhase * 3.37f);
		ScreenHost->SetRenderTranslation(FVector2D(12.f * Strength * XWave, 8.f * Strength * YWave));
		if (ScreenShakeTrauma <= KINDA_SMALL_NUMBER)
			ScreenHost->SetRenderTranslation(FVector2D::ZeroVector);
	}
	if (AnimatedHoverCardVisual.IsValid() && AnimatedHoverCardButton.IsValid())
	{
		const float Target = bHandHoverTargetVisible ? 1.f : 0.f;
		HandHoverAnimationAlpha = FMath::FInterpTo(HandHoverAnimationAlpha, Target,
			DeltaTime, bHandHoverTargetVisible ? 13.f : 18.f);
		if (FMath::Abs(HandHoverAnimationAlpha - Target) < 0.002f)
			HandHoverAnimationAlpha = Target;
		const float Ease = 1.f - FMath::Pow(1.f - HandHoverAnimationAlpha, 3.f);
#if PLATFORM_ANDROID
		const float PreviewLift = TouchHandHoverLift;
#else
		const float PreviewLift = HandHoverLift;
#endif
		// The focus card is rebuilt at its final size in ShowCardPreview.  Only
		// opacity/translation are animated here; scaling the composed card (even
		// by a small overshoot) reintroduces intermittent bilinear blur.
		AnimatedHoverCardVisual->SetRenderTransformAngle(0.f);
		AnimatedHoverCardVisual->SetRenderTranslation(FVector2D(0.f, -PreviewLift * Ease));
		AnimatedHoverCardVisual->SetRenderScale(FVector2D(1.f, 1.f));
		AnimatedHoverCardVisual->SetRenderOpacity(Ease);
		if (!bHandHoverTargetVisible && HandHoverAnimationAlpha <= 0.f)
		{
			AnimatedHoverCardVisual->RemoveFromParent();
			if (AnimatedHoverCardButton.IsValid())
			{
				if (UWidget* OriginalVisual = AnimatedHoverCardButton->GetContent())
				{
					OriginalVisual->SetRenderTransformAngle(HoveredCardOriginalAngle);
					OriginalVisual->SetRenderTranslation(FVector2D::ZeroVector);
					OriginalVisual->SetRenderScale(FVector2D(1.f, 1.f));
				}
				AnimatedHoverCardButton->SetRenderOpacity(1.f);
				if (UCanvasPanelSlot* Slot = Cast<UCanvasPanelSlot>(AnimatedHoverCardButton->Slot))
					Slot->SetZOrder(AnimatedHoverCardIndex);
			}
			AnimatedHoverCardButton.Reset();
			AnimatedHoverCardVisual.Reset();
			AnimatedHoverCardIndex = INDEX_NONE;
			HoveredCardOriginalAngle = 0.f;
		}
	}
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

bool AAscendPlayerController::InputTouch(const FTouchId TouchId, const ETouchType::Type Type,
	const FVector2D& TouchLocation, const float Force, const uint64 Timestamp)
{
	const bool bHandledByParent = Super::InputTouch(TouchId, Type, TouchLocation, Force, Timestamp);

#if PLATFORM_ANDROID
	// PlayerController::InputTouch is fed by the viewport independently of Slate/UMG.
	// That makes it the reliable lifecycle for a drag: SButton may stop receiving
	// OnReleased once a finger leaves its hit box, while the viewport still reports
	// every move and the final release. Ignore the synthetic cursor pointer.
	if (TouchId.GetIndex() < ETouchIndex::CursorPointerIndex)
	{
		if (Type == ETouchType::Began)
		{
			LastTouchEventIndex = TouchId.GetIndex();
			bHasLastTouchEvent = true;
		}
		if (Type == ETouchType::Began)
		{
			const bool bDragAlreadyStartedByButton = bIsDraggingCard && bDragUsingTouch;
			if (bDragAlreadyStartedByButton)
			{
				// UButton::OnPressed may be the first reliable hit-test on Android.
				// Bind this raw touch to the drag it just started.
				if (!bHasActiveDragTouch)
				{
					ActiveDragTouchIndex = TouchId.GetIndex();
					bHasActiveDragTouch = true;
				}
				return bHandledByParent;
			}
			bHasActiveDragTouch = false;

			// Resolve the top-most hand card directly from the same cached hit
			// geometries that Slate uses, with a scaled fallback for devices
			// applying viewport DPI. UButton::OnPressed is also wired as a fallback.
			int32 TouchedCardIndex = INDEX_NONE;
			if (Combat && Combat->bCombatActive && CurrentScreen == EGameScreen::Combat
				&& Combat->PendingDiscoverChoices.Num() == 0 && !Combat->bPlayerTurnSkipped)
			{
				for (int32 Index = HandCardButtons.Num() - 1; Index >= 0; --Index)
				{
					if (!HandCardButtons[Index].IsValid() || !HandCardButtons[Index]->GetIsEnabled()) continue;
					if (HandCardButtons[Index]->GetCachedGeometry().IsUnderLocation(TouchLocation))
					{
						TouchedCardIndex = Index;
						break;
					}
				}

				if (TouchedCardIndex == INDEX_NONE && AnimCanvas)
				{
					const FGeometry CanvasGeometry = AnimCanvas->GetTickSpaceGeometry();
					const FVector2D CanvasSize = CanvasGeometry.GetLocalSize();
					const FVector2D ViewportSize = GetViewportSize();
					if (CanvasSize.X > 1.f && CanvasSize.Y > 1.f
						&& ViewportSize.X > 1.f && ViewportSize.Y > 1.f)
					{
						const FVector2D ScaledLocal(
							TouchLocation.X * CanvasSize.X / ViewportSize.X,
							TouchLocation.Y * CanvasSize.Y / ViewportSize.Y);
						const FVector2D ScaledScreen = CanvasGeometry.LocalToAbsolute(ScaledLocal);
						for (int32 Index = HandCardButtons.Num() - 1; Index >= 0; --Index)
						{
							if (!HandCardButtons[Index].IsValid() || !HandCardButtons[Index]->GetIsEnabled()) continue;
							if (HandCardButtons[Index]->GetCachedGeometry().IsUnderLocation(ScaledScreen))
							{
								TouchedCardIndex = Index;
								break;
							}
						}
					}
				}
			}

			if (TouchedCardIndex != INDEX_NONE)
			{
				HandleCardPressed(TouchedCardIndex);
				if (bIsDraggingCard)
				{
					ActiveDragTouchIndex = TouchId.GetIndex();
					bHasActiveDragTouch = true;
				}
			}
		}
		else if (bIsDraggingCard && bDragUsingTouch
			&& (!bHasActiveDragTouch || TouchId.GetIndex() == ActiveDragTouchIndex))
		{
			// Prefer the same Slate screen coordinates used by the root widget.
			// If a vendor reports physical pixels while Slate uses logical units,
			// use the scaled conversion when the direct position is outside Canvas.
			if (AnimCanvas)
			{
				const FGeometry CanvasGeometry = AnimCanvas->GetTickSpaceGeometry();
				const FVector2D CanvasSize = CanvasGeometry.GetLocalSize();
				const FVector2D DirectLocal = CanvasGeometry.AbsoluteToLocal(TouchLocation);
				const bool bDirectInBounds = DirectLocal.X >= -32.f && DirectLocal.Y >= -32.f
					&& DirectLocal.X <= CanvasSize.X + 32.f && DirectLocal.Y <= CanvasSize.Y + 32.f;
				if (bDirectInBounds)
				{
					HandleRootPointerMoved(TouchLocation);
				}
				else
				{
					const FVector2D ViewportSize = GetViewportSize();
					if (CanvasSize.X > 1.f && CanvasSize.Y > 1.f
						&& ViewportSize.X > 1.f && ViewportSize.Y > 1.f)
					{
						const FVector2D ScaledLocal(
							TouchLocation.X * CanvasSize.X / ViewportSize.X,
							TouchLocation.Y * CanvasSize.Y / ViewportSize.Y);
						if (ScaledLocal.X >= -32.f && ScaledLocal.Y >= -32.f
							&& ScaledLocal.X <= CanvasSize.X + 32.f && ScaledLocal.Y <= CanvasSize.Y + 32.f)
						{
							WidgetTouchCanvasPosition = ScaledLocal;
							bHasWidgetTouchPosition = true;
						}
					}
				}
			}

			if (Type == ETouchType::Ended)
			{
				// Consume the release location before resolving the play zone so a
				// final move event is never required to play a card.
				UpdateCardDrag();
				EndCardDrag();
				bHasActiveDragTouch = false;
			}
		}
		if (Type == ETouchType::Ended && TouchId.GetIndex() == LastTouchEventIndex)
			bHasLastTouchEvent = false;
	}
#endif

	return bHandledByParent;
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
	const float CardW = N <= 1 ? 220.f : (N == 2 ? 198.f : (N == 3 ? 176.f : 158.f));
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
	float CardSlotW = CardW + Gap;
	float TotalLog = N * CardSlotW;
	float FillW = (VW - HPad - TotalLog) * 0.5f;
	float XLog = HBoxLeft + FillW + EnemyIndex * CardSlotW + 8.f + CardW * 0.5f;
	FVector2D LogicalPos(XLog, VH * 0.18f);

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
			FixedDescription = FString::Printf(TEXT("对所有敌人造成 %d 点伤害（每层强化+3）。保留，消失"),
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
		DisplayDesc = FString::Printf(TEXT("对所有敌人造成 %d 点伤害（每层强化+3）。保留，消失"), TotalDmg);
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
// 手牌悬停放大（原牌保留命中区，焦点层按最终尺寸重建并立正上抬）
// -----------------------------------------------------------

void AAscendPlayerController::ShowCardPreview(int32 CardIndex)
{
	if (!Combat || !Combat->Hand.IsValidIndex(CardIndex)) return;
	if (!HandCardButtons.IsValidIndex(CardIndex) || !HandCardButtons[CardIndex].IsValid()) return;
	if (bIsDraggingCard) return; // 拖拽时不弹预览
	if (!AnimCanvas) return;

	UButton* HoveredCard = HandCardButtons[CardIndex].Get();
	UWidget* CardVisual = HoveredCard->GetContent();
	if (!CardVisual) return;
	if (AnimatedHoverCardVisual.IsValid() && AnimatedHoverCardVisual.Get() != CardVisual)
	{
		// The previous focus layer is a separate crisp rebuild. Remove it before
		// creating the new one and restore the original fan card underneath.
		AnimatedHoverCardVisual->RemoveFromParent();
		if (AnimatedHoverCardButton.IsValid())
		{
			if (UWidget* OriginalVisual = AnimatedHoverCardButton->GetContent())
			{
				OriginalVisual->SetRenderTransformAngle(HoveredCardOriginalAngle);
				OriginalVisual->SetRenderTranslation(FVector2D::ZeroVector);
				OriginalVisual->SetRenderScale(FVector2D(1.f, 1.f));
			}
			AnimatedHoverCardButton->SetRenderOpacity(1.f);
			if (UCanvasPanelSlot* OldSlot = Cast<UCanvasPanelSlot>(AnimatedHoverCardButton->Slot))
				OldSlot->SetZOrder(AnimatedHoverCardIndex);
		}
		AnimatedHoverCardButton.Reset();
		AnimatedHoverCardVisual.Reset();
		AnimatedHoverCardIndex = INDEX_NONE;
	}

	HoveredCardIndex = CardIndex;
	HoveredCardOriginalAngle = CardVisual->GetRenderTransform().Angle;
	AnimatedHoverCardButton = HoveredCard;
	AnimatedHoverCardIndex = CardIndex;
	HandHoverAnimationAlpha = 0.f;
	bHandHoverTargetVisible = true;

	// Rebuild at the complete focus size.  This avoids enlarging the small
	// hand-layout card through RenderScale and guarantees that title/description
	// text is rasterized once at a discrete final font size.
	#if PLATFORM_ANDROID
	const float FocusScale = CurrentHandCardScale * TouchHandHoverScale;
	#else
	const float FocusScale = CurrentHandCardScale * HandHoverScale;
	#endif
	const FVector2D FocusSize(
		SnapCardPixel(AscendCardLayout::Width * FocusScale),
		SnapCardPixel(AscendCardLayout::Height * FocusScale));
	UButton* FocusCard = NewObject<UButton>(AnimCanvas);
	FocusCard->SetBackgroundColor(FLinearColor::Transparent);
	BuildCardWidget(FocusCard, CardIndex, true, FocusScale);
	FocusCard->SetVisibility(ESlateVisibility::HitTestInvisible);
	FocusCard->SetRenderTransformPivot(FVector2D(0.5f, 0.5f));
	FocusCard->SetRenderTransformAngle(0.f);
	FocusCard->SetRenderScale(FVector2D(1.f, 1.f));
	FocusCard->SetRenderOpacity(0.f);

	FGeometry CanvasGeometry = AnimCanvas->GetTickSpaceGeometry();
	FGeometry CardGeometry = HoveredCard->GetCachedGeometry();
	FVector2D BasePosition = CanvasGeometry.AbsoluteToLocal(CardGeometry.GetAbsolutePosition());
	FVector2D BaseSize = CardGeometry.GetLocalSize();
	if (BaseSize.X < 1.f || BaseSize.Y < 1.f)
		BaseSize = FVector2D(
			SnapCardPixel(AscendCardLayout::Width * CurrentHandCardScale),
			SnapCardPixel(AscendCardLayout::Height * CurrentHandCardScale));
	FVector2D FocusPosition = BasePosition + BaseSize * 0.5f - FocusSize * 0.5f;
	FocusPosition.X = SnapCardPixel(FocusPosition.X);
	FocusPosition.Y = SnapCardPixel(FocusPosition.Y);
	if (UCanvasPanelSlot* FocusSlot = AnimCanvas->AddChildToCanvas(FocusCard))
	{
		FocusSlot->SetPosition(FocusPosition);
		FocusSlot->SetSize(FocusSize);
		FocusSlot->SetZOrder(HandHoverZOrder);
	}
	AnimatedHoverCardVisual = FocusCard;

	// 若鼠标在入场动画尚未结束时进入，立即固定按钮命中区域到最终位置；
	// 原按钮仍负责命中测试，焦点层本身是 HitTestInvisible。
	HoveredCard->SetRenderTranslation(FVector2D::ZeroVector);
	HoveredCard->SetRenderScale(FVector2D(1.f, 1.f));
	HoveredCard->SetRenderOpacity(0.f);
}

void AAscendPlayerController::HideCardPreview(int32 CardIndex)
{
	// 快速扫过重叠手牌时，旧卡的 Unhover 不应取消新卡的悬停状态。
	if (CardIndex != INDEX_NONE && HoveredCardIndex != CardIndex) return;

	bHandHoverTargetVisible = false;
	HoveredCardIndex = INDEX_NONE;
}

void AAscendPlayerController::BuildEnemyCardWidget(UVerticalBox* EBox, int32 EnemyIndex, bool bIsLocked)
{
	if (!Combat || !Combat->Enemies.IsValidIndex(EnemyIndex)) return;
	const FEnemyCombatant& E = Combat->Enemies[EnemyIndex];
	const bool bAlive = E.State.IsAlive();

	const int32 EnemyCount = Combat->Enemies.Num();
	const float CardWidth = EnemyCount <= 1 ? 220.f : (EnemyCount == 2 ? 198.f : (EnemyCount == 3 ? 176.f : 158.f));
	const float CardHeight = CardWidth * 1.40f;
	const float InfoScale = CardWidth / 150.f;
	USizeBox* Sizer = NewObject<USizeBox>(EBox);
	Sizer->SetWidthOverride(CardWidth);
	Sizer->SetHeightOverride(CardHeight);

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

	UTextBlock* NameT = FAscendUIStyle::MakeText(TV, E.State.Name,
		FMath::RoundToInt(14.f * InfoScale),
		bAlive ? FAscendUIStyle::BloodRed() : FAscendUIStyle::DimGray());
	NameT->SetJustification(ETextJustify::Center);
	TV->AddChildToVerticalBox(NameT);

	UProgressBar* HPBar = NewObject<UProgressBar>(TV);
	HPBar->SetPercent(bAlive ? (float)E.State.HP / E.State.MaxHP : 0.f);
	HPBar->SetFillColorAndOpacity(FLinearColor(0.75f, 0.22f, 0.18f));
	TV->AddChildToVerticalBox(HPBar);

	UTextBlock* HPT = FAscendUIStyle::MakeText(TV,
		FString::Printf(TEXT("%d/%d  罡气 %d"), E.State.HP, E.State.MaxHP, E.State.Block),
		FMath::RoundToInt(12.f * InfoScale), FAscendUIStyle::PaperWhite());
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
		BadgeSize->SetWidthOverride(22.f * InfoScale);
		BadgeSize->SetHeightOverride(22.f * InfoScale);
		UBorder* Badge = NewObject<UBorder>(BadgeSize);
		Badge->SetBrushColor(BadgeCol);
		UTextBlock* GlyphT = FAscendUIStyle::MakeText(Badge, Glyph,
			FMath::RoundToInt(11.f * InfoScale), FAscendUIStyle::PaperWhite());
		GlyphT->SetJustification(ETextJustify::Center);
		Badge->SetContent(GlyphT);
		BadgeSize->SetContent(Badge);
		IntentRow->AddChildToHorizontalBox(BadgeSize);

		if (!ValStr.IsEmpty())
		{
			UTextBlock* ValT = FAscendUIStyle::MakeText(IntentRow, ValStr,
				FMath::RoundToInt(12.f * InfoScale), FAscendUIStyle::GoldYellow());
			UHorizontalBoxSlot* ValSlot = IntentRow->AddChildToHorizontalBox(ValT);
			ValSlot->SetPadding(FMargin(4.f, 1.f, 0.f, 0.f));
		}
		IntentRow->AddChildToHorizontalBox(NewObject<USpacer>(TV))->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		TV->AddChildToVerticalBox(IntentRow)->SetPadding(FMargin(0.f, 2.f, 0.f, 1.f));
	}

	if (UHorizontalBox* StatusRow = BuildStatusRow(TV, E.State,
		FMath::RoundToInt(10.f * InfoScale)))
	{
		TV->AddChildToVerticalBox(StatusRow)->SetPadding(FMargin(0.f, 2.f));
	}

	// 特殊机制说明（小字，灰金色）
	if (!E.Data.AbilityDesc.IsEmpty())
	{
		UTextBlock* AbilT = FAscendUIStyle::MakeText(TV, E.Data.AbilityDesc,
			FMath::Max(9, FMath::RoundToInt(8.f * InfoScale)),
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

	// A touch can arrive through both UButton::OnPressed and the viewport
	// lifecycle. Ignore that duplicate for the same card; a second card press
	// still cancels the current drag as before.
	if (bIsDraggingCard)
	{
		if (DragCardIndex == CardIndex) return;
		EndCardDrag();
		return;
	}

	HideCardPreview();

	if (Combat->GetEffectiveCost(Combat->Hand[CardIndex]) > Combat->Spirit)
	{
		PlayAudioEvent(TEXT("ui_deny"), 0.8f);
		return;
	}

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
	if (bDragUsingTouch && !bHasActiveDragTouch && bHasLastTouchEvent)
	{
		// When UButton::OnPressed wins the race with InputTouch::Began, retain
		// the finger identity recorded by the viewport event.
		ActiveDragTouchIndex = LastTouchEventIndex;
		bHasActiveDragTouch = true;
	}
	PlayAudioEvent(TEXT("card_pick"), 0.58f, 0.99f, 1.01f);
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
		// Android 的 UMG 触点可能被卡牌按钮捕获，导致 RootWidget 收不到
		// TouchMoved。每帧从 PlayerInput 读取当前手指位置，彻底绕过 Slate
		// 的冒泡/捕获；InputTouch/RootWidget 仍作为事件驱动的低延迟更新。
		FVector2D PolledTouchViewportPosition = FVector2D::ZeroVector;
		bool bFoundPolledTouch = false;
		ETouchIndex::Type PolledTouchIndex = ActiveDragTouchIndex;
		auto TryReadTouch = [this](ETouchIndex::Type TouchIndex, FVector2D& OutPosition, bool bAcceptUnpressed)
		{
			float X = 0.f;
			float Y = 0.f;
			bool bPressed = false;
			GetInputTouchState(TouchIndex, X, Y, bPressed);
			if ((!bPressed && !bAcceptUnpressed) || !FMath::IsFinite(X) || !FMath::IsFinite(Y)) return false;
			// Some Android input layers expose an unused Touch1 at (0,0). A card
			// cannot start there, so ignore that slot instead of snapping the drag.
			if (X <= 2.f && Y <= 2.f) return false;
			OutPosition = FVector2D(X, Y);
			return true;
		};

		if (bHasActiveDragTouch)
		{
			bFoundPolledTouch = TryReadTouch(ActiveDragTouchIndex, PolledTouchViewportPosition, true);
		}
		if (!bFoundPolledTouch)
		{
			for (int32 TouchIndex = 0; TouchIndex < EKeys::NUM_TOUCH_KEYS; ++TouchIndex)
			{
				if (TryReadTouch(static_cast<ETouchIndex::Type>(TouchIndex), PolledTouchViewportPosition, false))
				{
					bFoundPolledTouch = true;
					PolledTouchIndex = static_cast<ETouchIndex::Type>(TouchIndex);
					break;
				}
			}
		}
		if (bFoundPolledTouch && !bHasActiveDragTouch)
		{
			// If UButton started the drag before PlayerController::InputTouch
			// receives Began, bind the currently pressed finger on this first tick.
			ActiveDragTouchIndex = PolledTouchIndex;
			bHasActiveDragTouch = true;
		}

		if (bFoundPolledTouch && AnimCanvas)
		{
			const FGeometry CanvasGeometry = AnimCanvas->GetTickSpaceGeometry();
			const FVector2D CanvasSize = CanvasGeometry.GetLocalSize();
			const FVector2D ViewportSize = GetViewportSize();
			if (CanvasSize.X > 1.f && CanvasSize.Y > 1.f
				&& ViewportSize.X > 1.f && ViewportSize.Y > 1.f)
			{
				// GetInputTouchState is viewport-relative; Canvas is DPI-scaled.
				// Convert by ratio so the finger delta matches the rendered card.
				const FVector2D PolledCanvasPosition(
					PolledTouchViewportPosition.X * CanvasSize.X / ViewportSize.X,
					PolledTouchViewportPosition.Y * CanvasSize.Y / ViewportSize.Y);
				if (FMath::IsFinite(PolledCanvasPosition.X) && FMath::IsFinite(PolledCanvasPosition.Y)
					&& PolledCanvasPosition.X >= -64.f && PolledCanvasPosition.Y >= -64.f
					&& PolledCanvasPosition.X <= CanvasSize.X + 64.f
					&& PolledCanvasPosition.Y <= CanvasSize.Y + 64.f)
				{
					WidgetTouchCanvasPosition = PolledCanvasPosition;
					bHasWidgetTouchPosition = true;
				}
			}
		}

		// Keep the last valid event position for the release frame, when
		// GetInputTouchState has already changed the finger to "not pressed".
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
		// Dragging must not leave the fading focus layer above the drag ghost. A
		// drag is an explicit state transition, so complete the hover teardown
		// immediately while preserving the original fan card underneath.
		if (AnimatedHoverCardVisual.IsValid())
			AnimatedHoverCardVisual->RemoveFromParent();
		if (AnimatedHoverCardButton.IsValid())
		{
			if (UWidget* OriginalVisual = AnimatedHoverCardButton->GetContent())
			{
				OriginalVisual->SetRenderTransformAngle(HoveredCardOriginalAngle);
				OriginalVisual->SetRenderTranslation(FVector2D::ZeroVector);
				OriginalVisual->SetRenderScale(FVector2D(1.f, 1.f));
			}
			// This may be a different card from the one that started the drag when
			// the pointer crossed overlapping fan cards. Restore it; the actual drag
			// card is hidden explicitly below by DragCardIndex.
			AnimatedHoverCardButton->SetRenderOpacity(1.f);
		}
		AnimatedHoverCardButton.Reset();
		AnimatedHoverCardVisual.Reset();
		AnimatedHoverCardIndex = INDEX_NONE;
		HandHoverAnimationAlpha = 0.f;

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

		const float DragVisualScale = GetDragCardScale(CurrentHandCardScale, bDragUsingTouch);
		const FVector2D DragVisualSize(
			SnapCardPixel(AscendCardLayout::Width * DragVisualScale),
			SnapCardPixel(AscendCardLayout::Height * DragVisualScale));
		const FVector2D DragBorderSize = DragVisualSize + FVector2D(6.f, 6.f);
		UButton* Ghost = NewObject<UButton>(AnimCanvas);
		// Build the drag ghost directly at its final size.  The highlight border
		// adds a fixed 3px inset, but neither it nor the card is RenderScaled.
		BuildCardWidget(Ghost, DragCardIndex, true, DragVisualScale);

		// Wrap in highlight border
		UBorder* HBorder = NewObject<UBorder>(AnimCanvas);
		HBorder->SetBrushColor(FLinearColor(0.f, 0.f, 0.f, 0.f));
		HBorder->SetPadding(FMargin(3.f));
		HBorder->SetContent(Ghost);
		GhostHighlightBorder = HBorder;
		DraggedCardWidget = HBorder;
		PlayAudioEvent(TEXT("card_drag"), 0.52f, 0.98f, 1.02f);

		UCanvasPanelSlot* Slot = AnimCanvas->AddChildToCanvas(HBorder);
		const FVector2D BaseCardSize = FVector2D(CardW, CardH);
		const FVector2D BaseCenter = HandLocal + BaseCardSize * 0.5f;
		Slot->SetPosition(BaseCenter - DragBorderSize * 0.5f);
		Slot->SetSize(DragBorderSize);
		Slot->SetAutoSize(false);
		const float DragLift = bDragUsingTouch ? 54.f : 35.f;
		HBorder->SetRenderTransformPivot(FVector2D(0.5f, 0.5f));
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
			const float DragVisualScale = GetDragCardScale(CurrentHandCardScale, bDragUsingTouch);
			const FVector2D CardHalfSize(
				SnapCardPixel(AscendCardLayout::HalfWidth * DragVisualScale + 3.f),
				SnapCardPixel(AscendCardLayout::HalfHeight * DragVisualScale + 3.f));
			Slot->SetPosition(MousePos - CardHalfSize);
		}
		const float DragLift = bDragUsingTouch ? 54.f : 35.f;
		DraggedCardWidget->SetRenderScale(FVector2D(1.f, 1.f));
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
	bHasActiveDragTouch = false;
	bHasLastTouchEvent = false;
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
			const float DragVisualScale = GetDragCardScale(CurrentHandCardScale, bDragUsingTouch);
			CardCenterY = S->GetPosition().Y
				+ SnapCardPixel(AscendCardLayout::HalfHeight * DragVisualScale + 3.f);
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
				if (GetWorld())
				{
					PlayCardVisual(PlayedCard, PlayTarget);
					TriggerCombatAnimations(TEXT("card"));
					const FString FinisherAnimation = AscendCardVisual::ResolveAnimation(PlayedCard);
					const float FinishDelay = FinisherAnimation == TEXT("greatsword") ? 1.08f
						: (FinisherAnimation == TEXT("myriad_swords") ? 1.12f
							: (FinisherAnimation == TEXT("sword_wave") || FinisherAnimation == TEXT("thunder") ? 0.82f
								: ((FinisherAnimation == TEXT("flame_burst") || FinisherAnimation == TEXT("poison"))
									? 0.72f : 0.66f)));
					GetWorld()->GetTimerManager().SetTimer(CombatEndTimer, this,
						&AAscendPlayerController::FinishCombatDelayed, FinishDelay, false);
				}
				return;
			}
			RefreshCombatPanel();
			PlayCardVisual(PlayedCard, PlayTarget);
			TriggerCombatAnimations(TEXT("card"));
			return;
		}
	}
	if (!bPlayCard)
		PlayAudioEvent(TEXT("card_invalid"), 0.52f);

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
	if (!AnimCanvas || !DraggedCardWidget || !GetWorld()) return;

	FVector2D CardPos(0.f);
	if (UCanvasPanelSlot* CS = Cast<UCanvasPanelSlot>(DraggedCardWidget->Slot))
	{
		const float DragVisualScale = GetDragCardScale(CurrentHandCardScale, bDragUsingTouch);
		CardPos = CS->GetPosition() + FVector2D(
			SnapCardPixel(AscendCardLayout::HalfWidth * DragVisualScale + 3.f),
			SnapCardPixel(AscendCardLayout::HalfHeight * DragVisualScale + 3.f));
	}
	FVector2D EnemyPos = GetEnemyScreenPos(EnemyIndex);
	FVector2D Delta = EnemyPos - CardPos;
	float Len = Delta.Size();
	if (Len < 1.f)
	{
		HideAttackLine();
		return;
	}

	// 鼠标移动事件可能远高于帧率。几何变化很小时直接复用现有线条，
	// 连续拖动时最多 30Hz 更新位置，避免每次 Tick 创建/销毁 51 个 UBorder。
	const double Now = FPlatformTime::Seconds();
	const bool bHasCachedLine = AttackLineSegments.Num() == 51 && AttackLineEnemyIndex >= 0;
	if (bHasCachedLine && AttackLineEnemyIndex == EnemyIndex
		&& FVector2D::Distance(CardPos, AttackLineLastCardPosition) < 6.f
		&& FVector2D::Distance(EnemyPos, AttackLineLastEnemyPosition) < 3.f)
	{
		return;
	}
	if (bHasCachedLine && Now - AttackLineLastBuildTime < (1.0 / 30.0))
	{
		return;
	}

	// Quadratic Bezier control point
	FVector2D Mid = (CardPos + EnemyPos) * 0.5f;
	FVector2D Perp(-Delta.Y, Delta.X);
	Perp.Normalize();
	FVector2D P0 = CardPos;
	FVector2D P1 = Mid + Perp * 40.f;
	FVector2D P2 = EnemyPos;

	const bool bRestartSpring = !bHasCachedLine || AttackLineEnemyIndex != EnemyIndex;
	auto SetSeg = [&](int32 SegmentIndex, FVector2D From, FVector2D To, FLinearColor Color, float Thick, FVector2D Offset)
	{
		FVector2D D = To - From;
		float L = D.Size();
		if (!AttackLineSegments.IsValidIndex(SegmentIndex))
		{
			UBorder* NewSeg = NewObject<UBorder>(AnimCanvas);
			AttackLineSegments.Add(NewSeg);
		}
		UBorder* Seg = AttackLineSegments[SegmentIndex];
		if (!Seg) return;
		if (Seg->GetParent() != AnimCanvas)
		{
			if (Seg->GetParent()) Seg->RemoveFromParent();
			AnimCanvas->AddChildToCanvas(Seg);
		}
		if (!ActiveAnimations.Contains(Seg)) ActiveAnimations.Add(Seg);
		Seg->SetVisibility(ESlateVisibility::Visible);
		Seg->SetBrushColor(Color);
		if (L < 0.5f)
		{
			Seg->SetVisibility(ESlateVisibility::Collapsed);
			return;
		}
		float A = FMath::RadiansToDegrees(FMath::Atan2(D.Y, D.X));
		UCanvasPanelSlot* S = Cast<UCanvasPanelSlot>(Seg->Slot);
		if (!S) return;
		S->SetPosition((From + To) * 0.5f - FVector2D(0.f, Thick * 0.5f) + Offset);
		S->SetSize(FVector2D(L, Thick));
		S->SetAutoSize(false);
		S->SetZOrder(9999);
		Seg->SetRenderTransformAngle(A);
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
		SetSeg(i, Bezier(t0), Bezier(t1), ShadowColor, 14.f, ShadowOff);
	}

	// Main curve
	for (int32 i = 0; i < NumSegs; ++i)
	{
		float t0 = (float)i / NumSegs;
		float t1 = (float)(i + 1) / NumSegs;
		SetSeg(NumSegs + i, Bezier(t0), Bezier(t1), MainColor, 10.f, FVector2D::ZeroVector);
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
		SetSeg(48, ArrowTip - AD * SpineLen, ArrowTip, ArrowColor, SpineThick, FVector2D::ZeroVector);
		// Wings
		for (float Sign : {-1.f, 1.f})
		{
			FVector2D Wing = AD.GetRotated(180.f + Sign * 30.f).GetSafeNormal();
			SetSeg(49 + (Sign > 0.f ? 1 : 0), ArrowTip, ArrowTip + Wing * WingLen, ArrowColor, WingThick, FVector2D::ZeroVector);
		}
	}
	AttackLineEnemyIndex = EnemyIndex;
	AttackLineLastCardPosition = CardPos;
	AttackLineLastEnemyPosition = EnemyPos;
	AttackLineLastBuildTime = Now;

	// 只有首次显示或切换目标时播放弹性，连续拖动不重复创建定时器。
	if (bRestartSpring)
	{
		if (AttackLineTimerHandle.IsValid())
			GetWorld()->GetTimerManager().ClearTimer(*AttackLineTimerHandle);
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
			Seg->SetVisibility(ESlateVisibility::Collapsed);
		}
	}
	AttackLineEnemyIndex = -1;
	AttackLineLastCardPosition = FVector2D::ZeroVector;
	AttackLineLastEnemyPosition = FVector2D::ZeroVector;
	AttackLineLastBuildTime = 0.0;
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
	ScreenShakeTrauma = 0.f;
	ScreenShakePhase = 0.f;
	if (ScreenHost) ScreenHost->SetRenderTransform(FWidgetTransform());
	// Focus cards live on AnimCanvas rather than in ActiveAnimations.  Remove
	// the crisp layer synchronously when a screen is rebuilt, otherwise the old
	// layer can survive one frame above the new combat panel.
	if (AnimatedHoverCardVisual.IsValid())
		AnimatedHoverCardVisual->RemoveFromParent();
	if (AnimatedHoverCardButton.IsValid())
	{
		if (UWidget* OriginalVisual = AnimatedHoverCardButton->GetContent())
		{
			OriginalVisual->SetRenderTransformAngle(HoveredCardOriginalAngle);
			OriginalVisual->SetRenderTranslation(FVector2D::ZeroVector);
			OriginalVisual->SetRenderScale(FVector2D(1.f, 1.f));
		}
		AnimatedHoverCardButton->SetRenderOpacity(1.f);
		if (UCanvasPanelSlot* Slot = Cast<UCanvasPanelSlot>(AnimatedHoverCardButton->Slot))
			Slot->SetZOrder(AnimatedHoverCardIndex);
	}
	AnimatedHoverCardButton.Reset();
	AnimatedHoverCardVisual.Reset();
	AnimatedHoverCardIndex = INDEX_NONE;
	HandHoverAnimationAlpha = 0.f;
	bHandHoverTargetVisible = false;
	HoveredCardOriginalAngle = 0.f;
	UE_LOG(LogTemp, Display, TEXT("[DIAG] ClearAnimations done"));
}

void AAscendPlayerController::SpawnFloatingText(const FString& Text, FLinearColor Color, float X, float Y,
	float Duration, float BaseScale, float HorizontalDrift)
{
	if (!AnimCanvas || !GetWorld()) return;

	UTextBlock* TB = FAscendUIStyle::MakeText(AnimCanvas, Text, 28, Color);
	TB->SetShadowOffset(FVector2D(2.f, 2.f));
	TB->SetShadowColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.88f));
	TB->SetRenderTransformPivot(FVector2D(0.5f, 0.5f));
	UCanvasPanelSlot* Slot = AnimCanvas->AddChildToCanvas(TB);
	Slot->SetPosition(FVector2D(X, Y));
	Slot->SetAutoSize(true);
	TB->SetRenderOpacity(1.f);
	TB->SetRenderScale(FVector2D(BaseScale, BaseScale));
	ActiveAnimations.Add(TB);

	const float StartTime = GetWorld()->GetTimeSeconds();
	TWeakObjectPtr<UTextBlock> WeakTB = TB;
	TSharedPtr<FTimerHandle> TimerHandle = MakeShared<FTimerHandle>();
	GetWorld()->GetTimerManager().SetTimer(*TimerHandle,
		FTimerDelegate::CreateWeakLambda(this, [this, WeakTB, TimerHandle, StartTime, Duration, BaseScale, HorizontalDrift]()
		{
			if (!WeakTB.IsValid())
			{
				if (UWorld* W = GetWorld()) W->GetTimerManager().ClearTimer(*TimerHandle);
				return;
			}
			const float Elapsed = GetWorld()->GetTimeSeconds() - StartTime;
			if (Elapsed >= Duration)
			{
				WeakTB->RemoveFromParent();
				ActiveAnimations.Remove(WeakTB.Get());
				if (UWorld* W = GetWorld()) W->GetTimerManager().ClearTimer(*TimerHandle);
				return;
			}
			const float T = Elapsed / Duration;
			const float Rise = 1.f - FMath::Pow(1.f - T, 3.f);
			const float PopT = FMath::Clamp(T / 0.24f, 0.f, 1.f);
			const float PopScale = BaseScale * (1.f + FMath::Sin(PopT * PI) * 0.24f);
			const float Fade = 1.f - FMath::Clamp((T - 0.46f) / 0.54f, 0.f, 1.f);
			WeakTB->SetRenderTranslation(FVector2D(HorizontalDrift * Rise, -Rise * 58.f));
			WeakTB->SetRenderScale(FVector2D(PopScale, PopScale));
			WeakTB->SetRenderOpacity(Fade);
		}), 0.033f, true);
	AnimTimerHandles.Add(*TimerHandle);
}

void AAscendPlayerController::QueueFloatingText(const FString& Text, FLinearColor Color,
	const FVector2D& Position, float Delay, float Duration, float BaseScale, float HorizontalDrift)
{
	if (!AnimCanvas || !GetWorld()) return;
	if (Delay <= KINDA_SMALL_NUMBER)
	{
		SpawnFloatingText(Text, Color, Position.X, Position.Y, Duration, BaseScale, HorizontalDrift);
		return;
	}

	FTimerHandle Handle;
	GetWorld()->GetTimerManager().SetTimer(Handle,
		FTimerDelegate::CreateWeakLambda(this,
			[this, Text, Color, Position, Duration, BaseScale, HorizontalDrift]()
			{
				SpawnFloatingText(Text, Color, Position.X, Position.Y, Duration, BaseScale, HorizontalDrift);
			}), Delay, false);
	AnimTimerHandles.Add(Handle);
}

void AAscendPlayerController::SpawnStrikeEffect(const FVector2D& Center, ECombatStrikeStyle Style,
	FLinearColor Color, float Duration, float Rotation, float Strength, int32 Variant, float Delay)
{
	if (!AnimCanvas || !GetWorld()) return;
	if (Delay > KINDA_SMALL_NUMBER)
	{
		FTimerHandle DelayedHandle;
		GetWorld()->GetTimerManager().SetTimer(DelayedHandle,
			FTimerDelegate::CreateWeakLambda(this,
				[this, Center, Style, Color, Duration, Rotation, Strength, Variant]()
				{
					SpawnStrikeEffect(Center, Style, Color, Duration, Rotation, Strength, Variant, 0.f);
				}), Delay, false);
		AnimTimerHandles.Add(DelayedHandle);
		return;
	}

	UCombatSlashWidget* Slash = CreateWidget<UCombatSlashWidget>(this, UCombatSlashWidget::StaticClass());
	if (!Slash) return;
	Slash->SetVisibility(ESlateVisibility::HitTestInvisible);
	Slash->SlashColor = Color;
	Slash->StrikeStyle = Style;
	Slash->Rotation = Rotation;
	Slash->Strength = Strength;
	Slash->Variant = Variant;
	Slash->SetSlashProgress(0.f);

	FVector2D EffectSize(340.f, 270.f);
	if (Style == ECombatStrikeStyle::Greatsword) EffectSize = FVector2D(470.f, 430.f);
	else if (Style == ECombatStrikeStyle::MyriadSwords) EffectSize = FVector2D(500.f, 360.f);
	else if (Style == ECombatStrikeStyle::SwordWave) EffectSize = FVector2D(560.f, 300.f);
	else if (Style == ECombatStrikeStyle::Thunder) EffectSize = FVector2D(430.f, 440.f);
	else if (Style == ECombatStrikeStyle::FlameBurst) EffectSize = FVector2D(380.f, 340.f);
	else if (Style == ECombatStrikeStyle::Poison) EffectSize = FVector2D(340.f, 330.f);
	else if (Style == ECombatStrikeStyle::Ward) EffectSize = FVector2D(360.f, 330.f);
	else if (Style == ECombatStrikeStyle::SpiritFlow) EffectSize = FVector2D(330.f, 360.f);
	else if (Style == ECombatStrikeStyle::PowerAura) EffectSize = FVector2D(360.f, 350.f);
	else if (Style == ECombatStrikeStyle::Talisman || Style == ECombatStrikeStyle::Seal)
		EffectSize = FVector2D(340.f, 340.f);
	else if (Style == ECombatStrikeStyle::CurseBurst) EffectSize = FVector2D(350.f, 330.f);
	else if (Style == ECombatStrikeStyle::ImpactBurst) EffectSize = FVector2D(220.f, 190.f);

	UCanvasPanelSlot* Slot = AnimCanvas->AddChildToCanvas(Slash);
	Slot->SetPosition(Center - EffectSize * 0.5f);
	Slot->SetSize(EffectSize);
	Slot->SetZOrder(1100 + FMath::Clamp(Variant, 0, 24));
	ActiveAnimations.Add(Slash);

	const float StartTime = GetWorld()->GetTimeSeconds();
	const float SafeDuration = FMath::Max(0.18f, Duration);
	TWeakObjectPtr<UCombatSlashWidget> WeakSlash = Slash;
	TSharedPtr<FTimerHandle> TimerHandle = MakeShared<FTimerHandle>();
	GetWorld()->GetTimerManager().SetTimer(*TimerHandle,
		FTimerDelegate::CreateWeakLambda(this, [this, WeakSlash, TimerHandle, StartTime, SafeDuration, Style]()
		{
			if (!WeakSlash.IsValid())
			{
				if (UWorld* W = GetWorld()) W->GetTimerManager().ClearTimer(*TimerHandle);
				return;
			}
			const float T = FMath::Clamp((GetWorld()->GetTimeSeconds() - StartTime) / SafeDuration, 0.f, 1.f);
			WeakSlash->SetSlashProgress(T);
			const float FadeStart = Style == ECombatStrikeStyle::Greatsword ? 0.72f
				: (Style == ECombatStrikeStyle::MyriadSwords ? 0.80f
					: (Style == ECombatStrikeStyle::Ward || Style == ECombatStrikeStyle::PowerAura
						|| Style == ECombatStrikeStyle::Talisman || Style == ECombatStrikeStyle::Seal
						|| Style == ECombatStrikeStyle::SpiritFlow ? 0.70f : 0.62f));
			const float Fade = 1.f - FMath::Clamp((T - FadeStart) / FMath::Max(0.01f, 1.f - FadeStart), 0.f, 1.f);
			WeakSlash->SetRenderOpacity(Fade);
			if (T >= 1.f)
			{
				WeakSlash->RemoveFromParent();
				ActiveAnimations.Remove(WeakSlash.Get());
				if (UWorld* W = GetWorld()) W->GetTimerManager().ClearTimer(*TimerHandle);
			}
		}), 0.016f, true);
	AnimTimerHandles.Add(*TimerHandle);
}

void AAscendPlayerController::SpawnImpactBurst(const FVector2D& Center, FLinearColor Color, float Duration)
{
	SpawnStrikeEffect(Center, ECombatStrikeStyle::ImpactBurst, Color,
		FMath::Max(0.14f, Duration), 0.f, 1.f, FMath::Abs(FMath::RoundToInt(Center.X + Center.Y)) % 17);
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
	// 音频路由器现在会规范化旧卡牌别名，并用共享的 impact_hit 兜底。
	// 只要路由器存在，就不再进入运行时程序化合成，避免旧 8-bit 音色和实时采样开销。
	if (AudioRouter)
	{
		AudioRouter->PlaySfx(SoundId);
		return;
	}
	// 音频路由器正常在 BeginPlay 创建；没有路由器时也静默跳过，
	// 不再生成任何临时程序化音频，避免质量和实时负载问题。
	return;
#if 0
	if (InfiniteNarrativeSettings.SfxVolume <= 0.001f) return;

	const int32 SampleRate = 44100;
	const bool bSword = SoundId.Contains(TEXT("sword"));
	const bool bHeavySword = SoundId.Contains(TEXT("heavy"));
	const bool bSwordFlurry = SoundId.Contains(TEXT("flurry"));
	const bool bSwordWave = SoundId.Contains(TEXT("wave"));
	const bool bFire = SoundId.Contains(TEXT("fire"));
	const bool bThunder = SoundId.Contains(TEXT("thunder"));
	const bool bPoison = SoundId.Contains(TEXT("poison"));
	const bool bBlock = SoundId.Contains(TEXT("block")) || SoundId.Contains(TEXT("ward"));
	const bool bHeal = SoundId.Contains(TEXT("heal"));
	const bool bDraw = SoundId.Contains(TEXT("draw"));
	const bool bSpirit = SoundId.Contains(TEXT("spirit"));
	const bool bPower = SoundId.Contains(TEXT("power"));
	const bool bTalisman = SoundId.Contains(TEXT("talisman"));
	const bool bSeal = SoundId.Contains(TEXT("seal"));
	const bool bCurse = SoundId.Contains(TEXT("curse"));
	const float Duration = bHeavySword ? 0.62f : (bSwordFlurry ? 0.74f : (bSwordWave ? 0.50f
		: (bThunder ? 0.58f : (bFire ? 0.46f : (bPoison ? 0.48f
			: (bSword ? 0.32f : (bHeal || bDraw || bSpirit || bPower || bTalisman || bSeal
				? 0.42f : (bCurse ? 0.52f : 0.28f))))))));
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
		if (bHeavySword)
		{
			// 巨剑：前半是下沉的蓄力风压，0.22s 落地时叠加低频冲击与金属余震。
			const float Windup = FMath::Clamp(T / 0.22f, 0.f, 1.f);
			const float WindFreq = FMath::Lerp(180.f, 62.f, Windup);
			const float Wind = (0.26f + 0.42f * Windup) * SmoothedNoise
				+ 0.28f * FMath::Sin(2.f * PI * WindFreq * T);
			const float ImpactT = FMath::Max(0.f, T - 0.22f);
			const float ImpactGate = T >= 0.22f ? 1.f : 0.f;
			const float Slam = ImpactGate * FMath::Exp(-15.f * ImpactT)
				* (0.82f * FMath::Sin(2.f * PI * 64.f * ImpactT)
					+ 0.30f * FMath::Sin(2.f * PI * 128.f * ImpactT));
			const float Steel = ImpactGate * FMath::Exp(-7.f * ImpactT)
				* (0.23f * FMath::Sin(2.f * PI * 690.f * ImpactT)
					+ 0.14f * FMath::Sin(2.f * PI * 1040.f * ImpactT));
			Signal = Wind * (T < 0.22f ? 0.72f : FMath::Exp(-9.f * ImpactT)) + Slam + Steel
				+ HighNoise * 0.24f * ImpactGate * FMath::Exp(-25.f * ImpactT);
		}
		else if (bSwordFlurry)
		{
			// 万剑：六个独立短促刃风包络，每次命中都有可分辨的高频起音。
			Signal = SmoothedNoise * 0.08f * FMath::Exp(-2.4f * T);
			for (int32 Strike = 0; Strike < 6; ++Strike)
			{
				const float LocalT = T - 0.072f * Strike;
				if (LocalT < 0.f) continue;
				const float Env = FMath::Exp(-34.f * LocalT);
				const float Root = 1520.f + Strike * 125.f;
				Signal += Env * (0.34f * FMath::Sin(2.f * PI * Root * LocalT)
					+ 0.16f * FMath::Sin(2.f * PI * Root * 0.47f * LocalT))
					+ HighNoise * 0.12f * Env;
			}
			const float FinalT = FMath::Max(0.f, T - 0.43f);
			if (T >= 0.43f)
				Signal += FMath::Sin(2.f * PI * 118.f * FinalT) * FMath::Exp(-22.f * FinalT) * 0.38f;
		}
		else if (bSwordWave)
		{
			const float Sweep = FMath::Lerp(1250.f, 96.f, FMath::Pow(NormalizedT, 0.64f));
			Signal = FMath::Exp(-4.2f * T) * (0.50f * FMath::Sin(2.f * PI * Sweep * T)
				+ 0.20f * FMath::Sin(2.f * PI * Sweep * 0.52f * T))
				+ SmoothedNoise * 0.28f * FMath::Exp(-5.4f * T);
		}
		else if (bSword)
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
		else if (bThunder)
		{
			// Lightning: an immediate broadband crack followed by a low rolling tail.
			const float Crack = FMath::Exp(-42.f * T);
			const float Roll = FMath::Exp(-5.2f * T);
			Signal = HighNoise * 0.84f * Crack
				+ SmoothedNoise * 0.30f * Roll
				+ FMath::Sin(2.f * PI * FMath::Lerp(168.f, 48.f, NormalizedT) * T) * 0.42f * Roll;
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
		else if (bPoison)
		{
			// Poison: wet filtered hiss and descending hollow bubbles.
			const float Hiss = SmoothedNoise * 0.36f * FMath::Exp(-3.4f * T);
			const float Bubble = FMath::Sin(2.f * PI * FMath::Lerp(510.f, 96.f, NormalizedT) * T)
				* FMath::Exp(-5.5f * T);
			Signal = Hiss + Bubble * 0.32f + FMath::Sin(2.f * PI * 73.f * T) * 0.12f;
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
		else if (bCurse)
		{
			const float Fall = FMath::Lerp(310.f, 42.f, NormalizedT);
			Signal = FMath::Sin(2.f * PI * Fall * T) * FMath::Exp(-4.2f * T) * 0.44f
				+ SmoothedNoise * 0.26f * (0.25f + NormalizedT) * FMath::Exp(-2.8f * T);
		}
		else if (bHeal || bDraw || bSpirit || bPower || bTalisman || bSeal)
		{
			// Support families use different pentatonic roots and overtones, keeping
			// utility cards gentle but audibly distinct from one another.
			const float Attack = FMath::Clamp(T / 0.018f, 0.f, 1.f);
			const float Release = FMath::Exp(-6.8f * T);
			const float Root = bHeal ? 640.f : (bSpirit ? 720.f : (bPower ? 410.f
				: (bTalisman ? 820.f : (bSeal ? 505.f : 560.f))));
			Signal = Attack * Release * (0.42f * FMath::Sin(2.f * PI * Root * T)
				+ 0.24f * FMath::Sin(2.f * PI * Root * 1.5f * T)
				+ 0.14f * FMath::Sin(2.f * PI * Root * 2.01f * T));
			if (bPower) Signal += FMath::Sin(2.f * PI * 96.f * T) * FMath::Exp(-5.f * T) * 0.30f;
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
	const float FamilyVolume = bHeavySword ? 0.80f : (bSwordFlurry ? 0.70f : (bSwordWave ? 0.68f
		: (bThunder ? 0.76f : (bFire ? 0.72f : (bCurse ? 0.60f : (bSword ? 0.62f : 0.58f))))));
	UGameplayStatics::PlaySound2D(this, Wave,
		FamilyVolume * FMath::Clamp(InfiniteNarrativeSettings.SfxVolume, 0.f, 1.f), 1.f);
#endif
}

void AAscendPlayerController::PlayAudioEvent(const FString& EventId, float VolumeScale,
	float PitchMin, float PitchMax)
{
	if (EventId.IsEmpty() || EventId == TEXT("none")) return;
	if (AudioRouter && AudioRouter->PlaySfx(EventId, VolumeScale, PitchMin, PitchMax)) return;
	// 仅在极端情况下（路由器尚未创建）保留兼容入口；正常运行完全使用导入的现代音频资产。
	PlayVisualSound(EventId);
}

TArray<TPair<int32, int32>> AAscendPlayerController::CollectPendingEnemyDamageEvents() const
{
	TArray<TPair<int32, int32>> Events;
	if (!Combat) return Events;
	// Combat indices are authoritative. Localized combat log text cannot identify
	// which copy was hit when several enemies share the same display name.
	const int32 EventCount = Combat->EnemyDamageEvents.Num();
	const int32 StartIndex = LastProcessedEnemyDamageEventIndex <= EventCount
		? FMath::Max(0, LastProcessedEnemyDamageEventIndex) : 0;
	for (int32 EventIndex = StartIndex; EventIndex < EventCount; ++EventIndex)
	{
		const FEnemyDamageEvent& Event = Combat->EnemyDamageEvents[EventIndex];
		if (Combat->Enemies.IsValidIndex(Event.EnemyIndex))
			Events.Emplace(Event.EnemyIndex, Event.Damage);
	}
	return Events;
}

void AAscendPlayerController::PlayCardVisual(const FCardInstance& Card, int32 TargetEnemyIndex)
{
	if (!Combat) return;
	const FCardVisualData& Visual = Card.Data.Visual;
	const FString Animation = AscendCardVisual::ResolveAnimation(Card);
	const FString Sound = AscendCardVisual::ResolveSound(Card, Animation);
	LastPlayedVisualAnimation = Animation == TEXT("none") ? TEXT("") : Animation;
	if (Sound != TEXT("none"))
	{
		PlayAudioEvent(TEXT("card_play"), 0.38f, 0.98f, 1.02f);
		PlayAudioEvent(Sound, 1.f);
	}
	if (LastPlayedVisualAnimation.IsEmpty()) return;

	const FString AccentHex = AscendCardVisual::ResolveAccent(Card, Animation);
	const FLinearColor Accent = FColor::FromHex(AccentHex);
	FVector2D CanvasSize = GetViewportSize();
	if (AnimCanvas)
	{
		const FVector2D Measured = AnimCanvas->GetTickSpaceGeometry().GetLocalSize();
		if (Measured.X > 1.f && Measured.Y > 1.f) CanvasSize = Measured;
	}
	const FVector2D PlayerPos = FVector2D(CanvasSize.X * 0.5f, CanvasSize.Y * 0.64f);
	float Duration = FMath::Max(0.12f, Visual.Duration);
	if (Animation == TEXT("greatsword")) Duration = FMath::Max(Duration, 0.82f);
	else if (Animation == TEXT("myriad_swords")) Duration = FMath::Max(Duration, 0.88f);
	else if (Animation == TEXT("sword_wave")) Duration = FMath::Max(Duration, 0.58f);
	else if (Animation == TEXT("thunder")) Duration = FMath::Max(Duration, 0.62f);
	else if (Animation == TEXT("flame_burst") || Animation == TEXT("poison")) Duration = FMath::Max(Duration, 0.58f);
	else if (Animation == TEXT("ward") || Animation == TEXT("spirit_flow")
		|| Animation == TEXT("power_aura") || Animation == TEXT("talisman")
		|| Animation == TEXT("seal") || Animation == TEXT("curse_burst"))
		Duration = FMath::Max(Duration, 0.56f);

	const TArray<TPair<int32, int32>> DamageEvents = CollectPendingEnemyDamageEvents();
	TMap<int32, int32> DamageTotals;
	TArray<int32> Targets;
	for (const TPair<int32, int32>& Event : DamageEvents)
	{
		DamageTotals.FindOrAdd(Event.Key) += Event.Value;
		Targets.AddUnique(Event.Key);
	}
	if (Targets.Num() == 0 && AscendCardVisual::DealsDamage(Card))
	{
		if (AscendCardVisual::TargetsAllEnemies(Card))
		{
			for (int32 Index = 0; Index < Combat->Enemies.Num(); ++Index)
				if (Combat->Enemies[Index].State.IsAlive()) Targets.Add(Index);
		}
		else if (Combat->Enemies.IsValidIndex(TargetEnemyIndex))
		{
			Targets.Add(TargetEnemyIndex);
		}
	}
	if (Targets.Num() == 0 && AscendCardVisual::TargetsEnemy(Card)
		&& Combat->Enemies.IsValidIndex(TargetEnemyIndex))
	{
		Targets.Add(TargetEnemyIndex);
	}

	auto QueueImpact = [this](const FVector2D& Center, FLinearColor Color, float Delay, float ImpactDuration)
	{
		if (!GetWorld()) return;
		if (Delay <= KINDA_SMALL_NUMBER)
		{
			SpawnImpactBurst(Center, Color, ImpactDuration);
			return;
		}
		FTimerHandle Handle;
		GetWorld()->GetTimerManager().SetTimer(Handle,
			FTimerDelegate::CreateWeakLambda(this, [this, Center, Color, ImpactDuration]()
			{
				SpawnImpactBurst(Center, Color, ImpactDuration);
			}), Delay, false);
		AnimTimerHandles.Add(Handle);
	};

	auto QueueShake = [this](float Intensity, float ShakeDuration, float Delay)
	{
		if (!GetWorld()) return;
		if (Delay <= KINDA_SMALL_NUMBER)
		{
			AnimateScreenShake(Intensity, ShakeDuration);
			return;
		}
		FTimerHandle Handle;
		GetWorld()->GetTimerManager().SetTimer(Handle,
			FTimerDelegate::CreateWeakLambda(this, [this, Intensity, ShakeDuration]()
			{
				AnimateScreenShake(Intensity, ShakeDuration);
			}), Delay, false);
		AnimTimerHandles.Add(Handle);
	};

	if (Animation == TEXT("greatsword"))
	{
		for (int32 TargetOrder = 0; TargetOrder < Targets.Num(); ++TargetOrder)
		{
			const int32 EnemyIndex = Targets[TargetOrder];
			const FVector2D Pos = GetEnemyScreenPos(EnemyIndex);
			const float Delay = TargetOrder * 0.035f;
			SpawnStrikeEffect(Pos, ECombatStrikeStyle::Greatsword, Accent, Duration,
				TargetOrder % 2 == 0 ? -5.f : 5.f, 1.28f, TargetOrder, Delay);
			QueueImpact(Pos + FVector2D(0.f, 30.f), FLinearColor(1.f, 0.82f, 0.32f), Delay + Duration * 0.46f, 0.30f);
			if (const int32* Damage = DamageTotals.Find(EnemyIndex))
			{
				QueueFloatingText(FString::Printf(TEXT("-%d"), *Damage),
					FLinearColor(1.f, 0.28f, 0.08f), Pos + FVector2D(-28.f, -42.f),
					Delay + Duration * 0.47f, 1.16f, 1.62f, TargetOrder % 2 == 0 ? -14.f : 14.f);
			}
		}
		QueueShake(FMath::Max(11.f, Visual.Intensity), 0.34f, Duration * 0.45f);
		if (GetWorld())
		{
			FTimerHandle FlashHandle;
			GetWorld()->GetTimerManager().SetTimer(FlashHandle,
				FTimerDelegate::CreateWeakLambda(this, [this, Accent]()
				{
					AnimateColorFlash(Accent, 0.12f);
				}), Duration * 0.45f, false);
			AnimTimerHandles.Add(FlashHandle);
		}
	}
	else if (Animation == TEXT("myriad_swords"))
	{
		for (int32 TargetOrder = 0; TargetOrder < Targets.Num(); ++TargetOrder)
		{
			const int32 EnemyIndex = Targets[TargetOrder];
			const FVector2D Pos = GetEnemyScreenPos(EnemyIndex);
			const float TargetDelay = TargetOrder * 0.045f;
			SpawnStrikeEffect(Pos, ECombatStrikeStyle::MyriadSwords, Accent, Duration,
				0.f, 1.05f, TargetOrder + Card.RepeatCount, TargetDelay);
			const int32 TotalDamage = DamageTotals.FindRef(EnemyIndex);
			const int32 HitCount = FMath::Clamp(FMath::Min(TotalDamage,
				5 + FMath::Min(3, Card.RepeatCount)), TotalDamage > 0 ? 1 : 0, 8);
			if (HitCount > 0)
			{
				const int32 BaseHit = TotalDamage / HitCount;
				const int32 Remainder = TotalDamage % HitCount;
				for (int32 Hit = 0; Hit < HitCount; ++Hit)
				{
					const int32 HitDamage = BaseHit + (Hit < Remainder ? 1 : 0);
					const float HitDelay = TargetDelay + 0.13f + Hit * 0.078f;
					const float XOffset = (Hit - (HitCount - 1) * 0.5f) * 17.f;
					const FLinearColor NumberColor = Hit % 2 == 0
						? FLinearColor(1.f, 0.38f, 0.12f) : FLinearColor(1.f, 0.78f, 0.26f);
					QueueFloatingText(FString::Printf(TEXT("-%d"), HitDamage), NumberColor,
						Pos + FVector2D(XOffset - 15.f, -24.f - (Hit % 2) * 15.f),
						HitDelay, 0.78f, 0.88f + Hit * 0.035f, (Hit % 2 == 0 ? -1.f : 1.f) * (8.f + Hit));
					QueueImpact(Pos + FVector2D(XOffset * 0.4f, (Hit % 3 - 1) * 13.f), Accent,
						HitDelay - 0.025f, 0.14f);
				}
			}
		}
		QueueShake(3.2f, 0.13f, 0.17f);
		QueueShake(3.8f, 0.14f, 0.38f);
		QueueShake(FMath::Max(6.2f, Visual.Intensity), 0.20f, 0.56f);
	}
	else if (Animation == TEXT("sword_wave"))
	{
		FVector2D WaveCenter = Targets.Num() > 0 ? FVector2D::ZeroVector : GetEnemyScreenPos(TargetEnemyIndex);
		for (int32 EnemyIndex : Targets) WaveCenter += GetEnemyScreenPos(EnemyIndex);
		if (Targets.Num() > 0) WaveCenter /= Targets.Num();
		SpawnStrikeEffect(WaveCenter, ECombatStrikeStyle::SwordWave, Accent, Duration,
			-4.f, 1.08f, Card.UID % 7);
		for (int32 TargetOrder = 0; TargetOrder < Targets.Num(); ++TargetOrder)
		{
			const int32 EnemyIndex = Targets[TargetOrder];
			const FVector2D Pos = GetEnemyScreenPos(EnemyIndex);
			if (const int32* Damage = DamageTotals.Find(EnemyIndex))
				QueueFloatingText(FString::Printf(TEXT("-%d"), *Damage), FLinearColor(1.f, 0.32f, 0.10f),
					Pos + FVector2D(-20.f, -28.f), 0.24f + TargetOrder * 0.035f, 0.95f, 1.18f,
					TargetOrder % 2 == 0 ? -10.f : 10.f);
			QueueImpact(Pos, Accent, 0.22f + TargetOrder * 0.035f, 0.20f);
		}
		QueueShake(FMath::Max(6.5f, Visual.Intensity), 0.22f, 0.23f);
	}
	else if (Animation == TEXT("slash"))
	{
		const int32 SlashCount = FMath::Clamp(Visual.Count, 1, 4);
		for (int32 TargetOrder = 0; TargetOrder < Targets.Num(); ++TargetOrder)
		{
			const int32 EnemyIndex = Targets[TargetOrder];
			const FVector2D Pos = GetEnemyScreenPos(EnemyIndex);
			for (int32 SlashIndex = 0; SlashIndex < SlashCount; ++SlashIndex)
			{
				const float Delay = TargetOrder * 0.035f + SlashIndex * 0.075f;
				const float Rotation = -12.f + SlashIndex * 17.f;
				SpawnStrikeEffect(Pos + FVector2D((SlashIndex - (SlashCount - 1) * 0.5f) * 12.f, 0.f),
					ECombatStrikeStyle::ArcSlash, Accent, FMath::Max(0.32f, Duration), Rotation,
					1.f + 0.05f * SlashIndex, SlashIndex + TargetOrder * 4, Delay);
			}
			if (const int32* Damage = DamageTotals.Find(EnemyIndex))
				QueueFloatingText(FString::Printf(TEXT("-%d"), *Damage), FLinearColor(1.f, 0.30f, 0.10f),
					Pos + FVector2D(-20.f, -25.f), 0.13f + (SlashCount - 1) * 0.055f,
					0.95f, SlashCount > 1 ? 1.18f : 1.f, TargetOrder % 2 == 0 ? -9.f : 9.f);
			QueueImpact(Pos, Accent, 0.12f + (SlashCount - 1) * 0.055f, 0.19f);
		}
		QueueShake(FMath::Max(2.8f, Visual.Intensity), 0.16f, 0.12f);
	}
	else if (Animation == TEXT("fireball"))
	{
		const FVector2D EnemyPos = GetEnemyScreenPos(TargetEnemyIndex);
		SpawnProjectileEffect(PlayerPos, EnemyPos, Accent, Duration);
		for (int32 EnemyIndex : Targets)
		{
			if (const int32* Damage = DamageTotals.Find(EnemyIndex))
				QueueFloatingText(FString::Printf(TEXT("-%d"), *Damage), FLinearColor(1.f, 0.28f, 0.06f),
					GetEnemyScreenPos(EnemyIndex) + FVector2D(-20.f, -28.f), Duration * 0.90f,
					0.95f, 1.16f, 8.f);
		}
		QueueShake(FMath::Max(4.f, Visual.Intensity), 0.20f, Duration * 0.88f);
	}
	else if (Animation == TEXT("impact"))
	{
		for (int32 EnemyIndex : Targets)
		{
			const FVector2D Pos = GetEnemyScreenPos(EnemyIndex);
			SpawnImpactBurst(Pos, Accent, Duration);
			if (const int32* Damage = DamageTotals.Find(EnemyIndex))
				QueueFloatingText(FString::Printf(TEXT("-%d"), *Damage), FLinearColor(1.f, 0.30f, 0.10f),
					Pos + FVector2D(-20.f, -28.f), 0.08f, 0.90f, 1.05f, 6.f);
		}
		QueueShake(FMath::Max(2.8f, Visual.Intensity), 0.15f, 0.05f);
	}
	else if (Animation == TEXT("thunder") || Animation == TEXT("flame_burst")
		|| Animation == TEXT("poison"))
	{
		const ECombatStrikeStyle Style = Animation == TEXT("thunder") ? ECombatStrikeStyle::Thunder
			: (Animation == TEXT("flame_burst") ? ECombatStrikeStyle::FlameBurst : ECombatStrikeStyle::Poison);
		const float ImpactDelay = Animation == TEXT("thunder") ? 0.19f
			: (Animation == TEXT("flame_burst") ? 0.22f : 0.18f);
		for (int32 TargetOrder = 0; TargetOrder < Targets.Num(); ++TargetOrder)
		{
			const int32 EnemyIndex = Targets[TargetOrder];
			const FVector2D Pos = GetEnemyScreenPos(EnemyIndex);
			const float Delay = TargetOrder * 0.045f;
			SpawnStrikeEffect(Pos, Style, Accent, Duration, 0.f,
				Animation == TEXT("thunder") ? 1.18f : 1.04f, Card.UID + TargetOrder, Delay);
			if (const int32* Damage = DamageTotals.Find(EnemyIndex))
			{
				const FLinearColor NumberColor = Animation == TEXT("poison")
					? FLinearColor(0.72f, 1.f, 0.28f) : FLinearColor(1.f, 0.32f, 0.08f);
				QueueFloatingText(FString::Printf(TEXT("-%d"), *Damage), NumberColor,
					Pos + FVector2D(-20.f, -30.f), Delay + ImpactDelay, 0.98f,
					Animation == TEXT("thunder") ? 1.32f : 1.14f, TargetOrder % 2 == 0 ? -9.f : 9.f);
			}
			QueueImpact(Pos, Accent, Delay + ImpactDelay, 0.20f);
		}
		QueueShake(Animation == TEXT("thunder") ? FMath::Max(7.8f, Visual.Intensity)
			: FMath::Max(4.2f, Visual.Intensity), Animation == TEXT("thunder") ? 0.24f : 0.18f,
			ImpactDelay);
	}
	else if (Animation == TEXT("ward"))
	{
		SpawnStrikeEffect(PlayerPos, ECombatStrikeStyle::Ward, Accent, Duration,
			0.f, 1.06f, Card.UID);
		QueueFloatingText(TEXT("护体"), Accent, PlayerPos + FVector2D(-36.f, -72.f),
			0.16f, 0.92f, 1.08f, 0.f);
		AnimateColorFlash(Accent, 0.10f);
	}
	else if (Animation == TEXT("spirit_flow"))
	{
		SpawnStrikeEffect(PlayerPos, ECombatStrikeStyle::SpiritFlow, Accent, Duration,
			0.f, 1.f, Card.UID);
		QueueFloatingText(TEXT("灵气流转"), Accent, PlayerPos + FVector2D(-62.f, -82.f),
			0.18f, 1.0f, 1.0f, 4.f);
	}
	else if (Animation == TEXT("power_aura"))
	{
		SpawnStrikeEffect(PlayerPos, ECombatStrikeStyle::PowerAura, Accent, Duration,
			0.f, 1.08f, Card.UID);
		QueueFloatingText(Card.Data.Type == TEXT("gongfa") ? TEXT("功法运转") : TEXT("剑势凝聚"),
			Accent, PlayerPos + FVector2D(-62.f, -78.f), 0.19f, 1.04f, 1.10f, 0.f);
		QueueShake(1.8f, 0.12f, 0.18f);
	}
	else if (Animation == TEXT("talisman"))
	{
		SpawnStrikeEffect(PlayerPos, ECombatStrikeStyle::Talisman, Accent, Duration,
			-4.f, 1.f, Card.UID);
		QueueFloatingText(TEXT("符箓生效"), Accent, PlayerPos + FVector2D(-62.f, -80.f),
			0.20f, 0.98f, 1.04f, 0.f);
	}
	else if (Animation == TEXT("seal"))
	{
		const bool bEnemySeal = AscendCardVisual::TargetsEnemy(Card) && Targets.Num() > 0;
		const FVector2D SealPos = bEnemySeal ? GetEnemyScreenPos(Targets[0]) : PlayerPos;
		SpawnStrikeEffect(SealPos, ECombatStrikeStyle::Seal, Accent, Duration,
			0.f, 1.f, Card.UID);
		QueueFloatingText(bEnemySeal ? TEXT("印记" ) : TEXT("状态强化"), Accent,
			SealPos + FVector2D(-42.f, -72.f), 0.18f, 0.94f, 1.02f, 0.f);
	}
	else if (Animation == TEXT("curse_burst"))
	{
		SpawnStrikeEffect(PlayerPos, ECombatStrikeStyle::CurseBurst, Accent, Duration,
			0.f, 1.08f, Card.UID);
		QueueFloatingText(TEXT("反噬"), FLinearColor(1.f, 0.28f, 0.42f),
			PlayerPos + FVector2D(-34.f, -68.f), 0.16f, 0.96f, 1.18f, -5.f);
		QueueShake(FMath::Max(4.6f, Visual.Intensity), 0.20f, 0.17f);
		AnimateColorFlash(Accent, 0.12f);
	}
	else if (Animation == TEXT("block"))
	{
		AnimateColorFlash(Accent, Duration * 0.8f);
		SpawnFloatingText(TEXT("罡气"), Accent, PlayerPos.X - 36.f, PlayerPos.Y - 30.f, Duration);
	}
	else if (Animation == TEXT("heal"))
	{
		SpawnHealBurst(PlayerPos, Accent, Duration);
		SpawnFloatingText(TEXT("回春"), Accent, PlayerPos.X - 36.f, PlayerPos.Y - 30.f, Duration + 0.25f);
	}
	else if (Animation == TEXT("draw"))
	{
		SpawnFloatingText(TEXT("抽牌"), Accent, CanvasSize.X - 185.f, CanvasSize.Y - 255.f, Duration);
	}

}

void AAscendPlayerController::AnimateScreenShake(float Intensity, float Duration)
{
	if (!InfiniteNarrativeSettings.bEnableScreenShake) return;
	if (!ScreenHost) return;
	const float AddedTrauma = FMath::Clamp(Intensity / 12.f, 0.06f, 0.82f);
	ScreenShakeTrauma = FMath::Clamp(ScreenShakeTrauma + AddedTrauma, 0.f, 1.f);
	ScreenShakeDecayRate = FMath::Max(0.9f, ScreenShakeTrauma / FMath::Max(0.08f, Duration));
}

void AAscendPlayerController::AnimateColorFlash(FLinearColor Color, float Duration)
{
	if (!AnimCanvas || !GetWorld()) return;

	UBorder* Flash = NewObject<UBorder>(AnimCanvas);
	const float PeakOpacity = InfiniteNarrativeSettings.bReduceFlashing ? 0.10f : 0.28f;
	const float SafeDuration = InfiniteNarrativeSettings.bReduceFlashing
		? FMath::Min(Duration, 0.14f) : FMath::Min(Duration, 0.28f);
	Flash->SetBrushColor(FLinearColor(Color.R, Color.G, Color.B, PeakOpacity));

	FVector2D CanvasSize = AnimCanvas->GetTickSpaceGeometry().GetLocalSize();
	UCanvasPanelSlot* Slot = AnimCanvas->AddChildToCanvas(Flash);
	Slot->SetPosition(FVector2D::ZeroVector);
	Slot->SetSize(CanvasSize);
	Flash->SetRenderOpacity(PeakOpacity);
	ActiveAnimations.Add(Flash);

	const float StartTime = GetWorld()->GetTimeSeconds();
	TWeakObjectPtr<UBorder> WeakFlash = Flash;
	FTimerHandle Handle;
	GetWorld()->GetTimerManager().SetTimer(Handle,
		FTimerDelegate::CreateWeakLambda(this, [this, WeakFlash, Handle, StartTime, SafeDuration, PeakOpacity]() mutable
		{
			if (!WeakFlash.IsValid())
			{
				if (UWorld* W = GetWorld()) W->GetTimerManager().ClearTimer(Handle);
				return;
			}
			const float Elapsed = GetWorld()->GetTimeSeconds() - StartTime;
			if (Elapsed >= SafeDuration)
			{
				WeakFlash->RemoveFromParent();
				ActiveAnimations.Remove(WeakFlash.Get());
				if (UWorld* W = GetWorld()) W->GetTimerManager().ClearTimer(Handle);
				return;
			}
			WeakFlash->SetRenderOpacity(PeakOpacity * (1.f - Elapsed / SafeDuration));
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
	bool bEnemyKilled = false;
	const TArray<TPair<int32, int32>> EnemyDamages = CollectPendingEnemyDamageEvents();

	int32 StartIdx = LastProcessedLogIndex;
	LastProcessedLogIndex = LogN;
	LastProcessedEnemyDamageEventIndex = Combat->EnemyDamageEvents.Num();
	for (int32 i = StartIdx; i < LogN; ++i)
	{
		const FString& L = CombatLogLines[i];

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

		if (L.Contains(TEXT("被击杀")))
		{
			bEnemyKilled = true;
		}
	}

	// ---- 生成动画 ----
	const bool bHasCardVisual = ActionType == TEXT("card") && !LastPlayedVisualAnimation.IsEmpty();
	if (ActionType == TEXT("draw")) PlayAudioEvent(TEXT("draw"), 0.68f);
	if (ActionType == TEXT("enemy_turn")) PlayAudioEvent(TEXT("enemy_turn"), 0.52f);
	if (bReshuffled) PlayAudioEvent(TEXT("reshuffle"), 0.78f);
	if (bEnemyKilled) PlayAudioEvent(TEXT("enemy_death"), 0.92f);
	if (!bHasCardVisual)
	{
		if (bPlayerHealed) PlayAudioEvent(TEXT("heal_chime"), 0.72f);
		if (bPlayerBlocked) PlayAudioEvent(TEXT("ward_raise"), 0.72f);
		if (bPlayerDamaged) PlayAudioEvent(PlayerBlockValue > 0 ? TEXT("block") : TEXT("player_hit"), 0.82f);
		if (EnemyDamages.Num() > 0) PlayAudioEvent(TEXT("impact_hit"), 0.62f);
	}

	const FGeometry& AG = AnimCanvas->GetCachedGeometry();
	FVector2D PlayerDmgPos = AG.AbsoluteToLocal(AG.GetAbsolutePosition() + FVector2D(560.f, 280.f));
	FVector2D PlayerStatusPos = AG.AbsoluteToLocal(AG.GetAbsolutePosition() + FVector2D(560.f, 320.f));

	if (bPlayerHealed)
	{
		SpawnFloatingText(TEXT("+回春"), FLinearColor(0.2f, 0.8f, 0.3f), PlayerStatusPos.X, PlayerStatusPos.Y);
	}

	int32 LargestEnemyHit = 0;
	float LargestEnemyHitRatio = 0.f;
	for (const auto& Pair : EnemyDamages)
	{
		LargestEnemyHit = FMath::Max(LargestEnemyHit, Pair.Value);
		if (Combat->Enemies.IsValidIndex(Pair.Key) && Combat->Enemies[Pair.Key].State.MaxHP > 0)
			LargestEnemyHitRatio = FMath::Max(LargestEnemyHitRatio,
				static_cast<float>(Pair.Value) / Combat->Enemies[Pair.Key].State.MaxHP);
		if (!bHasCardVisual)
		{
			const FVector2D EP = GetEnemyScreenPos(Pair.Key);
			SpawnFloatingText(FString::Printf(TEXT("-%d"), Pair.Value),
				FLinearColor(0.95f, 0.25f, 0.15f), EP.X - 20.f, EP.Y - 20.f);
			SpawnStrikeEffect(EP, ECombatStrikeStyle::ArcSlash,
				FLinearColor(0.88f, 0.96f, 1.f, 0.92f), 0.34f, -8.f, 0.88f, Pair.Key);
		}
	}
	if (LargestEnemyHit > 0 && !bHasCardVisual)
	{
		AnimateScreenShake(LargestEnemyHitRatio >= 0.20f ? 7.f : (LargestEnemyHitRatio >= 0.10f ? 4.5f : 2.8f),
			LargestEnemyHitRatio >= 0.20f ? 0.22f : 0.15f);
	}

	if (bPlayerDamaged)
	{
		if (PlayerDamageValue > 0)
		{
			int32 ActualHP = FMath::Max(0, PlayerDamageValue - PlayerBlockValue);
			if (PlayerBlockValue > 0)
			{
				AnimateScreenShake(ActualHP > Combat->Player.MaxHP / 5 ? 9.f : 6.f, 0.24f);
				AnimateColorFlash(FLinearColor(0.8f, 0.1f, 0.05f), 0.24f);
				SpawnFloatingText(FString::Printf(TEXT("-%d (抵消%d)"), ActualHP, PlayerBlockValue),
					FLinearColor(0.85f, 0.2f, 0.1f), PlayerDmgPos.X, PlayerDmgPos.Y);
			}
			else
			{
				AnimateScreenShake(PlayerDamageValue > Combat->Player.MaxHP / 5 ? 9.f : 6.f, 0.24f);
				AnimateColorFlash(FLinearColor(0.8f, 0.1f, 0.05f), 0.24f);
				SpawnFloatingText(FString::Printf(TEXT("-%d"), PlayerDamageValue),
					FLinearColor(0.85f, 0.2f, 0.1f), PlayerDmgPos.X, PlayerDmgPos.Y);
			}
		}
		else
		{
			AnimateScreenShake(6.f, 0.20f);
			AnimateColorFlash(FLinearColor(0.8f, 0.1f, 0.05f), 0.20f);
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
		// Keep the card at its authored hand size throughout the entrance.  A
		// scale-up from 0.2 would magnify the already-laid-out card and make the
		// frame/text visibly soft during every draw.
		Btn->SetRenderScale(FVector2D(1.f, 1.f));
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
					Btn->SetRenderTranslation(FVector2D::ZeroVector);
					Btn->SetRenderScale(FVector2D(1.f, 1.f));
					Btn->SetRenderOpacity(1.f);
					FTimerHandle CompletedHandle = *H;
					W->GetTimerManager().ClearTimer(CompletedHandle);
					return;
				}

				const float T = Elapsed / Dur;
				const float Ease = 1.f - FMath::Pow(1.f - T, 3.f);

				FVector2D Tr = PileAnchor * (1.f - Ease);
				Tr.Y -= FMath::Sin(T * PI) * 40.f;
				if (PC->HoveredCardIndex == i)
				{
					Btn->SetRenderTranslation(FVector2D::ZeroVector);
					Btn->SetRenderScale(FVector2D(1.f, 1.f));
					Btn->SetRenderOpacity(1.f);
				}
				else
				{
					Btn->SetRenderTranslation(Tr);
					Btn->SetRenderScale(FVector2D(1.f, 1.f));
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
	PlayAudioEvent(TEXT("reward"), 0.72f);
	for (const FString& Note : Notes)
	{
		if (Note.Contains(TEXT("灵石"))) PlayAudioEvent(TEXT("gold"), 0.48f);
		if (Note.Contains(TEXT("突破"))) PlayAudioEvent(TEXT("breakthrough"), 0.70f);
	}

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
