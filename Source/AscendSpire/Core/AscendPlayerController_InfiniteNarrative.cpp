#include "AscendPlayerController.h"

#include "UI/AscendRootWidget.h"
#include "UI/AscendUIStyle.h"
#include "UI/AscendArt.h"
#include "UI/ClickProxy.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CheckBox.h"
#include "Components/ComboBoxString.h"
#include "Components/EditableTextBox.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/MultiLineEditableTextBox.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/ScaleBox.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/Spacer.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/Texture2D.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Styling/SlateBrush.h"
#include "Styling/SlateTypes.h"
#include "TimerManager.h"

namespace
{
	const TCHAR* NarrativeConfigSection = TEXT("AscendSpire.InfiniteNarrative");

	FString GetNarrativeUserConfigPath()
	{
		// GameUserSettings.ini is owned and rewritten by UGameUserSettings, which drops
		// unrelated custom sections in packaged builds. Keep AI credentials and RP
		// preferences in a dedicated per-user file instead.
		return FPaths::ProjectSavedDir() / TEXT("Config/InfiniteNarrative.ini");
	}

	FString DescribeChoiceDestination(const FInfiniteNarrativeChoice& Choice)
	{
		TArray<FString> Destinations;
		if (Choice.CardForgeJobs.Num() > 0 || Choice.Next.Equals(TEXT("card_forge"), ESearchCase::IgnoreCase))
			Destinations.Add(TEXT("进入原创卡锻造"));
		for (const FInfiniteGameOperation& Operation : Choice.Operations)
		{
			if (Operation.Op == TEXT("choose_upgrade_card"))
				Destinations.Add(FString::Printf(TEXT("选择升级%d张卡牌"), FMath::Max(1, Operation.Count)));
			else if (Operation.Op == TEXT("choose_remove_card"))
				Destinations.Add(FString::Printf(TEXT("选择剔除%d张卡牌"), FMath::Max(1, Operation.Count)));
			else if (Operation.Op == TEXT("open_shop"))
			{
				const bool bLegendary = Operation.Rarity.Equals(TEXT("legendary"), ESearchCase::IgnoreCase);
				const int32 PricePercent = FMath::RoundToInt(FMath::Max(0.5f, Operation.PriceMultiplier) * 100.f);
				Destinations.Add(bLegendary
					? FString::Printf(TEXT("进入仙品坊市（售价%d%%）"), PricePercent)
					: TEXT("进入坊市"));
			}
			else if (Operation.Op == TEXT("open_reward")) Destinations.Add(TEXT("进入三选一奖励"));
			else if (Operation.Op == TEXT("open_rest")) Destinations.Add(TEXT("进入休息界面"));
		}
		if (Choice.Next.Equals(TEXT("combat"), ESearchCase::IgnoreCase))
		{
			const FString EnemyName = Choice.Enemy.Name.IsEmpty() ? TEXT("剧情敌人") : Choice.Enemy.Name;
			Destinations.Add(TEXT("遭遇【") + EnemyName + TEXT("】"));
		}
		return FString::Join(Destinations, TEXT(" → "));
	}

	FString ChoiceDestinationButtonLabel(const FInfiniteNarrativeChoice& Choice)
	{
		if (Choice.CardForgeJobs.Num() > 0 || Choice.Next.Equals(TEXT("card_forge"), ESearchCase::IgnoreCase))
			return TEXT("【锻造原创卡】");
		for (const FInfiniteGameOperation& Operation : Choice.Operations)
		{
			if (Operation.Op == TEXT("choose_upgrade_card")) return TEXT("【前往卡牌升级】");
			if (Operation.Op == TEXT("choose_remove_card")) return TEXT("【前往卡牌剔除】");
			if (Operation.Op == TEXT("open_shop")) return TEXT("【进入坊市】");
			if (Operation.Op == TEXT("open_reward")) return TEXT("【查看奖励】");
			if (Operation.Op == TEXT("open_rest")) return TEXT("【进入休息】");
		}
		return Choice.Next.Equals(TEXT("combat"), ESearchCase::IgnoreCase)
			? TEXT("【迎战】") : TEXT("【继续剧情】");
	}

	struct FRPPortraitDefinition
	{
		FString Id;
		FString Name;
		FString Art;
		FLinearColor Color = FLinearColor(0.28f, 0.35f, 0.40f, 1.f);
	};

	FString LoadRPDataFile(const TCHAR* RelativePath)
	{
		FString Content;
		FFileHelper::LoadFileToString(Content, *(FPaths::ProjectContentDir() / RelativePath));
		return Content;
	}

	FRPPortraitDefinition ResolveRPPortrait(const FString& PortraitId, const FString& Speaker,
		const FString& Expression, const FString& RegistryOverride)
	{
		FRPPortraitDefinition Result;
		Result.Id = PortraitId;
		Result.Name = Speaker.IsEmpty() ? TEXT("旁白") : Speaker;
		const uint32 Hash = GetTypeHash(Result.Name);
		Result.Color = FLinearColor(0.22f + ((Hash >> 0) & 0xff) / 900.f,
			0.22f + ((Hash >> 8) & 0xff) / 900.f, 0.22f + ((Hash >> 16) & 0xff) / 900.f, 1.f);

		const FString Content = RegistryOverride.TrimStartAndEnd().IsEmpty()
			? LoadRPDataFile(TEXT("Data/rp_characters.json")) : RegistryOverride;
		if (Content.IsEmpty()) return Result;
		TSharedPtr<FJsonObject> Root;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Content);
		if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid()) return Result;
		const TArray<TSharedPtr<FJsonValue>>* Characters = nullptr;
		if (!Root->TryGetArrayField(TEXT("characters"), Characters)) return Result;
		for (const TSharedPtr<FJsonValue>& Value : *Characters)
		{
			const TSharedPtr<FJsonObject> Character = Value->AsObject();
			if (!Character.IsValid()) continue;
			FString Id;
			FString Name;
			Character->TryGetStringField(TEXT("id"), Id);
			Character->TryGetStringField(TEXT("name"), Name);
			bool bMatches = !PortraitId.IsEmpty() && Id == PortraitId;
			bMatches = bMatches || (!Speaker.IsEmpty() && Name == Speaker);
			const TArray<TSharedPtr<FJsonValue>>* Aliases = nullptr;
			if (!bMatches && !Speaker.IsEmpty() && Character->TryGetArrayField(TEXT("aliases"), Aliases))
			{
				for (const TSharedPtr<FJsonValue>& AliasValue : *Aliases)
				{
					FString Alias;
					if (AliasValue->TryGetString(Alias) && Alias == Speaker) { bMatches = true; break; }
				}
			}
			if (!bMatches) continue;
			Result.Id = Id;
			Result.Name = Name.IsEmpty() ? Result.Name : Name;
			Character->TryGetStringField(TEXT("art"), Result.Art);
			const TSharedPtr<FJsonObject>* Expressions = nullptr;
			if (!Expression.IsEmpty() && Character->TryGetObjectField(TEXT("expressions"), Expressions)
				&& Expressions && Expressions->IsValid())
			{
				FString ExpressionArt;
				if ((*Expressions)->TryGetStringField(Expression, ExpressionArt) && !ExpressionArt.IsEmpty())
				{
					Result.Art = ExpressionArt;
				}
			}
			FString Hex;
			if (Character->TryGetStringField(TEXT("color"), Hex))
			{
				const FColor Parsed = FColor::FromHex(Hex);
				Result.Color = FLinearColor(Parsed);
			}
			break;
		}
		return Result;
	}

	UWidget* RPMakePortrait(UObject* Outer, const FString& PortraitId, const FString& Speaker,
		const FString& Expression, const FString& RegistryOverride)
	{
		const FRPPortraitDefinition Definition = ResolveRPPortrait(PortraitId, Speaker, Expression, RegistryOverride);
		USizeBox* Size = NewObject<USizeBox>(Outer);
		Size->SetWidthOverride(184.f);
		Size->SetHeightOverride(184.f);
		UOverlay* Overlay = NewObject<UOverlay>(Size);
		// 旧版绿色占位底只在缺少人物素材时保留；已有头像时外围必须完全透明。
		if (Definition.Art.IsEmpty())
		{
			UBorder* Backdrop = NewObject<UBorder>(Overlay);
			Backdrop->SetBrushColor(Definition.Color * 0.32f + FLinearColor(0.025f, 0.035f, 0.04f, 1.f) * 0.68f);
			UOverlaySlot* BackdropSlot = Overlay->AddChildToOverlay(Backdrop);
			BackdropSlot->SetPadding(FMargin(36.f));
			BackdropSlot->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Fill);
			BackdropSlot->SetVerticalAlignment(EVerticalAlignment::VAlign_Fill);
		}
		if (!Definition.Art.IsEmpty())
		{
			if (UImage* Image = FAscendArt::MakeImage(Overlay, Definition.Art))
			{
				UScaleBox* Scale = NewObject<UScaleBox>(Overlay);
				Scale->SetStretch(EStretch::ScaleToFill);
				Scale->SetContent(Image);
				UOverlaySlot* ImageSlot = Overlay->AddChildToOverlay(Scale);
				ImageSlot->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Fill);
				ImageSlot->SetVerticalAlignment(EVerticalAlignment::VAlign_Fill);
				// 头像只填充玉石框的内窗，避免人物图从外框下方溢出。
				ImageSlot->SetPadding(FMargin(36.f));
			}
		}
		if (Definition.Art.IsEmpty())
		{
			UTextBlock* Initial = FAscendUIStyle::MakeText(Overlay, Definition.Name.Left(1), 30,
				FAscendUIStyle::PaperWhite());
			Initial->SetJustification(ETextJustify::Center);
			Initial->SetShadowOffset(FVector2D(1.f, 1.f));
			UOverlaySlot* InitialSlot = Overlay->AddChildToOverlay(Initial);
			InitialSlot->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Fill);
			InitialSlot->SetVerticalAlignment(EVerticalAlignment::VAlign_Center);
		}
		// 表情已经由对应差分图传达；只有缺少头像素材时才显示调试用表情文字。
		if (Definition.Art.IsEmpty() && !Expression.IsEmpty()
			&& !Expression.Equals(TEXT("neutral"), ESearchCase::IgnoreCase))
		{
			UTextBlock* Mood = FAscendUIStyle::MakeText(Overlay, Expression, 10, FAscendUIStyle::PaperWhite());
			Mood->SetJustification(ETextJustify::Center);
			UOverlaySlot* MoodSlot = Overlay->AddChildToOverlay(Mood);
			MoodSlot->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Fill);
			MoodSlot->SetVerticalAlignment(EVerticalAlignment::VAlign_Bottom);
			MoodSlot->SetPadding(FMargin(2.f));
		}
		if (UImage* Ornament = FAscendArt::MakeImage(Overlay, TEXT("Art/ui/avatar_frame.png")))
		{
			Ornament->SetVisibility(ESlateVisibility::HitTestInvisible);
			UOverlaySlot* OrnamentSlot = Overlay->AddChildToOverlay(Ornament);
			OrnamentSlot->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Fill);
			OrnamentSlot->SetVerticalAlignment(EVerticalAlignment::VAlign_Fill);
		}
		Size->SetContent(Overlay);
		return Size;
	}

	void RPAddToVBox(UVerticalBox* Box, UWidget* Widget, FMargin Padding = FMargin(6.f))
	{
		if (UVerticalBoxSlot* Slot = Box->AddChildToVerticalBox(Widget)) Slot->SetPadding(Padding);
	}

	void RPAddToHBox(UHorizontalBox* Box, UWidget* Widget, FMargin Padding = FMargin(6.f))
	{
		if (UHorizontalBoxSlot* Slot = Box->AddChildToHorizontalBox(Widget))
		{
			Slot->SetPadding(Padding);
			Slot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
		}
	}

	void RPAddPad(UVerticalBox* Box, float Height)
	{
		USpacer* Spacer = NewObject<USpacer>(Box);
		Spacer->SetSize(FVector2D(1.f, Height));
		Box->AddChildToVerticalBox(Spacer);
	}

	UTextBlock* RPMakeWrappedText(UObject* Outer, const FString& Text, int32 Size, FSlateColor Color,
		ETextJustify::Type Justification = ETextJustify::Center)
	{
		UTextBlock* Result = FAscendUIStyle::MakeText(Outer, Text, Size, Color);
		Result->SetAutoWrapText(true);
		Result->SetJustification(Justification);
		return Result;
	}

	UWidget* RPMakeDialogueBubble(UObject* Outer, const FInfiniteDialogueLine& Line,
		bool bShowPortrait, const FString& RegistryOverride, UTextBlock** OutBodyText = nullptr)
	{
		UHorizontalBox* Row = NewObject<UHorizontalBox>(Outer);
		Row->AddChildToHorizontalBox(NewObject<USpacer>(Row))->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		if (bShowPortrait)
		{
			RPAddToHBox(Row, RPMakePortrait(Row, Line.PortraitId, Line.Speaker,
				Line.Expression, RegistryOverride), FMargin(8.f, 22.f, 10.f, 4.f));
		}

		UVerticalBox* MessageColumn = NewObject<UVerticalBox>(Row);
		RPAddToVBox(MessageColumn, RPMakeWrappedText(MessageColumn,
			Line.Speaker.IsEmpty() ? TEXT("旁白") : Line.Speaker, 15,
			FAscendUIStyle::JadeGreen(), ETextJustify::Left), FMargin(5.f, 0.f, 5.f, 3.f));
		UBorder* Bubble = NewObject<UBorder>(MessageColumn);
		if (UTexture2D* BubbleTexture = FAscendArt::GetTexture(Bubble, TEXT("Art/ui/card_rules_panel_v2.png")))
		{
			FSlateBrush BubbleBrush;
			BubbleBrush.SetResourceObject(BubbleTexture);
			BubbleBrush.DrawAs = ESlateBrushDrawType::Box;
			// card_rules_panel_v2 has shallow corner ornaments designed for short
			// panels.  The previous chat texture's tall top/bottom slices overlapped
			// whenever a bubble was only one or two lines high.
			BubbleBrush.Margin = FMargin(0.07f, 0.12f);
			BubbleBrush.ImageSize = FVector2D(1525.f, 783.f);
			Bubble->SetBrush(BubbleBrush);
			Bubble->SetBrushColor(FLinearColor::White);
		}
		else
		{
			Bubble->SetBrushColor(FLinearColor(0.055f, 0.075f, 0.08f, 0.96f));
		}
		Bubble->SetClipping(EWidgetClipping::ClipToBounds);
		Bubble->SetPadding(FMargin(50.f, 18.f));
		USizeBox* BubbleSize = NewObject<USizeBox>(Bubble);
		// TextBlock 的真实 DesiredSize 决定气泡高度；不再猜测字符数后强制
		// HeightOverride。短句自然收缩，长句按 WrapTextAt 自动增高。
		BubbleSize->SetMinDesiredWidth(120.f);
		BubbleSize->SetMaxDesiredWidth(760.f);
		UTextBlock* BodyText = RPMakeWrappedText(BubbleSize, Line.Text, 19,
			FAscendUIStyle::PaperWhite(), ETextJustify::Left);
		// 显式 WrapTextAt 让 Slate 在 DesiredSize 阶段就计算真实多行高度，
		// 避免 AutoWrap 第一帧仍按单行高度把文字画到气泡外。
		BodyText->SetWrapTextAt(680.f);
		if (OutBodyText) *OutBodyText = BodyText;
		BubbleSize->SetContent(BodyText);
		Bubble->SetContent(BubbleSize);
		RPAddToVBox(MessageColumn, Bubble, FMargin(4.f));
		RPAddToHBox(Row, MessageColumn, FMargin(2.f, 4.f));
		Row->AddChildToHorizontalBox(NewObject<USpacer>(Row))->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		return Row;
	}

	void RPConfigureAdaptiveChoiceButton(UButton* Button, const FString& Label, int32 FontSize)
	{
		if (!Button) return;
		Button->SetClipping(EWidgetClipping::ClipToBounds);
		if (UTexture2D* Texture = FAscendArt::GetTexture(Button, TEXT("Art/ui/card_rules_panel_v2.png")))
		{
			auto MakeBrush = [Texture](const FLinearColor& Tint)
			{
				FSlateBrush Brush;
				Brush.SetResourceObject(Texture);
				Brush.DrawAs = ESlateBrushDrawType::Box;
				Brush.Margin = FMargin(0.07f, 0.12f);
				Brush.ImageSize = FVector2D(1525.f, 783.f);
				Brush.TintColor = FSlateColor(Tint);
				return Brush;
			};
			FButtonStyle Style;
			Style.SetNormal(MakeBrush(FLinearColor(0.83f, 0.88f, 0.84f, 0.96f)));
			Style.SetHovered(MakeBrush(FLinearColor(1.06f, 1.10f, 1.03f, 1.f)));
			Style.SetPressed(MakeBrush(FLinearColor(0.64f, 0.72f, 0.67f, 0.98f)));
			Style.SetDisabled(MakeBrush(FLinearColor(0.32f, 0.34f, 0.34f, 0.58f)));
			Style.SetNormalPadding(FMargin(48.f, 15.f));
			Style.SetPressedPadding(FMargin(48.f, 17.f, 48.f, 13.f));
			Button->SetStyle(Style);
		}

		USizeBox* SafeBox = NewObject<USizeBox>(Button);
		SafeBox->SetMinDesiredWidth(150.f);
		SafeBox->SetMinDesiredHeight(30.f);
		SafeBox->SetMaxDesiredWidth(760.f);
		UTextBlock* Text = FAscendUIStyle::MakeText(SafeBox, Label, FontSize,
			FAscendUIStyle::PaperWhite());
		Text->SetAutoWrapText(true);
		Text->SetWrapTextAt(680.f);
		Text->SetJustification(ETextJustify::Center);
		Text->SetShadowOffset(FVector2D(1.f, 1.f));
		Text->SetShadowColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.82f));
		SafeBox->SetContent(Text);
		Button->SetContent(SafeBox);
	}

	struct FRPOpeningPreset
	{
		FString Title = TEXT("新的旅程");
		FString Speaker = TEXT("同行者");
		FString PortraitId;
		FString Expression = TEXT("neutral");
		FString Narration = TEXT("你在陌生之地醒来，眼前的选择将决定这段旅程如何开始。");
		FString Dialogue = TEXT("选择一件带上吧。接下来，我们必须一起面对逼近的危险。");
		TArray<FString> ChoiceTemplates = {TEXT("选择【{item}】作为起始法宝")};
		FString ResultTemplate = TEXT("你获得了【{item}】。局势随即推进，眼前的威胁已经无法回避。");
		TArray<FString> EnemyNames = {TEXT("逼近的敌人"), TEXT("拦路者"), TEXT("追踪者")};
	};

	FRPOpeningPreset ParseOpeningPreset(const FString& WorldBook)
	{
		FRPOpeningPreset Result;
		TSharedPtr<FJsonObject> Root;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(WorldBook);
		if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid()) return Result;
		const TSharedPtr<FJsonObject>* Opening = nullptr;
		if (!Root->TryGetObjectField(TEXT("opening"), Opening) || !Opening || !Opening->IsValid()) return Result;
		auto Read = [Opening](const TCHAR* Key, FString& Target)
		{
			FString Value;
			if ((*Opening)->TryGetStringField(Key, Value)) Target = Value;
		};
		Read(TEXT("title"), Result.Title);
		Read(TEXT("speaker"), Result.Speaker);
		Read(TEXT("portrait_id"), Result.PortraitId);
		Read(TEXT("expression"), Result.Expression);
		Read(TEXT("narration"), Result.Narration);
		Read(TEXT("dialogue"), Result.Dialogue);
		Read(TEXT("result_template"), Result.ResultTemplate);
		auto ReadArray = [Opening](const TCHAR* Key, TArray<FString>& Target)
		{
			const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
			if (!(*Opening)->TryGetArrayField(Key, Values) || !Values) return;
			TArray<FString> Parsed;
			for (const TSharedPtr<FJsonValue>& Value : *Values)
			{
				FString Text;
				if (Value.IsValid() && Value->TryGetString(Text) && !Text.IsEmpty()) Parsed.Add(Text);
			}
			if (Parsed.Num() > 0) Target = MoveTemp(Parsed);
		};
		ReadArray(TEXT("choice_templates"), Result.ChoiceTemplates);
		ReadArray(TEXT("enemy_names"), Result.EnemyNames);
		return Result;
	}
}

