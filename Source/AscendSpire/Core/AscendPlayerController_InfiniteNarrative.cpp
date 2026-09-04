#include "AscendPlayerController.h"
#include "AscendAudioRouter.h"
#include "NarrativeContentLibrary.h"

#include "UI/AscendRootWidget.h"
#include "UI/AscendUIStyle.h"
#include "UI/AscendArt.h"
#include "UI/ClickProxy.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
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
#include "Components/ScrollBoxSlot.h"
#include "Components/Slider.h"
#include "Components/SizeBox.h"
#include "Components/Spacer.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/Texture2D.h"
#include "Framework/Application/SlateApplication.h"
#include "Fonts/FontMeasure.h"
#include "Rendering/SlateRenderer.h"
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
	// Measured from rp_scroll_vertical_v2.png (1086x1448): the opaque paper is
	// x=155..930 and y=120..1325. Keep these ratios in one place so the frame,
	// scrollbar and every RP state share the same safe geometry.
	constexpr float RPScrollPaperWidthRatio = (930.f - 155.f) / 1086.f;
	constexpr float RPScrollReadingInset = 28.f;
	constexpr float RPScrollArtworkAspect = 3.f / 4.f;
	constexpr float RPScrollViewportInset = 24.f;
	constexpr float RPDialogueBubbleBorder = 2.f;
	constexpr float RPDialogueBubbleInnerHorizontalPadding = 28.f;
	constexpr float RPDialogueBubbleInnerVerticalPadding = 24.f;
	// Bubble's vertical slot contributes 4px on each horizontal side. Keep this
	// explicit so glyph packing and the actual BodyBox never use different widths.
	constexpr float RPDialogueBubbleContentSlotHorizontalInset = 8.f;
	constexpr float RPScrollMaxReadingWidth = 1180.f;
	// RP body text owns its tracking and line rhythm.  Keep these values local to
	// the reading UI so card faces and combat HUD text retain their existing font
	// metrics.
	constexpr int32 RPBodyLetterSpacing = 30; // 0.03em in Slate's thousandths-of-em units.
	constexpr float RPBodyLineHeightMultiplier = 1.6f;
	constexpr float RPBodyParagraphGapMultiplier = 0.5f;

	float RPScrollReadingWidthForViewport(float ViewportWidth)
	{
		return FMath::Max(140.f, FMath::Min(RPScrollMaxReadingWidth, ViewportWidth * 0.85f));
	}

	float RPScrollPaperWidthForReading(float ReadingWidth)
	{
		return ReadingWidth + 2.f * RPScrollReadingInset;
	}

	float RPScrollArtworkWidthForReading(float ReadingWidth)
	{
		return FMath::Max(306.f,
			RPScrollPaperWidthForReading(ReadingWidth) / RPScrollPaperWidthRatio);
	}

	float RPDialogueBubbleTextWidth(float MaxRowWidth, bool bStackPortrait,
		bool bShowPortrait, float PortraitSize)
	{
		const float BubbleHorizontal = 2.f *
			(RPDialogueBubbleInnerHorizontalPadding + RPDialogueBubbleBorder);
		// Every horizontal slot is accounted for so the native panel, rather than
		// its old artwork, defines the actual text-safe width.
		const float RowInset = bStackPortrait
			? 4.f // CompactRoot's MessageSizer slot: 2px on each side.
			: 4.f; // MessageSizer's horizontal row slot padding.
		const float PortraitInset = bShowPortrait && !bStackPortrait
			? PortraitSize + 22.f // portrait slot padding (8 + 14)
			: 0.f;
		return FMath::Max(96.f, MaxRowWidth - BubbleHorizontal - RowInset - PortraitInset);
	}

	float RPDialogueBodyTextWidth(float MaxRowWidth, bool bStackPortrait,
		bool bShowPortrait, float PortraitSize)
	{
		const float TextWidth = FMath::Min(980.f,
			RPDialogueBubbleTextWidth(MaxRowWidth, bStackPortrait, bShowPortrait, PortraitSize));
		return FMath::Max(96.f,
			TextWidth - RPDialogueBubbleContentSlotHorizontalInset);
	}

	float RPChoiceTextWidth(float PaperWidth)
	{
		// card_rules_panel_v2 has ornamental curls in its outer 7% on both sides.
		// Reserve those pixels plus the button's normal 48px horizontal padding and
		// the row's 8px slots, so the label wraps inside the illustrated quiet area.
		const float OrnamentInset = FMath::Max(12.f, PaperWidth * 0.07f);
		constexpr float ButtonHorizontalPadding = 96.f;
		constexpr float RowPadding = 16.f;
		const float Available = PaperWidth - (OrnamentInset * 2.f)
			- ButtonHorizontalPadding - RowPadding;
		return FMath::Max(72.f, FMath::Min(720.f, Available));
	}

	FString GetNarrativeUserConfigPath()
	{
		// GameUserSettings.ini is owned and rewritten by UGameUserSettings, which drops
		// unrelated custom sections in packaged builds. Keep AI credentials and RP
		// preferences in a dedicated per-user file instead.
		return FPaths::ProjectSavedDir() / TEXT("Config/InfiniteNarrative.ini");
	}

	FString NormalizeCardForgeReasoningEffort(FString Effort)
	{
		Effort.TrimStartAndEndInline();
		Effort.ToLowerInline();
		Effort.ReplaceInline(TEXT("-"), TEXT("_"));
		if (Effort == TEXT("off") || Effort == TEXT("none")) Effort = TEXT("disabled");
		return Effort == TEXT("disabled") || Effort == TEXT("low") || Effort == TEXT("medium")
			|| Effort == TEXT("high") || Effort == TEXT("max") ? Effort : TEXT("disabled");
	}

	float CardForgeReasoningSliderValue(const FString& Effort)
	{
		const FString Normalized = NormalizeCardForgeReasoningEffort(Effort);
		if (Normalized == TEXT("disabled")) return 0.f;
		if (Normalized == TEXT("low")) return 0.25f;
		if (Normalized == TEXT("high")) return 0.75f;
		if (Normalized == TEXT("max")) return 1.f;
		return 0.5f;
	}

	FString CardForgeReasoningEffortFromSlider(float Value)
	{
		switch (FMath::Clamp(FMath::RoundToInt(Value * 4.f), 0, 4))
		{
		case 0: return TEXT("disabled");
		case 1: return TEXT("low");
		case 3: return TEXT("high");
		case 4: return TEXT("max");
		default: return TEXT("medium");
		}
	}

	FString CardForgeReasoningLabel(const FString& Effort)
	{
		const FString Normalized = NormalizeCardForgeReasoningEffort(Effort);
		if (Normalized == TEXT("disabled")) return TEXT("快速");
		if (Normalized == TEXT("low")) return TEXT("低");
		if (Normalized == TEXT("high")) return TEXT("高");
		if (Normalized == TEXT("max")) return TEXT("最大");
		return TEXT("中");
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

	void ConfigureAuthoredOpeningCombat(const TCHAR* OpeningId, FInfiniteNarrativeChoice& Choice)
	{
		Choice.Next = TEXT("combat");
		Choice.bGrantRewardBeforeCombat = true;
		// The three authored choices are three ways to arm the same opening scene.
		// Their enemy and consequence must therefore be identical within one beat.
		const FString OpeningKey(OpeningId);
		if (OpeningKey == TEXT("qinghe_spirit_stone"))
		{
			Choice.Enemy.TemplateId = TEXT("wolf_demon");
			Choice.Enemy.Name = TEXT("芦荡野狼精");
			Choice.Enemy.FactionId = TEXT("qinghe_reed_marsh");
			Choice.Enemy.Story = TEXT("灵石气息惊动了芦苇荡中吞过修士血肉的野狼精，它循着药钱与秘密追到渡口。");
			Choice.Enemy.HPScale = 0.98f;
			Choice.Enemy.IntentScale = 1.00f;
		}
		else if (OpeningKey == TEXT("lingpan_last_chime"))
		{
			Choice.Enemy.TemplateId = TEXT("corpse_puppet");
			Choice.Enemy.Name = TEXT("旧药圃尸傀");
			Choice.Enemy.FactionId = TEXT("qixia_old_garden");
			Choice.Enemy.Story = TEXT("你与桑晚刚踏进旧药圃，地窖里被遗弃的尸傀便被新入的灵息惊醒，挡住了唯一的退路。");
			Choice.Enemy.HPScale = 1.00f;
			Choice.Enemy.IntentScale = 0.98f;
		}
		else if (OpeningKey == TEXT("ancestral_house_lamp"))
		{
			Choice.Enemy.TemplateId = TEXT("mountain_imp");
			Choice.Enemy.Name = TEXT("河湾山魈");
			Choice.Enemy.FactionId = TEXT("linxi_riverbend");
			Choice.Enemy.Story = TEXT("离族驴车在河湾山道暂歇时，木匣泄出的旧灵息引来盘踞石缝的山魈，众人被迫在雨夜护住行李。");
			Choice.Enemy.HPScale = 1.02f;
			Choice.Enemy.IntentScale = 1.02f;
		}
		else if (OpeningKey == TEXT("third_furnace_watch"))
		{
			Choice.Enemy.TemplateId = TEXT("talisman_cultist");
			Choice.Enemy.Name = TEXT("焚账符修");
			Choice.Enemy.FactionId = TEXT("xuanwei_outer_court");
			Choice.Enemy.Story = TEXT("警钟响起后，潜伏在丹房的神霄弃徒现身灭口；他正是这口主炉被人动过手脚的源头。");
			Choice.Enemy.HPScale = 1.01f;
			Choice.Enemy.IntentScale = 1.03f;
		}
		else if (OpeningKey == TEXT("father_debt_box"))
		{
			Choice.Enemy.TemplateId = TEXT("rogue_cultivator");
			Choice.Enemy.Name = TEXT("独眼段九");
			Choice.Enemy.FactionId = TEXT("baishi_debt");
			Choice.Enemy.Story = TEXT("黑木匣打开的一刻，段九确认匣中确有他要找的旧物，撕毁血契的说辞随即变成了夺匣杀人。");
			Choice.Enemy.HPScale = 1.03f;
			Choice.Enemy.IntentScale = 1.04f;
		}
	}

	const TCHAR* AuthoredOpeningChoiceLead(const TCHAR* OpeningId)
	{
		const FString OpeningKey(OpeningId ? OpeningId : TEXT(""));
		if (OpeningKey == TEXT("qinghe_spirit_stone"))
			return TEXT("你把这件法器藏进油布，先去换药；芦苇荡的铃声却在身后停住。一路回望时，雨幕里已有东西循着灵息逼近。");
		if (OpeningKey == TEXT("lingpan_last_chime"))
			return TEXT("你把这件法器交给桑晚看过，带着它踏入旧药圃；地窖深处随即传来第一声闷响，像有什么在等新来的灵息。");
		if (OpeningKey == TEXT("ancestral_house_lamp"))
			return TEXT("你把这件法器收入行囊，跟着驴车离开祖祠；木匣的旧灵息在雨夜山道上泄出，石缝里很快亮起一双眼睛。");
		if (OpeningKey == TEXT("third_furnace_watch"))
			return TEXT("你把这件法器扣在掌中，先按下炉册上的犹疑并敲响警钟；丹房外有人应声，火光后藏着灭口的脚步。");
		if (OpeningKey == TEXT("father_debt_box"))
			return TEXT("你把这件法器贴身收好，伸手打开黑木匣；柜台对面的独眼忽然放下茶盏，十三年的旧账终于露出獠牙。");
		return TEXT("你把这件法器收好，沿着眼前唯一的退路迈出一步；前方的动静已经因灵息而起。");
	}

	FString AuthoredOpeningChoiceFallback(const TCHAR* OpeningId, int32 ChoiceIndex)
	{
		const FString OpeningKey(OpeningId ? OpeningId : TEXT(""));
		const int32 Index = FMath::Clamp(ChoiceIndex, 0, 2);
		if (OpeningKey == TEXT("qinghe_spirit_stone"))
		{
			static const TCHAR* Choices[] = {
				TEXT("你先沿渡口北侧查脚印，确认雨幕后的动静，再决定是否出门换药。"),
				TEXT("你先回头看住芦苇荡，贴着堤岸辨认铃声的来处，免得有人循迹逼近。"),
				TEXT("你先把随身东西藏妥，借油布遮住气息，随后再踏出这间屋子。")
			};
			return Choices[Index];
		}
		if (OpeningKey == TEXT("lingpan_last_chime"))
		{
			static const TCHAR* Choices[] = {
				TEXT("你先守住旧药圃的正门，等桑晚把地形看清，再一起向地窖靠近。"),
				TEXT("你先沿地窖外墙听动静，找出闷响的来处，免得一进门便断了退路。"),
				TEXT("你先替桑晚整理药圃里的落脚处，借杂役路线避开耳目，再去查那声闷响。")
			};
			return Choices[Index];
		}
		if (OpeningKey == TEXT("ancestral_house_lamp"))
		{
			static const TCHAR* Choices[] = {
				TEXT("你先随驴车离开祖祠，路上看住木匣，不让族中人追上来。"),
				TEXT("你先留在供桌前查清蜡封，听完陆青禾那一夜的说法，再决定怎样上路。"),
				TEXT("你先把木匣藏进车底，沿河湾小路绕开族中眼线，再护住自己的退路。")
			};
			return Choices[Index];
		}
		if (OpeningKey == TEXT("third_furnace_watch"))
		{
			static const TCHAR* Choices[] = {
				TEXT("你先敲响警钟，让第三班的人撤离炉房，再回头看裂纹有没有继续扩大。"),
				TEXT("你先沿炉册核对时辰，找出动过主炉的人影，再决定是否放寒髓砂。"),
				TEXT("你先把寒髓砂留在砖台，站到丹房侧门观察火光，等巡夜脚步露出破绽。")
			};
			return Choices[Index];
		}
		if (OpeningKey == TEXT("father_debt_box"))
		{
			static const TCHAR* Choices[] = {
				TEXT("你先把黑木匣移到柜台下，稳住段九的视线，再听瞎婆婆的竹杖暗号。"),
				TEXT("你先查旧契上的血印和红线，确认父亲留下的线索，再决定是否当众开匣。"),
				TEXT("你先绕到修伞铺一侧观察段九，留出退路后再伸手碰那只没有锁的木匣。")
			};
			return Choices[Index];
		}
		static const TCHAR* Choices[] = {
			TEXT("你先确认眼前的退路，听清近处动静，再决定怎样向前。"),
			TEXT("你先检查身边可用的掩护，辨认来路后再迈出下一步。"),
			TEXT("你先把随身东西安置妥当，留出转身的位置，再迎向前方的动静。")
		};
		return Choices[Index];
	}

	FInfiniteNarrativeChoice MakeAuthoredOpeningChoice(const TCHAR* OpeningId,
		const FRelicData& Relic, const TCHAR* ResultSummary)
	{
		FInfiniteNarrativeChoice Choice;
		// Before selection the button is intentionally lead-only. Relic identity and
		// mechanics remain engine-owned and are revealed only in the resolution panel.
		Choice.Text = AuthoredOpeningChoiceLead(OpeningId);
		Choice.ResultSummary = ResultSummary;
		Choice.ConsequenceIntent = ResultSummary;
		Choice.ResolvedImpactKind = TEXT("acquire_relic");
		Choice.ResolvedImpactSubject = Relic.Id;
		Choice.bResolvedImpactCompleted = true;
		// All three choices share one settlement key: choosing twice after a reload can
		// never grant another opening relic. The selected relic itself remains branch-local.
		Choice.SettlementKey = FString::Printf(TEXT("authored_opening_relic:%s"), OpeningId);
		Choice.StatePatchJson = TEXT("{}");
		if (!Relic.Id.IsEmpty()) Choice.Reward.RelicIds.AddUnique(Relic.Id);
		ConfigureAuthoredOpeningCombat(OpeningId, Choice);
		return Choice;
	}

	TArray<FRelicData> ResolveAuthoredOpeningRelics(URunManager* Run, TArray<FString>& OutIds)
	{
		OutIds.Reset();
		TArray<FRelicData> Result;
		if (!Run) return Result;

		// Roll through the original start-relic API first.  It already samples
		// without replacement; the extra pass below is deliberately defensive so
		// imported tables, stale save data, or a future API regression can never
		// surface an empty/duplicate option card.
		TArray<FString> CandidateIds = Run->RollInitialRelicChoices(3);
		// The normal pool is common relics. If an imported/custom data set leaves
		// fewer than three unowned commons, extend from every unowned fixed relic
		// without ever duplicating an ID or offering one already in the run.
		CandidateIds.Append(Run->GetAvailableFixedNarrativeRelicIds());
		TSet<FString> SeenIds;
		for (const FString& RelicId : CandidateIds)
		{
			if (OutIds.Num() >= 3) break;
			if (RelicId.IsEmpty() || SeenIds.Contains(RelicId) || Run->State.RelicIds.Contains(RelicId)) continue;
			const FRelicData* Relic = Run->GetRelicData(RelicId);
			if (!Relic || Relic->Id.IsEmpty() || Relic->Name.IsEmpty()) continue;
			SeenIds.Add(RelicId);
			OutIds.Add(RelicId);
			Result.Add(*Relic);
		}
		if (OutIds.Num() < 3)
		{
			UE_LOG(LogTemp, Warning, TEXT("[InfiniteRP] 起始法器安全回退后仍只有 %d/3 件有效法器"), OutIds.Num());
		}
		return Result;
	}

	/** Normalize transport/editor line endings before any incremental comparison.
	 * This keeps a CRLF split across provider chunks from becoming a visible extra
	 * line or an accidental duplicate character. */
	FString NormalizeRPLineEndings(FString Text)
	{
		Text.ReplaceInline(TEXT("\r\n"), TEXT("\n"));
		Text.ReplaceInline(TEXT("\r"), TEXT("\n"));
		return Text;
	}

	/** Display-only dialogue normalization. Provider quote wrappers are removed so
	 * the bubble does not render a second pair around engine-owned dialogue. */
	FString NormalizeRPDialogueText(FString Text)
	{
		Text = NormalizeRPLineEndings(MoveTemp(Text));
		Text.TrimStartAndEndInline();
		Text.ReplaceInline(TEXT("“"), TEXT(""));
		Text.ReplaceInline(TEXT("”"), TEXT(""));
		Text.ReplaceInline(TEXT("「"), TEXT(""));
		Text.ReplaceInline(TEXT("」"), TEXT(""));
		return Text;
	}

	FInfiniteDialogueLine MakeLegacyRPDialogueLine(const FInfiniteNarrativeBeat& Beat)
	{
		FInfiniteDialogueLine Line;
		Line.Speaker = Beat.Speaker;
		Line.PortraitId = Beat.PortraitId;
		Line.Expression = Beat.Expression;
		Line.Text = NormalizeRPDialogueText(Beat.Dialogue);
		const FString SpeakerPrefix = Beat.Speaker.IsEmpty()
			? FString() : Beat.Speaker + TEXT("：");
		if (!SpeakerPrefix.IsEmpty() && Line.Text.StartsWith(SpeakerPrefix))
			Line.Text.RightChopInline(SpeakerPrefix.Len());
		Line.Text.TrimStartAndEndInline();
		return Line;
	}

	FString FlattenRPDialogueForHistory(const FInfiniteNarrativeBeat& Beat)
	{
		if (!Beat.Dialogue.IsEmpty()) return NormalizeRPDialogueText(Beat.Dialogue);
		TArray<FString> Lines;
		for (const FInfiniteDialogueLine& Line : Beat.DialogueLines)
		{
			const FString Text = NormalizeRPDialogueText(Line.Text);
			if (!Text.IsEmpty())
				Lines.Add(Line.Speaker.IsEmpty() ? Text : Line.Speaker + TEXT("：") + Text);
		}
		return FString::Join(Lines, TEXT("\n"));
	}

	FString BuildCurrentRPSceneContext(const FInfiniteNarrativeBeat& Beat)
	{
		FString Result = TEXT("[当前页面中尚未提交的RP场景]\n");
		if (!Beat.Narration.IsEmpty()) Result += Beat.Narration;
		const FString Dialogue = FlattenRPDialogueForHistory(Beat);
		if (!Dialogue.IsEmpty()) Result += TEXT("\n") + Dialogue;
		return Result;
	}

	FString BuildFreeRPForcedDirection(const FInfiniteNarrativeChoice& Choice)
	{
		FString Direction = Choice.Text.TrimStartAndEnd();
		if (!Choice.ResultSummary.TrimStartAndEnd().IsEmpty())
			Direction += TEXT("\n因果方向：") + Choice.ResultSummary.TrimStartAndEnd();
		if (!Choice.ConsequenceIntent.TrimStartAndEnd().IsEmpty())
			Direction += TEXT("\n叙事意图：") + Choice.ConsequenceIntent.TrimStartAndEnd();
		Direction = Direction.Left(2400).TrimStartAndEnd();
		return Direction.IsEmpty()
			? TEXT("沿玩家刚才的自由行动产生的直接后果继续推进，不替玩家补写未发生的选择。")
			: Direction;
	}

	void NormalizeRPBeatForDisplay(FInfiniteNarrativeBeat& Beat)
	{
		Beat.Narration = NormalizeRPLineEndings(MoveTemp(Beat.Narration));
		Beat.Dialogue = NormalizeRPDialogueText(MoveTemp(Beat.Dialogue));
		for (FInfiniteDialogueLine& Line : Beat.DialogueLines)
			Line.Text = NormalizeRPDialogueText(MoveTemp(Line.Text));
		if (Beat.DialogueLines.Num() == 0 && !Beat.Dialogue.IsEmpty())
			Beat.DialogueLines.Add(MakeLegacyRPDialogueLine(Beat));
	}

	FString MakeRPDialogueIdentityKey(const FString& Speaker, const FString& PortraitId,
		const FString& Expression)
	{
		return Speaker + TEXT("\x1f") + PortraitId + TEXT("\x1f") + Expression;
	}

	bool MergeRPStreamText(const FString& ExistingText, const FString& IncomingText,
		FString& OutText, bool& bOutRebuilt)
	{
		const FString NormalizedExisting = NormalizeRPLineEndings(ExistingText);
		const FString NormalizedIncoming = NormalizeRPLineEndings(IncomingText);
		bOutRebuilt = false;
		if (NormalizedIncoming.IsEmpty())
		{
			OutText = NormalizedExisting;
			return true;
		}
		if (NormalizedExisting.IsEmpty() || NormalizedIncoming.StartsWith(NormalizedExisting))
		{
			OutText = NormalizedIncoming;
			return true;
		}
		// The caller clears one affected glyph segment and replays this complete
		// source. Other narration/dialogue rows remain mounted and do not move.
		bOutRebuilt = true;
		OutText = NormalizedIncoming;
		return true;
	}

	bool ShouldRPStreamingAutoScroll(float ScrollOffset, float EndOffset)
	{
		return EndOffset - ScrollOffset < 140.f;
	}

	bool IsRPHighSurrogate(TCHAR Ch)
	{
		const uint32 Value = static_cast<uint32>(static_cast<uint16>(Ch));
		return Value >= 0xD800u && Value <= 0xDBFFu;
	}

	bool IsRPLowSurrogate(TCHAR Ch)
	{
		const uint32 Value = static_cast<uint32>(static_cast<uint16>(Ch));
		return Value >= 0xDC00u && Value <= 0xDFFFu;
	}

	bool IsRPCombiningMark(TCHAR Ch)
	{
		const uint32 Value = static_cast<uint32>(static_cast<uint16>(Ch));
		return (Value >= 0x0300u && Value <= 0x036Fu)
			|| (Value >= 0x1AB0u && Value <= 0x1AFFu)
			|| (Value >= 0x1DC0u && Value <= 0x1DFFu)
			|| (Value >= 0x20D0u && Value <= 0x20FFu)
			|| (Value >= 0xFE20u && Value <= 0xFE2Fu);
	}

	/** One visible cluster (base code point plus combining marks).  UTF-16
	 * surrogate pairs are kept together so emoji and astral CJK never tear while
	 * being appended or faded in. */
	FString NextRPTextCluster(const FString& Text, int32& InOutIndex)
	{
		if (!Text.IsValidIndex(InOutIndex)) return FString();
		const int32 Start = InOutIndex;
		++InOutIndex;
		if (Text.IsValidIndex(InOutIndex) && IsRPHighSurrogate(Text[Start])
			&& IsRPLowSurrogate(Text[InOutIndex]))
		{
			++InOutIndex;
		}
		while (Text.IsValidIndex(InOutIndex) && IsRPCombiningMark(Text[InOutIndex])) ++InOutIndex;
		return Text.Mid(Start, InOutIndex - Start);
	}

	FSlateFontInfo RPBodyFont(int32 FontSize)
	{
		FSlateFontInfo Font = FAscendUIStyle::Font(FontSize);
		Font.LetterSpacing = RPBodyLetterSpacing;
		return Font;
	}

	FVector2D RPMeasureTextSize(const FString& Text, int32 FontSize);

	float RPBodyLineHeight(float FontSize)
	{
		const int32 SafeFontSize = FMath::Max(10, FMath::RoundToInt(FontSize));
		const float MeasuredHeight = RPMeasureTextSize(TEXT("中"), SafeFontSize).Y;
		const float BaseHeight = MeasuredHeight > 0.f
			? MeasuredHeight : FAscendUIStyle::Font(SafeFontSize).Size;
		return FMath::Max(18.f, BaseHeight * RPBodyLineHeightMultiplier);
	}

	float RPBodyParagraphGap(float FontSize)
	{
		return RPBodyLineHeight(FontSize) * RPBodyParagraphGapMultiplier;
	}

	FVector2D RPMeasureTextSize(const FString& Text, int32 FontSize)
	{
		if (Text.IsEmpty() || !FSlateApplication::IsInitialized()) return FVector2D::ZeroVector;
		FSlateRenderer* Renderer = FSlateApplication::Get().GetRenderer();
		if (!Renderer) return FVector2D::ZeroVector;
		const FSlateFontInfo Font = RPBodyFont(FontSize);
		const TSharedRef<FSlateFontMeasure> Measure = Renderer->GetFontMeasureService();
		const auto Measured = Measure->Measure(FStringView(Text), Font);
		return FVector2D(FMath::Max(0.f, static_cast<float>(Measured.X)),
			FMath::Max(0.f, static_cast<float>(Measured.Y)));
	}

	float RPMeasureTextWidth(const FString& Text, int32 FontSize)
	{
		return RPMeasureTextSize(Text, FontSize).X;
	}

	float RPApproximateClusterAdvance(const FString& Cluster, float FontSize)
	{
		if (Cluster.IsEmpty()) return 0.f;
		const TCHAR Ch = Cluster[0];
		if (Ch == TEXT('\n')) return 0.f;
		if (Ch == TEXT('\t')) return FontSize * 2.f;
		if (FChar::IsWhitespace(Ch)) return FontSize * 0.48f;
		const uint32 Value = static_cast<uint32>(static_cast<uint16>(Ch));
		// CJK, full-width punctuation and surrogate pairs are square glyphs in
		// the bundled UI font.  Latin text and ASCII punctuation use a conservative
		// advance so manual lines never exceed the clipping width.
		if (Value >= 0x2E80u || IsRPHighSurrogate(Ch)) return FontSize;
		if (FChar::IsAlnum(Ch)) return FontSize * 0.62f;
		return FontSize * 0.56f;
	}

	float RPClusterAdvance(const FString& Cluster, float FontSize)
	{
		if (Cluster.IsEmpty() || Cluster == TEXT("\n")) return 0.f;
		const int32 SafeFontSize = FMath::Max(10, FMath::RoundToInt(FontSize));
		const float Measured = RPMeasureTextWidth(Cluster, SafeFontSize);
		if (Measured > 0.f)
		{
			// A streamed cluster is its own TextBlock. Slate applies LetterSpacing
			// between glyphs in one block, but has no following glyph to attach the
			// tracking to when the cluster is isolated. Reserve that exact tracking
			// amount in the fixed slot so streamed and wrapped text use the same
			// 0.03em rhythm without moving existing glyphs when a chunk arrives.
			const FSlateFontInfo Font = RPBodyFont(SafeFontSize);
			const float Tracking = Font.Size * Font.LetterSpacing / 1000.f;
			return FMath::Max(1.f, Measured + FMath::Max(0.f, Tracking));
		}
		return FMath::Max(1.f, RPApproximateClusterAdvance(Cluster, FontSize));
	}

	FString FormatRPNaturalParagraphs(const FString& Source)
	{
		const FString Normalized = NormalizeRPLineEndings(Source);
		TArray<FString> Paragraphs;
		Normalized.ParseIntoArray(Paragraphs, TEXT("\n\n"), false);
		TArray<FString> Formatted;
		for (FString Paragraph : Paragraphs)
		{
			Paragraph.TrimStartAndEndInline();
			if (Paragraph.IsEmpty()) continue;
			Paragraph.ReplaceInline(TEXT("\n"), TEXT(" "));
			Formatted.Add(TEXT("　　") + Paragraph);
		}
		return FString::Join(Formatted, TEXT("\n\n"));
	}

	void AddAuthoredOpeningDialogue(FInfiniteNarrativeBeat& Beat, const TCHAR* Speaker,
		const TCHAR* Text, const TCHAR* Expression = TEXT("neutral"))
	{
		FInfiniteDialogueLine Line;
		Line.Speaker = Speaker;
		Line.Expression = Expression;
		Line.Text = Text;
		Beat.DialogueLines.Add(Line);
		if (Beat.Speaker.IsEmpty()) Beat.Speaker = Speaker;
		if (!Beat.Dialogue.IsEmpty()) Beat.Dialogue += TEXT("\n");
		Beat.Dialogue += FString::Printf(TEXT("%s：\u201c%s\u201d"), Speaker, Text);
	}

	FInfiniteNarrativeBeat BuildAuthoredOpeningBeat(int32 OpeningIndex,
		const TArray<FRelicData>& OpeningRelics)
	{
		FInfiniteNarrativeBeat Beat;
		Beat.StatePatchJson = TEXT("{}");
		Beat.MemoryJson = TEXT("{}");
		FRelicData EmptyRelic;
		const FRelicData& Relic0 = OpeningRelics.IsValidIndex(0) ? OpeningRelics[0] : EmptyRelic;
		const FRelicData& Relic1 = OpeningRelics.IsValidIndex(1) ? OpeningRelics[1] : EmptyRelic;
		const FRelicData& Relic2 = OpeningRelics.IsValidIndex(2) ? OpeningRelics[2] : EmptyRelic;
		switch (OpeningIndex)
		{
		case 0:
			Beat.Title = TEXT("青禾渡 · 一块不该留下的灵石");
			Beat.Narration = TEXT(
				"青禾渡连下了七日冷雨。田埂被河水啃出缺口，半熟的灵谷伏在泥里，像一片再也直不起腰的人。你踩着齐踝的水回到家时，讨租的木牌已经钉在门上：明日午时以前，三斗灵谷，缺一升便收走东边那亩薄田。那是全家最后一块能长出灵气的地。\n\n"
				"屋里没有点灯。父亲咳得胸腔发响，药罐却早已见底；十二岁的阿杳抱膝坐在灶边，裙角还沾着替人采藕换来的黑泥。她从梁上摸下一只旧竹筒，倒出一块灰白石头。石头落在掌心的一刻，冷灶里的余火忽然向它偏了偏。\n\n"
				"你认得那点微光——一块下品灵石，足够请镇上的游医再来三次，也够补齐田租。父亲清醒时反复说，这是母亲失踪前留下的唯一东西，谁也不许动。可门外雨声里，已经混进了里正家青骡的铃铛。阿杳抬头看你，眼睛很亮，却一句求你的话也没有说。");
				AddAuthoredOpeningDialogue(Beat, TEXT("阿杳"), TEXT("哥（姐），你来定。只是定了以后，别说是为了我。"), TEXT("worried"));
				Beat.Choices = {
					MakeAuthoredOpeningChoice(TEXT("qinghe_spirit_stone"), Relic0, TEXT("你把灵石攥进掌心，决定先换药。刚走出渡口，灵石气息便引来芦苇荡中的野狼精；田契期限仍在逼近，而母亲留下的最后凭证也暴露在狼口前。")),
					MakeAuthoredOpeningChoice(TEXT("qinghe_spirit_stone"), Relic1, TEXT("你把灵石攥进掌心，决定先换药。刚走出渡口，灵石气息便引来芦苇荡中的野狼精；田契期限仍在逼近，而母亲留下的最后凭证也暴露在狼口前。")),
					MakeAuthoredOpeningChoice(TEXT("qinghe_spirit_stone"), Relic2, TEXT("你把灵石攥进掌心，决定先换药。刚走出渡口，灵石气息便引来芦苇荡中的野狼精；田契期限仍在逼近，而母亲留下的最后凭证也暴露在狼口前。"))
				};
			Beat.Diagnostic = TEXT("authored_opening:qinghe_spirit_stone");
			break;

		case 1:
			Beat.Title = TEXT("栖鹤观 · 灵盘最后一声");
			Beat.Narration = TEXT(
				"栖鹤观三年开一次山门。你从天未亮排到日头偏西，鞋底的草绳断了两回，怀里那张由全村凑钱换来的测验木签，已经被汗浸得发软。前面有人测出双灵根，当场被披上青袍；也有人让铜镜沉默，哭着被家人拖下石阶。每响一声钟，队伍便短一截，你身后的退路也跟着窄一分。\n\n"
				"轮到你时，测灵盘先是死寂，随后从边缘渗出一线黯淡青光。执事皱眉，在名册上写下‘木水杂灵根，灵息微弱’。这不是没有仙缘，却只够换来一个药圃杂役名额：签十年身契，前两年没有月例，若中途逃离，村里替你作保的三户人家一并赔偿。\n\n"
				"石阶另一侧，一个衣袖洗得发白的少女悄悄向你亮出自己的木牌。她叫桑晚，同样是杂灵根，却被判作不收。她说自己知道观后旧药圃缺人，只要你肯把名字借她挂在名下，两个人或许都能留下。此时执事已经抬起朱笔，等你在身契、返乡与这桩违规交易之间作答。");
			AddAuthoredOpeningDialogue(Beat, TEXT("栖鹤观执事"), TEXT("仙门不欠谁的前程。签，便守规矩；不签，今日下山。"), TEXT("stern"));
				AddAuthoredOpeningDialogue(Beat, TEXT("桑晚"), TEXT("我不要你的名额。让我进药圃，活做两份，记一份工就行。"), TEXT("determined"));
				Beat.Choices = {
					MakeAuthoredOpeningChoice(TEXT("lingpan_last_chime"), Relic0, TEXT("你接下这件法器，与桑晚一同踏进旧药圃。地窖里的遗弃尸傀被新入的灵息惊醒；你们从第一天起便共享劳作、秘密与被迫死战的代价。")),
					MakeAuthoredOpeningChoice(TEXT("lingpan_last_chime"), Relic1, TEXT("你接下这件法器，与桑晚一同踏进旧药圃。地窖里的遗弃尸傀被新入的灵息惊醒；你们从第一天起便共享劳作、秘密与被迫死战的代价。")),
					MakeAuthoredOpeningChoice(TEXT("lingpan_last_chime"), Relic2, TEXT("你接下这件法器，与桑晚一同踏进旧药圃。地窖里的遗弃尸傀被新入的灵息惊醒；你们从第一天起便共享劳作、秘密与被迫死战的代价。"))
				};
			Beat.Diagnostic = TEXT("authored_opening:lingpan_last_chime");
			break;

		case 2:
			Beat.Title = TEXT("临溪陆氏 · 祖屋分灯");
			Beat.Narration = TEXT(
				"祖祠的长明灯灭了一盏。对临溪陆氏这样的末流修仙小族而言，这不是凶兆，而是一笔谁都不愿承认的账：东院灵脉枯了，供不起所有人的修行。族老把旁支子弟叫进祠堂，宣布今冬只留三人领取吐纳散，其余人迁去河湾看守凡田。\n\n"
				"你的名字排在第四。三日前病逝的二叔本该替你争一争，如今他的蒲团已经撤走，只剩一只封了蜡的木匣放在供桌下。堂兄陆承礼说匣中是二叔侵吞族产的证据，交出来，你便能顶替他的名额；族姐陆青禾却在众目睽睽下跪到你身侧，说匣里是她父亲用命换来的采药路线，若交给族中，她们母女连最后的活路也保不住。\n\n"
				"门外，迁往河湾的驴车已经套好。族老没有催你，只把三枚吐纳散放在案上。药香极淡，却让祠堂里每个人的呼吸都重了一分。你忽然明白，今日争的并非一枚丹药，而是谁有资格被家族当作‘修士’继续供养。");
			AddAuthoredOpeningDialogue(Beat, TEXT("族老陆怀山"), TEXT("族里养不起清白二字。你若要名额，就拿能让众人信服的东西来换。"), TEXT("stern"));
				AddAuthoredOpeningDialogue(Beat, TEXT("陆青禾"), TEXT("我不求你信我父亲。我只求你打开匣子以前，先听我把那一夜说完。"), TEXT("worried"));
				Beat.Choices = {
					MakeAuthoredOpeningChoice(TEXT("ancestral_house_lamp"), Relic0, TEXT("你带着这件法器随河湾驴车离族。夜宿山道时，木匣泄出的旧灵息引来山魈；失去族中资源的同时，你也把这桩旧账带进了雨夜死战。")),
					MakeAuthoredOpeningChoice(TEXT("ancestral_house_lamp"), Relic1, TEXT("你带着这件法器随河湾驴车离族。夜宿山道时，木匣泄出的旧灵息引来山魈；失去族中资源的同时，你也把这桩旧账带进了雨夜死战。")),
					MakeAuthoredOpeningChoice(TEXT("ancestral_house_lamp"), Relic2, TEXT("你带着这件法器随河湾驴车离族。夜宿山道时，木匣泄出的旧灵息引来山魈；失去族中资源的同时，你也把这桩旧账带进了雨夜死战。"))
				};
			Beat.Diagnostic = TEXT("authored_opening:ancestral_house_lamp");
			break;

		case 3:
			Beat.Title = TEXT("玄微门 · 第三班炉火");
			Beat.Narration = TEXT(
				"玄微门外院的药炉昼夜不熄。你做了两年烧火杂役，最熟悉的不是功法，而是哪位管事会克扣炭钱、哪口炉子在雨天漏风，以及一枚聚气丹要烧掉多少像你这样的人半个月薪。今晚轮到第三班看火，偏偏丹房主炉在子时前裂开一道细纹。\n\n"
				"炉中炼的是外门弟子下月的份额。上报，第三班所有人的月例都要抵赔；隐瞒，炉炸时站得最近的人可能连尸骨都找不齐。和你同班的老周已经把铺盖卷到门边，他儿子等着这月灵石赎身。负责巡夜的外门弟子闻雁回却提前来到丹房，手里拿着一包不在账册上的寒髓砂。她说能封住裂纹，但要你替她在炉册上改一个时辰。\n\n"
				"远处更鼓敲过第二遍，炉腹里传来沉闷的‘咚’声，像有什么东西在火中慢慢醒来。闻雁回没有拔剑，也没有许诺照应你，只把寒髓砂放在砖台上。老周看了你一眼，伸手按住发抖的膝盖。第三班十七个人，今夜都在等你先动。");
			AddAuthoredOpeningDialogue(Beat, TEXT("闻雁回"), TEXT("我能补炉，你替我改账。出了事，各认一半；成了，也别问我为何有寒髓砂。"));
				AddAuthoredOpeningDialogue(Beat, TEXT("老周"), TEXT("小师傅，我家那张赎身契……就差这一个月。"), TEXT("worried"));
				Beat.Choices = {
					MakeAuthoredOpeningChoice(TEXT("third_furnace_watch"), Relic0, TEXT("你敲响警钟，第三班的人先得以撤离；潜伏丹房的焚账符修随即现身灭口——闻雁回的私藏与老周的赎身钱，都可能被一并卷入这场死战。")),
					MakeAuthoredOpeningChoice(TEXT("third_furnace_watch"), Relic1, TEXT("你敲响警钟，第三班的人先得以撤离；潜伏丹房的焚账符修随即现身灭口——闻雁回的私藏与老周的赎身钱，都可能被一并卷入这场死战。")),
					MakeAuthoredOpeningChoice(TEXT("third_furnace_watch"), Relic2, TEXT("你敲响警钟，第三班的人先得以撤离；潜伏丹房的焚账符修随即现身灭口——闻雁回的私藏与老周的赎身钱，都可能被一并卷入这场死战。"))
				};
			Beat.Diagnostic = TEXT("authored_opening:third_furnace_watch");
			break;

		default:
			Beat.Title = TEXT("白石坊 · 父债开匣");
			Beat.Narration = TEXT(
				"你在白石坊替人抄了五年符账，直到今日才知道，死人留下的债也会生利息。午后，一个自称段九的独眼散修把旧契拍在柜台上：你父亲十三年前借走三十块灵石，如今本息一百二十七块，三日不还，城南那间漏雨的小院便归他。契上有父亲的血印，连掌纹缺口都对得上。\n\n"
				"你没有一百二十七块灵石。你只有七块、半袋辟谷米，以及父母留下却从未打开的黑木匣。段九看见木匣时，独眼第一次偏开。他说可以用匣子抵债，当场撕契；隔壁修伞的瞎婆婆却隔着墙敲了三下竹杖——那是父亲生前约定的暗号，意思是‘莫信眼前人’。\n\n"
				"坊市闭门鼓已经响起。三日之期从此刻开始，段九却没有走，只在柜台对面慢慢喝茶。黑木匣没有锁，匣缝里压着一根褪色红线。你记得母亲曾用同样的红线替你量过手腕，后来她说尺寸不对，便再也没有提过。此刻只要伸手，十三年的沉默就会被你亲自揭开。");
			AddAuthoredOpeningDialogue(Beat, TEXT("段九"), TEXT("欠债还钱，天经地义。我肯收这破匣子，是看你爹当年还算个人。"));
				AddAuthoredOpeningDialogue(Beat, TEXT("瞎婆婆"), TEXT("三日长得很，够一个人死两回，也够一句假话露出脚。"), TEXT("stern"));
				Beat.Choices = {
					MakeAuthoredOpeningChoice(TEXT("father_debt_box"), Relic0, TEXT("你打开黑木匣，段九确认匣中确有他要找的旧物，撕毁血契的说辞随即变成了夺匣杀人；父母守了十三年的秘密也将伴你押上性命。")),
					MakeAuthoredOpeningChoice(TEXT("father_debt_box"), Relic1, TEXT("你打开黑木匣，段九确认匣中确有他要找的旧物，撕毁血契的说辞随即变成了夺匣杀人；父母守了十三年的秘密也将伴你押上性命。")),
					MakeAuthoredOpeningChoice(TEXT("father_debt_box"), Relic2, TEXT("你打开黑木匣，段九确认匣中确有他要找的旧物，撕毁血契的说辞随即变成了夺匣杀人；父母守了十三年的秘密也将伴你押上性命。"))
				};
			Beat.Diagnostic = TEXT("authored_opening:father_debt_box");
			break;
		}
		if (Beat.Diagnostic.StartsWith(TEXT("authored_opening:")))
		{
			const FString OpeningId = Beat.Diagnostic.RightChop(17);
			for (int32 Index = 0; Index < Beat.Choices.Num(); ++Index)
				Beat.Choices[Index].Text = AuthoredOpeningChoiceFallback(*OpeningId, Index);
		}
		return Beat;
	}

	FString BuildAuthoredOpeningChoiceWordingPrompt(const FInfiniteNarrativeBeat& Beat,
		const TArray<FRelicData>& OpeningRelics)
	{
		FString Prompt = TEXT("这是一个已经锁定结果的游戏开场。固定正文、人物对白、敌人、奖励与剧情结果都不可改写；你只为三个已绑定法器生成短的选择按钮行动衔接。\n");
		Prompt += TEXT("固定标题：") + Beat.Title + TEXT("\n固定旁白：") + Beat.Narration + TEXT("\n固定对白：") + Beat.Dialogue + TEXT("\n");
		for (int32 Index = 0; Index < 3; ++Index)
		{
			const FRelicData* Relic = OpeningRelics.IsValidIndex(Index) ? &OpeningRelics[Index] : nullptr;
			if (!Relic) continue;
			FString Description = Relic->Description;
			Description.ReplaceInline(TEXT("\r"), TEXT(" "));
			Description.ReplaceInline(TEXT("\n"), TEXT(" "));
			Prompt += FString::Printf(TEXT("法器槽位%c（只能使用此ID，不得交换）：id=%s；名称=%s；说明=%s\n"),
				TCHAR(TEXT('A') + Index), *Relic->Id, *Relic->Name, *Description.Left(240));
		}
		Prompt += TEXT("请让每条文案说明玩家此刻如何处理眼前处境、它怎样贴合现场并自然引向固定的首战。绝不能在text正文泄露绑定法器的精确名称、ID、品级、效果、数值、触发条件，或暗示玩家将获得哪件物品；不要重复法器名称或说明，不要写战斗结果，不要增加分支。三条文案必须使用不同的动作、观察角度或物件位置，不能三句相同。输出choice_id和text，choice_id必须逐字复制对应法器ID。");
		return Prompt;
	}

	FString BuildAuthoredOpeningChoiceDisplay(const FRelicData& Relic, const FString& Lead)
	{
		const FString DisplayName = Relic.Name.IsEmpty() ? Relic.Id : Relic.Name;
		const FString Description = Relic.Description.IsEmpty()
			? TEXT("一件尚未认主的法器") : Relic.Description.Left(180);
		return FString::Printf(TEXT("%s\n法器【%s】\n%s"), *Lead, *DisplayName, *Description);
	}

	FString ExtractAuthoredOpeningChoiceLead(const FString& DisplayText)
	{
		const int32 Marker = DisplayText.Find(TEXT("\n法器【"));
		return (Marker > 0 ? DisplayText.Left(Marker) : DisplayText).TrimStartAndEnd();
	}

	FString FilterAuthoredOpeningChoiceText(const FString& OpeningId, int32 ChoiceIndex,
		const TArray<FRelicData>& LockedRelics, const FString& StoredText)
	{
		FString Lead = ExtractAuthoredOpeningChoiceLead(StoredText);
		if (Lead.IsEmpty()) return AuthoredOpeningChoiceFallback(*OpeningId, ChoiceIndex);
		for (const FRelicData& Relic : LockedRelics)
			if (!UInfiniteNarrativeService::IsOpeningChoiceWordingSafe(Lead, Relic.Id,
				Relic.Name, Relic.Description, Relic.Rarity))
				return AuthoredOpeningChoiceFallback(*OpeningId, ChoiceIndex);
		return Lead;
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

	FString ResolveNarrativeRegistryForDisplay(const FInfiniteNarrativeSettings& Settings)
	{
		if (!Settings.CharacterRegistryOverride.TrimStartAndEnd().IsEmpty())
			return Settings.CharacterRegistryOverride;
		if (Settings.bAllowImportedContentToNarrativeModel && Settings.bCharacterCardEnabled
			&& !Settings.CharacterCardId.TrimStartAndEnd().IsEmpty())
		{
			FNarrativeCharacterCardAsset Card;
			FString Error;
			if (FNarrativeContentLibrary::LoadCharacterCard(Settings.CharacterCardId.TrimStartAndEnd(), Card, Error)
				&& !Card.RegistryJson.IsEmpty())
				return Card.RegistryJson;
		}
		return LoadRPDataFile(TEXT("Data/rp_characters.json"));
	}

	FRPPortraitDefinition ResolveRPPortrait(const FString& PortraitId, const FString& Speaker,
		const FString& Expression, const FString& RegistryOverride)
	{
		FRPPortraitDefinition Result;
		// Keep an unknown id out of the resolved definition.  In particular, do not
		// let an LLM typo such as "unknown_woman" select a real character's art;
		// the neutral color/initial fallback below is deliberately safe.
		Result.Id.Reset();
		Result.Name = Speaker.IsEmpty() ? TEXT("旁白") : Speaker;
		Result.Color = FLinearColor(0.22f, 0.28f, 0.30f, 1.f);

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
		const FString& Expression, const FString& RegistryOverride, float PortraitSize = 208.f)
	{
		const FRPPortraitDefinition Definition = ResolveRPPortrait(PortraitId, Speaker, Expression, RegistryOverride);
		USizeBox* Size = NewObject<USizeBox>(Outer);
		const float SafePortraitSize = FMath::Clamp(PortraitSize, 72.f, 208.f);
		Size->SetWidthOverride(SafePortraitSize);
		Size->SetHeightOverride(SafePortraitSize);
		UOverlay* Overlay = NewObject<UOverlay>(Size);
		// 旧版绿色占位底只在缺少人物素材时保留；已有头像时外围必须完全透明。
		if (Definition.Art.IsEmpty())
		{
			UBorder* Backdrop = NewObject<UBorder>(Overlay);
			Backdrop->SetBrushColor(Definition.Color * 0.32f + FLinearColor(0.025f, 0.035f, 0.04f, 1.f) * 0.68f);
			UOverlaySlot* BackdropSlot = Overlay->AddChildToOverlay(Backdrop);
			BackdropSlot->SetPadding(FMargin(SafePortraitSize * 0.13f));
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
			ImageSlot->SetPadding(FMargin(SafePortraitSize * 0.13f));
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
		if (UVerticalBoxSlot* Slot = Box->AddChildToVerticalBox(Widget))
		{
			// A vertical box otherwise sizes to its widest desired child.  Fill
			// alignment lets the outer RP SizeBox provide the actual viewport-safe
			// width, so wrapped text never wins a width race against the screen.
			Slot->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Fill);
			Slot->SetPadding(Padding);
		}
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
		// This helper is used only by the RP/settings reading layer.  Cards still
		// call FAscendUIStyle::MakeText directly and therefore keep their existing
		// spacing and wrapping metrics.
		Result->SetFont(RPBodyFont(Size));
		Result->SetLineHeightPercentage(RPBodyLineHeightMultiplier);
		Result->SetApplyLineHeightToBottomLine(true);
		Result->SetAutoWrapText(true);
		Result->SetJustification(Justification);
		return Result;
	}

	void RPSplitNaturalParagraphs(const FString& Source, TArray<FString>& OutParagraphs)
	{
		OutParagraphs.Reset();
		const FString Normalized = NormalizeRPLineEndings(Source);
		Normalized.ParseIntoArray(OutParagraphs, TEXT("\n\n"), false);
		for (FString& Paragraph : OutParagraphs)
		{
			Paragraph.TrimStartAndEndInline();
			Paragraph.ReplaceInline(TEXT("\n"), TEXT(" "));
		}
		OutParagraphs.RemoveAll([](const FString& Paragraph) { return Paragraph.IsEmpty(); });
	}

	void RPAddNaturalParagraphs(UVerticalBox* Box, const FString& Text, int32 Size,
		FSlateColor Color, float WrapWidth, UTextBlock** OutFirstText = nullptr)
	{
		if (OutFirstText) *OutFirstText = nullptr;
		if (!Box) return;
		TArray<FString> Paragraphs;
		RPSplitNaturalParagraphs(Text, Paragraphs);
		if (Paragraphs.Num() == 0) Paragraphs.Add(FString());
		for (int32 Index = 0; Index < Paragraphs.Num(); ++Index)
		{
			UTextBlock* ParagraphText = RPMakeWrappedText(Box,
				TEXT("　　") + Paragraphs[Index], Size, Color, ETextJustify::Left);
			ParagraphText->SetWrapTextAt(FMath::Max(96.f, WrapWidth));
			ParagraphText->SetShadowOffset(FVector2D(1.f, 1.f));
			ParagraphText->SetShadowColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.64f));
			if (OutFirstText && !*OutFirstText) *OutFirstText = ParagraphText;
			if (UVerticalBoxSlot* Slot = Box->AddChildToVerticalBox(ParagraphText))
			{
				Slot->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Fill);
				Slot->SetPadding(FMargin(0.f));
			}
			if (Index + 1 < Paragraphs.Num())
			{
				USpacer* Gap = NewObject<USpacer>(Box);
				Gap->SetSize(FVector2D(1.f, RPBodyParagraphGap(static_cast<float>(Size))));
				if (UVerticalBoxSlot* GapSlot = Box->AddChildToVerticalBox(Gap))
				{
					GapSlot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
				}
			}
		}
	}

	void RPConfigureNativeDialogueBubble(UBorder* Bubble, UBorder*& OutSurface)
	{
		OutSurface = nullptr;
		if (!Bubble) return;

		// Keep RP dialogue independent from decorative chat artwork. The outer
		// border remains visible at every height, while the inner solid surface is
		// the complete text-safe field that naturally grows with the body widget.
		Bubble->SetBrushColor(FLinearColor(0.12f, 0.20f, 0.22f, 1.f));
		Bubble->SetPadding(FMargin(RPDialogueBubbleBorder));
		Bubble->SetClipping(EWidgetClipping::ClipToBounds);

		UBorder* Surface = NewObject<UBorder>(Bubble);
		Surface->SetBrushColor(FLinearColor(0.025f, 0.040f, 0.050f, 0.98f));
		Surface->SetPadding(FMargin(RPDialogueBubbleInnerHorizontalPadding,
			RPDialogueBubbleInnerVerticalPadding));
		Surface->SetClipping(EWidgetClipping::ClipToBounds);
		Bubble->SetContent(Surface);
		OutSurface = Surface;
	}

	void RPAddReadingParagraph(UVerticalBox* Box, const FString& Text, int32 Size,
		FSlateColor Color, FMargin Padding, float MaxWidth = 900.f)
	{
		USizeBox* ReadingWidth = NewObject<USizeBox>(Box);
		const float SafeWidth = FMath::Max(140.f, MaxWidth);
		ReadingWidth->SetWidthOverride(SafeWidth);
		ReadingWidth->SetMinDesiredWidth(SafeWidth);
		ReadingWidth->SetMaxDesiredWidth(SafeWidth);
		UVerticalBox* ParagraphBox = NewObject<UVerticalBox>(ReadingWidth);
		RPAddNaturalParagraphs(ParagraphBox, Text, Size, Color, SafeWidth);
		ReadingWidth->SetContent(ParagraphBox);
		if (UVerticalBoxSlot* Slot = Box->AddChildToVerticalBox(ReadingWidth))
		{
			Slot->SetPadding(Padding);
			Slot->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Center);
		}
	}

	UWidget* RPMakeDialogueBubble(UObject* Outer, const FInfiniteDialogueLine& Line,
		bool bShowPortrait, const FString& RegistryOverride, UTextBlock** OutBodyText = nullptr,
		float MaxRowWidth = 920.f, float PortraitSize = 208.f)
	{
		const bool bStackPortrait = bShowPortrait && MaxRowWidth < 620.f;
		UHorizontalBox* Row = NewObject<UHorizontalBox>(Outer);
		Row->SetClipping(EWidgetClipping::ClipToBounds);
		UVerticalBox* CompactRoot = bStackPortrait ? NewObject<UVerticalBox>(Outer) : nullptr;
		if (!bStackPortrait)
			Row->AddChildToHorizontalBox(NewObject<USpacer>(Row))->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		if (bShowPortrait)
		{
			UWidget* PortraitWidget = RPMakePortrait(Row, Line.PortraitId, Line.Speaker,
				Line.Expression, RegistryOverride, PortraitSize);
			if (bStackPortrait)
			{
				if (UVerticalBoxSlot* Slot = CompactRoot->AddChildToVerticalBox(PortraitWidget))
				{
					Slot->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Center);
					Slot->SetPadding(FMargin(4.f, 4.f, 4.f, 2.f));
				}
			}
			else RPAddToHBox(Row, PortraitWidget, FMargin(8.f, 10.f, 14.f, 2.f));
		}

		UVerticalBox* MessageColumn = NewObject<UVerticalBox>(Row);
		RPAddToVBox(MessageColumn, RPMakeWrappedText(MessageColumn,
			Line.Speaker.IsEmpty() ? TEXT("旁白") : Line.Speaker, 18,
			FAscendUIStyle::InkBlack(), ETextJustify::Left), FMargin(12.f, 0.f, 5.f, 4.f));
		UBorder* Bubble = NewObject<UBorder>(MessageColumn);
		UBorder* BubbleSurface = nullptr;
		RPConfigureNativeDialogueBubble(Bubble, BubbleSurface);
		USizeBox* BubbleSize = NewObject<USizeBox>(BubbleSurface);
		// TextBlock 的真实 DesiredSize 决定气泡高度；短句自然收缩，长句按
		// WrapTextAt 自动增高。
		const float TextWidth = FMath::Min(980.f,
			RPDialogueBubbleTextWidth(MaxRowWidth, bStackPortrait, bShowPortrait, PortraitSize));
		const float BodyTextWidth = RPDialogueBodyTextWidth(MaxRowWidth,
			bStackPortrait, bShowPortrait, PortraitSize);
		const float BubbleWidth = TextWidth + 2.f *
			(RPDialogueBubbleInnerHorizontalPadding + RPDialogueBubbleBorder);
		BubbleSize->SetWidthOverride(TextWidth);
		BubbleSize->SetMinDesiredWidth(TextWidth);
		BubbleSize->SetMinDesiredHeight(28.f);
		BubbleSize->SetMaxDesiredWidth(TextWidth);
		UVerticalBox* BodyBox = NewObject<UVerticalBox>(BubbleSize);
		UTextBlock* BodyText = nullptr;
		// Use the same paragraph widgets and half-line spacer as streaming. This
		// avoids a full blank line in normal mode while keeping the actual wrap
		// width identical to the fixed glyph body width.
		RPAddNaturalParagraphs(BodyBox, Line.Text, 20, FAscendUIStyle::PaperWhite(),
			BodyTextWidth, &BodyText);
		if (OutBodyText) *OutBodyText = BodyText;
		BubbleSize->SetContent(BodyBox);
		BubbleSurface->SetContent(BubbleSize);
		RPAddToVBox(MessageColumn, Bubble, FMargin(4.f));
		USizeBox* MessageSizer = NewObject<USizeBox>(Row);
		MessageSizer->SetWidthOverride(BubbleWidth);
		MessageSizer->SetMaxDesiredWidth(BubbleWidth);
		MessageSizer->SetContent(MessageColumn);
		if (bStackPortrait)
		{
			if (UVerticalBoxSlot* Slot = CompactRoot->AddChildToVerticalBox(MessageSizer))
			{
				Slot->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Fill);
				Slot->SetPadding(FMargin(2.f, 4.f));
			}
		}
		else
		{
			RPAddToHBox(Row, MessageSizer, FMargin(2.f, 4.f));
			Row->AddChildToHorizontalBox(NewObject<USpacer>(Row))->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		}
		USizeBox* RowSizer = NewObject<USizeBox>(Outer);
		RowSizer->SetWidthOverride(FMath::Max(220.f, MaxRowWidth));
		RowSizer->SetMaxDesiredWidth(FMath::Max(220.f, MaxRowWidth));
		RowSizer->SetClipping(EWidgetClipping::ClipToBounds);
		RowSizer->SetContent(bStackPortrait ? static_cast<UWidget*>(CompactRoot) : static_cast<UWidget*>(Row));
		return RowSizer;
	}

	UWidget* RPMakeStreamingDialogueBubble(UObject* Outer, const FInfiniteDialogueLine& Line,
		bool bShowPortrait, const FString& RegistryOverride, UVerticalBox** OutBodyBox,
		float MaxRowWidth, float PortraitSize, UWidget** OutPortrait = nullptr,
		USizeBox** OutBubbleSize = nullptr, float* OutBodyTextWidth = nullptr)
	{
		const bool bStackPortrait = bShowPortrait && MaxRowWidth < 620.f;
		UHorizontalBox* Row = NewObject<UHorizontalBox>(Outer);
		Row->SetClipping(EWidgetClipping::ClipToBounds);
		UVerticalBox* CompactRoot = bStackPortrait ? NewObject<UVerticalBox>(Outer) : nullptr;
		if (!bStackPortrait)
			Row->AddChildToHorizontalBox(NewObject<USpacer>(Row))->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		UWidget* PortraitWidget = nullptr;
		if (bShowPortrait)
		{
			PortraitWidget = RPMakePortrait(Row, Line.PortraitId, Line.Speaker,
				Line.Expression, RegistryOverride, PortraitSize);
			if (bStackPortrait)
			{
				if (UVerticalBoxSlot* Slot = CompactRoot->AddChildToVerticalBox(PortraitWidget))
				{
					Slot->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Center);
					Slot->SetPadding(FMargin(4.f, 4.f, 4.f, 2.f));
				}
			}
			else RPAddToHBox(Row, PortraitWidget, FMargin(8.f, 10.f, 14.f, 2.f));
		}
		if (OutPortrait) *OutPortrait = PortraitWidget;

		const float TextWidth = FMath::Min(980.f,
			RPDialogueBubbleTextWidth(MaxRowWidth, bStackPortrait, bShowPortrait, PortraitSize));
		const float BodyTextWidth = RPDialogueBodyTextWidth(MaxRowWidth,
			bStackPortrait, bShowPortrait, PortraitSize);
		if (OutBodyTextWidth) *OutBodyTextWidth = BodyTextWidth;
		UVerticalBox* MessageColumn = NewObject<UVerticalBox>(Row);
		RPAddToVBox(MessageColumn, RPMakeWrappedText(MessageColumn,
			Line.Speaker.IsEmpty() ? TEXT("旁白") : Line.Speaker, 18,
			FAscendUIStyle::InkBlack(), ETextJustify::Left), FMargin(12.f, 0.f, 5.f, 4.f));
		UBorder* Bubble = NewObject<UBorder>(MessageColumn);
		UBorder* BubbleSurface = nullptr;
		RPConfigureNativeDialogueBubble(Bubble, BubbleSurface);
		USizeBox* BubbleSize = NewObject<USizeBox>(BubbleSurface);
		if (OutBubbleSize) *OutBubbleSize = BubbleSize;
		const float BubbleWidth = TextWidth + 2.f *
			(RPDialogueBubbleInnerHorizontalPadding + RPDialogueBubbleBorder);
		BubbleSize->SetWidthOverride(TextWidth);
		BubbleSize->SetMinDesiredWidth(TextWidth);
		BubbleSize->SetMinDesiredHeight(28.f);
		BubbleSize->SetMaxDesiredWidth(TextWidth);
		UVerticalBox* BodyBox = NewObject<UVerticalBox>(BubbleSize);
		BodyBox->SetClipping(EWidgetClipping::ClipToBounds);
		BubbleSize->SetContent(BodyBox);
		BubbleSurface->SetContent(BubbleSize);
		RPAddToVBox(MessageColumn, Bubble, FMargin(4.f));
		USizeBox* MessageSizer = NewObject<USizeBox>(Row);
		MessageSizer->SetWidthOverride(BubbleWidth);
		MessageSizer->SetMaxDesiredWidth(BubbleWidth);
		MessageSizer->SetContent(MessageColumn);
		if (bStackPortrait)
		{
			if (UVerticalBoxSlot* Slot = CompactRoot->AddChildToVerticalBox(MessageSizer))
			{
				Slot->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Fill);
				Slot->SetPadding(FMargin(2.f, 4.f));
			}
		}
		else
		{
			RPAddToHBox(Row, MessageSizer, FMargin(2.f, 4.f));
			Row->AddChildToHorizontalBox(NewObject<USpacer>(Row))->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		}

		if (OutBodyBox) *OutBodyBox = BodyBox;
		USizeBox* RowSizer = NewObject<USizeBox>(Outer);
		RowSizer->SetWidthOverride(FMath::Max(220.f, MaxRowWidth));
		RowSizer->SetMaxDesiredWidth(FMath::Max(220.f, MaxRowWidth));
		RowSizer->SetClipping(EWidgetClipping::ClipToBounds);
		RowSizer->SetContent(bStackPortrait ? static_cast<UWidget*>(CompactRoot) : static_cast<UWidget*>(Row));
		return RowSizer;
	}

	void RPUpdateStreamingBubbleGeometry(UVerticalBox* BodyBox, USizeBox* BubbleSize,
		float SafeFontSize)
	{
		if (!BodyBox || !BubbleSize) return;

		int32 LineCount = 0;
		int32 ParagraphGapCount = 0;
		for (int32 ChildIndex = 0; ChildIndex < BodyBox->GetChildrenCount(); ++ChildIndex)
		{
			UWidget* Child = BodyBox->GetChildAt(ChildIndex);
			if (Cast<UHorizontalBox>(Child)) ++LineCount;
			else if (Cast<USpacer>(Child)) ++ParagraphGapCount;
		}

		const float LineHeight = RPBodyLineHeight(SafeFontSize);
		const float RequiredBodyHeight = FMath::Max(LineHeight,
			LineCount * LineHeight + ParagraphGapCount * RPBodyParagraphGapMultiplier * LineHeight);
		// Set an exact override after every append. MinDesiredHeight alone can leave
		// the old one-line allocation cached by the parent row, clipping the final
		// glyph line even though BodyBox's children already exist.
		BubbleSize->SetMinDesiredHeight(RequiredBodyHeight);
		BubbleSize->SetHeightOverride(RequiredBodyHeight);
		BodyBox->InvalidateLayoutAndVolatility();
		BubbleSize->InvalidateLayoutAndVolatility();
		for (UWidget* Ancestor = BubbleSize->GetParent(); Ancestor; Ancestor = Ancestor->GetParent())
		{
			Ancestor->InvalidateLayoutAndVolatility();
		}
	}

	void RPAppendStreamingGlyphs(UVerticalBox* Host, const FString& NewSource,
		FString& ExistingSource, TArray<UHorizontalBox*>& Lines, TArray<float>& LineWidths,
		bool& bNeedsParagraphIndent, bool& bPreviousNewline, bool bIndentParagraphs,
		float MaxWidth, int32 FontSize, FSlateColor Color, TArray<UTextBlock*>& RevealGlyphs,
		USizeBox* HeightHost = nullptr)
	{
		const FString Normalized = NormalizeRPLineEndings(NewSource);
		// An empty source is the normal first chunk.  Keep the prefix guard for
		// subsequent updates, but do not ask FString::StartsWith to validate an
		// empty prefix: some UE string implementations treat that as false and
		// would silently drop the entire first streamed paragraph.
		if (Normalized.IsEmpty()) return;
		if (!ExistingSource.IsEmpty()
			&& (Normalized.Len() <= ExistingSource.Len() || !Normalized.StartsWith(ExistingSource))) return;
		if (!Host) return;

		const float SafeFontSize = static_cast<float>(FMath::Max(10, FontSize));
		// Leave one glyph of breathing room.  A Chinese closing mark arriving in
		// the next provider chunk can then still join the preceding line instead
		// of becoming an orphan at column zero, without ever exceeding the parent
		// clip rectangle.
		// MaxWidth is already the actual BodyBox/WrapTextAt width. Do not subtract
		// an arbitrary font-size cushion here: it made streaming wrap earlier than
		// normal mode and was the source of the clipped mixed-language tail.
		const float SafeWidth = FMath::Max(160.f, MaxWidth);
		const int32 ExistingLength = ExistingSource.Len();
		const FString Suffix = Normalized.Mid(ExistingLength);
		// A provider may split a UTF-16 surrogate pair exactly at the receive
		// boundary.  Keep a trailing high surrogate in the source buffer until its
		// low partner arrives; no half-emoji widget is ever created.
		int32 ProcessLength = Suffix.Len();
		if (ProcessLength > 0 && IsRPHighSurrogate(Suffix[ProcessLength - 1])) --ProcessLength;
		if (ProcessLength <= 0) return;
		const FString ProcessableSuffix = Suffix.Left(ProcessLength);
		ExistingSource = Normalized.Left(ExistingLength) + ProcessableSuffix;
		int32 Index = 0;
		while (Index < ProcessableSuffix.Len())
		{
			const FString Cluster = NextRPTextCluster(ProcessableSuffix, Index);
			if (Cluster.IsEmpty()) continue;
			if (Cluster == TEXT("\n"))
			{
				if (bPreviousNewline && bIndentParagraphs)
				{
					USpacer* ParagraphGap = NewObject<USpacer>(Host);
					// Keep the streaming spacer on the same measured line-height basis as
					// normal/final paragraph widgets. A second hard-coded multiplier here
					// made a later chunk change the vertical rhythm at the stream/final seam.
					ParagraphGap->SetSize(FVector2D(1.f, RPBodyParagraphGap(SafeFontSize)));
					if (UVerticalBoxSlot* GapSlot = Host->AddChildToVerticalBox(ParagraphGap))
					{
						GapSlot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
					}
					bNeedsParagraphIndent = true;
				}
				bPreviousNewline = true;
				continue;
			}

			if (bIndentParagraphs && bNeedsParagraphIndent)
			{
				// Two ideographic spaces are intentional: they are exactly two CJK
				// glyph advances regardless of the provider chunk boundary.
				bPreviousNewline = false;
				if (Lines.Num() == 0 || LineWidths.Last() > 0.f)
				{
					Lines.Add(NewObject<UHorizontalBox>(Host));
					LineWidths.Add(0.f);
					if (UVerticalBoxSlot* LineSlot = Host->AddChildToVerticalBox(Lines.Last()))
					{
						LineSlot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
					}
				}
				for (int32 Indent = 0; Indent < 2; ++Indent)
				{
					const FString IdeographicSpace = TEXT("　");
					USizeBox* GlyphSize = NewObject<USizeBox>(Lines.Last());
					const float Advance = RPClusterAdvance(IdeographicSpace, SafeFontSize);
					GlyphSize->SetWidthOverride(Advance);
					GlyphSize->SetHeightOverride(RPBodyLineHeight(SafeFontSize));
					UTextBlock* Glyph = RPMakeWrappedText(GlyphSize, IdeographicSpace, FontSize,
						Color, ETextJustify::Center);
					Glyph->SetRenderOpacity(0.f);
					GlyphSize->SetContent(Glyph);
					if (UHorizontalBoxSlot* Slot = Lines.Last()->AddChildToHorizontalBox(GlyphSize))
					{
						Slot->SetPadding(FMargin(0.f));
						Slot->SetVerticalAlignment(EVerticalAlignment::VAlign_Center);
					}
					LineWidths.Last() += Advance;
					RevealGlyphs.Add(Glyph);
				}
				bNeedsParagraphIndent = false;
			}
			if (bPreviousNewline && !bNeedsParagraphIndent)
			{
				if (Lines.Num() == 0 || LineWidths.Last() > 0.f)
				{
					Lines.Add(NewObject<UHorizontalBox>(Host));
					LineWidths.Add(0.f);
					if (UVerticalBoxSlot* LineSlot = Host->AddChildToVerticalBox(Lines.Last()))
					{
						LineSlot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
					}
				}
				bPreviousNewline = false;
			}
			bPreviousNewline = false;

			const float Advance = FMath::Max(1.f, RPClusterAdvance(Cluster, SafeFontSize));
			if (Lines.Num() == 0 || (LineWidths.Num() > 0 && LineWidths.Last() > 0.f
				&& LineWidths.Last() + Advance > SafeWidth))
			{
				Lines.Add(NewObject<UHorizontalBox>(Host));
				LineWidths.Add(0.f);
				if (UVerticalBoxSlot* LineSlot = Host->AddChildToVerticalBox(Lines.Last()))
				{
					LineSlot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
				}
			}
			USizeBox* GlyphSize = NewObject<USizeBox>(Lines.Last());
			GlyphSize->SetWidthOverride(Advance);
			GlyphSize->SetHeightOverride(RPBodyLineHeight(SafeFontSize));
			UTextBlock* Glyph = RPMakeWrappedText(GlyphSize, Cluster, FontSize, Color,
				ETextJustify::Center);
			Glyph->SetRenderOpacity(0.f);
			GlyphSize->SetContent(Glyph);
			if (UHorizontalBoxSlot* Slot = Lines.Last()->AddChildToHorizontalBox(GlyphSize))
			{
				Slot->SetPadding(FMargin(0.f));
				Slot->SetVerticalAlignment(EVerticalAlignment::VAlign_Center);
			}
			LineWidths.Last() += Advance;
			RevealGlyphs.Add(Glyph);
		}
		if (HeightHost)
		{
			RPUpdateStreamingBubbleGeometry(Host, HeightHost, SafeFontSize);
		}
	}

	void RPConfigureAdaptiveChoiceButton(UButton* Button, const FString& Label, int32 FontSize,
		float PaperWidth, UTextBlock** OutLabel = nullptr)
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
		const float TextWidth = RPChoiceTextWidth(PaperWidth);
		SafeBox->SetWidthOverride(TextWidth);
		SafeBox->SetMinDesiredWidth(FMath::Min(150.f, TextWidth));
		SafeBox->SetMinDesiredHeight(static_cast<float>(FMath::Max(24, FontSize + 8)));
		SafeBox->SetMaxDesiredWidth(TextWidth);
		UTextBlock* Text = RPMakeWrappedText(SafeBox, Label, FontSize,
			FAscendUIStyle::PaperWhite(), ETextJustify::Center);
		Text->SetAutoWrapText(true);
		Text->SetWrapTextAt(TextWidth);
		Text->SetJustification(ETextJustify::Center);
		Text->SetShadowOffset(FVector2D(1.f, 1.f));
		Text->SetShadowColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.82f));
		if (OutLabel) *OutLabel = Text;
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
	// New name for the same persisted preference; the old key remains a migration
	// fallback so existing players do not unexpectedly lose the option.
	GConfig->GetBool(NarrativeConfigSection, TEXT("FreeRPMode"),
		InfiniteNarrativeSettings.bShowFreeformInput, NarrativeConfigFile);
	GConfig->GetBool(NarrativeConfigSection, TEXT("ShowSpeakerPortrait"),
		InfiniteNarrativeSettings.bShowSpeakerPortrait, NarrativeConfigFile);
	GConfig->GetBool(NarrativeConfigSection, TEXT("EnableScreenShake"),
		InfiniteNarrativeSettings.bEnableScreenShake, NarrativeConfigFile);
	GConfig->GetBool(NarrativeConfigSection, TEXT("ReduceFlashing"),
		InfiniteNarrativeSettings.bReduceFlashing, NarrativeConfigFile);
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
	GConfig->GetString(NarrativeConfigSection, TEXT("CardForgeReasoningEffort"), InfiniteNarrativeSettings.CardForgeReasoningEffort, NarrativeConfigFile);
	GConfig->GetBool(NarrativeConfigSection, TEXT("RequestReasoning"), InfiniteNarrativeSettings.bRequestReasoning, NarrativeConfigFile);
	GConfig->GetBool(NarrativeConfigSection, TEXT("RequestMvuReasoning"), InfiniteNarrativeSettings.bRequestMvuReasoning, NarrativeConfigFile);
	GConfig->GetBool(NarrativeConfigSection, TEXT("ShowReasoning"), InfiniteNarrativeSettings.bShowReasoning, NarrativeConfigFile);
	GConfig->GetBool(NarrativeConfigSection, TEXT("EnableReasoningPrefill"),
		InfiniteNarrativeSettings.bEnableReasoningPrefill, NarrativeConfigFile);
	GConfig->GetString(NarrativeConfigSection, TEXT("ReasoningPrefill"),
		InfiniteNarrativeSettings.ReasoningPrefill, NarrativeConfigFile);
	GConfig->GetString(NarrativeConfigSection, TEXT("HiddenReasoningPrefillPath"),
		InfiniteNarrativeSettings.HiddenReasoningPrefillPath, NarrativeConfigFile);
	GConfig->GetString(NarrativeConfigSection, TEXT("HiddenReasoningPrefillFieldPath"),
		InfiniteNarrativeSettings.HiddenReasoningPrefillFieldPath, NarrativeConfigFile);
	GConfig->GetBool(NarrativeConfigSection, TEXT("MaintenanceMarkerEnabled"),
		InfiniteNarrativeSettings.bMaintenanceMarkerEnabled, NarrativeConfigFile);
	GConfig->GetString(NarrativeConfigSection, TEXT("MaintenanceMarker"),
		InfiniteNarrativeSettings.MaintenanceMarker, NarrativeConfigFile);
	if (InfiniteNarrativeSettings.MaintenanceMarker.IsEmpty())
		InfiniteNarrativeSettings.MaintenanceMarker = TEXT("[继续遵循既定角色,关系与写作要求,直接回应本条消息]");
	GConfig->GetBool(NarrativeConfigSection, TEXT("EnableTemporaryReasoningPreInjection"),
		InfiniteNarrativeSettings.bEnableTemporaryReasoningPreInjection, NarrativeConfigFile);
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
	GConfig->GetString(NarrativeConfigSection, TEXT("StoryDirection"), InfiniteNarrativeSettings.StoryDirection, NarrativeConfigFile);
	GConfig->GetBool(NarrativeConfigSection, TEXT("StoryDirectionEnabled"),
		InfiniteNarrativeSettings.bStoryDirectionEnabled, NarrativeConfigFile);
	GConfig->GetString(NarrativeConfigSection, TEXT("WorldBookId"), InfiniteNarrativeSettings.WorldBookId, NarrativeConfigFile);
	GConfig->GetBool(NarrativeConfigSection, TEXT("WorldBookEnabled"),
		InfiniteNarrativeSettings.bWorldBookEnabled, NarrativeConfigFile);
	GConfig->GetString(NarrativeConfigSection, TEXT("CharacterCardId"), InfiniteNarrativeSettings.CharacterCardId, NarrativeConfigFile);
	GConfig->GetBool(NarrativeConfigSection, TEXT("CharacterCardEnabled"),
		InfiniteNarrativeSettings.bCharacterCardEnabled, NarrativeConfigFile);
	GConfig->GetBool(NarrativeConfigSection, TEXT("EmbeddedCharacterBook"),
		InfiniteNarrativeSettings.bUseEmbeddedCharacterBook, NarrativeConfigFile);
	GConfig->GetBool(NarrativeConfigSection, TEXT("AllowImportedContentToNarrativeModel"),
		InfiniteNarrativeSettings.bAllowImportedContentToNarrativeModel, NarrativeConfigFile);
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
	GConfig->GetFloat(NarrativeConfigSection, TEXT("MusicVolume"), InfiniteNarrativeSettings.MusicVolume, NarrativeConfigFile);
	// Existing builds stored the old loud music default.  Migrate only that
	// untouched default; a player's deliberate custom value remains intact.
	if (SettingsSchemaVersion < 7 && FMath::IsNearlyEqual(InfiniteNarrativeSettings.MusicVolume, 0.65f, 0.001f))
		InfiniteNarrativeSettings.MusicVolume = 0.35f;
	InfiniteNarrativeSettings.InputContextTokens = FMath::Clamp(InfiniteNarrativeSettings.InputContextTokens, 8192, 2000000);
	InfiniteNarrativeSettings.MaxOutputTokens = FMath::Clamp(InfiniteNarrativeSettings.MaxOutputTokens, 1024, 262144);
	InfiniteNarrativeSettings.MvuMaxOutputTokens = FMath::Clamp(InfiniteNarrativeSettings.MvuMaxOutputTokens, 2048, 262144);
	InfiniteNarrativeSettings.NarrativeMinChars = FMath::Clamp(InfiniteNarrativeSettings.NarrativeMinChars, 200, 20000);
	InfiniteNarrativeSettings.NarrativeMaxChars = FMath::Clamp(InfiniteNarrativeSettings.NarrativeMaxChars,
		InfiniteNarrativeSettings.NarrativeMinChars, 30000);
	InfiniteNarrativeSettings.Temperature = FMath::Clamp(InfiniteNarrativeSettings.Temperature, 0.f, 1.5f);
	InfiniteNarrativeSettings.TopP = FMath::Clamp(InfiniteNarrativeSettings.TopP, 0.f, 1.f);
	InfiniteNarrativeSettings.CardForgeReasoningEffort = NormalizeCardForgeReasoningEffort(
		InfiniteNarrativeSettings.CardForgeReasoningEffort);
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
	InfiniteNarrativeSettings.MusicVolume = FMath::Clamp(InfiniteNarrativeSettings.MusicVolume, 0.f, 1.f);
	if (InfiniteNarrativeSettings.Model.IsEmpty()) InfiniteNarrativeSettings.Model = TEXT("local-model");
	UE_LOG(LogTemp, Display, TEXT("[InfiniteRP] settings loaded source=%s endpoint_configured=%s model=%s key_configured=%s"),
		*NarrativeConfigFile, InfiniteNarrativeSettings.Endpoint.IsEmpty() ? TEXT("false") : TEXT("true"),
		*InfiniteNarrativeSettings.Model, InfiniteNarrativeSettings.ApiKey.IsEmpty() ? TEXT("false") : TEXT("true"));
}

