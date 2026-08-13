#pragma once

#include "CoreMinimal.h"
#include "Styling/SlateColor.h"
#include "Fonts/SlateFontInfo.h"
#include "Misc/Paths.h"

class UTextBlock;
class UButton;

/** UI 样式工具 —— 修仙水墨风配色 */
class ASCENDSPIRE_API FAscendUIStyle
{
public:
	// 配色
	static const FSlateColor InkBlack()      { return FSlateColor(FLinearColor(0.06f, 0.06f, 0.08f)); }
	static const FSlateColor PaperWhite()    { return FSlateColor(FLinearColor(0.92f, 0.90f, 0.85f)); }
	static const FSlateColor GoldYellow()    { return FSlateColor(FLinearColor(0.85f, 0.68f, 0.25f)); }
	static const FSlateColor JadeGreen()     { return FSlateColor(FLinearColor(0.30f, 0.70f, 0.50f)); }
	static const FSlateColor BloodRed()      { return FSlateColor(FLinearColor(0.75f, 0.22f, 0.18f)); }
	static const FSlateColor SpiritBlue()    { return FSlateColor(FLinearColor(0.35f, 0.60f, 0.90f)); }
	static const FSlateColor DimGray()       { return FSlateColor(FLinearColor(0.45f, 0.45f, 0.48f)); }
	static const FSlateColor PoisonPurple()  { return FSlateColor(FLinearColor(0.60f, 0.35f, 0.75f)); }

	// 按钮主题
	static FLinearColor BtnNormal()   { return FLinearColor(0.16f, 0.13f, 0.10f); }
	static FLinearColor BtnHover()    { return FLinearColor(0.34f, 0.27f, 0.17f); }
	static FLinearColor BtnPressed()  { return FLinearColor(0.10f, 0.08f, 0.06f); }

	/** 卡牌类型主题色（基础=青玉 / 功法=鎏金 / 招式=绯红 / 剑=金 / 术=赤 / 体=青 / 符=紫 / 功=蓝） */
	static FLinearColor CardTypeColor(const FString& Type)
	{
		if (Type == TEXT("basic"))    return FLinearColor(0.40f, 0.62f, 0.55f);
		if (Type == TEXT("gongfa"))   return FLinearColor(0.85f, 0.62f, 0.18f);
		if (Type == TEXT("zhaoshi"))  return FLinearColor(0.78f, 0.25f, 0.20f);
		if (Type == TEXT("sword"))    return FLinearColor(0.75f, 0.55f, 0.20f);
		if (Type == TEXT("spell"))    return FLinearColor(0.70f, 0.22f, 0.18f);
		if (Type == TEXT("body"))     return FLinearColor(0.30f, 0.62f, 0.42f);
		if (Type == TEXT("talisman")) return FLinearColor(0.55f, 0.38f, 0.72f);
		return FLinearColor(0.35f, 0.55f, 0.80f);
	}

	/** 卡牌类型中文名 */
	static FString CardTypeName(const FString& Type)
	{
		if (Type == TEXT("basic"))    return TEXT("基础");
		if (Type == TEXT("gongfa"))   return TEXT("功法");
		if (Type == TEXT("zhaoshi"))  return TEXT("招式");
		if (Type == TEXT("sword"))    return TEXT("剑诀");
		if (Type == TEXT("spell"))    return TEXT("法术");
		if (Type == TEXT("body"))     return TEXT("体术");
		if (Type == TEXT("talisman")) return TEXT("符箓");
		if (Type == TEXT("skill"))    return TEXT("心法");
		return Type;
	}

	/** 稀有度配色 */
	static FLinearColor RarityColor(const FString& Rarity)
	{
		if (Rarity == TEXT("uncommon"))  return FLinearColor(0.25f, 0.70f, 0.40f);
		if (Rarity == TEXT("rare"))      return FLinearColor(0.30f, 0.55f, 0.95f);
		if (Rarity == TEXT("legendary")) return FLinearColor(0.90f, 0.65f, 0.15f);
		return FLinearColor(0.55f, 0.55f, 0.58f);
	}

	/** 中文字体：霞鹜文楷轻便版 Medium，SIL OFL 1.1，可随游戏分发。 */
	static FSlateFontInfo Font(int32 Size)
	{
		static const FString FontPath = FPaths::ProjectContentDir() / TEXT("Fonts/LXGWWenKaiLite-Medium.ttf");
		// 文楷的汉字字面率比旧字体高，统一缩减约 8%，为 HUD 上下边缘留下呼吸空间。
		const int32 VisualSize = FMath::Max(8, FMath::RoundToInt(Size * 0.92f));
		FSlateFontInfo F(*FontPath, VisualSize);
		return F;
	}

	static UTextBlock* MakeText(UObject* Outer, const FString& Text, int32 Size, FSlateColor Color);
	static UButton* MakeButton(UObject* Outer, const FString& Label, int32 Size);

	/** 主题化按钮：暗底 + 悬停高亮 + 内边距 */
	static UButton* MakeStyledButton(UObject* Outer, const FString& Label, int32 Size,
		FSlateColor TextColor, FLinearColor BgOverride = FLinearColor(0.f, 0.f, 0.f, 0.f));
};
