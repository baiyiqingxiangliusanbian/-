#include "CombatSlashWidget.h"

#include "Rendering/DrawElements.h"

int32 UCombatSlashWidget::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
	int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const int32 BaseLayer = Super::NativePaint(Args, AllottedGeometry, MyCullingRect,
		OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
	const float Reveal = FMath::Clamp(Progress, 0.f, 1.f);
	if (Reveal <= 0.f) return BaseLayer;

	const FLinearColor Tint = InWidgetStyle.GetColorAndOpacityTint() * SlashColor;
	const FVector2D Center(130.f, 106.f);
	const float Angle = FMath::DegreesToRadians(Rotation);
	const FVector2D Axis(FMath::Cos(Angle), FMath::Sin(Angle));

	auto RotatePoint = [&](const FVector2D& Point)
	{
		const FVector2D Local = Point - Center;
		return Center + FVector2D(Local.X * Axis.X - Local.Y * Axis.Y,
			Local.X * Axis.Y + Local.Y * Axis.X);
	};

	auto MakeCurve = [&](float Offset, float Bend, TArray<FVector2D>& OutPoints)
	{
		const int32 Segments = 24;
		const int32 VisibleSegments = FMath::Max(2, FMath::RoundToInt(Segments * Reveal));
		OutPoints.Reset(VisibleSegments + 1);
		for (int32 Index = 0; Index <= VisibleSegments; ++Index)
		{
			const float T = static_cast<float>(Index) / Segments;
			const float X = 22.f + 216.f * T;
			const float Y = 154.f - 102.f * FMath::Sin(T * PI) + Offset + Bend * (T - 0.5f);
			OutPoints.Add(RotatePoint(FVector2D(X, Y)));
		}
	};

	TArray<FVector2D> Curve;
	MakeCurve(0.f, 10.f, Curve);
	TArray<FVector2D> GlowCurve;
	MakeCurve(-3.f, 8.f, GlowCurve);

	FLinearColor Glow = Tint;
	Glow.A *= 0.18f * (1.f - 0.25f * Reveal);
	FSlateDrawElement::MakeLines(OutDrawElements, BaseLayer + 1,
		AllottedGeometry.ToPaintGeometry(), GlowCurve, ESlateDrawEffect::None, Glow, true, 24.f);

	FLinearColor Mid = Tint;
	Mid.A *= 0.42f;
	FSlateDrawElement::MakeLines(OutDrawElements, BaseLayer + 2,
		AllottedGeometry.ToPaintGeometry(), Curve, ESlateDrawEffect::None, Mid, true, 11.f);

	FLinearColor Core(0.96f, 1.f, 1.f, Tint.A * (1.f - 0.15f * Reveal));
	FSlateDrawElement::MakeLines(OutDrawElements, BaseLayer + 3,
		AllottedGeometry.ToPaintGeometry(), Curve, ESlateDrawEffect::None, Core, true, 3.2f);

	// 刀尖火星：短小的放射线比纯矩形更有“刃口擦过”的感觉。
	const FVector2D Tip = Curve.Last();
	const float SparkAlpha = 1.f - Reveal * 0.65f;
	for (int32 SparkIndex = 0; SparkIndex < 6; ++SparkIndex)
	{
		const float SparkAngle = FMath::DegreesToRadians(SparkIndex * 60.f + 12.f);
		const FVector2D SparkDir(FMath::Cos(SparkAngle), FMath::Sin(SparkAngle));
		TArray<FVector2D> Spark;
		Spark.Add(Tip + SparkDir * 5.f);
		Spark.Add(Tip + SparkDir * (12.f + 14.f * (1.f - Reveal)));
		FLinearColor SparkColor(1.f, 0.95f, 0.72f, Tint.A * SparkAlpha);
		FSlateDrawElement::MakeLines(OutDrawElements, BaseLayer + 4,
			AllottedGeometry.ToPaintGeometry(), Spark, ESlateDrawEffect::None, SparkColor, true, 2.2f);
	}

	return BaseLayer + 4;
}
