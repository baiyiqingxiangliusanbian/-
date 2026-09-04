#include "AscendPlayerController.h"
#include "UI/ClickProxy.h"
#include "UI/AscendUIStyle.h"
#include "UI/AscendRootWidget.h"
#include "UI/AscendArt.h"
#include "UI/AscendCardLayout.h"
#include "Components/Button.h"
#include "Blueprint/UserWidget.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/Image.h"
#include "Components/ScaleBox.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/ScrollBox.h"
#include "Components/ProgressBar.h"
#include "Components/Spacer.h"
#include "Components/SizeBox.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"

using Style = FAscendUIStyle;

// -----------------------------------------------------------
// 小工具
// -----------------------------------------------------------

namespace
{
	void PlaceCardWidget(UCanvasPanel* Canvas, UWidget* Widget, float X, float Y, float W, float H, float Scale, int32 ZOrder)
	{
		if (UCanvasPanelSlot* Slot = Canvas->AddChildToCanvas(Widget))
		{
			Slot->SetPosition(FVector2D(X * Scale, Y * Scale));
			Slot->SetSize(FVector2D(W * Scale, H * Scale));
			Slot->SetZOrder(ZOrder);
		}
	}

	void Pad(UVerticalBox* Box, float Height)
	{
		USpacer* S = NewObject<USpacer>(Box);
		S->SetSize(FVector2D(1.f, Height));
		Box->AddChildToVerticalBox(S);
	}

	void AddToVBox(UVerticalBox* Box, UWidget* W, FMargin Padding = FMargin(6.f))
	{
		UVerticalBoxSlot* S = Box->AddChildToVerticalBox(W);
		S->SetPadding(Padding);
	}

	void AddToHBox(UHorizontalBox* Box, UWidget* W, FMargin Padding = FMargin(4.f))
	{
		UHorizontalBoxSlot* S = Box->AddChildToHorizontalBox(W);
		S->SetPadding(Padding);
		S->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
	}

	FString IntentText(const FEnemyIntent& I)
	{
		if (I.Action == TEXT("attack")) return FString::Printf(TEXT("攻击 %d"), I.Value);
		if (I.Action == TEXT("attack_multi")) return FString::Printf(TEXT("连击 %dx%d"), I.Value, I.Times);
		if (I.Action == TEXT("defend")) return FString::Printf(TEXT("防御 %d"), I.Value);
		if (I.Action == TEXT("buff")) return TEXT("强化");
		if (I.Action == TEXT("debuff")) return TEXT("削弱");
		return TEXT("?");
	}

	FString CultivationStatusText(const FCultivationState& Cultivation)
	{
		if (Cultivation.bFoundationEstablished || Cultivation.RealmIndex >= FCultivationSystem::FoundationRealm)
		{
			return FString::Printf(TEXT("当前境界：%s　修为：%d　道基已成"),
				*Cultivation.RealmName, Cultivation.CultivationPoints);
		}
		if (Cultivation.bAtBottleneck)
		{
			return FString::Printf(TEXT("当前境界：%s　修为：%d　瓶颈：击败首领后方可筑基"),
				*Cultivation.RealmName, Cultivation.CultivationPoints);
		}
		const int32 Remaining = FMath::Max(0, Cultivation.NextThreshold - Cultivation.CultivationPoints);
		return FString::Printf(TEXT("当前境界：%s　修为：%d　下一层阈值：%d（还差%d）"),
			*Cultivation.RealmName, Cultivation.CultivationPoints,
			Cultivation.NextThreshold, Remaining);
	}



	UWidget* MakeHPBar(UObject* Outer, int32 HP, int32 MaxHP, FLinearColor Color)
	{
		UProgressBar* Bar = NewObject<UProgressBar>(Outer);
		Bar->SetPercent(MaxHP > 0 ? (float)HP / MaxHP : 0.f);
		Bar->SetFillColorAndOpacity(Color);
		return Bar;
	}

	UWidget* MakeNarrativeReadingPanel(UObject* Outer, const FString& Text, int32 FontSize = 21)
	{
		USizeBox* Width = NewObject<USizeBox>(Outer);
		Width->SetWidthOverride(1040.f);
		Width->SetMinDesiredHeight(132.f);
		UBorder* Panel = NewObject<UBorder>(Width);
		if (UTexture2D* Texture = FAscendArt::GetTexture(Panel, TEXT("Art/ui/chat_bubble.png")))
		{
			FSlateBrush Brush;
			Brush.SetResourceObject(Texture);
			Brush.DrawAs = ESlateBrushDrawType::Box;
			Brush.Margin = FMargin(0.065f, 0.19f);
			Brush.ImageSize = FVector2D(1024.f, 409.f);
			Panel->SetBrush(Brush);
			Panel->SetBrushColor(FLinearColor::White);
		}
		else
		{
			Panel->SetBrushColor(FLinearColor(0.035f, 0.055f, 0.06f, 0.96f));
		}
		Panel->SetPadding(FMargin(70.f, 30.f, 70.f, 32.f));
		UTextBlock* Body = Style::MakeText(Panel, Text, FontSize, Style::PaperWhite());
		Body->SetAutoWrapText(true);
		Body->SetWrapTextAt(880.f);
		Body->SetJustification(ETextJustify::Left);
		Body->SetShadowOffset(FVector2D(1.f, 1.f));
		Body->SetShadowColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.65f));
		Panel->SetContent(Body);
		Width->SetContent(Panel);
		return Width;
	}
}

// -----------------------------------------------------------
// 标题
// -----------------------------------------------------------

FLinearColor RarityColor(const FString& Rarity)
{
	return FAscendUIStyle::RarityColor(Rarity);
}

void AAscendPlayerController::ShowTitle()
{
	if (Run) Run->RefreshPersistentAuthoredContent();
	UVerticalBox* Box = NewObject<UVerticalBox>(RootWidget);
	Pad(Box, 120);

	UTextBlock* Title = Style::MakeText(Box, TEXT("登 仙 路"), 64, Style::GoldYellow());
	Title->SetJustification(ETextJustify::Center);
	AddToVBox(Box, Title);

	UTextBlock* Sub = Style::MakeText(Box, TEXT("—— 修仙 · 肉鸽 · 打牌 ——"), 22, Style::DimGray());
	Sub->SetJustification(ETextJustify::Center);
	AddToVBox(Box, Sub);
	Pad(Box, 60);

	UHorizontalBox* BtnRow = NewObject<UHorizontalBox>(Box);
	BtnRow->AddChildToHorizontalBox(NewObject<USpacer>(Box))->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	AddToHBox(BtnRow, MakeLinkedButton(BtnRow, TEXT("【 新游戏 】"), TEXT("title_infinite"), 0, 26));
	if (URunManager::HasSaveFile())
	{
		AddToHBox(BtnRow, MakeLinkedButton(BtnRow, TEXT("【 继续修行 】"), TEXT("title_continue"), 0, 26));
	}
	BtnRow->AddChildToHorizontalBox(NewObject<USpacer>(Box))->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	AddToVBox(Box, BtnRow);

	UHorizontalBox* SettingsRow = NewObject<UHorizontalBox>(Box);
	SettingsRow->AddChildToHorizontalBox(NewObject<USpacer>(Box))->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	AddToHBox(SettingsRow, MakeLinkedButton(SettingsRow, TEXT("【 叙事设置 】"), TEXT("title_settings"), 0, 18));
	if (Run && (Run->GetPersistentAuthoredCards().Num() > 0 || Run->GetPersistentAuthoredRelics().Num() > 0))
	{
		const int32 AuthoredCount = Run->GetPersistentAuthoredCards().Num() + Run->GetPersistentAuthoredRelics().Num();
		AddToHBox(SettingsRow, MakeLinkedButton(SettingsRow,
			FString::Printf(TEXT("【 永久原创库 %d 】"), AuthoredCount), TEXT("title_authored_library"), 0, 18));
	}
	SettingsRow->AddChildToHorizontalBox(NewObject<USpacer>(Box))->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	AddToVBox(Box, SettingsRow, FMargin(0.f, 10.f));

	Pad(Box, 40);
	UTextBlock* Tip = Style::MakeText(Box, TEXT("一介散修，逆天改命。杀人夺宝，快意恩仇。"), 16, Style::DimGray());
	Tip->SetJustification(ETextJustify::Center);
	AddToVBox(Box, Tip);

	// 元进程：斩妖图鉴与已解锁品级
	if (Run)
	{
		Run->EnsureMetaLoaded();
		if (Run->Meta.HighestFloor > 0 || Run->Meta.DefeatedEnemyIds.Num() > 0)
		{
			Pad(Box, 24);
			static const TCHAR* TierNames[] = {TEXT("凡品"), TEXT("中品"), TEXT("上品"), TEXT("仙品")};
			const int32 Lv = FMath::Clamp(Run->Meta.UnlockLevel, 0, 3);
			UTextBlock* MetaT = Style::MakeText(Box,
				FString::Printf(TEXT("斩妖图鉴：%d/10    已解锁品级：%s    历史最高：第 %d 层"),
					Run->Meta.DefeatedEnemyIds.Num(), TierNames[Lv], Run->Meta.HighestFloor),
				14, Style::GoldYellow());
			MetaT->SetJustification(ETextJustify::Center);
			AddToVBox(Box, MetaT);
		}
	}

	SetScreen(Box, EGameScreen::Title);
}

void AAscendPlayerController::ShowAuthoredLibrary()
{
	if (!Run) { ShowTitle(); return; }
	Run->RefreshPersistentAuthoredContent();
	UScrollBox* Scroll = NewObject<UScrollBox>(RootWidget);
	UVerticalBox* Box = NewObject<UVerticalBox>(Scroll);
	Scroll->AddChild(Box);
	Pad(Box, 35.f);
	UTextBlock* Title = Style::MakeText(Box, TEXT("永 久 原 创 库"), 38, Style::GoldYellow());
	Title->SetJustification(ETextJustify::Center);
	AddToVBox(Box, Title, FMargin(30.f, 8.f));
	UTextBlock* Hint = Style::MakeText(Box,
		TEXT("LLM 成功登记的原创卡牌、法宝与伙伴会跨存档保留。删除只移出永久库；已有存档内已经获得的内容仍随该存档保存。"),
		16, Style::DimGray());
	Hint->SetJustification(ETextJustify::Center);
	Hint->SetAutoWrapText(true);
	AddToVBox(Box, Hint, FMargin(100.f, 0.f, 100.f, 18.f));

	auto AddDeleteRow = [this, Box](UWidget* Preview, const FString& DeleteTag, int32 Index)
	{
		UHorizontalBox* Row = NewObject<UHorizontalBox>(Box);
		Row->AddChildToHorizontalBox(NewObject<USpacer>(Row))->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		AddToHBox(Row, Preview, FMargin(10.f, 6.f));
		AddToHBox(Row, MakeLinkedButton(Row, TEXT("【 删除永久记录 】"), DeleteTag, Index, 16), FMargin(18.f, 6.f));
		Row->AddChildToHorizontalBox(NewObject<USpacer>(Row))->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		AddToVBox(Box, Row, FMargin(12.f, 4.f));
	};

	if (Run->GetPersistentAuthoredCards().Num() > 0)
	{
		AddToVBox(Box, Style::MakeText(Box, TEXT("原创卡牌"), 25, Style::PaperWhite()), FMargin(80.f, 14.f, 80.f, 4.f));
		for (int32 Index = 0; Index < Run->GetPersistentAuthoredCards().Num(); ++Index)
		{
			const FCardData& Card = Run->GetPersistentAuthoredCards()[Index];
			AddDeleteRow(MakeCardContentFromData(Box, Card, false, 0.78f), TEXT("authored_delete_card"), Index);
		}
	}
	if (Run->GetPersistentAuthoredRelics().Num() > 0)
	{
		AddToVBox(Box, Style::MakeText(Box, TEXT("原创法宝与伙伴"), 25, Style::PaperWhite()), FMargin(80.f, 18.f, 80.f, 4.f));
		for (int32 Index = 0; Index < Run->GetPersistentAuthoredRelics().Num(); ++Index)
		{
			const FRelicData& Relic = Run->GetPersistentAuthoredRelics()[Index];
			AddDeleteRow(MakeRelicCardContentFromData(Box, Relic, 0.88f), TEXT("authored_delete_relic"), Index);
		}
	}
	if (Run->GetPersistentAuthoredCards().Num() + Run->GetPersistentAuthoredRelics().Num() == 0)
	{
		UTextBlock* Empty = Style::MakeText(Box, TEXT("尚未登记原创内容。"), 20, Style::DimGray());
		Empty->SetJustification(ETextJustify::Center);
		AddToVBox(Box, Empty, FMargin(40.f, 40.f));
	}
	AddToVBox(Box, MakeLinkedButton(Box, TEXT("【 返回标题 】"), TEXT("authored_library_back"), 0, 19), FMargin(260.f, 24.f));
	Pad(Box, 35.f);
	SetScreen(Scroll, EGameScreen::AuthoredLibrary);
}

// -----------------------------------------------------------
// 初始法器选择
// -----------------------------------------------------------

