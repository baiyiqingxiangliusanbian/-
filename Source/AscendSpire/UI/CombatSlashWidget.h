#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "CombatSlashWidget.generated.h"

/** 卡牌命中特效族。所有样式共用同一个轻量 Slate 绘制器，但拥有独立构图和时间轴。 */
UENUM(BlueprintType)
enum class ECombatStrikeStyle : uint8
{
	ArcSlash,
	Greatsword,
	MyriadSwords,
	SwordWave,
	Thunder,
	FlameBurst,
	Poison,
	Ward,
	SpiritFlow,
	PowerAura,
	Talisman,
	Seal,
	CurseBurst,
	ImpactBurst
};

/**
 * 程序化卡牌命中特效。
 *
 * 旧版本只画一条等宽曲线，所有剑牌看起来都像临时占位线。本控件改为四套
 * 可辨识的视觉语汇：剑系的弧斩、巨剑、万剑、剑气波，以及雷、火、毒、
 * 护盾、聚灵、功法气场、符箓、状态印记和诅咒爆发。每套都有独立构图、
 * 时间轴和多层辉光，不再依赖一条临时占位线。
 */
UCLASS()
class ASCENDSPIRE_API UCombatSlashWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FLinearColor SlashColor = FLinearColor(0.82f, 0.95f, 1.f, 1.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float Progress = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float Rotation = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	ECombatStrikeStyle StrikeStyle = ECombatStrikeStyle::ArcSlash;

	/** 0.5~2.0，控制刃宽、辉光和冲击波规模。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float Strength = 1.f;

	/** 让多次同类特效仍保持稳定但略有差异，不使用逐帧随机数。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 Variant = 0;

	void SetSlashProgress(float InProgress)
	{
		Progress = FMath::Clamp(InProgress, 0.f, 1.f);
		Invalidate(EInvalidateWidgetReason::Paint);
	}

	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
		int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
};
