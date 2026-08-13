#include "AscendPlayerController.h"

#include "UI/AscendUIStyle.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CheckBox.h"
#include "Components/EditableTextBox.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Kismet/KismetSystemLibrary.h"

void AAscendPlayerController::OnEscapeKey()
{
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

	UTextBlock* Title = FAscendUIStyle::MakeText(Box, TEXT("行 囊 与 设 置"), 30,
		FAscendUIStyle::GoldYellow());
	Title->SetJustification(ETextJustify::Center);
	Box->AddChildToVerticalBox(Title)->SetPadding(FMargin(4.f, 2.f, 4.f, 12.f));

	auto AddField = [Box](const FString& Label, const FString& Value, bool bPassword) -> UEditableTextBox*
	{
		UTextBlock* FieldLabel = FAscendUIStyle::MakeText(Box, Label, 14, FAscendUIStyle::DimGray());
		Box->AddChildToVerticalBox(FieldLabel)->SetPadding(FMargin(4.f, 4.f, 4.f, 2.f));
		UEditableTextBox* Input = NewObject<UEditableTextBox>(Box);
		Input->SetText(FText::FromString(Value));
		Input->SetIsPassword(bPassword);
		Box->AddChildToVerticalBox(Input)->SetPadding(FMargin(4.f, 0.f, 4.f, 4.f));
		return Input;
	};

	SettingsEndpointInput = AddField(TEXT("LLM 接口地址"), InfiniteNarrativeSettings.Endpoint, false);
	SettingsModelInput = AddField(TEXT("模型"), InfiniteNarrativeSettings.Model, false);
	SettingsApiKeyInput = AddField(TEXT("API Key（仅保存在本机）"), InfiniteNarrativeSettings.ApiKey, true);

	UHorizontalBox* ToggleRow = NewObject<UHorizontalBox>(Box);
	SettingsShowInputCheckBox = NewObject<UCheckBox>(ToggleRow);
	SettingsShowInputCheckBox->SetIsChecked(InfiniteNarrativeSettings.bShowFreeformInput);
	ToggleRow->AddChildToHorizontalBox(SettingsShowInputCheckBox)->SetPadding(FMargin(4.f, 7.f));
	UTextBlock* ToggleText = FAscendUIStyle::MakeText(ToggleRow, TEXT("显示 RP 自由文本输入框"), 15,
		FAscendUIStyle::PaperWhite());
	ToggleRow->AddChildToHorizontalBox(ToggleText)->SetPadding(FMargin(4.f, 7.f));
	Box->AddChildToVerticalBox(ToggleRow);

	auto AddMenuButton = [this, Box](const FString& Label, const FString& Tag)
	{
		UButton* Button = MakeLinkedButton(Box, Label, Tag, 0, 18);
		Box->AddChildToVerticalBox(Button)->SetPadding(FMargin(80.f, 4.f));
	};
	AddMenuButton(TEXT("【保存设置并继续】"), TEXT("menu_save_settings"));
	AddMenuButton(TEXT("【显示 / 声音 / 游戏 / AI / RP 完整设置】"), TEXT("menu_full_settings"));
	AddMenuButton(TEXT("【继续游戏】"), TEXT("menu_resume"));
	AddMenuButton(TEXT("【保存并返回标题】"), TEXT("menu_title"));
	AddMenuButton(TEXT("【保存并退出游戏】"), TEXT("menu_quit"));
	PauseMenuProxies.Append(PendingScreenProxies);
	PendingScreenProxies.Reset();

	Panel->SetContent(Box);
	PanelSize->SetContent(Panel);
	PauseMenuOverlay->SetContent(PanelSize);
}

void AAscendPlayerController::HidePauseMenu()
{
	bPauseMenuOpen = false;
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
		UE_LOG(LogTemp, Error, TEXT("[AscendUI] 保存并返回标题：存档写入失败，但仍返回标题以免界面卡死"));
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
	UE_LOG(LogTemp, Display, TEXT("[AscendUI] 保存并返回标题完成 saved=%s"), bSaved ? TEXT("true") : TEXT("false"));
}

void AAscendPlayerController::SaveAndQuitGame()
{
	if (Run && Run->State.bRunActive) Run->SaveRun();
	UKismetSystemLibrary::QuitGame(this, this, EQuitPreference::Quit, false);
}
