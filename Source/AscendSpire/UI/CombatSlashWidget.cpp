#include "CombatSlashWidget.h"

#include "Rendering/DrawElements.h"

namespace
{
	float EaseOutCubic(float T)
	{
		T = FMath::Clamp(T, 0.f, 1.f);
		return 1.f - FMath::Pow(1.f - T, 3.f);
	}

	float EaseInCubic(float T)
	{
		T = FMath::Clamp(T, 0.f, 1.f);
		return T * T * T;
	}

	FLinearColor WithAlpha(FLinearColor Color, float Alpha)
	{
		Color.A *= FMath::Clamp(Alpha, 0.f, 1.f);
		return Color;
	}

	FVector2D RotateVector(const FVector2D& V, float Degrees)
	{
		const float Angle = FMath::DegreesToRadians(Degrees);
		const float C = FMath::Cos(Angle);
		const float S = FMath::Sin(Angle);
		return FVector2D(V.X * C - V.Y * S, V.X * S + V.Y * C);
	}

	void DrawSegment(FSlateWindowElementList& OutDrawElements, int32 Layer,
		const FGeometry& Geometry, const FVector2D& A, const FVector2D& B,
		const FLinearColor& Color, float Thickness)
	{
		TArray<FVector2D> Points;
		Points.Reserve(2);
		Points.Add(A);
		Points.Add(B);
		FSlateDrawElement::MakeLines(OutDrawElements, Layer, Geometry.ToPaintGeometry(),
			Points, ESlateDrawEffect::None, Color, true, FMath::Max(0.5f, Thickness));
	}

	void DrawTaperedPath(FSlateWindowElementList& OutDrawElements, int32 Layer,
		const FGeometry& Geometry, const TArray<FVector2D>& Points,
		const FLinearColor& Color, float StartWidth, float EndWidth)
	{
		if (Points.Num() < 2) return;
		// Drawing every segment as an independent Slate line leaves visible round
		// caps at every joint (a ribbed "metal hose" look). Three overlapping
		// continuous polylines retain a readable taper without exposing those caps.
		constexpr int32 Bands = 3;
		for (int32 Band = 0; Band < Bands; ++Band)
		{
			const int32 First = FMath::Max(0, FMath::FloorToInt((Points.Num() - 1) * Band / static_cast<float>(Bands)) - 1);
			const int32 Last = FMath::Min(Points.Num() - 1,
				FMath::CeilToInt((Points.Num() - 1) * (Band + 1) / static_cast<float>(Bands)) + 1);
			TArray<FVector2D> BandPoints;
			for (int32 Index = First; Index <= Last; ++Index) BandPoints.Add(Points[Index]);
			const float T = (Band + 0.5f) / Bands;
			FSlateDrawElement::MakeLines(OutDrawElements, Layer, Geometry.ToPaintGeometry(),
				BandPoints, ESlateDrawEffect::None, Color, true,
				FMath::Max(0.5f, FMath::Lerp(StartWidth, EndWidth, T)));
		}
	}

	void DrawEllipse(FSlateWindowElementList& OutDrawElements, int32 Layer,
		const FGeometry& Geometry, const FVector2D& Center, const FVector2D& Radius,
		const FLinearColor& Color, float Thickness, float VisibleFraction = 1.f)
	{
		constexpr int32 Segments = 40;
		const int32 VisibleSegments = FMath::Clamp(FMath::CeilToInt(Segments * VisibleFraction), 2, Segments);
		TArray<FVector2D> Points;
		Points.Reserve(VisibleSegments + 1);
		for (int32 Index = 0; Index <= VisibleSegments; ++Index)
		{
			const float Angle = 2.f * PI * static_cast<float>(Index) / Segments;
			Points.Add(Center + FVector2D(FMath::Cos(Angle) * Radius.X, FMath::Sin(Angle) * Radius.Y));
		}
		FSlateDrawElement::MakeLines(OutDrawElements, Layer, Geometry.ToPaintGeometry(),
			Points, ESlateDrawEffect::None, Color, true, Thickness);
	}