void AAscendPlayerController::OnCardForgeReasoningChanged(float Value)
{
	if (!SettingsCardForgeReasoningValueText) return;
	const FString Effort = CardForgeReasoningEffortFromSlider(Value);
	SettingsCardForgeReasoningValueText->SetText(FText::FromString(
		TEXT("生卡模型思考强度：") + CardForgeReasoningLabel(Effort)));
}

void AAscendPlayerController::OnSfxVolumeChanged(float Value)
{
	FInfiniteNarrativeSettings& Target = bSettingsDraftActive ? SettingsDraft : InfiniteNarrativeSettings;
	Target.SfxVolume = FMath::Clamp(Value, 0.f, 1.f);
	if (SettingsSfxVolumeValueText)
	{
		SettingsSfxVolumeValueText->SetText(FText::FromString(
			FString::Printf(TEXT("当前音效音量：%.0f%%"), Target.SfxVolume * 100.f)));
	}
	if (AudioRouter)
		AudioRouter->SetVolumes(Target.SfxVolume, Target.MusicVolume);
}

void AAscendPlayerController::OnMusicVolumeChanged(float Value)
{
	FInfiniteNarrativeSettings& Target = bSettingsDraftActive ? SettingsDraft : InfiniteNarrativeSettings;
	Target.MusicVolume = FMath::Clamp(Value, 0.f, 1.f);
	if (SettingsMusicVolumeValueText)
	{
		SettingsMusicVolumeValueText->SetText(FText::FromString(
			FString::Printf(TEXT("当前背景音乐音量：%.0f%%"), Target.MusicVolume * 100.f)));
	}
	if (AudioRouter)
		AudioRouter->SetVolumes(Target.SfxVolume, Target.MusicVolume);
}