void AAscendPlayerController::LoadInfiniteNarrativeSettings()
{
	InfiniteNarrativeSettings = FInfiniteNarrativeSettings();
	InfiniteNarrativeSettings.Model = TEXT("local-model");
	if (!GConfig) return;
	FString NarrativeConfigFile = GetNarrativeUserConfigPath();
	if (FPaths::FileExists(NarrativeConfigFile))
	{
		GConfig->LoadFile(NarrativeConfigFile);
	}
	else
	{
		// One-time compatibility path for builds that managed to retain this section.
		NarrativeConfigFile = GGameUserSettingsIni;
	}
	FString StoredEndpoint;
	int32 SettingsSchemaVersion = 0;
	GConfig->GetInt(NarrativeConfigSection, TEXT("SettingsSchemaVersion"), SettingsSchemaVersion, NarrativeConfigFile);
	bool bEndpointUseHttps = true;
	GConfig->GetString(NarrativeConfigSection, TEXT("Endpoint"), StoredEndpoint, NarrativeConfigFile);
	GConfig->GetBool(NarrativeConfigSection, TEXT("EndpointUseHttps"), bEndpointUseHttps, NarrativeConfigFile);
	StoredEndpoint = StoredEndpoint.TrimStartAndEnd();
	if (StoredEndpoint.Contains(TEXT("://")))
	{
		// 兼容旧配置；新配置不会把 :// 写进 INI，避免 UE 将 // 误判为注释。
		InfiniteNarrativeSettings.Endpoint = StoredEndpoint;
	}
	else if (!StoredEndpoint.IsEmpty() && StoredEndpoint != TEXT("https:") && StoredEndpoint != TEXT("http:"))
	{
		InfiniteNarrativeSettings.Endpoint = FString(bEndpointUseHttps ? TEXT("https://") : TEXT("http://"))
			+ StoredEndpoint;
	}
	GConfig->GetString(NarrativeConfigSection, TEXT("ApiKey"), InfiniteNarrativeSettings.ApiKey, NarrativeConfigFile);
	GConfig->GetString(NarrativeConfigSection, TEXT("Model"), InfiniteNarrativeSettings.Model, NarrativeConfigFile);
	GConfig->GetString(NarrativeConfigSection, TEXT("MvuVerifierModel"), InfiniteNarrativeSettings.MvuVerifierModel, NarrativeConfigFile);
	GConfig->GetBool(NarrativeConfigSection, TEXT("ShowFreeformInput"),
		InfiniteNarrativeSettings.bShowFreeformInput, NarrativeConfigFile);
	GConfig->GetBool(NarrativeConfigSection, TEXT("ShowSpeakerPortrait"),
		InfiniteNarrativeSettings.bShowSpeakerPortrait, NarrativeConfigFile);
	GConfig->GetBool(NarrativeConfigSection, TEXT("EnableScreenShake"),
		InfiniteNarrativeSettings.bEnableScreenShake, NarrativeConfigFile);
	GConfig->GetBool(NarrativeConfigSection, TEXT("StructuredMemory"),
		InfiniteNarrativeSettings.bEnableStructuredMemory, NarrativeConfigFile);
	GConfig->GetBool(NarrativeConfigSection, TEXT("ContinuityChecklist"),
		InfiniteNarrativeSettings.bEnableContinuityChecklist, NarrativeConfigFile);
	GConfig->GetBool(NarrativeConfigSection, TEXT("GenerateAfterCombatWithLog"),
		InfiniteNarrativeSettings.bGenerateAfterCombatWithLog, NarrativeConfigFile);
	GConfig->GetInt(NarrativeConfigSection, TEXT("InputContextTokens"), InfiniteNarrativeSettings.InputContextTokens, NarrativeConfigFile);
	GConfig->GetInt(NarrativeConfigSection, TEXT("MaxOutputTokens"), InfiniteNarrativeSettings.MaxOutputTokens, NarrativeConfigFile);
	GConfig->GetInt(NarrativeConfigSection, TEXT("MvuMaxOutputTokens"), InfiniteNarrativeSettings.MvuMaxOutputTokens, NarrativeConfigFile);
	GConfig->GetInt(NarrativeConfigSection, TEXT("NarrativeMinChars"), InfiniteNarrativeSettings.NarrativeMinChars, NarrativeConfigFile);
	GConfig->GetInt(NarrativeConfigSection, TEXT("NarrativeMaxChars"), InfiniteNarrativeSettings.NarrativeMaxChars, NarrativeConfigFile);
	GConfig->GetInt(NarrativeConfigSection, TEXT("RecentRawRounds"), InfiniteNarrativeSettings.RecentRawRounds, NarrativeConfigFile);
	GConfig->GetInt(NarrativeConfigSection, TEXT("CompressAfterRounds"), InfiniteNarrativeSettings.CompressAfterRounds, NarrativeConfigFile);
	GConfig->GetInt(NarrativeConfigSection, TEXT("UnsummarizedTokenThreshold"), InfiniteNarrativeSettings.UnsummarizedTokenThreshold, NarrativeConfigFile);
	GConfig->GetInt(NarrativeConfigSection, TEXT("MemoryTokenBudget"), InfiniteNarrativeSettings.MemoryTokenBudget, NarrativeConfigFile);
	GConfig->GetInt(NarrativeConfigSection, TEXT("WorldBookTokenBudget"), InfiniteNarrativeSettings.WorldBookTokenBudget, NarrativeConfigFile);
	GConfig->GetInt(NarrativeConfigSection, TEXT("WorldInfoScanDepth"), InfiniteNarrativeSettings.WorldInfoScanDepth, NarrativeConfigFile);
	GConfig->GetInt(NarrativeConfigSection, TEXT("Seed"), InfiniteNarrativeSettings.Seed, NarrativeConfigFile);
	GConfig->GetString(NarrativeConfigSection, TEXT("ReasoningEffort"), InfiniteNarrativeSettings.ReasoningEffort, NarrativeConfigFile);
	GConfig->GetString(NarrativeConfigSection, TEXT("MvuReasoningEffort"), InfiniteNarrativeSettings.MvuReasoningEffort, NarrativeConfigFile);
	GConfig->GetBool(NarrativeConfigSection, TEXT("RequestReasoning"), InfiniteNarrativeSettings.bRequestReasoning, NarrativeConfigFile);
	GConfig->GetBool(NarrativeConfigSection, TEXT("RequestMvuReasoning"), InfiniteNarrativeSettings.bRequestMvuReasoning, NarrativeConfigFile);
	GConfig->GetBool(NarrativeConfigSection, TEXT("ShowReasoning"), InfiniteNarrativeSettings.bShowReasoning, NarrativeConfigFile);
	GConfig->GetBool(NarrativeConfigSection, TEXT("StreamResponse"), InfiniteNarrativeSettings.bStreamResponse, NarrativeConfigFile);
	GConfig->GetString(NarrativeConfigSection, TEXT("StopStrings"), InfiniteNarrativeSettings.StopStrings, NarrativeConfigFile);
	GConfig->GetString(NarrativeConfigSection, TEXT("NarrativePromptPresetPath"), InfiniteNarrativeSettings.NarrativePromptPresetPath, NarrativeConfigFile);
	GConfig->GetString(NarrativeConfigSection, TEXT("MvuPromptPresetPath"), InfiniteNarrativeSettings.MvuPromptPresetPath, NarrativeConfigFile);
	GConfig->GetString(NarrativeConfigSection, TEXT("NarrativePromptPresetOverride"), InfiniteNarrativeSettings.NarrativePromptPresetOverride, NarrativeConfigFile);
	GConfig->GetString(NarrativeConfigSection, TEXT("MvuPromptPresetOverride"), InfiniteNarrativeSettings.MvuPromptPresetOverride, NarrativeConfigFile);
	GConfig->GetString(NarrativeConfigSection, TEXT("PersonaDescription"), InfiniteNarrativeSettings.PersonaDescription, NarrativeConfigFile);
	GConfig->GetString(NarrativeConfigSection, TEXT("CharacterDescription"), InfiniteNarrativeSettings.CharacterDescription, NarrativeConfigFile);
	GConfig->GetString(NarrativeConfigSection, TEXT("CharacterPersonality"), InfiniteNarrativeSettings.CharacterPersonality, NarrativeConfigFile);
	GConfig->GetString(NarrativeConfigSection, TEXT("Scenario"), InfiniteNarrativeSettings.Scenario, NarrativeConfigFile);
	GConfig->GetString(NarrativeConfigSection, TEXT("DialogueExamples"), InfiniteNarrativeSettings.DialogueExamples, NarrativeConfigFile);
	GConfig->GetString(NarrativeConfigSection, TEXT("AuthorNote"), InfiniteNarrativeSettings.AuthorNote, NarrativeConfigFile);
	GConfig->GetString(NarrativeConfigSection, TEXT("WorldBookOverride"), InfiniteNarrativeSettings.WorldBookOverride, NarrativeConfigFile);
	GConfig->GetString(NarrativeConfigSection, TEXT("CharacterRegistryOverride"), InfiniteNarrativeSettings.CharacterRegistryOverride, NarrativeConfigFile);
	GConfig->GetString(NarrativeConfigSection, TEXT("OpeningTitleOverride"), InfiniteNarrativeSettings.OpeningTitleOverride, NarrativeConfigFile);
	GConfig->GetString(NarrativeConfigSection, TEXT("OpeningSpeakerOverride"), InfiniteNarrativeSettings.OpeningSpeakerOverride, NarrativeConfigFile);
	GConfig->GetString(NarrativeConfigSection, TEXT("OpeningPortraitOverride"), InfiniteNarrativeSettings.OpeningPortraitOverride, NarrativeConfigFile);
	GConfig->GetString(NarrativeConfigSection, TEXT("OpeningExpressionOverride"), InfiniteNarrativeSettings.OpeningExpressionOverride, NarrativeConfigFile);
	GConfig->GetString(NarrativeConfigSection, TEXT("OpeningNarrationOverride"), InfiniteNarrativeSettings.OpeningNarrationOverride, NarrativeConfigFile);
	GConfig->GetString(NarrativeConfigSection, TEXT("OpeningDialogueOverride"), InfiniteNarrativeSettings.OpeningDialogueOverride, NarrativeConfigFile);
	GConfig->GetString(NarrativeConfigSection, TEXT("CustomWorldBook"), InfiniteNarrativeSettings.CustomWorldBook, NarrativeConfigFile);
	GConfig->GetString(NarrativeConfigSection, TEXT("CustomContinuityChecklist"), InfiniteNarrativeSettings.CustomContinuityChecklist, NarrativeConfigFile);
	GConfig->GetFloat(NarrativeConfigSection, TEXT("Temperature"), InfiniteNarrativeSettings.Temperature, NarrativeConfigFile);
	GConfig->GetFloat(NarrativeConfigSection, TEXT("TopP"), InfiniteNarrativeSettings.TopP, NarrativeConfigFile);
	GConfig->GetFloat(NarrativeConfigSection, TEXT("TopK"), InfiniteNarrativeSettings.TopK, NarrativeConfigFile);
	GConfig->GetFloat(NarrativeConfigSection, TEXT("TopA"), InfiniteNarrativeSettings.TopA, NarrativeConfigFile);
	GConfig->GetFloat(NarrativeConfigSection, TEXT("MinP"), InfiniteNarrativeSettings.MinP, NarrativeConfigFile);
	GConfig->GetFloat(NarrativeConfigSection, TEXT("FrequencyPenalty"), InfiniteNarrativeSettings.FrequencyPenalty, NarrativeConfigFile);
	GConfig->GetFloat(NarrativeConfigSection, TEXT("PresencePenalty"), InfiniteNarrativeSettings.PresencePenalty, NarrativeConfigFile);
	GConfig->GetFloat(NarrativeConfigSection, TEXT("RepetitionPenalty"), InfiniteNarrativeSettings.RepetitionPenalty, NarrativeConfigFile);
	GConfig->GetFloat(NarrativeConfigSection, TEXT("TimeoutSeconds"), InfiniteNarrativeSettings.TimeoutSeconds, NarrativeConfigFile);
	GConfig->GetFloat(NarrativeConfigSection, TEXT("SfxVolume"), InfiniteNarrativeSettings.SfxVolume, NarrativeConfigFile);
	InfiniteNarrativeSettings.InputContextTokens = FMath::Clamp(InfiniteNarrativeSettings.InputContextTokens, 8192, 2000000);
	InfiniteNarrativeSettings.MaxOutputTokens = FMath::Clamp(InfiniteNarrativeSettings.MaxOutputTokens, 1024, 262144);
	InfiniteNarrativeSettings.MvuMaxOutputTokens = FMath::Clamp(InfiniteNarrativeSettings.MvuMaxOutputTokens, 2048, 262144);
	InfiniteNarrativeSettings.NarrativeMinChars = FMath::Clamp(InfiniteNarrativeSettings.NarrativeMinChars, 200, 20000);
	InfiniteNarrativeSettings.NarrativeMaxChars = FMath::Clamp(InfiniteNarrativeSettings.NarrativeMaxChars,
		InfiniteNarrativeSettings.NarrativeMinChars, 30000);
	InfiniteNarrativeSettings.Temperature = FMath::Clamp(InfiniteNarrativeSettings.Temperature, 0.f, 1.5f);
	InfiniteNarrativeSettings.TopP = FMath::Clamp(InfiniteNarrativeSettings.TopP, 0.f, 1.f);
	InfiniteNarrativeSettings.RecentRawRounds = FMath::Max(0, InfiniteNarrativeSettings.RecentRawRounds);
	InfiniteNarrativeSettings.CompressAfterRounds = FMath::Max(0, InfiniteNarrativeSettings.CompressAfterRounds);
	InfiniteNarrativeSettings.WorldInfoScanDepth = FMath::Max(0, InfiniteNarrativeSettings.WorldInfoScanDepth);
	if (SettingsSchemaVersion < 6)
	{
		if (SettingsSchemaVersion < 2)
		{
			// v2 changes the quality default from a three-floor summary window to durable,
			// token-packed chat history and automatic provider reasoning.
			InfiniteNarrativeSettings.RecentRawRounds = 0;
			InfiniteNarrativeSettings.CompressAfterRounds = 0;
			InfiniteNarrativeSettings.ReasoningEffort = TEXT("auto");
			InfiniteNarrativeSettings.MvuReasoningEffort = TEXT("auto");
		}
		// v3 accommodates slow first-token latency from reasoning models and long prompts.
		InfiniteNarrativeSettings.TimeoutSeconds = 180.f;
		// v4 makes the already persisted but previously unused streaming setting real.
		// Older builds always wrote false without exposing a user control, so migrate it.
		InfiniteNarrativeSettings.bStreamResponse = true;
		// v5 follows current SillyTavern DeepSeek V4 behavior: thinking is an explicit
		// request, independent from whether returned reasoning is visible.
		InfiniteNarrativeSettings.bRequestReasoning = false;
		InfiniteNarrativeSettings.bRequestMvuReasoning = false;
		// v6 changes the writer from slow serial-fiction pacing to high-density roguelike scenes.
		if (FMath::IsNearlyEqual(InfiniteNarrativeSettings.Temperature, 0.55f)
			|| FMath::IsNearlyEqual(InfiniteNarrativeSettings.Temperature, 0.85f))
			InfiniteNarrativeSettings.Temperature = 1.05f;
		if (FMath::IsNearlyZero(InfiniteNarrativeSettings.FrequencyPenalty))
			InfiniteNarrativeSettings.FrequencyPenalty = 0.1f;
		if (FMath::IsNearlyZero(InfiniteNarrativeSettings.PresencePenalty))
			InfiniteNarrativeSettings.PresencePenalty = 0.15f;
		if (InfiniteNarrativeSettings.NarrativeMinChars == 1500
			&& InfiniteNarrativeSettings.NarrativeMaxChars == 3500)
		{
			InfiniteNarrativeSettings.NarrativeMinChars = 600;
			InfiniteNarrativeSettings.NarrativeMaxChars = 1200;
		}
	}
	InfiniteNarrativeSettings.TimeoutSeconds = FMath::Clamp(InfiniteNarrativeSettings.TimeoutSeconds, 5.f, 180.f);
	InfiniteNarrativeSettings.SfxVolume = FMath::Clamp(InfiniteNarrativeSettings.SfxVolume, 0.f, 1.f);
	if (InfiniteNarrativeSettings.Model.IsEmpty()) InfiniteNarrativeSettings.Model = TEXT("local-model");
	UE_LOG(LogTemp, Display, TEXT("[InfiniteRP] settings loaded source=%s endpoint_configured=%s model=%s key_configured=%s"),
		*NarrativeConfigFile, InfiniteNarrativeSettings.Endpoint.IsEmpty() ? TEXT("false") : TEXT("true"),
		*InfiniteNarrativeSettings.Model, InfiniteNarrativeSettings.ApiKey.IsEmpty() ? TEXT("false") : TEXT("true"));
}

