#include "CombatSimCommandlet.h"
#include "CombatEngine.h"
#include "CombatAI.h"
#include "Core/GameDataLibrary.h"

int32 UAscendCombatSimCommandlet::Main(const FString& Params)
{
	FString Error;

	TArray<FString> StarterDeck;
	if (!UGameDataLibrary::LoadStarterDeck(StarterDeck, Error))
	{
		UE_LOG(LogTemp, Error, TEXT("加载初始卡组失败: %s"), *Error);
		return 1;
	}

	// 参数解析
	int32 Runs = 10;
	FParse::Value(*Params, TEXT("runs="), Runs);
	FString SingleEnemy;
	FParse::Value(*Params, TEXT("enemy="), SingleEnemy);
	FString RelicsCsv;
	FParse::Value(*Params, TEXT("relics="), RelicsCsv);
	FString PillsCsv;
	FParse::Value(*Params, TEXT("pills="), PillsCsv);
	FString EnemiesCsv;
	FParse::Value(*Params, TEXT("enemies="), EnemiesCsv);
	const bool bVerbose = FParse::Param(*Params, TEXT("verbose"));

	// 注意: UE 命令行把逗号当分隔符，列表参数用 + 连接
	TArray<FString> TestRelics, TestPills, MultiEnemies;
	RelicsCsv.ParseIntoArray(TestRelics, TEXT("+"));
	PillsCsv.ParseIntoArray(TestPills, TEXT("+"));
	EnemiesCsv.ParseIntoArray(MultiEnemies, TEXT("+"));

	UE_LOG(LogTemp, Display, TEXT("初始卡组: %d 张, 法宝: %d 件, 丹药: %d 颗"),
		StarterDeck.Num(), TestRelics.Num(), TestPills.Num());

	// 测试对象
	TArray<FEnemyData> AllEnemies;
	if (!UGameDataLibrary::LoadEnemies(AllEnemies, Error)) return 1;

	TArray<TArray<FString>> Battles;
	if (MultiEnemies.Num() > 0)
	{
		Battles.Add(MultiEnemies); // 多敌战
	}
	else if (!SingleEnemy.IsEmpty())
	{
		Battles.Add({SingleEnemy});
	}
	else
	{
		for (const FEnemyData& E : AllEnemies) Battles.Add({E.Id});
	}

	UE_LOG(LogTemp, Display, TEXT("====== 战斗模拟开始: 每场 %d 次 ======"), Runs);

	int32 TotalWins = 0, TotalRuns = 0;

	for (const TArray<FString>& EnemyGroup : Battles)
	{
		int32 Wins = 0;
		int32 TotalTurns = 0;

		for (int32 Run = 0; Run < Runs; ++Run)
		{
			TArray<FDeckCard> Deck;
			for (const FString& Id : StarterDeck)
			{
				FDeckCard DC;
				DC.CardId = Id;
				Deck.Add(DC);
			}

			UCombatEngine* Engine = NewObject<UCombatEngine>();
			Engine->TestPills = TestPills;
			if (!Engine->StartCombat(Deck, EnemyGroup, TestRelics, 70, -1, 0, 12345 + Run))
			{
				UE_LOG(LogTemp, Error, TEXT("战斗初始化失败"));
				return 1;
			}

			if (FCombatAI::SimulateBattle(Engine, 50)) Wins++;
			TotalTurns += Engine->TurnCount;
		}

		FString GroupName;
		for (const FString& Id : EnemyGroup)
		{
			const FEnemyData* EData = AllEnemies.FindByPredicate([&](const FEnemyData& E) { return E.Id == Id; });
			GroupName += (EData ? EData->Name : Id) + TEXT(" ");
		}

		UE_LOG(LogTemp, Display, TEXT("[%-20s] 胜率 %2d/%2d  平均回合 %.1f"),
			*GroupName, Wins, Runs, Runs > 0 ? (float)TotalTurns / Runs : 0.f);

		TotalWins += Wins;
		TotalRuns += Runs;
	}

	UE_LOG(LogTemp, Display, TEXT("====== 模拟结束: 总胜率 %d/%d ======"), TotalWins, TotalRuns);
	return 0;
}
