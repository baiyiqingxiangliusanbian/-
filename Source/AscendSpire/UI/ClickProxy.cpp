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

void UClickProxy::HandleHovered()
{
	if (!Owner.IsValid()) return;
	if (Tag == TEXT("hand_hover") && BoundButton)
	{
		// 悬停：原生尺寸大卡预览（位图拉伸会让字体模糊，已弃用缩放方案）
		BoundButton->SetRenderTranslation(FVector2D(0.f, -10.f));
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
		BoundButton->SetRenderTranslation(FVector2D::ZeroVector);
		Owner->HideCardPreview();
	}
	else if (Tag == TEXT("relic_hover"))
	{
		Owner->HideRelicTooltip();
	}
}