void AAscendPlayerController::InvalidateNarrativeForStoryDirectionChange()
{
	const bool bOpeningWordingWasPending = bOpeningChoiceWordingPending;
	const bool bPrefetchWasInFlight = bCombatNarrativePrefetchRequestInFlight;
	const bool bPrefetchWasWaitingForReward = bWaitingForCombatNarrativeAfterReward;
	// CardForge is a separate, already-selected branch. It has no RP flow serial
	// and must keep its own completion/return path when settings are saved.
	const bool bCardForgeWasInFlight = PendingCardForgeChoiceIndex != INDEX_NONE;
	const bool bOrdinaryNarrativeWasInFlight = bInfiniteNarrativeRequestInFlight
		&& !bOpeningWordingWasPending && !bPrefetchWasInFlight && !bCardForgeWasInFlight;
	const bool bNarrativeTransportToCancel = bOpeningWordingWasPending
		|| bOrdinaryNarrativeWasInFlight || bPrefetchWasInFlight;

	// The service owns one transport at a time. Cancel it before advancing both
	// controller serials; this prevents an old ordinary stream or opening wording
	// response from repainting the settings page after the user saved a new direction.
	if (bNarrativeTransportToCancel && InfiniteNarrativeService)
		InfiniteNarrativeService->CancelGeneration();
	if (bNarrativeTransportToCancel)
	{
		++InfiniteNarrativeFlowSerial;
		++OpeningWordingRequestSerial;
	}

	if (bOpeningWordingWasPending)
	{
		// Keep the authored opening, relic IDs, and pending-save record intact. The
		// just-saved direction may not retroactively rewrite fixed prose; only the
		// optional wording is abandoned in favor of its deterministic local fallback.
		HandleAuthoredOpeningChoiceWordingReady(false, TArray<FString>(),
			TEXT("剧情走向已更改；开场选择文案使用本地衔接文案"));
	}
	else if (bOrdinaryNarrativeWasInFlight)
	{
		bInfiniteNarrativeRequestInFlight = false;
		bInputLocked = false;
		PendingInfiniteOpeningId.Reset();
		// The previously visible turn was generated under a different literary
		// contract. Keep it readable for continuity, but never let its choices be
		// submitted again after returning from Settings. The retry button below
		// reuses the already-recorded freeform action when there was one.
		bInfiniteNarrativeNeedsRefresh = true;
	}
	else if (bPrefetchWasInFlight)
	{
		// Prefetch shares the service's transport flag but not the ordinary RP
		// request. Clear both halves on cancellation so the controller cannot stay
		// permanently locked, while retaining an explicit retry route if reward flow
		// was already waiting for this result.
		bInfiniteNarrativeRequestInFlight = false;
		bInputLocked = false;
		bCombatNarrativePrefetchNeedsRetry = bPrefetchWasWaitingForReward;
	}

	bCombatNarrativePrefetchRequestInFlight = false;
	bCombatNarrativePrefetchReady = false;
	bCombatNarrativePrefetchFailed = bPrefetchWasInFlight;
	bWaitingForCombatNarrativeAfterReward = false;
	bHasCombatNarrativePrefetchStreamUpdate = false;
	CombatNarrativePrefetchStreamUpdate = FInfiniteNarrativeStreamUpdate();
	CombatNarrativePrefetchedBeat = FInfiniteNarrativeBeat();
	CombatNarrativePrefetchStoryDirectionCacheKey.Reset();
	bDiscardCombatNarrativePrefetch = bPrefetchWasInFlight;
	CombatNarrativePrefetchDiagnostic = bPrefetchWasInFlight
		? TEXT("剧情走向已更改，旧战后预取已作废") : FString();

	if (bOrdinaryNarrativeWasInFlight || bOpeningWordingWasPending)
	{
		// Drop the old ordinary stream tree as well. ReturnFromSettings will rebuild
		// the last committed beat from CurrentInfiniteBeat without issuing a request.
		ResetInfiniteNarrativeStreamPreview();
	}
}

