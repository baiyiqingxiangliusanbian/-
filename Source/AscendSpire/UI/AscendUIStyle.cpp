#include "AscendUIStyle.h"
#include "AscendArt.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"
#include "Components/SizeBox.h"
#include "Components/ScaleBox.h"
#include "Engine/Texture2D.h"
#include "Styling/SlateBrush.h"
#include "Styling/SlateTypes.h"
#include "Misc/Paths.h"

UTextBlock* FAscendUIStyle::MakeText(UObject* Outer, const FString& Text, int32 Size, FSlateColor Color)
{
	UTextBlock* TB = NewObject<UTextBlock>(Outer);
	TB->SetText(FText::FromString(Text));
	TB->SetFont(Font(Size));
	TB->SetColorAndOpacity(Color);
	return TB;
}

UButton* FAscendUIStyle::MakeButton(UObject* Outer, const FString& Label, int32 Size)
{
	UButton* Btn = NewObject<UButton>(Outer);
	UTextBlock* TB = MakeText(Btn, Label, Size, PaperWhite());
	Btn->AddChild(TB);
	return Btn;
}

UButton* FAscendUIStyle::MakeStyledButton(UObject* Outer, const FString& Label, int32 Size,
	FSlateColor TextColor, FLinearColor BgOverride)
{
	UButton* Btn = NewObject<UButton>(Outer);
	Btn->SetClipping(EWidgetClipping::ClipToBounds);

	// 旧版用书名号模拟按钮边框。现在已有完整 HUD 框，继续保留只会让文字越出安全区。
	FString DisplayLabel = Label.TrimStartAndEnd();
	if (DisplayLabel.StartsWith(TEXT("【")) && DisplayLabel.EndsWith(TEXT("】")) && DisplayLabel.Len() >= 2)
	{
		DisplayLabel = DisplayLabel.Mid(1, DisplayLabel.Len() - 2).TrimStartAndEnd();
	}

	const FLinearColor Bg = (BgOverride.A > 0.01f) ? BgOverride : BtnNormal();
	UTexture2D* ButtonTexture = FAscendArt::GetTexture(Btn, TEXT("Art/ui/button_frame.png"));

	auto MakeBrush = [ButtonTexture](FLinearColor C)
	{
		FSlateBrush B;
		if (ButtonTexture)
		{
			B.SetResourceObject(ButtonTexture);
			B.DrawAs = ESlateBrushDrawType::Box;
			// 九宫格只拉伸框体中央，左右云纹和四角装饰保持原比例。
			B.Margin = FMargin(0.13f, 0.40f);
			B.ImageSize = FVector2D(1024.f, 128.f);
		}
		B.TintColor = FSlateColor(C);
		return B;
	};

	FButtonStyle BS;
	if (ButtonTexture)
	{
		const FLinearColor Accent = BgOverride.A > 0.01f
			? FLinearColor::LerpUsingHSV(FLinearColor::White, BgOverride, 0.18f)
			: FLinearColor::White;
		BS.SetNormal(MakeBrush(Accent));
		BS.SetHovered(MakeBrush(Accent * 1.10f));
		BS.SetPressed(MakeBrush(Accent * 0.76f));
		BS.SetDisabled(MakeBrush(FLinearColor(0.30f, 0.32f, 0.30f, 0.58f)));
	}
	else
	{
		BS.SetNormal(MakeBrush(Bg));
		BS.SetHovered(MakeBrush(BgOverride.A > 0.01f ? BgOverride * 1.5f : BtnHover()));
		BS.SetPressed(MakeBrush(BtnPressed()));
		BS.SetDisabled(MakeBrush(Bg * 0.45f));
	}
	BS.SetNormalPadding(FMargin(24.f, 4.f));
	BS.SetPressedPadding(FMargin(24.f, 5.f, 24.f, 3.f));
	Btn->SetStyle(BS);

	// 文字永远待在框体安全区内：正常尺寸不放大，空间不足时才自动缩小。
	TArray<FString> Lines;
	DisplayLabel.ParseIntoArray(Lines, TEXT("\n"), false);
	int32 LongestLine = 1;
	for (const FString& Line : Lines) LongestLine = FMath::Max(LongestLine, Line.Len());
	const int32 LineCount = FMath::Max(1, Lines.Num());
	USizeBox* TextSafeBox = NewObject<USizeBox>(Btn);
	TextSafeBox->SetMinDesiredWidth(FMath::Clamp(LongestLine * Size * 1.05f + 84.f, 128.f, 560.f));
	TextSafeBox->SetMinDesiredHeight(FMath::Max(25.f, LineCount * Size * 1.08f));
	UScaleBox* TextScale = NewObject<UScaleBox>(TextSafeBox);
	TextScale->SetStretch(EStretch::ScaleToFit);
	TextScale->SetStretchDirection(EStretchDirection::DownOnly);
	UTextBlock* TB = MakeText(TextScale, DisplayLabel, Size, TextColor);
	TB->SetJustification(ETextJustify::Center);
	TB->SetShadowOffset(FVector2D(1.f, 1.f));
	TB->SetShadowColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.82f));
	TextScale->SetContent(TB);
	TextSafeBox->SetContent(TextScale);
	Btn->AddChild(TextSafeBox);
	return Btn;
}
