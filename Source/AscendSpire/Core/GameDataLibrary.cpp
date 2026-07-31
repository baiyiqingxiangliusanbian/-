#include "GameDataLibrary.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

DEFINE_LOG_CATEGORY_STATIC(LogGameData, Log, All);

bool UGameDataLibrary::LoadJsonFile(const FString& RelativePath, TSharedPtr<FJsonObject>& OutRoot, FString& OutError)
{
	const FString FullPath = FPaths::ProjectContentDir() / RelativePath;

	FString Content;
	if (!FFileHelper::LoadFileToString(Content, *FullPath))
	{
		OutError = FString::Printf(TEXT("File not found: %s"), *FullPath);
		return false;
	}

	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Content);
	if (!FJsonSerializer::Deserialize(Reader, OutRoot) || !OutRoot.IsValid())
	{
		OutError = FString::Printf(TEXT("JSON parse failed: %s"), *FullPath);
		return false;
	}

	return true;
}

FCardEffect UGameDataLibrary::ParseEffect(const TSharedPtr<FJsonObject>& Obj)
{
	FCardEffect E;
	if (!Obj.IsValid())
	{
		return E;
	}

	Obj->TryGetStringField(TEXT("action"), E.Action);
	Obj->TryGetStringField(TEXT("target"), E.Target);
	Obj->TryGetStringField(TEXT("status"), E.StatusId);

	double NumVal;
	if (Obj->TryGetNumberField(TEXT("value"), NumVal)) E.Value = static_cast<int32>(NumVal);
	if (Obj->TryGetNumberField(TEXT("times"), NumVal)) E.Times = FMath::Max(1, static_cast<int32>(NumVal));
	if (Obj->TryGetNumberField(TEXT("stacks"), NumVal)) E.StatusStacks = static_cast<int32>(NumVal);

	return E;
}

void UGameDataLibrary::ParseEffectArray(const TSharedPtr<FJsonObject>& Obj, const FString& FieldName, TArray<FCardEffect>& OutEffects)
{
	if (!Obj.IsValid()) return;

	const TArray<TSharedPtr<FJsonValue>>* Arr;
	if (Obj->TryGetArrayField(FieldName, Arr))
	{
		for (const TSharedPtr<FJsonValue>& Val : *Arr)
		{
			const TSharedPtr<FJsonObject>* EffectObj;
			if (Val->TryGetObject(EffectObj))
			{
				OutEffects.Add(ParseEffect(*EffectObj));
			}
		}
	}
}

bool UGameDataLibrary::LoadCards(TArray<FCardData>& OutCards, FString& OutError)
{
	TSharedPtr<FJsonObject> Root;
	if (!LoadJsonFile(TEXT("Data/cards.json"), Root, OutError)) return false;

	const TArray<TSharedPtr<FJsonValue>>* Arr;
	if (!Root->TryGetArrayField(TEXT("cards"), Arr))
	{
		OutError = TEXT("cards.json: missing 'cards' array");
		return false;
	}

	for (const TSharedPtr<FJsonValue>& Val : *Arr)
	{
		const TSharedPtr<FJsonObject>* Obj;
		if (!Val->TryGetObject(Obj)) continue;

		FCardData Card;
		(*Obj)->TryGetStringField(TEXT("id"), Card.Id);
		(*Obj)->TryGetStringField(TEXT("name"), Card.Name);
		(*Obj)->TryGetStringField(TEXT("type"), Card.Type);
		(*Obj)->TryGetStringField(TEXT("rarity"), Card.Rarity);
		(*Obj)->TryGetStringField(TEXT("description"), Card.Description);
		(*Obj)->TryGetStringField(TEXT("flavor"), Card.Flavor);
		(*Obj)->TryGetStringField(TEXT("art"), Card.ArtPath);
		(*Obj)->TryGetStringField(TEXT("class"), Card.Class);
		(*Obj)->TryGetBoolField(TEXT("exhaust"), Card.bExhaust);
		(*Obj)->TryGetBoolField(TEXT("retain"), Card.bRetain);
		(*Obj)->TryGetStringField(TEXT("counter_condition"), Card.CounterCondition);

		double NumVal;
		if ((*Obj)->TryGetNumberField(TEXT("cost"), NumVal)) Card.Cost = static_cast<int32>(NumVal);

		ParseEffectArray(*Obj, TEXT("effects"), Card.Effects);

		// 升级数据
		const TSharedPtr<FJsonObject>* UpgradeObj;
		if ((*Obj)->TryGetObjectField(TEXT("upgrade"), UpgradeObj))
		{
			if ((*UpgradeObj)->TryGetNumberField(TEXT("cost"), NumVal))
			{
				Card.UpgradedCost = static_cast<int32>(NumVal);
			}
			(*UpgradeObj)->TryGetStringField(TEXT("description"), Card.UpgradedDescription);
			(*UpgradeObj)->TryGetStringField(TEXT("counter_condition"), Card.UpgradedCounterCondition);
			ParseEffectArray(*UpgradeObj, TEXT("effects"), Card.UpgradedEffects);
		}

		if (Card.Id.IsEmpty())
		{
			UE_LOG(LogGameData, Warning, TEXT("Card with empty id skipped"));
			continue;
		}
		OutCards.Add(Card);
	}

	return true;
}