void AAscendPlayerController::SaveInfiniteNarrativeSettings()
{
	if (SettingsEndpointInput) InfiniteNarrativeSettings.Endpoint = SettingsEndpointInput->GetText().ToString().TrimStartAndEnd();
	if (SettingsApiKeyInput) InfiniteNarrativeSettings.ApiKey = SettingsApiKeyInput->GetText().ToString().TrimStartAndEnd();
	if (SettingsModelInput) InfiniteNarrativeSettings.Model = SettingsModelInput->GetText().ToString().TrimStartAndEnd();
	if (SettingsMvuModelInput) InfiniteNarrativeSettings.MvuVerifierModel = SettingsMvuModelInput->GetText().ToString().TrimStartAndEnd();
	if (SettingsShowInputCheckBox) InfiniteNarrativeSettings.bShowFreeformInput = SettingsShowInputCheckBox->IsChecked();
	if (SettingsShowPortraitCheckBox) InfiniteNarrativeSettings.bShowSpeakerPortrait = SettingsShowPortraitCheckBox->IsChecked();
	if (SettingsScreenShakeCheckBox) InfiniteNarrativeSettings.bEnableScreenShake = SettingsScreenShakeCheckBox->IsChecked();
	if (SettingsStructuredMemoryCheckBox) InfiniteNarrativeSettings.bEnableStructuredMemory = SettingsStructuredMemoryCheckBox->IsChecked();
	if (SettingsContinuityCheckBox) InfiniteNarrativeSettings.bEnableContinuityChecklist = SettingsContinuityCheckBox->IsChecked();
	if (SettingsCombatGenerationTimingCombo)
		InfiniteNarrativeSettings.bGenerateAfterCombatWithLog =
			SettingsCombatGenerationTimingCombo->GetSelectedOption().StartsWith(TEXT("模式A"));
	auto ReadInt = [](UEditableTextBox* Input, int32 Current, int32 Min, int32 Max)
	{
		return Input ? FMath::Clamp(FCString::Atoi(*Input->GetText().ToString()), Min, Max) : Current;
	};
	InfiniteNarrativeSettings.InputContextTokens = ReadInt(SettingsInputContextInput, InfiniteNarrativeSettings.InputContextTokens, 8192, 2000000);
	InfiniteNarrativeSettings.MaxOutputTokens = ReadInt(SettingsMaxOutputInput, InfiniteNarrativeSettings.MaxOutputTokens, 1024, 262144);
	InfiniteNarrativeSettings.MvuMaxOutputTokens = ReadInt(SettingsMvuMaxOutputInput,
		InfiniteNarrativeSettings.MvuMaxOutputTokens, 2048, 262144);
	InfiniteNarrativeSettings.NarrativeMinChars = ReadInt(SettingsNarrativeMinInput, InfiniteNarrativeSettings.NarrativeMinChars, 200, 20000);
	InfiniteNarrativeSettings.NarrativeMaxChars = ReadInt(SettingsNarrativeMaxInput, InfiniteNarrativeSettings.NarrativeMaxChars,
		InfiniteNarrativeSettings.NarrativeMinChars, 30000);
	InfiniteNarrativeSettings.RecentRawRounds = ReadInt(SettingsRecentRoundsInput, InfiniteNarrativeSettings.RecentRawRounds, 0, 100000);
	InfiniteNarrativeSettings.CompressAfterRounds = ReadInt(SettingsCompressRoundsInput,
		InfiniteNarrativeSettings.CompressAfterRounds, 0, 100000);
	InfiniteNarrativeSettings.UnsummarizedTokenThreshold = ReadInt(SettingsUnsummarizedTokensInput,
		InfiniteNarrativeSettings.UnsummarizedTokenThreshold, 4000, 200000);
	InfiniteNarrativeSettings.MemoryTokenBudget = ReadInt(SettingsMemoryBudgetInput, InfiniteNarrativeSettings.MemoryTokenBudget, 1000, 100000);
	InfiniteNarrativeSettings.WorldBookTokenBudget = ReadInt(SettingsWorldBookBudgetInput, InfiniteNarrativeSettings.WorldBookTokenBudget, 1000, 100000);
	InfiniteNarrativeSettings.WorldInfoScanDepth = ReadInt(SettingsWorldInfoScanDepthInput,
		InfiniteNarrativeSettings.WorldInfoScanDepth, 0, 100000);
	InfiniteNarrativeSettings.Seed = ReadInt(SettingsSeedInput, InfiniteNarrativeSettings.Seed, -1, MAX_int32);
	auto ReadFloat = [](UEditableTextBox* Input, float Current, float Min, float Max)
	{
		return Input ? FMath::Clamp(FCString::Atof(*Input->GetText().ToString()), Min, Max) : Current;
	};
	InfiniteNarrativeSettings.Temperature = ReadFloat(SettingsTemperatureInput, InfiniteNarrativeSettings.Temperature, 0.f, 1.5f);
	InfiniteNarrativeSettings.TopP = ReadFloat(SettingsTopPInput, InfiniteNarrativeSettings.TopP, 0.f, 1.f);
	InfiniteNarrativeSettings.TopK = ReadFloat(SettingsTopKInput, InfiniteNarrativeSettings.TopK, 0.f, 1000.f);
	InfiniteNarrativeSettings.TopA = ReadFloat(SettingsTopAInput, InfiniteNarrativeSettings.TopA, 0.f, 1.f);
	InfiniteNarrativeSettings.MinP = ReadFloat(SettingsMinPInput, InfiniteNarrativeSettings.MinP, 0.f, 1.f);
	InfiniteNarrativeSettings.FrequencyPenalty = ReadFloat(SettingsFrequencyPenaltyInput,
		InfiniteNarrativeSettings.FrequencyPenalty, -2.f, 2.f);
	InfiniteNarrativeSettings.PresencePenalty = ReadFloat(SettingsPresencePenaltyInput,
		InfiniteNarrativeSettings.PresencePenalty, -2.f, 2.f);
	InfiniteNarrativeSettings.RepetitionPenalty = ReadFloat(SettingsRepetitionPenaltyInput,
		InfiniteNarrativeSettings.RepetitionPenalty, 0.f, 2.f);
	InfiniteNarrativeSettings.TimeoutSeconds = ReadFloat(SettingsTimeoutInput, InfiniteNarrativeSettings.TimeoutSeconds, 5.f, 180.f);
	InfiniteNarrativeSettings.SfxVolume = ReadFloat(SettingsSfxVolumeInput, InfiniteNarrativeSettings.SfxVolume, 0.f, 1.f);
	if (SettingsReasoningEffortCombo)
		InfiniteNarrativeSettings.ReasoningEffort = SettingsReasoningEffortCombo->GetSelectedOption();
	if (SettingsMvuReasoningEffortCombo)
		InfiniteNarrativeSettings.MvuReasoningEffort = SettingsMvuReasoningEffortCombo->GetSelectedOption();
	if (SettingsStreamResponseCheckBox)
		InfiniteNarrativeSettings.bStreamResponse = SettingsStreamResponseCheckBox->IsChecked();
	if (SettingsRequestReasoningCheckBox)
		InfiniteNarrativeSettings.bRequestReasoning = SettingsRequestReasoningCheckBox->IsChecked();
	if (SettingsRequestMvuReasoningCheckBox)
		InfiniteNarrativeSettings.bRequestMvuReasoning = SettingsRequestMvuReasoningCheckBox->IsChecked();
	if (SettingsStopStringsInput) InfiniteNarrativeSettings.StopStrings = SettingsStopStringsInput->GetText().ToString();
	if (SettingsNarrativePresetPathInput) InfiniteNarrativeSettings.NarrativePromptPresetPath =
		SettingsNarrativePresetPathInput->GetText().ToString().TrimStartAndEnd();
	if (SettingsMvuPresetPathInput) InfiniteNarrativeSettings.MvuPromptPresetPath =
		SettingsMvuPresetPathInput->GetText().ToString().TrimStartAndEnd();
	if (SettingsPersonaInput) InfiniteNarrativeSettings.PersonaDescription = SettingsPersonaInput->GetText().ToString();
	if (SettingsCharacterDescriptionInput) InfiniteNarrativeSettings.CharacterDescription = SettingsCharacterDescriptionInput->GetText().ToString();
	if (SettingsCharacterPersonalityInput) InfiniteNarrativeSettings.CharacterPersonality = SettingsCharacterPersonalityInput->GetText().ToString();
	if (SettingsScenarioInput) InfiniteNarrativeSettings.Scenario = SettingsScenarioInput->GetText().ToString();
	if (SettingsDialogueExamplesInput) InfiniteNarrativeSettings.DialogueExamples = SettingsDialogueExamplesInput->GetText().ToString();
	if (SettingsAuthorNoteInput) InfiniteNarrativeSettings.AuthorNote = SettingsAuthorNoteInput->GetText().ToString();
	if (SettingsCustomWorldBookInput)
	{
		InfiniteNarrativeSettings.WorldBookOverride = SettingsCustomWorldBookInput->GetText().ToString().TrimStartAndEnd();
		// 首次通过新版完整编辑器保存后，旧版追加段已经并入全文，避免重复注入。
		InfiniteNarrativeSettings.CustomWorldBook.Reset();
	}
	if (SettingsCharacterRegistryInput) InfiniteNarrativeSettings.CharacterRegistryOverride = SettingsCharacterRegistryInput->GetText().ToString().TrimStartAndEnd();
	if (SettingsOpeningTitleInput) InfiniteNarrativeSettings.OpeningTitleOverride = SettingsOpeningTitleInput->GetText().ToString().TrimStartAndEnd();
	if (SettingsOpeningSpeakerInput) InfiniteNarrativeSettings.OpeningSpeakerOverride = SettingsOpeningSpeakerInput->GetText().ToString().TrimStartAndEnd();
	if (SettingsOpeningPortraitInput) InfiniteNarrativeSettings.OpeningPortraitOverride = SettingsOpeningPortraitInput->GetText().ToString().TrimStartAndEnd();
	if (SettingsOpeningExpressionInput) InfiniteNarrativeSettings.OpeningExpressionOverride = SettingsOpeningExpressionInput->GetText().ToString().TrimStartAndEnd();
	if (SettingsOpeningNarrationInput) InfiniteNarrativeSettings.OpeningNarrationOverride = SettingsOpeningNarrationInput->GetText().ToString().TrimStartAndEnd();
	if (SettingsOpeningDialogueInput) InfiniteNarrativeSettings.OpeningDialogueOverride = SettingsOpeningDialogueInput->GetText().ToString().TrimStartAndEnd();
	if (SettingsContinuityInput) InfiniteNarrativeSettings.CustomContinuityChecklist = SettingsContinuityInput->GetText().ToString().TrimStartAndEnd();
	if (InfiniteNarrativeSettings.Model.IsEmpty()) InfiniteNarrativeSettings.Model = TEXT("local-model");
	if (!GConfig) return;
	const FString NarrativeConfigFile = GetNarrativeUserConfigPath();
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(NarrativeConfigFile), true);
	if (!FPaths::FileExists(NarrativeConfigFile))
	{
		FFileHelper::SaveStringToFile(TEXT("; AscendSpire local AI/RP settings\n"), *NarrativeConfigFile);
	}
	GConfig->LoadFile(NarrativeConfigFile);
	GConfig->SetInt(NarrativeConfigSection, TEXT("SettingsSchemaVersion"), 6, NarrativeConfigFile);
	FString StoredEndpoint = InfiniteNarrativeSettings.Endpoint;
	const bool bEndpointUseHttps = StoredEndpoint.RemoveFromStart(TEXT("https://"), ESearchCase::IgnoreCase);
	if (!bEndpointUseHttps) StoredEndpoint.RemoveFromStart(TEXT("http://"), ESearchCase::IgnoreCase);
	GConfig->SetString(NarrativeConfigSection, TEXT("Endpoint"), *StoredEndpoint, NarrativeConfigFile);
	GConfig->SetBool(NarrativeConfigSection, TEXT("EndpointUseHttps"), bEndpointUseHttps, NarrativeConfigFile);
	GConfig->SetString(NarrativeConfigSection, TEXT("ApiKey"), *InfiniteNarrativeSettings.ApiKey, NarrativeConfigFile);
	GConfig->SetString(NarrativeConfigSection, TEXT("Model"), *InfiniteNarrativeSettings.Model, NarrativeConfigFile);
	GConfig->SetString(NarrativeConfigSection, TEXT("MvuVerifierModel"), *InfiniteNarrativeSettings.MvuVerifierModel, NarrativeConfigFile);
	GConfig->SetBool(NarrativeConfigSection, TEXT("ShowFreeformInput"),
		InfiniteNarrativeSettings.bShowFreeformInput, NarrativeConfigFile);
	GConfig->SetBool(NarrativeConfigSection, TEXT("ShowSpeakerPortrait"), InfiniteNarrativeSettings.bShowSpeakerPortrait, NarrativeConfigFile);
	GConfig->SetBool(NarrativeConfigSection, TEXT("EnableScreenShake"), InfiniteNarrativeSettings.bEnableScreenShake, NarrativeConfigFile);
	GConfig->SetBool(NarrativeConfigSection, TEXT("StructuredMemory"), InfiniteNarrativeSettings.bEnableStructuredMemory, NarrativeConfigFile);
	GConfig->SetBool(NarrativeConfigSection, TEXT("ContinuityChecklist"), InfiniteNarrativeSettings.bEnableContinuityChecklist, NarrativeConfigFile);
	GConfig->SetBool(NarrativeConfigSection, TEXT("GenerateAfterCombatWithLog"),
		InfiniteNarrativeSettings.bGenerateAfterCombatWithLog, NarrativeConfigFile);
	GConfig->SetInt(NarrativeConfigSection, TEXT("InputContextTokens"), InfiniteNarrativeSettings.InputContextTokens, NarrativeConfigFile);
	GConfig->SetInt(NarrativeConfigSection, TEXT("MaxOutputTokens"), InfiniteNarrativeSettings.MaxOutputTokens, NarrativeConfigFile);
	GConfig->SetInt(NarrativeConfigSection, TEXT("MvuMaxOutputTokens"), InfiniteNarrativeSettings.MvuMaxOutputTokens, NarrativeConfigFile);
	GConfig->SetInt(NarrativeConfigSection, TEXT("NarrativeMinChars"), InfiniteNarrativeSettings.NarrativeMinChars, NarrativeConfigFile);
	GConfig->SetInt(NarrativeConfigSection, TEXT("NarrativeMaxChars"), InfiniteNarrativeSettings.NarrativeMaxChars, NarrativeConfigFile);
	GConfig->SetInt(NarrativeConfigSection, TEXT("RecentRawRounds"), InfiniteNarrativeSettings.RecentRawRounds, NarrativeConfigFile);
	GConfig->SetInt(NarrativeConfigSection, TEXT("CompressAfterRounds"), InfiniteNarrativeSettings.CompressAfterRounds, NarrativeConfigFile);
	GConfig->SetInt(NarrativeConfigSection, TEXT("UnsummarizedTokenThreshold"), InfiniteNarrativeSettings.UnsummarizedTokenThreshold, NarrativeConfigFile);
	GConfig->SetInt(NarrativeConfigSection, TEXT("MemoryTokenBudget"), InfiniteNarrativeSettings.MemoryTokenBudget, NarrativeConfigFile);
	GConfig->SetInt(NarrativeConfigSection, TEXT("WorldBookTokenBudget"), InfiniteNarrativeSettings.WorldBookTokenBudget, NarrativeConfigFile);
	GConfig->SetInt(NarrativeConfigSection, TEXT("WorldInfoScanDepth"), InfiniteNarrativeSettings.WorldInfoScanDepth, NarrativeConfigFile);
	GConfig->SetInt(NarrativeConfigSection, TEXT("Seed"), InfiniteNarrativeSettings.Seed, NarrativeConfigFile);
	GConfig->SetString(NarrativeConfigSection, TEXT("ReasoningEffort"), *InfiniteNarrativeSettings.ReasoningEffort, NarrativeConfigFile);
	GConfig->SetString(NarrativeConfigSection, TEXT("MvuReasoningEffort"), *InfiniteNarrativeSettings.MvuReasoningEffort, NarrativeConfigFile);
	GConfig->SetBool(NarrativeConfigSection, TEXT("RequestReasoning"), InfiniteNarrativeSettings.bRequestReasoning, NarrativeConfigFile);
	GConfig->SetBool(NarrativeConfigSection, TEXT("RequestMvuReasoning"), InfiniteNarrativeSettings.bRequestMvuReasoning, NarrativeConfigFile);
	GConfig->SetBool(NarrativeConfigSection, TEXT("ShowReasoning"), InfiniteNarrativeSettings.bShowReasoning, NarrativeConfigFile);
	GConfig->SetBool(NarrativeConfigSection, TEXT("StreamResponse"), InfiniteNarrativeSettings.bStreamResponse, NarrativeConfigFile);
	GConfig->SetString(NarrativeConfigSection, TEXT("StopStrings"), *InfiniteNarrativeSettings.StopStrings, NarrativeConfigFile);
	GConfig->SetString(NarrativeConfigSection, TEXT("NarrativePromptPresetPath"), *InfiniteNarrativeSettings.NarrativePromptPresetPath, NarrativeConfigFile);
	GConfig->SetString(NarrativeConfigSection, TEXT("MvuPromptPresetPath"), *InfiniteNarrativeSettings.MvuPromptPresetPath, NarrativeConfigFile);
	GConfig->SetString(NarrativeConfigSection, TEXT("NarrativePromptPresetOverride"), *InfiniteNarrativeSettings.NarrativePromptPresetOverride, NarrativeConfigFile);
	GConfig->SetString(NarrativeConfigSection, TEXT("MvuPromptPresetOverride"), *InfiniteNarrativeSettings.MvuPromptPresetOverride, NarrativeConfigFile);
	GConfig->SetString(NarrativeConfigSection, TEXT("PersonaDescription"), *InfiniteNarrativeSettings.PersonaDescription, NarrativeConfigFile);
	GConfig->SetString(NarrativeConfigSection, TEXT("CharacterDescription"), *InfiniteNarrativeSettings.CharacterDescription, NarrativeConfigFile);
	GConfig->SetString(NarrativeConfigSection, TEXT("CharacterPersonality"), *InfiniteNarrativeSettings.CharacterPersonality, NarrativeConfigFile);
	GConfig->SetString(NarrativeConfigSection, TEXT("Scenario"), *InfiniteNarrativeSettings.Scenario, NarrativeConfigFile);
	GConfig->SetString(NarrativeConfigSection, TEXT("DialogueExamples"), *InfiniteNarrativeSettings.DialogueExamples, NarrativeConfigFile);
	GConfig->SetString(NarrativeConfigSection, TEXT("AuthorNote"), *InfiniteNarrativeSettings.AuthorNote, NarrativeConfigFile);
	GConfig->SetString(NarrativeConfigSection, TEXT("WorldBookOverride"), *InfiniteNarrativeSettings.WorldBookOverride, NarrativeConfigFile);
	GConfig->SetString(NarrativeConfigSection, TEXT("CharacterRegistryOverride"), *InfiniteNarrativeSettings.CharacterRegistryOverride, NarrativeConfigFile);
	GConfig->SetString(NarrativeConfigSection, TEXT("OpeningTitleOverride"), *InfiniteNarrativeSettings.OpeningTitleOverride, NarrativeConfigFile);
	GConfig->SetString(NarrativeConfigSection, TEXT("OpeningSpeakerOverride"), *InfiniteNarrativeSettings.OpeningSpeakerOverride, NarrativeConfigFile);
	GConfig->SetString(NarrativeConfigSection, TEXT("OpeningPortraitOverride"), *InfiniteNarrativeSettings.OpeningPortraitOverride, NarrativeConfigFile);
	GConfig->SetString(NarrativeConfigSection, TEXT("OpeningExpressionOverride"), *InfiniteNarrativeSettings.OpeningExpressionOverride, NarrativeConfigFile);
	GConfig->SetString(NarrativeConfigSection, TEXT("OpeningNarrationOverride"), *InfiniteNarrativeSettings.OpeningNarrationOverride, NarrativeConfigFile);
	GConfig->SetString(NarrativeConfigSection, TEXT("OpeningDialogueOverride"), *InfiniteNarrativeSettings.OpeningDialogueOverride, NarrativeConfigFile);
	GConfig->SetString(NarrativeConfigSection, TEXT("CustomWorldBook"), *InfiniteNarrativeSettings.CustomWorldBook, NarrativeConfigFile);
	GConfig->SetString(NarrativeConfigSection, TEXT("CustomContinuityChecklist"), *InfiniteNarrativeSettings.CustomContinuityChecklist, NarrativeConfigFile);
	GConfig->SetFloat(NarrativeConfigSection, TEXT("Temperature"), InfiniteNarrativeSettings.Temperature, NarrativeConfigFile);
	GConfig->SetFloat(NarrativeConfigSection, TEXT("TopP"), InfiniteNarrativeSettings.TopP, NarrativeConfigFile);
	GConfig->SetFloat(NarrativeConfigSection, TEXT("TopK"), InfiniteNarrativeSettings.TopK, NarrativeConfigFile);
	GConfig->SetFloat(NarrativeConfigSection, TEXT("TopA"), InfiniteNarrativeSettings.TopA, NarrativeConfigFile);
	GConfig->SetFloat(NarrativeConfigSection, TEXT("MinP"), InfiniteNarrativeSettings.MinP, NarrativeConfigFile);
	GConfig->SetFloat(NarrativeConfigSection, TEXT("FrequencyPenalty"), InfiniteNarrativeSettings.FrequencyPenalty, NarrativeConfigFile);
	GConfig->SetFloat(NarrativeConfigSection, TEXT("PresencePenalty"), InfiniteNarrativeSettings.PresencePenalty, NarrativeConfigFile);
	GConfig->SetFloat(NarrativeConfigSection, TEXT("RepetitionPenalty"), InfiniteNarrativeSettings.RepetitionPenalty, NarrativeConfigFile);
	GConfig->SetFloat(NarrativeConfigSection, TEXT("TimeoutSeconds"), InfiniteNarrativeSettings.TimeoutSeconds, NarrativeConfigFile);
	GConfig->SetFloat(NarrativeConfigSection, TEXT("SfxVolume"), InfiniteNarrativeSettings.SfxVolume, NarrativeConfigFile);
	GConfig->Flush(false, NarrativeConfigFile);
	UE_LOG(LogTemp, Display, TEXT("[InfiniteRP] settings saved to dedicated file=%s"), *NarrativeConfigFile);
}

