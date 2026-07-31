#include "AscendArt.h"
#include "Engine/Texture2D.h"
#include "Components/Image.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"

TMap<FString, TWeakObjectPtr<UTexture2D>> FAscendArt::Cache;

bool FAscendArt::Exists(const FString& RelativePath)
{
	if (RelativePath.IsEmpty()) return false;
	return IFileManager::Get().FileExists(*(FPaths::ProjectContentDir() / RelativePath));
}

UTexture2D* FAscendArt::GetTexture(UObject* WorldContext, const FString& RelativePath)
{
	if (RelativePath.IsEmpty()) return nullptr;

	// 缓存命中且存活
	if (TWeakObjectPtr<UTexture2D>* Found = Cache.Find(RelativePath))
	{
		if (Found->IsValid()) return Found->Get();
	}

	const FString FullPath = FPaths::ProjectContentDir() / RelativePath;
	if (!IFileManager::Get().FileExists(*FullPath)) return nullptr;

	UTexture2D* Tex = UKismetRenderingLibrary::ImportFileAsTexture2D(WorldContext, FullPath);
	if (Tex)
	{
		Tex->AddToRoot(); // 防止被 GC（缓存持有弱引用）
		Cache.Add(RelativePath, Tex);
	}
	return Tex;
}

UImage* FAscendArt::MakeImage(UObject* Outer, const FString& RelativePath)
{
	UTexture2D* Tex = GetTexture(Outer, RelativePath);
	if (!Tex) return nullptr;

	UImage* Img = NewObject<UImage>(Outer);
	FSlateBrush Brush;
	Brush.SetResourceObject(Tex);
	Brush.ImageSize = FVector2D(Tex->GetSizeX(), Tex->GetSizeY());
	Img->SetBrush(Brush);
	return Img;
}