void AAscendPlayerController::ApplySettingsWidgetsTo(
	FInfiniteNarrativeSettings& Target, bool bCommitExternalImports)
{
	FInfiniteNarrativeSettings& TargetSettings = Target;
	const FString PreviousStoryDirectionCacheKey = bCommitExternalImports
		? UInfiniteNarrativeService::BuildStoryDirectionCacheKey(this->InfiniteNarrativeSettings)
		: FString();
	if (SettingsEndpointInput) TargetSettings.Endpoint = SettingsEndpointInput->GetText().ToString().TrimStartAndEnd();
	if (SettingsApiKeyInput) TargetSettings.ApiKey = SettingsApiKeyInput->GetText().ToString().TrimStartAndEnd();
	if (SettingsModelInput) TargetSettings.Model = SettingsModelInput->GetText().ToString().TrimStartAndEnd();
	if (SettingsMvuModelInput) TargetSettings.MvuVerifierModel = SettingsMvuModelInput->GetText().ToString().TrimStartAndEnd();
	if (SettingsShowInputCheckBox) TargetSettings.bShowFreeformInput = SettingsShowInputCheckBox->IsChecked();
	if (SettingsShowPortraitCheckBox) TargetSettings.bShowSpeakerPortrait = SettingsShowPortraitCheckBox->IsChecked();
	if (SettingsScreenShakeCheckBox) TargetSettings.bEnableScreenShake = SettingsScreenShakeCheckBox->IsChecked();
	if (SettingsReduceFlashingCheckBox) TargetSettings.bReduceFlashing = SettingsReduceFlashingCheckBox->IsChecked();
	if (SettingsStructuredMemoryCheckBox) TargetSettings.bEnableStructuredMemory = SettingsStructuredMemoryCheckBox->IsChecked();
	if (SettingsContinuityCheckBox) TargetSettings.bEnableContinuityChecklist = SettingsContinuityCheckBox->IsChecked();
	if (SettingsCombatGenerationTimingCombo)
		TargetSettings.bGenerateAfterCombatWithLog =
			SettingsCombatGenerationTimingCombo->GetSelectedOption().StartsWith(TEXT("模式A"));
	auto ReadInt = [](UEditableTextBox* Input, int32 Current, int32 Min, int32 Max)
	{
		return Input ? FMath::Clamp(FCString::Atoi(*Input->GetText().ToString()), Min, Max) : Current;
	};
	TargetSettings.InputContextTokens = ReadInt(SettingsInputContextInput, TargetSettings.InputContextTokens, 8192, 2000000);
	TargetSettings.MaxOutputTokens = ReadInt(SettingsMaxOutputInput, TargetSettings.MaxOutputTokens, 1024, 262144);
	TargetSettings.MvuMaxOutputTokens = ReadInt(SettingsMvuMaxOutputInput,
		TargetSettings.MvuMaxOutputTokens, 2048, 262144);
	TargetSettings.NarrativeMinChars = ReadInt(SettingsNarrativeMinInput, TargetSettings.NarrativeMinChars, 200, 20000);
	TargetSettings.NarrativeMaxChars = ReadInt(SettingsNarrativeMaxInput, TargetSettings.NarrativeMaxChars,
		TargetSettings.NarrativeMinChars, 30000);
	TargetSettings.RecentRawRounds = ReadInt(SettingsRecentRoundsInput, TargetSettings.RecentRawRounds, 0, 100000);
	TargetSettings.CompressAfterRounds = ReadInt(SettingsCompressRoundsInput,
		TargetSettings.CompressAfterRounds, 0, 100000);
	TargetSettings.UnsummarizedTokenThreshold = ReadInt(SettingsUnsummarizedTokensInput,
		TargetSettings.UnsummarizedTokenThreshold, 4000, 200000);
	TargetSettings.MemoryTokenBudget = ReadInt(SettingsMemoryBudgetInput, TargetSettings.MemoryTokenBudget, 1000, 100000);
	TargetSettings.WorldBookTokenBudget = ReadInt(SettingsWorldBookBudgetInput, TargetSettings.WorldBookTokenBudget, 1000, 100000);
	TargetSettings.WorldInfoScanDepth = ReadInt(SettingsWorldInfoScanDepthInput,
		TargetSettings.WorldInfoScanDepth, 0, 100000);
	TargetSettings.Seed = ReadInt(SettingsSeedInput, TargetSettings.Seed, -1, MAX_int32);
	auto ReadFloat = [](UEditableTextBox* Input, float Current, float Min, float Max)
	{
		return Input ? FMath::Clamp(FCString::Atof(*Input->GetText().ToString()), Min, Max) : Current;
	};
	TargetSettings.Temperature = ReadFloat(SettingsTemperatureInput, TargetSettings.Temperature, 0.f, 1.5f);
	TargetSettings.TopP = ReadFloat(SettingsTopPInput, TargetSettings.TopP, 0.f, 1.f);
	TargetSettings.TopK = ReadFloat(SettingsTopKInput, TargetSettings.TopK, 0.f, 1000.f);
	TargetSettings.TopA = ReadFloat(SettingsTopAInput, TargetSettings.TopA, 0.f, 1.f);
	TargetSettings.MinP = ReadFloat(SettingsMinPInput, TargetSettings.MinP, 0.f, 1.f);
	TargetSettings.FrequencyPenalty = ReadFloat(SettingsFrequencyPenaltyInput,
		TargetSettings.FrequencyPenalty, -2.f, 2.f);
	TargetSettings.PresencePenalty = ReadFloat(SettingsPresencePenaltyInput,
		TargetSettings.PresencePenalty, -2.f, 2.f);
	TargetSettings.RepetitionPenalty = ReadFloat(SettingsRepetitionPenaltyInput,
		TargetSettings.RepetitionPenalty, 0.f, 2.f);
	TargetSettings.TimeoutSeconds = ReadFloat(SettingsTimeoutInput, TargetSettings.TimeoutSeconds, 5.f, 180.f);
	TargetSettings.SfxVolume = ReadFloat(SettingsSfxVolumeInput, TargetSettings.SfxVolume, 0.f, 1.f);
	if (SettingsSfxVolumeSlider)
		TargetSettings.SfxVolume = FMath::Clamp(SettingsSfxVolumeSlider->GetValue(), 0.f, 1.f);
	if (SettingsMusicVolumeSlider)
		TargetSettings.MusicVolume = FMath::Clamp(SettingsMusicVolumeSlider->GetValue(), 0.f, 1.f);
	else
		TargetSettings.MusicVolume = ReadFloat(SettingsMusicVolumeInput, TargetSettings.MusicVolume, 0.f, 1.f);
	if (AudioRouter)
		AudioRouter->SetVolumes(TargetSettings.SfxVolume, TargetSettings.MusicVolume);
	if (SettingsCardForgeReasoningSlider)
		TargetSettings.CardForgeReasoningEffort = CardForgeReasoningEffortFromSlider(
			SettingsCardForgeReasoningSlider->GetValue());
	if (SettingsReasoningEffortCombo)
		TargetSettings.ReasoningEffort = SettingsReasoningEffortCombo->GetSelectedOption();
	if (SettingsMvuReasoningEffortCombo)
		TargetSettings.MvuReasoningEffort = SettingsMvuReasoningEffortCombo->GetSelectedOption();
	if (SettingsStreamResponseCheckBox)
		TargetSettings.bStreamResponse = SettingsStreamResponseCheckBox->IsChecked();
	if (SettingsRequestReasoningCheckBox)
		TargetSettings.bRequestReasoning = SettingsRequestReasoningCheckBox->IsChecked();
	if (SettingsRequestMvuReasoningCheckBox)
		TargetSettings.bRequestMvuReasoning = SettingsRequestMvuReasoningCheckBox->IsChecked();
	if (SettingsTemporaryReasoningCheckBox)
		TargetSettings.bEnableTemporaryReasoningPreInjection = SettingsTemporaryReasoningCheckBox->IsChecked();
	if (SettingsReasoningPrefillCheckBox)
		TargetSettings.bEnableReasoningPrefill = SettingsReasoningPrefillCheckBox->IsChecked();
	if (SettingsReasoningPrefillInput)
		TargetSettings.ReasoningPrefill = SettingsReasoningPrefillInput->GetText().ToString().TrimStartAndEnd();
	if (SettingsMaintenanceMarkerCheckBox)
		TargetSettings.bMaintenanceMarkerEnabled = SettingsMaintenanceMarkerCheckBox->IsChecked();
	if (SettingsMaintenanceMarkerInput)
		TargetSettings.MaintenanceMarker = SettingsMaintenanceMarkerInput->GetText().ToString().TrimStartAndEnd();
	if (TargetSettings.MaintenanceMarker.IsEmpty())
		TargetSettings.MaintenanceMarker = TEXT("[继续遵循既定角色,关系与写作要求,直接回应本条消息]");
	if (SettingsStopStringsInput) TargetSettings.StopStrings = SettingsStopStringsInput->GetText().ToString();
	if (SettingsNarrativePresetPathInput) TargetSettings.NarrativePromptPresetPath =
		SettingsNarrativePresetPathInput->GetText().ToString().TrimStartAndEnd();
	if (SettingsMvuPresetPathInput) TargetSettings.MvuPromptPresetPath =
		SettingsMvuPresetPathInput->GetText().ToString().TrimStartAndEnd();
	if (SettingsPersonaInput) TargetSettings.PersonaDescription = SettingsPersonaInput->GetText().ToString();
	if (SettingsCharacterDescriptionInput) TargetSettings.CharacterDescription = SettingsCharacterDescriptionInput->GetText().ToString();
	if (SettingsCharacterPersonalityInput) TargetSettings.CharacterPersonality = SettingsCharacterPersonalityInput->GetText().ToString();
	if (SettingsScenarioInput) TargetSettings.Scenario = SettingsScenarioInput->GetText().ToString();
	if (SettingsDialogueExamplesInput) TargetSettings.DialogueExamples = SettingsDialogueExamplesInput->GetText().ToString();
	if (SettingsAuthorNoteInput) TargetSettings.AuthorNote = SettingsAuthorNoteInput->GetText().ToString();
	if (SettingsStoryDirectionInput)
		TargetSettings.StoryDirection = SettingsStoryDirectionInput->GetText().ToString().TrimStartAndEnd();
	if (SettingsStoryDirectionCheckBox)
		TargetSettings.bStoryDirectionEnabled = SettingsStoryDirectionCheckBox->IsChecked();
	if (SettingsAllowImportedContentCheckBox)
		TargetSettings.bAllowImportedContentToNarrativeModel = SettingsAllowImportedContentCheckBox->IsChecked();
	if (SettingsWorldBookCombo)
	{
		const FString Selected = SettingsWorldBookCombo->GetSelectedOption();
		if (Selected != TEXT("（不使用导入世界书）")) TargetSettings.WorldBookId = Selected;
		else TargetSettings.WorldBookId.Reset();
	}
	if (SettingsWorldBookEnabledCheckBox)
		TargetSettings.bWorldBookEnabled = SettingsWorldBookEnabledCheckBox->IsChecked();
	if (SettingsCharacterCardCombo)
	{
		const FString Selected = SettingsCharacterCardCombo->GetSelectedOption();
		if (Selected != TEXT("（不使用导入角色卡）")) TargetSettings.CharacterCardId = Selected;
		else TargetSettings.CharacterCardId.Reset();
	}
	if (SettingsCharacterCardEnabledCheckBox)
		TargetSettings.bCharacterCardEnabled = SettingsCharacterCardEnabledCheckBox->IsChecked();
	if (SettingsEmbeddedCharacterBookCheckBox)
		TargetSettings.bUseEmbeddedCharacterBook = SettingsEmbeddedCharacterBookCheckBox->IsChecked();
	if (SettingsWorldBookImportPathInput)
		PendingWorldBookImportPath = SettingsWorldBookImportPathInput->GetText().ToString().TrimStartAndEnd();
	if (SettingsCharacterCardImportPathInput)
		PendingCharacterCardImportPath = SettingsCharacterCardImportPathInput->GetText().ToString().TrimStartAndEnd();
	if (bCommitExternalImports && !PendingWorldBookImportPath.IsEmpty())
	{
		FNarrativeWorldBookAsset Imported;
		FString ImportError;
		if (FNarrativeContentLibrary::ImportWorldBook(PendingWorldBookImportPath, Imported, ImportError))
		{
			TargetSettings.WorldBookId = Imported.Id;
			TargetSettings.bWorldBookEnabled = true;
			UE_LOG(LogTemp, Display, TEXT("[InfiniteRP] imported worldbook id=%s entries=%d"),
				*Imported.Id, Imported.EntryCount);
		}
		else UE_LOG(LogTemp, Warning, TEXT("[InfiniteRP] worldbook import failed: %s"), *ImportError);
	}
	if (bCommitExternalImports && !PendingCharacterCardImportPath.IsEmpty())
	{
		FNarrativeCharacterCardAsset Imported;
		FString ImportError;
		if (FNarrativeContentLibrary::ImportCharacterCard(PendingCharacterCardImportPath, Imported, ImportError))
		{
			TargetSettings.CharacterCardId = Imported.Id;
			TargetSettings.bCharacterCardEnabled = true;
			UE_LOG(LogTemp, Display, TEXT("[InfiniteRP] imported character card id=%s format=%s"),
				*Imported.Id, *Imported.SourceFormat);
		}
		else UE_LOG(LogTemp, Warning, TEXT("[InfiniteRP] character card import failed: %s"), *ImportError);
	}
	if (bCommitExternalImports)
	{
		PendingWorldBookImportPath.Reset();
		PendingCharacterCardImportPath.Reset();
	}
	if (SettingsCustomWorldBookInput)
	{
		TargetSettings.WorldBookOverride = SettingsCustomWorldBookInput->GetText().ToString().TrimStartAndEnd();
		// 首次通过新版完整编辑器保存后，旧版追加段已经并入全文，避免重复注入。
		TargetSettings.CustomWorldBook.Reset();
	}
	if (SettingsCharacterRegistryInput) TargetSettings.CharacterRegistryOverride = SettingsCharacterRegistryInput->GetText().ToString().TrimStartAndEnd();
	if (SettingsOpeningTitleInput) TargetSettings.OpeningTitleOverride = SettingsOpeningTitleInput->GetText().ToString().TrimStartAndEnd();
	if (SettingsOpeningSpeakerInput) TargetSettings.OpeningSpeakerOverride = SettingsOpeningSpeakerInput->GetText().ToString().TrimStartAndEnd();
	if (SettingsOpeningPortraitInput) TargetSettings.OpeningPortraitOverride = SettingsOpeningPortraitInput->GetText().ToString().TrimStartAndEnd();
	if (SettingsOpeningExpressionInput) TargetSettings.OpeningExpressionOverride = SettingsOpeningExpressionInput->GetText().ToString().TrimStartAndEnd();
	if (SettingsOpeningNarrationInput) TargetSettings.OpeningNarrationOverride = SettingsOpeningNarrationInput->GetText().ToString().TrimStartAndEnd();
	if (SettingsOpeningDialogueInput) TargetSettings.OpeningDialogueOverride = SettingsOpeningDialogueInput->GetText().ToString().TrimStartAndEnd();
	if (SettingsContinuityInput) TargetSettings.CustomContinuityChecklist = SettingsContinuityInput->GetText().ToString().TrimStartAndEnd();
	if (TargetSettings.Model.IsEmpty()) TargetSettings.Model = TEXT("local-model");
	const FString NewStoryDirectionCacheKey =
		UInfiniteNarrativeService::BuildStoryDirectionCacheKey(TargetSettings);
	if (bCommitExternalImports && PreviousStoryDirectionCacheKey != NewStoryDirectionCacheKey)
	{
		// A direction edit changes the literary contract of an in-flight or ready
		// prefetch. Invalidate only that speculative result; the active run, combat
		// state, and user-visible settings remain intact. The completion callback
		// also checks the fingerprint, covering a late response after this save.
		bCombatNarrativePrefetchReady = false;
		bHasCombatNarrativePrefetchStreamUpdate = false;
		CombatNarrativePrefetchedBeat = FInfiniteNarrativeBeat();
		CombatNarrativePrefetchStreamUpdate = FInfiniteNarrativeStreamUpdate();
		CombatNarrativePrefetchStoryDirectionCacheKey.Reset();
		bDiscardCombatNarrativePrefetch = bCombatNarrativePrefetchRequestInFlight;
		InvalidateNarrativeForStoryDirectionChange();
		UE_LOG(LogTemp, Display, TEXT("[InfiniteRP] story direction changed; stale combat prefetch invalidated"));
	}
	// A category switch captures only into SettingsDraft. It must not write the
	// dedicated INI, import assets, or invalidate any live generation.
	if (!bCommitExternalImports || !GConfig) return;
	const FString NarrativeConfigFile = GetNarrativeUserConfigPath();
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(NarrativeConfigFile), true);
	if (!FPaths::FileExists(NarrativeConfigFile))
	{
		FFileHelper::SaveStringToFile(TEXT("; AscendSpire local AI/RP settings\n"), *NarrativeConfigFile);
	}
	GConfig->LoadFile(NarrativeConfigFile);
	GConfig->SetInt(NarrativeConfigSection, TEXT("SettingsSchemaVersion"), 9, NarrativeConfigFile);
	FString StoredEndpoint = TargetSettings.Endpoint;
	const bool bEndpointUseHttps = StoredEndpoint.RemoveFromStart(TEXT("https://"), ESearchCase::IgnoreCase);
	if (!bEndpointUseHttps) StoredEndpoint.RemoveFromStart(TEXT("http://"), ESearchCase::IgnoreCase);
	GConfig->SetString(NarrativeConfigSection, TEXT("Endpoint"), *StoredEndpoint, NarrativeConfigFile);
	GConfig->SetBool(NarrativeConfigSection, TEXT("EndpointUseHttps"), bEndpointUseHttps, NarrativeConfigFile);
	GConfig->SetString(NarrativeConfigSection, TEXT("ApiKey"), *TargetSettings.ApiKey, NarrativeConfigFile);
	GConfig->SetString(NarrativeConfigSection, TEXT("Model"), *TargetSettings.Model, NarrativeConfigFile);
	GConfig->SetString(NarrativeConfigSection, TEXT("MvuVerifierModel"), *TargetSettings.MvuVerifierModel, NarrativeConfigFile);
	GConfig->SetBool(NarrativeConfigSection, TEXT("ShowFreeformInput"),
		TargetSettings.bShowFreeformInput, NarrativeConfigFile);
	GConfig->SetBool(NarrativeConfigSection, TEXT("FreeRPMode"),
		TargetSettings.bShowFreeformInput, NarrativeConfigFile);
	GConfig->SetBool(NarrativeConfigSection, TEXT("ShowSpeakerPortrait"), TargetSettings.bShowSpeakerPortrait, NarrativeConfigFile);
	GConfig->SetBool(NarrativeConfigSection, TEXT("EnableScreenShake"), TargetSettings.bEnableScreenShake, NarrativeConfigFile);
	GConfig->SetBool(NarrativeConfigSection, TEXT("ReduceFlashing"), TargetSettings.bReduceFlashing, NarrativeConfigFile);
	GConfig->SetBool(NarrativeConfigSection, TEXT("StructuredMemory"), TargetSettings.bEnableStructuredMemory, NarrativeConfigFile);
	GConfig->SetBool(NarrativeConfigSection, TEXT("ContinuityChecklist"), TargetSettings.bEnableContinuityChecklist, NarrativeConfigFile);
	GConfig->SetBool(NarrativeConfigSection, TEXT("GenerateAfterCombatWithLog"),
		TargetSettings.bGenerateAfterCombatWithLog, NarrativeConfigFile);
	GConfig->SetInt(NarrativeConfigSection, TEXT("InputContextTokens"), TargetSettings.InputContextTokens, NarrativeConfigFile);
	GConfig->SetInt(NarrativeConfigSection, TEXT("MaxOutputTokens"), TargetSettings.MaxOutputTokens, NarrativeConfigFile);
	GConfig->SetInt(NarrativeConfigSection, TEXT("MvuMaxOutputTokens"), TargetSettings.MvuMaxOutputTokens, NarrativeConfigFile);
	GConfig->SetInt(NarrativeConfigSection, TEXT("NarrativeMinChars"), TargetSettings.NarrativeMinChars, NarrativeConfigFile);
	GConfig->SetInt(NarrativeConfigSection, TEXT("NarrativeMaxChars"), TargetSettings.NarrativeMaxChars, NarrativeConfigFile);
	GConfig->SetInt(NarrativeConfigSection, TEXT("RecentRawRounds"), TargetSettings.RecentRawRounds, NarrativeConfigFile);
	GConfig->SetInt(NarrativeConfigSection, TEXT("CompressAfterRounds"), TargetSettings.CompressAfterRounds, NarrativeConfigFile);
	GConfig->SetInt(NarrativeConfigSection, TEXT("UnsummarizedTokenThreshold"), TargetSettings.UnsummarizedTokenThreshold, NarrativeConfigFile);
	GConfig->SetInt(NarrativeConfigSection, TEXT("MemoryTokenBudget"), TargetSettings.MemoryTokenBudget, NarrativeConfigFile);
	GConfig->SetInt(NarrativeConfigSection, TEXT("WorldBookTokenBudget"), TargetSettings.WorldBookTokenBudget, NarrativeConfigFile);
	GConfig->SetInt(NarrativeConfigSection, TEXT("WorldInfoScanDepth"), TargetSettings.WorldInfoScanDepth, NarrativeConfigFile);
	GConfig->SetInt(NarrativeConfigSection, TEXT("Seed"), TargetSettings.Seed, NarrativeConfigFile);
	GConfig->SetString(NarrativeConfigSection, TEXT("ReasoningEffort"), *TargetSettings.ReasoningEffort, NarrativeConfigFile);
	GConfig->SetString(NarrativeConfigSection, TEXT("MvuReasoningEffort"), *TargetSettings.MvuReasoningEffort, NarrativeConfigFile);
	GConfig->SetString(NarrativeConfigSection, TEXT("CardForgeReasoningEffort"), *TargetSettings.CardForgeReasoningEffort, NarrativeConfigFile);
	GConfig->SetBool(NarrativeConfigSection, TEXT("RequestReasoning"), TargetSettings.bRequestReasoning, NarrativeConfigFile);
	GConfig->SetBool(NarrativeConfigSection, TEXT("RequestMvuReasoning"), TargetSettings.bRequestMvuReasoning, NarrativeConfigFile);
	GConfig->SetBool(NarrativeConfigSection, TEXT("ShowReasoning"), TargetSettings.bShowReasoning, NarrativeConfigFile);
	GConfig->SetBool(NarrativeConfigSection, TEXT("EnableReasoningPrefill"),
		TargetSettings.bEnableReasoningPrefill, NarrativeConfigFile);
	GConfig->SetString(NarrativeConfigSection, TEXT("ReasoningPrefill"),
		*TargetSettings.ReasoningPrefill, NarrativeConfigFile);
	GConfig->SetBool(NarrativeConfigSection, TEXT("EnableTemporaryReasoningPreInjection"),
		TargetSettings.bEnableTemporaryReasoningPreInjection, NarrativeConfigFile);
	GConfig->SetBool(NarrativeConfigSection, TEXT("MaintenanceMarkerEnabled"),
		TargetSettings.bMaintenanceMarkerEnabled, NarrativeConfigFile);
	GConfig->SetString(NarrativeConfigSection, TEXT("MaintenanceMarker"),
		*TargetSettings.MaintenanceMarker, NarrativeConfigFile);
	GConfig->SetBool(NarrativeConfigSection, TEXT("StreamResponse"), TargetSettings.bStreamResponse, NarrativeConfigFile);
	GConfig->SetString(NarrativeConfigSection, TEXT("StopStrings"), *TargetSettings.StopStrings, NarrativeConfigFile);
	GConfig->SetString(NarrativeConfigSection, TEXT("NarrativePromptPresetPath"), *TargetSettings.NarrativePromptPresetPath, NarrativeConfigFile);
	GConfig->SetString(NarrativeConfigSection, TEXT("MvuPromptPresetPath"), *TargetSettings.MvuPromptPresetPath, NarrativeConfigFile);
	GConfig->SetString(NarrativeConfigSection, TEXT("NarrativePromptPresetOverride"), *TargetSettings.NarrativePromptPresetOverride, NarrativeConfigFile);
	GConfig->SetString(NarrativeConfigSection, TEXT("MvuPromptPresetOverride"), *TargetSettings.MvuPromptPresetOverride, NarrativeConfigFile);
	GConfig->SetString(NarrativeConfigSection, TEXT("PersonaDescription"), *TargetSettings.PersonaDescription, NarrativeConfigFile);
	GConfig->SetString(NarrativeConfigSection, TEXT("CharacterDescription"), *TargetSettings.CharacterDescription, NarrativeConfigFile);
	GConfig->SetString(NarrativeConfigSection, TEXT("CharacterPersonality"), *TargetSettings.CharacterPersonality, NarrativeConfigFile);
	GConfig->SetString(NarrativeConfigSection, TEXT("Scenario"), *TargetSettings.Scenario, NarrativeConfigFile);
	GConfig->SetString(NarrativeConfigSection, TEXT("DialogueExamples"), *TargetSettings.DialogueExamples, NarrativeConfigFile);
	GConfig->SetString(NarrativeConfigSection, TEXT("AuthorNote"), *TargetSettings.AuthorNote, NarrativeConfigFile);
	GConfig->SetString(NarrativeConfigSection, TEXT("StoryDirection"), *TargetSettings.StoryDirection, NarrativeConfigFile);
	GConfig->SetBool(NarrativeConfigSection, TEXT("StoryDirectionEnabled"), TargetSettings.bStoryDirectionEnabled, NarrativeConfigFile);
	GConfig->SetString(NarrativeConfigSection, TEXT("WorldBookId"), *TargetSettings.WorldBookId, NarrativeConfigFile);
	GConfig->SetBool(NarrativeConfigSection, TEXT("WorldBookEnabled"), TargetSettings.bWorldBookEnabled, NarrativeConfigFile);
	GConfig->SetString(NarrativeConfigSection, TEXT("CharacterCardId"), *TargetSettings.CharacterCardId, NarrativeConfigFile);
	GConfig->SetBool(NarrativeConfigSection, TEXT("CharacterCardEnabled"), TargetSettings.bCharacterCardEnabled, NarrativeConfigFile);
	GConfig->SetBool(NarrativeConfigSection, TEXT("EmbeddedCharacterBook"), TargetSettings.bUseEmbeddedCharacterBook, NarrativeConfigFile);
	GConfig->SetBool(NarrativeConfigSection, TEXT("AllowImportedContentToNarrativeModel"),
		TargetSettings.bAllowImportedContentToNarrativeModel, NarrativeConfigFile);
	GConfig->SetString(NarrativeConfigSection, TEXT("WorldBookOverride"), *TargetSettings.WorldBookOverride, NarrativeConfigFile);
	GConfig->SetString(NarrativeConfigSection, TEXT("CharacterRegistryOverride"), *TargetSettings.CharacterRegistryOverride, NarrativeConfigFile);
	GConfig->SetString(NarrativeConfigSection, TEXT("OpeningTitleOverride"), *TargetSettings.OpeningTitleOverride, NarrativeConfigFile);
	GConfig->SetString(NarrativeConfigSection, TEXT("OpeningSpeakerOverride"), *TargetSettings.OpeningSpeakerOverride, NarrativeConfigFile);
	GConfig->SetString(NarrativeConfigSection, TEXT("OpeningPortraitOverride"), *TargetSettings.OpeningPortraitOverride, NarrativeConfigFile);
	GConfig->SetString(NarrativeConfigSection, TEXT("OpeningExpressionOverride"), *TargetSettings.OpeningExpressionOverride, NarrativeConfigFile);
	GConfig->SetString(NarrativeConfigSection, TEXT("OpeningNarrationOverride"), *TargetSettings.OpeningNarrationOverride, NarrativeConfigFile);
	GConfig->SetString(NarrativeConfigSection, TEXT("OpeningDialogueOverride"), *TargetSettings.OpeningDialogueOverride, NarrativeConfigFile);
	GConfig->SetString(NarrativeConfigSection, TEXT("CustomWorldBook"), *TargetSettings.CustomWorldBook, NarrativeConfigFile);
	GConfig->SetString(NarrativeConfigSection, TEXT("CustomContinuityChecklist"), *TargetSettings.CustomContinuityChecklist, NarrativeConfigFile);
	GConfig->SetFloat(NarrativeConfigSection, TEXT("Temperature"), TargetSettings.Temperature, NarrativeConfigFile);
	GConfig->SetFloat(NarrativeConfigSection, TEXT("TopP"), TargetSettings.TopP, NarrativeConfigFile);
	GConfig->SetFloat(NarrativeConfigSection, TEXT("TopK"), TargetSettings.TopK, NarrativeConfigFile);
	GConfig->SetFloat(NarrativeConfigSection, TEXT("TopA"), TargetSettings.TopA, NarrativeConfigFile);
	GConfig->SetFloat(NarrativeConfigSection, TEXT("MinP"), TargetSettings.MinP, NarrativeConfigFile);
	GConfig->SetFloat(NarrativeConfigSection, TEXT("FrequencyPenalty"), TargetSettings.FrequencyPenalty, NarrativeConfigFile);
	GConfig->SetFloat(NarrativeConfigSection, TEXT("PresencePenalty"), TargetSettings.PresencePenalty, NarrativeConfigFile);
	GConfig->SetFloat(NarrativeConfigSection, TEXT("RepetitionPenalty"), TargetSettings.RepetitionPenalty, NarrativeConfigFile);
	GConfig->SetFloat(NarrativeConfigSection, TEXT("TimeoutSeconds"), TargetSettings.TimeoutSeconds, NarrativeConfigFile);
	GConfig->SetFloat(NarrativeConfigSection, TEXT("SfxVolume"), TargetSettings.SfxVolume, NarrativeConfigFile);
	GConfig->SetFloat(NarrativeConfigSection, TEXT("MusicVolume"), TargetSettings.MusicVolume, NarrativeConfigFile);
	GConfig->Flush(false, NarrativeConfigFile);
	UE_LOG(LogTemp, Display, TEXT("[InfiniteRP] settings saved to dedicated file=%s"), *NarrativeConfigFile);
}

void AAscendPlayerController::SaveInfiniteNarrativeSettings()
{
	if (bSettingsDraftActive)
	{
		ApplySettingsWidgetsTo(SettingsDraft, true);
		InfiniteNarrativeSettings = SettingsDraft;
		bSettingsDraftActive = false;
		SettingsDraftBaseline = InfiniteNarrativeSettings;
		SettingsDraft = InfiniteNarrativeSettings;
		return;
	}
	// Compatibility for a legacy caller which opens no draft page. Normal UI always
	// takes the draft path above, so this branch is intentionally small and explicit.
	ApplySettingsWidgetsTo(InfiniteNarrativeSettings, true);
}

void AAscendPlayerController::CancelSettingsDraft()
{
	if (!bSettingsDraftActive) return;
	InfiniteNarrativeSettings = SettingsDraftBaseline;
	SettingsDraft = InfiniteNarrativeSettings;
	SettingsDraftBaseline = InfiniteNarrativeSettings;
	bSettingsDraftActive = false;
	PendingWorldBookImportPath.Reset();
	PendingCharacterCardImportPath.Reset();
	if (AudioRouter)
		AudioRouter->SetVolumes(InfiniteNarrativeSettings.SfxVolume, InfiniteNarrativeSettings.MusicVolume);
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
	SettingsCardForgeReasoningSlider = nullptr;
	SettingsCardForgeReasoningValueText = nullptr;
	SettingsStreamResponseCheckBox = nullptr;
	SettingsRequestReasoningCheckBox = nullptr;
	SettingsRequestMvuReasoningCheckBox = nullptr;
	SettingsTemporaryReasoningCheckBox = nullptr;
	SettingsReasoningPrefillCheckBox = nullptr;
	SettingsReasoningPrefillInput = nullptr;
	SettingsMaintenanceMarkerCheckBox = nullptr;
	SettingsMaintenanceMarkerInput = nullptr;
	SettingsTimeoutInput = nullptr;
	SettingsSfxVolumeInput = nullptr;
	SettingsMusicVolumeInput = nullptr;
	SettingsSfxVolumeSlider = nullptr;
	SettingsMusicVolumeSlider = nullptr;
	SettingsSfxVolumeValueText = nullptr;
	SettingsMusicVolumeValueText = nullptr;
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
	SettingsStoryDirectionInput = nullptr;
	SettingsStoryDirectionCheckBox = nullptr;
	SettingsAllowImportedContentCheckBox = nullptr;
	SettingsWorldBookCombo = nullptr;
	SettingsWorldBookImportPathInput = nullptr;
	SettingsWorldBookEnabledCheckBox = nullptr;
	SettingsWorldBookStatusText = nullptr;
	SettingsCharacterCardCombo = nullptr;
	SettingsCharacterCardImportPathInput = nullptr;
	SettingsCharacterCardEnabledCheckBox = nullptr;
	SettingsEmbeddedCharacterBookCheckBox = nullptr;
	SettingsCharacterCardStatusText = nullptr;
	SettingsPromptPreviewInput = nullptr;
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
	SettingsReduceFlashingCheckBox = nullptr;
	SettingsCombatGenerationTimingCombo = nullptr;
	SettingsStructuredMemoryCheckBox = nullptr;
	SettingsContinuityCheckBox = nullptr;
}