void AAscendPlayerController::ResetSettingsWidgetRefs()
{
	SettingsEndpointInput = nullptr;
	SettingsApiKeyInput = nullptr;
	SettingsModelInput = nullptr;
	SettingsMvuModelInput = nullptr;
	SettingsTemperatureInput = nullptr;
	SettingsTopPInput = nullptr;
	SettingsTopKInput = nullptr;
	SettingsTopAInput = nullptr;
	SettingsMinPInput = nullptr;
	SettingsFrequencyPenaltyInput = nullptr;
	SettingsPresencePenaltyInput = nullptr;
	SettingsRepetitionPenaltyInput = nullptr;
	SettingsSeedInput = nullptr;
	SettingsStopStringsInput = nullptr;
	SettingsReasoningEffortCombo = nullptr;
	SettingsMvuReasoningEffortCombo = nullptr;
	SettingsStreamResponseCheckBox = nullptr;
	SettingsRequestReasoningCheckBox = nullptr;
	SettingsRequestMvuReasoningCheckBox = nullptr;
	SettingsTimeoutInput = nullptr;
	SettingsSfxVolumeInput = nullptr;
	SettingsInputContextInput = nullptr;
	SettingsMaxOutputInput = nullptr;
	SettingsMvuMaxOutputInput = nullptr;
	SettingsNarrativeMinInput = nullptr;
	SettingsNarrativeMaxInput = nullptr;
	SettingsRecentRoundsInput = nullptr;
	SettingsCompressRoundsInput = nullptr;
	SettingsUnsummarizedTokensInput = nullptr;
	SettingsMemoryBudgetInput = nullptr;
	SettingsWorldBookBudgetInput = nullptr;
	SettingsWorldInfoScanDepthInput = nullptr;
	SettingsNarrativePresetPathInput = nullptr;
	SettingsMvuPresetPathInput = nullptr;
	SettingsPersonaInput = nullptr;
	SettingsCharacterDescriptionInput = nullptr;
	SettingsCharacterPersonalityInput = nullptr;
	SettingsScenarioInput = nullptr;
	SettingsDialogueExamplesInput = nullptr;
	SettingsAuthorNoteInput = nullptr;
	SettingsCustomWorldBookInput = nullptr;
	SettingsCharacterRegistryInput = nullptr;
	SettingsOpeningTitleInput = nullptr;
	SettingsOpeningSpeakerInput = nullptr;
	SettingsOpeningPortraitInput = nullptr;
	SettingsOpeningExpressionInput = nullptr;
	SettingsOpeningNarrationInput = nullptr;
	SettingsOpeningDialogueInput = nullptr;
	SettingsContinuityInput = nullptr;
	SettingsShowInputCheckBox = nullptr;
	SettingsShowPortraitCheckBox = nullptr;
	SettingsScreenShakeCheckBox = nullptr;
	SettingsCombatGenerationTimingCombo = nullptr;
	SettingsStructuredMemoryCheckBox = nullptr;
	SettingsContinuityCheckBox = nullptr;
}

void AAscendPlayerController::ShowSettings(int32 Category)
{
	if (Category >= 0) SettingsCategory = FMath::Clamp(Category, 0, 5);
	ResetSettingsWidgetRefs();
	UScrollBox* Scroll = NewObject<UScrollBox>(RootWidget);
	UVerticalBox* Box = NewObject<UVerticalBox>(Scroll);
	Scroll->AddChild(Box);
	RPAddPad(Box, 34.f);

	UTextBlock* Title = RPMakeWrappedText(Box, TEXT("设 置"), 34, FAscendUIStyle::GoldYellow());
	RPAddToVBox(Box, Title);

	static const TCHAR* CategoryNames[] = {TEXT("显示"), TEXT("声音"), TEXT("游戏"), TEXT("AI参数"), TEXT("Prompt"), TEXT("RP功能")};
	UHorizontalBox* Tabs = NewObject<UHorizontalBox>(Box);
	Tabs->AddChildToHorizontalBox(NewObject<USpacer>(Tabs))->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	for (int32 Index = 0; Index < 6; ++Index)
	{
		const FString Label = Index == SettingsCategory
			? FString::Printf(TEXT("【◆ %s ◆】"), CategoryNames[Index])
			: FString::Printf(TEXT("【%s】"), CategoryNames[Index]);
		RPAddToHBox(Tabs, MakeLinkedButton(Tabs, Label, TEXT("settings_tab"), Index, 17), FMargin(5.f, 4.f));
	}
	Tabs->AddChildToHorizontalBox(NewObject<USpacer>(Tabs))->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	RPAddToVBox(Box, Tabs, FMargin(0.f, 14.f, 0.f, 18.f));

	auto AddField = [Box](const FString& Label, const FString& Value, bool bPassword) -> UEditableTextBox*
	{
		RPAddToVBox(Box, RPMakeWrappedText(Box, Label, 16, FAscendUIStyle::PaperWhite(), ETextJustify::Left),
			FMargin(180.f, 4.f, 180.f, 0.f));
		UEditableTextBox* Input = NewObject<UEditableTextBox>(Box);
		Input->SetText(FText::FromString(Value));
		Input->SetIsPassword(bPassword);
		RPAddToVBox(Box, Input, FMargin(180.f, 0.f, 180.f, 10.f));
		return Input;
	};

	auto AddToggle = [Box](const FString& Label, bool bChecked) -> UCheckBox*
	{
		UHorizontalBox* Row = NewObject<UHorizontalBox>(Box);
		Row->AddChildToHorizontalBox(NewObject<USpacer>(Row))->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		UCheckBox* Check = NewObject<UCheckBox>(Row);
		Check->SetIsChecked(bChecked);
		RPAddToHBox(Row, Check);
		RPAddToHBox(Row, RPMakeWrappedText(Row, Label, 16, FAscendUIStyle::PaperWhite(), ETextJustify::Left));
		Row->AddChildToHorizontalBox(NewObject<USpacer>(Row))->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		RPAddToVBox(Box, Row, FMargin(0.f, 5.f));
		return Check;
	};

	auto AddMultiLine = [Box](const FString& Label, const FString& Value, float Height = 180.f) -> UMultiLineEditableTextBox*
	{
		RPAddToVBox(Box, RPMakeWrappedText(Box, Label, 16, FAscendUIStyle::PaperWhite(), ETextJustify::Left),
			FMargin(150.f, 12.f, 150.f, 2.f));
		USizeBox* InputSize = NewObject<USizeBox>(Box);
		InputSize->SetHeightOverride(Height);
		UMultiLineEditableTextBox* Input = NewObject<UMultiLineEditableTextBox>(InputSize);
		Input->SetText(FText::FromString(Value));
		Input->SetHintText(FText::FromString(TEXT("留空则使用游戏默认内容")));
		Input->SetAutoWrapText(true);
		InputSize->SetContent(Input);
		RPAddToVBox(Box, InputSize, FMargin(150.f, 0.f, 150.f, 12.f));
		return Input;
	};

	if (SettingsCategory == 0)
	{
		RPAddToVBox(Box, RPMakeWrappedText(Box, TEXT("显示设置"), 25, FAscendUIStyle::GoldYellow()));
		SettingsShowPortraitCheckBox = AddToggle(TEXT("显示 RP 角色头像（当前无素材时使用角色专属颜色方块）"),
			InfiniteNarrativeSettings.bShowSpeakerPortrait);
		SettingsShowInputCheckBox = AddToggle(TEXT("在 RP 界面显示自由文本输入框"),
			InfiniteNarrativeSettings.bShowFreeformInput);
		SettingsScreenShakeCheckBox = AddToggle(TEXT("启用战斗屏幕震动"),
			InfiniteNarrativeSettings.bEnableScreenShake);
	}
	else if (SettingsCategory == 1)
	{
		RPAddToVBox(Box, RPMakeWrappedText(Box, TEXT("声音设置"), 25, FAscendUIStyle::GoldYellow()));
		SettingsSfxVolumeInput = AddField(TEXT("战斗与卡牌音效音量（0.0~1.0）"),
			FString::Printf(TEXT("%.2f"), InfiniteNarrativeSettings.SfxVolume), false);
		RPAddToVBox(Box, RPMakeWrappedText(Box,
			TEXT("当前版本尚未加入独立背景音乐轨道；后续接入音乐素材后会在此页增加音乐、环境音与语音音量。"),
			15, FAscendUIStyle::DimGray()), FMargin(150.f, 12.f));
	}
	else if (SettingsCategory == 2)
	{
		RPAddToVBox(Box, RPMakeWrappedText(Box, TEXT("游戏设置"), 25, FAscendUIStyle::GoldYellow()));
		RPAddToVBox(Box, RPMakeWrappedText(Box,
			TEXT("无尽叙事模式会在剧情选择、奖励结算和战斗结束后自动保存。游戏核心数值由本地规则裁决，LLM 无权直接改写气血、灵石、卡组或法宝。"),
			17, FAscendUIStyle::PaperWhite()), FMargin(150.f, 16.f));
		RPAddToVBox(Box, RPMakeWrappedText(Box,
			TEXT("战斗难度、动画速度、自动结束回合等通用选项将在对应系统完成后加入这里。"),
			15, FAscendUIStyle::DimGray()), FMargin(150.f, 8.f));
	}
	else if (SettingsCategory == 3)
	{
		RPAddToVBox(Box, RPMakeWrappedText(Box, TEXT("AI连接与生成"), 25, FAscendUIStyle::GoldYellow()));
		SettingsEndpointInput = AddField(TEXT("接口地址（OpenAI Chat Completions 兼容）"), InfiniteNarrativeSettings.Endpoint, false);
		SettingsModelInput = AddField(TEXT("主模型名称"), InfiniteNarrativeSettings.Model, false);
		SettingsApiKeyInput = AddField(TEXT("API Key（仅保存在本机用户设置中）"), InfiniteNarrativeSettings.ApiKey, true);
		SettingsTemperatureInput = AddField(TEXT("Temperature（0.0~1.5，默认0.85）"),
			FString::Printf(TEXT("%.2f"), InfiniteNarrativeSettings.Temperature), false);
		SettingsTopPInput = AddField(TEXT("Top P（0~1，默认0.99）"),
			FString::Printf(TEXT("%.3f"), InfiniteNarrativeSettings.TopP), false);
		SettingsTopKInput = AddField(TEXT("Top K（0表示交给模型/接口）"),
			FString::Printf(TEXT("%.0f"), InfiniteNarrativeSettings.TopK), false);
		SettingsTopAInput = AddField(TEXT("Top A（0表示关闭）"),
			FString::Printf(TEXT("%.3f"), InfiniteNarrativeSettings.TopA), false);
		SettingsMinPInput = AddField(TEXT("Min P（0表示关闭）"),
			FString::Printf(TEXT("%.3f"), InfiniteNarrativeSettings.MinP), false);
		SettingsFrequencyPenaltyInput = AddField(TEXT("Frequency penalty（-2~2）"),
			FString::Printf(TEXT("%.2f"), InfiniteNarrativeSettings.FrequencyPenalty), false);
		SettingsPresencePenaltyInput = AddField(TEXT("Presence penalty（-2~2）"),
			FString::Printf(TEXT("%.2f"), InfiniteNarrativeSettings.PresencePenalty), false);
		SettingsRepetitionPenaltyInput = AddField(TEXT("Repetition penalty（默认1）"),
			FString::Printf(TEXT("%.2f"), InfiniteNarrativeSettings.RepetitionPenalty), false);
		SettingsSeedInput = AddField(TEXT("Seed（-1为随机）"), FString::FromInt(InfiniteNarrativeSettings.Seed), false);
		SettingsStopStringsInput = AddMultiLine(TEXT("Stop strings（每行一个；留空使用预设）"),
			InfiniteNarrativeSettings.StopStrings, 110.f);
		auto AddReasoningCombo = [Box](const FString& Label, const FString& Selected) -> UComboBoxString*
		{
			RPAddToVBox(Box, RPMakeWrappedText(Box, Label, 16, FAscendUIStyle::PaperWhite(), ETextJustify::Left),
				FMargin(180.f, 8.f, 180.f, 2.f));
			UComboBoxString* Combo = NewObject<UComboBoxString>(Box);
			for (const TCHAR* Option : {TEXT("auto"), TEXT("disabled"), TEXT("low"), TEXT("medium"), TEXT("high")})
				Combo->AddOption(Option);
			Combo->SetSelectedOption(Selected.IsEmpty() ? TEXT("auto") : Selected);
			RPAddToVBox(Box, Combo, FMargin(180.f, 0.f, 180.f, 8.f));
			return Combo;
		};
		SettingsRequestReasoningCheckBox = AddToggle(TEXT("请求剧情模型输出思考（默认关闭）"),
			InfiniteNarrativeSettings.bRequestReasoning);
		SettingsReasoningEffortCombo = AddReasoningCombo(TEXT("剧情模型思考强度（仅开启上项后生效；auto=服务商决定）"),
			InfiniteNarrativeSettings.ReasoningEffort);
		SettingsStreamResponseCheckBox = AddToggle(
			TEXT("流式显示剧情（推荐；完整选项生成前保持锁定）"),
			InfiniteNarrativeSettings.bStreamResponse);
		SettingsTimeoutInput = AddField(TEXT("单次请求超时秒数（5~180）"),
			FString::Printf(TEXT("%.0f"), InfiniteNarrativeSettings.TimeoutSeconds), false);
		SettingsMaxOutputInput = AddField(TEXT("最大输出 Token（默认65535；接口不支持时自动降档）"),
			FString::FromInt(InfiniteNarrativeSettings.MaxOutputTokens), false);
		RPAddToVBox(Box, RPMakeWrappedText(Box, TEXT("LLM推演时机"), 16,
			FAscendUIStyle::PaperWhite(), ETextJustify::Left), FMargin(180.f, 8.f, 180.f, 2.f));
		SettingsCombatGenerationTimingCombo = NewObject<UComboBoxString>(Box);
		SettingsCombatGenerationTimingCombo->AddOption(TEXT("模式A · 战斗结束后推演（读取完整战斗日志）"));
		SettingsCombatGenerationTimingCombo->AddOption(TEXT("模式B · 战斗开始即假定胜利（不读取战斗日志）"));
		SettingsCombatGenerationTimingCombo->SetSelectedOption(InfiniteNarrativeSettings.bGenerateAfterCombatWithLog
			? TEXT("模式A · 战斗结束后推演（读取完整战斗日志）")
			: TEXT("模式B · 战斗开始即假定胜利（不读取战斗日志）"));
		RPAddToVBox(Box, SettingsCombatGenerationTimingCombo, FMargin(180.f, 0.f, 180.f, 8.f));
		RPAddToVBox(Box, RPMakeWrappedText(Box,
			TEXT("每一幕只调用一次剧情导演：同一次输出完成剧情、人物状态和下一页面判断。遇到原创卡时，选中后才进入无剧情的独立短脚本生卡流程。模式A偏重真实战况，模式B偏重进入RP时的流畅度；每局首场战斗固定使用模式B。"),
			14, FAscendUIStyle::DimGray()), FMargin(150.f, 6.f, 150.f, 12.f));
		RPAddToVBox(Box, RPMakeWrappedText(Box,
			TEXT("思考请求与思维链显示相互独立。DeepSeek V4 在未勾选请求思考时会明确发送 thinking=disabled；勾选后 auto 才由提供方决定 High/Max。卡片工坊固定关闭思考并只输出几行可编译脚本。"),
			14, FAscendUIStyle::DimGray()), FMargin(150.f, 4.f, 150.f, 12.f));
	}
	else if (SettingsCategory == 4)
	{
		RPAddToVBox(Box, RPMakeWrappedText(Box, TEXT("Prompt Manager 与角色定义"), 25, FAscendUIStyle::GoldYellow()));
		RPAddToVBox(Box, RPMakeWrappedText(Box,
			TEXT("内置 Prompt Manager 支持 SillyTavern 常用 prompts、prompt_order、角色宏、世界书位置和 in-chat depth。填写外部 JSON 路径即可载入兼容预设；留空使用内置预设。"),
			15, FAscendUIStyle::DimGray()), FMargin(140.f, 10.f));
		SettingsNarrativePresetPathInput = AddField(TEXT("剧情 Prompt 预设 JSON 路径"),
			InfiniteNarrativeSettings.NarrativePromptPresetPath, false);
		SettingsPersonaInput = AddMultiLine(TEXT("玩家 Persona（对应 personaDescription）"),
			InfiniteNarrativeSettings.PersonaDescription, 150.f);
		SettingsCharacterDescriptionInput = AddMultiLine(TEXT("角色描述（留空使用角色注册表）"),
			InfiniteNarrativeSettings.CharacterDescription, 200.f);
		SettingsCharacterPersonalityInput = AddMultiLine(TEXT("角色性格"),
			InfiniteNarrativeSettings.CharacterPersonality, 150.f);
		SettingsScenarioInput = AddMultiLine(TEXT("当前 Scenario"), InfiniteNarrativeSettings.Scenario, 180.f);
		SettingsDialogueExamplesInput = AddMultiLine(TEXT("对话示例 / mesExamples"),
			InfiniteNarrativeSettings.DialogueExamples, 220.f);
		SettingsAuthorNoteInput = AddMultiLine(TEXT("作者注释（按 in-chat depth 注入）"),
			InfiniteNarrativeSettings.AuthorNote, 160.f);
	}
	else
	{
		RPAddToVBox(Box, RPMakeWrappedText(Box, TEXT("RP、世界书与长期记忆"), 25, FAscendUIStyle::GoldYellow()));
		RPAddToVBox(Box, RPMakeWrappedText(Box,
			TEXT("默认保留全部逐字历史，并像 SillyTavern 一样按总上下文 Token 动态装载；只有超出模型预算才移除最旧消息。可把轮数或压缩阈值设为非零来主动限制。"),
			15, FAscendUIStyle::DimGray()), FMargin(140.f, 10.f));
		SettingsNarrativeMinInput = AddField(TEXT("可见剧情目标下限（软约束，默认600；偏短不会报错）"),
			FString::FromInt(InfiniteNarrativeSettings.NarrativeMinChars), false);
		SettingsNarrativeMaxInput = AddField(TEXT("可见剧情目标上限（软约束，默认1200；偏长不会报错）"),
			FString::FromInt(InfiniteNarrativeSettings.NarrativeMaxChars), false);
		SettingsInputContextInput = AddField(TEXT("输入上下文预算 Token（默认131072）"),
			FString::FromInt(InfiniteNarrativeSettings.InputContextTokens), false);
		SettingsRecentRoundsInput = AddField(TEXT("保留最近完整原文轮数（0=不限制，默认0）"),
			FString::FromInt(InfiniteNarrativeSettings.RecentRawRounds), false);
		SettingsCompressRoundsInput = AddField(TEXT("积累多少轮后压缩旧历史（0=关闭，默认0）"),
			FString::FromInt(InfiniteNarrativeSettings.CompressAfterRounds), false);
		SettingsUnsummarizedTokensInput = AddField(TEXT("未压缩原文 Token 阈值（默认18000）"),
			FString::FromInt(InfiniteNarrativeSettings.UnsummarizedTokenThreshold), false);
		SettingsMemoryBudgetInput = AddField(TEXT("长期记忆召回 Token 预算（默认12000）"),
			FString::FromInt(InfiniteNarrativeSettings.MemoryTokenBudget), false);
		SettingsWorldBookBudgetInput = AddField(TEXT("世界书 Token 预算（默认10000）"),
			FString::FromInt(InfiniteNarrativeSettings.WorldBookTokenBudget), false);
		SettingsWorldInfoScanDepthInput = AddField(TEXT("世界书关键词扫描最近消息数（0=全部，默认20）"),
			FString::FromInt(InfiniteNarrativeSettings.WorldInfoScanDepth), false);
		SettingsStructuredMemoryCheckBox = AddToggle(TEXT("启用结构化长期记忆与按需召回（不会自动压缩，除非上方阈值非0）"),
			InfiniteNarrativeSettings.bEnableStructuredMemory);
		SettingsContinuityCheckBox = AddToggle(TEXT("启用角色知识边界、连续性、奖励因果与格式检查"),
			InfiniteNarrativeSettings.bEnableContinuityChecklist);
		FString EditableWorldBook = InfiniteNarrativeSettings.WorldBookOverride;
		if (EditableWorldBook.IsEmpty()) EditableWorldBook = LoadRPDataFile(TEXT("Data/rp_worldbook.json"));
		if (!InfiniteNarrativeSettings.CustomWorldBook.IsEmpty())
			EditableWorldBook += TEXT("\n\n") + InfiniteNarrativeSettings.CustomWorldBook;
		FString EditableRegistry = InfiniteNarrativeSettings.CharacterRegistryOverride;
		if (EditableRegistry.IsEmpty()) EditableRegistry = LoadRPDataFile(TEXT("Data/rp_characters.json"));
		SettingsCustomWorldBookInput = AddMultiLine(
			TEXT("完整世界书（可完全替换默认《登仙路》设定；也可改成赛博朋克等任意世界。留空恢复游戏默认）"),
			EditableWorldBook, 420.f);
		SettingsCharacterRegistryInput = AddMultiLine(
			TEXT("角色与头像注册表 JSON（art 为默认头像；expressions 可配置 neutral/smile/sad/angry/surprised/hurt 差分）"),
			EditableRegistry, 280.f);
		RPAddToVBox(Box, RPMakeWrappedText(Box, TEXT("开场白便捷覆盖（以下字段留空时读取世界书 opening 节点）"),
			20, FAscendUIStyle::GoldYellow()), FMargin(140.f, 18.f, 140.f, 6.f));
		SettingsOpeningTitleInput = AddField(TEXT("开场标题覆盖"), InfiniteNarrativeSettings.OpeningTitleOverride, false);
		SettingsOpeningSpeakerInput = AddField(TEXT("开场说话人覆盖"), InfiniteNarrativeSettings.OpeningSpeakerOverride, false);
		SettingsOpeningPortraitInput = AddField(TEXT("开场 portrait_id 覆盖"), InfiniteNarrativeSettings.OpeningPortraitOverride, false);
		SettingsOpeningExpressionInput = AddField(TEXT("开场表情覆盖（neutral/smile/sad/angry/surprised/hurt）"),
			InfiniteNarrativeSettings.OpeningExpressionOverride, false);
		SettingsOpeningNarrationInput = AddMultiLine(TEXT("开场正文覆盖"),
			InfiniteNarrativeSettings.OpeningNarrationOverride, 220.f);
		SettingsOpeningDialogueInput = AddMultiLine(TEXT("开场角色台词覆盖"),
			InfiniteNarrativeSettings.OpeningDialogueOverride, 120.f);
		SettingsContinuityInput = AddMultiLine(TEXT("自定义 COT/连续性检查清单（不显示模型思维过程）"),
			InfiniteNarrativeSettings.CustomContinuityChecklist);
	}

	RPAddPad(Box, 18.f);
	UHorizontalBox* Buttons = NewObject<UHorizontalBox>(Box);
	Buttons->AddChildToHorizontalBox(NewObject<USpacer>(Buttons))->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	RPAddToHBox(Buttons, MakeLinkedButton(Buttons, TEXT("【保存并返回】"), TEXT("settings_save"), 0, 20));
	RPAddToHBox(Buttons, MakeLinkedButton(Buttons, TEXT("【返回】"), TEXT("settings_back"), 0, 20));
	Buttons->AddChildToHorizontalBox(NewObject<USpacer>(Buttons))->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	RPAddToVBox(Box, Buttons);

	SetScreen(Scroll, EGameScreen::Settings);
}

