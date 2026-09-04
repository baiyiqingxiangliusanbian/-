#include "AscendPlayerController.h"

#include "UI/AscendUIStyle.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Kismet/KismetSystemLibrary.h"

void AAscendPlayerController::OnEscapeKey()
{
	if (SettingsOverlayLayer)
	{
		ReturnFromSettings();
		return;
	}
	TogglePauseMenu();
}

void AAscendPlayerController::TogglePauseMenu()
{
	if (bPauseMenuOpen) HidePauseMenu();
	else ShowPauseMenu();
}

void AAscendPlayerController::ShowPauseMenu()
{
	if (!RootOverlay || bPauseMenuOpen) return;
	ResetSettingsWidgetRefs();
	bLeaveToQuitGame = false;
	bLeaveSaveFailed = false;
	bPauseMenuOpen = true;
	if (MenuButtonLayer) MenuButtonLayer->SetVisibility(ESlateVisibility::Collapsed);

	PauseMenuOverlay = NewObject<UBorder>(RootOverlay);
	PauseMenuOverlay->SetBrushColor(FLinearColor(0.015f, 0.012f, 0.02f, 0.90f));
	PauseMenuOverlay->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Center);
	PauseMenuOverlay->SetVerticalAlignment(EVerticalAlignment::VAlign_Center);
	UOverlaySlot* ModalSlot = RootOverlay->AddChildToOverlay(PauseMenuOverlay);
	ModalSlot->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Fill);
	ModalSlot->SetVerticalAlignment(EVerticalAlignment::VAlign_Fill);

	USizeBox* PanelSize = NewObject<USizeBox>(PauseMenuOverlay);
	PanelSize->SetWidthOverride(760.f);
	UBorder* Panel = NewObject<UBorder>(PanelSize);
	Panel->SetBrushColor(FLinearColor(0.10f, 0.085f, 0.075f, 0.99f));
	Panel->SetPadding(FMargin(34.f, 24.f));
	UVerticalBox* Box = NewObject<UVerticalBox>(Panel);

	UTextBlock* Title = FAscendUIStyle::MakeText(Box, TEXT("暂 停 菜 单"), 30,
		FAscendUIStyle::GoldYellow());
	Title->SetJustification(ETextJustify::Center);
	Box->AddChildToVerticalBox(Title)->SetPadding(FMargin(4.f, 2.f, 4.f, 12.f));

	auto AddMenuButton = [this, Box](const FString& Label, const FString& Tag)
	{
		UButton* Button = MakeLinkedButton(Box, Label, Tag, 0, 18);
		Box->AddChildToVerticalBox(Button)->SetPadding(FMargin(80.f, 4.f));
	};
	AddMenuButton(TEXT("继续游戏"), TEXT("menu_resume"));
	AddMenuButton(TEXT("设置"), TEXT("menu_full_settings"));
	AddMenuButton(TEXT("保存并返回主菜单"), TEXT("menu_title"));
	AddMenuButton(TEXT("保存并退出游戏"), TEXT("menu_quit"));
	PauseMenuProxies.Append(PendingScreenProxies);
	PendingScreenProxies.Reset();

	Panel->SetContent(Box);
	PanelSize->SetContent(Panel);
	PauseMenuOverlay->SetContent(PanelSize);
}

void AAscendPlayerController::RequestLeaveConfirmation(bool bQuitGame)
{
	if (!RootOverlay || !bPauseMenuOpen) return;
	bLeaveToQuitGame = bQuitGame;
	bLeaveSaveFailed = false;
	ShowLeaveConfirmation();
}

