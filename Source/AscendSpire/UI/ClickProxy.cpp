#include "ClickProxy.h"
#include "Core/AscendPlayerController.h"
#include "Components/Button.h"

void UClickProxy::HandleClick()
{
	UE_LOG(LogTemp, Display, TEXT("[CLICK] 按钮点击到达代理: %s [%d]"), *Tag, Index);
	if (Owner.IsValid())
	{
		Owner->DispatchClick(Tag, Index);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[CLICK] Owner 已失效!"));
	}
}

void UClickProxy::HandlePress()
{
	if (Owner.IsValid())
	{
		if (Tag == TEXT("hand_card"))
		{
			Owner->HandleCardPressed(Index);
		}
	}
}

void UClickProxy::HandleRelease()
{
	if (Owner.IsValid() && Tag == TEXT("hand_card"))
	{
		// UButton 会捕获鼠标/触点，因此即使拖出按钮范围也能在释放时结束拖牌。
		Owner->OnMouseLeftReleased();
	}
}

void UClickProxy::HandleHovered()
{
	if (!Owner.IsValid()) return;
	if (Tag == TEXT("hand_hover") && BoundButton)
	{
		// 直接放大扇形手牌中的原牌，不再创建第二张预览牌。
		Owner->ShowCardPreview(Index);
	}
	else if (Tag == TEXT("relic_hover"))
	{
		Owner->ShowRelicTooltipByIndex(Index);
	}
}

void UClickProxy::HandleUnhovered()
{
	if (!Owner.IsValid()) return;
	if (Tag == TEXT("hand_hover") && BoundButton)
	{
		Owner->HideCardPreview(Index);
	}
	else if (Tag == TEXT("relic_hover"))
	{
		Owner->HideRelicTooltip();
	}
}
