#include "AscendPlayerController.h"

#include "Components/CanvasPanel.h"
#include "Components/HorizontalBox.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/Spacer.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "HAL/PlatformTime.h"
#include "TimerManager.h"

#if !UE_BUILD_SHIPPING

namespace
{
	FInfiniteNarrativeStreamUpdate MakeRPVisualUpdate(EInfiniteNarrativeStreamStage Stage,
		const FString& Title, const FString& Narration, const TArray<FInfiniteDialogueLine>& Dialogue)
	{
		FInfiniteNarrativeStreamUpdate Update;
		Update.Stage = Stage;
		Update.Preview.Title = Title;
		Update.Preview.Narration = Narration;
		Update.Preview.DialogueLines = Dialogue;
		Update.bHasVisibleContent = !Title.IsEmpty() || !Narration.IsEmpty() || Dialogue.Num() > 0;
		return Update;
	}

	FInfiniteDialogueLine MakeRPVisualDialogue(const TCHAR* Speaker, const TCHAR* PortraitId,
		const TCHAR* Expression, const FString& Text)
	{
		FInfiniteDialogueLine Line;
		Line.Speaker = Speaker;
		Line.PortraitId = PortraitId;
		Line.Expression = Expression;
		Line.Text = Text;
		return Line;
	}

	FString BuildRPVisualExpectedDialogueGlyphText(const FString& Source)
	{
		FString Normalized = Source;
		Normalized.ReplaceInline(TEXT("\r\n"), TEXT("\n"));
		Normalized.ReplaceInline(TEXT("\r"), TEXT("\n"));
		FString Expected = TEXT("　　");
		for (int32 Index = 0; Index < Normalized.Len(); ++Index)
		{
			if (Normalized[Index] == TEXT('\n'))
			{
				if (Normalized.IsValidIndex(Index + 1) && Normalized[Index + 1] == TEXT('\n'))
				{
					Expected += TEXT("　　");
					++Index;
				}
				continue;
			}
			Expected.AppendChar(Normalized[Index]);
		}
		return Expected;
	}

}