void AAscendPlayerController::ReturnFromSettings()
{
	const EGameScreen Destination = SettingsReturnScreen;
	ResetSettingsWidgetRefs();
	switch (Destination)
	{
	case EGameScreen::InfiniteNarrative: ShowInfiniteNarrative(); break;
	case EGameScreen::Combat: ShowCombat(); break;
	case EGameScreen::Reward: ShowReward(); break;
	case EGameScreen::Map: ShowMap(); break;
	case EGameScreen::Shop: ShowShop(); break;
	case EGameScreen::Rest: ShowRest(); break;
	case EGameScreen::GameOver: ShowGameOver(); break;
	case EGameScreen::Victory: ShowVictory(); break;
	default: ShowTitle(); break;
	}
}

void AAscendPlayerController::StartInfiniteNarrativeRun()
{
	if (!Run || !Run->StartNewRun(FMath::RandRange(1, 999999), true))
	{
		ShowTitle();
		return;
	}
	bInfiniteChoiceResolved = false;
	BuildInfiniteOpening();
	ShowInfiniteNarrative();
}

void AAscendPlayerController::BuildInfiniteOpening()
{
	FString EffectiveWorldBook = InfiniteNarrativeSettings.WorldBookOverride;
	if (EffectiveWorldBook.IsEmpty()) EffectiveWorldBook = LoadRPDataFile(TEXT("Data/rp_worldbook.json"));
	FRPOpeningPreset Opening = ParseOpeningPreset(EffectiveWorldBook);
	if (!InfiniteNarrativeSettings.OpeningTitleOverride.IsEmpty()) Opening.Title = InfiniteNarrativeSettings.OpeningTitleOverride;
	if (!InfiniteNarrativeSettings.OpeningSpeakerOverride.IsEmpty()) Opening.Speaker = InfiniteNarrativeSettings.OpeningSpeakerOverride;
	if (!InfiniteNarrativeSettings.OpeningPortraitOverride.IsEmpty()) Opening.PortraitId = InfiniteNarrativeSettings.OpeningPortraitOverride;
	if (!InfiniteNarrativeSettings.OpeningExpressionOverride.IsEmpty()) Opening.Expression = InfiniteNarrativeSettings.OpeningExpressionOverride;
	if (!InfiniteNarrativeSettings.OpeningNarrationOverride.IsEmpty()) Opening.Narration = InfiniteNarrativeSettings.OpeningNarrationOverride;
	if (!InfiniteNarrativeSettings.OpeningDialogueOverride.IsEmpty()) Opening.Dialogue = InfiniteNarrativeSettings.OpeningDialogueOverride;

	CurrentInfiniteBeat = FInfiniteNarrativeBeat();
	CurrentInfiniteBeat.Title = Opening.Title;
	CurrentInfiniteBeat.Speaker = Opening.Speaker;
	CurrentInfiniteBeat.PortraitId = Opening.PortraitId;
	CurrentInfiniteBeat.Expression = Opening.Expression;
	CurrentInfiniteBeat.Narration = Opening.Narration;
	CurrentInfiniteBeat.Dialogue = Opening.Dialogue;
	if (!Opening.Dialogue.IsEmpty())
	{
		FInfiniteDialogueLine Line;
		Line.Speaker = Opening.Speaker;
		Line.PortraitId = Opening.PortraitId;
		Line.Expression = Opening.Expression;
		Line.Text = Opening.Dialogue;
		CurrentInfiniteBeat.DialogueLines.Add(Line);
	}

	StartRelicChoices = Run->RollInitialRelicChoices(3);
	static const TCHAR* EnemyTemplates[] = {TEXT("wolf_demon"), TEXT("rogue_cultivator"), TEXT("corpse_puppet")};
	for (int32 Index = 0; Index < StartRelicChoices.Num() && Index < 3; ++Index)
	{
		const FRelicData* Relic = Run->GetRelicData(StartRelicChoices[Index]);
		if (!Relic) continue;
		FInfiniteNarrativeChoice Choice;
		const FString ChoiceTemplate = Opening.ChoiceTemplates[Index % Opening.ChoiceTemplates.Num()];
		Choice.Text = ChoiceTemplate.Replace(TEXT("{item}"), *Relic->Name).Replace(TEXT("%s"), *Relic->Name);
		Choice.Next = TEXT("combat");
		Choice.bGrantRewardBeforeCombat = true;
		Choice.ResultSummary = Opening.ResultTemplate.Replace(TEXT("{item}"), *Relic->Name).Replace(TEXT("%s"), *Relic->Name);
		Choice.Reward.RelicIds.Add(Relic->Id);
		Choice.Enemy.TemplateId = EnemyTemplates[Index];
		Choice.Enemy.Name = Opening.EnemyNames[Index % Opening.EnemyNames.Num()];
		Choice.Enemy.Story = TEXT("开场白中正在逼近、并最终触发战斗的威胁。");
		Choice.Enemy.HPScale = 0.92f + Index * 0.06f;
		Choice.Enemy.IntentScale = 0.95f + Index * 0.04f;
		CurrentInfiniteBeat.Choices.Add(Choice);
	}
	if (CurrentInfiniteBeat.Choices.Num() != 3)
	{
		FInfiniteNarrativeRequestContext Context;
		CurrentInfiniteBeat = UInfiniteNarrativeService::BuildFallbackBeat(Context, TEXT("初始法器不足"));
	}
}

FInfiniteNarrativeRequestContext AAscendPlayerController::BuildInfiniteNarrativeContext(
	const FString& FreeformAction, bool bCombatPrefetch, bool bAssumeVictoryWithoutLog) const
{
	FInfiniteNarrativeRequestContext Context;
	if (!Run) return Context;
	Context.Cycle = Run->State.InfiniteCycle;
	Context.HP = Run->State.HP;
	Context.MaxHP = Run->State.MaxHP;
	Context.Gold = Run->State.Gold;
	Context.DeckSize = Run->State.Deck.Num();
	for (const FDeckCard& Owned : Run->State.Deck)
	{
		if (!Owned.bUpgraded) ++Context.UpgradeableCardCount;
		if (const FCardData* Card = Run->GetCardData(Owned.CardId))
		{
			Context.AbilityNames.AddUnique(Card->Name);
			Context.CardNameToId.Add(Card->Name, Card->Id);
		}
	}
	for (const FString& RelicId : Run->State.RelicIds)
	{
		if (const FRelicData* Relic = Run->GetRelicData(RelicId))
		{
			Context.RelicNames.AddUnique(Relic->Name);
			Context.RelicNameToId.Add(Relic->Name, Relic->Id);
			if (Relic->Condition == TEXT("narrative_reward_luck"))
				Context.RouteRewardBias += FMath::Max(0.f, Relic->Modifier);
		}
	}
	Context.RouteRewardBias = FMath::Clamp(Context.RouteRewardBias, 0.f, 1.f);
	for (const FString& RelicId : Run->GetAvailableFixedNarrativeRelicIds())
	{
		if (const FRelicData* Relic = Run->GetRelicData(RelicId))
		{
			Context.AvailableFixedRelicIds.Add(RelicId);
			Context.FixedRelicIdToName.Add(RelicId, Relic->Name);
		}
	}
	// Runtime-authored library entries remain resolvable by display name even when the
	// current run does not own them. They are never exposed to the writer as definitions.
	for (const FCardData& Card : Run->GetDynamicCards())
		Context.CardNameToId.Add(Card.Name, Card.Id);
	for (const FRelicData& Relic : Run->GetDynamicRelics())
		Context.RelicNameToId.Add(Relic.Name, Relic.Id);
	Context.RecentHistory = Run->State.RPHistory;
	Context.RecentRawContext = Run->BuildRPRecentContext(InfiniteNarrativeSettings.RecentRawRounds,
		FMath::Max(2500, InfiniteNarrativeSettings.InputContextTokens));
	const int32 HistoryStart = InfiniteNarrativeSettings.RecentRawRounds <= 0 ? 0
		: FMath::Max(0, Run->State.RPRecentTurns.Num() - InfiniteNarrativeSettings.RecentRawRounds);
	for (int32 TurnIndex = HistoryStart; TurnIndex < Run->State.RPRecentTurns.Num(); ++TurnIndex)
	{
		const FRPNarrativeTurn& Turn = Run->State.RPRecentTurns[TurnIndex];
		FString AssistantText = FString::Printf(TEXT("[%s]\n%s"), *Turn.Title, *Turn.Narration);
		if (!Turn.Dialogue.IsEmpty()) AssistantText += TEXT("\n") + Turn.Dialogue;
		Context.ChatHistory.Add({TEXT("assistant"), AssistantText});
		FString UserText = FString::Printf(TEXT("玩家选择：%s\n即时发展：%s\n下一步：%s"),
			*Turn.ChoiceText, *Turn.ResultSummary, *Turn.Next);
		Context.ChatHistory.Add({TEXT("user"), UserText});
	}
	FString RecallQuery = FreeformAction;
	if (Run->State.RPRecentTurns.Num() > 0)
	{
		const FRPNarrativeTurn& LastTurn = Run->State.RPRecentTurns.Last();
		RecallQuery += TEXT(" ") + LastTurn.Title + TEXT(" ") + LastTurn.ChoiceText + TEXT(" ") + LastTurn.ResultSummary;
	}
	Context.RecalledMemoryContext = Run->BuildRPMemoryContext(RecallQuery,
		InfiniteNarrativeSettings.MemoryTokenBudget);
	Context.bCombatPrefetch = bCombatPrefetch;
	Context.bAssumeCombatVictoryWithoutLog = bAssumeVictoryWithoutLog;
	if (bCombatPrefetch)
	{
		TArray<FString> EnemyDescriptions;
		TArray<FString> EnemyNames;
		for (const FString& EnemyId : CurrentEncounter.EnemyIds)
		{
			if (const FEnemyData* Enemy = Run->GetEnemyData(EnemyId))
			{
				EnemyDescriptions.Add(FString::Printf(TEXT("%s（%s）：%s"), *Enemy->Name, *Enemy->Tier, *Enemy->Story));
				EnemyNames.AddUnique(Enemy->Name);
			}
		}
		Context.CombatSetup = EnemyDescriptions.Num() > 0
			? FString::Join(EnemyDescriptions, TEXT("\n")) : TEXT("敌人资料未登记");
		const FString ResolvedNames = EnemyNames.Num() > 0
			? FString::Join(EnemyNames, TEXT("、")) : TEXT("本场实际战斗对象");
		Context.CombatResolutionFact = FString::Printf(TEXT(
			"玩家已经战胜【%s】。这些具体敌人均已被击倒或击杀、失去继续战斗能力，"
			"本次遭遇已经彻底结束。旧历史中任何‘受伤’‘回防’‘战斗未定’‘仍在交战’"
			"的描述均已过期，以本条为准。不得让同一敌人实体再次起身、追击或成为下一场战斗对象；"
			"同类敌人只有在明确是新的个体或群体，并交代新的来源时才可再次出现。"), *ResolvedNames);
		if (!bAssumeVictoryWithoutLog)
		{
			Context.CombatDigest = Run->State.LastCombatDigest;
			if (CombatLogLines.Num() > 0)
				Context.CombatDigest += TEXT("\n\n[完整战斗日志]\n") + FString::Join(CombatLogLines, TEXT("\n"));
		}
	}
	else
	{
		Context.CombatDigest = Run->State.LastCombatDigest;
		Context.CombatResolutionFact = Run->State.LastResolvedEncounterFact;
	}
	Context.WorldStateJson = Run->State.RPWorldStateJson;
	Context.EngineVariableContext = Run->BuildRPVariableContext();
	for (int32 TurnIndex = Run->State.RPRecentTurns.Num() - 1; TurnIndex >= 0; --TurnIndex)
	{
		if (Run->State.RPRecentTurns[TurnIndex].Next.Equals(TEXT("combat"), ESearchCase::IgnoreCase)) break;
		++Context.NarrativeOnlyStreak;
	}
	Context.bAllowIncidentalEffect = Run->State.RPTurnSerial - Run->State.LastRPIncidentalTurn >= 4;
	Context.FreeformAction = FreeformAction;
	return Context;
}

