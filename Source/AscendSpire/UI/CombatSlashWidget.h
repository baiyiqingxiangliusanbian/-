#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "CombatSlashWidget.generated.h"

/**
 * 轻量 Slate 自绘剑光：用多层曲线、辉光和火星替代两根粗白矩形。
 * 它没有贴图依赖，适合先快速迭代表现层，后续也可替换为 Niagara/贴图特效。
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

	void SetSlashProgress(float InProgress)
	{
		Progress = FMath::Clamp(InProgress, 0.f, 1.f);
		Invalidate(EInvalidateWidgetReason::Paint);
	}

	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
		int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
};