bool UGameDataLibrary::LoadStarterDeck(TArray<FString>& OutCardIds, FString& OutError)
{
	TSharedPtr<FJsonObject> Root;
	if (!LoadJsonFile(TEXT("Data/cards.json"), Root, OutError)) return false;

	const TArray<TSharedPtr<FJsonValue>>* Arr;
	if (!Root->TryGetArrayField(TEXT("starter_deck"), Arr))
	{
		OutError = TEXT("cards.json: missing 'starter_deck' array");
		return false;
	}

	for (const TSharedPtr<FJsonValue>& Val : *Arr)
	{
		FString Id;
		if (Val->TryGetString(Id)) OutCardIds.Add(Id);
	}
	return OutCardIds.Num() > 0;
}

bool UGameDataLibrary::LoadEnemies(TArray<FEnemyData>& OutEnemies, FString& OutError)
{
	TSharedPtr<FJsonObject> Root;
	if (!LoadJsonFile(TEXT("Data/enemies.json"), Root, OutError)) return false;

	const TArray<TSharedPtr<FJsonValue>>* Arr;
	if (!Root->TryGetArrayField(TEXT("enemies"), Arr))
	{
		OutError = TEXT("enemies.json: missing 'enemies' array");
		return false;
	}

	for (const TSharedPtr<FJsonValue>& Val : *Arr)
	{
		const TSharedPtr<FJsonObject>* Obj;
		if (!Val->TryGetObject(Obj)) continue;

		FEnemyData Enemy;
		(*Obj)->TryGetStringField(TEXT("id"), Enemy.Id);
		(*Obj)->TryGetStringField(TEXT("name"), Enemy.Name);
		(*Obj)->TryGetStringField(TEXT("tier"), Enemy.Tier);
		(*Obj)->TryGetStringField(TEXT("story"), Enemy.Story);
		(*Obj)->TryGetStringField(TEXT("ability_desc"), Enemy.AbilityDesc);
		(*Obj)->TryGetStringField(TEXT("art"), Enemy.ArtPath);

		const TArray<TSharedPtr<FJsonValue>>* AbilArr;
		if ((*Obj)->TryGetArrayField(TEXT("abilities"), AbilArr))
		{
			for (const TSharedPtr<FJsonValue>& AVal : *AbilArr)
			{
				FString AStr;
				if (AVal->TryGetString(AStr)) Enemy.Abilities.Add(AStr);
			}
		}

		double NumVal;
		if ((*Obj)->TryGetNumberField(TEXT("hp"), NumVal)) Enemy.MaxHP = static_cast<int32>(NumVal);

		const TArray<TSharedPtr<FJsonValue>>* IntentArr;
		if ((*Obj)->TryGetArrayField(TEXT("intents"), IntentArr))
		{
			for (const TSharedPtr<FJsonValue>& IVal : *IntentArr)
			{
				const TSharedPtr<FJsonObject>* IObj;
				if (!IVal->TryGetObject(IObj)) continue;

				FEnemyIntent Intent;
				(*IObj)->TryGetStringField(TEXT("action"), Intent.Action);
				(*IObj)->TryGetStringField(TEXT("status"), Intent.StatusId);

				double N;
				if ((*IObj)->TryGetNumberField(TEXT("value"), N)) Intent.Value = static_cast<int32>(N);
				if ((*IObj)->TryGetNumberField(TEXT("times"), N)) Intent.Times = FMath::Max(1, static_cast<int32>(N));
				if ((*IObj)->TryGetNumberField(TEXT("weight"), N)) Intent.Weight = FMath::Max(1, static_cast<int32>(N));
				if ((*IObj)->TryGetNumberField(TEXT("stacks"), N)) Intent.StatusStacks = static_cast<int32>(N);

				Enemy.Intents.Add(Intent);
			}
		}

		if (Enemy.Id.IsEmpty()) continue;
		OutEnemies.Add(Enemy);
	}

	return true;
}

