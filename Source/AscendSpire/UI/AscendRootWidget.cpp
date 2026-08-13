#include "AscendRootWidget.h"

#include "Core/AscendPlayerController.h"

FReply UAscendRootWidget::NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (InMouseEvent.IsTouchEvent() && OwnerController.IsValid())
	{
		// SButton 的移动事件会继续向父级冒泡。直接使用 PointerEvent 的 Slate
		// 屏幕坐标，避开部分 Android 设备错误的 PlayerInput Touch(0,0)。
		OwnerController->HandleRootPointerMoved(InMouseEvent.GetScreenSpacePosition());
	}
	return Super::NativeOnMouseMove(InGeometry, InMouseEvent);
}

FReply UAscendRootWidget::NativeOnTouchMoved(const FGeometry& InGeometry, const FPointerEvent& InTouchEvent)
{
	if (OwnerController.IsValid())
	{
		OwnerController->HandleRootPointerMoved(InTouchEvent.GetScreenSpacePosition());
	}
	return Super::NativeOnTouchMoved(InGeometry, InTouchEvent);
}