void AAscendPlayerController::LogRPNarrativeVisualGeometry(int32 Step, const TCHAR* Prefix) const
{
	const double Now = FPlatformTime::Seconds();
	const FVector2D Viewport = GetInfiniteNarrativeLocalViewportSize();
	UE_LOG(LogTemp, Display, TEXT("[RPVisualTest] %s step=%d local_viewport=%.0fx%.0f text_width=%.1f frame_height=%.1f portrait_size=%.1f finalized=%s"),
		Prefix ? Prefix : TEXT("geometry"), Step, Viewport.X, Viewport.Y, RPStreamingTextWidth,
		RPStreamingFrameHeight, RPStreamingPortraitSize, bRPStreamingSceneFinalized ? TEXT("yes") : TEXT("no"));
	if (RPScrollBox)
	{
		const FGeometry ScrollGeometry = RPScrollBox->GetCachedGeometry();
		UE_LOG(LogTemp, Display, TEXT("[RPVisualTest] scroll_rect x=%.1f y=%.1f w=%.1f h=%.1f offset=%.1f end=%.1f"),
			ScrollGeometry.GetAbsolutePosition().X, ScrollGeometry.GetAbsolutePosition().Y,
			ScrollGeometry.GetLocalSize().X, ScrollGeometry.GetLocalSize().Y,
			RPScrollBox->GetScrollOffset(), RPScrollBox->GetScrollOffsetOfEnd());
		// The vertical artwork is a sibling of ScrollSize in the canvas layer. Read
		// its post-arrangement geometry, rather than only logging the requested
		// constants, so the development fixture proves that Slate did not clamp the
		// 3:4 decoration back to the screen viewport.
		if (UWidget* ScrollSize = RPScrollBox->GetParent())
		{
			if (UCanvasPanel* Layers = Cast<UCanvasPanel>(ScrollSize->GetParent()))
			{
				if (Layers->GetChildrenCount() > 0)
				{
					if (UWidget* Artwork = Layers->GetChildAt(0))
					{
						const FGeometry ArtworkGeometry = Artwork->GetCachedGeometry();
						const float ExpectedArtworkWidth = FMath::Max(306.f,
							(RPStreamingTextWidth + 56.f) / ((930.f - 155.f) / 1086.f));
						const float ExpectedArtworkHeight = ExpectedArtworkWidth / (3.f / 4.f);
						const FVector2D ArtworkSize = ArtworkGeometry.GetLocalSize();
						const bool bArtworkSizeMatches = FMath::IsNearlyEqual(ArtworkSize.X,
							ExpectedArtworkWidth, 1.f)
							&& FMath::IsNearlyEqual(ArtworkSize.Y, ExpectedArtworkHeight, 1.f);
						UE_LOG(LogTemp, Display, TEXT("[RPVisualTest] artwork_rect x=%.1f y=%.1f w=%.1f h=%.1f expected_w=%.1f expected_h=%.1f size_matches=%s"),
							ArtworkGeometry.GetAbsolutePosition().X, ArtworkGeometry.GetAbsolutePosition().Y,
							ArtworkSize.X, ArtworkSize.Y, ExpectedArtworkWidth, ExpectedArtworkHeight,
							bArtworkSizeMatches ? TEXT("yes") : TEXT("no"));
					}
				}
			}
		}
	}
	int32 FullyVisibleGlyphs = 0;
	UTextBlock* LastGlyph = nullptr;
	for (UTextBlock* Glyph : RPStreamingRevealGlyphs)
	{
		if (!Glyph) continue;
		LastGlyph = Glyph;
		if (Glyph->GetRenderOpacity() >= 0.999f) ++FullyVisibleGlyphs;
	}
	const bool bAlphaComplete = RPStreamingRevealCompletedAt > 0.0
		&& FullyVisibleGlyphs == RPStreamingRevealGlyphs.Num();
	UE_LOG(LogTemp, Display, TEXT("[RPVisualTest] glyph_audit count=%d fully_visible=%d alpha_complete=%s alpha_complete_at=%.3f alpha_age=%.3f expected_tail_visible=%s"),
		RPStreamingRevealGlyphs.Num(), FullyVisibleGlyphs, bAlphaComplete ? TEXT("yes") : TEXT("no"),
		RPStreamingRevealCompletedAt, RPStreamingRevealCompletedAt > 0.0
			? FMath::Max(0.0, Now - RPStreamingRevealCompletedAt) : -1.0,
		(!RPVisualExpectedTail.IsEmpty() && RPStreamingNarrationSource.EndsWith(RPVisualExpectedTail)
			&& RPStreamingNarrationLines.Num() > 0 && bAlphaComplete)
			? TEXT("yes") : RPVisualExpectedTail.IsEmpty() ? TEXT("n/a") : TEXT("no"));
	if (RPStreamingRevealGlyphs.Num() > 0 && RPStreamingRevealGlyphs[0])
	{
		const FGeometry GlyphGeometry = RPStreamingRevealGlyphs[0]->GetCachedGeometry();
		UE_LOG(LogTemp, Display, TEXT("[RPVisualTest] first_glyph_rect x=%.1f y=%.1f w=%.1f h=%.1f opacity=%.3f"),
			GlyphGeometry.GetAbsolutePosition().X, GlyphGeometry.GetAbsolutePosition().Y,
			GlyphGeometry.GetLocalSize().X, GlyphGeometry.GetLocalSize().Y,
			RPStreamingRevealGlyphs[0]->GetRenderOpacity());
	}
	if (LastGlyph)
	{
		const FGeometry GlyphGeometry = LastGlyph->GetCachedGeometry();
		UE_LOG(LogTemp, Display, TEXT("[RPVisualTest] last_glyph_rect x=%.1f y=%.1f w=%.1f h=%.1f opacity=%.3f"),
			GlyphGeometry.GetAbsolutePosition().X, GlyphGeometry.GetAbsolutePosition().Y,
			GlyphGeometry.GetLocalSize().X, GlyphGeometry.GetLocalSize().Y,
			LastGlyph->GetRenderOpacity());
	}
	if (RPStreamingNarrationGlyphBox)
	{
		const int32 NarrationLines = RPStreamingNarrationLines.Num();
		const int32 NarrationGaps = FMath::Max(0,
			RPStreamingNarrationGlyphBox->GetChildrenCount() - NarrationLines);
		// Fixture expectation mirrors the production Font(19) + 1.6 line rhythm;
		// the production audit remains authoritative for actual Slate geometry.
		const float NarrationLineHeight = 27.f * 1.6f;
		const float NarrationExpectedHeight = FMath::Max(NarrationLineHeight,
			NarrationLines * NarrationLineHeight + NarrationGaps * NarrationLineHeight * 0.55f);
		const FGeometry NarrationGeometry = RPStreamingNarrationGlyphBox->GetCachedGeometry();
		UE_LOG(LogTemp, Display, TEXT("[RPVisualTest] narration_audit lines=%d gaps=%d expected_body_h=%.1f body_rect x=%.1f y=%.1f w=%.1f h=%.1f"),
			NarrationLines, NarrationGaps, NarrationExpectedHeight,
			NarrationGeometry.GetAbsolutePosition().X, NarrationGeometry.GetAbsolutePosition().Y,
			NarrationGeometry.GetLocalSize().X, NarrationGeometry.GetLocalSize().Y);
	}
	auto LastGlyphInLines = [](const TArray<UHorizontalBox*>& Lines) -> UTextBlock*
	{
		for (int32 LineIndex = Lines.Num() - 1; LineIndex >= 0; --LineIndex)
		{
			UHorizontalBox* Line = Lines[LineIndex];
			if (!Line) continue;
			for (int32 ChildIndex = Line->GetChildrenCount() - 1; ChildIndex >= 0; --ChildIndex)
			{
				if (USizeBox* GlyphSize = Cast<USizeBox>(Line->GetChildAt(ChildIndex)))
					if (UTextBlock* Glyph = Cast<UTextBlock>(GlyphSize->GetContent())) return Glyph;
			}
		}
		return nullptr;
	};
	for (int32 RowIndex = 0; RowIndex < RPStreamingDialogueGlyphBoxes.Num(); ++RowIndex)
	{
		UVerticalBox* BodyBox = RPStreamingDialogueGlyphBoxes[RowIndex];
		USizeBox* BubbleSize = RPStreamingDialogueBubbleSizes.IsValidIndex(RowIndex)
			? RPStreamingDialogueBubbleSizes[RowIndex] : nullptr;
		if (!BodyBox || !BubbleSize) continue;
		const int32 LineCount = RPStreamingDialogueLines.IsValidIndex(RowIndex)
			? RPStreamingDialogueLines[RowIndex].Num() : 0;
		const int32 GapCount = FMath::Max(0, BodyBox->GetChildrenCount() - LineCount);
		const float LineHeight = 28.f * 1.6f;
		const float ExpectedBodyHeight = FMath::Max(LineHeight,
			LineCount * LineHeight + GapCount * LineHeight * 0.5f);
		const FGeometry BodyGeometry = BodyBox->GetCachedGeometry();
		const FGeometry BubbleSizeGeometry = BubbleSize->GetCachedGeometry();
		const UWidget* BubbleWidget = BubbleSize->GetParent();
		const FGeometry BubbleGeometry = BubbleWidget
			? BubbleWidget->GetCachedGeometry() : BubbleSizeGeometry;
		int32 RowGlyphCount = 0;
		int32 RowOutOfBoundsGlyphs = 0;
		FString LaidOutText;
		if (RPStreamingDialogueLines.IsValidIndex(RowIndex))
		{
			for (UHorizontalBox* LineWidget : RPStreamingDialogueLines[RowIndex])
			{
				if (!LineWidget) continue;
				for (int32 ChildIndex = 0; ChildIndex < LineWidget->GetChildrenCount(); ++ChildIndex)
				{
					USizeBox* GlyphSize = Cast<USizeBox>(LineWidget->GetChildAt(ChildIndex));
					UTextBlock* Glyph = GlyphSize ? Cast<UTextBlock>(GlyphSize->GetContent()) : nullptr;
					if (!Glyph) continue;
					++RowGlyphCount;
					LaidOutText += Glyph->GetText().ToString();
					const FGeometry GlyphGeometry = Glyph->GetCachedGeometry();
					const FVector2D GlyphTopLeft = GlyphGeometry.LocalToAbsolute(FVector2D::ZeroVector);
					const FVector2D GlyphBottomRight = GlyphGeometry.LocalToAbsolute(GlyphGeometry.GetLocalSize());
					const FVector2D BodyTopLeft = BodyGeometry.AbsoluteToLocal(GlyphTopLeft);
					const FVector2D BodyBottomRight = BodyGeometry.AbsoluteToLocal(GlyphBottomRight);
					const bool bInsideBody = BodyTopLeft.X >= -0.5f && BodyTopLeft.Y >= -0.5f
						&& BodyBottomRight.X <= BodyGeometry.GetLocalSize().X + 0.5f
						&& BodyBottomRight.Y <= BodyGeometry.GetLocalSize().Y + 0.5f;
					if (!bInsideBody) ++RowOutOfBoundsGlyphs;
				}
			}
		}
		const FString ExpectedGlyphText = RPStreamingDialogueSources.IsValidIndex(RowIndex)
			? BuildRPVisualExpectedDialogueGlyphText(RPStreamingDialogueSources[RowIndex]) : FString();
		const bool bGlyphTextMatches = LaidOutText == ExpectedGlyphText;
		UTextBlock* RowLastGlyph = RPStreamingDialogueLines.IsValidIndex(RowIndex)
			? LastGlyphInLines(RPStreamingDialogueLines[RowIndex]) : nullptr;
		bool bGlyphInsideBody = false;
		bool bGlyphInsideBubble = false;
		float LastOpacity = -1.f;
		if (RowLastGlyph)
		{
			const FGeometry GlyphGeometry = RowLastGlyph->GetCachedGeometry();
			const FVector2D GlyphTopLeft = GlyphGeometry.LocalToAbsolute(FVector2D::ZeroVector);
			const FVector2D GlyphBottomRight = GlyphGeometry.LocalToAbsolute(GlyphGeometry.GetLocalSize());
			const FVector2D BodyTopLeft = BodyGeometry.AbsoluteToLocal(GlyphTopLeft);
			const FVector2D BodyBottomRight = BodyGeometry.AbsoluteToLocal(GlyphBottomRight);
			const FVector2D BubbleTopLeft = BubbleSizeGeometry.AbsoluteToLocal(GlyphTopLeft);
			const FVector2D BubbleBottomRight = BubbleSizeGeometry.AbsoluteToLocal(GlyphBottomRight);
			bGlyphInsideBody = BodyTopLeft.X >= -0.5f && BodyTopLeft.Y >= -0.5f
				&& BodyBottomRight.X <= BodyGeometry.GetLocalSize().X + 0.5f
				&& BodyBottomRight.Y <= BodyGeometry.GetLocalSize().Y + 0.5f;
			bGlyphInsideBubble = BubbleTopLeft.X >= -0.5f && BubbleTopLeft.Y >= -0.5f
				&& BubbleBottomRight.X <= BubbleSizeGeometry.GetLocalSize().X + 0.5f
				&& BubbleBottomRight.Y <= BubbleSizeGeometry.GetLocalSize().Y + 0.5f;
			LastOpacity = RowLastGlyph->GetRenderOpacity();
		}
		const float RecordedBodyTextWidth = RPStreamingDialogueTextWidths.IsValidIndex(RowIndex)
			? RPStreamingDialogueTextWidths[RowIndex] : -1.f;
		UE_LOG(LogTemp, Display, TEXT("[RPVisualTest] dialogue_audit row=%d lines=%d gaps=%d expected_body_h=%.1f body_rect x=%.1f y=%.1f w=%.1f h=%.1f bubble_rect x=%.1f y=%.1f w=%.1f h=%.1f inner_rect x=%.1f y=%.1f w=%.1f h=%.1f wrap_width=%.1f glyphs=%d out_of_bounds=%d source_units=%d laid_units=%d text_match=%s last_opacity=%.3f inside_body=%s inside_bubble=%s"),
			RowIndex, LineCount, GapCount, ExpectedBodyHeight,
			BodyGeometry.GetAbsolutePosition().X, BodyGeometry.GetAbsolutePosition().Y,
			BodyGeometry.GetLocalSize().X, BodyGeometry.GetLocalSize().Y,
			BubbleGeometry.GetAbsolutePosition().X, BubbleGeometry.GetAbsolutePosition().Y,
			BubbleGeometry.GetLocalSize().X, BubbleGeometry.GetLocalSize().Y,
			BubbleSizeGeometry.GetAbsolutePosition().X, BubbleSizeGeometry.GetAbsolutePosition().Y,
			BubbleSizeGeometry.GetLocalSize().X, BubbleSizeGeometry.GetLocalSize().Y,
			RecordedBodyTextWidth, RowGlyphCount, RowOutOfBoundsGlyphs,
			ExpectedGlyphText.Len(), LaidOutText.Len(), bGlyphTextMatches ? TEXT("yes") : TEXT("no"),
			LastOpacity, bGlyphInsideBody ? TEXT("yes") : TEXT("no"),
			bGlyphInsideBubble ? TEXT("yes") : TEXT("no"));
	}
	if (RPStreamingPortraitWidgets.Num() > 0 && RPStreamingPortraitWidgets[0])
	{
		const FGeometry PortraitGeometry = RPStreamingPortraitWidgets[0]->GetCachedGeometry();
		UE_LOG(LogTemp, Display, TEXT("[RPVisualTest] first_portrait_rect x=%.1f y=%.1f w=%.1f h=%.1f"),
			PortraitGeometry.GetAbsolutePosition().X, PortraitGeometry.GetAbsolutePosition().Y,
			PortraitGeometry.GetLocalSize().X, PortraitGeometry.GetLocalSize().Y);
	}
}