void AAscendPlayerController::ShowSettings(int32 Category)
{
	if (Category >= 0) SettingsCategory = FMath::Clamp(Category, 0, 5);
	if (!RootOverlay) return;
	if (!bSettingsDraftActive)
	{
		SettingsDraft = InfiniteNarrativeSettings;
		SettingsDraftBaseline = InfiniteNarrativeSettings;
		bSettingsDraftActive = true;
		PendingWorldBookImportPath.Reset();
		PendingCharacterCardImportPath.Reset();
	}
	// All controls in this page bind to the draft. The active settings object is
	// changed only by SaveInfiniteNarrativeSettings, never by tab construction.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wshadow"
	FInfiniteNarrativeSettings& InfiniteNarrativeSettings = SettingsDraft;
#pragma clang diagnostic pop
	// Replacing a category must replace only the settings layer's controls. The
	// underlying ScreenHost is deliberately untouched so an in-flight RP callback
	// can update it without ever navigating away from this page.
	if (!SettingsOverlayLayer)
	{
		SettingsOverlayLayer = NewObject<UOverlay>(RootOverlay);
		UOverlaySlot* LayerSlot = RootOverlay->AddChildToOverlay(SettingsOverlayLayer);
		LayerSlot->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Fill);
		LayerSlot->SetVerticalAlignment(EVerticalAlignment::VAlign_Fill);
	}
	SettingsProxies.Reset();
	SettingsOverlayLayer->ClearChildren();
	PendingScreenProxies.Reset();
	SettingsOverlayLayer->SetVisibility(ESlateVisibility::Visible);
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

	auto AddCombo = [Box](const FString& Label, const TArray<FString>& Options,
		const FString& Selected) -> UComboBoxString*
	{
		RPAddToVBox(Box, RPMakeWrappedText(Box, Label, 16, FAscendUIStyle::PaperWhite(), ETextJustify::Left),
			FMargin(180.f, 8.f, 180.f, 2.f));
		UComboBoxString* Combo = NewObject<UComboBoxString>(Box);
		for (const FString& Option : Options) Combo->AddOption(Option);
		if (!Selected.IsEmpty() && !Options.Contains(Selected)) Combo->AddOption(Selected);
		Combo->SetSelectedOption(Selected.IsEmpty() ? Options[0] : Selected);
		RPAddToVBox(Box, Combo, FMargin(180.f, 0.f, 180.f, 8.f));
		return Combo;
	};

	// Keep the direction block at the top of every settings category so its
	// precedence is visible and users do not have to hunt through Prompt tabs.
	RPAddToVBox(Box, RPMakeWrappedText(Box, TEXT("剧情大纲与走向（最高剧情优先级）"), 25,
		FAscendUIStyle::GoldYellow()));
	RPAddToVBox(Box, RPMakeWrappedText(Box,
		TEXT("可填写阴暗绝望、欢乐无厘头、悬疑反转、细腻情感、热血成长等偏好；留空则由剧情导演自由发挥。它只影响叙事风格，不得覆盖引擎事实、输出契约或操作白名单。"),
		15, FAscendUIStyle::DimGray()), FMargin(140.f, 4.f, 140.f, 8.f));
	SettingsStoryDirectionCheckBox = AddToggle(TEXT("启用剧情大纲与走向"),
		InfiniteNarrativeSettings.bStoryDirectionEnabled);
	SettingsStoryDirectionInput = AddMultiLine(TEXT("剧情大纲与走向"),
		InfiniteNarrativeSettings.StoryDirection, 150.f);
	SettingsAllowImportedContentCheckBox = AddToggle(
		TEXT("允许将已启用的导入世界书/角色卡内容发送给剧情模型（默认关闭）"),
		InfiniteNarrativeSettings.bAllowImportedContentToNarrativeModel);

	if (SettingsCategory == 0)
	{
		RPAddToVBox(Box, RPMakeWrappedText(Box, TEXT("显示设置"), 25, FAscendUIStyle::GoldYellow()));
		SettingsShowPortraitCheckBox = AddToggle(TEXT("显示 RP 角色头像（当前无素材时使用角色专属颜色方块）"),
			InfiniteNarrativeSettings.bShowSpeakerPortrait);
		SettingsShowInputCheckBox = AddToggle(TEXT("启用自由 RP 模式（每隔一轮开放输入）"),
			InfiniteNarrativeSettings.bShowFreeformInput);
		RPAddToVBox(Box, RPMakeWrappedText(Box,
			TEXT("提交自由 RP 后，本轮的三个选项不会进入上下文；引擎会随机锁定一个方向，自动衔接一轮，期间不能选项或再次输入。自动衔接结束后重新开放自由 RP。"),
			14, FAscendUIStyle::DimGray()), FMargin(150.f, 0.f, 150.f, 10.f));
		SettingsScreenShakeCheckBox = AddToggle(TEXT("启用战斗屏幕震动"),
			InfiniteNarrativeSettings.bEnableScreenShake);
		SettingsReduceFlashingCheckBox = AddToggle(TEXT("减弱全屏闪光（光敏与视觉舒适）"),
			InfiniteNarrativeSettings.bReduceFlashing);
	}
	else if (SettingsCategory == 1)
	{
		RPAddToVBox(Box, RPMakeWrappedText(Box, TEXT("声音设置"), 25, FAscendUIStyle::GoldYellow()));
		RPAddToVBox(Box, RPMakeWrappedText(Box,
			TEXT("音量会实时生效；建议让背景音乐保持在 25%~40%，给卡牌和战斗反馈留出空间。"),
			15, FAscendUIStyle::DimGray()), FMargin(150.f, 4.f, 150.f, 12.f));

		SettingsSfxVolumeValueText = FAscendUIStyle::MakeText(Box,
			FString::Printf(TEXT("当前音效音量：%.0f%%"), InfiniteNarrativeSettings.SfxVolume * 100.f),
			16, FAscendUIStyle::PaperWhite());
		RPAddToVBox(Box, SettingsSfxVolumeValueText, FMargin(180.f, 4.f, 180.f, 2.f));
		SettingsSfxVolumeSlider = NewObject<USlider>(Box);
		SettingsSfxVolumeSlider->SetStepSize(0.05f);
		SettingsSfxVolumeSlider->SetValue(InfiniteNarrativeSettings.SfxVolume);
		SettingsSfxVolumeSlider->OnValueChanged.AddDynamic(this, &AAscendPlayerController::OnSfxVolumeChanged);
		RPAddToVBox(Box, SettingsSfxVolumeSlider, FMargin(180.f, 0.f, 180.f, 12.f));

		SettingsMusicVolumeValueText = FAscendUIStyle::MakeText(Box,
			FString::Printf(TEXT("当前背景音乐音量：%.0f%%"), InfiniteNarrativeSettings.MusicVolume * 100.f),
			16, FAscendUIStyle::PaperWhite());
		RPAddToVBox(Box, SettingsMusicVolumeValueText, FMargin(180.f, 4.f, 180.f, 2.f));
		SettingsMusicVolumeSlider = NewObject<USlider>(Box);
		SettingsMusicVolumeSlider->SetStepSize(0.05f);
		SettingsMusicVolumeSlider->SetValue(InfiniteNarrativeSettings.MusicVolume);
		SettingsMusicVolumeSlider->OnValueChanged.AddDynamic(this, &AAscendPlayerController::OnMusicVolumeChanged);
		RPAddToVBox(Box, SettingsMusicVolumeSlider, FMargin(180.f, 0.f, 180.f, 12.f));
		RPAddToVBox(Box, RPMakeWrappedText(Box,
			TEXT("音频按 Music / Gameplay SFX / UI 反馈语义分层；切换到战斗时旧音乐立即停止，新的战斗音乐淡入。"),
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
		SettingsTemporaryReasoningCheckBox = AddToggle(
			TEXT("允许格式重试时临时预注入 reasoning_content（仅本次请求，不写入历史；默认关闭）"),
			InfiniteNarrativeSettings.bEnableTemporaryReasoningPreInjection);
		SettingsReasoningPrefillCheckBox = AddToggle(
			TEXT("启用剧情 writer 的常规 ReasoningPrefill（仅本次请求，不写入历史；默认关闭）"),
			InfiniteNarrativeSettings.bEnableReasoningPrefill);
		SettingsReasoningPrefillInput = AddMultiLine(TEXT("ReasoningPrefill 文本"),
			InfiniteNarrativeSettings.ReasoningPrefill, 105.f);
		SettingsMaintenanceMarkerCheckBox = AddToggle(TEXT("为每条复制的 user 历史添加维护标记（默认开启）"),
			InfiniteNarrativeSettings.bMaintenanceMarkerEnabled);
		SettingsMaintenanceMarkerInput = AddField(TEXT("维护标记（默认精确文本）"),
			InfiniteNarrativeSettings.MaintenanceMarker, false);
		SettingsReasoningEffortCombo = AddReasoningCombo(TEXT("剧情模型思考强度（仅开启上项后生效；auto=服务商决定）"),
			InfiniteNarrativeSettings.ReasoningEffort);
		const FString CardForgeEffort = NormalizeCardForgeReasoningEffort(
			InfiniteNarrativeSettings.CardForgeReasoningEffort);
		SettingsCardForgeReasoningValueText = FAscendUIStyle::MakeText(Box,
			FString::Printf(TEXT("生卡模型思考强度：%s"), *CardForgeReasoningLabel(CardForgeEffort)),
			16, FAscendUIStyle::PaperWhite());
		RPAddToVBox(Box, SettingsCardForgeReasoningValueText, FMargin(180.f, 8.f, 180.f, 2.f));
		SettingsCardForgeReasoningSlider = NewObject<USlider>(Box);
		SettingsCardForgeReasoningSlider->SetStepSize(0.25f);
		SettingsCardForgeReasoningSlider->SetValue(CardForgeReasoningSliderValue(CardForgeEffort));
		SettingsCardForgeReasoningSlider->OnValueChanged.AddDynamic(
			this, &AAscendPlayerController::OnCardForgeReasoningChanged);
		RPAddToVBox(Box, SettingsCardForgeReasoningSlider, FMargin(180.f, 0.f, 180.f, 2.f));
		RPAddToVBox(Box, RPMakeWrappedText(Box,
			TEXT("生卡 agent 独立使用此强度：快速 / 低 / 中 / 高 / 最大，默认快速。调高会给模型更多内部推理空间；"
				"无论档位如何，明确的实现错误都会在同一生卡 session 中继续修复。"),
			14, FAscendUIStyle::DimGray()), FMargin(180.f, 2.f, 180.f, 8.f));
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
			TEXT("思考请求与思维链显示相互独立。剧情模型仍按上方设置；生卡 agent 使用下方独立的思考强度，默认 medium，并在每轮只输出工具 JSON，CardScript 只放在 script 字段中。"),
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
		RPAddToVBox(Box, RPMakeWrappedText(Box, TEXT("世界书库与角色卡库"), 21, FAscendUIStyle::GoldYellow()),
			FMargin(140.f, 16.f, 140.f, 6.f));
		RPAddToVBox(Box, RPMakeWrappedText(Box,
			TEXT("导入文件会原样保存在 Saved/Narrative；INI 只保存资产 ID。绿色表示已选、已启用且获得发送许可，黄色表示未启用或等待许可，红色表示资产读取失败，灰色表示未选择。"),
			14, FAscendUIStyle::DimGray()), FMargin(140.f, 2.f, 140.f, 10.f));
		TArray<FString> WorldBookOptions;
		WorldBookOptions.Add(TEXT("（不使用导入世界书）"));
		TArray<FNarrativeWorldBookAsset> WorldBooks;
		FNarrativeContentLibrary::ListWorldBooks(WorldBooks);
		for (const FNarrativeWorldBookAsset& Asset : WorldBooks) WorldBookOptions.Add(Asset.Id);
		SettingsWorldBookCombo = AddCombo(TEXT("导入世界书（下拉选择 ID）"), WorldBookOptions,
			InfiniteNarrativeSettings.WorldBookId);
		SettingsWorldBookEnabledCheckBox = AddToggle(TEXT("启用选中的导入世界书"),
			InfiniteNarrativeSettings.bWorldBookEnabled);
		SettingsWorldBookImportPathInput = AddField(TEXT("导入世界书 JSON 路径（保存时导入；不写入 INI）"),
			PendingWorldBookImportPath, false);
		FNarrativeWorldBookAsset SelectedWorldBook;
		FString WorldBookStatusError;
		const bool bWorldBookFound = !InfiniteNarrativeSettings.WorldBookId.IsEmpty()
			&& FNarrativeContentLibrary::LoadWorldBook(InfiniteNarrativeSettings.WorldBookId,
				SelectedWorldBook, WorldBookStatusError);
		const bool bWorldBookEffective = bWorldBookFound && InfiniteNarrativeSettings.bWorldBookEnabled
			&& InfiniteNarrativeSettings.bAllowImportedContentToNarrativeModel;
		const FString WorldBookLamp = InfiniteNarrativeSettings.WorldBookId.IsEmpty()
			? TEXT("● 灰色：未选择导入世界书")
			: !bWorldBookFound ? TEXT("● 红色：资产读取失败")
			: bWorldBookEffective ? FString::Printf(TEXT("● 绿色：%s（%d 条词条）"), *SelectedWorldBook.Name, SelectedWorldBook.EntryCount)
			: TEXT("● 黄色：已选择但未同时启用/授权");
		const FSlateColor WorldBookLampColor = InfiniteNarrativeSettings.WorldBookId.IsEmpty()
			? FAscendUIStyle::DimGray() : !bWorldBookFound ? FAscendUIStyle::BloodRed()
			: bWorldBookEffective ? FAscendUIStyle::JadeGreen() : FAscendUIStyle::GoldYellow();
		SettingsWorldBookStatusText = FAscendUIStyle::MakeText(Box, WorldBookLamp, 15,
			WorldBookLampColor);
		RPAddToVBox(Box, SettingsWorldBookStatusText, FMargin(180.f, 0.f, 180.f, 12.f));

		TArray<FString> CharacterCardOptions;
		CharacterCardOptions.Add(TEXT("（不使用导入角色卡）"));
		TArray<FNarrativeCharacterCardAsset> CharacterCards;
		FNarrativeContentLibrary::ListCharacterCards(CharacterCards);
		for (const FNarrativeCharacterCardAsset& Asset : CharacterCards) CharacterCardOptions.Add(Asset.Id);
		SettingsCharacterCardCombo = AddCombo(TEXT("导入角色卡（V1/V2/V3 JSON 或 PNG）"), CharacterCardOptions,
			InfiniteNarrativeSettings.CharacterCardId);
		SettingsCharacterCardEnabledCheckBox = AddToggle(TEXT("启用选中的导入角色卡"),
			InfiniteNarrativeSettings.bCharacterCardEnabled);
		SettingsEmbeddedCharacterBookCheckBox = AddToggle(TEXT("启用角色卡内嵌世界书 character_book"),
			InfiniteNarrativeSettings.bUseEmbeddedCharacterBook);
		SettingsCharacterCardImportPathInput = AddField(TEXT("导入角色卡路径（保存时导入；不写入 INI）"),
			PendingCharacterCardImportPath, false);
		FNarrativeCharacterCardAsset SelectedCharacterCard;
		FString CharacterCardStatusError;
		const bool bCharacterCardFound = !InfiniteNarrativeSettings.CharacterCardId.IsEmpty()
			&& FNarrativeContentLibrary::LoadCharacterCard(InfiniteNarrativeSettings.CharacterCardId,
				SelectedCharacterCard, CharacterCardStatusError);
		const bool bCharacterCardEffective = bCharacterCardFound && InfiniteNarrativeSettings.bCharacterCardEnabled
			&& InfiniteNarrativeSettings.bAllowImportedContentToNarrativeModel;
		FString CharacterCardLamp;
		if (InfiniteNarrativeSettings.CharacterCardId.IsEmpty()) CharacterCardLamp = TEXT("● 灰色：未选择导入角色卡");
		else if (!bCharacterCardFound) CharacterCardLamp = TEXT("● 红色：资产读取失败");
		else if (bCharacterCardEffective)
			CharacterCardLamp = FString::Printf(TEXT("● 绿色：%s%s%s"), *SelectedCharacterCard.Name,
				SelectedCharacterCard.bHasEmbeddedAvatar ? TEXT(" · 含头像") : TEXT(""),
				SelectedCharacterCard.bHasEmbeddedWorldBook ? TEXT(" · 含内嵌世界书") : TEXT(""));
		else CharacterCardLamp = TEXT("● 黄色：已选择但未同时启用/授权");
		const FSlateColor CharacterCardLampColor = InfiniteNarrativeSettings.CharacterCardId.IsEmpty()
			? FAscendUIStyle::DimGray() : !bCharacterCardFound ? FAscendUIStyle::BloodRed()
			: bCharacterCardEffective ? FAscendUIStyle::JadeGreen() : FAscendUIStyle::GoldYellow();
		SettingsCharacterCardStatusText = FAscendUIStyle::MakeText(Box, CharacterCardLamp, 15,
			CharacterCardLampColor);
		RPAddToVBox(Box, SettingsCharacterCardStatusText, FMargin(180.f, 0.f, 180.f, 12.f));

		RPAddToVBox(Box, RPMakeWrappedText(Box, TEXT("提示词生效预览"), 20, FAscendUIStyle::GoldYellow()),
			FMargin(140.f, 14.f, 140.f, 4.f));
		FString PromptPreview = TEXT("1. 引擎权威事实（不可覆盖）\n2. 剧情大纲与走向（最高剧情偏好，永久 system）\n");
		PromptPreview += InfiniteNarrativeSettings.bAllowImportedContentToNarrativeModel
			? TEXT("3. 已启用的世界书 / 角色卡 / character_book（仅影响叙事）\n")
			: TEXT("3. 外部导入内容当前未获发送许可（不会进入模型请求）\n");
		PromptPreview += TEXT("4. SillyTavern Prompt Manager 与逐条历史（user 历史前加默认继续标记）\n5. 当前行动与预抽路由\n6. 输出契约（不可覆盖）");
		SettingsPromptPreviewInput = AddMultiLine(TEXT("当前提示词顺序（只读预览）"), PromptPreview, 130.f);
		SettingsPromptPreviewInput->SetIsReadOnly(true);
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
		RPAddToVBox(Box, RPMakeWrappedText(Box,
			TEXT("开场白从游戏内置的有限精品开场中随机抽取；世界书与角色卡从第一次选择后的续写开始生效。"),
			16, FAscendUIStyle::DimGray()), FMargin(140.f, 18.f, 140.f, 6.f));
		SettingsContinuityInput = AddMultiLine(TEXT("自定义 COT/连续性检查清单（不显示模型思维过程）"),
			InfiniteNarrativeSettings.CustomContinuityChecklist);
	}

	RPAddPad(Box, 18.f);
	RPAddToVBox(Box, RPMakeWrappedText(Box,
		TEXT("设置草稿：分类切换会保留草稿，但不写入配置或改变当前剧情；声音滑块仅实时预听。保存并返回统一提交，取消返回会恢复进入设置前的全部设置。"),
		14, FAscendUIStyle::DimGray(), ETextJustify::Left), FMargin(140.f, 4.f, 140.f, 8.f));
	UHorizontalBox* Buttons = NewObject<UHorizontalBox>(Box);
	Buttons->AddChildToHorizontalBox(NewObject<USpacer>(Buttons))->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	RPAddToHBox(Buttons, MakeLinkedButton(Buttons, TEXT("【保存并返回】"), TEXT("settings_save"), 0, 20));
	RPAddToHBox(Buttons, MakeLinkedButton(Buttons, TEXT("【取消并返回】"), TEXT("settings_back"), 0, 20));
	Buttons->AddChildToHorizontalBox(NewObject<USpacer>(Buttons))->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	RPAddToVBox(Box, Buttons);

	UBorder* Scrim = NewObject<UBorder>(SettingsOverlayLayer);
	Scrim->SetBrushColor(FLinearColor(0.015f, 0.018f, 0.024f, 0.84f));
	UOverlaySlot* ScrimSlot = SettingsOverlayLayer->AddChildToOverlay(Scrim);
	ScrimSlot->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Fill);
	ScrimSlot->SetVerticalAlignment(EVerticalAlignment::VAlign_Fill);
	UOverlaySlot* SettingsSlot = SettingsOverlayLayer->AddChildToOverlay(Scroll);
	SettingsSlot->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Fill);
	SettingsSlot->SetVerticalAlignment(EVerticalAlignment::VAlign_Fill);
	SettingsProxies = MoveTemp(PendingScreenProxies);
	PendingScreenProxies.Reset();
}

void AAscendPlayerController::ReturnFromSettings()
{
	// A settings back action is a cancellation, not an implicit save. This also
	// restores audio preview sliders before the original screen is rebuilt.
	CancelSettingsDraft();
	const EGameScreen Destination = SettingsReturnScreen;
	const bool bReopenPause = bSettingsReturnToPause;
	const float SavedScrollOffset = SettingsReturnScrollOffset;
	const bool bShowNarrativeRefresh = bInfiniteNarrativeNeedsRefresh;
	const bool bShowPrefetchRetry = bCombatNarrativePrefetchNeedsRetry;
	const FString PrefetchRetryDiagnostic = CombatNarrativePrefetchDiagnostic;
	bSettingsReturnToPause = false;
	SettingsReturnScrollOffset = 0.f;
	ResetSettingsWidgetRefs();
	if (SettingsOverlayLayer)
	{
		SettingsOverlayLayer->ClearChildren();
		SettingsOverlayLayer->RemoveFromParent();
		SettingsOverlayLayer = nullptr;
	}
	SettingsProxies.Reset();
	// The screen underneath was never replaced. This preserves exact combat/RP/
	// reward/map state (including scroll offset and pending operations) instead of
	// guessing a finite enum-to-builder mapping. Any callback that completed while
	// settings was open has already updated that mounted screen through its normal
	// path.
	PendingScreenProxies.Reset();
	if (bShowPrefetchRetry)
	{
		// The stale prefetch was cancelled without submitting any branch. Surface a
		// retry page only after settings has closed; the retry button is the sole
		// path that can start a replacement request.
		bCombatNarrativePrefetchNeedsRetry = false;
		ShowInfiniteNarrativeError(PrefetchRetryDiagnostic.IsEmpty()
			? TEXT("后台战后剧情已作废，请重试") : PrefetchRetryDiagnostic);
	}
	else if (bShowNarrativeRefresh)
	{
		// ShowInfiniteNarrative sees the marker and disables the old choices while
		// adding an explicit retry button. It does not settle or resubmit anything.
		ShowInfiniteNarrative();
	}
	if (bReopenPause && !bShowNarrativeRefresh && !bShowPrefetchRetry)
	{
		if (Destination == EGameScreen::InfiniteNarrative && RPScrollBox && GetWorld())
		{
			TWeakObjectPtr<UScrollBox> WeakScroll(RPScrollBox);
			GetWorld()->GetTimerManager().SetTimerForNextTick(
				FTimerDelegate::CreateWeakLambda(this, [WeakScroll, SavedScrollOffset]()
				{
					if (WeakScroll.IsValid()) WeakScroll->SetScrollOffset(SavedScrollOffset);
				}));
		}
		ShowPauseMenu();
	}
}

void AAscendPlayerController::StartInfiniteNarrativeRun()
{
	InvalidateInfiniteNarrativeFlow();
	if (InfiniteNarrativeService) InfiniteNarrativeService->CancelGeneration();
	const int32 RunSeed = FMath::RandRange(1, 999999);
	// Build the candidate run off to the side first.  A malformed/custom relic table
	// must never destroy the currently loaded in-memory run before we know that the
	// authored opening can offer its required three distinct, unowned relics.
	URunManager* CandidateRun = NewObject<URunManager>(this);
	if (!CandidateRun || !CandidateRun->StartNewRun(RunSeed, true))
	{
		ShowTitle();
		return;
	}
	TArray<FString> CandidateRelicIds;
	const TArray<FRelicData> CandidateRelics = ResolveAuthoredOpeningRelics(CandidateRun, CandidateRelicIds);
	if (CandidateRelics.Num() < 3 || CandidateRelicIds.Num() != 3
		|| CandidateRelicIds[0].IsEmpty() || CandidateRelicIds[1].IsEmpty() || CandidateRelicIds[2].IsEmpty()
		|| CandidateRelicIds[0] == CandidateRelicIds[1] || CandidateRelicIds[0] == CandidateRelicIds[2]
		|| CandidateRelicIds[1] == CandidateRelicIds[2])
	{
		UE_LOG(LogTemp, Error, TEXT("[InfiniteRP] candidate run has fewer than three distinct valid opening relics; previous run preserved"));
		ShowTitle();
		return;
	}
	Run = CandidateRun;
	bInfiniteChoiceResolved = false;
	bInfiniteNarrativeNeedsRefresh = false;
	PendingInfiniteFreeformAction.Reset();
	bPendingInfiniteFreeformActionRecorded = false;
	ActiveInfiniteNarrativeRequestKind = EInfiniteNarrativeRequestKind::Normal;
	bInfiniteFreeRPForcedTurnActive = false;
	FRandomStream OpeningStream(RunSeed ^ 0x4A17B3);
	StartRelicChoices = MoveTemp(CandidateRelicIds);
	const TArray<FRelicData> OpeningRelics = CandidateRelics;
	const int32 OpeningIndex = OpeningStream.RandRange(0, 4);
	CurrentInfiniteBeat = BuildAuthoredOpeningBeat(OpeningIndex, OpeningRelics);
	PendingInfiniteOpeningId = CurrentInfiniteBeat.Diagnostic;
	PendingInfiniteOpeningId.RemoveFromStart(TEXT("authored_opening:"));
	PendingInfiniteOpeningSeed.Reset();
	Run->SavePendingInfiniteOpening(OpeningIndex, PendingInfiniteOpeningId, StartRelicChoices,
		TArray<FString>(), false);
	bOpeningChoiceWordingPending = true;
	bInfiniteNarrativeRequestInFlight = true;
	bInputLocked = true;
	ShowInfiniteNarrative();
	RequestAuthoredOpeningChoiceWording();
}

void AAscendPlayerController::InvalidateInfiniteNarrativeFlow()
{
	++InfiniteNarrativeFlowSerial;
	++OpeningWordingRequestSerial;
	bOpeningChoiceWordingPending = false;
	bInfiniteNarrativeRequestInFlight = false;
	bInputLocked = false;
	PendingInfiniteOpeningId.Reset();
	bInfiniteNarrativeNeedsRefresh = false;
	PendingInfiniteFreeformAction.Reset();
	bPendingInfiniteFreeformActionRecorded = false;
	ActiveInfiniteNarrativeRequestKind = EInfiniteNarrativeRequestKind::Normal;
	bInfiniteFreeRPForcedTurnActive = false;
}

bool AAscendPlayerController::RestoreAuthoredOpeningFromRunState()
{
	if (!Run) return false;
	int32 OpeningIndex = INDEX_NONE;
	FString OpeningId;
	TArray<FString> RelicIds;
	TArray<FString> ChoiceTexts;
	bool bWordingReady = false;
	if (!Run->RestorePendingInfiniteOpening(OpeningIndex, OpeningId, RelicIds, ChoiceTexts, bWordingReady))
		return false;
	TArray<FRelicData> OpeningRelics;
	for (const FString& RelicId : RelicIds)
	{
		const FRelicData* Relic = Run->GetRelicData(RelicId);
		if (!Relic) return false;
		OpeningRelics.Add(*Relic);
	}
	if (OpeningRelics.Num() != 3) return false;
	CurrentInfiniteBeat = BuildAuthoredOpeningBeat(OpeningIndex, OpeningRelics);
	if (CurrentInfiniteBeat.Diagnostic != TEXT("authored_opening:") + OpeningId) return false;
	StartRelicChoices = RelicIds;
	PendingInfiniteOpeningId = OpeningId;
	PendingInfiniteOpeningSeed.Reset();
	bInfiniteChoiceResolved = false;
	bInfiniteNarrativeNeedsRefresh = false;
	PendingInfiniteFreeformAction.Reset();
	bPendingInfiniteFreeformActionRecorded = false;
	ActiveInfiniteNarrativeRequestKind = EInfiniteNarrativeRequestKind::Normal;
	bInfiniteFreeRPForcedTurnActive = false;
	bOpeningChoiceWordingPending = false;
	bInfiniteNarrativeRequestInFlight = false;
	bInputLocked = false;
	if (bWordingReady && ChoiceTexts.Num() == 3)
	{
		bool bStoredTextsValid = true;
		TArray<FString> StoredLeads;
		for (int32 Index = 0; Index < 3; ++Index)
		{
			const FRelicData* Relic = OpeningRelics.IsValidIndex(Index) ? &OpeningRelics[Index] : nullptr;
			if (!Relic)
			{
				bStoredTextsValid = false;
				break;
			}
			// Old saves may contain the former name/description suffix or an unsafe
			// model lead. Keep the locked IDs and replace only that display copy locally.
			StoredLeads.Add(FilterAuthoredOpeningChoiceText(OpeningId, Index, OpeningRelics, ChoiceTexts[Index]));
		}
		if (bStoredTextsValid)
		{
			TSet<FString> UniqueStoredLeads;
			for (const FString& Lead : StoredLeads) UniqueStoredLeads.Add(Lead);
			if (UniqueStoredLeads.Num() != 3)
			{
				for (int32 Index = 0; Index < 3; ++Index)
					StoredLeads[Index] = AuthoredOpeningChoiceFallback(*OpeningId, Index);
			}
			for (int32 Index = 0; Index < 3; ++Index)
				CurrentInfiniteBeat.Choices[Index].Text = StoredLeads[Index];
		}
	}
	return true;
}

bool AAscendPlayerController::RestoreFreeRPForcedJumpResultFromRunState()
{
	if (!Run || !Run->State.bInfiniteFreeRPForcedJumpPending
		|| !Run->State.bInfiniteFreeRPForcedJumpAwaitingContinue)
		return false;

	// The response beat is already durable as the bridge turn. Rehydrate only its
	// readable scene; the generated choices are intentionally not persisted or
	// exposed as selectable choices during this pause.
	for (int32 Index = Run->State.RPRecentTurns.Num() - 1; Index >= 0; --Index)
	{
		const FRPNarrativeTurn& Turn = Run->State.RPRecentTurns[Index];
		if (!Turn.Next.Equals(TEXT("free_rp_forced_jump"), ESearchCase::IgnoreCase)) continue;
		CurrentInfiniteBeat = FInfiniteNarrativeBeat();
		CurrentInfiniteBeat.Title = Turn.Title;
		CurrentInfiniteBeat.Speaker = Turn.Speaker;
		CurrentInfiniteBeat.Narration = Turn.Narration;
		CurrentInfiniteBeat.Dialogue = Turn.Dialogue;
		CurrentInfiniteBeat.MemoryJson = Turn.MemoryJson;
		CurrentInfiniteBeat.Diagnostic = TEXT("free_rp_forced_wait");
		bInfiniteChoiceResolved = false;
		return true;
	}

	// A very old/over-compressed save may have lost the raw bridge text, but it
	// must still stop at a visible continue gate rather than auto-running a turn.
	CurrentInfiniteBeat = FInfiniteNarrativeBeat();
	CurrentInfiniteBeat.Narration = TEXT("上一轮自由 RP 的回应已经准备好。读完后点击继续，沿已锁定方向自动衔接下一幕。");
	CurrentInfiniteBeat.Diagnostic = TEXT("free_rp_forced_wait");
	bInfiniteChoiceResolved = false;
	return true;
}

void AAscendPlayerController::RequestAuthoredOpeningChoiceWording()
{
	if (!InfiniteNarrativeService || !Run || !Run->State.bInfiniteNarrativeMode
		|| StartRelicChoices.Num() != 3 || CurrentInfiniteBeat.Choices.Num() != 3)
	{
		HandleAuthoredOpeningChoiceWordingReady(false, TArray<FString>(),
			TEXT("开场文案请求上下文不完整；已使用本地衔接文案"));
		return;
	}
	FInfiniteNarrativeRequestContext Context;
	Context.bOpeningChoiceWording = true;
	TArray<FRelicData> OpeningRelics;
	for (const FString& RelicId : StartRelicChoices)
	{
		const FRelicData* Relic = Run->GetRelicData(RelicId);
		if (Relic)
		{
			OpeningRelics.Add(*Relic);
			Context.OpeningChoiceRelicNames.Add(Relic->Name);
			Context.OpeningChoiceRelicDescriptions.Add(Relic->Description);
			Context.OpeningChoiceRelicRarities.Add(Relic->Rarity);
		}
	}
	if (OpeningRelics.Num() != 3)
	{
		HandleAuthoredOpeningChoiceWordingReady(false, TArray<FString>(),
			TEXT("开场法器数据不完整；已使用本地衔接文案"));
		return;
	}
	Context.OpeningChoicePrompt = BuildAuthoredOpeningChoiceWordingPrompt(CurrentInfiniteBeat, OpeningRelics);
	Context.OpeningChoiceRelicIds = StartRelicChoices;
	const uint64 RequestToken = ++OpeningWordingRequestSerial;
	InfiniteNarrativeService->GenerateOpeningChoiceWording(InfiniteNarrativeSettings, Context,
		FOnInfiniteOpeningWordingReady::CreateWeakLambda(this,
			[this, RequestToken](bool bSuccess, const TArray<FString>& Texts, const FString& Diagnostic)
			{
				if (RequestToken != OpeningWordingRequestSerial) return;
				HandleAuthoredOpeningChoiceWordingReady(bSuccess, Texts, Diagnostic);
			}));
}