void AAscendPlayerController::ShowStartRelicChoice()
{
	UVerticalBox* Box = NewObject<UVerticalBox>(RootWidget);
	Pad(Box, 80);

	UTextBlock* Title = Style::MakeText(Box, TEXT("选择一件初始法器"), 32, Style::GoldYellow());
	Title->SetJustification(ETextJustify::Center);
	AddToVBox(Box, Title);
	Pad(Box, 6);

	UTextBlock* Sub = Style::MakeText(Box, TEXT("\"这面古镜看似平平无奇，镜中却有灵光流转……\""), 16, Style::DimGray());
	Sub->SetJustification(ETextJustify::Center);
	AddToVBox(Box, Sub);
	Pad(Box, 30);

	UHorizontalBox* Row = NewObject<UHorizontalBox>(Box);
	Row->AddChildToHorizontalBox(NewObject<USpacer>(Box))->SetSize(FSlateChildSize(ESlateSizeRule::Fill));

	StartRelicChoices = Run->RollInitialRelicChoices(3);
	for (int32 i = 0; i < StartRelicChoices.Num(); ++i)
	{
		const FRelicData* RD = Run->GetRelicData(StartRelicChoices[i]);
		if (!RD) continue;

		UVerticalBox* CardBox = NewObject<UVerticalBox>(Box);
		Pad(CardBox, 4);

		UWidget* RelicFace = MakeRelicCardContentFromData(CardBox, *RD, 1.0f);
		if (UVerticalBoxSlot* RelicSlot = CardBox->AddChildToVerticalBox(RelicFace))
		{
			RelicSlot->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Center);
			RelicSlot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
		}
		Pad(CardBox, 6);

		UButton* Btn = Style::MakeStyledButton(CardBox, TEXT("【选择】"), 16, RarityColor(RD->Rarity),
			FLinearColor(0.20f, 0.16f, 0.10f));
		UClickProxy* CP = NewObject<UClickProxy>(Btn);
		CP->Tag = TEXT("start_relic");
		CP->Index = i;
		CP->Owner = this;
		Btn->OnClicked.AddDynamic(CP, &UClickProxy::HandleClick);
		PendingScreenProxies.Add(CP);
		AddToVBox(CardBox, Btn, FMargin(0.f));

		USizeBox* CardSize = NewObject<USizeBox>(Box);
		CardSize->SetWidthOverride(200.f);
		CardSize->SetContent(CardBox);
		AddToHBox(Row, CardSize, FMargin(16.f, 0.f));
	}

	Row->AddChildToHorizontalBox(NewObject<USpacer>(Box))->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	AddToVBox(Box, Row);

	SetScreen(Box, EGameScreen::Title, TEXT("bgm_card_discovery"));
}

// -----------------------------------------------------------
// 地图
// -----------------------------------------------------------

void AAscendPlayerController::ShowMap()
{
	UE_LOG(LogTemp, Display, TEXT("[DIAG] ShowMap begin floor=%d choices=%d"), Run ? Run->State.CurrentFloor : -1, Run ? Run->CurrentChoices.Num() : -1);
	Run->EnsureFloorChoices();
	UE_LOG(LogTemp, Display, TEXT("[DIAG] ShowMap after EnsureFloorChoices choices=%d"), Run ? Run->CurrentChoices.Num() : -1);

	UVerticalBox* Box = NewObject<UVerticalBox>(RootWidget);
	Pad(Box, 30);

	// 层数指示
	UTextBlock* FloorT = Style::MakeText(Box,
		FString::Printf(TEXT("—— 第 %d 层 / 共 %d 层 ——"), Run->State.CurrentFloor + 1, URunManager::TotalFloors),
		26, Style::GoldYellow());
	FloorT->SetJustification(ETextJustify::Center);
	AddToVBox(Box, FloorT);
	Pad(Box, 8);

	// 状态栏
	const FRunState& S = Run->State;
	UTextBlock* StateBar = Style::MakeText(Box,
		FString::Printf(TEXT("【%s】  气血 %d/%d   灵石 %d   卡组 %d 张   法宝 %d 件   丹药 %d 颗   魔道因果 %d"),
			*S.Realm, S.HP, S.MaxHP, S.Gold, S.Deck.Num(), S.RelicIds.Num(), S.PillIds.Num(), S.KillStreak),
		18, Style::PaperWhite());
	StateBar->SetJustification(ETextJustify::Center);
	AddToVBox(Box, StateBar);
	Pad(Box, 30);

	// 迷雾提示
	UTextBlock* MistT = Style::MakeText(Box, TEXT("山间雾气弥漫，前路未卜。选择你的方向："), 16, Style::DimGray());
	MistT->SetJustification(ETextJustify::Center);
	AddToVBox(Box, MistT);
	Pad(Box, 16);

	// 选项按钮
	static const TCHAR* DirNames[] = {TEXT("向左走"), TEXT("向右走"), TEXT("深入迷雾"), TEXT("绕道而行")};
	int32 HiddenCount = 0;
	for (int32 i = 0; i < Run->CurrentChoices.Num(); ++i)
	{
		const FMysteryChoice& C = Run->CurrentChoices[i];

		FString Label;
		FSlateColor Color = Style::GoldYellow();
		if (C.bRevealed)
		{
			switch (C.Type)
			{
			case EMapNodeType::Combat:
			{
				FString Names;
				for (const FString& Id : C.EnemyIds)
				{
					if (const FEnemyData* E = Run->GetEnemyData(Id)) Names += E->Name + TEXT("、");
				}
				Names.RemoveFromEnd(TEXT("、"));
				Label = FString::Printf(TEXT("【与 %s 战斗】"), *Names);
				Color = Style::BloodRed();
				break;
			}
			case EMapNodeType::Elite:
			{
				FString Names;
				for (const FString& Id : C.EnemyIds)
				{
					if (const FEnemyData* E = Run->GetEnemyData(Id)) Names += E->Name;
				}
				Label = FString::Printf(TEXT("【精英强敌：%s！】"), *Names);
				Color = Style::PoisonPurple();
				break;
			}
			case EMapNodeType::Boss:
				Label = TEXT("【血袍老祖 · 决一死战】");
				Color = Style::BloodRed();
				break;
			case EMapNodeType::Rest:
				Label = TEXT("【打坐调息】");
				Color = Style::JadeGreen();
				break;
			case EMapNodeType::Shop:
				Label = TEXT("【坊市】");
				Color = Style::GoldYellow();
				break;
			case EMapNodeType::Event:
				Label = TEXT("【奇遇】");
				Color = Style::SpiritBlue();
				break;
			}
		}
		else
		{
			Label = FString::Printf(TEXT("【%s】"), DirNames[FMath::Min(HiddenCount, 3)]);
			HiddenCount++;
			Color = Style::DimGray();
		}

		UHorizontalBox* Row = NewObject<UHorizontalBox>(Box);
		Row->AddChildToHorizontalBox(NewObject<USpacer>(Box))->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		AddToHBox(Row, MakeLinkedButton(Row, Label, TEXT("node"), i, 20));
		Row->AddChildToHorizontalBox(NewObject<USpacer>(Box))->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		AddToVBox(Box, Row, FMargin(0.f, 8.f));
	}

	Pad(Box, 24);
	UTextBlock* Hint = Style::MakeText(Box, TEXT("未知方向藏着机缘，也藏着杀机"), 14, Style::DimGray());
	Hint->SetJustification(ETextJustify::Center);
	AddToVBox(Box, Hint);

	UE_LOG(LogTemp, Display, TEXT("[DIAG] ShowMap SetScreen begin"));
	SetScreen(Box, EGameScreen::Map);
	UE_LOG(LogTemp, Display, TEXT("[DIAG] ShowMap SetScreen done"));
	ShowGainToasts();
	UE_LOG(LogTemp, Display, TEXT("[DIAG] ShowMap done"));
}

// -----------------------------------------------------------
// 战斗
// -----------------------------------------------------------

void AAscendPlayerController::ShowCombat()
{
	// Boss 剧情
	if (!CurrentEncounter.StoryText.IsEmpty())
	{
		OnCombatLog(FString::Printf(TEXT("「%s」"), *CurrentEncounter.StoryText));
		CurrentEncounter.StoryText.Empty();
	}
	PileViewerMode = 0;
	bLogExpanded = false;
	RefreshCombatPanel();
	CurrentScreen = EGameScreen::Combat;
}