bool UGameDataLibrary::LoadRelics(TArray<FRelicData>& OutRelics, FString& OutError)
{
	TSharedPtr<FJsonObject> Root;
	if (!LoadJsonFile(TEXT("Data/relics.json"), Root, OutError)) return false;

	const TArray<TSharedPtr<FJsonValue>>* Arr;
	if (!Root->TryGetArrayField(TEXT("relics"), Arr))
	{
		OutError = TEXT("relics.json: missing 'relics' array");
		return false;
	}

	for (const TSharedPtr<FJsonValue>& Val : *Arr)
	{
		const TSharedPtr<FJsonObject>* Obj;
		if (!Val->TryGetObject(Obj)) continue;

		FRelicData Relic;
		(*Obj)->TryGetStringField(TEXT("id"), Relic.Id);
		(*Obj)->TryGetStringField(TEXT("name"), Relic.Name);
		(*Obj)->TryGetStringField(TEXT("rarity"), Relic.Rarity);
		(*Obj)->TryGetStringField(TEXT("description"), Relic.Description);
		(*Obj)->TryGetStringField(TEXT("trigger"), Relic.Trigger);
		(*Obj)->TryGetStringField(TEXT("condition"), Relic.Condition);
		(*Obj)->TryGetStringField(TEXT("art"), Relic.ArtPath);

		double NumVal;
		if ((*Obj)->TryGetNumberField(TEXT("modifier"), NumVal)) Relic.Modifier = static_cast<float>(NumVal);
		if ((*Obj)->TryGetNumberField(TEXT("counter"), NumVal)) Relic.Counter = static_cast<int32>(NumVal);

		const TSharedPtr<FJsonObject>* EffectObj;
		if ((*Obj)->TryGetObjectField(TEXT("effect"), EffectObj))
		{
			Relic.Effect = ParseEffect(*EffectObj);
		}

		if (Relic.Id.IsEmpty()) continue;
		OutRelics.Add(Relic);
	}

	return true;
}

bool UGameDataLibrary::LoadEvents(TArray<FEventData>& OutEvents, FString& OutError)
{
	TSharedPtr<FJsonObject> Root;
	if (!LoadJsonFile(TEXT("Data/events.json"), Root, OutError)) return false;

	const TArray<TSharedPtr<FJsonValue>>* Arr;
	if (!Root->TryGetArrayField(TEXT("events"), Arr))
	{
		OutError = TEXT("events.json: missing 'events' array");
		return false;
	}

	for (const TSharedPtr<FJsonValue>& Val : *Arr)
	{
		const TSharedPtr<FJsonObject>* Obj;
		if (!Val->TryGetObject(Obj)) continue;

		FEventData Ev;
		(*Obj)->TryGetStringField(TEXT("id"), Ev.Id);
		(*Obj)->TryGetStringField(TEXT("title"), Ev.Title);
		(*Obj)->TryGetStringField(TEXT("text"), Ev.Text);

		const TArray<TSharedPtr<FJsonValue>>* ChoiceArr;
		if ((*Obj)->TryGetArrayField(TEXT("choices"), ChoiceArr))
		{
			for (const TSharedPtr<FJsonValue>& CVal : *ChoiceArr)
			{
				const TSharedPtr<FJsonObject>* CObj;
				if (!CVal->TryGetObject(CObj)) continue;

				FEventChoice Choice;
				(*CObj)->TryGetStringField(TEXT("text"), Choice.Text);
				(*CObj)->TryGetStringField(TEXT("result"), Choice.ResultText);

				const TArray<TSharedPtr<FJsonValue>>* EffectArr;
				if ((*CObj)->TryGetArrayField(TEXT("effects"), EffectArr))
				{
					for (const TSharedPtr<FJsonValue>& EVal : *EffectArr)
					{
						const TSharedPtr<FJsonObject>* EObj;
						if (!EVal->TryGetObject(EObj)) continue;

						FEventEffect Eff;
						(*EObj)->TryGetStringField(TEXT("action"), Eff.Action);
						(*EObj)->TryGetStringField(TEXT("param"), Eff.Param);
						double N;
						if ((*EObj)->TryGetNumberField(TEXT("value"), N)) Eff.Value = static_cast<int32>(N);
						Choice.Effects.Add(Eff);
					}
				}
				Ev.Choices.Add(Choice);
			}
		}

		if (Ev.Id.IsEmpty()) continue;
		OutEvents.Add(Ev);
	}

	return true;
}

bool UGameDataLibrary::LoadPills(TArray<FPillData>& OutPills, FString& OutError)
{
	TSharedPtr<FJsonObject> Root;
	if (!LoadJsonFile(TEXT("Data/pills.json"), Root, OutError)) return false;

	const TArray<TSharedPtr<FJsonValue>>* Arr;
	if (!Root->TryGetArrayField(TEXT("pills"), Arr))
	{
		OutError = TEXT("pills.json: missing 'pills' array");
		return false;
	}

	for (const TSharedPtr<FJsonValue>& Val : *Arr)
	{
		const TSharedPtr<FJsonObject>* Obj;
		if (!Val->TryGetObject(Obj)) continue;

		FPillData Pill;
		(*Obj)->TryGetStringField(TEXT("id"), Pill.Id);
		(*Obj)->TryGetStringField(TEXT("name"), Pill.Name);
		(*Obj)->TryGetStringField(TEXT("description"), Pill.Description);

		double NumVal;
		if ((*Obj)->TryGetNumberField(TEXT("toxicity"), NumVal)) Pill.Toxicity = static_cast<int32>(NumVal);

		const TSharedPtr<FJsonObject>* EffectObj;
		if ((*Obj)->TryGetObjectField(TEXT("effect"), EffectObj))
		{
			Pill.Effect = ParseEffect(*EffectObj);
		}

		if (Pill.Id.IsEmpty()) continue;
		OutPills.Add(Pill);
	}

	return true;
}
