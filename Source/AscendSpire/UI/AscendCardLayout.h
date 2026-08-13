#pragma once

// One shared geometry contract for every playable/reward/relic card.
// All values are authored in a 168 x 252 (2:3) logical canvas and scaled
// uniformly by the caller.  Keeping this independent from the art texture's
// source resolution prevents the frame, illustration and text from drifting.
namespace AscendCardLayout
{
	constexpr float Width = 168.f;
	constexpr float Height = 252.f;
	constexpr float HalfWidth = Width * 0.5f;
	constexpr float HalfHeight = Height * 0.5f;

	constexpr float InnerX = 10.f;
	constexpr float InnerY = 14.f;
	constexpr float InnerW = 148.f;
	constexpr float InnerH = 226.f;

	constexpr float ArtX = 11.f;
	constexpr float ArtY = 15.f;
	constexpr float ArtW = 146.f;
	constexpr float ArtH = 130.f;

	constexpr float TitleX = 13.f;
	constexpr float TitleY = 137.f;
	constexpr float TitleW = 142.f;
	constexpr float TitleH = 29.f;

	constexpr float RulesX = 12.f;
	constexpr float RulesY = 164.f;
	constexpr float RulesW = 144.f;
	constexpr float RulesH = 76.f;

	constexpr float CostX = 5.f;
	constexpr float CostY = 6.f;
	constexpr float CostW = 39.f;
	constexpr float CostH = 47.f;

	// Rotated fan cards extend below their pivot.  These margins keep even the
	// outermost cards fully on-screen instead of clipping their rules panels.
	constexpr float HandBottomMargin = 54.f;
	constexpr float CompactHandBottomMargin = 34.f;
}