void AAscendPlayerController::RefreshCombatPanel()
{
	if (!Combat) return;
	UE_LOG(LogTemp, Display, TEXT("[DIAG] RefreshCombatPanel begin turn=%d logN=%d"), Combat->TurnCount, CombatLogLines.Num());

	UVerticalBox* Box = NewObject<UVerticalBox>(RootWidget);
	Pad(Box, 8);

	// ---- 主布局：左法器栏 + 右战斗内容 ----
	UHorizontalBox* MainHBox = NewObject<UHorizontalBox>(Box);
	UVerticalBoxSlot* MainSlot = Box->AddChildToVerticalBox(MainHBox);
	MainSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));

	// 左侧法器栏
	BuildRelicSidebar(MainHBox);

	// 右侧战斗内容
	UVerticalBox* RightBox = NewObject<UVerticalBox>(Box);

	// ---- 顶栏：中央立体回合牌匾 + 右侧日志 ----
	USizeBox* TopHudSize = NewObject<USizeBox>(RightBox);
	TopHudSize->SetHeightOverride(80.f);
	UOverlay* TopHud = NewObject<UOverlay>(TopHudSize);
	USizeBox* TurnSize = NewObject<USizeBox>(TopHud);
	TurnSize->SetWidthOverride(304.f);
	TurnSize->SetHeightOverride(68.f);
	UOverlay* TurnVisual = NewObject<UOverlay>(TurnSize);
	if (UImage* TurnFrame = FAscendArt::MakeImage(TurnVisual, TEXT("Art/ui/hud_turn_plaque.png")))
	{
		TurnFrame->SetVisibility(ESlateVisibility::HitTestInvisible);
		UOverlaySlot* TurnFrameSlot = TurnVisual->AddChildToOverlay(TurnFrame);
		TurnFrameSlot->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Fill);
		TurnFrameSlot->SetVerticalAlignment(EVerticalAlignment::VAlign_Fill);
	}
	UScaleBox* TurnTextScale = NewObject<UScaleBox>(TurnVisual);
	TurnTextScale->SetStretch(EStretch::ScaleToFit);
	TurnTextScale->SetStretchDirection(EStretchDirection::DownOnly);
	UTextBlock* TurnText = Style::MakeText(TurnTextScale,
		FString::Printf(TEXT("第 %d 回合"), Combat->TurnCount), 22, Style::GoldYellow());
	TurnText->SetJustification(ETextJustify::Center);
	TurnText->SetShadowOffset(FVector2D(1.f, 1.f));
	TurnTextScale->SetContent(TurnText);
	UOverlaySlot* TurnTextSlot = TurnVisual->AddChildToOverlay(TurnTextScale);
	TurnTextSlot->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Fill);
	TurnTextSlot->SetVerticalAlignment(EVerticalAlignment::VAlign_Fill);
	TurnTextSlot->SetPadding(FMargin(68.f, 15.f, 68.f, 16.f));
	TurnSize->SetContent(TurnVisual);
	UOverlaySlot* TurnSlot = TopHud->AddChildToOverlay(TurnSize);
	TurnSlot->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Center);
	TurnSlot->SetVerticalAlignment(EVerticalAlignment::VAlign_Top);
	TurnSlot->SetPadding(FMargin(0.f, 6.f, 0.f, 0.f));

	const FString LogLabel = bLogExpanded ? FString(TEXT("日志 ▼")) : FString::Printf(TEXT("日志 ▶ %d"), CombatLogLines.Num());
	UButton* LogButton = MakeLinkedButton(TopHud, LogLabel, TEXT("toggle_log"), 0, 12);
	USizeBox* LogSize = NewObject<USizeBox>(TopHud);
	LogSize->SetWidthOverride(132.f);
	LogSize->SetHeightOverride(42.f);
	LogSize->SetContent(LogButton);
	UOverlaySlot* LogSlot = TopHud->AddChildToOverlay(LogSize);
	LogSlot->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Right);
	LogSlot->SetVerticalAlignment(EVerticalAlignment::VAlign_Top);
	LogSlot->SetPadding(FMargin(0.f, 10.f, 18.f, 0.f));
	TopHudSize->SetContent(TopHud);
	AddToVBox(RightBox, TopHudSize, FMargin(0.f));

	if (Combat->PendingDiscoverChoices.Num() > 0)
	{
		UBorder* DiscoverPanel = NewObject<UBorder>(RightBox);
		DiscoverPanel->SetBrushColor(FLinearColor(0.08f, 0.16f, 0.19f, 0.97f));
		DiscoverPanel->SetPadding(FMargin(18.f, 10.f));
		UVerticalBox* DiscoverBox = NewObject<UVerticalBox>(DiscoverPanel);
		UTextBlock* DiscoverTitle = Style::MakeText(DiscoverBox,
			TEXT("地图指引 · 从随机候选中选择一张加入手牌"), 20, Style::GoldYellow());
		DiscoverTitle->SetJustification(ETextJustify::Center);
		AddToVBox(DiscoverBox, DiscoverTitle, FMargin(0.f, 0.f, 0.f, 8.f));
		UHorizontalBox* DiscoverRow = NewObject<UHorizontalBox>(DiscoverBox);
		DiscoverRow->AddChildToHorizontalBox(NewObject<USpacer>(DiscoverRow))->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		for (int32 ChoiceIndex = 0; ChoiceIndex < Combat->PendingDiscoverChoices.Num(); ++ChoiceIndex)
		{
			const FCardInstance& Candidate = Combat->PendingDiscoverChoices[ChoiceIndex];
			UButton* ChoiceButton = NewObject<UButton>(DiscoverRow);
			ChoiceButton->SetBackgroundColor(FLinearColor::Transparent);
			ChoiceButton->SetContent(MakeCardContentFromData(ChoiceButton, Candidate.Data, Candidate.bUpgraded, 0.82f));
			UClickProxy* Proxy = NewObject<UClickProxy>(ChoiceButton);
			Proxy->Tag = TEXT("discover_choice");
			Proxy->Index = ChoiceIndex;
			Proxy->Owner = this;
			ChoiceButton->OnClicked.AddDynamic(Proxy, &UClickProxy::HandleClick);
			PendingScreenProxies.Add(Proxy);
			AddToHBox(DiscoverRow, ChoiceButton, FMargin(8.f, 2.f));
		}
		DiscoverRow->AddChildToHorizontalBox(NewObject<USpacer>(DiscoverRow))->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		AddToVBox(DiscoverBox, DiscoverRow);
		DiscoverPanel->SetContent(DiscoverBox);
		AddToVBox(RightBox, DiscoverPanel, FMargin(20.f, 6.f));
	}

	// ---- 敌人区（卡面，可点击锁定） ----
	UHorizontalBox* EnemyRow = NewObject<UHorizontalBox>(RightBox);
	EnemyRow->AddChildToHorizontalBox(NewObject<USpacer>(RightBox))->SetSize(FSlateChildSize(ESlateSizeRule::Fill));

	for (int32 i = 0; i < Combat->Enemies.Num(); ++i)
	{
		const bool bAlive = Combat->Enemies[i].State.IsAlive();
		const bool bIsLocked = (LockedTargetIndex == i);

		UVerticalBox* EBox = NewObject<UVerticalBox>(RightBox);
		BuildEnemyCardWidget(EBox, i, bIsLocked);

		if (bAlive)
		{
			UButton* EBtn = NewObject<UButton>(EnemyRow);
			EBtn->SetBackgroundColor(FLinearColor::Transparent);
			EBtn->SetContent(EBox);
			UClickProxy* Proxy = NewObject<UClickProxy>(EBtn);
			Proxy->Tag = TEXT("lock_target");
			Proxy->Index = i;
			Proxy->Owner = this;
			EBtn->OnClicked.AddDynamic(Proxy, &UClickProxy::HandleClick);
			PendingScreenProxies.Add(Proxy);
			AddToHBox(EnemyRow, EBtn, FMargin(8.f, 0.f));
		}
		else
		{
			AddToHBox(EnemyRow, EBox, FMargin(8.f, 0.f));
		}
	}
	EnemyRow->AddChildToHorizontalBox(NewObject<USpacer>(RightBox))->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	AddToVBox(RightBox, EnemyRow);

	Pad(RightBox, 6);

	// ---- 战斗日志（可折叠） ----
	if (bLogExpanded)
	{
		UBorder* LogBorder = NewObject<UBorder>(RightBox);
		LogBorder->SetBrushColor(FLinearColor(0.1f, 0.1f, 0.13f, 1.f));
		LogBorder->SetPadding(FMargin(8.f));
		UScrollBox* LogScroll = NewObject<UScrollBox>(RightBox);
		const int32 StartLine = FMath::Max(0, CombatLogLines.Num() - 14);
		for (int32 i = StartLine; i < CombatLogLines.Num(); ++i)
		{
			LogScroll->AddChild(MakeLogText(CombatLogLines[i], Style::PaperWhite()));
		}
		LogBorder->SetContent(LogScroll);
		UVerticalBoxSlot* ExpandedLogSlot = RightBox->AddChildToVerticalBox(LogBorder);
		ExpandedLogSlot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
		ExpandedLogSlot->SetPadding(FMargin(40.f, 2.f));
		LogScroll->ScrollToEnd();
	}

	// ---- 已激活功法徽标行（有功法时显示） ----
	if (Combat->ActivePowerNames.Num() > 0)
	{
		UHorizontalBox* PowerRow = NewObject<UHorizontalBox>(RightBox);
		AddToHBox(PowerRow, Style::MakeText(PowerRow, TEXT("功法运转"), 12, Style::DimGray()),
			FMargin(2.f, 2.f, 6.f, 0.f));
		for (const FString& PName : Combat->ActivePowerNames)
		{
			UBorder* PBadge = NewObject<UBorder>(PowerRow);
			PBadge->SetBrushColor(FLinearColor(0.30f, 0.23f, 0.10f));
			PBadge->SetPadding(FMargin(6.f, 1.f));
			UTextBlock* PT = Style::MakeText(PBadge, PName, 12, Style::GoldYellow());
			PBadge->SetContent(PT);
			AddToHBox(PowerRow, PBadge, FMargin(0.f, 0.f, 4.f, 0.f));
		}
		PowerRow->AddChildToHorizontalBox(NewObject<USpacer>(RightBox))->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		AddToVBox(RightBox, PowerRow, FMargin(0.f, 2.f));
	}

	// 丹药仍放在内容流中；结束回合改为独立 HUD 浮层，避免挤压战场。
	if (Run->State.PillIds.Num() > 0)
	{
		UHorizontalBox* ActionRow = NewObject<UHorizontalBox>(RightBox);
		ActionRow->AddChildToHorizontalBox(NewObject<USpacer>(RightBox))->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		for (int32 i = 0; i < Run->State.PillIds.Num(); ++i)
		{
			UButton* PillButton = MakeLinkedButton(ActionRow,
				FString::Printf(TEXT("丹:%s"), *Run->State.PillIds[i]), TEXT("pill"), i, 14);
			if (Combat->bPlayerTurnSkipped) PillButton->SetIsEnabled(false);
			AddToHBox(ActionRow, PillButton);
		}
		UVerticalBoxSlot* ActionSlot = RightBox->AddChildToVerticalBox(ActionRow);
		ActionSlot->SetPadding(FMargin(0.f, 4.f, 28.f, 4.f));
		ActionSlot->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Fill);
	}

	// ---- Fill spacer pushes bottom area to bottom ----
	{
		USpacer* FS = NewObject<USpacer>(RightBox);
		UVerticalBoxSlot* FSlot = RightBox->AddChildToVerticalBox(FS);
		FSlateChildSize FillSize(ESlateSizeRule::Fill);
		FillSize.Value = 1.f;
		FSlot->SetSize(FillSize);
		FSlot->SetPadding(FMargin(0.f));
	}

	// ---- 底部栏：左 头像+气血 | 右 立体牌堆 ----
	UHorizontalBox* BottomRow = NewObject<UHorizontalBox>(RightBox);

	// 左下：固定坐标头像 HUD。美术框、椭圆头像、气血条和数字共用同一坐标契约，
	// 不再依靠 Overlay Padding 反推大小，避免头像方角或拉伸穿出圆孔。
	{
		USizeBox* HPCluster = NewObject<USizeBox>(BottomRow);
		HPCluster->SetWidthOverride(507.f);
		HPCluster->SetHeightOverride(158.f);
		UCanvasPanel* HPVisual = NewObject<UCanvasPanel>(HPCluster);

		if (UImage* AvatarImg = FAscendArt::MakeImage(HPVisual, TEXT("Art/avatar/player_cultivator_v2_hud.png")))
		{
			AvatarImg->SetVisibility(ESlateVisibility::HitTestInvisible);
			if (UCanvasPanelSlot* AvatarSlot = HPVisual->AddChildToCanvas(AvatarImg))
			{
				// hud_hp_panel 的透明内窗在原图 (58,38)-(317,218)，
				// 按 430x134 等比换算后正好是下面这块 109x75 椭圆区域。
				AvatarSlot->SetPosition(FVector2D(28.f, 19.f));
				AvatarSlot->SetSize(FVector2D(129.f, 89.f));
				AvatarSlot->SetZOrder(0);
			}
		}

		if (UImage* HPFrame = FAscendArt::MakeImage(HPVisual, TEXT("Art/ui/hud_hp_panel.png")))
		{
			HPFrame->SetVisibility(ESlateVisibility::HitTestInvisible);
			if (UCanvasPanelSlot* FrameSlot = HPVisual->AddChildToCanvas(HPFrame))
			{
				FrameSlot->SetPosition(FVector2D::ZeroVector);
				FrameSlot->SetSize(FVector2D(507.f, 158.f));
				FrameSlot->SetZOrder(10);
			}
		}

		// 覆盖美术稿中的示意红条，显示真实气血百分比。
		UBorder* HPBarBg = NewObject<UBorder>(HPVisual);
		HPBarBg->SetBrushColor(FLinearColor(0.055f, 0.045f, 0.04f, 1.f));
		HPBarBg->SetPadding(FMargin(2.f));
		UProgressBar* HPBar = NewObject<UProgressBar>(HPBarBg);
		HPBar->SetPercent(Combat->Player.MaxHP > 0 ? (float)Combat->Player.HP / Combat->Player.MaxHP : 0.f);
		HPBar->SetFillColorAndOpacity(FLinearColor(0.76f, 0.19f, 0.14f));
		HPBarBg->SetContent(HPBar);
		if (UCanvasPanelSlot* BarSlot = HPVisual->AddChildToCanvas(HPBarBg))
		{
			BarSlot->SetPosition(FVector2D(191.f, 97.f));
			BarSlot->SetSize(FVector2D(260.f, 22.f));
			BarSlot->SetZOrder(20);
		}

		UTextBlock* HPText = Style::MakeText(HPVisual,
			FString::Printf(TEXT("%d / %d"), Combat->Player.HP, Combat->Player.MaxHP), 27, Style::PaperWhite());
		HPText->SetJustification(ETextJustify::Center);
		HPText->SetShadowOffset(FVector2D(1.f, 1.f));
		if (UCanvasPanelSlot* TextSlot = HPVisual->AddChildToCanvas(HPText))
		{
			TextSlot->SetPosition(FVector2D(186.f, 38.f));
			TextSlot->SetSize(FVector2D(276.f, 47.f));
			TextSlot->SetZOrder(20);
		}

		HPCluster->SetContent(HPVisual);
		AddToHBox(BottomRow, HPCluster, FMargin(0.f, 0.f, 12.f, 0.f));
	}

	// Spacer 撑开，让牌堆在右
	BottomRow->AddChildToHorizontalBox(NewObject<USpacer>(BottomRow))->SetSize(FSlateChildSize(ESlateSizeRule::Fill));

	// 牌堆/弃牌堆（右下，以堆叠卡片展示）
	{
		USizeBox* PileCluster = NewObject<USizeBox>(BottomRow);
		PileCluster->SetWidthOverride(204.f);
		PileCluster->SetHeightOverride(132.f);
		UHorizontalBox* PileRow = NewObject<UHorizontalBox>(PileCluster);

		const float PileCardW = 78.f;
		const float PileCardH = 104.f;
		const float StackOffset = 3.f;
		const int32 MaxStackCards = 4;

		auto AddPileBack = [&](UOverlay* Target, const FString& ArtPath, int32 Count)
		{
			const int32 ShowN = FMath::Clamp(Count, 1, MaxStackCards);
			for (int32 si = ShowN - 1; si >= 0; --si)
			{
				USizeBox* CardSize = NewObject<USizeBox>(Target);
				CardSize->SetWidthOverride(PileCardW);
				CardSize->SetHeightOverride(PileCardH);
				if (UImage* Back = FAscendArt::MakeImage(CardSize, ArtPath))
				{
					Back->SetVisibility(ESlateVisibility::HitTestInvisible);
					CardSize->SetContent(Back);
				}
				UOverlaySlot* CardSlot = Target->AddChildToOverlay(CardSize);
				CardSlot->SetPadding(FMargin(StackOffset * si, 0.f, 0.f, StackOffset * si));
				CardSlot->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Left);
				CardSlot->SetVerticalAlignment(EVerticalAlignment::VAlign_Bottom);
			}
		};

		// --- 牌堆（背面朝上堆叠） ---
		{
			UButton* DrawBtn = NewObject<UButton>(PileRow);
			DrawBtn->SetBackgroundColor(FLinearColor::Transparent);
			UOverlay* DrawOvl = NewObject<UOverlay>(DrawBtn);
			AddPileBack(DrawOvl, TEXT("Art/ui/deck_back.png"), Combat->DrawPile.Num());

			// 数量文字
			UTextBlock* CountT = Style::MakeText(DrawOvl,
				FString::Printf(TEXT("%d"), Combat->DrawPile.Num()), 20, Style::GoldYellow());
			CountT->SetJustification(ETextJustify::Center);
			CountT->SetShadowOffset(FVector2D(1.f, 1.f));
			UOverlaySlot* CSlot = DrawOvl->AddChildToOverlay(CountT);
			CSlot->SetPadding(FMargin(0.f, 0.f, 8.f, 3.f));
			CSlot->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Fill);
			CSlot->SetVerticalAlignment(EVerticalAlignment::VAlign_Bottom);

			DrawBtn->SetContent(DrawOvl);

			UClickProxy* DP = NewObject<UClickProxy>(DrawBtn);
			DP->Tag = TEXT("pile_draw");
			DP->Index = 0;
			DP->Owner = this;
			DrawBtn->OnClicked.AddDynamic(DP, &UClickProxy::HandleClick);
			PendingScreenProxies.Add(DP);

			UHorizontalBoxSlot* DrawSlot = PileRow->AddChildToHorizontalBox(DrawBtn);
			DrawSlot->SetPadding(FMargin(0.f, 0.f, 12.f, 0.f));
			DrawSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		}

		// --- 弃牌堆（专用赤色牌背，和抽牌堆一眼可区分） ---
		{
			UButton* DiscardBtn = NewObject<UButton>(PileRow);
			DiscardBtn->SetBackgroundColor(FLinearColor::Transparent);
			UOverlay* DiscardOvl = NewObject<UOverlay>(DiscardBtn);

			const int32 N = Combat->DiscardPile.Num();
			AddPileBack(DiscardOvl, TEXT("Art/ui/discard_back.png"), N);

			UTextBlock* CountT = Style::MakeText(DiscardOvl,
				FString::Printf(TEXT("%d"), N), 20, Style::GoldYellow());
			CountT->SetJustification(ETextJustify::Center);
			CountT->SetShadowOffset(FVector2D(1.f, 1.f));
			UOverlaySlot* CSlot = DiscardOvl->AddChildToOverlay(CountT);
			CSlot->SetPadding(FMargin(0.f, 0.f, 8.f, 3.f));
			CSlot->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Fill);
			CSlot->SetVerticalAlignment(EVerticalAlignment::VAlign_Bottom);

			DiscardBtn->SetContent(DiscardOvl);

			UClickProxy* DP = NewObject<UClickProxy>(DiscardBtn);
			DP->Tag = TEXT("pile_discard");
			DP->Index = 0;
			DP->Owner = this;
			DiscardBtn->OnClicked.AddDynamic(DP, &UClickProxy::HandleClick);
			PendingScreenProxies.Add(DP);

			UHorizontalBoxSlot* DiscardSlot = PileRow->AddChildToHorizontalBox(DiscardBtn);
			DiscardSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		}

		PileCluster->SetContent(PileRow);
		AddToHBox(BottomRow, PileCluster, FMargin(6.f, 0.f, 2.f, 0.f));
	}

	AddToVBox(RightBox, BottomRow);

	// 右侧面板接入主布局
	MainHBox->AddChildToHorizontalBox(RightBox)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));

	// ---- 屏幕根：Overlay（底层主内容 + 牌堆检视浮层） ----
	UOverlay* ScreenOvl = NewObject<UOverlay>(RootWidget);
	{
		UOverlaySlot* MainOvlSlot = ScreenOvl->AddChildToOverlay(Box);
		MainOvlSlot->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Fill);
		MainOvlSlot->SetVerticalAlignment(EVerticalAlignment::VAlign_Fill);
	}
	if (PileViewerMode != 0)
	{
		BuildPileViewer(ScreenOvl);
	}

	// ---- 灵力+罡气+状态（置于手牌区上方，居中） ----
	{
		// UMG Canvas 的 Slot 坐标是 Slate 局部坐标，不一定等于窗口像素。
		// Retina 窗口下常见的是 1922x1081 local / 1280x720 absolute；
		// 直接拿 Viewport 像素摆牌会把整副牌压到左上区域。
		FVector2D CanvasSize(1920.f, 1080.f);
		if (AnimCanvas)
		{
			const FVector2D Measured = AnimCanvas->GetTickSpaceGeometry().GetLocalSize();
			if (Measured.X >= 640.f && Measured.Y >= 360.f)
				CanvasSize = Measured;
		}
		const float Vw = CanvasSize.X, Vh = CanvasSize.Y;
		CurrentHandCardScale = GetResponsiveHandScale(CanvasSize);
		const bool bCompactHand = CurrentHandCardScale < 0.99f;
		const float CardH = AscendCardLayout::Height * CurrentHandCardScale;
		const float HandBottomMargin = bCompactHand
			? AscendCardLayout::CompactHandBottomMargin : AscendCardLayout::HandBottomMargin;
		const float CardY = Vh - CardH - HandBottomMargin;
		const float SpiritH = 72.f;
		const float StatusRowH = 44.f;
		const float HandInfoGap = 8.f;
		const float SpiritY = CardY - SpiritH - HandInfoGap;
		const float StatusY = SpiritY - StatusRowH - 4.f;

		UCanvasPanel* InfoLayer = NewObject<UCanvasPanel>(ScreenOvl);
		UOverlaySlot* InfoSlot = ScreenOvl->AddChildToOverlay(InfoLayer);
		InfoSlot->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Fill);
		InfoSlot->SetVerticalAlignment(EVerticalAlignment::VAlign_Fill);

		// ---- 状态行（剑意值 / 负面效果），紧贴罡气上方 ----
		float StatusH = 0.f;
		if (UHorizontalBox* StatusRow = BuildStatusRow(InfoLayer, Combat->Player, 32))
		{
			USizeBox* StatusWrap = NewObject<USizeBox>(InfoLayer);
			StatusWrap->SetWidthOverride(Vw);
			StatusWrap->SetHeightOverride(44.f);
			UHorizontalBox* StatusCenter = NewObject<UHorizontalBox>(StatusWrap);
			StatusCenter->AddChildToHorizontalBox(NewObject<USpacer>(StatusCenter))->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			StatusCenter->AddChildToHorizontalBox(StatusRow);
			StatusCenter->AddChildToHorizontalBox(NewObject<USpacer>(StatusCenter))->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			StatusWrap->SetContent(StatusCenter);
			if (UCanvasPanelSlot* SSlot = InfoLayer->AddChildToCanvas(StatusWrap))
			{
				SSlot->SetPosition(FVector2D(0.f, StatusY));
				SSlot->SetSize(FVector2D(Vw, 44.f));
				SSlot->SetZOrder(101);
			}
			StatusH = 44.f;
		}

		UHorizontalBox* SpiritRow = NewObject<UHorizontalBox>(InfoLayer);
		SpiritRow->AddChildToHorizontalBox(NewObject<USpacer>(InfoLayer))->SetSize(FSlateChildSize(ESlateSizeRule::Fill));

		// 罡气与灵气共享一个立体外框，但内部仍是两个明确分区。
		const int32 VisibleSpiritSlots = FMath::Max(Combat->MaxSpirit, Combat->Spirit);
		USizeBox* ResourceSize = NewObject<USizeBox>(SpiritRow);
		ResourceSize->SetWidthOverride(720.f);
		ResourceSize->SetHeightOverride(66.f);
		UOverlay* ResourceVisual = NewObject<UOverlay>(ResourceSize);
		if (UImage* ResourceFrame = FAscendArt::MakeImage(ResourceVisual, TEXT("Art/ui/resource_panel.png")))
		{
			ResourceFrame->SetVisibility(ESlateVisibility::HitTestInvisible);
			UOverlaySlot* FrameSlot = ResourceVisual->AddChildToOverlay(ResourceFrame);
			FrameSlot->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Fill);
			FrameSlot->SetVerticalAlignment(EVerticalAlignment::VAlign_Fill);
		}
		UHorizontalBox* ResourceContent = NewObject<UHorizontalBox>(ResourceVisual);
		USizeBox* BlockArea = NewObject<USizeBox>(ResourceContent);
		BlockArea->SetWidthOverride(285.f);
		UTextBlock* BlockT = Style::MakeText(BlockArea,
			FString::Printf(TEXT("罡气  %d"), Combat->Player.Block), 28, Style::PaperWhite());
		BlockT->SetJustification(ETextJustify::Center);
		BlockT->SetShadowOffset(FVector2D(1.f, 1.f));
		BlockArea->SetContent(BlockT);
		UHorizontalBoxSlot* BlockAreaSlot = ResourceContent->AddChildToHorizontalBox(BlockArea);
		BlockAreaSlot->SetPadding(FMargin(72.f, 8.f, 0.f, 0.f));

		UHorizontalBox* SpiritContent = NewObject<UHorizontalBox>(ResourceContent);
		UTextBlock* SpiritLabel = Style::MakeText(SpiritContent, TEXT("灵气"), 26, Style::PaperWhite());
		UHorizontalBoxSlot* LabelSlot = SpiritContent->AddChildToHorizontalBox(SpiritLabel);
		LabelSlot->SetPadding(FMargin(18.f, 10.f, 8.f, 0.f));
		UHorizontalBox* GemRow = NewObject<UHorizontalBox>(SpiritContent);
		const float GemSize = VisibleSpiritSlots <= 8 ? 34.f : (VisibleSpiritSlots <= 12 ? 27.f : 21.f);
		for (int32 gi = 0; gi < VisibleSpiritSlots; ++gi)
		{
			USizeBox* GemSlotSize = NewObject<USizeBox>(GemRow);
			GemSlotSize->SetWidthOverride(GemSize);
			GemSlotSize->SetHeightOverride(GemSize);
			if (UImage* Gem = FAscendArt::MakeImage(GemSlotSize, TEXT("Art/ui/spirit_gem.png")))
			{
				Gem->SetVisibility(ESlateVisibility::HitTestInvisible);
				Gem->SetRenderOpacity(gi < Combat->Spirit ? 1.f : 0.20f);
				GemSlotSize->SetContent(Gem);
			}
			UHorizontalBoxSlot* GemSlot = GemRow->AddChildToHorizontalBox(GemSlotSize);
			GemSlot->SetPadding(FMargin(1.f, 9.f, 1.f, 0.f));
		}
		SpiritContent->AddChildToHorizontalBox(GemRow);
		UHorizontalBoxSlot* SpiritAreaSlot = ResourceContent->AddChildToHorizontalBox(SpiritContent);
		SpiritAreaSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		UOverlaySlot* ContentSlot = ResourceVisual->AddChildToOverlay(ResourceContent);
		ContentSlot->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Fill);
		ContentSlot->SetVerticalAlignment(EVerticalAlignment::VAlign_Fill);
		ResourceSize->SetContent(ResourceVisual);
		SpiritRow->AddChildToHorizontalBox(ResourceSize);

		if (Combat->Toxicity > 0)
		{
			const bool bDanger = Combat->Toxicity >= 5;
			UTextBlock* ToxT = Style::MakeText(SpiritRow,
				FString::Printf(TEXT("丹毒%d%s"), Combat->Toxicity, bDanger ? TEXT("!") : TEXT("")),
				28, bDanger ? Style::BloodRed() : Style::PoisonPurple());
			SpiritRow->AddChildToHorizontalBox(ToxT);
		}
		SpiritRow->AddChildToHorizontalBox(NewObject<USpacer>(InfoLayer))->SetSize(FSlateChildSize(ESlateSizeRule::Fill));

		USizeBox* SpiritWrap = NewObject<USizeBox>(InfoLayer);
		SpiritWrap->SetWidthOverride(Vw);
		SpiritWrap->SetHeightOverride(72.f);
		SpiritWrap->SetContent(SpiritRow);

		if (UCanvasPanelSlot* ISlot = InfoLayer->AddChildToCanvas(SpiritWrap))
		{
			ISlot->SetPosition(FVector2D(0.f, SpiritY));
			ISlot->SetSize(FVector2D(Vw, 72.f));
			ISlot->SetZOrder(100);
		}
	}

	// ---- 手牌：叠加在屏幕底部（根据视口尺寸计算坐标） ----
	HideCardPreview();
	HandCardButtons.Empty();
	{
		FVector2D CanvasSize(1920.f, 1080.f);
		if (AnimCanvas)
		{
			const FVector2D Measured = AnimCanvas->GetTickSpaceGeometry().GetLocalSize();
			if (Measured.X >= 640.f && Measured.Y >= 360.f)
				CanvasSize = Measured;
		}
		const float ViewW = CanvasSize.X;
		const float ViewH = CanvasSize.Y;
		CurrentHandCardScale = GetResponsiveHandScale(CanvasSize);
		const bool bCompactHand = CurrentHandCardScale < 0.99f;

		UCanvasPanel* HandLayer = NewObject<UCanvasPanel>(ScreenOvl);
		UOverlaySlot* HandLayerSlot = ScreenOvl->AddChildToOverlay(HandLayer);
		HandLayerSlot->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Fill);
		HandLayerSlot->SetVerticalAlignment(EVerticalAlignment::VAlign_Fill);

		const int32 N = Combat->Hand.Num();
		const float CardW = AscendCardLayout::Width * CurrentHandCardScale;
		const float CardH = AscendCardLayout::Height * CurrentHandCardScale;
		const float HandBottomMargin = bCompactHand
			? AscendCardLayout::CompactHandBottomMargin : AscendCardLayout::HandBottomMargin;
		// 恢复底部居中的扇形持牌；悬停时仅改变卡面视觉（竖直上抬+放大），
		// 按钮本身仍保留固定命中区域，避免 Hover/Unhover 震荡。
		const float FanAngle = bCompactHand ? 46.f : 40.f;
		const float FanRadius = ViewH * (bCompactHand ? 0.30f : 0.35f);
		const float FanCenterX = ViewW * 0.5f;
		const float FanCenterY = ViewH - HandBottomMargin + FanRadius;

		for (int32 i = 0; i < N; ++i)
		{
			const bool bPlayable = !Combat->bPlayerTurnSkipped &&
				(Combat->GetEffectiveCost(Combat->Hand[i]) <= Combat->Spirit);

			UButton* CardBtn = NewObject<UButton>(HandLayer);
			// Do not let SButton capture the Android pointer. With Down/MouseDown
			// the press still starts the drag, while subsequent moves reach the root
			// widget and the PlayerController even after leaving the card bounds.
#if PLATFORM_ANDROID
			CardBtn->SetTouchMethod(EButtonTouchMethod::Down);
			CardBtn->SetClickMethod(EButtonClickMethod::MouseDown);
#endif
			BuildCardWidget(CardBtn, i, bPlayable, CurrentHandCardScale);
			if (!bPlayable) CardBtn->SetIsEnabled(false);

			HandCardButtons.SetNum(FMath::Max(HandCardButtons.Num(), i + 1));
			HandCardButtons[i] = CardBtn;

			UClickProxy* Proxy = NewObject<UClickProxy>(CardBtn);
			Proxy->Tag = TEXT("hand_card");
			Proxy->Index = i;
			Proxy->Owner = this;
			// Keep OnPressed as the reliable card hit-test fallback. Android also
			// receives the viewport touch lifecycle; HandleCardPressed is idempotent
			// so the two paths cannot cancel each other.
			CardBtn->OnPressed.AddDynamic(Proxy, &UClickProxy::HandlePress);
			PendingScreenProxies.Add(Proxy);

			UClickProxy* HoverProxy = NewObject<UClickProxy>(CardBtn);
			HoverProxy->Tag = TEXT("hand_hover");
			HoverProxy->Index = i;
			HoverProxy->Owner = this;
			HoverProxy->BoundButton = CardBtn;
			CardBtn->OnHovered.AddDynamic(HoverProxy, &UClickProxy::HandleHovered);
			CardBtn->OnUnhovered.AddDynamic(HoverProxy, &UClickProxy::HandleUnhovered);
			PendingScreenProxies.Add(HoverProxy);

			const float AngleDeg = (N > 1) ? (static_cast<float>(i) / (N - 1) - 0.5f) * FanAngle : 0.f;
			const float AngleRad = FMath::DegreesToRadians(AngleDeg);
			const float Bx = FanCenterX + FanRadius * FMath::Sin(AngleRad);
			const float By = FanCenterY - FanRadius * FMath::Cos(AngleRad);
			const float CardX = Bx - CardW * 0.5f;
			const float CardY = By - CardH;

			if (UCanvasPanelSlot* Slot = HandLayer->AddChildToCanvas(CardBtn))
			{
				Slot->SetPosition(FVector2D(CardX, CardY));
				Slot->SetSize(FVector2D(CardW, CardH));
				Slot->SetZOrder(i);
				// 只旋转卡面视觉，按钮命中区域保持固定，避免悬停抬升后反复 Hover/Unhover。
				if (UWidget* CardVisual = CardBtn->GetContent())
				{
					CardVisual->SetRenderTransformPivot(FVector2D(0.5f, 1.0f));
					CardVisual->SetRenderTransformAngle(AngleDeg);
				}
			}
		}
	}

	// ---- 右侧独立圆形结束回合控件：不占据纵向布局，也不挤压手牌 ----
	UCanvasPanel* EndTurnLayer = NewObject<UCanvasPanel>(ScreenOvl);
	EndTurnLayer->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	UOverlaySlot* EndLayerSlot = ScreenOvl->AddChildToOverlay(EndTurnLayer);
	EndLayerSlot->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Fill);
	EndLayerSlot->SetVerticalAlignment(EVerticalAlignment::VAlign_Fill);
	UButton* EndTurnButton = MakeLinkedButton(EndTurnLayer, TEXT(""), TEXT("endturn"), 0, 20);
	EndTurnButton->SetBackgroundColor(FLinearColor::Transparent);
	EndTurnButton->SetIsEnabled(Combat->PendingDiscoverChoices.Num() == 0);
	UOverlay* EndVisual = NewObject<UOverlay>(EndTurnButton);
	if (UImage* EndFrame = FAscendArt::MakeImage(EndVisual, TEXT("Art/ui/hud_end_turn.png")))
	{
		EndFrame->SetVisibility(ESlateVisibility::HitTestInvisible);
		UOverlaySlot* EndFrameSlot = EndVisual->AddChildToOverlay(EndFrame);
		EndFrameSlot->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Fill);
		EndFrameSlot->SetVerticalAlignment(EVerticalAlignment::VAlign_Fill);
	}
	UScaleBox* EndTextScale = NewObject<UScaleBox>(EndVisual);
	EndTextScale->SetStretch(EStretch::ScaleToFit);
	EndTextScale->SetStretchDirection(EStretchDirection::DownOnly);
	UTextBlock* EndText = Style::MakeText(EndTextScale,
		Combat->bPlayerTurnSkipped ? TEXT("梦魇中\n结束回合") : TEXT("结束回合"),
		Combat->bPlayerTurnSkipped ? 15 : 20, Style::GoldYellow());
	EndText->SetJustification(ETextJustify::Center);
	EndText->SetShadowOffset(FVector2D(1.f, 1.f));
	EndTextScale->SetContent(EndText);
	UOverlaySlot* EndTextSlot = EndVisual->AddChildToOverlay(EndTextScale);
	EndTextSlot->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Fill);
	EndTextSlot->SetVerticalAlignment(EVerticalAlignment::VAlign_Fill);
	EndTextSlot->SetPadding(FMargin(38.f, 42.f, 38.f, 49.f));
	EndTurnButton->SetContent(EndVisual);
	if (UCanvasPanelSlot* EndSlot = EndTurnLayer->AddChildToCanvas(EndTurnButton))
	{
		EndSlot->SetAnchors(FAnchors(1.f, 0.57f));
		EndSlot->SetAlignment(FVector2D(1.f, 0.5f));
		EndSlot->SetPosition(FVector2D(-24.f, 0.f));
		EndSlot->SetSize(FVector2D(182.f, 182.f));
		EndSlot->SetZOrder(300);
	}

	SetScreen(ScreenOvl, EGameScreen::Combat);

	// ---- 动画判定：回合切换或回合内抽牌 ----
	const bool bTurnStart = Combat->TurnCount != LastSeenTurnCount;
	const bool bInTurnDraw = Combat->PendingDrawCount > 0 && !bTurnStart;

	if (bTurnStart || bInTurnDraw)
	{
		// 设初始动画状态：从牌堆位置缩小透明飞入（在 SetScreen 之后立即设置，首帧已渲染但用户不可见）
		const FVector2D PileA(GetViewportSize().X - 100.f, GetViewportSize().Y - 200.f);
		const int32 FirstAnimatedCard = bInTurnDraw
			? FMath::Max(0, HandCardButtons.Num() - Combat->PendingDrawCount)
			: 0;
		for (int32 si = FirstAnimatedCard; si < HandCardButtons.Num(); ++si)
		{
			if (!HandCardButtons[si].IsValid()) continue;
			HandCardButtons[si]->SetRenderTranslation(PileA);
			HandCardButtons[si]->SetRenderScale(FVector2D(0.2f, 0.2f));
			HandCardButtons[si]->SetRenderOpacity(0.f);
		}
	}

	if (bTurnStart)
	{
		LastSeenTurnCount = Combat->TurnCount;
		PlayHandEntranceAnimation(0);
		Combat->PendingDrawCount = 0;
	}
	else if (bInTurnDraw)
	{
		PlayHandEntranceAnimation(Combat->PendingDrawCount);
		Combat->PendingDrawCount = 0;
	}
}