void AAscendPlayerController::ShowLeaveConfirmation()
{
	if (!RootOverlay) return;
	if (PauseMenuOverlay) PauseMenuOverlay->RemoveFromParent();
	PauseMenuOverlay = nullptr;
	PauseMenuProxies.Reset();

	PauseMenuOverlay = NewObject<UBorder>(RootOverlay);
	PauseMenuOverlay->SetBrushColor(FLinearColor(0.015f, 0.012f, 0.02f, 0.90f));
	UOverlaySlot* ModalSlot = RootOverlay->AddChildToOverlay(PauseMenuOverlay);
	ModalSlot->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Fill);
	ModalSlot->SetVerticalAlignment(EVerticalAlignment::VAlign_Fill);

	USizeBox* PanelSize = NewObject<USizeBox>(PauseMenuOverlay);
	PanelSize->SetWidthOverride(760.f);
	UBorder* Panel = NewObject<UBorder>(PanelSize);
	Panel->SetBrushColor(FLinearColor(0.10f, 0.085f, 0.075f, 0.99f));
	Panel->SetPadding(FMargin(34.f, 28.f));
	UVerticalBox* Box = NewObject<UVerticalBox>(Panel);

	UTextBlock* Title = FAscendUIStyle::MakeText(Box,
		bLeaveToQuitGame ? TEXT("保存并退出游戏") : TEXT("保存并返回主菜单"), 28,
		FAscendUIStyle::GoldYellow());
	Title->SetJustification(ETextJustify::Center);
	Box->AddChildToVerticalBox(Title)->SetPadding(FMargin(4.f, 2.f, 4.f, 16.f));

	const FString Prompt = bLeaveSaveFailed
		? TEXT("保存失败，仍停留在当前界面。请重试保存，或取消返回暂停菜单。")
		: bLeaveToQuitGame
		? TEXT("将保存当前进度并退出游戏。确认继续？")
		: TEXT("将保存当前进度并返回主菜单。确认继续？");
	UTextBlock* Message = FAscendUIStyle::MakeText(Box, Prompt, 18,
		bLeaveSaveFailed ? FAscendUIStyle::BloodRed() : FAscendUIStyle::PaperWhite());
	Message->SetAutoWrapText(true);
	Message->SetJustification(ETextJustify::Center);
	Box->AddChildToVerticalBox(Message)->SetPadding(FMargin(10.f, 4.f, 10.f, 18.f));

	auto AddButton = [this, Box](const FString& Label, const FString& Tag)
	{
		UButton* Button = MakeLinkedButton(Box, Label, Tag, 0, 18);
		Box->AddChildToVerticalBox(Button)->SetPadding(FMargin(80.f, 4.f));
	};
	AddButton(bLeaveSaveFailed ? TEXT("重试保存") : TEXT("确认保存并继续"), TEXT("menu_leave_confirm"));
	AddButton(TEXT("取消"), TEXT("menu_leave_cancel"));

	PauseMenuProxies.Append(PendingScreenProxies);
	PendingScreenProxies.Reset();
	Panel->SetContent(Box);
	PanelSize->SetContent(Panel);
	PauseMenuOverlay->SetContent(PanelSize);
}

void AAscendPlayerController::CancelLeaveConfirmation()
{
	bLeaveToQuitGame = false;
	bLeaveSaveFailed = false;
	HidePauseMenu();
	ShowPauseMenu();
}

void AAscendPlayerController::ConfirmLeaveConfirmation()
{
	if (bLeaveToQuitGame) SaveAndQuitGame();
	else SaveAndReturnToTitle();
}

void AAscendPlayerController::HidePauseMenu()
{
	bPauseMenuOpen = false;
	bLeaveToQuitGame = false;
	bLeaveSaveFailed = false;
	if (PauseMenuOverlay) PauseMenuOverlay->RemoveFromParent();
	PauseMenuOverlay = nullptr;
	ResetSettingsWidgetRefs();
	PauseMenuProxies.Reset();
	if (MenuButtonLayer) MenuButtonLayer->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
}

void AAscendPlayerController::SaveAndReturnToTitle()
{
	const bool bSaved = !Run || !Run->State.bRunActive || Run->SaveRun();
	if (!bSaved)
	{
		bLeaveSaveFailed = true;
		UE_LOG(LogTemp, Error, TEXT("[AscendUI] 保存并返回主菜单失败，保留当前界面"));
		ShowLeaveConfirmation();
		return;
	}
	InvalidateInfiniteNarrativeFlow();
	// Both ordinary RP generation and combat prefetch share this service. Cancel the
	// entire writer/MVU chain before changing screens; otherwise its late callback calls
	// ShowInfiniteNarrative() and silently replaces the title page again.
	if (InfiniteNarrativeService) InfiniteNarrativeService->CancelGeneration();
	bInfiniteNarrativeRequestInFlight = false;
	bInputLocked = false;
	bDiscardCombatNarrativePrefetch = true;
	bWaitingForCombatNarrativeAfterReward = false;
	bCombatNarrativePrefetchReady = false;
	bCombatNarrativePrefetchFailed = false;
	ResetInfiniteNarrativeStreamPreview();
	HidePauseMenu();
	ShowTitle();
	UE_LOG(LogTemp, Display, TEXT("[AscendUI] 保存并返回主菜单完成"));
}

void AAscendPlayerController::SaveAndQuitGame()
{
	const bool bSaved = !Run || !Run->State.bRunActive || Run->SaveRun();
	if (!bSaved)
	{
		bLeaveSaveFailed = true;
		UE_LOG(LogTemp, Error, TEXT("[AscendUI] 保存并退出游戏失败，保留当前界面"));
		ShowLeaveConfirmation();
		return;
	}
	InvalidateInfiniteNarrativeFlow();
	if (InfiniteNarrativeService) InfiniteNarrativeService->CancelGeneration();
	UKismetSystemLibrary::QuitGame(this, this, EQuitPreference::Quit, false);
}