	void DrawSparks(FSlateWindowElementList& OutDrawElements, int32 Layer,
		const FGeometry& Geometry, const FVector2D& Center, float Radius,
		const FLinearColor& Color, float Progress, int32 Count, float AngleOffset)
	{
		const float Spread = EaseOutCubic(Progress);
		for (int32 Index = 0; Index < Count; ++Index)
		{
			const float Angle = AngleOffset + 360.f * static_cast<float>(Index) / Count;
			const FVector2D Dir = RotateVector(FVector2D(1.f, 0.f), Angle);
			const float LengthScale = 0.72f + 0.28f * FMath::Sin((Index + 1) * 2.31f);
			const FVector2D A = Center + Dir * (8.f + Radius * 0.18f * Spread);
			const FVector2D B = Center + Dir * (18.f + Radius * LengthScale * Spread);
			DrawSegment(OutDrawElements, Layer, Geometry, A, B,
				WithAlpha(Color, (1.f - Progress) * 0.88f), FMath::Lerp(3.4f, 1.1f, Progress));
		}
	}

	void DrawRegularPolygon(FSlateWindowElementList& OutDrawElements, int32 Layer,
		const FGeometry& Geometry, const FVector2D& Center, float Radius, int32 Sides,
		float AngleOffset, const FLinearColor& Color, float Thickness)
	{
		TArray<FVector2D> Points;
		Points.Reserve(Sides + 1);
		for (int32 Index = 0; Index <= Sides; ++Index)
		{
			const float Angle = AngleOffset + 360.f * static_cast<float>(Index) / Sides;
			Points.Add(Center + RotateVector(FVector2D(Radius, 0.f), Angle));
		}
		FSlateDrawElement::MakeLines(OutDrawElements, Layer, Geometry.ToPaintGeometry(),
			Points, ESlateDrawEffect::None, Color, true, Thickness);
	}

	void DrawArc(FSlateWindowElementList& OutDrawElements, int32 Layer,
		const FGeometry& Geometry, const FVector2D& Center, const FVector2D& Radius,
		float StartDegrees, float SweepDegrees, const FLinearColor& Color, float Thickness)
	{
		constexpr int32 Segments = 28;
		TArray<FVector2D> Points;
		Points.Reserve(Segments + 1);
		for (int32 Index = 0; Index <= Segments; ++Index)
		{
			const float Angle = FMath::DegreesToRadians(StartDegrees + SweepDegrees * Index / Segments);
			Points.Add(Center + FVector2D(FMath::Cos(Angle) * Radius.X, FMath::Sin(Angle) * Radius.Y));
		}
		FSlateDrawElement::MakeLines(OutDrawElements, Layer, Geometry.ToPaintGeometry(),
			Points, ESlateDrawEffect::None, Color, true, Thickness);
	}
}

