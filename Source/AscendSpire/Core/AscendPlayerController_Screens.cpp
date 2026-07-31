#include "AscendPlayerController.h"
#include "UI/ClickProxy.h"
#include "UI/AscendUIStyle.h"
#include "UI/AscendRootWidget.h"
#include "UI/AscendArt.h"
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



	UWidget* MakeHPBar(UObject* Outer, int32 HP, int32 MaxHP, FLinearColor Color)
	{
		UProgressBar* Bar = NewObject<UProgressBar>(Outer);
		Bar->SetPercent(MaxHP > 0 ? (float)HP / MaxHP : 0.f);
		Bar->SetFillColorAndOpacity(Color);
		return Bar;
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
	AddToHBox(BtnRow, MakeLinkedButton(BtnRow, TEXT("【 新的征程 】"), TEXT("title_new"), 0, 26));
	if (URunManager::HasSaveFile())
	{
		AddToHBox(BtnRow, MakeLinkedButton(BtnRow, TEXT("【 继续修行 】"), TEXT("title_continue"), 0, 26));
	}
	BtnRow->AddChildToHorizontalBox(NewObject<USpacer>(Box))->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	AddToVBox(Box, BtnRow);

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
		CardBox->AddChild(RelicFace);
		Pad(CardBox, 6);

		UButton* Btn = Style::MakeStyledButton(CardBox, TEXT("【选择】"), 16, RarityColor(RD->Rarity),
			FLinearColor(0.20f, 0.16f, 0.10f));
		UClickProxy* CP = NewObject<UClickProxy>(Btn);
		CP->Tag = TEXT("start_relic");
		CP->Index = i;
		CP->Owner = this;
		Btn->OnClicked.AddDynamic(CP, &UClickProxy::HandleClick);
		Proxies.Add(CP);
		AddToVBox(CardBox, Btn, FMargin(0.f));

		USizeBox* CardSize = NewObject<USizeBox>(Box);
		CardSize->SetWidthOverride(200.f);
		CardSize->SetContent(CardBox);
		AddToHBox(Row, CardSize, FMargin(16.f, 0.f));
	}

	Row->AddChildToHorizontalBox(NewObject<USpacer>(Box))->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	AddToVBox(Box, Row);

	SetScreen(Box, EGameScreen::Title);
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

	// ---- 顶栏：回合数（左） + 日志折叠按钮（右） ----
	UHorizontalBox* TopRow = NewObject<UHorizontalBox>(RightBox);
	AddToHBox(TopRow, Style::MakeText(TopRow,
		FString::Printf(TEXT("第 %d 回合"), Combat->TurnCount), 15, Style::GoldYellow()));
	TopRow->AddChildToHorizontalBox(NewObject<USpacer>(RightBox))->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	const FString LogLabel = bLogExpanded ? FString(TEXT("日志 ▼")) : FString::Printf(TEXT("日志 ▶ %d"), CombatLogLines.Num());
	AddToHBox(TopRow, MakeLinkedButton(TopRow, LogLabel, TEXT("toggle_log"), 0, 12));
	AddToVBox(RightBox, TopRow, FMargin(0.f, 0.f));

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
			Proxies.Add(Proxy);
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
		UVerticalBoxSlot* LogSlot = RightBox->AddChildToVerticalBox(LogBorder);
		LogSlot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
		LogSlot->SetPadding(FMargin(40.f, 2.f));
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

	// ---- 丹药 + 结束回合 ----
	UHorizontalBox* ActionRow = NewObject<UHorizontalBox>(RightBox);
	ActionRow->AddChildToHorizontalBox(NewObject<USpacer>(RightBox))->SetSize(FSlateChildSize(ESlateSizeRule::Fill));

	for (int32 i = 0; i < Run->State.PillIds.Num(); ++i)
	{
		AddToHBox(ActionRow, MakeLinkedButton(ActionRow,
			FString::Printf(TEXT("丹:%s"), *Run->State.PillIds[i]), TEXT("pill"), i, 14));
	}

	AddToHBox(ActionRow, MakeLinkedButton(ActionRow, TEXT("【结束回合】"), TEXT("endturn"), 0, 20));
	ActionRow->AddChildToHorizontalBox(NewObject<USpacer>(RightBox))->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	AddToVBox(RightBox, ActionRow);

	// ---- Fill spacer pushes bottom area to bottom ----
	{
		USpacer* FS = NewObject<USpacer>(RightBox);
		UVerticalBoxSlot* FSlot = RightBox->AddChildToVerticalBox(FS);
		FSlateChildSize FillSize(ESlateSizeRule::Fill);
		FillSize.Value = 1.f;
		FSlot->SetSize(FillSize);
		FSlot->SetPadding(FMargin(0.f));
	}

	// ---- 底部栏：左 头像+气血 | 右 牌堆按钮 ----
	UHorizontalBox* BottomRow = NewObject<UHorizontalBox>(RightBox);

	// 左下：主角头像 + 气血
	{
		USizeBox* HPCluster = NewObject<USizeBox>(BottomRow);
		HPCluster->SetWidthOverride(400.f);
		UHorizontalBox* HPBox = NewObject<UHorizontalBox>(HPCluster);
		{
			if (UImage* AvatarImg = FAscendArt::MakeImage(HPBox, TEXT("Art/avatar/sword_cultivator.png")))
			{
				UBorder* AvatarFrame = NewObject<UBorder>(HPBox);
				AvatarFrame->SetBrushColor(FLinearColor(0.45f, 0.35f, 0.16f));
				AvatarFrame->SetPadding(FMargin(3.f));
				USizeBox* AvatarSize = NewObject<USizeBox>(AvatarFrame);
				AvatarSize->SetWidthOverride(96.f);
				AvatarSize->SetHeightOverride(96.f);
				UScaleBox* AvatarScale = NewObject<UScaleBox>(AvatarSize);
				AvatarScale->SetStretch(EStretch::ScaleToFill);
				AvatarScale->SetContent(AvatarImg);
				AvatarSize->SetContent(AvatarScale);
				AvatarFrame->SetContent(AvatarSize);
				HPBox->AddChildToHorizontalBox(AvatarFrame)->SetPadding(FMargin(0.f, 0.f, 8.f, 0.f));
			}

			UVerticalBox* HPVBox = NewObject<UVerticalBox>(HPBox);
			{
				UHorizontalBox* HPTopRow = NewObject<UHorizontalBox>(HPVBox);
				UBorder* MingBadge = NewObject<UBorder>(HPTopRow);
				MingBadge->SetBrushColor(FLinearColor(0.55f, 0.15f, 0.12f));
				MingBadge->SetPadding(FMargin(8.f, 2.f));
				UTextBlock* MingT = Style::MakeText(MingBadge, TEXT("命"), 20, Style::PaperWhite());
				MingBadge->SetContent(MingT);
				HPTopRow->AddChildToHorizontalBox(MingBadge);

				UTextBlock* HPT = Style::MakeText(HPTopRow,
					FString::Printf(TEXT("%d/%d"), Combat->Player.HP, Combat->Player.MaxHP), 18, Style::PaperWhite());
				HPTopRow->AddChildToHorizontalBox(HPT)->SetPadding(FMargin(10.f, 2.f, 0.f, 0.f));

				HPVBox->AddChildToVerticalBox(HPTopRow)->SetPadding(FMargin(0.f, 4.f, 0.f, 0.f));

				USizeBox* HPBarSize = NewObject<USizeBox>(HPVBox);
				HPBarSize->SetWidthOverride(240.f);
				HPBarSize->SetHeightOverride(24.f);
				UBorder* HPBg = NewObject<UBorder>(HPBarSize);
				HPBg->SetBrushColor(FLinearColor(0.16f, 0.10f, 0.10f));
				UProgressBar* PHPBar = NewObject<UProgressBar>(HPBg);
				PHPBar->SetPercent(Combat->Player.MaxHP > 0 ? (float)Combat->Player.HP / Combat->Player.MaxHP : 0.f);
				PHPBar->SetFillColorAndOpacity(FLinearColor(0.68f, 0.20f, 0.16f));
				HPBg->SetContent(PHPBar);
				HPBarSize->SetContent(HPBg);
				HPVBox->AddChildToVerticalBox(HPBarSize)->SetPadding(FMargin(8.f, 4.f, 0.f, 0.f));
			}
			HPBox->AddChildToHorizontalBox(HPVBox);
		}
		HPCluster->SetContent(HPBox);
		AddToHBox(BottomRow, HPCluster, FMargin(0.f, 0.f, 12.f, 0.f));
	}

	// Spacer 撑开，让牌堆在右
	BottomRow->AddChildToHorizontalBox(NewObject<USpacer>(BottomRow))->SetSize(FSlateChildSize(ESlateSizeRule::Fill));

	// 牌堆/弃牌堆（右下，以堆叠卡片展示）
	{
		USizeBox* PileCluster = NewObject<USizeBox>(BottomRow);
		PileCluster->SetWidthOverride(160.f);
		UHorizontalBox* PileRow = NewObject<UHorizontalBox>(PileCluster);

		const float PileCardW = 60.f;
		const float PileCardH = 84.f;
		const float StackOffset = 2.f;
		const int32 MaxStackCards = 5;

		// --- 牌堆（背面朝上堆叠） ---
		{
			UButton* DrawBtn = NewObject<UButton>(PileRow);
			DrawBtn->SetBackgroundColor(FLinearColor::Transparent);
			UOverlay* DrawOvl = NewObject<UOverlay>(DrawBtn);

			const int32 N = FMath::Min(Combat->DrawPile.Num(), MaxStackCards);
			for (int32 si = 0; si < N; ++si)
			{
				UBorder* CardBack = NewObject<UBorder>(DrawOvl);
				CardBack->SetBrushColor(FLinearColor(0.25f, 0.15f, 0.10f));
				CardBack->SetPadding(FMargin(2.f));
				FString ShortN = TEXT("牌");
				UTextBlock* CT = Style::MakeText(CardBack, ShortN, 10, Style::DimGray());
				CT->SetJustification(ETextJustify::Center);
				CardBack->SetContent(CT);
				USizeBox* CSize = NewObject<USizeBox>(CardBack);
				CSize->SetWidthOverride(PileCardW);
				CSize->SetHeightOverride(PileCardH);
				CardBack->SetContent(CSize);

				UOverlaySlot* OSlot = DrawOvl->AddChildToOverlay(CardBack);
				OSlot->SetPadding(FMargin(StackOffset * si, -(StackOffset * si)));
				OSlot->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Left);
				OSlot->SetVerticalAlignment(EVerticalAlignment::VAlign_Top);
			}

			// 数量文字
			UTextBlock* CountT = Style::MakeText(DrawOvl,
				FString::Printf(TEXT("%d"), Combat->DrawPile.Num()), 14, Style::GoldYellow());
			CountT->SetJustification(ETextJustify::Right);
			UOverlaySlot* CSlot = DrawOvl->AddChildToOverlay(CountT);
			CSlot->SetPadding(FMargin(0.f, 0.f, 4.f, 2.f));
			CSlot->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Right);
			CSlot->SetVerticalAlignment(EVerticalAlignment::VAlign_Bottom);

			DrawBtn->SetContent(DrawOvl);

			UClickProxy* DP = NewObject<UClickProxy>(DrawBtn);
			DP->Tag = TEXT("pile_draw");
			DP->Index = 0;
			DP->Owner = this;
			DrawBtn->OnClicked.AddDynamic(DP, &UClickProxy::HandleClick);
			Proxies.Add(DP);

			PileRow->AddChildToHorizontalBox(DrawBtn)->SetPadding(FMargin(0.f, 0.f, 8.f, 0.f));
		}

		// --- 弃牌堆（正面朝上堆叠，顶牌可见） ---
		{
			UButton* DiscardBtn = NewObject<UButton>(PileRow);
			DiscardBtn->SetBackgroundColor(FLinearColor::Transparent);
			UOverlay* DiscardOvl = NewObject<UOverlay>(DiscardBtn);

			const int32 N = Combat->DiscardPile.Num();
			const int32 ShowN = FMath::Min(N, MaxStackCards);
			for (int32 si = 0; si < ShowN; ++si)
			{
				// 取倒数第 si 张（从顶到底）
				const int32 CardIdx = N - 1 - si;
				if (!Combat->DiscardPile.IsValidIndex(CardIdx)) break;
				const FCardData* CD = Run->GetCardData(Combat->DiscardPile[CardIdx].Data.Id);
				if (!CD) continue;

				UBorder* CardFace = NewObject<UBorder>(DiscardOvl);
				CardFace->SetBrushColor(FAscendUIStyle::CardTypeColor(CD->Type) * 0.5f + FLinearColor(0.15f, 0.12f, 0.10f) * 0.5f);
				CardFace->SetPadding(FMargin(2.f));

				UVerticalBox* CVBox = NewObject<UVerticalBox>(CardFace);
				UTextBlock* CN = Style::MakeText(CVBox, CD->Name, 9, Style::PaperWhite());
				CN->SetJustification(ETextJustify::Center);
				CVBox->AddChildToVerticalBox(CN)->SetPadding(FMargin(1.f, 0.f));
				UTextBlock* CCost = Style::MakeText(CVBox,
					FString::FromInt(CD->Cost), 10, Style::SpiritBlue());
				CCost->SetJustification(ETextJustify::Center);
				CVBox->AddChildToVerticalBox(CCost);

				CardFace->SetContent(CVBox);
				USizeBox* CSize = NewObject<USizeBox>(CardFace);
				CSize->SetWidthOverride(PileCardW);
				CSize->SetHeightOverride(PileCardH);
				CardFace->SetContent(CSize);

				UOverlaySlot* OSlot = DiscardOvl->AddChildToOverlay(CardFace);
				OSlot->SetPadding(FMargin(StackOffset * si, -(StackOffset * si)));
				OSlot->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Left);
				OSlot->SetVerticalAlignment(EVerticalAlignment::VAlign_Top);
			}

			UTextBlock* CountT = Style::MakeText(DiscardOvl,
				FString::Printf(TEXT("%d"), N), 14, Style::PaperWhite());
			CountT->SetJustification(ETextJustify::Right);
			UOverlaySlot* CSlot = DiscardOvl->AddChildToOverlay(CountT);
			CSlot->SetPadding(FMargin(0.f, 0.f, 4.f, 2.f));
			CSlot->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Right);
			CSlot->SetVerticalAlignment(EVerticalAlignment::VAlign_Bottom);

			DiscardBtn->SetContent(DiscardOvl);

			UClickProxy* DP = NewObject<UClickProxy>(DiscardBtn);
			DP->Tag = TEXT("pile_discard");
			DP->Index = 0;
			DP->Owner = this;
			DiscardBtn->OnClicked.AddDynamic(DP, &UClickProxy::HandleClick);
			Proxies.Add(DP);

			PileRow->AddChildToHorizontalBox(DiscardBtn);
		}

		PileCluster->SetContent(PileRow);
		AddToHBox(BottomRow, PileCluster, FMargin(6.f, 0.f, 2.f, 0.f));
	}

	AddToVBox(RightBox, BottomRow);

	// 右侧面板接入主布局
	MainHBox->AddChildToHorizontalBox(RightBox)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));

	// ---- 回合中抽卡动画（牌效果触发）：每抽一张播一次 ----
	if (Combat->PendingDrawCount > 0 && Combat->TurnCount == LastSeenTurnCount)
	{
		PlayHandEntranceAnimation(Combat->PendingDrawCount);
		Combat->PendingDrawCount = 0;
	}

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
		FIntPoint VPS(1280, 720);
		if (GetWorld() && GetWorld()->GetGameViewport())
			VPS = GetWorld()->GetGameViewport()->Viewport->GetSizeXY();
		const float Vw = VPS.X, Vh = VPS.Y;

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
				SSlot->SetPosition(FVector2D(0.f, Vh * 1.5f - Vh * 0.35f - 194.f - 80.f - 44.f));
				SSlot->SetSize(FVector2D(Vw, 44.f));
				SSlot->SetZOrder(101);
			}
			StatusH = 44.f;
		}

		UHorizontalBox* SpiritRow = NewObject<UHorizontalBox>(InfoLayer);
		SpiritRow->AddChildToHorizontalBox(NewObject<USpacer>(InfoLayer))->SetSize(FSlateChildSize(ESlateSizeRule::Fill));

		UBorder* BlockBadge = NewObject<UBorder>(SpiritRow);
		BlockBadge->SetBrushColor(FLinearColor(0.20f, 0.38f, 0.58f));
		BlockBadge->SetPadding(FMargin(16.f, 4.f));
		UTextBlock* BlockT = Style::MakeText(BlockBadge,
			FString::Printf(TEXT("罡气 %d"), Combat->Player.Block), 32, Style::PaperWhite());
		BlockBadge->SetContent(BlockT);
		UHorizontalBoxSlot* BSlot = SpiritRow->AddChildToHorizontalBox(BlockBadge);
		BSlot->SetPadding(FMargin(0.f, 0.f, 24.f, 0.f));

		FString Gems;
		for (int32 gi = 0; gi < Combat->MaxSpirit; ++gi)
			Gems += (gi < Combat->Spirit) ? TEXT("◆") : TEXT("◇");
		if (Combat->Spirit > Combat->MaxSpirit)
			Gems += FString::Printf(TEXT(" +%d"), Combat->Spirit - Combat->MaxSpirit);
		UTextBlock* GemT = Style::MakeText(SpiritRow, Gems, 40, Style::SpiritBlue());
		UHorizontalBoxSlot* GSlot = SpiritRow->AddChildToHorizontalBox(GemT);
		GSlot->SetPadding(FMargin(0.f, 0.f, 24.f, 0.f));

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
		SpiritWrap->SetHeightOverride(50.f);
		SpiritWrap->SetContent(SpiritRow);

		if (UCanvasPanelSlot* ISlot = InfoLayer->AddChildToCanvas(SpiritWrap))
		{
			ISlot->SetPosition(FVector2D(0.f, Vh * 1.5f - Vh * 0.35f - 194.f - 80.f));
			ISlot->SetSize(FVector2D(Vw, 50.f));
			ISlot->SetZOrder(100);
		}
	}

	// ---- 手牌：叠加在屏幕底部（根据视口尺寸计算坐标） ----
	HandCardButtons.Empty();
	{
		FIntPoint ViewportSize(1280, 720);
		if (GetWorld() && GetWorld()->GetGameViewport())
			ViewportSize = GetWorld()->GetGameViewport()->Viewport->GetSizeXY();
		const float ViewW = ViewportSize.X;
		const float ViewH = ViewportSize.Y;

		UCanvasPanel* HandLayer = NewObject<UCanvasPanel>(ScreenOvl);
		UOverlaySlot* HandLayerSlot = ScreenOvl->AddChildToOverlay(HandLayer);
		HandLayerSlot->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Fill);
		HandLayerSlot->SetVerticalAlignment(EVerticalAlignment::VAlign_Fill);

		const int32 N = Combat->Hand.Num();
		const float FanAngle = 40.f;
		const float FanRadius = ViewH * 0.35f;
		const float CardW = 162.f;
		const float CardH = 194.f;
		const float FanCenterX = ViewW * 0.6f;
		const float FanCenterY = ViewH * 1.5f;

		for (int32 i = 0; i < N; ++i)
		{
			const bool bPlayable = Combat->GetEffectiveCost(Combat->Hand[i]) <= Combat->Spirit;

			UButton* CardBtn = NewObject<UButton>(HandLayer);
			BuildCardWidget(CardBtn, i, bPlayable);
			if (!bPlayable) CardBtn->SetIsEnabled(false);

			HandCardButtons.SetNum(FMath::Max(HandCardButtons.Num(), i + 1));
			HandCardButtons[i] = CardBtn;

			UClickProxy* Proxy = NewObject<UClickProxy>(CardBtn);
			Proxy->Tag = TEXT("hand_card");
			Proxy->Index = i;
			Proxy->Owner = this;
			CardBtn->OnPressed.AddDynamic(Proxy, &UClickProxy::HandlePress);
			Proxies.Add(Proxy);

			UClickProxy* HoverProxy = NewObject<UClickProxy>(CardBtn);
			HoverProxy->Tag = TEXT("hand_hover");
			HoverProxy->Index = i;
			HoverProxy->Owner = this;
			HoverProxy->BoundButton = CardBtn;
			CardBtn->OnHovered.AddDynamic(HoverProxy, &UClickProxy::HandleHovered);
			CardBtn->OnUnhovered.AddDynamic(HoverProxy, &UClickProxy::HandleUnhovered);
			Proxies.Add(HoverProxy);

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
				CardBtn->SetRenderTransformPivot(FVector2D(0.5f, 1.0f));
				CardBtn->SetRenderTransformAngle(AngleDeg);
			}
		}
	}

	SetScreen(ScreenOvl, EGameScreen::Combat);

	// ---- 动画判定：回合切换或回合内抽牌 ----
	const bool bTurnStart = Combat->TurnCount != LastSeenTurnCount;
	const bool bInTurnDraw = Combat->PendingDrawCount > 0 && !bTurnStart;

	if (bTurnStart || bInTurnDraw)
	{
		// 设初始动画状态：从牌堆位置缩小透明飞入（在 SetScreen 之后立即设置，首帧已渲染但用户不可见）
		const FVector2D PileA(GetViewportSize().X - 100.f, GetViewportSize().Y - 200.f);
		for (int32 si = 0; si < HandCardButtons.Num(); ++si)
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

	UTextBlock* PickHint = Style::MakeText(Box, TEXT("—— 选择一张卡牌加入卡组 ——"), 18, Style::JadeGreen());
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
			CardBox->AddChild(MakeCardContentFromData(CardBox, *C, DC.bUpgraded, 0.9f));
			Pad(CardBox, 4);
			UButton* Btn = MakeLinkedButton(CardBox, TEXT("【选择】"), TEXT("reward"), i, 16);
			AddToVBox(CardBox, Btn, FMargin(0.f));
			AddToHBox(Row, CardBox, FMargin(12.f, 0.f));
		}
	}
	Row->AddChildToHorizontalBox(NewObject<USpacer>(Box))->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	AddToVBox(Box, Row);
	Pad(Box, 20);

	UHorizontalBox* SkipRow = NewObject<UHorizontalBox>(Box);
	SkipRow->AddChildToHorizontalBox(NewObject<USpacer>(Box))->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	AddToHBox(SkipRow, MakeLinkedButton(SkipRow, TEXT("【跳过】"), TEXT("reward_skip"), 0, 18));
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

	USizeBox* CardSizer = NewObject<USizeBox>(Outer);
	CardSizer->SetWidthOverride(160.f * S);
	CardSizer->SetHeightOverride(220.f * S);

	const FLinearColor RCol = RarityColor(RelicData.Rarity);

	UBorder* Frame = NewObject<UBorder>(CardSizer);
	Frame->SetBrushColor(RCol * 0.4f + FLinearColor(0.08f, 0.07f, 0.05f) * 0.6f);
	Frame->SetPadding(FMargin(3.f * S));

	UBorder* Face = NewObject<UBorder>(Frame);
	Face->SetBrushColor(FLinearColor(0.28f, 0.24f, 0.20f));

	UVerticalBox* VBox = NewObject<UVerticalBox>(Face);

	UBorder* IconArea = NewObject<UBorder>(VBox);
	IconArea->SetBrushColor(RCol * 0.3f + FLinearColor(0.13f, 0.11f, 0.09f) * 0.7f);
	UVerticalBoxSlot* IconSlot = VBox->AddChildToVerticalBox(IconArea);
	FSlateChildSize FillAll(ESlateSizeRule::Fill);
	FillAll.Value = 1.f;
	IconSlot->SetSize(FillAll);

	FString Short2 = RelicData.Name.Len() >= 2 ? RelicData.Name.Mid(0, 2)
		: (RelicData.Name.IsEmpty() ? TEXT("?") : RelicData.Name.Left(1));
	UTextBlock* IconTxt = Style::MakeText(IconArea, Short2, 32.f * S, Style::PaperWhite());
	IconTxt->SetJustification(ETextJustify::Center);
	IconArea->SetContent(IconTxt);

	FString RarityCN = TEXT("凡品");
	if (RelicData.Rarity == TEXT("uncommon")) RarityCN = TEXT("中品");
	else if (RelicData.Rarity == TEXT("rare")) RarityCN = TEXT("上品");
	else if (RelicData.Rarity == TEXT("legendary")) RarityCN = TEXT("仙品");

	UBorder* RarityBar = NewObject<UBorder>(VBox);
	RarityBar->SetBrushColor(RCol * 0.6f);
	UTextBlock* RarityT = Style::MakeText(RarityBar, RarityCN, 10.f * S, Style::PaperWhite());
	RarityT->SetJustification(ETextJustify::Center);
	RarityBar->SetContent(RarityT);
	VBox->AddChildToVerticalBox(RarityBar)->SetPadding(FMargin(0.f));

	UTextBlock* NameT = Style::MakeText(VBox, RelicData.Name, 14.f * S, RCol);
	NameT->SetJustification(ETextJustify::Center);
	VBox->AddChildToVerticalBox(NameT)->SetPadding(FMargin(4.f * S, 3.f * S));

	UTextBlock* DescT = Style::MakeText(VBox, RelicData.Description, 11.f * S, Style::PaperWhite());
	DescT->SetAutoWrapText(true);
	DescT->SetJustification(ETextJustify::Center);
	VBox->AddChildToVerticalBox(DescT)->SetPadding(FMargin(4.f * S, 1.f * S, 4.f * S, 4.f * S));

	Face->SetContent(VBox);
	Frame->SetContent(Face);
	CardSizer->SetContent(Frame);
	return CardSizer;
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
	Pad(Box, 60);

	// 叙述文本（居中，自动换行）
	UTextBlock* Narrator = Style::MakeText(Box, Beat.NarratorText, 20, Style::PaperWhite());
	Narrator->SetJustification(ETextJustify::Center);
	Narrator->SetAutoWrapText(true);
	AddToVBox(Box, Narrator, FMargin(80.f, 20.f));
	Pad(Box, 30);

	// 选项
	for (int32 i = 0; i < Beat.Choices.Num(); ++i)
	{
		UHorizontalBox* Row = NewObject<UHorizontalBox>(Box);
		Row->AddChildToHorizontalBox(NewObject<USpacer>(Box))->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		AddToHBox(Row, MakeLinkedButton(Row,
			FString::Printf(TEXT("【%s】"), *Beat.Choices[i].Text), TEXT("node"), i, 20));
		Row->AddChildToHorizontalBox(NewObject<USpacer>(Box))->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		AddToVBox(Box, Row, FMargin(0.f, 8.f));
	}

	SetScreen(Box, EGameScreen::Event);
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

	UTextBlock* Summary = Style::MakeText(Box, Run->PendingNarrativeSummary, 20, Style::PaperWhite());
	Summary->SetJustification(ETextJustify::Center);
	Summary->SetAutoWrapText(true);
	AddToVBox(Box, Summary, FMargin(80.f, 20.f));
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

	UTextBlock* T = Style::MakeText(Box, TEXT("坊 市"), 32, Style::GoldYellow());
	T->SetJustification(ETextJustify::Center);
	AddToVBox(Box, T);

	UTextBlock* Gold = Style::MakeText(Box,
		FString::Printf(TEXT("灵石: %d"), Run->State.Gold), 20, Style::GoldYellow());
	Gold->SetJustification(ETextJustify::Center);
	AddToVBox(Box, Gold);
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
			Proxies.Add(CP);
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
	Pad(Box, 30);

	UHorizontalBox* Row = NewObject<UHorizontalBox>(Box);
	Row->AddChildToHorizontalBox(NewObject<USpacer>(Box))->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	AddToHBox(Row, MakeLinkedButton(Row, TEXT("【调息疗伤及恢复55%气血】"), TEXT("rest_heal"), 0, 20));
	AddToHBox(Row, MakeLinkedButton(Row, TEXT("【悟道修炼·随机升级一张牌】"), TEXT("rest_upgrade"), 0, 20));
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
	UVerticalBox* Sidebar = NewObject<UVerticalBox>(HBox);
	USizeBox* SidebarSize = NewObject<USizeBox>(HBox);
	SidebarSize->SetWidthOverride(80.f);
	SidebarSize->SetContent(Sidebar);
	HBox->AddChildToHorizontalBox(SidebarSize)->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));

	if (!Run) return;

	int32 RelicIdx = 0;
	for (const FString& Rid : Run->State.RelicIds)
	{
		const FRelicData* RD = Run->GetRelicData(Rid);
		if (!RD) { RelicIdx++; continue; }

		FString Short2 = RD->Name.Len() >= 2 ? RD->Name.Mid(0, 2) : (RD->Name.IsEmpty() ? TEXT("?") : RD->Name.Left(1));

		FLinearColor RCol = RarityColor(RD->Rarity);
		FLinearColor IconBg = RCol * 0.3f + FLinearColor(0.08f, 0.08f, 0.10f) * 0.7f;

		UButton* IconBtn = NewObject<UButton>(Sidebar);
		IconBtn->SetBackgroundColor(IconBg);

		UTextBlock* Txt = Style::MakeText(IconBtn, Short2, 18, Style::PaperWhite());
		Txt->SetJustification(ETextJustify::Center);
		IconBtn->SetContent(Txt);

		UVerticalBoxSlot* Slot = Sidebar->AddChildToVerticalBox(IconBtn);
		Slot->SetPadding(FMargin(0.f, 2.f));
		Slot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));

		// 悬停工具提示
		UClickProxy* HoverProxy = NewObject<UClickProxy>(IconBtn);
		HoverProxy->Tag = TEXT("relic_hover");
		HoverProxy->Index = RelicIdx;
		HoverProxy->Owner = this;
		IconBtn->OnHovered.AddDynamic(HoverProxy, &UClickProxy::HandleHovered);
		IconBtn->OnUnhovered.AddDynamic(HoverProxy, &UClickProxy::HandleUnhovered);
		Proxies.Add(HoverProxy);

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