void AAscendPlayerController::RequestNextInfiniteNarrative(const FString& FreeformAction)
{
	if (!Run || !Run->State.bInfiniteNarrativeMode || !Run->State.bRunActive || bInfiniteNarrativeRequestInFlight) return;
	if (!FreeformAction.IsEmpty()) Run->AddRPHistory(FString::Printf(TEXT("玩家自由行动：%s"), *FreeformAction));

	const FInfiniteNarrativeRequestContext Context = BuildInfiniteNarrativeContext(FreeformAction);

	bInfiniteNarrativeRequestInFlight = true;
	bInputLocked = true;
	ResetInfiniteNarrativeStreamPreview();
	const FString LoadingMessage = FreeformAction.IsEmpty() ? TEXT("正在推演下一幕……") : TEXT("世界正在回应你的行动……");
	if (CurrentScreen == EGameScreen::InfiniteNarrative && !CurrentInfiniteBeat.Title.IsEmpty() && RPGenerationStatusText)
	{
		RPGenerationStatusText->SetText(FText::FromString(LoadingMessage + TEXT("  你可以继续阅读本轮内容。")));
		RPGenerationStatusText->SetVisibility(ESlateVisibility::HitTestInvisible);
	}
	else
	{
		ShowInfiniteNarrativeLoading(LoadingMessage);
	}
	InfiniteNarrativeService->Generate(InfiniteNarrativeSettings, Context,
		FOnInfiniteNarrativeReady::CreateWeakLambda(this,
			[this](bool bFromLLM, const FInfiniteNarrativeBeat& Beat)
			{
				HandleInfiniteNarrativeReady(bFromLLM, Beat);
			}),
		FOnInfiniteNarrativeStreamUpdate::CreateWeakLambda(this,
			[this](const FInfiniteNarrativeStreamUpdate& Update)
			{
				HandleInfiniteNarrativeStreamUpdate(Update, false);
			}));
}

void AAscendPlayerController::ResetInfiniteNarrativeStreamPreview()
{
	RPStreamingContainer = nullptr;
	RPStreamingDialogueBox = nullptr;
	RPStreamingTitleText = nullptr;
	RPStreamingNarrationText = nullptr;
	RPStreamingDialogueTexts.Reset();
	RPStreamingDialogueKeys.Reset();
}

void AAscendPlayerController::HandleInfiniteNarrativeStreamUpdate(
	const FInfiniteNarrativeStreamUpdate& Update, bool bCombatPrefetch)
{
	if (bCombatPrefetch)
	{
		CombatNarrativePrefetchStreamUpdate = Update;
		bHasCombatNarrativePrefetchStreamUpdate = true;
		if (!bWaitingForCombatNarrativeAfterReward) return;
	}
	RenderInfiniteNarrativeStreamPreview(Update);
}

void AAscendPlayerController::RenderInfiniteNarrativeStreamPreview(
	const FInfiniteNarrativeStreamUpdate& Update)
{
	FString StatusText;
	switch (Update.Stage)
	{
	case EInfiniteNarrativeStreamStage::Thinking:
		StatusText = TEXT("正在构思下一幕……");
		break;
	case EInfiniteNarrativeStreamStage::Writing:
		StatusText = TEXT("剧情正在生成……");
		break;
	case EInfiniteNarrativeStreamStage::Compiling:
		StatusText = TEXT("正在完成本轮选项与页面去向……");
		break;
	default:
		StatusText = TEXT("正在推演下一幕……");
		break;
	}
	if (RPGenerationStatusText)
	{
		RPGenerationStatusText->SetText(FText::FromString(StatusText));
		RPGenerationStatusText->SetVisibility(ESlateVisibility::HitTestInvisible);
	}
	if (!Update.bHasVisibleContent) return;

	bool bShouldAutoScroll = true;
	if (RPScrollBox)
	{
		bShouldAutoScroll = RPScrollBox->GetScrollOffsetOfEnd() - RPScrollBox->GetScrollOffset() < 140.f;
	}
	if (!RPContentBox || CurrentScreen != EGameScreen::InfiniteNarrative)
	{
		UScrollBox* Scroll = NewObject<UScrollBox>(RootWidget);
		UVerticalBox* Box = NewObject<UVerticalBox>(Scroll);
		Scroll->AddChild(Box);
		RPScrollBox = Scroll;
		RPContentBox = Box;
		RPAddPad(Box, 30.f);
		RPAddToVBox(Box, RPMakeWrappedText(Box, TEXT("天机流转"), 28,
			FAscendUIStyle::GoldYellow()), FMargin(40.f, 8.f));
		RPGenerationStatusText = RPMakeWrappedText(Box, StatusText, 15, FAscendUIStyle::DimGray());
		RPAddToVBox(Box, RPGenerationStatusText, FMargin(120.f, 8.f, 120.f, 12.f));
		SetScreen(Scroll, EGameScreen::InfiniteNarrative);
		bShouldAutoScroll = true;
	}

	if (!RPStreamingContainer)
	{
		RPStreamingContainer = NewObject<UVerticalBox>(RPContentBox);
		RPAddToVBox(RPStreamingContainer, RPMakeWrappedText(RPStreamingContainer,
			TEXT("—— 新的一幕 ——"), 14, FAscendUIStyle::DimGray()), FMargin(80.f, 20.f, 80.f, 8.f));
		RPStreamingTitleText = RPMakeWrappedText(RPStreamingContainer,
			Update.Preview.Title.IsEmpty() ? TEXT("下一幕") : Update.Preview.Title,
			32, FAscendUIStyle::GoldYellow());
		RPAddToVBox(RPStreamingContainer, RPStreamingTitleText, FMargin(24.f, 6.f));
		RPStreamingNarrationText = RPMakeWrappedText(RPStreamingContainer,
			Update.Preview.Narration, 19, FAscendUIStyle::PaperWhite());
		RPAddToVBox(RPStreamingContainer, RPStreamingNarrationText, FMargin(120.f, 10.f));
		RPStreamingDialogueBox = NewObject<UVerticalBox>(RPStreamingContainer);
		RPAddToVBox(RPStreamingContainer, RPStreamingDialogueBox, FMargin(8.f));
		RPAddToVBox(RPContentBox, RPStreamingContainer, FMargin(4.f));
		RPAddPad(RPContentBox, 48.f);
	}

	if (RPStreamingTitleText && !Update.Preview.Title.IsEmpty())
		RPStreamingTitleText->SetText(FText::FromString(Update.Preview.Title));
	if (RPStreamingNarrationText)
		RPStreamingNarrationText->SetText(FText::FromString(Update.Preview.Narration));

	TArray<FString> DialogueKeys;
	for (const FInfiniteDialogueLine& Line : Update.Preview.DialogueLines)
		DialogueKeys.Add(Line.Speaker + TEXT("\x1f") + Line.PortraitId + TEXT("\x1f") + Line.Expression);
	const bool bRebuildDialogue = DialogueKeys != RPStreamingDialogueKeys
		|| RPStreamingDialogueTexts.Num() != Update.Preview.DialogueLines.Num();
	if (bRebuildDialogue && RPStreamingDialogueBox)
	{
		RPStreamingDialogueBox->ClearChildren();
		RPStreamingDialogueTexts.Reset();
		RPStreamingDialogueKeys = DialogueKeys;
		for (const FInfiniteDialogueLine& Line : Update.Preview.DialogueLines)
		{
			UTextBlock* BodyText = nullptr;
			RPAddToVBox(RPStreamingDialogueBox, RPMakeDialogueBubble(RPStreamingDialogueBox, Line,
				InfiniteNarrativeSettings.bShowSpeakerPortrait,
				InfiniteNarrativeSettings.CharacterRegistryOverride, &BodyText), FMargin(90.f, 8.f));
			RPStreamingDialogueTexts.Add(BodyText);
		}
	}
	else
	{
		for (int32 Index = 0; Index < Update.Preview.DialogueLines.Num(); ++Index)
		{
			if (RPStreamingDialogueTexts.IsValidIndex(Index) && RPStreamingDialogueTexts[Index])
				RPStreamingDialogueTexts[Index]->SetText(FText::FromString(Update.Preview.DialogueLines[Index].Text));
		}
	}

	if (bShouldAutoScroll && RPScrollBox && GetWorld())
	{
		TWeakObjectPtr<UScrollBox> WeakScroll(RPScrollBox);
		GetWorld()->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this,
			[WeakScroll]()
			{
				if (WeakScroll.IsValid()) WeakScroll->ScrollToEnd();
			}));
	}
}

void AAscendPlayerController::ResetCombatNarrativePrefetch()
{
	bCombatNarrativePrefetchReady = false;
	bCombatNarrativePrefetchFailed = false;
	bDiscardCombatNarrativePrefetch = false;
	bWaitingForCombatNarrativeAfterReward = false;
	bCombatPrefetchIncludedLog = false;
	CombatNarrativePrefetchDiagnostic.Reset();
	CombatNarrativePrefetchedBeat = FInfiniteNarrativeBeat();
	CombatNarrativePrefetchStreamUpdate = FInfiniteNarrativeStreamUpdate();
	bHasCombatNarrativePrefetchStreamUpdate = false;
}

void AAscendPlayerController::StartCombatNarrativePrefetch(bool bIncludeCombatLog)
{
	if (!Run || !Run->State.bInfiniteNarrativeMode || !Run->State.bRunActive
		|| bInfiniteNarrativeRequestInFlight || bCombatNarrativePrefetchReady) return;

	bCombatNarrativePrefetchFailed = false;
	bDiscardCombatNarrativePrefetch = false;
	bCombatPrefetchIncludedLog = bIncludeCombatLog;
	CombatNarrativePrefetchDiagnostic.Reset();
	bHasCombatNarrativePrefetchStreamUpdate = false;
	CombatNarrativePrefetchStreamUpdate = FInfiniteNarrativeStreamUpdate();
	FInfiniteNarrativeRequestContext Context = BuildInfiniteNarrativeContext(
		TEXT(""), true, !bIncludeCombatLog);
	if (Combat)
	{
		Context.HP = Combat->Player.HP;
		Context.MaxHP = Combat->Player.MaxHP;
	}
	bInfiniteNarrativeRequestInFlight = true;
	UE_LOG(LogTemp, Display, TEXT("[InfiniteRP] combat prefetch start mode=%s cycle=%d log_lines=%d"),
		bIncludeCombatLog ? TEXT("A_after_combat_with_log") : TEXT("B_combat_start_assume_victory"),
		Run->State.InfiniteCycle, bIncludeCombatLog ? CombatLogLines.Num() : 0);
	InfiniteNarrativeService->Generate(InfiniteNarrativeSettings, Context,
		FOnInfiniteNarrativeReady::CreateWeakLambda(this,
			[this](bool bFromLLM, const FInfiniteNarrativeBeat& Beat)
			{
				HandleCombatNarrativePrefetchReady(bFromLLM, Beat);
			}),
		FOnInfiniteNarrativeStreamUpdate::CreateWeakLambda(this,
			[this](const FInfiniteNarrativeStreamUpdate& Update)
			{
				HandleInfiniteNarrativeStreamUpdate(Update, true);
			}));
}

void AAscendPlayerController::HandleCombatNarrativePrefetchReady(bool bFromLLM,
	const FInfiniteNarrativeBeat& Beat)
{
	bInfiniteNarrativeRequestInFlight = false;
	// The reward flow can leave the previous RP page visible while combat prefetch
	// runs in the background. Clear its loading label as soon as the request ends;
	// otherwise a successful prefetch still looks permanently stuck to the player.
	if (RPGenerationStatusText)
	{
		RPGenerationStatusText->SetVisibility(ESlateVisibility::Collapsed);
	}
	if (bDiscardCombatNarrativePrefetch || !Run || !Run->State.bRunActive)
	{
		UE_LOG(LogTemp, Display, TEXT("[InfiniteRP] discarded combat prefetch result"));
		return;
	}
	if (!bFromLLM || Beat.bError)
	{
		bCombatNarrativePrefetchFailed = true;
		CombatNarrativePrefetchDiagnostic = Beat.Diagnostic.IsEmpty()
			? TEXT("后台战后剧情生成失败") : Beat.Diagnostic;
		UE_LOG(LogTemp, Warning, TEXT("[InfiniteRP] combat prefetch failed: %s"),
			*CombatNarrativePrefetchDiagnostic);
		if (bWaitingForCombatNarrativeAfterReward)
			ShowInfiniteNarrativeError(CombatNarrativePrefetchDiagnostic);
		return;
	}
	bCombatNarrativePrefetchReady = true;
	CombatNarrativePrefetchedBeat = Beat;
	UE_LOG(LogTemp, Display, TEXT("[InfiniteRP] combat prefetch ready mode=%s"),
		bCombatPrefetchIncludedLog ? TEXT("A") : TEXT("B"));
	if (bWaitingForCombatNarrativeAfterReward) ConsumeCombatNarrativePrefetch();
}

void AAscendPlayerController::ConsumeCombatNarrativePrefetch()
{
	if (!bCombatNarrativePrefetchReady) return;
	const FInfiniteNarrativeBeat Beat = CombatNarrativePrefetchedBeat;
	bCombatNarrativePrefetchReady = false;
	bWaitingForCombatNarrativeAfterReward = false;
	CombatNarrativePrefetchedBeat = FInfiniteNarrativeBeat();
	HandleInfiniteNarrativeReady(true, Beat);
}

void AAscendPlayerController::HandleInfiniteNarrativeReady(bool bFromLLM, const FInfiniteNarrativeBeat& Beat)
{
	bInfiniteNarrativeRequestInFlight = false;
	bInputLocked = false;
	CurrentInfiniteBeat = Beat;
	bInfiniteChoiceResolved = false;
	if (!bFromLLM || Beat.bError)
	{
		ResetInfiniteNarrativeStreamPreview();
		if (Run) Run->SaveRun();
		UE_LOG(LogTemp, Warning, TEXT("[InfiniteRP] beat failed diagnostic=%s"), *Beat.Diagnostic);
		ShowInfiniteNarrativeError(Beat.Diagnostic);
		return;
	}
	if (Run)
	{
		FString PatchError;
		if (!Run->ApplyRPWorldStatePatchTransactional(Beat.StatePatchJson, PatchError))
		{
			// State updates are a best-effort side channel. Keep the already generated
			// scene and discard only the malformed patch instead of rolling back RP.
			UE_LOG(LogTemp, Warning, TEXT("[InfiniteRP] ignored state patch commit failure: %s"), *PatchError);
			CurrentInfiniteBeat.StatePatchJson = TEXT("{}");
			if (!CurrentInfiniteBeat.Diagnostic.IsEmpty()) CurrentInfiniteBeat.Diagnostic += TEXT("；");
			CurrentInfiniteBeat.Diagnostic += TEXT("本轮状态补丁未应用，但剧情已保留");
		}
		// 本轮生成已经读取过战斗摘要；后续连续对话依赖本轮原文与结构化记忆，不重复注入旧日志。
		Run->State.LastCombatDigest.Reset();
		Run->SaveRun();
	}
	UE_LOG(LogTemp, Display, TEXT("[InfiniteRP] beat ready source=llm diagnostic=%s"), *Beat.Diagnostic);
	ShowInfiniteNarrative();
}

void AAscendPlayerController::ShowInfiniteNarrativeLoading(const FString& Message)
{
	ResetInfiniteNarrativeStreamPreview();
	RPScrollBox = nullptr;
	RPContentBox = nullptr;
	RPGenerationStatusText = nullptr;
	UVerticalBox* Box = NewObject<UVerticalBox>(RootWidget);
	RPAddPad(Box, 220.f);
	RPAddToVBox(Box, RPMakeWrappedText(Box, TEXT("天机流转"), 38, FAscendUIStyle::GoldYellow()));
	RPAddToVBox(Box, RPMakeWrappedText(Box, Message, 20, FAscendUIStyle::PaperWhite()), FMargin(100.f, 20.f));
	RPAddToVBox(Box, RPMakeWrappedText(Box,
		TEXT("若接口超时或返回格式不合格，你可以重试，或保存当前进度返回标题。"),
		14, FAscendUIStyle::DimGray()), FMargin(120.f, 10.f));
	SetScreen(Box, EGameScreen::InfiniteNarrative);
}

void AAscendPlayerController::ShowInfiniteNarrativeError(const FString& Diagnostic)
{
	ResetInfiniteNarrativeStreamPreview();
	RPScrollBox = nullptr;
	RPContentBox = nullptr;
	RPGenerationStatusText = nullptr;
	UVerticalBox* Box = NewObject<UVerticalBox>(RootWidget);
	RPAddPad(Box, 180.f);
	RPAddToVBox(Box, RPMakeWrappedText(Box, TEXT("天 机 暂 断"), 38, FAscendUIStyle::BloodRed()));
	RPAddToVBox(Box, RPMakeWrappedText(Box,
		TEXT("本次剧情导演调用没有完整结束。游戏没有推进剧情，也没有套用本地替代剧情。"),
		20, FAscendUIStyle::PaperWhite()), FMargin(120.f, 22.f, 120.f, 8.f));
	RPAddToVBox(Box, RPMakeWrappedText(Box, Diagnostic.IsEmpty() ? TEXT("未知连接错误") : Diagnostic,
		14, FAscendUIStyle::DimGray()), FMargin(150.f, 4.f, 150.f, 24.f));

	UHorizontalBox* Row = NewObject<UHorizontalBox>(Box);
	Row->AddChildToHorizontalBox(NewObject<USpacer>(Row))->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	RPAddToHBox(Row, MakeLinkedButton(Row, TEXT("【重试本幕】"), TEXT("rp_retry"), 0, 21), FMargin(12.f, 4.f));
	RPAddToHBox(Row, MakeLinkedButton(Row, TEXT("【保存进度并返回标题】"), TEXT("rp_error_title"), 0, 19), FMargin(12.f, 4.f));
	Row->AddChildToHorizontalBox(NewObject<USpacer>(Row))->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	RPAddToVBox(Box, Row);
	SetScreen(Box, EGameScreen::InfiniteNarrative);
}