void AAscendPlayerController::HandleAuthoredOpeningChoiceWordingReady(bool bSuccess,
	const TArray<FString>& Texts, const FString& Diagnostic)
{
	if (!bOpeningChoiceWordingPending) return;
	bOpeningChoiceWordingPending = false;
	bInfiniteNarrativeRequestInFlight = false;
	bInputLocked = false;
	TArray<FString> AcceptedTexts;
	if (bSuccess && Texts.Num() == 3 && CurrentInfiniteBeat.Choices.Num() == 3)
	{
		for (int32 Index = 0; Index < 3; ++Index)
		{
			if (!StartRelicChoices.IsValidIndex(Index) || !Texts.IsValidIndex(Index))
			{
				bSuccess = false;
				break;
			}
			const FRelicData* Relic = Run ? Run->GetRelicData(StartRelicChoices[Index]) : nullptr;
			if (!Relic) { bSuccess = false; break; }
			FString Copy = Texts[Index].TrimStartAndEnd();
			if (Copy.IsEmpty() || Copy.Len() > 800
				|| !UInfiniteNarrativeService::IsOpeningChoiceWordingSafe(Copy, Relic->Id,
					Relic->Name, Relic->Description, Relic->Rarity))
			{
				bSuccess = false;
				break;
			}
			bool bSafeForLockedRelics = true;
			for (const FString& LockedRelicId : StartRelicChoices)
			{
				const FRelicData* LockedRelic = Run ? Run->GetRelicData(LockedRelicId) : nullptr;
				if (!LockedRelic || !UInfiniteNarrativeService::IsOpeningChoiceWordingSafe(Copy,
					LockedRelic->Id, LockedRelic->Name, LockedRelic->Description, LockedRelic->Rarity))
				{
					bSafeForLockedRelics = false;
					break;
				}
			}
			if (!bSafeForLockedRelics)
			{
				bSuccess = false;
				break;
			}
			AcceptedTexts.Add(MoveTemp(Copy));
		}
		if (bSuccess)
		{
			TSet<FString> UniqueAcceptedTexts;
			for (const FString& AcceptedText : AcceptedTexts) UniqueAcceptedTexts.Add(AcceptedText);
			bSuccess = UniqueAcceptedTexts.Num() == 3;
		}
		if (bSuccess)
		{
			for (int32 Index = 0; Index < 3; ++Index)
			{
				const FRelicData* Relic = Run ? Run->GetRelicData(StartRelicChoices[Index]) : nullptr;
				if (!Relic || !AcceptedTexts.IsValidIndex(Index)) { bSuccess = false; break; }
				// Only the model-authored lead is accepted. Identity/description lines remain
				// engine-owned and are revealed only after the player commits a choice.
				CurrentInfiniteBeat.Choices[Index].Text = AcceptedTexts[Index];
			}
		}
	}
	if (!bSuccess)
	{
		UE_LOG(LogTemp, Display, TEXT("[InfiniteRP] opening wording fallback applied diagnostic=%s"),
			Diagnostic.IsEmpty() ? TEXT("provider unavailable") : *Diagnostic);
	}
	TArray<FString> PersistedTexts;
	for (const FInfiniteNarrativeChoice& Choice : CurrentInfiniteBeat.Choices)
		PersistedTexts.Add(ExtractAuthoredOpeningChoiceLead(Choice.Text));
	if (Run && PersistedTexts.Num() == 3)
	{
		Run->SavePendingInfiniteOpening(Run->State.PendingInfiniteOpeningIndex,
			PendingInfiniteOpeningId, StartRelicChoices, PersistedTexts, true);
	}
	RefreshOpeningChoiceWordingUI();
}

FInfiniteNarrativeBeat AAscendPlayerController::BuildAuthoredOpeningForAutomationTest(
	int32 OpeningIndex, const TArray<FRelicData>& OpeningRelics)
{
	return BuildAuthoredOpeningBeat(FMath::Clamp(OpeningIndex, 0, 4), OpeningRelics);
}

FString AAscendPlayerController::FilterAuthoredOpeningChoiceForAutomationTest(
	const FString& OpeningId, int32 ChoiceIndex, const FRelicData& Relic, const FString& StoredText)
{
	const TArray<FRelicData> LockedRelics = {Relic};
	return FilterAuthoredOpeningChoiceText(OpeningId, ChoiceIndex, LockedRelics, StoredText);
}

FString AAscendPlayerController::FilterAuthoredOpeningChoiceForAutomationTest(
	const FString& OpeningId, int32 ChoiceIndex, const TArray<FRelicData>& LockedRelics,
	const FString& StoredText)
{
	return FilterAuthoredOpeningChoiceText(OpeningId, ChoiceIndex, LockedRelics, StoredText);
}

bool AAscendPlayerController::MergeRPStreamTextForAutomationTest(const FString& ExistingText,
	const FString& IncomingText, FString& OutText, bool& bOutRebuilt)
{
	return MergeRPStreamText(ExistingText, IncomingText, OutText, bOutRebuilt);
}

FString AAscendPlayerController::NormalizeRPDialogueForAutomationTest(const FString& Text)
{
	return NormalizeRPDialogueText(Text);
}

FString AAscendPlayerController::MakeRPDialogueIdentityKeyForAutomationTest(
	const FString& Speaker, const FString& PortraitId, const FString& Expression)
{
	return MakeRPDialogueIdentityKey(Speaker, PortraitId, Expression);
}

bool AAscendPlayerController::ShouldAutoScrollRPStreamForAutomationTest(float ScrollOffset,
	float EndOffset)
{
	return ShouldRPStreamingAutoScroll(ScrollOffset, EndOffset);
}

bool AAscendPlayerController::ValidateRPNarrativeDisplayForAutomationTest(FString& OutDiagnostic)
{
	OutDiagnostic.Reset();
	const FString Source = TEXT("第一段在行尾落下，") TEXT("\r\n\r\n")
		TEXT("第二段从下一 chunk 接上：跨块标点。\r\n\r\n")
		TEXT("第三段包含中英 mixed text 与尾部句号。");
	const FString Formatted = FormatRPNaturalParagraphs(Source);
	if (!Formatted.StartsWith(TEXT("　　第一段")) || !Formatted.Contains(TEXT("\n\n　　第二段"))
		|| !Formatted.Contains(TEXT("\n\n　　第三段")))
	{
		OutDiagnostic = TEXT("natural paragraph formatting lost a paragraph break or two-CJK indent");
		return false;
	}

	FString Unicode;
	Unicode.AppendChar(TEXT('甲'));
	Unicode.AppendChar(static_cast<TCHAR>(0xD83D));
	Unicode.AppendChar(static_cast<TCHAR>(0xDE00));
	Unicode.AppendChar(TEXT('e'));
	Unicode.AppendChar(static_cast<TCHAR>(0x0301));
	TArray<FString> Clusters;
	int32 Cursor = 0;
	while (Cursor < Unicode.Len()) Clusters.Add(NextRPTextCluster(Unicode, Cursor));
	const int32 ExpectedClusters = sizeof(TCHAR) == 2 ? 3 : 4;
	const int32 ExpectedAstralClusterLength = sizeof(TCHAR) == 2 ? 2 : 1;
	if (Clusters.Num() != ExpectedClusters || Clusters[1].Len() != ExpectedAstralClusterLength
		|| Clusters[ExpectedClusters - 1].Len() != 2)
	{
		OutDiagnostic = TEXT("Unicode cluster guard split a surrogate pair or combining mark");
		return false;
	}

	// This guard intentionally exercises only deterministic local strings.  It
	// never opens the narrative service, reads a save, or exposes request-local
	// reasoning/private prefill content to the UI or test output.
	OutDiagnostic = FString::Printf(TEXT("paragraphs=3; clusters=%d; CRLF=normalized; indent=2-CJK"), Clusters.Num());
	return true;
}

bool AAscendPlayerController::ShouldStartCombatNarrativePrefetch(bool bInfiniteNarrativeMode,
	bool bRunActive, bool bRequestInFlight, bool bPrefetchReady)
{
	return bInfiniteNarrativeMode && bRunActive && !bRequestInFlight && !bPrefetchReady;
}

bool AAscendPlayerController::ShouldPrefetchAtCombatStart(bool bFirstInfiniteCombat,
	bool bGenerateAfterCombatWithLog)
{
	// The first authored-opening combat is always speculative mode B. Later
	// combats honor the player's A/B setting: A waits for the real combat log,
	// while B starts the same victory-assuming prefetch immediately.
	return bFirstInfiniteCombat || !bGenerateAfterCombatWithLog;
}

bool AAscendPlayerController::IsFreeRPInputAvailableForAutomationTest(bool bFreeRPModeEnabled,
	bool bForcedJumpPending, bool bRequestInFlight, bool bOpeningPending)
{
	return bFreeRPModeEnabled && !bForcedJumpPending && !bRequestInFlight && !bOpeningPending;
}

bool AAscendPlayerController::IsFreeRPForcedContinueAvailableForAutomationTest(
	bool bForcedJumpPending, bool bAwaitingContinue, bool bRequestInFlight)
{
	return bForcedJumpPending && bAwaitingContinue && !bRequestInFlight;
}

int32 AAscendPlayerController::ChooseFreeRPForcedChoiceIndexForAutomationTest(int32 ChoiceCount,
	int32 RunSeed, int32 NarrativeTurnSerial)
{
	if (ChoiceCount <= 0) return INDEX_NONE;
	// The selection is random from the player's perspective but replayable for a run,
	// so a retry or a read-back never silently changes the already locked direction.
	const int32 Salt = NarrativeTurnSerial * 7919 + 0x5EED17;
	FRandomStream DirectionStream(RunSeed ^ Salt);
	return DirectionStream.RandRange(0, ChoiceCount - 1);
}

void AAscendPlayerController::BuildInfiniteOpening()
{
	// Compatibility hook for old callers. Do not reroll a live opening: the three
	// relic IDs belong to the run and are sampled exactly once by StartInfiniteNarrativeRun.
	if (Run && Run->HasPendingInfiniteOpening() && RestoreAuthoredOpeningFromRunState()) return;
	if (CurrentInfiniteBeat.Diagnostic.StartsWith(TEXT("authored_opening:"))
		&& StartRelicChoices.Num() == 3 && CurrentInfiniteBeat.Choices.Num() == 3) return;
	TArray<FRelicData> OpeningRelics = ResolveAuthoredOpeningRelics(Run, StartRelicChoices);
	FRandomStream OpeningStream(Run ? Run->GetRunSeed() ^ 0x4A17B3 : FMath::Rand());
	CurrentInfiniteBeat = BuildAuthoredOpeningBeat(OpeningStream.RandRange(0, 4), OpeningRelics);
	PendingInfiniteOpeningId = CurrentInfiniteBeat.Diagnostic;
	PendingInfiniteOpeningId.RemoveFromStart(TEXT("authored_opening:"));
	PendingInfiniteOpeningSeed.Reset();
}

FInfiniteNarrativeRequestContext AAscendPlayerController::BuildInfiniteNarrativeContext(
	const FString& FreeformAction, bool bCombatPrefetch, bool bAssumeVictoryWithoutLog,
	EInfiniteNarrativeRequestKind RequestKind, const FString& ForcedDirection) const
{
	FInfiniteNarrativeRequestContext Context;
	if (!Run) return Context;
	Context.Cycle = Run->State.InfiniteCycle;
	Context.HP = Run->State.HP;
	Context.MaxHP = Run->State.MaxHP;
	Context.Gold = Run->State.Gold;
	Context.DeckSize = Run->State.Deck.Num();
	TMap<FString, int32> OwnedCardCounts;
	for (const FDeckCard& Owned : Run->State.Deck)
	{
		if (!Owned.bUpgraded) ++Context.UpgradeableCardCount;
		OwnedCardCounts.FindOrAdd(Owned.CardId + (Owned.bUpgraded ? TEXT("|1") : TEXT("|0")))++;
		if (const FCardData* Card = Run->GetCardData(Owned.CardId))
		{
			Context.AbilityNames.AddUnique(Card->Name);
			Context.CardNameToId.Add(Card->Name, Card->Id);
		}
	}
	TArray<FString> OwnedCardKeys;
	OwnedCardCounts.GetKeys(OwnedCardKeys);
	OwnedCardKeys.Sort();
	TArray<FString> ForgeBuildLines;
	for (const FString& OwnedKey : OwnedCardKeys)
	{
		if (OwnedKey.Len() < 3) continue;
		const bool bUpgraded = OwnedKey.EndsWith(TEXT("|1"));
		const FString CardId = OwnedKey.LeftChop(2);
		const FCardData* Card = Run->GetCardData(CardId);
		if (!Card) continue;
		FString Description = bUpgraded && !Card->UpgradedDescription.IsEmpty()
			? Card->UpgradedDescription : Card->Description;
		Description.ReplaceInline(TEXT("\n"), TEXT(" "));
		const int32 Cost = bUpgraded && Card->UpgradedCost >= 0 ? Card->UpgradedCost : Card->Cost;
		ForgeBuildLines.Add(FString::Printf(TEXT("卡牌×%d：%s%s；%d费；%s；%s"),
			OwnedCardCounts.FindRef(OwnedKey), *Card->Name, bUpgraded ? TEXT("+") : TEXT(""),
			Cost, *Card->Type, *Description.Left(180)));
	}
	for (const FString& RelicId : Run->State.RelicIds)
	{
		if (const FRelicData* Relic = Run->GetRelicData(RelicId))
		{
			Context.RelicNames.AddUnique(Relic->Name);
			Context.RelicNameToId.Add(Relic->Name, Relic->Id);
			FString Description = Relic->Description;
			Description.ReplaceInline(TEXT("\n"), TEXT(" "));
			ForgeBuildLines.Add(FString::Printf(TEXT("法宝：%s；%s"), *Relic->Name, *Description.Left(180)));
			if (Relic->Condition == TEXT("narrative_reward_luck"))
				Context.RouteRewardBias += FMath::Max(0.f, Relic->Modifier);
		}
	}
	Context.CardForgeBuildSummary = FString::Join(ForgeBuildLines, TEXT("\n"));
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
	// The scene currently visible on the RP page has not yet been committed by a
	// choice. Attach it only to the freeform request that originates from that page;
	// ordinary option requests already commit the scene through Select... and the
	// forced jump gets the freeform bridge from RPRecentTurns.
	if (RequestKind == EInfiniteNarrativeRequestKind::Freeform
		&& (!CurrentInfiniteBeat.Narration.IsEmpty()
			|| CurrentInfiniteBeat.DialogueLines.Num() > 0
			|| !CurrentInfiniteBeat.Dialogue.IsEmpty()))
	{
		Context.ChatHistory.Add({TEXT("assistant"), BuildCurrentRPSceneContext(CurrentInfiniteBeat)});
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
	Context.bFreeRPAction = RequestKind == EInfiniteNarrativeRequestKind::Freeform;
	Context.bFreeRPForcedJump = RequestKind == EInfiniteNarrativeRequestKind::ForcedFreeRPJump;
	Context.ForcedRPDirection = ForcedDirection.TrimStartAndEnd();
	Context.FreeformAction = Context.bFreeRPAction ? FreeformAction : TEXT("");
	Context.OpeningSeed = PendingInfiniteOpeningSeed;
	return Context;
}

void AAscendPlayerController::RequestNextInfiniteNarrative(const FString& FreeformAction,
	EInfiniteNarrativeRequestKind RequestKind, const FString& ForcedDirection)
{
	if (!Run || !Run->State.bInfiniteNarrativeMode || !Run->State.bRunActive || bInfiniteNarrativeRequestInFlight) return;
	if (RequestKind == EInfiniteNarrativeRequestKind::ForcedFreeRPJump)
	{
		// The persisted value wins on retry/read-back. A caller-supplied direction is
		// accepted only when the save has not captured one yet.
		if (Run->State.InfiniteFreeRPForcedDirection.IsEmpty()
			&& !ForcedDirection.TrimStartAndEnd().IsEmpty())
			Run->State.InfiniteFreeRPForcedDirection = ForcedDirection.TrimStartAndEnd();
		if (Run->State.InfiniteFreeRPForcedDirection.IsEmpty())
			Run->State.InfiniteFreeRPForcedDirection = TEXT("沿上一轮自由行动的直接后果自然推进，不替玩家补写未发生的选择。");
		Run->State.bInfiniteFreeRPForcedJumpPending = true;
		Run->State.bInfiniteFreeRPForcedJumpAwaitingContinue = false;
		bInfiniteFreeRPForcedTurnActive = true;
		Run->SaveRun();
	}
	else if (RequestKind == EInfiniteNarrativeRequestKind::Freeform)
	{
		PendingInfiniteFreeformAction = FreeformAction.TrimStartAndEnd();
		bPendingInfiniteFreeformActionRecorded = !PendingInfiniteFreeformAction.IsEmpty();
	}
	ActiveInfiniteNarrativeRequestKind = RequestKind;
	bInfiniteNarrativeNeedsRefresh = false;

	const FString EffectiveForcedDirection = RequestKind == EInfiniteNarrativeRequestKind::ForcedFreeRPJump
		? Run->State.InfiniteFreeRPForcedDirection : FString();
	const FString EffectiveFreeformAction = RequestKind == EInfiniteNarrativeRequestKind::Freeform
		? PendingInfiniteFreeformAction : FString();
	const FInfiniteNarrativeRequestContext Context = BuildInfiniteNarrativeContext(
		EffectiveFreeformAction, false, false, RequestKind, EffectiveForcedDirection);

	bInfiniteNarrativeRequestInFlight = true;
	bInputLocked = true;
	const uint64 RequestToken = InfiniteNarrativeFlowSerial;
	ResetInfiniteNarrativeStreamPreview();
	const FString LoadingMessage = RequestKind == EInfiniteNarrativeRequestKind::ForcedFreeRPJump
		? TEXT("正在沿已锁定的方向自动衔接下一幕……")
		: RequestKind == EInfiniteNarrativeRequestKind::Freeform
		? TEXT("世界正在回应你的自由 RP 行动……") : TEXT("正在推演下一幕……");
	const bool bHasCurrentRPContent = !CurrentInfiniteBeat.Narration.IsEmpty()
		|| CurrentInfiniteBeat.DialogueLines.Num() > 0 || !CurrentInfiniteBeat.Dialogue.IsEmpty();
	if (CurrentScreen == EGameScreen::InfiniteNarrative && bHasCurrentRPContent && RPGenerationStatusText)
	{
		RPGenerationStatusText->SetText(FText::FromString(LoadingMessage + TEXT("  你可以继续阅读本轮内容。")));
		RPGenerationStatusText->SetVisibility(ESlateVisibility::HitTestInvisible);
	}
	else
	{
		ShowInfiniteNarrativeLoading(LoadingMessage);
	}
	// A freeform request and its mandatory follow-up are both single-flight. Disable
	// the already-mounted controls immediately, including the text box that would
	// otherwise remain editable while the old page is kept on screen.
	for (UButton* ChoiceButton : RPInfiniteChoiceButtons)
		if (ChoiceButton) ChoiceButton->SetIsEnabled(false);
	if (RPFreeformInput) RPFreeformInput->SetIsEnabled(false);
	InfiniteNarrativeService->Generate(InfiniteNarrativeSettings, Context,
		FOnInfiniteNarrativeReady::CreateWeakLambda(this,
			[this, RequestToken](bool bFromLLM, const FInfiniteNarrativeBeat& Beat)
			{
				if (RequestToken != InfiniteNarrativeFlowSerial) return;
				HandleInfiniteNarrativeReady(bFromLLM, Beat);
			}),
		FOnInfiniteNarrativeStreamUpdate::CreateWeakLambda(this,
			[this, RequestToken](const FInfiniteNarrativeStreamUpdate& Update)
			{
				if (RequestToken != InfiniteNarrativeFlowSerial) return;
				HandleInfiniteNarrativeStreamUpdate(Update, false);
			}));
}

void AAscendPlayerController::ResetInfiniteNarrativeStreamPreview()
{
	if (GetWorld()) GetWorld()->GetTimerManager().ClearTimer(RPStreamingRevealTimer);
	RPStreamingRevealTimer.Invalidate();
	RPStreamingContainer = nullptr;
	RPStreamingDialogueBox = nullptr;
	RPStreamingNarrationText = nullptr;
	RPStreamingNarrationGlyphBox = nullptr;
	RPStreamingDialogueGlyphBoxes.Reset();
	RPStreamingDialogueRows.Reset();
	RPStreamingDialogueBubbleSizes.Reset();
	RPStreamingDialogueTextWidths.Reset();
	RPStreamingPortraitWidgets.Reset();
	RPStreamingRevealGlyphs.Reset();
	RPStreamingRevealStartTimes.Reset();
	RPStreamingDialogueKeys.Reset();
	RPStreamingDialogueSources.Reset();
	RPStreamingNarrationLines.Reset();
	RPStreamingNarrationLineWidths.Reset();
	RPStreamingDialogueLines.Reset();
	RPStreamingDialogueLineWidths.Reset();
	RPStreamingNarrationSource.Reset();
	RPStreamingDialogueNeedsIndent.Reset();
	RPStreamingDialoguePreviousNewline.Reset();
	bRPStreamingNarrationNeedsIndent = true;
	bRPStreamingNarrationPreviousNewline = false;
	bRPStreamingSceneFinalized = false;
	RPStreamingTextWidth = 720.f;
	RPStreamingPortraitSize = 208.f;
	RPStreamingFrameHeight = 640.f;
	RPStreamingRevealCompletedAt = 0.0;
	RPVisualExpectedTail.Reset();
}

void AAscendPlayerController::FinalizeInfiniteNarrativeStream()
{
	if (bRPStreamingSceneFinalized || !RPStreamingContainer || !RPContentBox || !RPScrollBox)
		return;
	// Only follow the terminal footer when the player was already reading at the
	// bottom.  A player who inspected an earlier paragraph must not be yanked away
	// merely because the async completion added choices below the scene.
	const bool bShouldAutoScroll = ShouldRPStreamingAutoScroll(
		RPScrollBox->GetScrollOffset(), RPScrollBox->GetScrollOffsetOfEnd());

	// Stop only the reveal timer.  The existing line boxes, glyph widgets, portraits,
	// and their fixed geometry remain in the same ScrollBox; the final beat adds the
	// actionable footer below them instead of rebuilding the scene with TextBlock wrap.
	if (GetWorld()) GetWorld()->GetTimerManager().ClearTimer(RPStreamingRevealTimer);
	for (UTextBlock* Glyph : RPStreamingRevealGlyphs)
		if (Glyph) Glyph->SetRenderOpacity(1.f);
	if (RPStreamingRevealGlyphs.Num() > 0)
		RPStreamingRevealCompletedAt = FPlatformTime::Seconds();
	if (RPGenerationStatusText)
		RPGenerationStatusText->SetVisibility(ESlateVisibility::Collapsed);

	RPInfiniteChoiceButtons.Reset();
	RPOpeningChoiceLabels.Reset();
	RPOpeningChoicePendingText = nullptr;
	const float SafeWidth = RPStreamingTextWidth;
	const float PortraitSize = RPStreamingPortraitSize;
	UVerticalBox* Box = RPContentBox;
	RPAddPad(Box, 18.f);
	UButton* FirstChoiceButton = nullptr;
	const bool bFreeRPForcedJumpPending = bInfiniteFreeRPForcedTurnActive
		|| (Run && Run->State.bInfiniteFreeRPForcedJumpPending);
	const bool bFreeRPForcedJumpAwaitingContinue = Run
		&& Run->State.bInfiniteFreeRPForcedJumpAwaitingContinue;

	if (bInfiniteChoiceResolved)
	{
		UBorder* Resolution = NewObject<UBorder>(Box);
		Resolution->SetBrushColor(FLinearColor(0.055f, 0.075f, 0.08f, 0.94f));
		Resolution->SetPadding(FMargin(42.f, 26.f));
		UVerticalBox* ResultBox = NewObject<UVerticalBox>(Resolution);
		RPAddToVBox(ResultBox, RPMakeWrappedText(ResultBox,
			FString::Printf(TEXT("你选择了：%s"), *PendingInfiniteChoice.Text), 17,
			FAscendUIStyle::GoldYellow(), ETextJustify::Left));
		if (!PendingInfiniteChoice.ResultSummary.IsEmpty())
			RPAddToVBox(ResultBox, RPMakeWrappedText(ResultBox, PendingInfiniteChoice.ResultSummary, 19,
				FAscendUIStyle::PaperWhite(), ETextJustify::Left), FMargin(4.f, 10.f));
		Resolution->SetContent(ResultBox);
		RPAddToVBox(Box, Resolution, FMargin(180.f, 8.f));
	}
	else
	{
		if (bOpeningChoiceWordingPending)
		{
			RPOpeningChoicePendingText = RPMakeWrappedText(Box,
				TEXT("正在衔接法器选择……"), 16, FAscendUIStyle::DimGray(), ETextJustify::Center);
			RPAddToVBox(Box, RPOpeningChoicePendingText, FMargin(16.f, 2.f, 16.f, 6.f));
		}
		for (int32 Index = 0; Index < CurrentInfiniteBeat.Choices.Num(); ++Index)
		{
			FString DisabledReason;
			const bool bOpeningWordingPending = bOpeningChoiceWordingPending;
			const bool bNarrativeBusy = bInfiniteNarrativeRequestInFlight;
			const bool bNarrativeRefreshRequired = bInfiniteNarrativeNeedsRefresh;
			const bool bAvailable = !bOpeningWordingPending && !bNarrativeBusy
				&& !bNarrativeRefreshRequired
				&& !bFreeRPForcedJumpPending
				&& IsInfiniteChoiceAvailable(CurrentInfiniteBeat.Choices[Index], DisabledReason);
			if (!bAvailable && DisabledReason.IsEmpty())
				DisabledReason = bFreeRPForcedJumpPending
					? (bFreeRPForcedJumpAwaitingContinue ? TEXT("等待点击继续") : TEXT("自由 RP 自动衔接中"))
					: bNarrativeBusy ? TEXT("正在推演本轮剧情")
					: bNarrativeRefreshRequired ? TEXT("剧情走向已更改，请先重新推演本幕")
					: TEXT("当前不可用");
			const FString ChoiceText = bOpeningWordingPending
				? TEXT("正在衔接法器选择……")
				: bAvailable
				? CurrentInfiniteBeat.Choices[Index].Text
				: FString::Printf(TEXT("%s（%s）"), *CurrentInfiniteBeat.Choices[Index].Text, *DisabledReason);
			static const TCHAR* ChoiceMarks[] = {TEXT("壹"), TEXT("贰"), TEXT("叁"), TEXT("肆")};
			const FString ChoiceLabel = FString::Printf(TEXT("%s　%s"),
				Index < UE_ARRAY_COUNT(ChoiceMarks) ? ChoiceMarks[Index] : *FString::FromInt(Index + 1), *ChoiceText);
			UHorizontalBox* Row = NewObject<UHorizontalBox>(Box);
			Row->AddChildToHorizontalBox(NewObject<USpacer>(Row))->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			UButton* ChoiceButton = MakeLinkedButton(Row, ChoiceLabel, TEXT("rp_choice"), Index, 18);
			UTextBlock* ChoiceLabelText = nullptr;
			RPConfigureAdaptiveChoiceButton(ChoiceButton, ChoiceLabel, 18, SafeWidth, &ChoiceLabelText);
			RPInfiniteChoiceButtons.Add(ChoiceButton);
			RPOpeningChoiceLabels.Add(ChoiceLabelText);
			ChoiceButton->SetIsEnabled(bAvailable);
			if (!FirstChoiceButton && bAvailable) FirstChoiceButton = ChoiceButton;
			RPAddToHBox(Row, ChoiceButton, FMargin(8.f, 6.f));
			Row->AddChildToHorizontalBox(NewObject<USpacer>(Row))->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			RPAddToVBox(Box, Row);
		}
		if (bFreeRPForcedJumpPending && bFreeRPForcedJumpAwaitingContinue)
		{
			RPAddPad(Box, 18.f);
			RPAddToVBox(Box, RPMakeWrappedText(Box,
				TEXT("本轮自由 RP 的回应已生成。读完后点击继续，才会沿引擎锁定的方向自动衔接。"),
				15, FAscendUIStyle::DimGray(), ETextJustify::Center), FMargin(80.f, 4.f));
			UButton* ContinueButton = MakeLinkedButton(Box, TEXT("【继续自动衔接】"),
				TEXT("rp_free_rp_continue"), 0, 21);
			ContinueButton->SetIsEnabled(IsFreeRPForcedContinueAvailableForAutomationTest(
				bFreeRPForcedJumpPending, bFreeRPForcedJumpAwaitingContinue,
				bInfiniteNarrativeRequestInFlight || bInfiniteFreeRPForcedTurnActive));
		}
		if (bInfiniteNarrativeNeedsRefresh)
		{
			RPAddToVBox(Box, MakeLinkedButton(Box, TEXT("【重新推演本幕】"), TEXT("rp_retry"), 0, 19),
				FMargin(120.f, 12.f, 120.f, 4.f));
		}
		if (IsFreeRPInputAvailableForAutomationTest(InfiniteNarrativeSettings.bShowFreeformInput,
			bFreeRPForcedJumpPending, bInfiniteNarrativeRequestInFlight,
			CurrentInfiniteBeat.Diagnostic.StartsWith(TEXT("authored_opening:"))
			|| bInfiniteNarrativeNeedsRefresh))
		{
			RPAddPad(Box, 18.f);
			RPFreeformInput = NewObject<UEditableTextBox>(Box);
			RPFreeformInput->SetHintText(FText::FromString(
			TEXT("自由 RP：描述你的行动；回应生成后先阅读，点击继续才沿随机方向衔接……")));
			const float FreeformSideInset = FMath::Clamp(RPStreamingTextWidth * 0.08f, 8.f, 32.f);
			RPAddToVBox(Box, RPFreeformInput, FMargin(FreeformSideInset, 5.f));
			UHorizontalBox* SubmitRow = NewObject<UHorizontalBox>(Box);
			SubmitRow->AddChildToHorizontalBox(NewObject<USpacer>(SubmitRow))->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			UButton* FreeformButton = MakeLinkedButton(SubmitRow, TEXT("【提交自由 RP】"), TEXT("rp_freeform"), 0, 17);
			FreeformButton->SetIsEnabled(!bInfiniteNarrativeRequestInFlight && !bFreeRPForcedJumpPending);
			RPAddToHBox(SubmitRow, FreeformButton);
			SubmitRow->AddChildToHorizontalBox(NewObject<USpacer>(SubmitRow))->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			RPAddToVBox(Box, SubmitRow);
		}
	}

	RPAddPad(Box, 45.f);
	bRPStreamingSceneFinalized = true;
	if (FirstChoiceButton && GetWorld())
	{
		TWeakObjectPtr<UButton> WeakFirstChoice(FirstChoiceButton);
		GetWorld()->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this,
			[WeakFirstChoice]()
			{
				if (WeakFirstChoice.IsValid()) WeakFirstChoice->SetKeyboardFocus();
			}));
	}
	if (bShouldAutoScroll && GetWorld())
	{
		TWeakObjectPtr<UScrollBox> WeakScroll(RPScrollBox);
		GetWorld()->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this,
			[WeakScroll]()
			{
				if (WeakScroll.IsValid()) WeakScroll->ScrollToEnd();
			}));
	}
}