int32 UCombatSlashWidget::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
	int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const int32 BaseLayer = Super::NativePaint(Args, AllottedGeometry, MyCullingRect,
		OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
	const float P = FMath::Clamp(Progress, 0.f, 1.f);
	if (P <= 0.f) return BaseLayer;

	const FVector2D Size = AllottedGeometry.GetLocalSize();
	const FVector2D Center = Size * 0.5f;
	const float SafeStrength = FMath::Clamp(Strength, 0.5f, 2.f);
	const FLinearColor Tint = InWidgetStyle.GetColorAndOpacityTint() * SlashColor;
	const FLinearColor BladeCore(0.97f, 1.f, 1.f, Tint.A);
	const FLinearColor WarmCore(1.f, 0.94f, 0.68f, Tint.A);

	if (StrikeStyle == ECombatStrikeStyle::ArcSlash)
	{
		// A tapered blade with motion echoes, a luminous core and an impact bloom.
		const float Reveal = EaseOutCubic(FMath::Clamp(P / 0.58f, 0.f, 1.f));
		constexpr int32 Segments = 30;
		const int32 Visible = FMath::Clamp(FMath::RoundToInt(Segments * Reveal), 3, Segments);
		TArray<FVector2D> Blade;
		Blade.Reserve(Visible + 1);
		for (int32 Index = 0; Index <= Visible; ++Index)
		{
			const float T = static_cast<float>(Index) / Segments;
			const FVector2D A(Size.X * 0.10f, Size.Y * 0.79f);
			const FVector2D B(Size.X * 0.91f, Size.Y * 0.28f);
			const FVector2D Control(Size.X * 0.50f, -Size.Y * 0.08f);
			const FVector2D Point = A * FMath::Square(1.f - T)
				+ Control * (2.f * (1.f - T) * T) + B * (T * T);
			Blade.Add(Center + RotateVector(Point - Center, Rotation));
		}

		for (int32 Echo = 2; Echo >= 1; --Echo)
		{
			TArray<FVector2D> EchoPath = Blade;
			const FVector2D Offset = RotateVector(FVector2D(-7.f * Echo, 9.f * Echo), Rotation);
			for (FVector2D& Point : EchoPath) Point += Offset;
			DrawTaperedPath(OutDrawElements, BaseLayer + Echo, AllottedGeometry, EchoPath,
				WithAlpha(Tint, 0.13f / Echo), 19.f * SafeStrength, 2.f);
		}
		DrawTaperedPath(OutDrawElements, BaseLayer + 3, AllottedGeometry, Blade,
			WithAlpha(Tint, 0.22f), 34.f * SafeStrength, 5.f);
		DrawTaperedPath(OutDrawElements, BaseLayer + 4, AllottedGeometry, Blade,
			WithAlpha(Tint, 0.72f), 12.f * SafeStrength, 2.2f);
		DrawTaperedPath(OutDrawElements, BaseLayer + 5, AllottedGeometry, Blade,
			BladeCore, 3.8f * SafeStrength, 0.8f);

		if (P > 0.32f)
		{
			const float Impact = FMath::Clamp((P - 0.32f) / 0.68f, 0.f, 1.f);
			const FVector2D Contact = Center + RotateVector(FVector2D(Size.X * 0.16f, -Size.Y * 0.09f), Rotation);
			DrawSparks(OutDrawElements, BaseLayer + 6, AllottedGeometry, Contact,
				52.f * SafeStrength, WarmCore, Impact, 9, 12.f + Variant * 7.f);
			DrawEllipse(OutDrawElements, BaseLayer + 5, AllottedGeometry, Contact,
				FVector2D(42.f, 18.f) * EaseOutCubic(Impact) * SafeStrength,
				WithAlpha(Tint, 0.48f * (1.f - Impact)), 3.f);
		}
		return BaseLayer + 6;
	}

	if (StrikeStyle == ECombatStrikeStyle::Greatsword)
	{
		// Anticipation, accelerating fall, then a long shockwave settle.
		const float Fall = EaseInCubic(FMath::Clamp((P - 0.14f) / 0.36f, 0.f, 1.f));
		const float Impact = FMath::Clamp((P - 0.46f) / 0.54f, 0.f, 1.f);
		const FVector2D Axis = RotateVector(FVector2D(0.f, 1.f), Rotation - 14.f);
		const FVector2D Perp(-Axis.Y, Axis.X);
		const FVector2D SwordCenter = Center - Axis * FMath::Lerp(Size.Y * 0.48f, 4.f, Fall);
		const FVector2D Tip = SwordCenter + Axis * 128.f * SafeStrength;
		const FVector2D Pommel = SwordCenter - Axis * 92.f * SafeStrength;
		const float WindupAlpha = P < 0.46f ? FMath::Lerp(0.18f, 0.92f, Fall) : 1.f - Impact * 0.25f;

		DrawSegment(OutDrawElements, BaseLayer + 1, AllottedGeometry, Pommel, Tip,
			WithAlpha(Tint, 0.16f * WindupAlpha), 70.f * SafeStrength);
		DrawSegment(OutDrawElements, BaseLayer + 2, AllottedGeometry, Pommel, Tip,
			WithAlpha(Tint, 0.54f * WindupAlpha), 30.f * SafeStrength);
		DrawSegment(OutDrawElements, BaseLayer + 3, AllottedGeometry,
			Pommel + Axis * 16.f, Tip, WithAlpha(WarmCore, WindupAlpha), 8.f * SafeStrength);
		DrawSegment(OutDrawElements, BaseLayer + 4, AllottedGeometry,
			SwordCenter - Perp * 48.f * SafeStrength - Axis * 62.f,
			SwordCenter + Perp * 48.f * SafeStrength - Axis * 62.f,
			WithAlpha(Tint, 0.86f * WindupAlpha), 8.f * SafeStrength);
		DrawSegment(OutDrawElements, BaseLayer + 4, AllottedGeometry,
			Pommel - Perp * 10.f, Pommel + Perp * 10.f, WithAlpha(WarmCore, WindupAlpha), 14.f);

		if (Impact > 0.f)
		{
			const float Shock = EaseOutCubic(Impact);
			const FVector2D Contact = Center + Axis * 62.f;
			DrawEllipse(OutDrawElements, BaseLayer + 5, AllottedGeometry, Contact,
				FVector2D(145.f, 34.f) * Shock * SafeStrength,
				WithAlpha(Tint, 0.58f * (1.f - Impact)), 9.f * (1.f - 0.55f * Impact));
			DrawEllipse(OutDrawElements, BaseLayer + 6, AllottedGeometry, Contact,
				FVector2D(102.f, 23.f) * Shock * SafeStrength,
				WithAlpha(WarmCore, 0.76f * (1.f - Impact)), 3.5f);
			DrawSparks(OutDrawElements, BaseLayer + 7, AllottedGeometry, Contact,
				110.f * SafeStrength, WarmCore, Impact, 14, -8.f + Variant * 3.f);
			for (int32 Crack = -2; Crack <= 2; ++Crack)
			{
				const FVector2D CrackDir = RotateVector(FVector2D(1.f, 0.f), Crack * 19.f + 90.f);
				DrawSegment(OutDrawElements, BaseLayer + 6, AllottedGeometry, Contact,
					Contact + CrackDir * (34.f + FMath::Abs(Crack) * 12.f) * Shock,
					WithAlpha(WarmCore, 0.62f * (1.f - Impact)), 3.2f);
			}
		}
		return BaseLayer + 7;
	}

	if (StrikeStyle == ECombatStrikeStyle::MyriadSwords)
	{
		constexpr int32 SwordCount = 9;
		for (int32 Index = 0; Index < SwordCount; ++Index)
		{
			const float Delay = Index * 0.045f;
			const float Flight = EaseInCubic(FMath::Clamp((P - Delay) / 0.48f, 0.f, 1.f));
			if (Flight <= 0.f) continue;
			const float Angle = -158.f + Index * (316.f / (SwordCount - 1)) + Variant * 2.5f;
			const FVector2D Origin = Center + FVector2D(
				FMath::Cos(FMath::DegreesToRadians(Angle)) * Size.X * 0.44f,
				FMath::Sin(FMath::DegreesToRadians(Angle)) * Size.Y * 0.45f);
			const FVector2D Target = Center + RotateVector(FVector2D(13.f + (Index % 3) * 7.f, 0.f), Angle + 180.f);
			const FVector2D Dir = (Target - Origin).GetSafeNormal();
			const FVector2D Perp(-Dir.Y, Dir.X);
			const FVector2D Pos = FMath::Lerp(Origin, Target, Flight);
			const FVector2D Rear = Pos - Dir * 48.f;
			const FVector2D Tip = Pos + Dir * 30.f;
			const float LocalAlpha = P > 0.78f ? (1.f - P) / 0.22f : 1.f;

			DrawSegment(OutDrawElements, BaseLayer + 1, AllottedGeometry, Origin, Rear,
				WithAlpha(Tint, 0.13f * LocalAlpha * (1.f - Flight * 0.3f)), 12.f);
			DrawSegment(OutDrawElements, BaseLayer + 2, AllottedGeometry, Rear, Tip,
				WithAlpha(Tint, 0.22f * LocalAlpha), 22.f * SafeStrength);
			DrawSegment(OutDrawElements, BaseLayer + 3, AllottedGeometry, Rear, Tip,
				WithAlpha((Index % 2 == 0) ? BladeCore : WarmCore, 0.92f * LocalAlpha), 4.2f * SafeStrength);
			DrawSegment(OutDrawElements, BaseLayer + 4, AllottedGeometry,
				Rear - Perp * 12.f, Rear + Perp * 12.f,
				WithAlpha(Tint, 0.82f * LocalAlpha), 3.3f * SafeStrength);
		}

		if (P > 0.35f)
		{
			const float Impact = FMath::Clamp((P - 0.35f) / 0.65f, 0.f, 1.f);
			DrawSparks(OutDrawElements, BaseLayer + 5, AllottedGeometry, Center,
				78.f * SafeStrength, WarmCore, Impact, 18, Variant * 5.f);
			DrawEllipse(OutDrawElements, BaseLayer + 5, AllottedGeometry, Center,
				FVector2D(105.f, 58.f) * EaseOutCubic(Impact) * SafeStrength,
				WithAlpha(Tint, 0.42f * (1.f - Impact)), 5.f);
		}
		return BaseLayer + 5;
	}

	if (StrikeStyle == ECombatStrikeStyle::SwordWave)
	{
		const float Reveal = EaseOutCubic(FMath::Clamp(P / 0.66f, 0.f, 1.f));
		for (int32 Wave = 0; Wave < 3; ++Wave)
		{
			constexpr int32 Segments = 34;
			const int32 Visible = FMath::Clamp(FMath::RoundToInt(Segments * Reveal), 3, Segments);
			TArray<FVector2D> Arc;
			Arc.Reserve(Visible + 1);
			for (int32 Index = 0; Index <= Visible; ++Index)
			{
				const float T = static_cast<float>(Index) / Segments;
				const float X = Size.X * (0.04f + 0.92f * T);
				const float Y = Center.Y + (Wave - 1) * 24.f - FMath::Sin(T * PI) * (70.f + Wave * 8.f);
				Arc.Add(Center + RotateVector(FVector2D(X, Y) - Center, Rotation));
			}
			DrawTaperedPath(OutDrawElements, BaseLayer + Wave + 1, AllottedGeometry, Arc,
				WithAlpha(Tint, Wave == 1 ? 0.32f : 0.13f), (26.f - Wave * 4.f) * SafeStrength, 2.f);
			DrawTaperedPath(OutDrawElements, BaseLayer + Wave + 4, AllottedGeometry, Arc,
				WithAlpha(Wave == 1 ? BladeCore : Tint, Wave == 1 ? 0.95f : 0.58f),
				(5.f - Wave * 0.7f) * SafeStrength, 0.8f);
		}
		if (P > 0.38f)
		{
			const float Impact = FMath::Clamp((P - 0.38f) / 0.62f, 0.f, 1.f);
			DrawSparks(OutDrawElements, BaseLayer + 8, AllottedGeometry,
				Center + FVector2D(Size.X * 0.22f, -Size.Y * 0.06f), 88.f,
				WarmCore, Impact, 12, Variant * 5.f);
		}
		return BaseLayer + 8;
	}

	if (StrikeStyle == ECombatStrikeStyle::Thunder)
	{
		const float Strike = EaseInCubic(FMath::Clamp(P / 0.42f, 0.f, 1.f));
		for (int32 Bolt = -1; Bolt <= 1; ++Bolt)
		{
			TArray<FVector2D> Path;
			const FVector2D Start(Center.X + Bolt * 68.f, 0.f);
			const FVector2D End = Center + FVector2D(Bolt * 18.f, 34.f);
			constexpr int32 Steps = 8;
			const int32 Visible = FMath::Clamp(FMath::CeilToInt(Steps * Strike), 2, Steps);
			for (int32 Step = 0; Step <= Visible; ++Step)
			{
				const float T = static_cast<float>(Step) / Steps;
				const float Jitter = Step == 0 || Step == Steps ? 0.f
					: FMath::Sin((Step + Variant * 3 + Bolt * 5) * 4.17f) * (20.f - 8.f * FMath::Abs(Bolt));
				Path.Add(FMath::Lerp(Start, End, T) + FVector2D(Jitter, 0.f));
			}
			DrawTaperedPath(OutDrawElements, BaseLayer + 1, AllottedGeometry, Path,
				WithAlpha(Tint, 0.24f), 25.f * SafeStrength, 7.f);
			DrawTaperedPath(OutDrawElements, BaseLayer + 2, AllottedGeometry, Path,
				WithAlpha(BladeCore, Bolt == 0 ? 1.f : 0.72f), 6.f * SafeStrength, 2.f);
		}
		if (P > 0.30f)
		{
			const float Impact = FMath::Clamp((P - 0.30f) / 0.70f, 0.f, 1.f);
			DrawEllipse(OutDrawElements, BaseLayer + 3, AllottedGeometry, Center + FVector2D(0.f, 34.f),
				FVector2D(132.f, 38.f) * EaseOutCubic(Impact), WithAlpha(Tint, 0.70f * (1.f - Impact)), 7.f);
			DrawSparks(OutDrawElements, BaseLayer + 4, AllottedGeometry, Center + FVector2D(0.f, 34.f),
				110.f * SafeStrength, BladeCore, Impact, 16, Variant * 9.f);
		}
		return BaseLayer + 4;
	}

	if (StrikeStyle == ECombatStrikeStyle::FlameBurst)
	{
		const float Bloom = EaseOutCubic(FMath::Clamp(P / 0.62f, 0.f, 1.f));
		for (int32 Flame = 0; Flame < 9; ++Flame)
		{
			const float Angle = -110.f + Flame * 27.5f + Variant * 2.f;
			const FVector2D Dir = RotateVector(FVector2D(1.f, 0.f), Angle);
			const FVector2D Perp(-Dir.Y, Dir.X);
			TArray<FVector2D> Tongue;
			Tongue.Add(Center + Dir * 8.f);
			Tongue.Add(Center + Dir * (34.f * Bloom) + Perp * (Flame % 2 == 0 ? 12.f : -12.f));
			Tongue.Add(Center + Dir * ((68.f + Flame * 4.f) * Bloom));
			DrawTaperedPath(OutDrawElements, BaseLayer + 1, AllottedGeometry, Tongue,
				WithAlpha(Tint, 0.32f), 24.f * SafeStrength, 3.f);
			DrawTaperedPath(OutDrawElements, BaseLayer + 2, AllottedGeometry, Tongue,
				WithAlpha(WarmCore, 0.88f), 7.f * SafeStrength, 1.f);
		}
		DrawEllipse(OutDrawElements, BaseLayer + 3, AllottedGeometry, Center,
			FVector2D(92.f, 58.f) * Bloom, WithAlpha(Tint, 0.58f * (1.f - P)), 8.f);
		DrawSparks(OutDrawElements, BaseLayer + 4, AllottedGeometry, Center,
			96.f * SafeStrength, WarmCore, P, 13, Variant * 11.f);
		return BaseLayer + 4;
	}

	if (StrikeStyle == ECombatStrikeStyle::Poison)
	{
		const float Bloom = EaseOutCubic(FMath::Clamp(P / 0.72f, 0.f, 1.f));
		for (int32 Coil = 0; Coil < 3; ++Coil)
		{
			const float Radius = (34.f + Coil * 24.f) * Bloom * SafeStrength;
			DrawArc(OutDrawElements, BaseLayer + Coil + 1, AllottedGeometry, Center,
				FVector2D(Radius, Radius * 0.68f), Variant * 17.f + Coil * 105.f,
				230.f, WithAlpha(Tint, 0.72f - Coil * 0.14f), 9.f - Coil * 1.7f);
		}
		for (int32 Bubble = 0; Bubble < 7; ++Bubble)
		{
			const float Phase = FMath::Fmod(P + Bubble * 0.13f, 1.f);
			const FVector2D Pos = Center + FVector2D(
				FMath::Sin(Bubble * 2.7f + Variant) * 58.f,
				42.f - Phase * 116.f);
			DrawEllipse(OutDrawElements, BaseLayer + 5, AllottedGeometry, Pos,
				FVector2D(7.f + Bubble % 3 * 2.f) * (0.7f + Phase * 0.3f),
				WithAlpha(BladeCore, 0.70f * (1.f - Phase)), 3.f);
		}
		return BaseLayer + 5;
	}

	if (StrikeStyle == ECombatStrikeStyle::Ward)
	{
		const float Form = EaseOutCubic(FMath::Clamp(P / 0.46f, 0.f, 1.f));
		const float Pulse = 1.f + 0.045f * FMath::Sin(P * PI * 5.f);
		for (int32 Ring = 0; Ring < 3; ++Ring)
		{
			const float Radius = (48.f + Ring * 25.f) * Form * Pulse * SafeStrength;
			DrawRegularPolygon(OutDrawElements, BaseLayer + Ring + 1, AllottedGeometry,
				Center, Radius, 6, 30.f + Rotation + Ring * 10.f,
				WithAlpha(Ring == 0 ? BladeCore : Tint, 0.90f - Ring * 0.22f),
				Ring == 0 ? 7.f : 4.f);
		}
		for (int32 Ray = 0; Ray < 6; ++Ray)
		{
			const FVector2D Inner = Center + RotateVector(FVector2D(28.f * Form, 0.f), Ray * 60.f + 30.f);
			const FVector2D Outer = Center + RotateVector(FVector2D(88.f * Form, 0.f), Ray * 60.f + 30.f);
			DrawSegment(OutDrawElements, BaseLayer + 4, AllottedGeometry, Inner, Outer,
				WithAlpha(Tint, 0.38f * (1.f - P * 0.35f)), 3.f);
		}
		return BaseLayer + 4;
	}

	if (StrikeStyle == ECombatStrikeStyle::SpiritFlow)
	{
		const float Rise = EaseOutCubic(P);
		for (int32 Wisp = 0; Wisp < 7; ++Wisp)
		{
			TArray<FVector2D> Path;
			for (int32 Step = 0; Step <= 10; ++Step)
			{
				const float T = static_cast<float>(Step) / 10.f;
				const float X = (Wisp - 3) * 22.f + FMath::Sin((T * 2.f + Wisp) * PI) * 15.f;
				const float Y = 78.f - T * (116.f * Rise) + FMath::Cos(Wisp * 1.7f) * 9.f;
				Path.Add(Center + FVector2D(X, Y));
			}
			DrawTaperedPath(OutDrawElements, BaseLayer + 1 + Wisp % 2, AllottedGeometry, Path,
				WithAlpha(Wisp % 2 == 0 ? BladeCore : Tint, 0.48f + 0.06f * Wisp),
				5.f * SafeStrength, 1.f);
		}
		DrawEllipse(OutDrawElements, BaseLayer + 3, AllottedGeometry, Center + FVector2D(0.f, 56.f),
			FVector2D(82.f, 22.f) * EaseOutCubic(FMath::Min(P * 1.5f, 1.f)),
			WithAlpha(Tint, 0.56f * (1.f - P * 0.55f)), 5.f);
		return BaseLayer + 3;
	}

	if (StrikeStyle == ECombatStrikeStyle::PowerAura)
	{
		const float Form = EaseOutCubic(FMath::Clamp(P / 0.58f, 0.f, 1.f));
		DrawEllipse(OutDrawElements, BaseLayer + 1, AllottedGeometry, Center,
			FVector2D(74.f, 74.f) * Form * SafeStrength, WithAlpha(Tint, 0.58f), 8.f);
		DrawEllipse(OutDrawElements, BaseLayer + 2, AllottedGeometry, Center,
			FVector2D(47.f, 47.f) * Form * SafeStrength, WithAlpha(WarmCore, 0.78f), 3.f);
		for (int32 Ray = 0; Ray < 12; ++Ray)
		{
			const FVector2D Dir = RotateVector(FVector2D(1.f, 0.f), Ray * 30.f + Variant * 3.f);
			DrawSegment(OutDrawElements, BaseLayer + 3, AllottedGeometry,
				Center + Dir * 63.f * Form, Center + Dir * (92.f + (Ray % 3) * 11.f) * Form,
				WithAlpha(Ray % 2 == 0 ? WarmCore : Tint, 0.74f * (1.f - P * 0.42f)),
				Ray % 2 == 0 ? 5.f : 3.f);
		}
		DrawRegularPolygon(OutDrawElements, BaseLayer + 4, AllottedGeometry, Center,
			33.f * Form, 4, 45.f + Rotation, WithAlpha(BladeCore, 0.92f), 5.f);
		return BaseLayer + 4;
	}

	if (StrikeStyle == ECombatStrikeStyle::Talisman || StrikeStyle == ECombatStrikeStyle::Seal)
	{
		const float Form = EaseOutCubic(FMath::Clamp(P / 0.52f, 0.f, 1.f));
		const int32 Sides = StrikeStyle == ECombatStrikeStyle::Talisman ? 4 : 8;
		DrawRegularPolygon(OutDrawElements, BaseLayer + 1, AllottedGeometry, Center,
			82.f * Form * SafeStrength, Sides, StrikeStyle == ECombatStrikeStyle::Talisman ? 45.f : 22.5f,
			WithAlpha(Tint, 0.46f), 15.f);
		DrawRegularPolygon(OutDrawElements, BaseLayer + 2, AllottedGeometry, Center,
			82.f * Form * SafeStrength, Sides, StrikeStyle == ECombatStrikeStyle::Talisman ? 45.f : 22.5f,
			WithAlpha(WarmCore, 0.92f), 4.f);
		DrawEllipse(OutDrawElements, BaseLayer + 3, AllottedGeometry, Center,
			FVector2D(53.f) * Form, WithAlpha(Tint, 0.68f), 4.f);
		for (int32 Stroke = -2; Stroke <= 2; ++Stroke)
		{
			const FVector2D A = Center + FVector2D(Stroke * 14.f, -42.f * Form);
			const FVector2D B = Center + FVector2D(-Stroke * 8.f, 42.f * Form);
			DrawSegment(OutDrawElements, BaseLayer + 4, AllottedGeometry, A, B,
				WithAlpha(Stroke == 0 ? BladeCore : Tint, 0.82f), Stroke == 0 ? 5.f : 3.f);
		}
		if (StrikeStyle == ECombatStrikeStyle::Seal)
			DrawSparks(OutDrawElements, BaseLayer + 5, AllottedGeometry, Center, 76.f,
				WarmCore, P, 8, Variant * 13.f);
		return BaseLayer + 5;
	}

	if (StrikeStyle == ECombatStrikeStyle::ImpactBurst)
	{
		const float Bloom = EaseOutCubic(P);
		DrawEllipse(OutDrawElements, BaseLayer + 1, AllottedGeometry, Center,
			FVector2D(78.f, 45.f) * Bloom * SafeStrength,
			WithAlpha(Tint, 0.72f * (1.f - P)), 8.f * (1.f - P * 0.55f));
		DrawEllipse(OutDrawElements, BaseLayer + 2, AllottedGeometry, Center,
			FVector2D(43.f, 25.f) * Bloom * SafeStrength,
			WithAlpha(WarmCore, 0.86f * (1.f - P)), 3.5f);
		DrawSparks(OutDrawElements, BaseLayer + 3, AllottedGeometry, Center,
			74.f * SafeStrength, WarmCore, P, 12, Variant * 9.f);
		return BaseLayer + 3;
	}

	// Curse: dark-red inward claws followed by a fractured pulse around the player.
	const float Contract = 1.f - EaseInCubic(FMath::Clamp(P / 0.60f, 0.f, 1.f));
	for (int32 Claw = 0; Claw < 10; ++Claw)
	{
		const FVector2D Dir = RotateVector(FVector2D(1.f, 0.f), Claw * 36.f + Variant * 4.f);
		const FVector2D A = Center + Dir * (40.f + 85.f * Contract);
		const FVector2D B = Center + Dir * 24.f;
		DrawSegment(OutDrawElements, BaseLayer + 1, AllottedGeometry, A, B,
			WithAlpha(Tint, 0.72f * (1.f - P * 0.35f)), 8.f * SafeStrength);
		DrawSegment(OutDrawElements, BaseLayer + 2, AllottedGeometry,
			FMath::Lerp(A, B, 0.58f), B, WithAlpha(BladeCore, 0.66f), 2.5f);
	}
	DrawEllipse(OutDrawElements, BaseLayer + 3, AllottedGeometry, Center,
		FVector2D(58.f, 44.f) * EaseOutCubic(P), WithAlpha(Tint, 0.62f * (1.f - P)), 7.f);
	return BaseLayer + 3;
}
