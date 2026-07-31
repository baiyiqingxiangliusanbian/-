#include "NarrativeSystem.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

void UNarrativeSystem::LoadFromJSON(const FString& JSONPath)
{
	FString Content;
	if (!FFileHelper::LoadFileToString(Content, *JSONPath))
	{
		UE_LOG(LogTemp, Warning, TEXT("[Narrative] 无法加载剧情文件: %s"), *JSONPath);
		return;
	}

	TSharedPtr<FJsonObject> Root;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Content);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("[Narrative] JSON 解析失败"));
		return;
	}

	const TArray<TSharedPtr<FJsonValue>>* ActsArr;
	if (!Root->TryGetArrayField(TEXT("acts"), ActsArr)) return;

	for (const TSharedPtr<FJsonValue>& ActVal : *ActsArr)
	{
		const TSharedPtr<FJsonObject> ActObj = ActVal->AsObject();
		if (!ActObj) continue;

		FActData Act;
		Act.ActId = ActObj->GetStringField(TEXT("act_id"));
		Act.StartBeatId = ActObj->GetStringField(TEXT("start_beat"));

		const TArray<TSharedPtr<FJsonValue>>* BeatsArr;
		if (ActObj->TryGetArrayField(TEXT("beats"), BeatsArr))
		{
			for (const TSharedPtr<FJsonValue>& BeatVal : *BeatsArr)
			{
				const TSharedPtr<FJsonObject> BeatObj = BeatVal->AsObject();
				if (!BeatObj) continue;

				FNarrativeBeat Beat;
				Beat.BeatId = BeatObj->GetStringField(TEXT("id"));
				Beat.NarratorText = BeatObj->GetStringField(TEXT("narrator_text"));

				const TArray<TSharedPtr<FJsonValue>>* ChoicesArr;
				if (BeatObj->TryGetArrayField(TEXT("choices"), ChoicesArr))
				{
					for (const TSharedPtr<FJsonValue>& ChVal : *ChoicesArr)
					{
						const TSharedPtr<FJsonObject> ChObj = ChVal->AsObject();
						if (!ChObj) continue;

						FNarrativeChoice Ch;
						Ch.Text = ChObj->GetStringField(TEXT("text"));

						const TSharedPtr<FJsonObject> OutObj = ChObj->GetObjectField(TEXT("outcome"));
						if (OutObj)
						{
							Ch.Outcome.SummaryText = OutObj->GetStringField(TEXT("summary"));
							FString TypeStr = OutObj->GetStringField(TEXT("type"));
							if (TypeStr == TEXT("combat")) Ch.Outcome.Type = ENarrativeOutcomeType::Combat;
							else if (TypeStr == TEXT("elite")) Ch.Outcome.Type = ENarrativeOutcomeType::Elite;
							else if (TypeStr == TEXT("boss")) Ch.Outcome.Type = ENarrativeOutcomeType::Boss;
							else if (TypeStr == TEXT("rest")) Ch.Outcome.Type = ENarrativeOutcomeType::Rest;
							else if (TypeStr == TEXT("shop")) Ch.Outcome.Type = ENarrativeOutcomeType::Shop;
							else if (TypeStr == TEXT("event")) Ch.Outcome.Type = ENarrativeOutcomeType::Event;
							else if (TypeStr == TEXT("game_over")) Ch.Outcome.Type = ENarrativeOutcomeType::GameOver;
							else Ch.Outcome.Type = ENarrativeOutcomeType::NextBeat;

							Ch.Outcome.Param = OutObj->GetStringField(TEXT("param"));
							Ch.Outcome.HPChange = OutObj->GetIntegerField(TEXT("hp"));
							Ch.Outcome.GoldChange = OutObj->GetIntegerField(TEXT("gold"));

							OutObj->TryGetStringField(TEXT("relic"), Ch.Outcome.GainRelicId);
							OutObj->TryGetStringField(TEXT("card"), Ch.Outcome.GainCardId);
							Ch.Outcome.bUpgraded = OutObj->GetBoolField(TEXT("upgraded"));
						}

						Beat.Choices.Add(Ch);
					}
				}

				Act.Beats.Add(Beat.BeatId, Beat);
			}
		}

		Acts.Add(Act);
	}

	UE_LOG(LogTemp, Display, TEXT("[Narrative] 已加载 %d 幕剧情"), Acts.Num());
}

FNarrativeBeat UNarrativeSystem::GetStartBeat(const FString& ActId) const
{
	for (const FActData& Act : Acts)
	{
		if (Act.ActId == ActId)
		{
			const FNarrativeBeat* Beat = Act.Beats.Find(Act.StartBeatId);
			if (Beat) return *Beat;
			break;
		}
	}
	FNarrativeBeat Fallback;
	Fallback.BeatId = TEXT("fallback");
	Fallback.NarratorText = TEXT("山路崎岖，前路未卜……");
	return Fallback;
}

const FNarrativeBeat* UNarrativeSystem::FindBeat(const FString& BeatId) const
{
	for (const FActData& Act : Acts)
	{
		const FNarrativeBeat* Beat = Act.Beats.Find(BeatId);
		if (Beat) return Beat;
	}
	return nullptr;
}