void AAscendPlayerController::StartRPNarrativeVisualTest()
{
	if (!GetWorld() || !RootWidget || !ScreenHost) return;
	// This route intentionally does not call StartNewRun, touch Run->State, invoke the
	// narrative service, or write settings. It feeds fixed snapshots into the same RP
	// renderer used by a real streamed response, so a packaged Development process can
	// be inspected without network access or a user-save side effect.
	bInfiniteNarrativeRequestInFlight = false;
	bInputLocked = false;
	InfiniteNarrativeSettings.bShowSpeakerPortrait = true;
	ResetInfiniteNarrativeStreamPreview();
	RPVisualExpectedTail = TEXT("收束完成。");

	const FString FullNarration =
		TEXT("第一段先给出足够长的固定旁白，用来检查窄窗口的行宽裁剪、逐字追加和自然段首行缩进。山路从窗下绕过旧渡口，雨水沿着瓦脊一寸一寸落进空坛，远处的灯火没有因为故事正在生成就改变位置。")
		TEXT("你能看见每一行都在同一条安全阅读框内换行，已经显示的字不会因为后续内容到来而重新排版。")
		TEXT("\n\n")
		TEXT("第二段继续加入中英 mixed text、数字 1254 和标点，故意让这一段跨过多次网络 chunk。它描述一张被水汽浸软的路引，以及门外逐渐靠近的脚步；内容只用于视觉验收，不会推进任何游戏状态。")
		TEXT("在宽窗口中，段落仍保留相同的两格中文缩进；在窄窗口中，超出部分必须留在滚动区域内。")
		TEXT("\n\n")
		TEXT("第三段作为结尾，验证 soft fade 会按时间推进到 1，而不是跳过中间 alpha。最后一句落下后，滚动位置只在用户仍接近底部时跟随；用户手动上滚后，后续快照不会把视线抢回末尾。收束完成。" );

	const FString FullQingheDialogue =
		TEXT("他说：‘第一句对白也遵循自然段规范，首行应该出现两个中文缩进空格。角色的头像和气泡外框在后续 chunk 到来时保持原有矩形")
		TEXT("。’这句连续句末标点故意跨 chunk 到达，句号和闭合引号都必须保留。")
		TEXT("\n\n")
		TEXT("第二段对白故意较长，检查气泡内部的固定宽度与裁剪，不允许出现漏字、重字或 portrait rect 漂移。这个固定展示不会发起请求。");
	const FString FullWenDialogue =
		TEXT("另一位角色在第二个时间点加入，第一段仍从两格中文缩进开始。她只描述眼前可见的火光和脚步，不改变任何敌人、法器或剧情事实。")
		TEXT("\n\n")
		TEXT("这条消息的第二自然段用于验证多行气泡在逐字 alpha 过程中保持坐标稳定。" );
	const FString FullGrandmotherDialogue = TEXT("瞎婆婆把竹杖轻轻点在地面：看清楚，文字可以慢慢亮起，位置却不该跟着晃动。\n\n最后一段在宽窄窗口都应完整留在可滚动区域。");

	TArray<FInfiniteNarrativeStreamUpdate> Updates;
	Updates.Add(MakeRPVisualUpdate(EInfiniteNarrativeStreamStage::Thinking,
		TEXT("RP 视觉验收 · 固定文本"), FullNarration.Left(82), {}));
	Updates.Add(MakeRPVisualUpdate(EInfiniteNarrativeStreamStage::Writing,
		TEXT("RP 视觉验收 · 固定文本"), FullNarration.Left(220), {
			MakeRPVisualDialogue(TEXT("陆青禾"), TEXT("opening_lu_qinghe"), TEXT("worried"), FullQingheDialogue.Left(FullQingheDialogue.Find(TEXT("。’"))))
		}));
	Updates.Add(MakeRPVisualUpdate(EInfiniteNarrativeStreamStage::Writing,
		TEXT("RP 视觉验收 · 固定文本"), FullNarration.Left(430), {
			MakeRPVisualDialogue(TEXT("陆青禾"), TEXT("opening_lu_qinghe"), TEXT("worried"), FullQingheDialogue),
			MakeRPVisualDialogue(TEXT("闻雁回"), TEXT("opening_wen_yanhui"), TEXT("neutral"), FullWenDialogue.Left(70))
		}));
	Updates.Add(MakeRPVisualUpdate(EInfiniteNarrativeStreamStage::Compiling,
		TEXT("RP 视觉验收 · 固定文本"), FullNarration, {
			MakeRPVisualDialogue(TEXT("陆青禾"), TEXT("opening_lu_qinghe"), TEXT("worried"), FullQingheDialogue),
			MakeRPVisualDialogue(TEXT("闻雁回"), TEXT("opening_wen_yanhui"), TEXT("neutral"), FullWenDialogue),
			MakeRPVisualDialogue(TEXT("瞎婆婆"), TEXT("opening_blind_grandmother"), TEXT("stern"), FullGrandmotherDialogue)
		}));

	for (int32 Index = 1; Index < Updates.Num(); ++Index)
	{
		if (!Updates[Index].Preview.Narration.StartsWith(Updates[Index - 1].Preview.Narration))
			UE_LOG(LogTemp, Error, TEXT("[RPVisualTest] fixture narration is not append-only at step=%d"), Index);
		for (int32 LineIndex = 0; LineIndex < Updates[Index - 1].Preview.DialogueLines.Num(); ++LineIndex)
		{
			if (!Updates[Index].Preview.DialogueLines.IsValidIndex(LineIndex)
				|| !Updates[Index].Preview.DialogueLines[LineIndex].Text.StartsWith(
					Updates[Index - 1].Preview.DialogueLines[LineIndex].Text))
				UE_LOG(LogTemp, Error, TEXT("[RPVisualTest] fixture dialogue is not append-only at step=%d line=%d"), Index, LineIndex);
		}
	}

	const TSharedPtr<TArray<FInfiniteNarrativeStreamUpdate>> SharedUpdates =
		MakeShared<TArray<FInfiniteNarrativeStreamUpdate>>(MoveTemp(Updates));
	const TSharedPtr<int32> NextStep = MakeShared<int32>(0);
	const TSharedPtr<FTimerHandle> Timer = MakeShared<FTimerHandle>();
	GetWorld()->GetTimerManager().SetTimer(*Timer,
		FTimerDelegate::CreateWeakLambda(this, [this, SharedUpdates, NextStep, Timer]()
		{
			if (!GetWorld() || !SharedUpdates->IsValidIndex(*NextStep))
			{
				if (GetWorld()) GetWorld()->GetTimerManager().ClearTimer(*Timer);
				return;
			}
			const int32 Step = (*NextStep)++;
			RenderInfiniteNarrativeStreamPreview((*SharedUpdates)[Step]);
			UE_LOG(LogTemp, Display, TEXT("[RPVisualTest] rendered fixed snapshot step=%d/%d; no HTTP/no save; update_narration=%d update_dialogue=%d host=%s narration_source=%d narration_lines=%d reveal_glyphs=%d dialogue_boxes=%d"),
				Step + 1, SharedUpdates->Num(), (*SharedUpdates)[Step].Preview.Narration.Len(),
				(*SharedUpdates)[Step].Preview.DialogueLines.Num(),
				RPStreamingNarrationGlyphBox ? TEXT("yes") : TEXT("no"), RPStreamingNarrationSource.Len(),
				RPStreamingNarrationLines.Num(), RPStreamingRevealGlyphs.Num(),
				RPStreamingDialogueGlyphBoxes.Num());
			if (GetWorld())
			{
				GetWorld()->GetTimerManager().SetTimerForNextTick(
					FTimerDelegate::CreateWeakLambda(this, [this, Step]()
					{
						LogRPNarrativeVisualGeometry(Step, TEXT("cached_geometry"));
					}));
			}
			if (Step + 1 >= SharedUpdates->Num())
			{
				// Exercise the production stream -> final handoff without Run/Save/HTTP:
				// retain the streamed glyph and portrait tree, then append a fixed choice
				// footer exactly as HandleInfiniteNarrativeReady would.
				FInfiniteNarrativeBeat FinalBeat = (*SharedUpdates)[Step].Preview;
				for (int32 ChoiceIndex = 0; ChoiceIndex < 3; ++ChoiceIndex)
				{
					FInfiniteNarrativeChoice Choice;
					Choice.Text = FString::Printf(TEXT("固定验收选项 %d：继续观察眼前局势"), ChoiceIndex + 1);
					FinalBeat.Choices.Add(MoveTemp(Choice));
				}
				CurrentInfiniteBeat = MoveTemp(FinalBeat);
				bInfiniteChoiceResolved = false;
				bOpeningChoiceWordingPending = false;
				if (GetWorld())
				{
					GetWorld()->GetTimerManager().SetTimerForNextTick(
						FTimerDelegate::CreateWeakLambda(this, [this, Step]()
						{
							FinalizeInfiniteNarrativeStream();
							UE_LOG(LogTemp, Display, TEXT("[RPVisualTest] final snapshot finalized in-place; glyph_tree_preserved=yes; no HTTP/no save; manually scroll now to verify no auto-scroll theft"));
							LogRPNarrativeVisualGeometry(Step, TEXT("finalized_geometry"));
							FTimerHandle SettledAuditTimer;
							GetWorld()->GetTimerManager().SetTimer(SettledAuditTimer,
								FTimerDelegate::CreateWeakLambda(this, [this, Step]()
								{
									UE_LOG(LogTemp, Display, TEXT("[RPVisualTest] settled_audit after_fade_wait=1.00s"));
									LogRPNarrativeVisualGeometry(Step, TEXT("settled_geometry"));
								}), 1.0f, false);
						}));
					// Clearing the repeating timer from inside its own delegate can destroy
					// the active closure before it returns. Defer that cleanup one tick.
					const TSharedPtr<FTimerHandle> TimerToClear = Timer;
					GetWorld()->GetTimerManager().SetTimerForNextTick(
						FTimerDelegate::CreateWeakLambda(this, [this, TimerToClear]()
						{
							if (GetWorld()) GetWorld()->GetTimerManager().ClearTimer(*TimerToClear);
						}));
				}
				return;
			}
		}), 1.85f, true, 0.35f);
	UE_LOG(LogTemp, Display, TEXT("[RPVisualTest] start fixed RP stream; local_viewport=%.0fx%.0f; run/service/save untouched"),
		GetInfiniteNarrativeLocalViewportSize().X, GetInfiniteNarrativeLocalViewportSize().Y);
}

#else

void AAscendPlayerController::StartRPNarrativeVisualTest()
{
}

void AAscendPlayerController::LogRPNarrativeVisualGeometry(int32 Step, const TCHAR* Prefix) const
{
}

#endif
