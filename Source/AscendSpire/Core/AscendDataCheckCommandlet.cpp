#include "AscendDataCheckCommandlet.h"
#include "GameDataLibrary.h"

int32 UAscendDataCheckCommandlet::Main(const FString& Params)
{
	UE_LOG(LogTemp, Display, TEXT("=============================================="));
	UE_LOG(LogTemp, Display, TEXT("  AscendSpire 登仙路 - Data Integrity Check"));
	UE_LOG(LogTemp, Display, TEXT("=============================================="));

	bool bAllOk = true;
	FString Err;

	// --- 卡牌 ---
	TArray<FCardData> Cards;
	if (UGameDataLibrary::LoadCards(Cards, Err))
	{
		UE_LOG(LogTemp, Display, TEXT("[CARDS]   OK: %d cards loaded"), Cards.Num());
		for (const FCardData& C : Cards)
		{
			UE_LOG(LogTemp, Display, TEXT("  - [%s] %s (%s/%s) cost=%d effects=%d"),
				*C.Id, *C.Name, *C.Type, *C.Rarity, C.Cost, C.Effects.Num());
			if (C.Effects.Num() == 0)
			{
				UE_LOG(LogTemp, Warning, TEXT("    WARNING: card has no effects"));
			}
		}
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[CARDS]   FAILED: %s"), *Err);
		bAllOk = false;
	}

	// --- 敌人 ---
	TArray<FEnemyData> Enemies;
	if (UGameDataLibrary::LoadEnemies(Enemies, Err))
	{
		UE_LOG(LogTemp, Display, TEXT("[ENEMIES] OK: %d enemies loaded"), Enemies.Num());
		for (const FEnemyData& E : Enemies)
		{
			UE_LOG(LogTemp, Display, TEXT("  - [%s] %s (%s) hp=%d intents=%d"),
				*E.Id, *E.Name, *E.Tier, E.MaxHP, E.Intents.Num());
			if (E.Intents.Num() == 0)
			{
				UE_LOG(LogTemp, Warning, TEXT("    WARNING: enemy has no intents"));
			}
		}
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[ENEMIES] FAILED: %s"), *Err);
		bAllOk = false;
	}

	// --- 法宝 ---
	TArray<FRelicData> Relics;
	if (UGameDataLibrary::LoadRelics(Relics, Err))
	{
		UE_LOG(LogTemp, Display, TEXT("[RELICS]  OK: %d relics loaded"), Relics.Num());
		for (const FRelicData& R : Relics)
		{
			UE_LOG(LogTemp, Display, TEXT("  - [%s] %s (%s) trigger=%s"),
				*R.Id, *R.Name, *R.Rarity, *R.Trigger);
		}
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[RELICS]  FAILED: %s"), *Err);
		bAllOk = false;
	}

	// --- 丹药 ---
	TArray<FPillData> Pills;
	if (UGameDataLibrary::LoadPills(Pills, Err))
	{
		UE_LOG(LogTemp, Display, TEXT("[PILLS]   OK: %d pills loaded"), Pills.Num());
		for (const FPillData& P : Pills)
		{
			UE_LOG(LogTemp, Display, TEXT("  - [%s] %s toxicity=%d"),
				*P.Id, *P.Name, P.Toxicity);
		}
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[PILLS]   FAILED: %s"), *Err);
		bAllOk = false;
	}

	UE_LOG(LogTemp, Display, TEXT("=============================================="));
	UE_LOG(LogTemp, Display, TEXT("  RESULT: %s"), bAllOk ? TEXT("ALL PASSED") : TEXT("FAILED"));
	UE_LOG(LogTemp, Display, TEXT("=============================================="));

	return bAllOk ? 0 : 1;
}