// -----------------------------------------------------------
// 奖励 / 杀人夺宝
// -----------------------------------------------------------

void AAscendPlayerController::ShowReward()
{
	UVerticalBox* Box = NewObject<UVerticalBox>(RootWidget);
	Pad(Box, 80);

	// Cultivation is a run-local reward that must stay visible until every
	// queued realm-upgrade choice is claimed.  Keep this panel in the reward
	// flow so a card/loot click can never silently discard pending choices.
	auto AddCultivationStatus = [this, Box](bool bShowChoiceButtons)
	{
		if (!Run) return;
		const FCultivationState& Cultivation = Run->State.Cultivation;
		FString StatusText = CultivationStatusText(Cultivation);
		if (Cultivation.PendingChoiceCount > 0)
		{
			StatusText += FString::Printf(TEXT("\n待领取修炼奖励：%d 次（领取后才能离开奖励页）"),
				Cultivation.PendingChoiceCount);
		}
		UTextBlock* Status = Style::MakeText(Box, StatusText, 16,
			Cultivation.PendingChoiceCount > 0 ? Style::GoldYellow() : Style::JadeGreen());
		Status->SetJustification(ETextJustify::Center);
		Status->SetAutoWrapText(true);
		Status->SetWrapTextAt(1000.f);
		AddToVBox(Box, Status, FMargin(24.f, 4.f));

		if (!bShowChoiceButtons || Cultivation.PendingChoiceCount <= 0) return;
		UTextBlock* ChoiceHint = Style::MakeText(Box,
			TEXT("—— 境界已突破：选择一项修炼奖励 ——"), 17, Style::GoldYellow());
		ChoiceHint->SetJustification(ETextJustify::Center);
		AddToVBox(Box, ChoiceHint, FMargin(6.f, 10.f, 6.f, 4.f));
		UHorizontalBox* ChoiceRow = NewObject<UHorizontalBox>(Box);
		ChoiceRow->AddChildToHorizontalBox(NewObject<USpacer>(Box))->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		static const TCHAR* ChoiceLabels[] = {
			TEXT("【锻体】气血上限 +2"), TEXT("【纳气】灵气储备 +1"), TEXT("【悟法】悟法次数 +1")
		};
		for (int32 ChoiceIndex = 0; ChoiceIndex < 3; ++ChoiceIndex)
		{
			AddToHBox(ChoiceRow, MakeLinkedButton(ChoiceRow, ChoiceLabels[ChoiceIndex],
				TEXT("cultivation_choice"), ChoiceIndex, 16), FMargin(8.f, 0.f));
		}
		ChoiceRow->AddChildToHorizontalBox(NewObject<USpacer>(Box))->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		AddToVBox(Box, ChoiceRow, FMargin(6.f, 0.f, 6.f, 8.f));
	};

	// 精英战杀人夺宝抉择
	if (bPendingKillLootChoice)
	{
		bPendingKillLootChoice = false;

		UTextBlock* T = Style::MakeText(Box, TEXT("精英伏诛"), 34, Style::GoldYellow());
		T->SetJustification(ETextJustify::Center);
		AddToVBox(Box, T);
		Pad(Box, 20);

		UTextBlock* Desc = Style::MakeText(Box,
			TEXT("敌人尸身就在眼前。搜刮夺宝可获双倍灵石与额外卡牌，\n但魔道因果缠身，日后的敌人会更强。"),
			18, Style::PaperWhite());
		Desc->SetJustification(ETextJustify::Center);
		AddToVBox(Box, Desc);
		Pad(Box, 30);
		AddCultivationStatus(false);
		Pad(Box, 8);

		UHorizontalBox* Row = NewObject<UHorizontalBox>(Box);
		Row->AddChildToHorizontalBox(NewObject<USpacer>(Box))->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		AddToHBox(Row, MakeLinkedButton(Row, TEXT("【杀人夺宝！】"), TEXT("loot_yes"), 0, 24));
		AddToHBox(Row, MakeLinkedButton(Row, TEXT("【见好就收】"), TEXT("loot_no"), 0, 24));
		Row->AddChildToHorizontalBox(NewObject<USpacer>(Box))->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		AddToVBox(Box, Row);

		SetScreen(Box, EGameScreen::Reward);
		return;
	}

	UTextBlock* T = Style::MakeText(Box, TEXT("战斗胜利"), 34, Style::GoldYellow());
	T->SetJustification(ETextJustify::Center);
	AddToVBox(Box, T);
	Pad(Box, 10);

	FString LootStr = FString::Printf(TEXT("获得 %d 灵石"), PendingReward.Gold);
	if (!PendingReward.RelicId.IsEmpty())
	{
		LootStr += FString::Printf(TEXT("   法宝 +1"));
	}
	UTextBlock* Loot = Style::MakeText(Box, LootStr, 18, Style::PaperWhite());
	Loot->SetJustification(ETextJustify::Center);
	AddToVBox(Box, Loot);
	Pad(Box, 20);
	AddCultivationStatus(true);
	Pad(Box, 10);

	const bool bCultivationBlocking = Run && Run->State.Cultivation.PendingChoiceCount > 0;
	UTextBlock* PickHint = Style::MakeText(Box,
		bCultivationBlocking
			? TEXT("—— 请先领取修炼奖励，再选择一张卡牌加入卡组 ——")
			: TEXT("—— 选择一张卡牌加入卡组 ——"),
		18, bCultivationBlocking ? Style::GoldYellow() : Style::JadeGreen());
	PickHint->SetJustification(ETextJustify::Center);
	AddToVBox(Box, PickHint);
	Pad(Box, 10);

	UHorizontalBox* Row = NewObject<UHorizontalBox>(Box);
	Row->AddChildToHorizontalBox(NewObject<USpacer>(Box))->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	for (int32 i = 0; i < PendingReward.CardChoices.Num(); ++i)
	{
		const FDeckCard& DC = PendingReward.CardChoices[i];
		if (const FCardData* C = Run->GetCardData(DC.CardId))
		{
			UVerticalBox* CardBox = NewObject<UVerticalBox>(Box);
			if (UVerticalBoxSlot* CardSlot = CardBox->AddChildToVerticalBox(
				MakeCardContentFromData(CardBox, *C, DC.bUpgraded, 0.9f)))
			{
				CardSlot->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Center);
				CardSlot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
			}
			Pad(CardBox, 4);
			UButton* Btn = MakeLinkedButton(CardBox, TEXT("【选择】"), TEXT("reward"), i, 16);
			if (bCultivationBlocking) Btn->SetIsEnabled(false);
			AddToVBox(CardBox, Btn, FMargin(0.f));
			AddToHBox(Row, CardBox, FMargin(12.f, 0.f));
		}
	}
	Row->AddChildToHorizontalBox(NewObject<USpacer>(Box))->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	AddToVBox(Box, Row);
	Pad(Box, 20);

	UHorizontalBox* SkipRow = NewObject<UHorizontalBox>(Box);
	SkipRow->AddChildToHorizontalBox(NewObject<USpacer>(Box))->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	UButton* SkipButton = MakeLinkedButton(SkipRow, TEXT("【跳过】"), TEXT("reward_skip"), 0, 18);
	if (bCultivationBlocking) SkipButton->SetIsEnabled(false);
	AddToHBox(SkipRow, SkipButton);
	SkipRow->AddChildToHorizontalBox(NewObject<USpacer>(Box))->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	AddToVBox(Box, SkipRow);

	SetScreen(Box, EGameScreen::Reward);
	ShowGainToasts();
}