void AAscendPlayerController::ShowInfiniteNarrative()
{
	ResetInfiniteNarrativeStreamPreview();
	UScrollBox* Scroll = NewObject<UScrollBox>(RootWidget);
	UVerticalBox* Box = NewObject<UVerticalBox>(Scroll);
	Scroll->AddChild(Box);
	RPScrollBox = Scroll;
	RPContentBox = Box;
	RPGenerationStatusText = nullptr;
	RPAddPad(Box, 28.f);

	const FString Status = Run ? FString::Printf(TEXT("第 %d 轮后 · 气血 %d/%d · 灵石 %d · 卡组 %d · 法宝 %d"),
		Run->State.InfiniteCycle, Run->State.HP, Run->State.MaxHP, Run->State.Gold,
		Run->State.Deck.Num(), Run->State.RelicIds.Num()) : TEXT("");
	RPAddToVBox(Box, RPMakeWrappedText(Box, Status, 14, FAscendUIStyle::DimGray()));

	RPAddToVBox(Box, RPMakeWrappedText(Box, CurrentInfiniteBeat.Title, 34,
		FAscendUIStyle::GoldYellow()), FMargin(20.f, 12.f, 20.f, 4.f));

	if (CurrentInfiniteBeat.bFallback && !CurrentInfiniteBeat.Diagnostic.IsEmpty())
	{
		RPAddToVBox(Box, RPMakeWrappedText(Box,
			FString::Printf(TEXT("本轮使用保守结算 · %s"), *CurrentInfiniteBeat.Diagnostic), 12,
			FAscendUIStyle::DimGray()), FMargin(120.f, 2.f));
	}
	RPAddPad(Box, 16.f);
	RPAddToVBox(Box, RPMakeWrappedText(Box, CurrentInfiniteBeat.Narration, 19, FAscendUIStyle::PaperWhite()),
		FMargin(120.f, 12.f));
	TArray<FInfiniteDialogueLine> DialogueLines = CurrentInfiniteBeat.DialogueLines;
	if (DialogueLines.Num() == 0 && !CurrentInfiniteBeat.Dialogue.IsEmpty())
	{
		FInfiniteDialogueLine LegacyLine;
		LegacyLine.Speaker = CurrentInfiniteBeat.Speaker;
		LegacyLine.PortraitId = CurrentInfiniteBeat.PortraitId;
		LegacyLine.Expression = CurrentInfiniteBeat.Expression;
		LegacyLine.Text = CurrentInfiniteBeat.Dialogue;
		DialogueLines.Add(LegacyLine);
	}
	for (FInfiniteDialogueLine& Line : DialogueLines)
	{
		Line.Text = Line.Text.Replace(TEXT("“"), TEXT("")).Replace(TEXT("”"), TEXT(""));
		RPAddToVBox(Box, RPMakeDialogueBubble(Box, Line, InfiniteNarrativeSettings.bShowSpeakerPortrait,
			InfiniteNarrativeSettings.CharacterRegistryOverride), FMargin(90.f, 8.f));
	}
	RPAddPad(Box, 18.f);

	if (bInfiniteChoiceResolved)
	{
		UBorder* Resolution = NewObject<UBorder>(Box);
		if (UTexture2D* BubbleTexture = FAscendArt::GetTexture(Resolution, TEXT("Art/ui/card_rules_panel_v2.png")))
		{
			FSlateBrush BubbleBrush;
			BubbleBrush.SetResourceObject(BubbleTexture);
			BubbleBrush.DrawAs = ESlateBrushDrawType::Box;
			BubbleBrush.Margin = FMargin(0.07f, 0.12f);
			BubbleBrush.ImageSize = FVector2D(1525.f, 783.f);
			Resolution->SetBrush(BubbleBrush);
			Resolution->SetBrushColor(FLinearColor::White);
		}
		else
		{
			Resolution->SetBrushColor(FLinearColor(0.055f, 0.075f, 0.08f, 0.94f));
		}
		Resolution->SetPadding(FMargin(42.f, 26.f));
		UVerticalBox* ResultBox = NewObject<UVerticalBox>(Resolution);
		RPAddToVBox(ResultBox, RPMakeWrappedText(ResultBox,
			FString::Printf(TEXT("你选择了：%s"), *PendingInfiniteChoice.Text), 17,
			FAscendUIStyle::GoldYellow(), ETextJustify::Left));
		if (!PendingInfiniteChoice.ResultSummary.IsEmpty())
			RPAddToVBox(ResultBox, RPMakeWrappedText(ResultBox, PendingInfiniteChoice.ResultSummary, 19,
				FAscendUIStyle::PaperWhite(), ETextJustify::Left), FMargin(4.f, 10.f));
		const bool bCombatQueued = PendingInfiniteEncounter.EnemyIds.Num() > 0;
		if (bCombatQueued)
		{
			FString EnemyName = PendingInfiniteChoice.Enemy.Name;
			FString EnemyStory = PendingInfiniteChoice.Enemy.Story;
			if (Run && PendingInfiniteEncounter.EnemyIds.Num() > 0)
			{
				if (const FEnemyData* Enemy = Run->GetEnemyData(PendingInfiniteEncounter.EnemyIds[0]))
				{
					if (EnemyName.IsEmpty()) EnemyName = Enemy->Name;
					if (EnemyStory.IsEmpty()) EnemyStory = Enemy->Story;
				}
			}
			if (!EnemyName.IsEmpty())
				RPAddToVBox(ResultBox, RPMakeWrappedText(ResultBox, TEXT("来敌 · ") + EnemyName, 18,
					FAscendUIStyle::BloodRed(), ETextJustify::Left), FMargin(4.f, 12.f, 4.f, 2.f));
			if (!EnemyStory.IsEmpty())
				RPAddToVBox(ResultBox, RPMakeWrappedText(ResultBox, EnemyStory, 16,
					FAscendUIStyle::DimGray(), ETextJustify::Left), FMargin(4.f, 2.f, 4.f, 6.f));
		}
		const FString Destination = DescribeChoiceDestination(PendingInfiniteChoice);
		if (!Destination.IsEmpty())
		{
			RPAddToVBox(ResultBox, RPMakeWrappedText(ResultBox, TEXT("接下来 · ") + Destination, 17,
				FAscendUIStyle::GoldYellow(), ETextJustify::Left), FMargin(4.f, 10.f, 4.f, 4.f));
		}

		TArray<FString> Consequences;
		if (PendingInfiniteChoice.Reward.HPChange != 0)
			Consequences.Add(FString::Printf(TEXT("气血 %+d"), PendingInfiniteChoice.Reward.HPChange));
		if (PendingInfiniteChoice.Reward.GoldChange != 0)
			Consequences.Add(FString::Printf(TEXT("灵石 %+d"), PendingInfiniteChoice.Reward.GoldChange));
		for (const FString& Name : PendingNarrativeLostCards) Consequences.Add(TEXT("失去卡牌【") + Name + TEXT("】"));
		for (const FString& Name : PendingNarrativeLostRelics) Consequences.Add(TEXT("失去法宝【") + Name + TEXT("】"));
		Consequences.Append(PendingNarrativeVariableReceipts);
		if (Consequences.Num() > 0)
			RPAddToVBox(ResultBox, RPMakeWrappedText(ResultBox, FString::Join(Consequences, TEXT("　")), 17,
				(PendingInfiniteChoice.Reward.HPChange < 0 || PendingNarrativeLostCards.Num() + PendingNarrativeLostRelics.Num() > 0)
					? FAscendUIStyle::BloodRed() : FAscendUIStyle::JadeGreen()), FMargin(4.f, 7.f));

		if (PendingNarrativeCards.Num() + PendingNarrativeRelics.Num() > 0)
		{
			RPAddToVBox(ResultBox, RPMakeWrappedText(ResultBox, TEXT("本轮获得"), 18,
				FAscendUIStyle::GoldYellow()), FMargin(4.f, 10.f, 4.f, 4.f));
			UHorizontalBox* Items = NewObject<UHorizontalBox>(ResultBox);
			Items->AddChildToHorizontalBox(NewObject<USpacer>(Items))->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			for (const FString& RelicId : PendingNarrativeRelics)
				if (const FRelicData* Relic = Run ? Run->GetRelicData(RelicId) : nullptr)
					RPAddToHBox(Items, MakeRelicCardContentFromData(Items, *Relic, 1.20f), FMargin(12.f, 4.f));
			for (const FDeckCard& DeckCard : PendingNarrativeCards)
				if (const FCardData* Card = Run ? Run->GetCardData(DeckCard.CardId) : nullptr)
					RPAddToHBox(Items, MakeCardContentFromData(Items, *Card, DeckCard.bUpgraded, 1.16f), FMargin(12.f, 4.f));
			Items->AddChildToHorizontalBox(NewObject<USpacer>(Items))->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			RPAddToVBox(ResultBox, Items);
		}
		Resolution->SetContent(ResultBox);
		RPAddToVBox(Box, Resolution, FMargin(120.f, 8.f));

		UHorizontalBox* NextRow = NewObject<UHorizontalBox>(Box);
		NextRow->AddChildToHorizontalBox(NewObject<USpacer>(NextRow))->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		RPAddToHBox(NextRow, MakeLinkedButton(NextRow,
			ChoiceDestinationButtonLabel(PendingInfiniteChoice), TEXT("rp_resolution_next"), 0, 21), FMargin(8.f, 10.f));
		NextRow->AddChildToHorizontalBox(NewObject<USpacer>(NextRow))->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		RPAddToVBox(Box, NextRow);
	}
	else
	{
		for (int32 Index = 0; Index < CurrentInfiniteBeat.Choices.Num(); ++Index)
		{
			FString DisabledReason;
			const bool bAvailable = IsInfiniteChoiceAvailable(CurrentInfiniteBeat.Choices[Index], DisabledReason);
			const FString ChoiceLabel = bAvailable
				? CurrentInfiniteBeat.Choices[Index].Text
				: FString::Printf(TEXT("%s（%s）"), *CurrentInfiniteBeat.Choices[Index].Text, *DisabledReason);
			UHorizontalBox* Row = NewObject<UHorizontalBox>(Box);
			Row->AddChildToHorizontalBox(NewObject<USpacer>(Row))->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			UButton* ChoiceButton = MakeLinkedButton(Row, ChoiceLabel, TEXT("rp_choice"), Index, 18);
			RPConfigureAdaptiveChoiceButton(ChoiceButton, ChoiceLabel, 18);
			ChoiceButton->SetIsEnabled(bAvailable);
			RPAddToHBox(Row, ChoiceButton, FMargin(8.f, 6.f));
			Row->AddChildToHorizontalBox(NewObject<USpacer>(Row))->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			RPAddToVBox(Box, Row);
		}

		RPFreeformInput = nullptr;
		if (InfiniteNarrativeSettings.bShowFreeformInput)
		{
			RPAddPad(Box, 18.f);
			RPFreeformInput = NewObject<UEditableTextBox>(Box);
			RPFreeformInput->SetHintText(FText::FromString(TEXT("自由描述你的行动；提交后世界会直接回应……")));
			RPAddToVBox(Box, RPFreeformInput, FMargin(160.f, 5.f));
			UHorizontalBox* SubmitRow = NewObject<UHorizontalBox>(Box);
			SubmitRow->AddChildToHorizontalBox(NewObject<USpacer>(SubmitRow))->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			RPAddToHBox(SubmitRow, MakeLinkedButton(SubmitRow, TEXT("【自由行动】"), TEXT("rp_freeform"), 0, 17));
			SubmitRow->AddChildToHorizontalBox(NewObject<USpacer>(SubmitRow))->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			RPAddToVBox(Box, SubmitRow);
		}
	}

	// 常驻牌组整理与本轮选项无关；只要仍在 RP 阶段就始终可进入。
	UHorizontalBox* DeckToolsRow = NewObject<UHorizontalBox>(Box);
	DeckToolsRow->AddChildToHorizontalBox(NewObject<USpacer>(DeckToolsRow))->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	UButton* DeckRemoveButton = MakeLinkedButton(DeckToolsRow,
		TEXT("【牌组整理 · 删牌 50 灵石】"), TEXT("rp_paid_remove_open"), 0, 16);
	DeckRemoveButton->SetIsEnabled(!bInfiniteNarrativeRequestInFlight);
	RPAddToHBox(DeckToolsRow, DeckRemoveButton, FMargin(8.f, 14.f, 8.f, 2.f));
	DeckToolsRow->AddChildToHorizontalBox(NewObject<USpacer>(DeckToolsRow))->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	RPAddToVBox(Box, DeckToolsRow);
	RPGenerationStatusText = RPMakeWrappedText(Box,
		TEXT("正在推演下一幕……  你可以继续阅读本轮内容。"), 15, FAscendUIStyle::DimGray());
	RPGenerationStatusText->SetVisibility(bInfiniteNarrativeRequestInFlight
		? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	RPAddToVBox(Box, RPGenerationStatusText, FMargin(120.f, 18.f));
	RPAddPad(Box, 45.f);
	SetScreen(Scroll, EGameScreen::InfiniteNarrative);
	if (bInfiniteChoiceResolved && GetWorld())
	{
		TWeakObjectPtr<UScrollBox> WeakScroll(Scroll);
		GetWorld()->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this,
			[WeakScroll]()
			{
				if (WeakScroll.IsValid()) WeakScroll->ScrollToEnd();
			}));
	}
}

void AAscendPlayerController::ShowInfinitePaidDeckRemoval()
{
	if (!Run) return;
	UScrollBox* Scroll = NewObject<UScrollBox>(RootWidget);
	UVerticalBox* Box = NewObject<UVerticalBox>(Scroll);
	Scroll->AddChild(Box);
	RPAddPad(Box, 34.f);
	RPAddToVBox(Box, RPMakeWrappedText(Box, TEXT("牌组整理"), 32, FAscendUIStyle::GoldYellow()),
		FMargin(30.f, 10.f));
	RPAddToVBox(Box, RPMakeWrappedText(Box,
		FString::Printf(TEXT("每删除一张牌消耗 50 灵石。可连续删除，无次数与最小卡组限制。\n当前：%d 灵石 · %d 张牌"),
			Run->State.Gold, Run->State.Deck.Num()), 16, FAscendUIStyle::DimGray()),
		FMargin(60.f, 2.f, 60.f, 18.f));

	UHorizontalBox* Row = nullptr;
	int32 Visible = 0;
	for (int32 DeckIndex = 0; DeckIndex < Run->State.Deck.Num(); ++DeckIndex)
	{
		const FDeckCard& DeckCard = Run->State.Deck[DeckIndex];
		const FCardData* Card = Run->GetCardData(DeckCard.CardId);
		if (!Card) continue;
		if (Visible % 3 == 0)
		{
			Row = NewObject<UHorizontalBox>(Box);
			Row->AddChildToHorizontalBox(NewObject<USpacer>(Row))->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			RPAddToVBox(Box, Row, FMargin(8.f, 5.f));
		}
		UButton* Button = NewObject<UButton>(Row);
		Button->SetBackgroundColor(FLinearColor::Transparent);
		Button->SetContent(MakeCardContentFromData(Button, *Card, DeckCard.bUpgraded, 0.84f));
		Button->SetIsEnabled(Run->State.Gold >= 50);
		UClickProxy* Proxy = NewObject<UClickProxy>(Button);
		Proxy->Tag = TEXT("rp_paid_remove_card");
		Proxy->Index = DeckIndex;
		Proxy->Owner = this;
		Button->OnClicked.AddDynamic(Proxy, &UClickProxy::HandleClick);
		PendingScreenProxies.Add(Proxy);
		RPAddToHBox(Row, Button, FMargin(8.f, 2.f));
		++Visible;
		if (Visible % 3 == 0)
			Row->AddChildToHorizontalBox(NewObject<USpacer>(Row))->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	}
	if (Row && Visible % 3 != 0)
		Row->AddChildToHorizontalBox(NewObject<USpacer>(Row))->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	if (Visible == 0)
		RPAddToVBox(Box, RPMakeWrappedText(Box, TEXT("卡组已空。"), 18, FAscendUIStyle::DimGray()), FMargin(80.f, 20.f));
	else if (Run->State.Gold < 50)
		RPAddToVBox(Box, RPMakeWrappedText(Box, TEXT("灵石不足，需要 50 灵石才能删除一张牌。"),
			18, FAscendUIStyle::BloodRed()), FMargin(80.f, 20.f));
	RPAddToVBox(Box, MakeLinkedButton(Box, TEXT("【返回剧情】"), TEXT("rp_paid_remove_back"), 0, 18),
		FMargin(180.f, 20.f));
	RPAddPad(Box, 35.f);
	SetScreen(Scroll, EGameScreen::InfiniteNarrative);
}

bool AAscendPlayerController::IsInfiniteChoiceAvailable(const FInfiniteNarrativeChoice& Choice,
	FString& OutReason) const
{
	OutReason.Reset();
	if (!Run) { OutReason = TEXT("当前不可用"); return false; }
	if (Choice.Reward.GoldChange < 0 && Run->State.Gold < -Choice.Reward.GoldChange)
	{
		OutReason = TEXT("灵石不足");
		return false;
	}
	if (Choice.Reward.HPChange < 0 && Run->State.HP + Choice.Reward.HPChange < 1)
	{
		OutReason = TEXT("气血不足");
		return false;
	}
	for (const FInfiniteChoiceRequirement& Requirement : Choice.Requirements)
	{
		if (Requirement.Type == TEXT("gold_at_least") && Run->State.Gold < Requirement.Value)
		{
			OutReason = TEXT("灵石不足"); return false;
		}
		if (Requirement.Type == TEXT("hp_above") && Run->State.HP <= Requirement.Value)
		{
			OutReason = TEXT("气血不足"); return false;
		}
		if (Requirement.Type == TEXT("deck_at_least") && Run->State.Deck.Num() < Requirement.Value)
		{
			OutReason = TEXT("卡组不满足条件"); return false;
		}
	}
	for (const FInfiniteGameOperation& Operation : Choice.Operations)
	{
		if (Operation.Op == TEXT("choose_remove_card") && Run->State.Deck.Num() <= Operation.Count)
		{
			OutReason = TEXT("没有足够卡牌可剔除"); return false;
		}
		if (Operation.Op == TEXT("choose_upgrade_card"))
		{
			const bool bHasCandidate = Run->State.Deck.ContainsByPredicate([](const FDeckCard& Card)
				{ return !Card.bUpgraded; });
			if (!bHasCandidate) { OutReason = TEXT("没有可升级卡牌"); return false; }
		}
	}
	return true;
}

bool AAscendPlayerController::BeginInfiniteGameFunction()
{
	if (!Run) return false;
	while (NextInfiniteGameOperationIndex < PendingInfiniteChoice.Operations.Num())
	{
		const FInfiniteGameOperation Operation = PendingInfiniteChoice.Operations[NextInfiniteGameOperationIndex++];
		if (Operation.Op == TEXT("open_shop"))
		{
			ActiveInfiniteGameOperation = Operation;
			bInfiniteFunctionFlowActive = true;
			Run->GenerateNarrativeShopStock(Operation.Rarity.IsEmpty() ? TEXT("legendary") : Operation.Rarity,
				Operation.PriceMultiplier <= 0.f ? 1.5f : Operation.PriceMultiplier, 6);
			ShowShop();
			return true;
		}
		if (Operation.Op == TEXT("choose_remove_card") || Operation.Op == TEXT("choose_upgrade_card"))
		{
			ActiveInfiniteGameOperation = Operation;
			bInfiniteCardOperationUpgrade = Operation.Op == TEXT("choose_upgrade_card");
			bInfiniteFunctionFlowActive = true;
			ShowInfiniteCardOperation(bInfiniteCardOperationUpgrade, Operation);
			return true;
		}
		if (Operation.Op == TEXT("open_rest"))
		{
			ActiveInfiniteGameOperation = Operation;
			bInfiniteFunctionFlowActive = true;
			ShowRest();
			return true;
		}
		if (Operation.Op == TEXT("open_reward"))
		{
			ActiveInfiniteGameOperation = Operation;
			bInfiniteFunctionFlowActive = true;
			PendingReward = FCombatReward();
			for (int32 Index = 0; Index < 3; ++Index)
				PendingReward.CardChoices.Add(Run->RollRandomRewardCard(EMapNodeType::Elite));
			ShowReward();
			return true;
		}
	}
	return false;
}