FVector2D AAscendPlayerController::GetInfiniteNarrativeLocalViewportSize() const
{
	// UMG layout is evaluated in Slate's logical space.  On a Retina Mac the
	// game viewport reports physical pixels while cached widget geometry is often
	// ~1.5x larger; mixing the two makes the RP frame look pinned or undersized.
	auto ReadLocalSize = [](const UWidget* Widget) -> FVector2D
	{
		if (!Widget) return FVector2D::ZeroVector;
		const FVector2D LocalSize = Widget->GetCachedGeometry().GetLocalSize();
		return LocalSize.X > 1.f && LocalSize.Y > 1.f ? LocalSize : FVector2D::ZeroVector;
	};
	if (const FVector2D HostSize = ReadLocalSize(ScreenHost); !HostSize.IsNearlyZero())
		return HostSize;
	if (const FVector2D RootSize = ReadLocalSize(RootWidget); !RootSize.IsNearlyZero())
		return RootSize;

	const FVector2D PhysicalViewport = GetViewportSize();
	const float ApplicationScale = FMath::Max(1.f, FSlateApplication::Get().GetApplicationScale());
	return PhysicalViewport / ApplicationScale;
}

FVector2D AAscendPlayerController::GetInfiniteNarrativeFrameSize() const
{
	const FVector2D Viewport = GetInfiniteNarrativeLocalViewportSize();
	const float AvailableWidth = FMath::Max(306.f, Viewport.X);
	const float AvailableHeight = FMath::Max(300.f, Viewport.Y - 72.f);
	// Readable width comes directly from the viewport. The vertical scroll artwork
	// is allowed to be wider/taller than this external layout placeholder and is
	// clipped by it, so the asset's 3:4 ratio can never squeeze the text column.
	return FVector2D(AvailableWidth, AvailableHeight);
}

UWidget* AAscendPlayerController::MakeInfiniteNarrativeScreenFrame(float ViewportWidth,
	float SafeHeight, UScrollBox*& OutScroll, UVerticalBox*& OutContent) const
{
	OutScroll = nullptr;
	OutContent = nullptr;
	if (!RootWidget) return nullptr;

	UOverlay* Page = NewObject<UOverlay>(RootWidget);
	USizeBox* FrameViewport = NewObject<USizeBox>(Page);
	FrameViewport->SetWidthOverride(ViewportWidth);
	FrameViewport->SetHeightOverride(SafeHeight);
	FrameViewport->SetMinDesiredWidth(ViewportWidth);
	FrameViewport->SetMaxDesiredWidth(ViewportWidth);
	FrameViewport->SetMinDesiredHeight(SafeHeight);
	FrameViewport->SetMaxDesiredHeight(SafeHeight);
	// This is the external layout placeholder. It clips only the oversized
	// artwork; the ScrollBox below remains a normal, screen-safe hit-test region.
	FrameViewport->SetClipping(EWidgetClipping::ClipToBounds);
	// A desired-size Overlay can clamp an oversized child back to its allotted
	// viewport. Use explicit canvas slots so the decoration is really drawn at
	// its 3:4 size; FrameViewport remains the only clipping boundary.
	UCanvasPanel* Layers = NewObject<UCanvasPanel>(FrameViewport);
	FrameViewport->SetContent(Layers);

	const float ReadingWidth = RPScrollReadingWidthForViewport(ViewportWidth);
	const float PaperWidth = RPScrollPaperWidthForReading(ReadingWidth);
	const float ArtworkWidth = RPScrollArtworkWidthForReading(ReadingWidth);
	const float ArtworkHeight = ArtworkWidth / RPScrollArtworkAspect;
	UBorder* Surface = NewObject<UBorder>(Layers);
	if (UTexture2D* ScrollFrameTexture = FAscendArt::GetTexture(Surface,
		TEXT("Art/ui/rp_scroll_vertical_v2.png")))
	{
		// Keep the source asset vertical (3:4) and enlarge it independently of the
		// reading viewport. FrameViewport clips the top/bottom rollers when needed;
		// their desired height must never resize or push the text layer.
		FSlateBrush ScrollBrush;
		ScrollBrush.SetResourceObject(ScrollFrameTexture);
		// The source already has the intended 3:4 composition. Drawing the whole
		// image avoids treating its opaque-paper ratio as a nine-slice margin (which
		// would make the left/right margins add up beyond one and distort the frame).
		ScrollBrush.DrawAs = ESlateBrushDrawType::Image;
		ScrollBrush.ImageSize = FVector2D(ScrollFrameTexture->GetSizeX(), ScrollFrameTexture->GetSizeY());
		Surface->SetBrush(ScrollBrush);
		Surface->SetBrushColor(FLinearColor::White);
	}
	else
	{
		Surface->SetBrushColor(FLinearColor(0.012f, 0.022f, 0.025f, 0.34f));
	}
	Surface->SetClipping(EWidgetClipping::ClipToBounds);
	Surface->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	if (UCanvasPanelSlot* ArtworkSlot = Layers->AddChildToCanvas(Surface))
	{
		ArtworkSlot->SetAnchors(FAnchors(0.5f, 0.5f));
		ArtworkSlot->SetAlignment(FVector2D(0.5f, 0.5f));
		ArtworkSlot->SetPosition(FVector2D::ZeroVector);
		ArtworkSlot->SetSize(FVector2D(ArtworkWidth, ArtworkHeight));
		ArtworkSlot->SetZOrder(0);
	}

	USizeBox* ScrollSize = NewObject<USizeBox>(Layers);
	const float ScrollHeight = FMath::Max(240.f, SafeHeight - RPScrollViewportInset);
	// The content viewport uses only the screen-safe frame height. It is deliberately
	// independent from the artwork's measured top/bottom transparent pixels.
	ScrollSize->SetWidthOverride(PaperWidth);
	ScrollSize->SetHeightOverride(ScrollHeight);
	ScrollSize->SetMinDesiredWidth(PaperWidth);
	ScrollSize->SetMaxDesiredWidth(PaperWidth);
	ScrollSize->SetMinDesiredHeight(ScrollHeight);
	ScrollSize->SetMaxDesiredHeight(ScrollHeight);
	OutScroll = NewObject<UScrollBox>(ScrollSize);
	OutScroll->SetClipping(EWidgetClipping::ClipToBoundsAlways);
	ScrollSize->SetContent(OutScroll);
	if (UCanvasPanelSlot* ScrollSlot = Layers->AddChildToCanvas(ScrollSize))
	{
		ScrollSlot->SetAnchors(FAnchors(0.5f, 0.5f));
		ScrollSlot->SetAlignment(FVector2D(0.5f, 0.5f));
		ScrollSlot->SetPosition(FVector2D::ZeroVector);
		ScrollSlot->SetSize(FVector2D(PaperWidth, ScrollHeight));
		ScrollSlot->SetZOrder(1);
	}

	UOverlaySlot* PageSlot = Page->AddChildToOverlay(FrameViewport);
	PageSlot->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Center);
	PageSlot->SetVerticalAlignment(EVerticalAlignment::VAlign_Center);

	OutContent = NewObject<UVerticalBox>(OutScroll);
	USizeBox* WidthFrame = NewObject<USizeBox>(OutScroll);
	// Keep the scroll viewport on the opaque paper, but give its child a narrower
	// centered reading column. This decouples text width from the vertical asset's
	// source aspect ratio while retaining a single safe width for every RP state.
	const float ContentWidth = ReadingWidth;
	WidthFrame->SetWidthOverride(ContentWidth);
	WidthFrame->SetMinDesiredWidth(ContentWidth);
	WidthFrame->SetMaxDesiredWidth(ContentWidth);
	WidthFrame->SetClipping(EWidgetClipping::ClipToBounds);
	WidthFrame->SetContent(OutContent);
	if (UScrollBoxSlot* ContentSlot = Cast<UScrollBoxSlot>(OutScroll->AddChild(WidthFrame)))
	{
		ContentSlot->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Center);
		ContentSlot->SetVerticalAlignment(EVerticalAlignment::VAlign_Top);
	}
	return Page;
}

void AAscendPlayerController::TickInfiniteNarrativeStreamReveal()
{
	const double Now = FPlatformTime::Seconds();
	bool bPending = false;
	for (int32 Index = 0; Index < RPStreamingRevealGlyphs.Num(); ++Index)
	{
		UTextBlock* Glyph = RPStreamingRevealGlyphs[Index];
		if (!Glyph) continue;
		const double Start = RPStreamingRevealStartTimes.IsValidIndex(Index)
			? RPStreamingRevealStartTimes[Index] : Now;
		const float T = FMath::Clamp(static_cast<float>((Now - Start) / 0.16), 0.f, 1.f);
		const float Ease = T * T * (3.f - 2.f * T);
		Glyph->SetRenderOpacity(Ease);
		if (T < 1.f)
		{
			bPending = true;
		}
	}
	if (!bPending)
	{
		if (RPStreamingRevealCompletedAt <= 0.0)
		{
			RPStreamingRevealCompletedAt = Now;
			if (!RPVisualExpectedTail.IsEmpty())
				UE_LOG(LogTemp, Display, TEXT("[RPVisualTest] alpha_complete_at=%.3f glyphs=%d"),
					RPStreamingRevealCompletedAt, RPStreamingRevealGlyphs.Num());
		}
		if (GetWorld()) GetWorld()->GetTimerManager().ClearTimer(RPStreamingRevealTimer);
	}
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
	// Settings is an independent root overlay. Stream data may continue to fill the
	// mounted RP page underneath it, but must never create a new page over Settings.
	if (!bCombatPrefetch && CurrentScreen != EGameScreen::InfiniteNarrative) return;
	RenderInfiniteNarrativeStreamPreview(Update);
}

