#include "AscendUIStyle.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"
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

	const FLinearColor Bg = (BgOverride.A > 0.01f) ? BgOverride : BtnNormal();

	auto MakeBrush = [](FLinearColor C)
	{
		FSlateBrush B;
		B.TintColor = FSlateColor(C);
		return B;
	};

	FButtonStyle BS;
	BS.SetNormal(MakeBrush(Bg));
	BS.SetHovered(MakeBrush(BgOverride.A > 0.01f ? BgOverride * 1.5f : BtnHover()));
	BS.SetPressed(MakeBrush(BtnPressed()));
	BS.SetDisabled(MakeBrush(Bg * 0.45f));
	BS.SetNormalPadding(FMargin(10.f, 6.f));
	BS.SetPressedPadding(FMargin(10.f, 7.f, 10.f, 5.f));
	Btn->SetStyle(BS);

	UTextBlock* TB = MakeText(Btn, Label, Size, TextColor);
	TB->SetJustification(ETextJustify::Center);
	Btn->AddChild(TB);
	return Btn;
}
