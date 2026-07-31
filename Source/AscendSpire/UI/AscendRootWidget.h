#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "AscendRootWidget.generated.h"

class UCanvasPanel;

/** 根界面 Widget —— C++ 构建全部内容，无蓝图 */
UCLASS()
class ASCENDSPIRE_API UAscendRootWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UPROPERTY()
	UCanvasPanel* AnimCanvas;
};