// -----------------------------------------------------------
// 从法器数据构建卡面
// -----------------------------------------------------------

UWidget* AAscendPlayerController::MakeRelicCardContentFromData(UObject* Outer, const FRelicData& RelicData, float Scale)
{
	const float S = Scale;
	{
		using namespace AscendCardLayout;
		const FLinearColor FixedRarityColor = RarityColor(RelicData.Rarity);
		FString RarityLabel = TEXT("凡品");
		if (RelicData.Rarity == TEXT("uncommon")) RarityLabel = TEXT("中品");
		else if (RelicData.Rarity == TEXT("rare")) RarityLabel = TEXT("上品");
		else if (RelicData.Rarity == TEXT("legendary")) RarityLabel = TEXT("仙品");

		USizeBox* FixedSizer = NewObject<USizeBox>(Outer);
		FixedSizer->SetWidthOverride(Width * S);
		FixedSizer->SetHeightOverride(Height * S);
		UCanvasPanel* Canvas = NewObject<UCanvasPanel>(FixedSizer);

		UBorder* Backplate = NewObject<UBorder>(Canvas);
		Backplate->SetBrushColor(FLinearColor(0.018f, 0.045f, 0.038f, 1.f));
		PlaceCardWidget(Canvas, Backplate, InnerX, InnerY, InnerW, InnerH, S, 0);

		UBorder* ArtClip = NewObject<UBorder>(Canvas);
		ArtClip->SetClipping(EWidgetClipping::ClipToBounds);
		ArtClip->SetBrushColor(FLinearColor(0.012f, 0.026f, 0.024f, 1.f));
		ArtClip->SetPadding(FMargin(7.f * S, 7.f * S, 7.f * S, 5.f * S));
		if (UImage* RelicImage = FAscendArt::MakeImage(ArtClip, RelicData.ArtPath))
		{
			UScaleBox* ArtScale = NewObject<UScaleBox>(ArtClip);
			ArtScale->SetStretch(EStretch::ScaleToFit);
			ArtScale->SetStretchDirection(EStretchDirection::DownOnly);
			ArtScale->SetContent(RelicImage);
			ArtClip->SetContent(ArtScale);
		}
		else
		{
			const FString Initials = RelicData.Name.Len() >= 2 ? RelicData.Name.Left(2) : TEXT("法器");
			UTextBlock* Fallback = Style::MakeText(ArtClip, Initials, FMath::RoundToInt(26.f * S), Style::PaperWhite());
			Fallback->SetJustification(ETextJustify::Center);
			ArtClip->SetContent(Fallback);
		}
		PlaceCardWidget(Canvas, ArtClip, ArtX, ArtY, ArtW, ArtH, S, 5);

		UOverlay* TitlePanel = NewObject<UOverlay>(Canvas);
		if (UImage* TitleArt = FAscendArt::MakeImage(TitlePanel, TEXT("Art/ui/card_title_bar_v2.png")))
		{
			TitleArt->SetVisibility(ESlateVisibility::HitTestInvisible);
			UOverlaySlot* TitleArtSlot = TitlePanel->AddChildToOverlay(TitleArt);
			TitleArtSlot->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Fill);
			TitleArtSlot->SetVerticalAlignment(EVerticalAlignment::VAlign_Fill);
		}
		UTextBlock* NameText = Style::MakeText(TitlePanel, RelicData.Name,
			FMath::Clamp(FMath::RoundToInt(14.f * S), 11, 17), FixedRarityColor);
		NameText->SetJustification(ETextJustify::Center);
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
			UOverlaySlot* RulesArtSlot = RulesPanel->AddChildToOverlay(RulesArt);
			RulesArtSlot->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Fill);
			RulesArtSlot->SetVerticalAlignment(EVerticalAlignment::VAlign_Fill);
		}
		UVerticalBox* Details = NewObject<UVerticalBox>(RulesPanel);
		UTextBlock* RarityText = Style::MakeText(Details, RarityLabel,
			FMath::Clamp(FMath::RoundToInt(9.f * S), 8, 11), FixedRarityColor);
		RarityText->SetJustification(ETextJustify::Center);
		Details->AddChildToVerticalBox(RarityText)->SetPadding(FMargin(0.f, 1.f * S, 0.f, 0.f));
		UTextBlock* DescriptionText = Style::MakeText(Details, RelicData.Description,
			FMath::Clamp(FMath::RoundToInt(10.f * S), 8, 12), Style::PaperWhite());
		DescriptionText->SetAutoWrapText(true);
		DescriptionText->SetWrapTextAt(120.f * S);
		DescriptionText->SetJustification(ETextJustify::Center);
		UScaleBox* DescriptionScale = NewObject<UScaleBox>(Details);
		DescriptionScale->SetStretch(EStretch::ScaleToFit);
		DescriptionScale->SetStretchDirection(EStretchDirection::DownOnly);
		DescriptionScale->SetContent(DescriptionText);
		UVerticalBoxSlot* DescriptionSlot = Details->AddChildToVerticalBox(DescriptionScale);
		DescriptionSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		DescriptionSlot->SetPadding(FMargin(2.f * S, 0.f, 2.f * S, 2.f * S));
		UOverlaySlot* DetailsSlot = RulesPanel->AddChildToOverlay(Details);
		DetailsSlot->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Fill);
		DetailsSlot->SetVerticalAlignment(EVerticalAlignment::VAlign_Fill);
		DetailsSlot->SetPadding(FMargin(10.f * S, 7.f * S, 10.f * S, 7.f * S));
		PlaceCardWidget(Canvas, RulesPanel, RulesX, RulesY, RulesW, RulesH, S, 15);

		if (UImage* Frame = FAscendArt::MakeImage(Canvas, TEXT("Art/ui/card_border_v2.png")))
		{
			Frame->SetVisibility(ESlateVisibility::HitTestInvisible);
			PlaceCardWidget(Canvas, Frame, 0.f, 0.f, Width, Height, S, 40);
		}
		FixedSizer->SetContent(Canvas);
		return FixedSizer;
	}