void AAscendPlayerController::RenderInfiniteNarrativeStreamPreview(
	const FInfiniteNarrativeStreamUpdate& Update)
{
	FInfiniteNarrativeBeat Preview = Update.Preview;
	NormalizeRPBeatForDisplay(Preview);
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
	const bool bHasVisiblePreview = !Preview.Narration.IsEmpty()
		|| Preview.DialogueLines.Num() > 0;
	if (!bHasVisiblePreview) return;

	const FVector2D FrameSize = GetInfiniteNarrativeFrameSize();
	bool bShouldAutoScroll = true;
	if (RPScrollBox)
	{
		bShouldAutoScroll = ShouldRPStreamingAutoScroll(
			RPScrollBox->GetScrollOffset(), RPScrollBox->GetScrollOffsetOfEnd());
	}
	if (!RPStreamingContainer)
	{
		const float SafeWidth = RPScrollReadingWidthForViewport(FrameSize.X);
		RPStreamingTextWidth = SafeWidth;
		RPStreamingPortraitSize = FMath::Clamp((SafeWidth - 142.f) * 0.32f, 72.f, 208.f);
		RPStreamingFrameHeight = FrameSize.Y;
	}
	if (!RPContentBox || CurrentScreen != EGameScreen::InfiniteNarrative)
	{
		// Keep one fixed logical-space frame for every RP state. The outer overlay
		// centers the reading area while the inner content remains stable for
		// append-only glyph coordinates and vertical scrolling.
		UScrollBox* Scroll = nullptr;
		UVerticalBox* Box = nullptr;
		UWidget* Page = MakeInfiniteNarrativeScreenFrame(
			FrameSize.X, RPStreamingFrameHeight, Scroll, Box);
		if (!Page || !Scroll || !Box) return;
		RPScrollBox = Scroll;
		RPContentBox = Box;
		RPGenerationStatusText = RPMakeWrappedText(Box, StatusText, 15, FAscendUIStyle::DimGray());
		RPAddToVBox(Box, RPGenerationStatusText, FMargin(16.f, 8.f, 16.f, 12.f));
		SetScreen(Page, EGameScreen::InfiniteNarrative);
		bShouldAutoScroll = true;
	}

	if (!RPStreamingContainer)
	{
		RPStreamingContainer = NewObject<UVerticalBox>(RPContentBox);
		RPStreamingContainer->SetClipping(EWidgetClipping::ClipToBounds);
		RPStreamingNarrationGlyphBox = NewObject<UVerticalBox>(RPStreamingContainer);
		RPStreamingNarrationGlyphBox->SetClipping(EWidgetClipping::ClipToBounds);
		RPAddToVBox(RPStreamingContainer, RPStreamingNarrationGlyphBox, FMargin(16.f, 10.f));
		RPStreamingDialogueBox = NewObject<UVerticalBox>(RPStreamingContainer);
		RPStreamingDialogueBox->SetClipping(EWidgetClipping::ClipToBounds);
		RPAddToVBox(RPStreamingContainer, RPStreamingDialogueBox, FMargin(8.f));
		RPAddToVBox(RPContentBox, RPStreamingContainer, FMargin(4.f));
	}

	const int32 NarrationGlyphStart = RPStreamingRevealGlyphs.Num();
	const FString NormalizedNarration = NormalizeRPLineEndings(Preview.Narration);
	FString MergedNarration;
	bool bRebuildNarration = false;
	MergeRPStreamText(RPStreamingNarrationSource, NormalizedNarration,
		MergedNarration, bRebuildNarration);
	if (bRebuildNarration)
	{
		// A provider can repair an earlier partial JSON prefix before completion.
		// Rebuild only this narration segment; dialogue rows and their portraits stay
		// mounted and keep their existing geometry.
		if (RPStreamingNarrationGlyphBox) RPStreamingNarrationGlyphBox->ClearChildren();
		RPStreamingNarrationSource.Reset();
		RPStreamingNarrationLines.Reset();
		RPStreamingNarrationLineWidths.Reset();
		bRPStreamingNarrationNeedsIndent = true;
		bRPStreamingNarrationPreviousNewline = false;
	}
	RPAppendStreamingGlyphs(RPStreamingNarrationGlyphBox, MergedNarration,
		RPStreamingNarrationSource, RPStreamingNarrationLines, RPStreamingNarrationLineWidths,
		bRPStreamingNarrationNeedsIndent, bRPStreamingNarrationPreviousNewline, true,
		RPStreamingTextWidth - 32.f, 19, FAscendUIStyle::InkBlack(), RPStreamingRevealGlyphs);

	const FString Registry = ResolveNarrativeRegistryForDisplay(InfiniteNarrativeSettings);
	for (int32 Index = 0; Index < Preview.DialogueLines.Num(); ++Index)
	{
		const FInfiniteDialogueLine& Line = Preview.DialogueLines[Index];
		const FString DialogueKey = MakeRPDialogueIdentityKey(Line.Speaker,
			Line.PortraitId, Line.Expression);
		const bool bNeedNewRow = !RPStreamingDialogueGlyphBoxes.IsValidIndex(Index)
			|| !RPStreamingDialogueRows.IsValidIndex(Index);
		if (bNeedNewRow)
		{
			UVerticalBox* BodyBox = nullptr;
			UWidget* PortraitWidget = nullptr;
			USizeBox* BubbleSize = nullptr;
			float BodyTextWidth = 0.f;
			UWidget* DialogueRow = RPMakeStreamingDialogueBubble(
				RPStreamingDialogueBox, Line, InfiniteNarrativeSettings.bShowSpeakerPortrait,
				Registry, &BodyBox, RPStreamingTextWidth - 16.f, RPStreamingPortraitSize,
				&PortraitWidget, &BubbleSize, &BodyTextWidth);
			RPAddToVBox(RPStreamingDialogueBox, DialogueRow, FMargin(8.f, 8.f));
			RPStreamingDialogueGlyphBoxes.SetNum(Index + 1);
			RPStreamingDialogueRows.SetNum(Index + 1);
			RPStreamingDialogueBubbleSizes.SetNum(Index + 1);
			RPStreamingDialogueTextWidths.SetNum(Index + 1);
			RPStreamingPortraitWidgets.SetNum(Index + 1);
			RPStreamingDialogueSources.SetNum(Index + 1);
			RPStreamingDialogueLines.SetNum(Index + 1);
			RPStreamingDialogueLineWidths.SetNum(Index + 1);
			RPStreamingDialogueNeedsIndent.SetNum(Index + 1);
			RPStreamingDialoguePreviousNewline.SetNum(Index + 1);
			RPStreamingDialogueKeys.SetNum(Index + 1);
			RPStreamingDialogueGlyphBoxes[Index] = BodyBox;
			RPStreamingDialogueRows[Index] = DialogueRow;
			RPStreamingDialogueBubbleSizes[Index] = BubbleSize;
			RPStreamingDialogueTextWidths[Index] = BodyTextWidth;
			RPStreamingPortraitWidgets[Index] = PortraitWidget;
			RPStreamingDialogueSources[Index].Reset();
			RPStreamingDialogueLines[Index].Reset();
			RPStreamingDialogueLineWidths[Index].Reset();
			// Dialogue paragraphs follow the same reading convention as narration:
			// each natural paragraph starts with two ideographic spaces. Starting the
			// state at true also covers the first paragraph, before any provider newline
			// has arrived.
			RPStreamingDialogueNeedsIndent[Index] = true;
			RPStreamingDialoguePreviousNewline[Index] = false;
			RPStreamingDialogueKeys[Index] = DialogueKey;
		}
		else if (!RPStreamingDialogueKeys.IsValidIndex(Index)
			|| RPStreamingDialogueKeys[Index] != DialogueKey)
		{
			// Speaker/portrait/expression is part of the identity key. If a final Beat
			// corrects that metadata, replace only this row and replay its text; never
			// leave a new speaker's words attached to the previous portrait.
			if (UWidget* OldRow = RPStreamingDialogueRows[Index])
				if (RPStreamingDialogueBox) RPStreamingDialogueBox->RemoveChild(OldRow);
			UVerticalBox* BodyBox = nullptr;
			UWidget* PortraitWidget = nullptr;
			USizeBox* BubbleSize = nullptr;
			float BodyTextWidth = 0.f;
			UWidget* DialogueRow = RPMakeStreamingDialogueBubble(
				RPStreamingDialogueBox, Line, InfiniteNarrativeSettings.bShowSpeakerPortrait,
				Registry, &BodyBox, RPStreamingTextWidth - 16.f, RPStreamingPortraitSize,
				&PortraitWidget, &BubbleSize, &BodyTextWidth);
			if (RPStreamingDialogueBox)
			{
				if (UVerticalBoxSlot* Slot = Cast<UVerticalBoxSlot>(
					RPStreamingDialogueBox->InsertChildAt(Index, DialogueRow)))
				{
					Slot->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Fill);
					Slot->SetPadding(FMargin(8.f, 8.f));
				}
			}
			RPStreamingDialogueGlyphBoxes[Index] = BodyBox;
			RPStreamingDialogueRows[Index] = DialogueRow;
			RPStreamingDialogueBubbleSizes[Index] = BubbleSize;
			if (!RPStreamingDialogueTextWidths.IsValidIndex(Index))
				RPStreamingDialogueTextWidths.SetNum(Index + 1);
			RPStreamingDialogueTextWidths[Index] = BodyTextWidth;
			RPStreamingPortraitWidgets[Index] = PortraitWidget;
			RPStreamingDialogueSources[Index].Reset();
			RPStreamingDialogueLines[Index].Reset();
			RPStreamingDialogueLineWidths[Index].Reset();
			RPStreamingDialogueNeedsIndent[Index] = true;
			RPStreamingDialoguePreviousNewline[Index] = false;
			RPStreamingDialogueKeys[Index] = DialogueKey;
		}
		if (!RPStreamingDialogueSources.IsValidIndex(Index)
			|| !RPStreamingDialogueLines.IsValidIndex(Index)
			|| !RPStreamingDialogueLineWidths.IsValidIndex(Index)
			|| !RPStreamingDialogueNeedsIndent.IsValidIndex(Index)
			|| !RPStreamingDialoguePreviousNewline.IsValidIndex(Index)) continue;
		const FString NormalizedDialogue = NormalizeRPDialogueText(Line.Text);
		FString MergedDialogue;
		bool bRebuildDialogue = false;
		MergeRPStreamText(RPStreamingDialogueSources[Index], NormalizedDialogue,
			MergedDialogue, bRebuildDialogue);
		if (bRebuildDialogue)
		{
			if (RPStreamingDialogueGlyphBoxes[Index]) RPStreamingDialogueGlyphBoxes[Index]->ClearChildren();
			RPStreamingDialogueSources[Index].Reset();
			RPStreamingDialogueLines[Index].Reset();
			RPStreamingDialogueLineWidths[Index].Reset();
			RPStreamingDialogueNeedsIndent[Index] = true;
			RPStreamingDialoguePreviousNewline[Index] = false;
		}
		const float DialogueBodyTextWidth = RPStreamingDialogueTextWidths.IsValidIndex(Index)
			? RPStreamingDialogueTextWidths[Index]
			: RPDialogueBodyTextWidth(RPStreamingTextWidth - 16.f,
				InfiniteNarrativeSettings.bShowSpeakerPortrait
					&& RPStreamingTextWidth - 16.f < 620.f,
				InfiniteNarrativeSettings.bShowSpeakerPortrait, RPStreamingPortraitSize);
		RPAppendStreamingGlyphs(RPStreamingDialogueGlyphBoxes[Index], MergedDialogue,
			RPStreamingDialogueSources[Index], RPStreamingDialogueLines[Index],
			RPStreamingDialogueLineWidths[Index], RPStreamingDialogueNeedsIndent[Index],
				RPStreamingDialoguePreviousNewline[Index], true, DialogueBodyTextWidth,
			20, FAscendUIStyle::PaperWhite(), RPStreamingRevealGlyphs,
			RPStreamingDialogueBubbleSizes[Index]);
	}

	// Stagger newly-added glyphs by a small amount and ease their alpha.  The
	// widgets themselves are created only for the new suffix; existing glyph
	// geometry is never touched.  This is the actual visual stream, not a batch
	// replacement of a TextBlock at chunk cadence.
	const double RevealBase = FPlatformTime::Seconds();
	while (RPStreamingRevealStartTimes.Num() < RPStreamingRevealGlyphs.Num())
	{
		const int32 GlyphIndex = RPStreamingRevealStartTimes.Num();
		RPStreamingRevealStartTimes.Add(RevealBase + (GlyphIndex - NarrationGlyphStart) * 0.018);
	}
	if (RPStreamingRevealGlyphs.Num() > 0 && GetWorld())
	{
		if (!RPStreamingRevealTimer.IsValid())
		{
			GetWorld()->GetTimerManager().SetTimer(RPStreamingRevealTimer,
				FTimerDelegate::CreateWeakLambda(this,
					[this]() { TickInfiniteNarrativeStreamReveal(); }), 0.016f, true);
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
	bCombatNarrativePrefetchNeedsRetry = false;
	bCombatNarrativePrefetchRequestInFlight = false;
	bDiscardCombatNarrativePrefetch = false;
	bWaitingForCombatNarrativeAfterReward = false;
	bCombatPrefetchIncludedLog = false;
	CombatNarrativePrefetchDiagnostic.Reset();
	CombatNarrativePrefetchedBeat = FInfiniteNarrativeBeat();
	CombatNarrativePrefetchStreamUpdate = FInfiniteNarrativeStreamUpdate();
	bHasCombatNarrativePrefetchStreamUpdate = false;
	CombatNarrativePrefetchStoryDirectionCacheKey.Reset();
}

void AAscendPlayerController::StartCombatNarrativePrefetch(bool bIncludeCombatLog)
{
	if (!Run || !ShouldStartCombatNarrativePrefetch(Run->State.bInfiniteNarrativeMode,
		Run->State.bRunActive, bInfiniteNarrativeRequestInFlight, bCombatNarrativePrefetchReady)) return;

	bCombatNarrativePrefetchFailed = false;
	bCombatNarrativePrefetchNeedsRetry = false;
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
	ActiveInfiniteNarrativeRequestKind = EInfiniteNarrativeRequestKind::Normal;
	bInfiniteNarrativeRequestInFlight = true;
	bCombatNarrativePrefetchRequestInFlight = true;
	CombatNarrativePrefetchStoryDirectionCacheKey =
		UInfiniteNarrativeService::BuildStoryDirectionCacheKey(InfiniteNarrativeSettings);
	const uint64 RequestToken = InfiniteNarrativeFlowSerial;
	UE_LOG(LogTemp, Display, TEXT("[InfiniteRP] combat prefetch start mode=%s cycle=%d log_lines=%d"),
		bIncludeCombatLog ? TEXT("A_after_combat_with_log") : TEXT("B_combat_start_assume_victory"),
		Run->State.InfiniteCycle, bIncludeCombatLog ? CombatLogLines.Num() : 0);
	InfiniteNarrativeService->Generate(InfiniteNarrativeSettings, Context,
		FOnInfiniteNarrativeReady::CreateWeakLambda(this,
			[this, RequestToken](bool bFromLLM, const FInfiniteNarrativeBeat& Beat)
			{
				if (RequestToken != InfiniteNarrativeFlowSerial) return;
				HandleCombatNarrativePrefetchReady(bFromLLM, Beat);
			}),
		FOnInfiniteNarrativeStreamUpdate::CreateWeakLambda(this,
			[this, RequestToken](const FInfiniteNarrativeStreamUpdate& Update)
			{
				if (RequestToken != InfiniteNarrativeFlowSerial) return;
				HandleInfiniteNarrativeStreamUpdate(Update, true);
			}));
}

void AAscendPlayerController::HandleCombatNarrativePrefetchReady(bool bFromLLM,
	const FInfiniteNarrativeBeat& Beat)
{
	const bool bWasWaitingForReward = bWaitingForCombatNarrativeAfterReward;
	bInfiniteNarrativeRequestInFlight = false;
	bCombatNarrativePrefetchRequestInFlight = false;
	const FString CurrentStoryDirectionCacheKey =
		UInfiniteNarrativeService::BuildStoryDirectionCacheKey(InfiniteNarrativeSettings);
	if (!CombatNarrativePrefetchStoryDirectionCacheKey.IsEmpty()
		&& CombatNarrativePrefetchStoryDirectionCacheKey != CurrentStoryDirectionCacheKey)
	{
		bCombatNarrativePrefetchReady = false;
		bCombatNarrativePrefetchFailed = true;
		bCombatNarrativePrefetchNeedsRetry = bWasWaitingForReward;
		bDiscardCombatNarrativePrefetch = false;
		bWaitingForCombatNarrativeAfterReward = false;
		CombatNarrativePrefetchDiagnostic = TEXT("剧情走向已更改，旧战后预取已作废");
		bHasCombatNarrativePrefetchStreamUpdate = false;
		CombatNarrativePrefetchedBeat = FInfiniteNarrativeBeat();
		CombatNarrativePrefetchStreamUpdate = FInfiniteNarrativeStreamUpdate();
		UE_LOG(LogTemp, Display, TEXT("[InfiniteRP] discarded stale combat prefetch after story direction change"));
		if (bWasWaitingForReward
			&& (SettingsOverlayLayer || CurrentScreen == EGameScreen::InfiniteNarrative))
		{
			bCombatNarrativePrefetchNeedsRetry = false;
			ShowInfiniteNarrativeError(CombatNarrativePrefetchDiagnostic);
		}
		return;
	}
	// The reward flow can leave the previous RP page visible while combat prefetch
	// runs in the background. Clear its loading label as soon as the request ends;
	// otherwise a successful prefetch still looks permanently stuck to the player.
	if (RPGenerationStatusText)
	{
		RPGenerationStatusText->SetVisibility(ESlateVisibility::Collapsed);
	}
	if (bDiscardCombatNarrativePrefetch || !Run || !Run->State.bRunActive)
	{
		bWaitingForCombatNarrativeAfterReward = false;
		UE_LOG(LogTemp, Display, TEXT("[InfiniteRP] discarded combat prefetch result"));
		return;
	}
	if (!bFromLLM || Beat.bError)
	{
		bCombatNarrativePrefetchFailed = true;
		bCombatNarrativePrefetchNeedsRetry = bWasWaitingForReward;
		bWaitingForCombatNarrativeAfterReward = false;
		CombatNarrativePrefetchDiagnostic = Beat.Diagnostic.IsEmpty()
			? TEXT("后台战后剧情生成失败") : Beat.Diagnostic;
		UE_LOG(LogTemp, Warning, TEXT("[InfiniteRP] combat prefetch failed: %s"),
			*CombatNarrativePrefetchDiagnostic);
		if (bWasWaitingForReward
			&& (SettingsOverlayLayer || CurrentScreen == EGameScreen::InfiniteNarrative))
		{
			bCombatNarrativePrefetchNeedsRetry = false;
			ShowInfiniteNarrativeError(CombatNarrativePrefetchDiagnostic);
		}
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
	const bool bWasWaitingForReward = bWaitingForCombatNarrativeAfterReward;
	const FString CurrentStoryDirectionCacheKey =
		UInfiniteNarrativeService::BuildStoryDirectionCacheKey(InfiniteNarrativeSettings);
	if (!CombatNarrativePrefetchStoryDirectionCacheKey.IsEmpty()
		&& CombatNarrativePrefetchStoryDirectionCacheKey != CurrentStoryDirectionCacheKey)
	{
		bCombatNarrativePrefetchReady = false;
		bCombatNarrativePrefetchFailed = true;
		bCombatNarrativePrefetchNeedsRetry = bWasWaitingForReward;
		bWaitingForCombatNarrativeAfterReward = false;
		bDiscardCombatNarrativePrefetch = false;
		CombatNarrativePrefetchDiagnostic = TEXT("剧情走向已更改，旧战后预取已作废");
		CombatNarrativePrefetchedBeat = FInfiniteNarrativeBeat();
		CombatNarrativePrefetchStreamUpdate = FInfiniteNarrativeStreamUpdate();
		bHasCombatNarrativePrefetchStreamUpdate = false;
		CombatNarrativePrefetchStoryDirectionCacheKey.Reset();
		UE_LOG(LogTemp, Display, TEXT("[InfiniteRP] refused stale combat prefetch at consume"));
		if (bWasWaitingForReward && !SettingsOverlayLayer)
		{
			bCombatNarrativePrefetchNeedsRetry = false;
			ShowInfiniteNarrativeError(CombatNarrativePrefetchDiagnostic);
		}
		return;
	}
	const FInfiniteNarrativeBeat Beat = CombatNarrativePrefetchedBeat;
	bCombatNarrativePrefetchReady = false;
	bCombatNarrativePrefetchNeedsRetry = false;
	bWaitingForCombatNarrativeAfterReward = false;
	CombatNarrativePrefetchedBeat = FInfiniteNarrativeBeat();
	HandleInfiniteNarrativeReady(true, Beat);
}

void AAscendPlayerController::HandleInfiniteNarrativeReady(bool bFromLLM, const FInfiniteNarrativeBeat& Beat)
{
	const bool bSettingsOverlayActive = SettingsOverlayLayer != nullptr;
	const bool bLegacySettingsScreen = CurrentScreen == EGameScreen::Settings
		&& !bSettingsOverlayActive;
	const FInfiniteNarrativeBeat PreviousBeat = CurrentInfiniteBeat;
	const bool bPreviousChoiceResolved = bInfiniteChoiceResolved;
	const EInfiniteNarrativeRequestKind CompletedRequestKind = ActiveInfiniteNarrativeRequestKind;
	const FString CompletedFreeformAction = PendingInfiniteFreeformAction;
	const bool bCompletedFreeform = CompletedRequestKind == EInfiniteNarrativeRequestKind::Freeform;
	const bool bCompletedForcedJump = CompletedRequestKind == EInfiniteNarrativeRequestKind::ForcedFreeRPJump;
	bInfiniteNarrativeRequestInFlight = false;
	bInputLocked = false;
	const FString CurrentStoryDirectionCacheKey =
		UInfiniteNarrativeService::BuildStoryDirectionCacheKey(InfiniteNarrativeSettings);
	if (!Beat.StoryDirectionCacheKey.IsEmpty()
		&& Beat.StoryDirectionCacheKey != CurrentStoryDirectionCacheKey)
	{
		UE_LOG(LogTemp, Display, TEXT("[InfiniteRP] discarded stale narrative result after story direction change"));
		bInfiniteNarrativeNeedsRefresh = true;
		if (bCompletedForcedJump) bInfiniteFreeRPForcedTurnActive = true;
		if (!bLegacySettingsScreen)
			ShowInfiniteNarrativeError(TEXT("剧情走向已更改，本轮旧生成已作废，请重新生成"));
		return;
	}
	CurrentInfiniteBeat = Beat;
	bInfiniteChoiceResolved = false;
	if (!bFromLLM || Beat.bError)
	{
		// Preserve the submitted freeform action or locked direction for the explicit
		// retry button. Neither phase is allowed to reopen a different interaction.
		bInfiniteNarrativeNeedsRefresh = true;
		if (bCompletedForcedJump) bInfiniteFreeRPForcedTurnActive = true;
		if (bCompletedFreeform || bLegacySettingsScreen)
		{
			// A freeform action is not written to RPRecentTurns until its response is
			// accepted. Restore the visible source beat so a retry still sends that
			// scene together with the preserved player text.
			CurrentInfiniteBeat = PreviousBeat;
			bInfiniteChoiceResolved = bPreviousChoiceResolved;
		}
		ResetInfiniteNarrativeStreamPreview();
		if (Run) Run->SaveRun();
		UE_LOG(LogTemp, Warning, TEXT("[InfiniteRP] beat failed diagnostic=%s"), *Beat.Diagnostic);
		if (!bLegacySettingsScreen)
			ShowInfiniteNarrativeError(Beat.Diagnostic);
		return;
	}
	bInfiniteNarrativeNeedsRefresh = false;
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
	// The seed is a one-turn launch context. Once the writer has accepted the first
	// scene, subsequent turns must rely on the persisted scene/history instead of
	// repeatedly reintroducing the opening premise.
	PendingInfiniteOpeningSeed.Reset();
	if (RPStreamingContainer && RPContentBox && RPScrollBox
		&& CurrentScreen == EGameScreen::InfiniteNarrative && !bRPStreamingSceneFinalized)
	{
		// The service's terminal flush normally delivered this already.  Reconcile
		// once more against the authoritative Beat because providers can finish a
		// response between the last throttled preview and the ready delegate.  The
		// renderer appends a suffix when possible and rebuilds only a mismatching
		// narration/dialogue segment when a provider rewrites its prefix.
		FInfiniteNarrativeStreamUpdate FinalUpdate;
		FinalUpdate.Stage = EInfiniteNarrativeStreamStage::Compiling;
		FinalUpdate.Preview = Beat;
		NormalizeRPBeatForDisplay(FinalUpdate.Preview);
		FinalUpdate.bHasVisibleContent = !FinalUpdate.Preview.Narration.IsEmpty()
			|| FinalUpdate.Preview.DialogueLines.Num() > 0;
		RenderInfiniteNarrativeStreamPreview(FinalUpdate);
	}
	UE_LOG(LogTemp, Display, TEXT("[InfiniteRP] beat ready source=llm diagnostic=%s"), *Beat.Diagnostic);
	if (bCompletedFreeform && Run && !CompletedFreeformAction.IsEmpty())
	{
		// Commit the player's action and the generated response as two bridge records.
		// The response's three newly generated choices are deliberately not stored;
		// only the one engine-selected direction is carried into the forced prompt.
		Run->AddRPHistory(FString::Printf(TEXT("自由 RP：%s"), *CompletedFreeformAction.Left(1200)));
		Run->AddRPNarrativeTurn(PreviousBeat.Title, PreviousBeat.Speaker, PreviousBeat.Narration,
			FlattenRPDialogueForHistory(PreviousBeat), CompletedFreeformAction,
			TEXT("玩家自由 RP 已被世界回应；本轮三个选项不进入上下文。"), TEXT("free_rp_response"),
			PreviousBeat.MemoryJson, InfiniteNarrativeSettings.CompressAfterRounds,
			InfiniteNarrativeSettings.RecentRawRounds, InfiniteNarrativeSettings.UnsummarizedTokenThreshold);

		const int32 ForcedChoiceIndex = ChooseFreeRPForcedChoiceIndexForAutomationTest(
			Beat.Choices.Num(), Run->GetRunSeed(), Run->State.RPTurnSerial);
		const FInfiniteNarrativeChoice* ForcedChoice = Beat.Choices.IsValidIndex(ForcedChoiceIndex)
			? &Beat.Choices[ForcedChoiceIndex] : nullptr;
		const FString LockedDirection = ForcedChoice
			? BuildFreeRPForcedDirection(*ForcedChoice)
			: TEXT("沿玩家刚才的自由行动的直接后果自然推进，不替玩家补写未发生的选择。");
		Run->State.bInfiniteFreeRPForcedJumpPending = true;
		Run->State.bInfiniteFreeRPForcedJumpAwaitingContinue = true;
		Run->State.InfiniteFreeRPForcedDirection = LockedDirection;
		Run->AddRPNarrativeTurn(Beat.Title, Beat.Speaker, Beat.Narration,
			FlattenRPDialogueForHistory(Beat), LockedDirection,
			TEXT("引擎已随机锁定下一幕方向；完成这次自动衔接后重新开放自由 RP。"),
			TEXT("free_rp_forced_jump"), Beat.MemoryJson,
			InfiniteNarrativeSettings.CompressAfterRounds,
			InfiniteNarrativeSettings.RecentRawRounds, InfiniteNarrativeSettings.UnsummarizedTokenThreshold);
		Run->SaveRun();
		bInfiniteFreeRPForcedTurnActive = false;
		PendingInfiniteFreeformAction.Reset();
		bPendingInfiniteFreeformActionRecorded = false;
		ActiveInfiniteNarrativeRequestKind = EInfiniteNarrativeRequestKind::Normal;
		if (!bLegacySettingsScreen) ShowInfiniteNarrative();
		return;
	}
	PendingInfiniteFreeformAction.Reset();
	bPendingInfiniteFreeformActionRecorded = false;
	if (bCompletedForcedJump && Run)
	{
		Run->State.bInfiniteFreeRPForcedJumpPending = false;
		Run->State.bInfiniteFreeRPForcedJumpAwaitingContinue = false;
		Run->State.InfiniteFreeRPForcedDirection.Reset();
		bInfiniteFreeRPForcedTurnActive = false;
		Run->SaveRun();
	}
	ActiveInfiniteNarrativeRequestKind = EInfiniteNarrativeRequestKind::Normal;
	if (!bLegacySettingsScreen)
		ShowInfiniteNarrative();
}

void AAscendPlayerController::ShowInfiniteNarrativeLoading(const FString& Message)
{
	ResetInfiniteNarrativeStreamPreview();
	RPScrollBox = nullptr;
	RPContentBox = nullptr;
	RPGenerationStatusText = nullptr;
	RPFreeformInput = nullptr;
	const FVector2D FrameSize = GetInfiniteNarrativeFrameSize();
	const float FrameWidth = FrameSize.X;
	const float SafeWidth = RPScrollReadingWidthForViewport(FrameWidth);
	const float SafeHeight = FrameSize.Y;
	UScrollBox* Scroll = nullptr;
	UVerticalBox* Box = nullptr;
	UWidget* Page = MakeInfiniteNarrativeScreenFrame(FrameWidth, SafeHeight, Scroll, Box);
	if (!Page || !Scroll || !Box) return;
	RPScrollBox = Scroll;
	RPContentBox = Box;
	const float LoadingSideInset = FMath::Clamp(SafeWidth * 0.06f, 8.f, 32.f);
	RPAddToVBox(Box, RPMakeWrappedText(Box, TEXT("天机流转"), 38, FAscendUIStyle::GoldYellow()));
	RPAddToVBox(Box, RPMakeWrappedText(Box, Message, 20, FAscendUIStyle::PaperWhite()), FMargin(LoadingSideInset, 20.f));
	RPAddToVBox(Box, RPMakeWrappedText(Box,
		TEXT("若接口超时或返回格式不合格，你可以重试，或保存当前进度返回标题。"),
		14, FAscendUIStyle::DimGray()), FMargin(LoadingSideInset, 10.f));
	SetScreen(Page, EGameScreen::InfiniteNarrative);
}

void AAscendPlayerController::ShowInfiniteNarrativeError(const FString& Diagnostic)
{
	ResetInfiniteNarrativeStreamPreview();
	RPScrollBox = nullptr;
	RPContentBox = nullptr;
	RPGenerationStatusText = nullptr;
	RPFreeformInput = nullptr;
	const FVector2D FrameSize = GetInfiniteNarrativeFrameSize();
	const float FrameWidth = FrameSize.X;
	const float SafeWidth = RPScrollReadingWidthForViewport(FrameWidth);
	const float SafeHeight = FrameSize.Y;
	UScrollBox* Scroll = nullptr;
	UVerticalBox* Box = nullptr;
	UWidget* Page = MakeInfiniteNarrativeScreenFrame(FrameWidth, SafeHeight, Scroll, Box);
	if (!Page || !Scroll || !Box) return;
	RPScrollBox = Scroll;
	RPContentBox = Box;
	const float ErrorSideInset = FMath::Clamp(SafeWidth * 0.06f, 8.f, 32.f);
	RPAddToVBox(Box, RPMakeWrappedText(Box, TEXT("天 机 暂 断"), 38, FAscendUIStyle::BloodRed()));
	RPAddToVBox(Box, RPMakeWrappedText(Box,
		TEXT("本次剧情导演调用没有完整结束。游戏没有推进剧情，也没有套用本地替代剧情。"),
		20, FAscendUIStyle::PaperWhite()), FMargin(ErrorSideInset, 22.f, ErrorSideInset, 8.f));
	RPAddToVBox(Box, RPMakeWrappedText(Box, Diagnostic.IsEmpty() ? TEXT("未知连接错误") : Diagnostic,
		14, FAscendUIStyle::DimGray()), FMargin(ErrorSideInset, 4.f, ErrorSideInset, 24.f));

	UHorizontalBox* Row = NewObject<UHorizontalBox>(Box);
	Row->AddChildToHorizontalBox(NewObject<USpacer>(Row))->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	RPAddToHBox(Row, MakeLinkedButton(Row, TEXT("【重试本幕】"), TEXT("rp_retry"), 0, 21), FMargin(12.f, 4.f));
	RPAddToHBox(Row, MakeLinkedButton(Row, TEXT("【保存进度并返回标题】"), TEXT("rp_error_title"), 0, 19), FMargin(12.f, 4.f));
	Row->AddChildToHorizontalBox(NewObject<USpacer>(Row))->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	RPAddToVBox(Box, Row);
	SetScreen(Page, EGameScreen::InfiniteNarrative);
}

void AAscendPlayerController::RefreshOpeningChoiceWordingUI()
{
	if (bInfiniteChoiceResolved) return;
	if (bInfiniteNarrativeNeedsRefresh) return;
	const bool bPending = bOpeningChoiceWordingPending;
	static const TCHAR* ChoiceMarks[] = {TEXT("壹"), TEXT("贰"), TEXT("叁"), TEXT("肆")};
	for (int32 Index = 0; Index < RPInfiniteChoiceButtons.Num(); ++Index)
	{
		UButton* Button = RPInfiniteChoiceButtons[Index];
		if (!Button) continue;
		FString DisabledReason;
		if (RPOpeningChoiceLabels.IsValidIndex(Index) && RPOpeningChoiceLabels[Index])
		{
			FString Label;
			if (bPending)
			{
				Label = FString::Printf(TEXT("%s　正在衔接法器选择…"),
					Index < UE_ARRAY_COUNT(ChoiceMarks) ? ChoiceMarks[Index] : *FString::FromInt(Index + 1));
			}
			else if (CurrentInfiniteBeat.Choices.IsValidIndex(Index))
			{
				Label = FString::Printf(TEXT("%s　%s"),
					Index < UE_ARRAY_COUNT(ChoiceMarks) ? ChoiceMarks[Index] : *FString::FromInt(Index + 1),
					*CurrentInfiniteBeat.Choices[Index].Text);
			}
			RPOpeningChoiceLabels[Index]->SetText(FText::FromString(Label));
		}
		Button->SetIsEnabled(!bPending && CurrentInfiniteBeat.Choices.IsValidIndex(Index)
			&& IsInfiniteChoiceAvailable(CurrentInfiniteBeat.Choices[Index], DisabledReason));
	}
	if (RPOpeningChoicePendingText)
	{
		RPOpeningChoicePendingText->SetVisibility(bPending
			? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
}

void AAscendPlayerController::ShowInfiniteNarrative()
{
	if (RPStreamingContainer && RPContentBox && RPScrollBox
		&& CurrentScreen == EGameScreen::InfiniteNarrative && !bRPStreamingSceneFinalized)
	{
		FinalizeInfiniteNarrativeStream();
		return;
	}
	ResetInfiniteNarrativeStreamPreview();
	RPInfiniteChoiceButtons.Reset();
	RPOpeningChoiceLabels.Reset();
	RPOpeningChoicePendingText = nullptr;
	const FVector2D FrameSize = GetInfiniteNarrativeFrameSize();
	const float FrameWidth = FrameSize.X;
	const float SafeWidth = RPScrollReadingWidthForViewport(FrameWidth);
	const float SafeHeight = FrameSize.Y;
	const float PortraitSize = FMath::Clamp((SafeWidth - 142.f) * 0.32f, 72.f, 208.f);
	UScrollBox* Scroll = nullptr;
	UVerticalBox* Box = nullptr;
	UWidget* Page = MakeInfiniteNarrativeScreenFrame(FrameWidth, SafeHeight, Scroll, Box);
	if (!Page || !Scroll || !Box) return;
	RPScrollBox = Scroll;
	RPContentBox = Box;
	RPGenerationStatusText = nullptr;
	const FString Status = Run ? FString::Printf(TEXT("第 %d 轮后 · 气血 %d/%d · 灵石 %d · 卡组 %d · 法宝 %d"),
		Run->State.InfiniteCycle, Run->State.HP, Run->State.MaxHP, Run->State.Gold,
		Run->State.Deck.Num(), Run->State.RelicIds.Num()) : TEXT("");
	UBorder* StatusRibbon = NewObject<UBorder>(Box);
	StatusRibbon->SetBrushColor(FLinearColor(0.035f, 0.060f, 0.058f, 0.88f));
	StatusRibbon->SetPadding(FMargin(22.f, 7.f));
	UTextBlock* StatusText = RPMakeWrappedText(StatusRibbon, Status, 14, FAscendUIStyle::DimGray());
	StatusRibbon->SetContent(StatusText);
	RPAddToVBox(Box, StatusRibbon, FMargin(16.f, 0.f, 16.f, 6.f));

	RPAddToVBox(Box, RPMakeWrappedText(Box, TEXT("◇"), 15, FAscendUIStyle::DimGray()), FMargin(0.f, 2.f));
	RPAddPad(Box, 8.f);
	RPAddReadingParagraph(Box, NormalizeRPLineEndings(CurrentInfiniteBeat.Narration), 20,
		FAscendUIStyle::InkBlack(),
		FMargin(16.f, 10.f, 16.f, 16.f), SafeWidth - 32.f);
	TArray<FInfiniteDialogueLine> DialogueLines = CurrentInfiniteBeat.DialogueLines;
	if (DialogueLines.Num() == 0 && !CurrentInfiniteBeat.Dialogue.IsEmpty())
	{
		DialogueLines.Add(MakeLegacyRPDialogueLine(CurrentInfiniteBeat));
	}
	for (FInfiniteDialogueLine& Line : DialogueLines)
	{
		Line.Text = NormalizeRPDialogueText(MoveTemp(Line.Text));
		RPAddToVBox(Box, RPMakeDialogueBubble(Box, Line, InfiniteNarrativeSettings.bShowSpeakerPortrait,
			ResolveNarrativeRegistryForDisplay(InfiniteNarrativeSettings), nullptr,
			SafeWidth - 24.f, PortraitSize), FMargin(8.f, 8.f));
	}
	RPAddPad(Box, 18.f);
	UButton* FirstChoiceButton = nullptr;
	const bool bFreeRPForcedJumpPending = bInfiniteFreeRPForcedTurnActive
		|| (Run && Run->State.bInfiniteFreeRPForcedJumpPending);
	const bool bFreeRPForcedJumpAwaitingContinue = Run
		&& Run->State.bInfiniteFreeRPForcedJumpAwaitingContinue;

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
			RPAddToVBox(ResultBox, RPMakeWrappedText(ResultBox, TEXT("本轮获得"), 20,
				FAscendUIStyle::GoldYellow()), FMargin(4.f, 10.f, 4.f, 4.f));
			UHorizontalBox* Items = NewObject<UHorizontalBox>(ResultBox);
			Items->AddChildToHorizontalBox(NewObject<USpacer>(Items))->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			for (const FString& RelicId : PendingNarrativeRelics)
				if (const FRelicData* Relic = Run ? Run->GetRelicData(RelicId) : nullptr)
					RPAddToHBox(Items, MakeRelicCardContentFromData(Items, *Relic, 1.35f), FMargin(12.f, 4.f));
			for (const FDeckCard& DeckCard : PendingNarrativeCards)
				if (const FCardData* Card = Run ? Run->GetCardData(DeckCard.CardId) : nullptr)
					RPAddToHBox(Items, MakeCardContentFromData(Items, *Card, DeckCard.bUpgraded, 1.28f), FMargin(12.f, 4.f));
			Items->AddChildToHorizontalBox(NewObject<USpacer>(Items))->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			RPAddToVBox(ResultBox, Items);
		}
		Resolution->SetContent(ResultBox);
		const float PanelSideInset = FMath::Clamp(SafeWidth * 0.08f, 8.f, 32.f);
		RPAddToVBox(Box, Resolution, FMargin(PanelSideInset, 8.f));

		UHorizontalBox* NextRow = NewObject<UHorizontalBox>(Box);
		NextRow->AddChildToHorizontalBox(NewObject<USpacer>(NextRow))->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		RPAddToHBox(NextRow, MakeLinkedButton(NextRow,
			ChoiceDestinationButtonLabel(PendingInfiniteChoice), TEXT("rp_resolution_next"), 0, 21), FMargin(8.f, 10.f));
		NextRow->AddChildToHorizontalBox(NewObject<USpacer>(NextRow))->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		RPAddToVBox(Box, NextRow);
	}
	else
	{
		if (bOpeningChoiceWordingPending)
		{
			RPOpeningChoicePendingText = RPMakeWrappedText(Box,
				TEXT("正在衔接法器选择……"), 16, FAscendUIStyle::DimGray(), ETextJustify::Center);
			RPAddToVBox(Box, RPOpeningChoicePendingText, FMargin(16.f, 2.f, 16.f, 6.f));
		}
		for (int32 Index = 0; Index < CurrentInfiniteBeat.Choices.Num(); ++Index)
		{
			FString DisabledReason;
			const bool bOpeningWordingPending = bOpeningChoiceWordingPending;
			const bool bNarrativeBusy = bInfiniteNarrativeRequestInFlight;
			const bool bNarrativeRefreshRequired = bInfiniteNarrativeNeedsRefresh;
			const bool bAvailable = !bOpeningWordingPending && !bNarrativeBusy
				&& !bNarrativeRefreshRequired
				&& !bFreeRPForcedJumpPending
				&& IsInfiniteChoiceAvailable(CurrentInfiniteBeat.Choices[Index], DisabledReason);
			if (!bAvailable && DisabledReason.IsEmpty())
				DisabledReason = bFreeRPForcedJumpPending
					? (bFreeRPForcedJumpAwaitingContinue ? TEXT("等待点击继续") : TEXT("自由 RP 自动衔接中"))
					: bNarrativeBusy ? TEXT("正在推演本轮剧情")
					: bNarrativeRefreshRequired ? TEXT("剧情走向已更改，请先重新推演本幕")
					: TEXT("当前不可用");
			const FString ChoiceText = bOpeningWordingPending
				? TEXT("正在衔接法器选择……")
				: bAvailable
				? CurrentInfiniteBeat.Choices[Index].Text
				: FString::Printf(TEXT("%s（%s）"), *CurrentInfiniteBeat.Choices[Index].Text, *DisabledReason);
			static const TCHAR* ChoiceMarks[] = {TEXT("壹"), TEXT("贰"), TEXT("叁"), TEXT("肆")};
			const FString ChoiceLabel = FString::Printf(TEXT("%s　%s"),
				Index < UE_ARRAY_COUNT(ChoiceMarks) ? ChoiceMarks[Index] : *FString::FromInt(Index + 1), *ChoiceText);
			UHorizontalBox* Row = NewObject<UHorizontalBox>(Box);
			Row->AddChildToHorizontalBox(NewObject<USpacer>(Row))->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			UButton* ChoiceButton = MakeLinkedButton(Row, ChoiceLabel, TEXT("rp_choice"), Index, 18);
			UTextBlock* ChoiceLabelText = nullptr;
			RPConfigureAdaptiveChoiceButton(ChoiceButton, ChoiceLabel, 18, SafeWidth, &ChoiceLabelText);
			RPInfiniteChoiceButtons.Add(ChoiceButton);
			RPOpeningChoiceLabels.Add(ChoiceLabelText);
			ChoiceButton->SetIsEnabled(bAvailable);
			if (!FirstChoiceButton && bAvailable) FirstChoiceButton = ChoiceButton;
			RPAddToHBox(Row, ChoiceButton, FMargin(8.f, 6.f));
			Row->AddChildToHorizontalBox(NewObject<USpacer>(Row))->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			RPAddToVBox(Box, Row);
		}

		if (bFreeRPForcedJumpPending && bFreeRPForcedJumpAwaitingContinue)
		{
			RPAddPad(Box, 18.f);
			RPAddToVBox(Box, RPMakeWrappedText(Box,
				TEXT("本轮自由 RP 的回应已生成。读完后点击继续，才会沿引擎锁定的方向自动衔接。"),
				15, FAscendUIStyle::DimGray(), ETextJustify::Center), FMargin(80.f, 4.f));
			UButton* ContinueButton = MakeLinkedButton(Box, TEXT("【继续自动衔接】"),
				TEXT("rp_free_rp_continue"), 0, 21);
			ContinueButton->SetIsEnabled(IsFreeRPForcedContinueAvailableForAutomationTest(
				bFreeRPForcedJumpPending, bFreeRPForcedJumpAwaitingContinue,
				bInfiniteNarrativeRequestInFlight || bInfiniteFreeRPForcedTurnActive));
		}

		if (bInfiniteNarrativeNeedsRefresh)
		{
			RPAddToVBox(Box, MakeLinkedButton(Box, TEXT("【重新推演本幕】"), TEXT("rp_retry"), 0, 19),
				FMargin(120.f, 12.f, 120.f, 4.f));
		}

		RPFreeformInput = nullptr;
		if (IsFreeRPInputAvailableForAutomationTest(InfiniteNarrativeSettings.bShowFreeformInput,
			bFreeRPForcedJumpPending, false,
			CurrentInfiniteBeat.Diagnostic.StartsWith(TEXT("authored_opening:"))
			|| bInfiniteNarrativeNeedsRefresh))
		{
			RPAddPad(Box, 18.f);
			RPFreeformInput = NewObject<UEditableTextBox>(Box);
			RPFreeformInput->SetHintText(FText::FromString(
				TEXT("自由 RP：描述你的行动；回应生成后先阅读，点击继续才沿随机方向衔接……")));
			const float FreeformSideInset = FMath::Clamp(SafeWidth * 0.08f, 8.f, 32.f);
			RPAddToVBox(Box, RPFreeformInput, FMargin(FreeformSideInset, 5.f));
			UHorizontalBox* SubmitRow = NewObject<UHorizontalBox>(Box);
			SubmitRow->AddChildToHorizontalBox(NewObject<USpacer>(SubmitRow))->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			UButton* FreeformButton = MakeLinkedButton(SubmitRow, TEXT("【提交自由 RP】"), TEXT("rp_freeform"), 0, 17);
			FreeformButton->SetIsEnabled(!bInfiniteNarrativeRequestInFlight && !bFreeRPForcedJumpPending);
			RPAddToHBox(SubmitRow, FreeformButton);
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
	const float StatusSideInset = FMath::Clamp(SafeWidth * 0.08f, 8.f, 32.f);
	RPAddToVBox(Box, RPGenerationStatusText, FMargin(StatusSideInset, 18.f));
	RPAddPad(Box, 45.f);
	SetScreen(Page, EGameScreen::InfiniteNarrative);
	if (FirstChoiceButton && GetWorld())
	{
		TWeakObjectPtr<UButton> WeakFirstChoice(FirstChoiceButton);
		GetWorld()->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this,
			[WeakFirstChoice]()
			{
				if (WeakFirstChoice.IsValid()) WeakFirstChoice->SetKeyboardFocus();
			}));
	}
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
	SetScreen(Scroll, EGameScreen::InfiniteNarrative, TEXT("bgm_deckbuilding"));
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
	SetScreen(Scroll, EGameScreen::InfiniteNarrative,
		bUpgrade ? TEXT("bgm_upgrade") : TEXT("bgm_deckbuilding"));
}

void AAscendPlayerController::SelectInfiniteNarrativeChoice(int32 ChoiceIndex)
{
	if (bOpeningChoiceWordingPending || bInfiniteNarrativeNeedsRefresh
		|| bInfiniteNarrativeRequestInFlight || bInfiniteFreeRPForcedTurnActive
		|| (Run && Run->State.bInfiniteFreeRPForcedJumpPending)) return;
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
		PlayAudioEvent(TEXT("forge_start"), 0.72f);
		ShowInfiniteNarrativeLoading(TEXT("正在把本轮所得锻造成一张真正可运行的原创卡……"));
		InfiniteNarrativeService->ForgeCard(InfiniteNarrativeSettings, BuildInfiniteNarrativeContext(), Job,
			FOnInfiniteCardForgeReady::CreateUObject(this,
				&AAscendPlayerController::HandleInfiniteCardForgeReady));
		return;
	}
	PendingInfiniteChoice = CurrentInfiniteBeat.Choices[ChoiceIndex];
	if (PendingInfiniteChoice.SettlementKey.StartsWith(TEXT("authored_opening_relic:"))
		&& PendingInfiniteChoice.Reward.RelicIds.Num() == 1)
	{
		if (const FRelicData* SelectedRelic = Run->GetRelicData(
			PendingInfiniteChoice.Reward.RelicIds[0]))
		{
			// Choice buttons stay identity-free; the selected item is revealed in the
			// existing resolution panel immediately before the fixed opening combat.
			PendingInfiniteChoice.Text = BuildAuthoredOpeningChoiceDisplay(*SelectedRelic,
				PendingInfiniteChoice.Text);
		}
	}
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
		if (!bNewSettlement
			&& PendingInfiniteChoice.SettlementKey.StartsWith(TEXT("authored_opening_relic:")))
		{
			UE_LOG(LogTemp, Display, TEXT("[InfiniteRP] authored opening relic choice already settled: %s"),
				*PendingInfiniteChoice.SettlementKey);
			return;
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
	const int32 EnemyCount = FMath::Clamp(PendingInfiniteChoice.Enemy.Count, 1, 3);
	TArray<FString> EnemyIds;
	EnemyIds.Init(EnemyId, EnemyCount);
	Run->PrepareInfiniteCombat(NodeType, EnemyIds);

	PendingInfiniteEncounter = FNodeEncounter();
	PendingInfiniteEncounter.Type = NodeType;
	PendingInfiniteEncounter.EnemyIds = EnemyIds;
	PendingInfiniteEncounter.EnemyLevel = 0;
	PendingInfiniteEncounter.EnemyHPBonus = Run->GetInfiniteEnemyHPBonus(PendingInfiniteChoice.Enemy.FactionId);
	PendingInfiniteEncounter.StoryText = PendingInfiniteChoice.Enemy.Story;
	if (const FEnemyData* Enemy = Run->GetEnemyData(EnemyId))
	{
		TArray<FEnemyData> PendingEnemies;
		PendingEnemies.Init(*Enemy, EnemyCount);
		Run->SavePendingInfiniteCombat(NodeType, PendingEnemies, PendingInfiniteEncounter.EnemyHPBonus,
			PendingInfiniteChoice.ResultSummary,
			PendingNarrativeCards, PendingNarrativeRelics);
	}
	// The encounter snapshot is now authoritative and is persisted below; the
	// pre-combat opening record must not be eligible for a second grant on reload.
	if (Run->HasPendingInfiniteOpening()) Run->ClearPendingInfiniteOpening();
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
		PlayAudioEvent(TEXT("forge_success"), 0.84f, 0.98f, 1.02f);
		Choice.Reward.CreatedCards.Add(Card);
		FInfiniteRewardCard RewardCard;
		RewardCard.CardId = Card.Id;
		Choice.Reward.Cards.Add(RewardCard);
		Choice.GmJudgement += FString::Printf(TEXT("；独立卡牌工坊已锻成【%s】"), *Card.Name);
	}
	else
	{
		PlayAudioEvent(TEXT("ui_deny"), 0.62f);
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
	if (CurrentInfiniteBeat.Narration.IsEmpty() && CurrentInfiniteBeat.DialogueLines.Num() == 0
		&& CurrentInfiniteBeat.Dialogue.IsEmpty())
	{
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
	LastProcessedEnemyDamageEventIndex = 0;
	if (Combat->StartCombatWithDifficulty(Run->State.Deck, CurrentEncounter.EnemyIds, Run->State.RelicIds,
		Run->State.MaxHP, Run->State.HP, CurrentEncounter.EnemyHPBonus,
		FMath::RandRange(1, 999999), Run->GetLastCombatDifficulty()))
	{
		PendingInfiniteEncounter = FNodeEncounter();
		ShowCombat();
		// 首场战斗固定模式 B；从第二场开始才读取用户的 A/B 设置。
		const bool bFirstInfiniteCombat = Run->State.InfiniteCycle <= 1;
		if (ShouldPrefetchAtCombatStart(bFirstInfiniteCombat,
			InfiniteNarrativeSettings.bGenerateAfterCombatWithLog))
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
