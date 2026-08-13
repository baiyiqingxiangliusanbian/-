#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "AscendRootWidget.generated.h"

class UCanvasPanel;
class AAscendPlayerController;

/** 根界面 Widget —— C++ 构建全部内容，无蓝图 */
UCLASS()
class ASCENDSPIRE_API UAscendRootWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UPROPERTY()
	UCanvasPanel* AnimCanvas;

	UPROPERTY()
	TWeakObjectPtr<AAscendPlayerController> OwnerController;

protected:
	virtual FReply NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnTouchMoved(const FGeometry& InGeometry, const FPointerEvent& InTouchEvent) override;
};