#if 0 // Legacy adaptive relic layout retained only for reference during the visual migration.
	USizeBox* CardSizer = NewObject<USizeBox>(Outer);
	CardSizer->SetWidthOverride(162.f * S);
	CardSizer->SetHeightOverride(203.f * S);

	const FLinearColor RCol = RarityColor(RelicData.Rarity);

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
		UOverlaySlot* FrameSlot = CardRoot->AddChildToOverlay(FrameArt);
		FrameSlot->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Fill);
		FrameSlot->SetVerticalAlignment(EVerticalAlignment::VAlign_Fill);
	}

	UBorder* Face = NewObject<UBorder>(CardRoot);
	Face->SetBrushColor(FLinearColor::Transparent);
	UOverlaySlot* FaceSlot = CardRoot->AddChildToOverlay(Face);
	FaceSlot->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Fill);
	FaceSlot->SetVerticalAlignment(EVerticalAlignment::VAlign_Fill);
	FaceSlot->SetPadding(FMargin(16.f * S, 16.f * S, 16.f * S, 14.f * S));

	UVerticalBox* VBox = NewObject<UVerticalBox>(Face);

	UBorder* IconArea = NewObject<UBorder>(VBox);
	IconArea->SetBrushColor(FLinearColor(0.018f, 0.03f, 0.028f, 0.72f));
	IconArea->SetClipping(EWidgetClipping::ClipToBounds);
	IconArea->SetPadding(FMargin(9.f * S, 12.f * S, 9.f * S, 4.f * S));
	UVerticalBoxSlot* IconSlot = VBox->AddChildToVerticalBox(IconArea);
	FSlateChildSize FillAll(ESlateSizeRule::Fill);
	FillAll.Value = 0.50f;
	IconSlot->SetSize(FillAll);

	if (UImage* RelicImage = FAscendArt::MakeImage(IconArea, RelicData.ArtPath))
	{
		UScaleBox* RelicScale = NewObject<UScaleBox>(IconArea);
		RelicScale->SetStretch(EStretch::ScaleToFit);
		RelicScale->SetStretchDirection(EStretchDirection::DownOnly);
		RelicScale->SetContent(RelicImage);
		IconArea->SetContent(RelicScale);
	}
	else
	{
		FString Short2 = RelicData.Name.Len() >= 2 ? RelicData.Name.Mid(0, 2)
			: (RelicData.Name.IsEmpty() ? TEXT("?") : RelicData.Name.Left(1));
		UTextBlock* IconTxt = Style::MakeText(IconArea, Short2, 26.f * S, Style::PaperWhite());
		IconTxt->SetJustification(ETextJustify::Center);
		IconArea->SetContent(IconTxt);
	}

	FString RarityCN = TEXT("凡品");
	if (RelicData.Rarity == TEXT("uncommon")) RarityCN = TEXT("中品");
	else if (RelicData.Rarity == TEXT("rare")) RarityCN = TEXT("上品");
	else if (RelicData.Rarity == TEXT("legendary")) RarityCN = TEXT("仙品");

	UBorder* RulesPanel = NewObject<UBorder>(VBox);
	RulesPanel->SetClipping(EWidgetClipping::ClipToBounds);
	RulesPanel->SetPadding(FMargin(7.f * S, 6.f * S, 7.f * S, 6.f * S));
	RulesPanel->SetBrushColor(FLinearColor(0.035f, 0.085f, 0.070f, 0.99f));
	UVerticalBoxSlot* RulesSlot = VBox->AddChildToVerticalBox(RulesPanel);
	FSlateChildSize RulesFill(ESlateSizeRule::Fill);
	RulesFill.Value = 0.50f;
	RulesSlot->SetSize(RulesFill);

	UVerticalBox* Details = NewObject<UVerticalBox>(RulesPanel);
	UTextBlock* RarityT = Style::MakeText(Details, RarityCN, 10.f * S, RCol);
	RarityT->SetJustification(ETextJustify::Center);
	Details->AddChildToVerticalBox(RarityT)->SetPadding(FMargin(0.f));

	UTextBlock* NameT = Style::MakeText(Details, RelicData.Name, 14.f * S, RCol);
	NameT->SetJustification(ETextJustify::Center);
	Details->AddChildToVerticalBox(NameT)->SetPadding(FMargin(4.f * S, 2.f * S));

	UTextBlock* DescT = Style::MakeText(Details, RelicData.Description, 10.f * S, Style::PaperWhite());
	DescT->SetAutoWrapText(true);
	DescT->SetWrapTextAt(122.f * S);
	DescT->SetJustification(ETextJustify::Center);
	UScaleBox* DescScale = NewObject<UScaleBox>(Details);
	DescScale->SetStretch(EStretch::ScaleToFit);
	DescScale->SetStretchDirection(EStretchDirection::DownOnly);
	DescScale->SetContent(DescT);
	UVerticalBoxSlot* DescSlot = Details->AddChildToVerticalBox(DescScale);
	DescSlot->SetPadding(FMargin(3.f * S, 1.f * S, 3.f * S, 2.f * S));
	DescSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));

	RulesPanel->SetContent(Details);

	Face->SetContent(VBox);
	CardSizer->SetContent(CardRoot);
	return CardSizer;
