#pragma once

#include "CoreMinimal.h"
#include "ClickProxy.generated.h"

class AAscendPlayerController;
class UButton;

/**
 * 点击代理 —— UButton::OnClicked 无参数，用代理对象携带下标/标签派发
 */
UCLASS()
class UClickProxy : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY()
	int32 Index = -1;

	UPROPERTY()
	FString Tag;

	UPROPERTY()
	TWeakObjectPtr<AAscendPlayerController> Owner;

	UFUNCTION()
	void HandleClick();

	UFUNCTION()
	void HandlePress();

	UFUNCTION()
	void HandleRelease();

	UPROPERTY()
	UButton* BoundButton;

	UFUNCTION()
	void HandleHovered();

	UFUNCTION()
	void HandleUnhovered();
};
