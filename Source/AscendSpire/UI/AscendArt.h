#pragma once

#include "CoreMinimal.h"

class UTexture2D;
class UImage;

/**
 * 贴图库 —— 运行时从 Content/Art 加载 PNG（带缓存）
 * 贴图缺失时返回 nullptr，UI 回退到纯色块
 */
class ASCENDSPIRE_API FAscendArt
{
public:
	/** 按 JSON 相对路径（如 "Art/cards/strike.png"）获取贴图，带缓存 */
	static UTexture2D* GetTexture(UObject* WorldContext, const FString& RelativePath);

	/** 创建图片控件（无贴图返回 nullptr） */
	static UImage* MakeImage(UObject* Outer, const FString& RelativePath);

	/** 贴图是否存在 */
	static bool Exists(const FString& RelativePath);

private:
	static TMap<FString, TWeakObjectPtr<UTexture2D>> Cache;
};