#endif
}

// -----------------------------------------------------------
// 事件
// -----------------------------------------------------------

void AAscendPlayerController::ShowEvent()
{
	UVerticalBox* Box = NewObject<UVerticalBox>(RootWidget);
	Pad(Box, 100);

	const FEventData* Ev = Run->GetCurrentEvent();
	if (!Ev) { ShowMap(); return; }

	UTextBlock* Title = Style::MakeText(Box, Ev->Title, 32, Style::GoldYellow());
	Title->SetJustification(ETextJustify::Center);
	AddToVBox(Box, Title);
	Pad(Box, 20);

	UTextBlock* Text = Style::MakeText(Box, Ev->Text, 18, Style::PaperWhite());
	Text->SetJustification(ETextJustify::Center);
	Text->SetAutoWrapText(true);
	AddToVBox(Box, Text, FMargin(120.f, 10.f));
	Pad(Box, 30);

	for (int32 i = 0; i < Ev->Choices.Num(); ++i)
	{
		UHorizontalBox* Row = NewObject<UHorizontalBox>(Box);
		Row->AddChildToHorizontalBox(NewObject<USpacer>(Box))->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		AddToHBox(Row, MakeLinkedButton(Row, FString::Printf(TEXT("【%s】"), *Ev->Choices[i].Text), TEXT("event_choice"), i, 18));
		Row->AddChildToHorizontalBox(NewObject<USpacer>(Box))->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		AddToVBox(Box, Row, FMargin(0.f, 6.f));
	}

	SetScreen(Box, EGameScreen::Event);
}

// -----------------------------------------------------------
// 剧情（叙事驱动）
// -----------------------------------------------------------

void AAscendPlayerController::ShowNarrative()
{
	if (!Run) return;
	const FNarrativeBeat Beat = Run->GetCurrentNarrativeBeat();

	UVerticalBox* Box = NewObject<UVerticalBox>(RootWidget);
	Pad(Box, 70);
	UTextBlock* Title = Style::MakeText(Box, TEXT("尘 缘 一 瞬"), 32, Style::GoldYellow());
	Title->SetJustification(ETextJustify::Center);
	AddToVBox(Box, Title, FMargin(0.f, 0.f, 0.f, 12.f));

	UHorizontalBox* StoryRow = NewObject<UHorizontalBox>(Box);
	StoryRow->AddChildToHorizontalBox(NewObject<USpacer>(StoryRow))->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	AddToHBox(StoryRow, MakeNarrativeReadingPanel(StoryRow, Beat.NarratorText));
	StoryRow->AddChildToHorizontalBox(NewObject<USpacer>(StoryRow))->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	AddToVBox(Box, StoryRow, FMargin(24.f, 0.f, 24.f, 18.f));

	// 选项
	UButton* FirstChoice = nullptr;
	for (int32 i = 0; i < Beat.Choices.Num(); ++i)
	{
		UHorizontalBox* Row = NewObject<UHorizontalBox>(Box);
		Row->AddChildToHorizontalBox(NewObject<USpacer>(Box))->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		UButton* Choice = MakeLinkedButton(Row,
			FString::Printf(TEXT("%d　%s"), i + 1, *Beat.Choices[i].Text), TEXT("node"), i, 20);
		if (!FirstChoice) FirstChoice = Choice;
		AddToHBox(Row, Choice);
		Row->AddChildToHorizontalBox(NewObject<USpacer>(Box))->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		AddToVBox(Box, Row, FMargin(0.f, 8.f));
	}

	SetScreen(Box, EGameScreen::Event);
	if (FirstChoice && GetWorld())
	{
		TWeakObjectPtr<UButton> WeakFirst(FirstChoice);
		GetWorld()->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this,
			[WeakFirst]() { if (WeakFirst.IsValid()) WeakFirst->SetKeyboardFocus(); }));
	}
}

void AAscendPlayerController::ShowNarrativeOutcome()
{
	if (!Run) return;

	UVerticalBox* Box = NewObject<UVerticalBox>(RootWidget);
	Pad(Box, 80);

	UTextBlock* Title = Style::MakeText(Box, TEXT("意 外 之 旅"), 28, Style::GoldYellow());
	Title->SetJustification(ETextJustify::Center);
	AddToVBox(Box, Title);
	Pad(Box, 20);

	UHorizontalBox* SummaryRow = NewObject<UHorizontalBox>(Box);
	SummaryRow->AddChildToHorizontalBox(NewObject<USpacer>(SummaryRow))->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	AddToHBox(SummaryRow, MakeNarrativeReadingPanel(SummaryRow, Run->PendingNarrativeSummary));
	SummaryRow->AddChildToHorizontalBox(NewObject<USpacer>(SummaryRow))->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	AddToVBox(Box, SummaryRow, FMargin(24.f, 10.f, 24.f, 0.f));
	Pad(Box, 30);

	// 确定后进入游戏节点
	UHorizontalBox* Row = NewObject<UHorizontalBox>(Box);
	Row->AddChildToHorizontalBox(NewObject<USpacer>(Box))->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	AddToHBox(Row, MakeLinkedButton(Row, TEXT("【继续】"), TEXT("narrative_proceed"), 0, 22));
	Row->AddChildToHorizontalBox(NewObject<USpacer>(Box))->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	AddToVBox(Box, Row);

	SetScreen(Box, EGameScreen::Event);
}

// -----------------------------------------------------------
// 坊市
// -----------------------------------------------------------

void AAscendPlayerController::ShowShop()
{
	UScrollBox* Scroll = NewObject<UScrollBox>(RootWidget);
	UVerticalBox* Box = NewObject<UVerticalBox>(Scroll);
	Pad(Box, 40);

	const bool bNarrativeShop = bInfiniteFunctionFlowActive
		&& ActiveInfiniteGameOperation.Op == TEXT("open_shop");
	UTextBlock* T = Style::MakeText(Box, bNarrativeShop ? TEXT("仙 品 秘 市") : TEXT("坊 市"),
		32, Style::GoldYellow());
	T->SetJustification(ETextJustify::Center);
	AddToVBox(Box, T);

	UTextBlock* Gold = Style::MakeText(Box,
		FString::Printf(TEXT("灵石: %d"), Run->State.Gold), 20, Style::GoldYellow());
	Gold->SetJustification(ETextJustify::Center);
	AddToVBox(Box, Gold);
	if (bNarrativeShop)
	{
		UTextBlock* Rule = Style::MakeText(Box,
			FString::Printf(TEXT("仅售仙品卡牌 · 售价为正常价格的 %.0f%%"),
				ActiveInfiniteGameOperation.PriceMultiplier * 100.f),
			15, Style::DimGray());
		Rule->SetJustification(ETextJustify::Center);
		AddToVBox(Box, Rule, FMargin(10.f, 4.f));
	}
	Pad(Box, 12);

	TArray<FShopItem> Stock = Run->GetShopStock();
	const float ItemW = 180.f;
	const float CardScale = 0.85f;
	const float RelicScale = 0.75f;
	const int32 ItemsPerRow = 3;

	UHorizontalBox* Row = nullptr;

	auto MakePriceBtn = [&](UObject* Outer, int32 Price, int32 Idx, bool bAvailable)
	{
		UButton* Btn = Style::MakeStyledButton(Outer,
			FString::Printf(TEXT("【%d灵石】"), Price),
			14,
			bAvailable ? Style::GoldYellow() : Style::DimGray(),
			bAvailable ? FLinearColor(0.25f, 0.20f, 0.10f) : FLinearColor(0.10f, 0.10f, 0.10f));
		if (bAvailable)
		{
			UClickProxy* CP = NewObject<UClickProxy>(Btn);
			CP->Tag = TEXT("shop_buy");
			CP->Index = Idx;
			CP->Owner = this;
			Btn->OnClicked.AddDynamic(CP, &UClickProxy::HandleClick);
			PendingScreenProxies.Add(CP);
		}
		else
		{
			Btn->SetIsEnabled(false);
		}
		return Btn;
	};

	for (int32 i = 0; i < Stock.Num(); ++i)
	{
		if (i % ItemsPerRow == 0)
		{
			Row = NewObject<UHorizontalBox>(Box);
			Row->AddChildToHorizontalBox(NewObject<USpacer>(Box))->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			AddToVBox(Box, Row, FMargin(0.f, 4.f));
		}

		const FShopItem& It = Stock[i];
		bool bAvailable = !It.bSold && Run->State.Gold >= It.Price;

		UVerticalBox* ItemBox = NewObject<UVerticalBox>(Box);

		if (It.ItemType == TEXT("card"))
		{
			if (const FCardData* C = Run->GetCardData(It.ItemId))
				ItemBox->AddChild(MakeCardContentFromData(ItemBox, *C, false, CardScale));
		}
		else if (It.ItemType == TEXT("relic"))
		{
			if (const FRelicData* RD = Run->GetRelicData(It.ItemId))
				ItemBox->AddChild(MakeRelicCardContentFromData(ItemBox, *RD, RelicScale));
		}
		else
		{
			USizeBox* PillSize = NewObject<USizeBox>(ItemBox);
			PillSize->SetWidthOverride(ItemW);
			PillSize->SetHeightOverride(110.f);
			UBorder* PillB = NewObject<UBorder>(PillSize);
			PillB->SetBrushColor(FLinearColor(0.20f, 0.35f, 0.20f));
			PillB->SetPadding(FMargin(8.f));
			UTextBlock* PN = Style::MakeText(PillB, It.ItemId, 16, Style::PaperWhite());
			PN->SetJustification(ETextJustify::Center);
			PillB->SetContent(PN);
			PillSize->SetContent(PillB);
			ItemBox->AddChild(PillSize);
		}

		Pad(ItemBox, 4);
		UWidget* PriceWidget = MakePriceBtn(ItemBox, It.Price, i, bAvailable);
		ItemBox->AddChild(PriceWidget);

		USizeBox* Wrap = NewObject<USizeBox>(Box);
		Wrap->SetWidthOverride(ItemW);
		Wrap->SetContent(ItemBox);
		AddToHBox(Row, Wrap, FMargin(6.f, 0.f));

		// 每行最后一项后追加 Fill spacer 实现居中
		if (i % ItemsPerRow == ItemsPerRow - 1 || i == Stock.Num() - 1)
			Row->AddChildToHorizontalBox(NewObject<USpacer>(Box))->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	}

	Pad(Box, 20);
	UHorizontalBox* LeaveRow = NewObject<UHorizontalBox>(Box);
	LeaveRow->AddChildToHorizontalBox(NewObject<USpacer>(Box))->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	AddToHBox(LeaveRow, MakeLinkedButton(LeaveRow, TEXT("【离开坊市】"), TEXT("shop_leave"), 0, 20));
	LeaveRow->AddChildToHorizontalBox(NewObject<USpacer>(Box))->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	AddToVBox(Box, LeaveRow);

	Scroll->AddChild(Box);
	SetScreen(Scroll, EGameScreen::Shop);
	ShowGainToasts();
}

// -----------------------------------------------------------
// 打坐调息
// -----------------------------------------------------------