void AAscendPlayerController::CompleteInfiniteGameFunction()
{
	if (!Run) return;
	bInfiniteFunctionFlowActive = false;
	bInfiniteCardOperationUpgrade = false;
	ActiveInfiniteGameOperation = FInfiniteGameOperation();
	if (BeginInfiniteGameFunction()) return;
	NextInfiniteGameOperationIndex = 0;
	// The operation screen is no longer interactive. Return to the resolved RP turn
	// before requesting the next one, so the player never remains on a stale card grid
	// during network generation and cannot click the same card repeatedly.
	bInfiniteChoiceResolved = true;
	Run->SaveRun();
	ShowInfiniteNarrative();
	RequestNextInfiniteNarrative();
}

void AAscendPlayerController::ShowInfiniteCardOperation(bool bUpgrade,
	const FInfiniteGameOperation& Operation)
{
	UScrollBox* Scroll = NewObject<UScrollBox>(RootWidget);
	UVerticalBox* Box = NewObject<UVerticalBox>(Scroll);
	Scroll->AddChild(Box);
	RPAddPad(Box, 34.f);
	RPAddToVBox(Box, RPMakeWrappedText(Box,
		bUpgrade ? TEXT("选择要升级的卡牌") : TEXT("选择要从卡组中剔除的卡牌"),
		32, bUpgrade ? FAscendUIStyle::JadeGreen() : FAscendUIStyle::BloodRed()), FMargin(30.f, 10.f));
	RPAddToVBox(Box, RPMakeWrappedText(Box,
		FString::Printf(TEXT("尚需选择 %d 张；完成后自动返回剧情。"), Operation.Count),
		16, FAscendUIStyle::DimGray()), FMargin(60.f, 2.f, 60.f, 18.f));

	UHorizontalBox* Row = nullptr;
	int32 Visible = 0;
	for (int32 DeckIndex = 0; DeckIndex < Run->State.Deck.Num(); ++DeckIndex)
	{
		const FDeckCard& DeckCard = Run->State.Deck[DeckIndex];
		const FCardData* Card = Run->GetCardData(DeckCard.CardId);
		if (!Card) continue;
		if (bUpgrade && DeckCard.bUpgraded) continue;
		if (!bUpgrade && !Operation.bAllowCurse && Card->Type == TEXT("curse")) continue;
		if (!bUpgrade && !Operation.bAllowStarter
			&& (DeckCard.CardId == TEXT("strike") || DeckCard.CardId == TEXT("defend")
				|| DeckCard.CardId == TEXT("one_sword"))) continue;
		if (Visible % 3 == 0)
		{
			Row = NewObject<UHorizontalBox>(Box);
			Row->AddChildToHorizontalBox(NewObject<USpacer>(Row))->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			RPAddToVBox(Box, Row, FMargin(8.f, 5.f));
		}
		UButton* Button = NewObject<UButton>(Row);
		Button->SetBackgroundColor(FLinearColor::Transparent);
		Button->SetContent(MakeCardContentFromData(Button, *Card, DeckCard.bUpgraded, 0.84f));
		UClickProxy* Proxy = NewObject<UClickProxy>(Button);
		Proxy->Tag = bUpgrade ? TEXT("rp_upgrade_card") : TEXT("rp_remove_card");
		Proxy->Index = DeckIndex;
		Proxy->Owner = this;
		Button->OnClicked.AddDynamic(Proxy, &UClickProxy::HandleClick);
		PendingScreenProxies.Add(Proxy);
		RPAddToHBox(Row, Button, FMargin(8.f, 2.f));
		++Visible;
		if (Visible % 3 == 0) Row->AddChildToHorizontalBox(NewObject<USpacer>(Row))->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	}
	if (Row && Visible % 3 != 0)
		Row->AddChildToHorizontalBox(NewObject<USpacer>(Row))->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	if (Visible == 0)
	{
		RPAddToVBox(Box, RPMakeWrappedText(Box, TEXT("没有符合条件的卡牌，本项操作已跳过。"),
			18, FAscendUIStyle::DimGray()), FMargin(80.f, 20.f));
		RPAddToVBox(Box, MakeLinkedButton(Box, TEXT("【返回剧情】"), TEXT("rp_function_complete"), 0, 18));
	}
	RPAddPad(Box, 35.f);
	SetScreen(Scroll, EGameScreen::InfiniteNarrative);
}

void AAscendPlayerController::SelectInfiniteNarrativeChoice(int32 ChoiceIndex)
{
	if (!Run || !CurrentInfiniteBeat.Choices.IsValidIndex(ChoiceIndex)) return;
	FString DisabledReason;
	if (!IsInfiniteChoiceAvailable(CurrentInfiniteBeat.Choices[ChoiceIndex], DisabledReason)) return;
	if (CurrentInfiniteBeat.Choices[ChoiceIndex].CardForgeJobs.Num() > 0)
	{
		// Card authoring is intentionally delayed until selection. The selected branch's
		// direct state and route stay pending while the script forge compiles only its card.
		PendingCardForgeChoiceIndex = ChoiceIndex;
		bInfiniteNarrativeRequestInFlight = true;
		bInputLocked = true;
		const FInfiniteCardForgeJob Job = CurrentInfiniteBeat.Choices[ChoiceIndex].CardForgeJobs[0];
		ShowInfiniteNarrativeLoading(TEXT("正在把本轮所得锻造成一张真正可运行的原创卡……"));
		InfiniteNarrativeService->ForgeCard(InfiniteNarrativeSettings, BuildInfiniteNarrativeContext(), Job,
			FOnInfiniteCardForgeReady::CreateUObject(this,
				&AAscendPlayerController::HandleInfiniteCardForgeReady));
		return;
	}
	PendingInfiniteChoice = CurrentInfiniteBeat.Choices[ChoiceIndex];
	PendingInfiniteEncounter = FNodeEncounter();
	PendingNarrativeCards.Reset();
	PendingNarrativeRelics.Reset();
	PendingNarrativeLostCards.Reset();
	PendingNarrativeLostRelics.Reset();
	PendingNarrativeVariableReceipts.Reset();
	NextInfiniteGameOperationIndex = 0;
	const bool bNewSettlement = Run->TryCommitNarrativeSettlement(PendingInfiniteChoice.SettlementKey);
	if (bNewSettlement && !PendingInfiniteChoice.StatePatchJson.IsEmpty()
		&& PendingInfiniteChoice.StatePatchJson != TEXT("{}"))
	{
		FString BranchPatchError;
		if (!Run->ApplyRPWorldStatePatchTransactional(PendingInfiniteChoice.StatePatchJson, BranchPatchError))
			UE_LOG(LogTemp, Warning, TEXT("[InfiniteRP] ignored selected branch state patch: %s"), *BranchPatchError);
	}
	if (!bNewSettlement)
	{
		PendingInfiniteChoice.Reward = FInfiniteNarrativeReward();
		PendingInfiniteChoice.Operations.Reset();
		UE_LOG(LogTemp, Display, TEXT("[InfiniteRP] duplicate settlement skipped: %s"),
			*PendingInfiniteChoice.SettlementKey);
	}
	if (bNewSettlement)
	{
		Run->AdvanceRPVariableDurations();
		Run->ApplyInfiniteVariableUpdates(PendingInfiniteChoice.VariableUpdates,
			PendingNarrativeVariableReceipts);
	}
	const int32 HPBeforeSettlement = Run->State.HP;
	const int32 GoldBeforeSettlement = Run->State.Gold;
	const int32 DeckBeforeSettlement = Run->State.Deck.Num();
	const bool bStartsCombat = PendingInfiniteChoice.Next.Equals(TEXT("combat"), ESearchCase::IgnoreCase);
	const bool bApplyRewardNow = !bStartsCombat || PendingInfiniteChoice.bGrantRewardBeforeCombat;
	if (bApplyRewardNow)
	{
		TSet<FString> PreviouslyOwnedRelics;
		for (const FString& OwnedRelicId : Run->State.RelicIds) PreviouslyOwnedRelics.Add(OwnedRelicId);
		TMap<FString, int32> PreviousCardCounts;
		for (const FDeckCard& OwnedCard : Run->State.Deck) ++PreviousCardCounts.FindOrAdd(OwnedCard.CardId);
		Run->ApplyInfiniteNarrativeReward(PendingInfiniteChoice.Reward);
		for (const FInfiniteRewardCard& RewardCard : PendingInfiniteChoice.Reward.Cards)
		{
			if (!Run->GetCardData(RewardCard.CardId)) continue;
			FDeckCard Card;
			Card.CardId = RewardCard.CardId;
			Card.bUpgraded = RewardCard.bUpgraded;
			PendingNarrativeCards.Add(Card);
		}
		for (const FString& RelicId : PendingInfiniteChoice.Reward.RelicIds)
		{
			if (Run->GetRelicData(RelicId) && !PreviouslyOwnedRelics.Contains(RelicId)) PendingNarrativeRelics.Add(RelicId);
		}
		for (const FString& CardId : PendingInfiniteChoice.Reward.RemovedCardIds)
		{
			const int32 Before = PreviousCardCounts.FindRef(CardId);
			int32 After = 0;
			for (const FDeckCard& Card : Run->State.Deck)
			{
				if (Card.CardId == CardId)
				{
					++After;
				}
			}
			if (After < Before)
			{
				const FCardData* Card = Run->GetCardData(CardId);
				PendingNarrativeLostCards.Add(Card ? Card->Name : CardId);
			}
		}
		for (const FString& RelicId : PendingInfiniteChoice.Reward.RemovedRelicIds)
		{
			if (PreviouslyOwnedRelics.Contains(RelicId) && !Run->State.RelicIds.Contains(RelicId))
			{
				const FRelicData* Relic = Run->GetRelicData(RelicId);
				PendingNarrativeLostRelics.Add(Relic ? Relic->Name : RelicId);
			}
		}
	}
	TArray<FString> RouteOperations;
	for (const FInfiniteGameOperation& Operation : PendingInfiniteChoice.Operations)
	{
		if (Operation.Op != TEXT("hp") && Operation.Op != TEXT("gold")
			&& Operation.Op != TEXT("grant_card") && Operation.Op != TEXT("grant_relic")
			&& Operation.Op != TEXT("remove_card") && Operation.Op != TEXT("remove_relic"))
			RouteOperations.Add(Operation.Op);
	}
	UE_LOG(LogTemp, Display, TEXT(
		"[InfiniteRP][Receipt] settlement=%s hp=%d->%d gold=%d->%d deck=%d->%d variables=%d route=%s next=%s"),
		*PendingInfiniteChoice.SettlementKey, HPBeforeSettlement, Run->State.HP,
		GoldBeforeSettlement, Run->State.Gold, DeckBeforeSettlement, Run->State.Deck.Num(),
		PendingNarrativeVariableReceipts.Num(),
		RouteOperations.Num() > 0 ? *FString::Join(RouteOperations, TEXT(",")) : TEXT("none"),
		bStartsCombat ? TEXT("combat") : TEXT("continue_rp"));

	Run->AddRPHistory(FString::Printf(TEXT("%s｜选择：%s｜即时发展：%s｜下一步：%s"), *CurrentInfiniteBeat.Title,
		*PendingInfiniteChoice.Text, *PendingInfiniteChoice.ResultSummary,
		bStartsCombat ? TEXT("进入战斗") : TEXT("继续对话")));
	Run->AddRPNarrativeTurn(CurrentInfiniteBeat.Title, CurrentInfiniteBeat.Speaker,
		CurrentInfiniteBeat.Narration, CurrentInfiniteBeat.Dialogue, PendingInfiniteChoice.Text,
		PendingInfiniteChoice.ResultSummary, PendingInfiniteChoice.Next, CurrentInfiniteBeat.MemoryJson,
		InfiniteNarrativeSettings.CompressAfterRounds, InfiniteNarrativeSettings.RecentRawRounds,
		InfiniteNarrativeSettings.UnsummarizedTokenThreshold);

	if (!bStartsCombat)
	{
		bInfiniteChoiceResolved = true;
		Run->SaveRun();
		ShowInfiniteNarrative();
		return;
	}

	const FString EnemyId = Run->RegisterInfiniteEnemy(PendingInfiniteChoice.Enemy, ChoiceIndex);
	if (EnemyId.IsEmpty())
	{
		RequestNextInfiniteNarrative();
		return;
	}
	EMapNodeType NodeType = EMapNodeType::Combat;
	if (PendingInfiniteChoice.Enemy.Tier == TEXT("elite")) NodeType = EMapNodeType::Elite;
	else if (PendingInfiniteChoice.Enemy.Tier == TEXT("boss")) NodeType = EMapNodeType::Boss;
	Run->PrepareInfiniteCombat(NodeType, {EnemyId});

	PendingInfiniteEncounter = FNodeEncounter();
	PendingInfiniteEncounter.Type = NodeType;
	PendingInfiniteEncounter.EnemyIds.Add(EnemyId);
	PendingInfiniteEncounter.EnemyLevel = 0;
	PendingInfiniteEncounter.EnemyHPBonus = Run->GetInfiniteEnemyHPBonus(PendingInfiniteChoice.Enemy.FactionId);
	PendingInfiniteEncounter.StoryText = PendingInfiniteChoice.Enemy.Story;
	if (const FEnemyData* Enemy = Run->GetEnemyData(EnemyId))
	{
		Run->SavePendingInfiniteCombat(NodeType, *Enemy, PendingInfiniteEncounter.EnemyHPBonus,
			PendingInfiniteChoice.ResultSummary,
			PendingNarrativeCards, PendingNarrativeRelics);
	}
	Run->SaveRun();
	// Every combat branch gets a short reveal before the encounter. This makes the
	// causal bridge and enemy origin readable even when no immediate item changes hands.
	bInfiniteChoiceResolved = true;
	ShowInfiniteNarrative();
}

void AAscendPlayerController::HandleInfiniteCardForgeReady(bool bSuccess, const FCardData& Card,
	const FString& Diagnostic)
{
	bInfiniteNarrativeRequestInFlight = false;
	bInputLocked = false;
	const int32 ChoiceIndex = PendingCardForgeChoiceIndex;
	PendingCardForgeChoiceIndex = INDEX_NONE;
	if (!CurrentInfiniteBeat.Choices.IsValidIndex(ChoiceIndex) || !Run) return;
	FInfiniteNarrativeChoice& Choice = CurrentInfiniteBeat.Choices[ChoiceIndex];
	Choice.CardForgeJobs.Reset();
	// The card-only stage has completed. Returning from it continues from the already
	// resolved RP branch; it must never route into the forge a second time.
	Choice.Next = TEXT("continue_rp");
	if (bSuccess && !Card.Id.IsEmpty())
	{
		Choice.Reward.CreatedCards.Add(Card);
		FInfiniteRewardCard RewardCard;
		RewardCard.CardId = Card.Id;
		Choice.Reward.Cards.Add(RewardCard);
		Choice.GmJudgement += FString::Printf(TEXT("；独立卡牌工坊已锻成【%s】"), *Card.Name);
	}
	else
	{
		// Never discard the already adjudicated item/state/route merely because the card
		// authoring service failed.  The capped failure is explicit in both log and history.
		Choice.GmJudgement += TEXT("；原创卡工坊达到重试上限，其他已裁决变化照常结算");
		Run->AddRPHistory(TEXT("原创卡工坊异常：") + Diagnostic);
		UE_LOG(LogTemp, Error, TEXT("[CardForge] exhausted retries; preserving branch mechanics: %s"), *Diagnostic);
	}
	SelectInfiniteNarrativeChoice(ChoiceIndex);
}

void AAscendPlayerController::ShowAcquiredItems()
{
	// 兼容旧存档和旧点击路径：获得物现在直接嵌在 RP 页面底部。
	bInfiniteChoiceResolved = true;
	if (CurrentInfiniteBeat.Title.IsEmpty())
	{
		CurrentInfiniteBeat.Title = TEXT("前尘已定");
		CurrentInfiniteBeat.Narration = PendingInfiniteChoice.ResultSummary;
	}
	ShowInfiniteNarrative();
}

void AAscendPlayerController::BeginPendingInfiniteCombat()
{
	if (!Run || PendingInfiniteEncounter.EnemyIds.Num() == 0) return;
	ResetCombatNarrativePrefetch();
	CurrentEncounter = PendingInfiniteEncounter;
	Combat = NewObject<UCombatEngine>(this);
	Combat->OnLog.AddDynamic(this, &AAscendPlayerController::OnCombatLogDynamic);
	RegisterEncounterRuntimeEnemies();
	CombatLogLines.Reset();
	LastProcessedLogIndex = 0;
	if (Combat->StartCombat(Run->State.Deck, CurrentEncounter.EnemyIds, Run->State.RelicIds,
		Run->State.MaxHP, Run->State.HP, CurrentEncounter.EnemyHPBonus,
		FMath::RandRange(1, 999999), CurrentEncounter.EnemyLevel))
	{
		PendingInfiniteEncounter = FNodeEncounter();
		ShowCombat();
		// 首场战斗固定模式 B；从第二场开始才读取用户的 A/B 设置。
		const bool bFirstInfiniteCombat = Run->State.InfiniteCycle <= 1;
		if (bFirstInfiniteCombat || !InfiniteNarrativeSettings.bGenerateAfterCombatWithLog)
			StartCombatNarrativePrefetch(false);
	}
	else
	{
		RequestNextInfiniteNarrative();
	}
}

void AAscendPlayerController::ContinueAfterReward()
{
	if (!Run) return;
	Run->SaveRun();
	if (Run->State.bInfiniteNarrativeMode && Run->State.bRunActive)
	{
		bWaitingForCombatNarrativeAfterReward = true;
		if (bCombatNarrativePrefetchReady)
		{
			ConsumeCombatNarrativePrefetch();
			return;
		}
		if (bCombatNarrativePrefetchFailed)
		{
			ShowInfiniteNarrativeError(CombatNarrativePrefetchDiagnostic);
			return;
		}
		// 安全兜底：模式 A 正常会在胜利瞬间启动；若因异常没有启动，现在携带战报补发。
		if (!bInfiniteNarrativeRequestInFlight) StartCombatNarrativePrefetch(true);
		ShowInfiniteNarrativeLoading(TEXT("战后剧情仍在生成中……"));
		if (bHasCombatNarrativePrefetchStreamUpdate)
			RenderInfiniteNarrativeStreamPreview(CombatNarrativePrefetchStreamUpdate);
		return;
	}
	if (Run->State.bRunVictory) ShowBreakthrough();
	else ShowMap();
}
