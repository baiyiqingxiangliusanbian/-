#pragma once

#include "CoreMinimal.h"
#include "GameDataTypes.h"
#include "GameDataLibrary.generated.h"

class FJsonObject;

/**
 * 游戏数据加载库 —— 从 Content/Data 下的 JSON 文件加载全部游戏数据
 * 所有数据热加载，改 JSON 不用重编译
 */
UCLASS()
class ASCENDSPIRE_API UGameDataLibrary : public UObject
{
	GENERATED_BODY()

public:
	static bool LoadCards(TArray<FCardData>& OutCards, FString& OutError);
	static bool LoadStarterDeck(TArray<FString>& OutCardIds, FString& OutError);
	static bool LoadEnemies(TArray<FEnemyData>& OutEnemies, FString& OutError);
	static bool LoadRelics(TArray<FRelicData>& OutRelics, FString& OutError);
	static bool LoadPills(TArray<FPillData>& OutPills, FString& OutError);
	static bool LoadEvents(TArray<FEventData>& OutEvents, FString& OutError);

private:
	static bool LoadJsonFile(const FString& RelativePath, TSharedPtr<FJsonObject>& OutRoot, FString& OutError);
	static FCardEffect ParseEffect(const TSharedPtr<FJsonObject>& Obj);
	static void ParseEffectArray(const TSharedPtr<FJsonObject>& Obj, const FString& FieldName, TArray<FCardEffect>& OutEffects);
};