void AAscendPlayerController::ShowRest()
{
	UVerticalBox* Box = NewObject<UVerticalBox>(RootWidget);
	Pad(Box, 120);

	UTextBlock* T = Style::MakeText(Box, TEXT("打坐调息"), 32, Style::JadeGreen());
	T->SetJustification(ETextJustify::Center);
	AddToVBox(Box, T);
	Pad(Box, 20);

	UTextBlock* Desc = Style::MakeText(Box,
		FString::Printf(TEXT("灵气环绕，正是修整的好时机。\n当前气血 %d/%d"), Run->State.HP, Run->State.MaxHP),
		18, Style::PaperWhite());
	Desc->SetJustification(ETextJustify::Center);
	AddToVBox(Box, Desc);
	Pad(Box, 16);

	const FCultivationState& Cultivation = Run->State.Cultivation;
	const bool bCultivationBlocking = Cultivation.PendingChoiceCount > 0;
	FString CultivationText = CultivationStatusText(Cultivation);
	if (bCultivationBlocking)
	{
		CultivationText += FString::Printf(TEXT("\n待领取修炼奖励：%d 次（先领取后才能调息）"),
			Cultivation.PendingChoiceCount);
	}
	UTextBlock* CultivationStatus = Style::MakeText(Box, CultivationText, 16,
		bCultivationBlocking ? Style::GoldYellow() : Style::JadeGreen());
	CultivationStatus->SetJustification(ETextJustify::Center);
	CultivationStatus->SetAutoWrapText(true);
	CultivationStatus->SetWrapTextAt(1000.f);
	AddToVBox(Box, CultivationStatus, FMargin(24.f, 4.f));

	if (bCultivationBlocking)
	{
		UTextBlock* ChoiceHint = Style::MakeText(Box,
			TEXT("—— 境界已突破：选择一项修炼奖励 ——"), 17, Style::GoldYellow());
		ChoiceHint->SetJustification(ETextJustify::Center);
		AddToVBox(Box, ChoiceHint, FMargin(6.f, 10.f, 6.f, 4.f));
		UHorizontalBox* ChoiceRow = NewObject<UHorizontalBox>(Box);
		ChoiceRow->AddChildToHorizontalBox(NewObject<USpacer>(Box))->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		static const TCHAR* ChoiceLabels[] = {
			TEXT("【锻体】气血上限 +2"), TEXT("【纳气】灵气储备 +1"), TEXT("【悟法】悟法次数 +1")
		};
		for (int32 ChoiceIndex = 0; ChoiceIndex < 3; ++ChoiceIndex)
		{
			AddToHBox(ChoiceRow, MakeLinkedButton(ChoiceRow, ChoiceLabels[ChoiceIndex],
				TEXT("cultivation_choice"), ChoiceIndex, 16), FMargin(8.f, 0.f));
		}
		ChoiceRow->AddChildToHorizontalBox(NewObject<USpacer>(Box))->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		AddToVBox(Box, ChoiceRow, FMargin(6.f, 0.f, 6.f, 8.f));
	}
	Pad(Box, 14);

	UHorizontalBox* Row = NewObject<UHorizontalBox>(Box);
	Row->AddChildToHorizontalBox(NewObject<USpacer>(Box))->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	UButton* HealButton = MakeLinkedButton(Row, TEXT("【调息疗伤及恢复55%气血】"), TEXT("rest_heal"), 0, 20);
	UButton* UpgradeButton = MakeLinkedButton(Row, TEXT("【悟道修炼·随机升级一张牌】"), TEXT("rest_upgrade"), 0, 20);
	if (bCultivationBlocking)
	{
		HealButton->SetIsEnabled(false);
		UpgradeButton->SetIsEnabled(false);
	}
	AddToHBox(Row, HealButton);
	AddToHBox(Row, UpgradeButton);
	Row->AddChildToHorizontalBox(NewObject<USpacer>(Box))->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	AddToVBox(Box, Row);

	SetScreen(Box, EGameScreen::Rest);
}

// -----------------------------------------------------------
// 结局
// -----------------------------------------------------------

void AAscendPlayerController::ShowGameOver()
{
	UVerticalBox* Box = NewObject<UVerticalBox>(RootWidget);
	Pad(Box, 140);

	UTextBlock* T = Style::MakeText(Box, TEXT("道消身殒"), 48, Style::BloodRed());
	T->SetJustification(ETextJustify::Center);
	AddToVBox(Box, T);
	Pad(Box, 20);

	UTextBlock* Desc = Style::MakeText(Box, TEXT("修仙之路，九死一生。这一世，就到这了。"), 18, Style::PaperWhite());
	Desc->SetJustification(ETextJustify::Center);
	AddToVBox(Box, Desc);

	// 成就解锁信息（破纪录才有）
	if (Run && Run->LastUnlockMessages.Num() > 0)
	{
		Pad(Box, 18);
		for (const FString& M : Run->LastUnlockMessages)
		{
			UTextBlock* MT = Style::MakeText(Box, M, 16, Style::GoldYellow());
			MT->SetJustification(ETextJustify::Center);
			AddToVBox(Box, MT, FMargin(0.f, 2.f));
		}
	}
	Pad(Box, 40);

	UHorizontalBox* Row = NewObject<UHorizontalBox>(Box);
	Row->AddChildToHorizontalBox(NewObject<USpacer>(Box))->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	AddToHBox(Row, MakeLinkedButton(Row, TEXT("【再入轮回】"), TEXT("restart"), 0, 24));
	Row->AddChildToHorizontalBox(NewObject<USpacer>(Box))->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	AddToVBox(Box, Row);

	SetScreen(Box, EGameScreen::GameOver);
}

void AAscendPlayerController::ShowBreakthrough()
{
	UVerticalBox* Box = NewObject<UVerticalBox>(RootWidget);
	Pad(Box, 100);

	UTextBlock* T = Style::MakeText(Box, TEXT("天雷淬体 · 道基初成"), 40, Style::GoldYellow());
	T->SetJustification(ETextJustify::Center);
	AddToVBox(Box, T);
	Pad(Box, 20);

	UTextBlock* Desc = Style::MakeText(Box,
		FString::Printf(TEXT("你斩灭血袍老祖，为青石镇报了血仇。\n一战顿悟，突破至【筑基期】！\n\n气血上限 +10\n\n—— 第一章 · 完 ——")),
		18, Style::PaperWhite());
	Desc->SetJustification(ETextJustify::Center);
	AddToVBox(Box, Desc);

	// 成就解锁信息
	if (Run && Run->LastUnlockMessages.Num() > 0)
	{
		Pad(Box, 14);
		for (const FString& M : Run->LastUnlockMessages)
		{
			UTextBlock* MT = Style::MakeText(Box, M, 16, Style::GoldYellow());
			MT->SetJustification(ETextJustify::Center);
			AddToVBox(Box, MT, FMargin(0.f, 2.f));
		}
	}
	Pad(Box, 40);

	UHorizontalBox* Row = NewObject<UHorizontalBox>(Box);
	Row->AddChildToHorizontalBox(NewObject<USpacer>(Box))->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	AddToHBox(Row, MakeLinkedButton(Row, TEXT("【再开新局】"), TEXT("restart"), 0, 24));
	Row->AddChildToHorizontalBox(NewObject<USpacer>(Box))->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	AddToVBox(Box, Row);

	SetScreen(Box, EGameScreen::Victory);
}

void AAscendPlayerController::ShowVictory()
{
	ShowBreakthrough();
}

// -----------------------------------------------------------
// 法器栏（战斗左侧）
// -----------------------------------------------------------

void AAscendPlayerController::BuildRelicSidebar(UHorizontalBox* HBox)
{
	USizeBox* SidebarSize = NewObject<USizeBox>(HBox);
	SidebarSize->SetWidthOverride(104.f);
	UOverlay* SidebarRoot = NewObject<UOverlay>(SidebarSize);

	if (UImage* SidebarFrame = FAscendArt::MakeImage(SidebarRoot, TEXT("Art/ui/hud_relic_sidebar.png")))
	{
		SidebarFrame->SetVisibility(ESlateVisibility::HitTestInvisible);
		UOverlaySlot* FrameSlot = SidebarRoot->AddChildToOverlay(SidebarFrame);
		FrameSlot->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Fill);
		FrameSlot->SetVerticalAlignment(EVerticalAlignment::VAlign_Fill);
	}

	UVerticalBox* Sidebar = NewObject<UVerticalBox>(SidebarRoot);
	UOverlaySlot* SidebarSlot = SidebarRoot->AddChildToOverlay(Sidebar);
	SidebarSlot->SetPadding(FMargin(13.f, 36.f, 13.f, 34.f));
	SidebarSlot->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Fill);
	SidebarSlot->SetVerticalAlignment(EVerticalAlignment::VAlign_Fill);
	SidebarSize->SetContent(SidebarRoot);
	HBox->AddChildToHorizontalBox(SidebarSize)->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));

	if (!Run) return;

	int32 RelicIdx = 0;
	for (const FString& Rid : Run->State.RelicIds)
	{
		const FRelicData* RD = Run->GetRelicData(Rid);
		if (!RD) { RelicIdx++; continue; }

		FString Short2 = RD->Name.Len() >= 2 ? RD->Name.Mid(0, 2) : (RD->Name.IsEmpty() ? TEXT("?") : RD->Name.Left(1));

		UButton* IconBtn = NewObject<UButton>(Sidebar);
		IconBtn->SetBackgroundColor(FLinearColor::Transparent);
		USizeBox* IconSize = NewObject<USizeBox>(IconBtn);
		IconSize->SetWidthOverride(78.f);
		IconSize->SetHeightOverride(78.f);
		UOverlay* IconVisual = NewObject<UOverlay>(IconSize);
		IconVisual->SetClipping(EWidgetClipping::ClipToBounds);

		bool bHasRelicArt = false;
		if (!RD->ArtPath.IsEmpty())
		{
			if (UImage* RelicImg = FAscendArt::MakeImage(IconVisual, RD->ArtPath))
			{
				RelicImg->SetVisibility(ESlateVisibility::HitTestInvisible);
				UOverlaySlot* ArtSlot = IconVisual->AddChildToOverlay(RelicImg);
				ArtSlot->SetPadding(FMargin(3.f));
				ArtSlot->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Fill);
				ArtSlot->SetVerticalAlignment(EVerticalAlignment::VAlign_Fill);
				bHasRelicArt = true;
			}
		}

		if (!bHasRelicArt)
		{
			UScaleBox* RelicTextScale = NewObject<UScaleBox>(IconVisual);
			RelicTextScale->SetStretch(EStretch::ScaleToFit);
			RelicTextScale->SetStretchDirection(EStretchDirection::DownOnly);
			UTextBlock* Txt = Style::MakeText(RelicTextScale, Short2, 14, RarityColor(RD->Rarity));
			Txt->SetJustification(ETextJustify::Center);
			Txt->SetShadowOffset(FVector2D(1.f, 1.f));
			RelicTextScale->SetContent(Txt);
			UOverlaySlot* TextSlot = IconVisual->AddChildToOverlay(RelicTextScale);
			TextSlot->SetPadding(FMargin(17.f));
			TextSlot->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Fill);
			TextSlot->SetVerticalAlignment(EVerticalAlignment::VAlign_Fill);
		}

		IconSize->SetContent(IconVisual);
		IconBtn->SetContent(IconSize);

		UVerticalBoxSlot* Slot = Sidebar->AddChildToVerticalBox(IconBtn);
		Slot->SetPadding(FMargin(2.f, 5.f));
		Slot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));

		// 悬停工具提示
		UClickProxy* HoverProxy = NewObject<UClickProxy>(IconBtn);
		HoverProxy->Tag = TEXT("relic_hover");
		HoverProxy->Index = RelicIdx;
		HoverProxy->Owner = this;
		IconBtn->OnHovered.AddDynamic(HoverProxy, &UClickProxy::HandleHovered);
		IconBtn->OnUnhovered.AddDynamic(HoverProxy, &UClickProxy::HandleUnhovered);
		PendingScreenProxies.Add(HoverProxy);

		RelicIdx++;
	}

	USpacer* FillSpacer = NewObject<USpacer>(Sidebar);
	UVerticalBoxSlot* FillSlot = Sidebar->AddChildToVerticalBox(FillSpacer);
	FSlateChildSize FillSize(ESlateSizeRule::Fill);
	FillSlot->SetSize(FillSize);
}

void AAscendPlayerController::ShowRelicTooltipByIndex(int32 Index)
{
	if (!Run || !AnimCanvas) return;
	if (!Run->State.RelicIds.IsValidIndex(Index)) return;
	const FRelicData* RD = Run->GetRelicData(Run->State.RelicIds[Index]);
	if (!RD) return;
	ShowRelicTooltip(*RD, nullptr);
}

void AAscendPlayerController::ShowRelicTooltip(const FRelicData& Relic, UWidget* Anchor)
{
	HideRelicTooltip();

	if (!AnimCanvas) return;

	RelicTooltipWidget = NewObject<UBorder>(AnimCanvas);
	RelicTooltipWidget->SetBrushColor(FLinearColor(0.06f, 0.06f, 0.10f, 0.95f));
	RelicTooltipWidget->SetPadding(FMargin(10.f));

	UVerticalBox* TB = NewObject<UVerticalBox>(RelicTooltipWidget);

	FString RarityCN = TEXT("凡品");
	if (Relic.Rarity == TEXT("uncommon")) RarityCN = TEXT("中品");
	else if (Relic.Rarity == TEXT("rare")) RarityCN = TEXT("上品");
	else if (Relic.Rarity == TEXT("legendary")) RarityCN = TEXT("仙品");

	UTextBlock* NameLine = Style::MakeText(TB,
		FString::Printf(TEXT("【%s】(%s)"), *Relic.Name, *RarityCN), 18, RarityColor(Relic.Rarity));
	TB->AddChildToVerticalBox(NameLine)->SetPadding(FMargin(0.f, 0.f, 0.f, 4.f));

	UTextBlock* Desc = Style::MakeText(TB, Relic.Description, 14, Style::PaperWhite());
	Desc->SetAutoWrapText(true);
	TB->AddChildToVerticalBox(Desc);

	RelicTooltipWidget->SetContent(TB);
	RelicTooltipWidget->SetVisibility(ESlateVisibility::SelfHitTestInvisible);

	if (UCanvasPanelSlot* Slot = AnimCanvas->AddChildToCanvas(RelicTooltipWidget))
	{
		Slot->SetPosition(FVector2D(90.f, 60.f));
		Slot->SetAutoSize(true);
		Slot->SetZOrder(9999);
	}
}

void AAscendPlayerController::HideRelicTooltip()
{
	if (RelicTooltipWidget)
	{
		RelicTooltipWidget->RemoveFromParent();
		RelicTooltipWidget = nullptr;
	}
}
