#include "InfiniteNarrativeService.h"

#include "CardScriptCompiler.h"

#include "GameDataLibrary.h"
#include "Async/Async.h"
#include "Containers/StringConv.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HttpModule.h"
#include "Misc/FileHelper.h"
#include "Misc/Crc.h"
#include "Misc/Paths.h"
#include "Internationalization/Regex.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/ScopeLock.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
	FString ResolveChatCompletionsUrl(FString Endpoint)
	{
		Endpoint.TrimStartAndEndInline();
		while (Endpoint.EndsWith(TEXT("/"))) Endpoint.LeftChopInline(1);
		// Settings accepts either an OpenAI-compatible base URL (.../v1) or the
		// complete endpoint. The provider supplied for this project is a base URL.
		if (!Endpoint.EndsWith(TEXT("/chat/completions"), ESearchCase::IgnoreCase))
			Endpoint += TEXT("/chat/completions");
		return Endpoint;
	}

	FString JsonString(const TSharedPtr<FJsonObject>& Object)
	{
		FString Result;
		if (!Object.IsValid()) return Result;
		const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Result);
		FJsonSerializer::Serialize(Object.ToSharedRef(), Writer);
		return Result;
	}

	FString MergeWorldBookEntries(const FString& BaseJson, const FString& EngineRouteJson)
	{
		TSharedPtr<FJsonObject> BaseRoot;
		TSharedPtr<FJsonObject> RouteRoot;
		const TSharedRef<TJsonReader<>> BaseReader = TJsonReaderFactory<>::Create(BaseJson);
		const TSharedRef<TJsonReader<>> RouteReader = TJsonReaderFactory<>::Create(EngineRouteJson);
		const bool bBaseValid = FJsonSerializer::Deserialize(BaseReader, BaseRoot) && BaseRoot.IsValid();
		const bool bRouteValid = FJsonSerializer::Deserialize(RouteReader, RouteRoot) && RouteRoot.IsValid();
		if (!bRouteValid) return BaseJson;
		if (!bBaseValid) return EngineRouteJson;

		TArray<TSharedPtr<FJsonValue>> CombinedEntries;
		const TArray<TSharedPtr<FJsonValue>>* BaseEntries = nullptr;
		if (BaseRoot->TryGetArrayField(TEXT("entries"), BaseEntries) && BaseEntries)
			CombinedEntries.Append(*BaseEntries);
		const TArray<TSharedPtr<FJsonValue>>* RouteEntries = nullptr;
		if (RouteRoot->TryGetArrayField(TEXT("entries"), RouteEntries) && RouteEntries)
			CombinedEntries.Append(*RouteEntries);
		BaseRoot->SetArrayField(TEXT("entries"), CombinedEntries);
		return JsonString(BaseRoot);
	}

	bool ExtractPartialJsonString(const FString& Source, const FString& Key, int32 SearchFrom,
		FString& OutValue, bool& bOutComplete, int32* OutValueEnd = nullptr)
	{
		OutValue.Reset();
		bOutComplete = false;
		const int32 KeyPos = Source.Find(TEXT("\"") + Key + TEXT("\""), ESearchCase::CaseSensitive,
			ESearchDir::FromStart, FMath::Max(0, SearchFrom));
		if (KeyPos == INDEX_NONE) return false;
		int32 Pos = Source.Find(TEXT(":"), ESearchCase::CaseSensitive, ESearchDir::FromStart,
			KeyPos + Key.Len() + 2);
		if (Pos == INDEX_NONE) return false;
		++Pos;
		while (Pos < Source.Len() && FChar::IsWhitespace(Source[Pos])) ++Pos;
		if (Pos >= Source.Len() || Source[Pos] != TEXT('"')) return false;
		++Pos;
		for (; Pos < Source.Len(); ++Pos)
		{
			const TCHAR Ch = Source[Pos];
			if (Ch == TEXT('"'))
			{
				bOutComplete = true;
				if (OutValueEnd) *OutValueEnd = Pos + 1;
				return true;
			}
			if (Ch != TEXT('\\'))
			{
				OutValue.AppendChar(Ch);
				continue;
			}
			if (Pos + 1 >= Source.Len()) break;
			const TCHAR Escape = Source[++Pos];
			switch (Escape)
			{
			case TEXT('"'): OutValue.AppendChar(TEXT('"')); break;
			case TEXT('\\'): OutValue.AppendChar(TEXT('\\')); break;
			case TEXT('/'): OutValue.AppendChar(TEXT('/')); break;
			case TEXT('b'): OutValue.AppendChar(TEXT('\b')); break;
			case TEXT('f'): OutValue.AppendChar(TEXT('\f')); break;
			case TEXT('n'): OutValue.AppendChar(TEXT('\n')); break;
			case TEXT('r'): OutValue.AppendChar(TEXT('\r')); break;
			case TEXT('t'): OutValue.AppendChar(TEXT('\t')); break;
			case TEXT('u'):
			{
				if (Pos + 4 >= Source.Len())
				{
					if (OutValueEnd) *OutValueEnd = Pos - 1;
					return true;
				}
				uint32 CodeUnit = 0;
				bool bValid = true;
				for (int32 HexIndex = 1; HexIndex <= 4; ++HexIndex)
				{
					const TCHAR Hex = Source[Pos + HexIndex];
					CodeUnit <<= 4;
					if (Hex >= TEXT('0') && Hex <= TEXT('9')) CodeUnit += Hex - TEXT('0');
					else if (Hex >= TEXT('a') && Hex <= TEXT('f')) CodeUnit += Hex - TEXT('a') + 10;
					else if (Hex >= TEXT('A') && Hex <= TEXT('F')) CodeUnit += Hex - TEXT('A') + 10;
					else { bValid = false; break; }
				}
				if (bValid) OutValue.AppendChar(static_cast<TCHAR>(CodeUnit));
				Pos += 4;
				break;
			}
			default: OutValue.AppendChar(Escape); break;
			}
		}
		if (OutValueEnd) *OutValueEnd = Pos;
		return true;
	}

	int32 FindBalancedJsonObjectEnd(const FString& Source, int32 Start)
	{
		if (!Source.IsValidIndex(Start) || Source[Start] != TEXT('{')) return INDEX_NONE;
		int32 Depth = 0;
		bool bInString = false;
		bool bEscaped = false;
		for (int32 Pos = Start; Pos < Source.Len(); ++Pos)
		{
			const TCHAR Ch = Source[Pos];
			if (bInString)
			{
				if (bEscaped) { bEscaped = false; continue; }
				if (Ch == TEXT('\\')) { bEscaped = true; continue; }
				if (Ch == TEXT('"')) bInString = false;
				continue;
			}
			if (Ch == TEXT('"')) { bInString = true; continue; }
			if (Ch == TEXT('{')) ++Depth;
			else if (Ch == TEXT('}') && --Depth == 0) return Pos;
		}
		return INDEX_NONE;
	}

	FInfiniteDialogueLine ParsePartialDialogueLine(const FString& ObjectText)
	{
		FInfiniteDialogueLine Line;
		bool bComplete = false;
		ExtractPartialJsonString(ObjectText, TEXT("speaker"), 0, Line.Speaker, bComplete);
		ExtractPartialJsonString(ObjectText, TEXT("portrait_id"), 0, Line.PortraitId, bComplete);
		ExtractPartialJsonString(ObjectText, TEXT("expression"), 0, Line.Expression, bComplete);
		ExtractPartialJsonString(ObjectText, TEXT("text"), 0, Line.Text, bComplete);
		if (Line.Expression.IsEmpty()) Line.Expression = TEXT("neutral");
		return Line;
	}

	FInfiniteNarrativeBeat ParseStreamingScenePreview(const FString& Content)
	{
		FInfiniteNarrativeBeat Preview;
		const int32 ScenePos = Content.Find(TEXT("\"scene\""));
		if (ScenePos == INDEX_NONE) return Preview;
		bool bComplete = false;
		ExtractPartialJsonString(Content, TEXT("title"), ScenePos, Preview.Title, bComplete);
		ExtractPartialJsonString(Content, TEXT("narration"), ScenePos, Preview.Narration, bComplete);

		const int32 MessagesKey = Content.Find(TEXT("\"messages\""), ESearchCase::CaseSensitive,
			ESearchDir::FromStart, ScenePos);
		if (MessagesKey == INDEX_NONE) return Preview;
		const int32 ArrayStart = Content.Find(TEXT("["), ESearchCase::CaseSensitive,
			ESearchDir::FromStart, MessagesKey);
		if (ArrayStart == INDEX_NONE) return Preview;
		int32 Cursor = ArrayStart + 1;
		while (Cursor < Content.Len() && Preview.DialogueLines.Num() < 16)
		{
			const int32 ObjectStart = Content.Find(TEXT("{"), ESearchCase::CaseSensitive,
				ESearchDir::FromStart, Cursor);
			if (ObjectStart == INDEX_NONE) break;
			const int32 ArrayEnd = Content.Find(TEXT("]"), ESearchCase::CaseSensitive,
				ESearchDir::FromStart, Cursor);
			if (ArrayEnd != INDEX_NONE && ArrayEnd < ObjectStart) break;
			const int32 ObjectEnd = FindBalancedJsonObjectEnd(Content, ObjectStart);
			const FString ObjectText = ObjectEnd == INDEX_NONE
				? Content.Mid(ObjectStart) : Content.Mid(ObjectStart, ObjectEnd - ObjectStart + 1);
			FInfiniteDialogueLine Line = ParsePartialDialogueLine(ObjectText);
			if (!Line.Text.IsEmpty() || !Line.Speaker.IsEmpty()) Preview.DialogueLines.Add(MoveTemp(Line));
			if (ObjectEnd == INDEX_NONE) break;
			Cursor = ObjectEnd + 1;
		}
		return Preview;
	}

	FString TrimJsonEnvelope(FString Text)
	{
		Text.TrimStartAndEndInline();
		if (Text.StartsWith(TEXT("```")))
		{
			int32 FirstNewline = INDEX_NONE;
			if (Text.FindChar(TEXT('\n'), FirstNewline)) Text = Text.Mid(FirstNewline + 1);
			const int32 LastFence = Text.Find(TEXT("```"), ESearchCase::CaseSensitive, ESearchDir::FromEnd);
			if (LastFence != INDEX_NONE) Text = Text.Left(LastFence);
			Text.TrimStartAndEndInline();
		}
		const int32 FirstBrace = Text.Find(TEXT("{"));
		const int32 LastBrace = Text.Find(TEXT("}"), ESearchCase::CaseSensitive, ESearchDir::FromEnd);
		if (FirstBrace != INDEX_NONE && LastBrace >= FirstBrace)
		{
			Text = Text.Mid(FirstBrace, LastBrace - FirstBrace + 1);
		}
		return Text;
	}

	FString ExtractTransportContent(const FString& ResponseBody)
	{
		TSharedPtr<FJsonObject> Transport;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ResponseBody);
		if (!FJsonSerializer::Deserialize(Reader, Transport) || !Transport.IsValid()) return ResponseBody.Left(60000);
		const TArray<TSharedPtr<FJsonValue>>* Choices = nullptr;
		if (Transport->TryGetArrayField(TEXT("choices"), Choices) && Choices->Num() > 0)
		{
			const TSharedPtr<FJsonObject> First = (*Choices)[0]->AsObject();
			const TSharedPtr<FJsonObject>* Message = nullptr;
			FString Content;
			if (First.IsValid() && First->TryGetObjectField(TEXT("message"), Message))
			{
				if ((*Message)->TryGetStringField(TEXT("content"), Content)) return Content.Left(60000);
				const TArray<TSharedPtr<FJsonValue>>* Parts = nullptr;
				if ((*Message)->TryGetArrayField(TEXT("content"), Parts) && Parts)
				{
					TArray<FString> TextParts;
					for (const TSharedPtr<FJsonValue>& PartValue : *Parts)
					{
						if (!PartValue.IsValid()) continue;
						FString Direct;
						if (PartValue->TryGetString(Direct)) { TextParts.Add(Direct); continue; }
						const TSharedPtr<FJsonObject> Part = PartValue->AsObject();
						if (Part.IsValid() && (Part->TryGetStringField(TEXT("text"), Direct)
							|| Part->TryGetStringField(TEXT("content"), Direct))) TextParts.Add(Direct);
					}
					if (TextParts.Num() > 0) return FString::Join(TextParts, TEXT("\n")).Left(60000);
				}
			}
		}
		return ResponseBody.Left(60000);
	}

	FString RepairCommonJsonMistakes(const FString& Text);

	bool ParseModelJsonObject(const FString& Text, TSharedPtr<FJsonObject>& OutObject, FString* OutClean = nullptr)
	{
		OutObject.Reset();
		auto TryCandidate = [&OutObject, OutClean](FString Candidate) -> bool
		{
			Candidate.TrimStartAndEndInline();
			if (Candidate.IsEmpty()) return false;
			const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Candidate);
			if (FJsonSerializer::Deserialize(Reader, OutObject) && OutObject.IsValid())
			{
				if (OutClean) *OutClean = Candidate;
				return true;
			}
			const FString Repaired = RepairCommonJsonMistakes(Candidate);
			const TSharedRef<TJsonReader<>> RepairedReader = TJsonReaderFactory<>::Create(Repaired);
			if (FJsonSerializer::Deserialize(RepairedReader, OutObject) && OutObject.IsValid())
			{
				if (OutClean) *OutClean = Repaired;
				return true;
			}
			OutObject.Reset();
			return false;
		};

		if (TryCandidate(TrimJsonEnvelope(Text))) return true;

		// Some OpenAI-compatible gateways prepend an explanation or emit two objects even
		// with response_format=json_object. Extract balanced top-level objects and prefer
		// the one that actually contains the event/narrative choices schema.
		TSharedPtr<FJsonObject> FirstValid;
		FString FirstValidText;
		int32 Depth = 0;
		int32 Start = INDEX_NONE;
		bool bInString = false;
		bool bEscaped = false;
		for (int32 Index = 0; Index < Text.Len(); ++Index)
		{
			const TCHAR Ch = Text[Index];
			if (bInString)
			{
				if (bEscaped) { bEscaped = false; continue; }
				if (Ch == TEXT('\\')) { bEscaped = true; continue; }
				if (Ch == TEXT('"')) bInString = false;
				continue;
			}
			if (Ch == TEXT('"')) { bInString = true; continue; }
			if (Ch == TEXT('{'))
			{
				if (Depth == 0) Start = Index;
				++Depth;
			}
			else if (Ch == TEXT('}') && Depth > 0)
			{
				--Depth;
				if (Depth == 0 && Start != INDEX_NONE)
				{
					TSharedPtr<FJsonObject> CandidateObject;
					FString CandidateText;
					const FString Candidate = Text.Mid(Start, Index - Start + 1);
					TSharedPtr<FJsonObject> SavedObject = OutObject;
					if (TryCandidate(Candidate))
					{
						CandidateObject = OutObject;
						CandidateText = OutClean ? *OutClean : Candidate;
						if (CandidateObject->HasField(TEXT("choices"))) return true;
						if (!FirstValid.IsValid())
						{
							FirstValid = CandidateObject;
							FirstValidText = CandidateText;
						}
					}
					OutObject = SavedObject;
					Start = INDEX_NONE;
				}
			}
		}
		if (FirstValid.IsValid())
		{
			OutObject = FirstValid;
			if (OutClean) *OutClean = FirstValidText;
			return true;
		}
		return false;
	}

	void LogTransportMetrics(const TCHAR* Stage, const FString& ResponseBody, double ElapsedSeconds)
	{
		TSharedPtr<FJsonObject> Root;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ResponseBody);
		if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid()) return;
		int32 ContentChars = 0;
		int32 ReasoningChars = 0;
		FString FinishReason;
		const TArray<TSharedPtr<FJsonValue>>* Choices = nullptr;
		if (Root->TryGetArrayField(TEXT("choices"), Choices) && Choices && Choices->Num() > 0)
		{
			const TSharedPtr<FJsonObject> Choice = (*Choices)[0].IsValid() ? (*Choices)[0]->AsObject() : nullptr;
			const TSharedPtr<FJsonObject>* Message = nullptr;
			if (Choice.IsValid()) Choice->TryGetStringField(TEXT("finish_reason"), FinishReason);
			if (Choice.IsValid() && Choice->TryGetObjectField(TEXT("message"), Message) && Message && Message->IsValid())
			{
				FString Value;
				if ((*Message)->TryGetStringField(TEXT("content"), Value)) ContentChars = Value.Len();
				if ((*Message)->TryGetStringField(TEXT("reasoning_content"), Value)) ReasoningChars = Value.Len();
			}
		}
		int32 PromptTokens = -1;
		int32 CompletionTokens = -1;
		int32 ReasoningTokens = -1;
		const TSharedPtr<FJsonObject>* Usage = nullptr;
		if (Root->TryGetObjectField(TEXT("usage"), Usage) && Usage && Usage->IsValid())
		{
			double Number = 0;
			if ((*Usage)->TryGetNumberField(TEXT("prompt_tokens"), Number)) PromptTokens = FMath::RoundToInt(Number);
			if ((*Usage)->TryGetNumberField(TEXT("completion_tokens"), Number)) CompletionTokens = FMath::RoundToInt(Number);
			const TSharedPtr<FJsonObject>* Details = nullptr;
			if ((*Usage)->TryGetObjectField(TEXT("completion_tokens_details"), Details) && Details && Details->IsValid()
				&& (*Details)->TryGetNumberField(TEXT("reasoning_tokens"), Number)) ReasoningTokens = FMath::RoundToInt(Number);
		}
		UE_LOG(LogTemp, Display, TEXT("[InfiniteRP] %s timing=%.2fs content_chars=%d reasoning_chars=%d prompt_tokens=%d completion_tokens=%d reasoning_tokens=%d finish=%s"),
			Stage, ElapsedSeconds, ContentChars, ReasoningChars, PromptTokens, CompletionTokens, ReasoningTokens,
			FinishReason.IsEmpty() ? TEXT("unknown") : *FinishReason);
	}

	FString RepairCommonJsonMistakes(const FString& Text)
	{
		FString Result;
		Result.Reserve(Text.Len() + 16);
		bool bInString = false;
		bool bEscaped = false;
		for (int32 Index = 0; Index < Text.Len(); ++Index)
		{
			const TCHAR Ch = Text[Index];
			if (bInString)
			{
				if (bEscaped)
				{
					Result.AppendChar(Ch);
					bEscaped = false;
					continue;
				}
				if (Ch == TEXT('\\'))
				{
					Result.AppendChar(Ch);
					bEscaped = true;
					continue;
				}
				if (Ch == TEXT('"')) bInString = false;
				if (Ch == TEXT('\n')) { Result += TEXT("\\n"); continue; }
				if (Ch == TEXT('\r')) continue;
				if (Ch == TEXT('\t')) { Result += TEXT("\\t"); continue; }
				Result.AppendChar(Ch);
				continue;
			}

			if (Ch == TEXT('"'))
			{
				bInString = true;
				Result.AppendChar(Ch);
				continue;
			}
			if (Ch == TEXT(','))
			{
				int32 Next = Index + 1;
				while (Next < Text.Len() && FChar::IsWhitespace(Text[Next])) ++Next;
				if (Next < Text.Len() && (Text[Next] == TEXT('}') || Text[Next] == TEXT(']'))) continue;
			}
			Result.AppendChar(Ch);
		}
		return Result;
	}

	int32 ApproxPromptTokens(const FString& Text)
	{
		return FMath::Max(1, FMath::CeilToInt(Text.Len() * 0.72f));
	}

	FString ClampPromptSection(const FString& Text, int32 TokenBudget, bool bKeepTail = false)
	{
		if (Text.IsEmpty() || TokenBudget <= 0 || ApproxPromptTokens(Text) <= TokenBudget) return Text;
		const int32 MaxChars = FMath::Max(300, FMath::FloorToInt(TokenBudget / 0.72f));
		return bKeepTail ? TEXT("[较早内容因预算省略]\n") + Text.Right(MaxChars)
			: Text.Left(MaxChars) + TEXT("\n[其余内容因预算省略]");
	}

	bool LooksLikeTokenLimitError(const FString& Body)
	{
		FString Lower = Body;
		Lower.ToLowerInline();
		return Lower.Contains(TEXT("max_tokens")) || Lower.Contains(TEXT("maximum context"))
			|| Lower.Contains(TEXT("context length")) || Lower.Contains(TEXT("too many tokens"))
			|| Lower.Contains(TEXT("token limit"));
	}

	FString GetString(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field, const FString& Default = TEXT(""))
	{
		FString Result = Default;
		if (Object.IsValid()) Object->TryGetStringField(Field, Result);
		return Result;
	}

	int32 GetInt(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field, int32 Default = 0)
	{
		double Result = Default;
		if (Object.IsValid()) Object->TryGetNumberField(Field, Result);
		return FMath::RoundToInt(Result);
	}

	float GetFloat(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field, float Default = 1.f)
	{
		double Result = Default;
		if (Object.IsValid()) Object->TryGetNumberField(Field, Result);
		return static_cast<float>(Result);
	}

	FString SafeRarity(FString Rarity)
	{
		Rarity.ToLowerInline();
		return Rarity == TEXT("uncommon") || Rarity == TEXT("rare") || Rarity == TEXT("legendary")
			? Rarity : TEXT("common");
	}

	FString SanitizeAuthoredFlavor(FString Flavor)
	{
		Flavor.TrimStartAndEndInline();
		Flavor.ReplaceInline(TEXT("\n"), TEXT(" "));
		static const TArray<FString> OutOfWorldMarkers = {
			TEXT("作为common"), TEXT("作为uncommon"), TEXT("作为rare"), TEXT("作为legendary"),
			TEXT("作为 common"), TEXT("作为 uncommon"), TEXT("作为 rare"), TEXT("作为 legendary"),
			TEXT("common卡"), TEXT("uncommon卡"), TEXT("rare卡"), TEXT("legendary卡"),
			TEXT("设计思路"), TEXT("设计说明"), TEXT("强度分析"), TEXT("强度定位"),
			TEXT("费用设计"), TEXT("品阶设计"), TEXT("技能卡定位")
		};
		int32 CutAt = Flavor.Len();
		for (const FString& Marker : OutOfWorldMarkers)
		{
			const int32 Found = Flavor.Find(Marker, ESearchCase::IgnoreCase);
			if (Found != INDEX_NONE) CutAt = FMath::Min(CutAt, Found);
		}
		Flavor = Flavor.Left(CutAt).TrimStartAndEnd();
		while (Flavor.EndsWith(TEXT("，")) || Flavor.EndsWith(TEXT("。"))
			|| Flavor.EndsWith(TEXT("；")) || Flavor.EndsWith(TEXT(",")))
			Flavor.LeftChopInline(1);
		return Flavor.Left(64);
	}

	int32 RarityTier(const FString& Rarity)
	{
		return Rarity == TEXT("legendary") ? 3 : Rarity == TEXT("rare") ? 2
			: Rarity == TEXT("uncommon") ? 1 : 0;
	}

	FString SafeRuntimeArt(FString ArtPath, const FString& Kind)
	{
		ArtPath.TrimStartAndEndInline();
		const bool bAllowedPrefix = ArtPath.StartsWith(TEXT("Art/cards/"))
			|| (Kind == TEXT("relic") && ArtPath.StartsWith(TEXT("Art/relics/")));
		if (!bAllowedPrefix || !ArtPath.EndsWith(TEXT(".png"))) return TEXT("");
		return IFileManager::Get().FileExists(*(FPaths::ProjectContentDir() / ArtPath)) ? ArtPath : TEXT("");
	}

	FString ResolveRuntimeArt(const FString& ProposedArt, const FString& Kind)
	{
		const FString SafeArt = SafeRuntimeArt(ProposedArt, Kind);
		if (!SafeArt.IsEmpty()) return SafeArt;

		// Art is presentation-only. Never discard an otherwise valid authored reward because
		// the model invented a filename. Relics currently share the card-art placeholder pool.
		const FString Fallback = Kind == TEXT("relic")
			? TEXT("Art/cards/bagua_mirror.png")
			: TEXT("Art/cards/jade_talisman.png");
		if (!ProposedArt.TrimStartAndEnd().IsEmpty())
		{
			UE_LOG(LogTemp, Warning, TEXT("[InfiniteRP] invalid authored %s art '%s'; using fallback '%s'"),
				*Kind, *ProposedArt, *Fallback);
		}
		return SafeRuntimeArt(Fallback, Kind);
	}

	FString MakeRuntimeContentId(const TCHAR* Prefix, const FString& Name, int32 Cycle, int32 ChoiceIndex)
	{
		const FString Seed = FString::Printf(TEXT("%s|%s|%d|%d"), Prefix, *Name, Cycle, ChoiceIndex);
		return FString::Printf(TEXT("%s_%08x"), Prefix, FCrc::StrCrc32(*Seed));
	}

	const TArray<FString>& AuthoredCardActions()
	{
		static const TArray<FString> Values = {
			TEXT("damage"), TEXT("damage_all"), TEXT("damage_random"), TEXT("block"), TEXT("draw"), TEXT("discover_draw"),
			TEXT("gain_spirit"), TEXT("lose_spirit"), TEXT("spirit_next_turn"), TEXT("heal"),
			TEXT("apply_status"), TEXT("remove_status"), TEXT("set_status"), TEXT("apply_temp_strength"),
			TEXT("self_damage"), TEXT("discard_random"), TEXT("discard_hand"), TEXT("gain_gold"),
			TEXT("gain_max_hp"), TEXT("cleanse_toxicity"), TEXT("create_card"), TEXT("add_card_to_draw"),
			TEXT("add_card_to_discard"), TEXT("power"), TEXT("damage_per_block"),
			TEXT("damage_per_status"), TEXT("damage_all_per_status"), TEXT("block_per_status"),
			TEXT("amplify_status"), TEXT("damage_all_per_repeat"), TEXT("damage_all_per_basic_gongfa"),
			TEXT("cost_free_basic_gongfa"), TEXT("defense_to_strength"), TEXT("transfer"),
			TEXT("move_cards"), TEXT("copy_cards"), TEXT("modify_card_cost"), TEXT("upgrade_cards"),
			TEXT("transform_cards"), TEXT("shuffle_zone"), TEXT("none")
		};
		return Values;
	}

	const TArray<FString>& AuthoredRelicActions()
	{
		static const TArray<FString> Values = {
			TEXT("damage_random"), TEXT("damage_all"), TEXT("block"), TEXT("draw"),
			TEXT("gain_spirit"), TEXT("heal"), TEXT("apply_status"), TEXT("gain_gold")
		};
		return Values;
	}

	const TArray<FString>& AuthoredStatuses()
	{
		static const TArray<FString> Values = {
			TEXT("strength"), TEXT("dexterity"), TEXT("weak"), TEXT("vulnerable"),
			TEXT("burn"), TEXT("poison"), TEXT("nightmare"), TEXT("temp_strength")
		};
		return Values;
	}

	bool IsSafeScriptToken(const FString& Token)
	{
		if (Token.IsEmpty() || Token.Len() > 32) return false;
		for (const TCHAR Character : Token)
			if (!(FChar::IsAlnum(Character) || Character == TEXT('_'))) return false;
		return true;
	}

	bool IsValidScaleSource(const FString& Source);

	bool IsValidConditionAtom(FString Condition)
	{
		Condition.TrimStartAndEndInline();
		if (Condition.IsEmpty() || Condition == TEXT("always")) return true;
		static const TArray<FString> NumericPrefixes = {
			TEXT("self_hp_below:"), TEXT("self_hp_above:"), TEXT("counter_at_least:"),
			TEXT("hand_size_at_least:"), TEXT("draw_pile_at_most:"), TEXT("discard_pile_at_least:")
		};
		for (const FString& Prefix : NumericPrefixes)
		{
			if (Condition.StartsWith(Prefix))
			{
				const FString Number = Condition.Mid(Prefix.Len());
				return !Number.IsEmpty() && Number.IsNumeric() && FCString::Atoi(*Number) >= 0;
			}
		}
		static const TArray<FString> StatusPrefixes = {
			TEXT("self_has_status:"), TEXT("self_missing_status:"),
			TEXT("target_has_status:"), TEXT("target_missing_status:")
		};
		for (const FString& Prefix : StatusPrefixes)
			if (Condition.StartsWith(Prefix)) return AuthoredStatuses().Contains(Condition.Mid(Prefix.Len()));
		if (Condition.StartsWith(TEXT("event_tag_is:")))
			return IsSafeScriptToken(Condition.Mid(13));
		for (const FString& Prefix : {TEXT("source_at_least:"), TEXT("source_at_most:"), TEXT("source_equals:")})
		{
			if (!Condition.StartsWith(Prefix)) continue;
			const FString Comparison = Condition.Mid(Prefix.Len());
			int32 Separator = INDEX_NONE;
			if (!Comparison.FindLastChar(TEXT('='), Separator) || Separator <= 0) return false;
			const FString Expected = Comparison.Mid(Separator + 1);
			return IsValidScaleSource(Comparison.Left(Separator)) && !Expected.IsEmpty() && Expected.IsNumeric();
		}
		return false;
	}

	bool IsValidCondition(const FString& Condition)
	{
		if (Condition.IsEmpty()) return true;
		TArray<FString> OrGroups;
		Condition.ParseIntoArray(OrGroups, TEXT("||"), true);
		if (OrGroups.Num() == 0 || OrGroups.Num() > 4) return false;
		for (const FString& Group : OrGroups)
		{
			TArray<FString> AndTerms;
			Group.ParseIntoArray(AndTerms, TEXT("&&"), true);
			if (AndTerms.Num() == 0 || AndTerms.Num() > 4) return false;
			for (const FString& Term : AndTerms) if (!IsValidConditionAtom(Term)) return false;
		}
		return true;
	}

	bool IsValidScaleSource(const FString& Source)
	{
		static const TArray<FString> Exact = {
			TEXT(""), TEXT("counter"), TEXT("self_block"), TEXT("missing_hp"), TEXT("hand_size"),
			TEXT("draw_pile"), TEXT("discard_pile"), TEXT("cards_played_this_turn"), TEXT("basic_gongfa_played"),
			TEXT("event_value"), TEXT("self_hp"), TEXT("self_spirit"), TEXT("turn"), TEXT("enemy_count"),
			TEXT("exhaust_pile"), TEXT("last_card_cost"), TEXT("target_hp"), TEXT("target_missing_hp"),
			TEXT("target_block")
		};
		if (Exact.Contains(Source)) return true;
		if (Source.StartsWith(TEXT("var:"))) return IsSafeScriptToken(Source.Mid(4));
		if (Source.StartsWith(TEXT("self_status:"))) return AuthoredStatuses().Contains(Source.Mid(12));
		if (Source.StartsWith(TEXT("target_status:"))) return AuthoredStatuses().Contains(Source.Mid(14));
		return false;
	}

	bool IsValidScriptTrigger(const FString& Trigger)
	{
		static const TArray<FString> Values = {
			TEXT(""), TEXT("on_play"), TEXT("before_gain_block"), TEXT("after_gain_block"),
			TEXT("on_turn_start"), TEXT("on_turn_end"), TEXT("on_card_played"),
			TEXT("on_damage_dealt"), TEXT("on_damage_taken"), TEXT("on_draw"), TEXT("on_discard"),
			TEXT("on_exhaust"), TEXT("on_reshuffle"), TEXT("on_kill"), TEXT("after_spend_spirit"),
			TEXT("before_deal_damage"), TEXT("before_take_damage"), TEXT("on_status_applied")
		};
		return Values.Contains(Trigger);
	}

	bool IsValidScriptSource(const FString& Source)
	{
		return !Source.IsEmpty() && Source != TEXT("counter") && IsValidScaleSource(Source);
	}

	bool IsValidScriptDestination(const FString& Destination)
	{
		static const TArray<FString> Exact = {
			TEXT("self_block"), TEXT("self_hp"), TEXT("self_spirit"), TEXT("target_block"), TEXT("target_hp")
		};
		if (Exact.Contains(Destination)) return true;
		if (Destination.StartsWith(TEXT("var:"))) return IsSafeScriptToken(Destination.Mid(4));
		if (Destination.StartsWith(TEXT("self_status:")))
			return AuthoredStatuses().Contains(Destination.Mid(12));
		if (Destination.StartsWith(TEXT("target_status:")))
			return AuthoredStatuses().Contains(Destination.Mid(14));
		return false;
	}

	int32 AuthoredEffectCap(const FString& Action, int32 Tier, bool bRelic)
	{
		if (Action == TEXT("none") || Action == TEXT("discard_hand") || Action == TEXT("cleanse_toxicity")
			|| Action == TEXT("cost_free_basic_gongfa") || Action == TEXT("transfer")
			|| Action == TEXT("shuffle_zone")) return 0;
		if (Action == TEXT("move_cards") || Action == TEXT("copy_cards") || Action == TEXT("modify_card_cost")
			|| Action == TEXT("upgrade_cards") || Action == TEXT("transform_cards")) return 3;
		if (Action == TEXT("damage") || Action == TEXT("damage_per_block") || Action == TEXT("damage_per_status"))
			return 7 + Tier * 4;
		if (Action == TEXT("damage_random")) return bRelic ? 2 + Tier * 2 : 6 + Tier * 3;
		if (Action == TEXT("damage_all") || Action == TEXT("damage_all_per_status")
			|| Action == TEXT("damage_all_per_repeat") || Action == TEXT("damage_all_per_basic_gongfa"))
			return bRelic ? 2 + Tier * 2 : 4 + Tier * 2;
		if (Action == TEXT("block") || Action == TEXT("block_per_status")) return bRelic ? 3 + Tier * 2 : 7 + Tier * 3;
		if (Action == TEXT("heal")) return bRelic ? 2 + Tier : 4 + Tier * 3;
		if (Action == TEXT("gain_gold")) return 3 + Tier * 2;
		if (Action == TEXT("self_damage")) return 10;
		if (Action == TEXT("gain_max_hp")) return 2 + Tier * 2;
		if (Action == TEXT("discard_random")) return Tier >= 2 ? 2 : 1;
		if (Action == TEXT("draw")) return Tier >= 2 && !bRelic ? 2 : 1;
		if (Action == TEXT("discover_draw")) return bRelic ? 0 : 2 + Tier;
		if (Action == TEXT("gain_spirit") || Action == TEXT("lose_spirit") || Action == TEXT("spirit_next_turn")) return 1;
		if (Action == TEXT("create_card") || Action == TEXT("add_card_to_draw") || Action == TEXT("add_card_to_discard")) return 1;
		if (Action == TEXT("amplify_status")) return 2;
		if (Action == TEXT("defense_to_strength")) return 2 + Tier;
		return 1;
	}

	bool IsKnownCardId(const FString& CardId)
	{
		if (CardId.IsEmpty()) return false;
		TArray<FCardData> Cards;
		FString Error;
		if (!UGameDataLibrary::LoadCards(Cards, Error)) return false;
		return Cards.ContainsByPredicate([&CardId](const FCardData& Card) { return Card.Id == CardId; });
	}

	bool ResolveKnownCardReference(const FString& Reference, FString& OutCardId)
	{
		if (Reference.IsEmpty()) return false;
		TArray<FCardData> Cards;
		FString Error;
		if (!UGameDataLibrary::LoadCards(Cards, Error)) return false;
		const FCardData* Match = Cards.FindByPredicate([&Reference](const FCardData& Card)
		{
			return Card.Id == Reference || Card.Name == Reference;
		});
		if (!Match) return false;
		OutCardId = Match->Id;
		return true;
	}

	bool ValidateAuthoredEffect(const TSharedPtr<FJsonObject>& Object, const FString& Rarity, bool bRelic,
		FString& OutError)
	{
		if (!Object.IsValid()) { OutError = TEXT("effect 必须是对象"); return false; }
		FString Action = GetString(Object, TEXT("action"));
		Action.ToLowerInline();
		if (!(bRelic ? AuthoredRelicActions().Contains(Action) : AuthoredCardActions().Contains(Action)))
		{
			OutError = FString::Printf(TEXT("不支持的 effect.action=%s"), *Action);
			return false;
		}
		static const TArray<FString> Targets = {TEXT("enemy"), TEXT("self"), TEXT("all_enemies"), TEXT("random_enemy")};
		const FString Target = GetString(Object, TEXT("target"), TEXT("enemy"));
		if (!Targets.Contains(Target)) { OutError = FString::Printf(TEXT("不支持的 effect.target=%s"), *Target); return false; }
		const int32 Tier = RarityTier(Rarity);
		const FString Trigger = GetString(Object, TEXT("trigger"), TEXT("on_play"));
		if (!IsValidScriptTrigger(Trigger))
		{
			OutError = FString::Printf(TEXT("不支持的 effect.trigger=%s"), *Trigger);
			return false;
		}
		const FString Duration = GetString(Object, TEXT("duration"),
			Trigger == TEXT("on_play") || Trigger.IsEmpty() ? TEXT("instant") : TEXT("combat"));
		if (Duration != TEXT("instant") && Duration != TEXT("turn") && Duration != TEXT("combat"))
		{
			OutError = FString::Printf(TEXT("不支持的 effect.duration=%s"), *Duration);
			return false;
		}
		const int32 MaxTriggers = GetInt(Object, TEXT("max_triggers"), 0);
		if (MaxTriggers < 0 || MaxTriggers > 99)
		{
			OutError = TEXT("effect.max_triggers 必须在0~99");
			return false;
		}
		const int32 Cap = AuthoredEffectCap(Action, Tier, bRelic);
		const int32 Value = GetInt(Object, TEXT("value"), Cap == 0 ? 0 : 1);
		if (Action == TEXT("transfer") && (Value < -10 || Value > 10))
		{
			OutError = FString::Printf(TEXT("transfer.value=%d 超出安全范围 -10~10"), Value);
			return false;
		}
		if (Action != TEXT("transfer") && (Value < (Cap == 0 ? 0 : 1) || Value > Cap))
		{
			OutError = FString::Printf(TEXT("%s.value=%d 超出该品阶允许范围 %d~%d"), *Action, Value,
				Cap == 0 ? 0 : 1, Cap);
			return false;
		}
		const int32 Times = GetInt(Object, TEXT("times"), 1);
		const int32 MaxTimes = (Action == TEXT("damage") || Action == TEXT("damage_random")) ? 1 + Tier : 1;
		if (Times < 1 || Times > MaxTimes)
		{
			OutError = FString::Printf(TEXT("%s.times=%d 超出允许范围 1~%d"), *Action, Times, MaxTimes);
			return false;
		}
		const float Chance = GetFloat(Object, TEXT("chance"), 1.f);
		if (bRelic && !FMath::IsNearlyEqual(Chance, 1.f)) { OutError = TEXT("原创法宝 effect.chance 必须为 1"); return false; }
		if (!bRelic && (Chance < 0.25f || Chance > 1.f)) { OutError = TEXT("effect.chance 必须在 0.25~1"); return false; }
		const bool bNeedsStatus = Action == TEXT("apply_status") || Action == TEXT("remove_status")
			|| Action == TEXT("set_status") || Action == TEXT("apply_temp_strength")
			|| Action == TEXT("damage_per_status") || Action == TEXT("damage_all_per_status")
			|| Action == TEXT("block_per_status") || Action == TEXT("amplify_status");
		const FString Status = GetString(Object, TEXT("status"));
		if (bNeedsStatus && !AuthoredStatuses().Contains(Status))
		{
			OutError = FString::Printf(TEXT("%s 需要合法 status"), *Action);
			return false;
		}
		if (bRelic && Action == TEXT("apply_status") && Status != TEXT("strength") && Status != TEXT("dexterity"))
		{
			OutError = TEXT("原创法宝 apply_status 只允许自身 strength/dexterity");
			return false;
		}
		const int32 Stacks = GetInt(Object, TEXT("stacks"), bNeedsStatus ? 1 : 0);
		if (bNeedsStatus && (Stacks < 1 || Stacks > 1 + Tier))
		{
			OutError = FString::Printf(TEXT("%s.stacks=%d 超出该品阶允许范围 1~%d"), *Action, Stacks, 1 + Tier);
			return false;
		}
		if (Action == TEXT("modify_card_cost") && (Stacks < -3 || Stacks > 3 || Stacks == 0))
		{
			OutError = TEXT("modify_card_cost.stacks 必须是 -3~-1 或 1~3 的费用变化量");
			return false;
		}
		const FString Condition = GetString(Object, TEXT("condition"));
		if (!IsValidCondition(Condition)) { OutError = FString::Printf(TEXT("不支持的 condition=%s"), *Condition); return false; }
		const FString ScaleBy = GetString(Object, TEXT("scale_by"));
		if (!IsValidScaleSource(ScaleBy)) { OutError = FString::Printf(TEXT("不支持的 scale_by=%s"), *ScaleBy); return false; }
		if (!ScaleBy.IsEmpty())
		{
			const int32 Factor = GetInt(Object, TEXT("scale_factor"));
			const int32 Divisor = GetInt(Object, TEXT("scale_divisor"), 1);
			if (Factor < 1 || Factor > 1 + Tier || Divisor < 1 || Divisor > 10)
			{
				OutError = FString::Printf(TEXT("动态倍率须满足 scale_factor=1~%d、scale_divisor=1~10"), 1 + Tier);
				return false;
			}
		}
		if (Action == TEXT("transfer"))
		{
			const FString Source = GetString(Object, TEXT("source"));
			const FString Destination = GetString(Object, TEXT("destination"));
			if (!IsValidScriptSource(Source))
			{
				OutError = FString::Printf(TEXT("transfer.source=%s 不可读取"), *Source);
				return false;
			}
			if (!IsValidScriptDestination(Destination))
			{
				OutError = FString::Printf(TEXT("transfer.destination=%s 不可写入"), *Destination);
				return false;
			}
			if (Source == Destination)
			{
				OutError = TEXT("transfer 的来源和目标不能相同");
				return false;
			}
			static const TArray<FString> WriteModes = {TEXT("add"), TEXT("set"), TEXT("min"), TEXT("max"), TEXT("multiply")};
			const FString WriteMode = GetString(Object, TEXT("write_mode"), TEXT("add"));
			if (!WriteModes.Contains(WriteMode))
			{
				OutError = FString::Printf(TEXT("transfer.write_mode=%s 不受支持"), *WriteMode);
				return false;
			}
		}
		static const TArray<FString> ZoneActions = {
			TEXT("move_cards"), TEXT("copy_cards"), TEXT("modify_card_cost"), TEXT("upgrade_cards"),
			TEXT("transform_cards"), TEXT("shuffle_zone")
		};
		if (ZoneActions.Contains(Action))
		{
			static const TArray<FString> SourceZones = {
				TEXT("hand"), TEXT("draw"), TEXT("discard"), TEXT("exhaust"), TEXT("last_played")
			};
			static const TArray<FString> DestinationZones = {
				TEXT("hand"), TEXT("draw"), TEXT("draw_top"), TEXT("draw_random"), TEXT("discard"), TEXT("exhaust")
			};
			const FString Source = GetString(Object, TEXT("source"));
			const FString Destination = GetString(Object, TEXT("destination"));
			const FString Selector = GetString(Object, TEXT("param"), TEXT("random"));
			if (!SourceZones.Contains(Source))
			{
				OutError = FString::Printf(TEXT("%s.source=%s 不是可读取牌区"), *Action, *Source);
				return false;
			}
			const bool bSelectorValid = Selector == TEXT("any") || Selector == TEXT("random")
				|| Selector == TEXT("highest_cost") || Selector == TEXT("lowest_cost")
				|| Selector == TEXT("upgraded") || Selector == TEXT("non_upgraded")
				|| Selector == TEXT("retained") || Selector == TEXT("exhausting")
				|| (Selector.StartsWith(TEXT("type:")) && IsSafeScriptToken(Selector.Mid(5)))
				|| (Selector.StartsWith(TEXT("rarity:")) && IsSafeScriptToken(Selector.Mid(7)))
				|| (Selector.StartsWith(TEXT("cost_at_most:")) && Selector.Mid(13).IsNumeric());
			if (!bSelectorValid)
			{
				OutError = FString::Printf(TEXT("%s.param=%s 不是合法选牌器"), *Action, *Selector);
				return false;
			}
			if (Action == TEXT("move_cards") || Action == TEXT("copy_cards"))
			{
				if (!DestinationZones.Contains(Destination) || (Action == TEXT("move_cards") && Source == Destination))
				{
					OutError = FString::Printf(TEXT("%s.destination=%s 非法或与来源相同"), *Action, *Destination);
					return false;
				}
			}
			if (Action == TEXT("transform_cards"))
			{
				FString CardId;
				if (!ResolveKnownCardReference(Destination, CardId))
				{
					OutError = FString::Printf(TEXT("transform_cards.destination=%s 不是已有卡牌名称或ID"), *Destination);
					return false;
				}
			}
		}
		if (Action == TEXT("create_card") || Action == TEXT("add_card_to_draw") || Action == TEXT("add_card_to_discard"))
		{
			const FString Param = GetString(Object, TEXT("param"));
			FString CardId;
			if (!ResolveKnownCardReference(Param, CardId))
			{
				OutError = FString::Printf(TEXT("%s.param=%s 不是目录中的已有卡牌名称或ID"), *Action, *Param);
				return false;
			}
		}
		static const TArray<FString> PowerIds = {
			TEXT("counter_stance"), TEXT("defense_to_strength"), TEXT("double_strength"), TEXT("flow_sutra"),
			TEXT("heart_clear"), TEXT("immortal_draw"), TEXT("one_sword_return"), TEXT("sword_art_flow"),
			TEXT("sword_gang"), TEXT("sword_intent"), TEXT("sword_relic_inscription"), TEXT("thorns_aura")
		};
		if (Action == TEXT("power") && !PowerIds.Contains(Status))
		{
			OutError = FString::Printf(TEXT("power.status=%s 不是已实现的功法 ID"), *Status);
			return false;
		}
		return true;
	}

	bool ParseSafeEffect(const TSharedPtr<FJsonObject>& Object, const FString& Rarity, bool bRelic,
		FCardEffect& OutEffect)
	{
		FString ValidationError;
		if (!ValidateAuthoredEffect(Object, Rarity, bRelic, ValidationError)) return false;
		OutEffect.Action = GetString(Object, TEXT("action"));
		OutEffect.Action.ToLowerInline();
		OutEffect.Trigger = GetString(Object, TEXT("trigger"), TEXT("on_play"));
		OutEffect.Duration = GetString(Object, TEXT("duration"),
			OutEffect.Trigger == TEXT("on_play") || OutEffect.Trigger.IsEmpty() ? TEXT("instant") : TEXT("combat"));
		OutEffect.Source = GetString(Object, TEXT("source"));
		OutEffect.Destination = GetString(Object, TEXT("destination"));
		OutEffect.WriteMode = GetString(Object, TEXT("write_mode"), TEXT("add"));
		Object->TryGetBoolField(TEXT("consume_source"), OutEffect.bConsumeSource);
		OutEffect.MaxTriggers = GetInt(Object, TEXT("max_triggers"), 0);
		const int32 Cap = AuthoredEffectCap(OutEffect.Action, RarityTier(Rarity), bRelic);
		OutEffect.Value = GetInt(Object, TEXT("value"), Cap == 0 ? 0 : 1);
		OutEffect.Times = GetInt(Object, TEXT("times"), 1);
		OutEffect.Target = GetString(Object, TEXT("target"), TEXT("enemy"));
		OutEffect.StatusId = GetString(Object, TEXT("status"));
		OutEffect.StatusStacks = GetInt(Object, TEXT("stacks"), 0);
		OutEffect.Param = GetString(Object, TEXT("param"));
		OutEffect.Condition = GetString(Object, TEXT("condition"));
		OutEffect.ScaleBy = GetString(Object, TEXT("scale_by"));
		OutEffect.ScaleFactor = GetInt(Object, TEXT("scale_factor"));
		OutEffect.ScaleDivisor = GetInt(Object, TEXT("scale_divisor"), 1);
		OutEffect.Chance = bRelic ? 1.f : GetFloat(Object, TEXT("chance"), 1.f);
		if (OutEffect.Action == TEXT("create_card") || OutEffect.Action == TEXT("add_card_to_draw")
			|| OutEffect.Action == TEXT("add_card_to_discard"))
		{
			FString CardId;
			if (ResolveKnownCardReference(OutEffect.Param, CardId)) OutEffect.Param = CardId;
		}
		if (OutEffect.Action == TEXT("transform_cards"))
		{
			FString CardId;
			if (ResolveKnownCardReference(OutEffect.Destination, CardId)) OutEffect.Destination = CardId;
		}
		if (OutEffect.Action == TEXT("block") || OutEffect.Action == TEXT("draw") || OutEffect.Action == TEXT("discover_draw") || OutEffect.Action == TEXT("gain_spirit")
			|| OutEffect.Action == TEXT("lose_spirit") || OutEffect.Action == TEXT("spirit_next_turn")
			|| OutEffect.Action == TEXT("heal") || OutEffect.Action == TEXT("self_damage")
			|| OutEffect.Action == TEXT("discard_random") || OutEffect.Action == TEXT("discard_hand")
			|| OutEffect.Action == TEXT("gain_gold") || OutEffect.Action == TEXT("gain_max_hp")
			|| OutEffect.Action == TEXT("cleanse_toxicity") || OutEffect.Action == TEXT("apply_temp_strength")
			|| OutEffect.Action == TEXT("cost_free_basic_gongfa") || OutEffect.Action == TEXT("defense_to_strength"))
			OutEffect.Target = TEXT("self");
		if (OutEffect.Action == TEXT("damage_random")) OutEffect.Target = TEXT("random_enemy");
		if (OutEffect.Action == TEXT("damage_all") || OutEffect.Action.StartsWith(TEXT("damage_all_")))
			OutEffect.Target = TEXT("all_enemies");
		if (bRelic && OutEffect.Action == TEXT("apply_status")) OutEffect.Target = TEXT("self");
		return true;
	}

	int32 NormalizeForgedCardForRuntime(const TSharedPtr<FJsonObject>& Card)
	{
		if (!Card.IsValid()) return 0;
		int32 Repairs = 0;
		FString Rarity = GetString(Card, TEXT("rarity"), TEXT("common"));
		Rarity.ToLowerInline();
		const FString SafeCardRarity = SafeRarity(Rarity);
		if (Rarity != SafeCardRarity)
		{
			Card->SetStringField(TEXT("rarity"), SafeCardRarity);
			++Repairs;
		}
		else if (!Card->HasField(TEXT("rarity")))
		{
			Card->SetStringField(TEXT("rarity"), SafeCardRarity);
			++Repairs;
		}
		FString Type = GetString(Card, TEXT("type"));
		const FString OriginalType = Type;
		Type.ToLowerInline();
		if (Type != OriginalType)
		{
			Card->SetStringField(TEXT("type"), Type);
			++Repairs;
		}
		double RawCost = 1.0;
		if (!Card->TryGetNumberField(TEXT("cost"), RawCost) || RawCost < 0.0 || RawCost > 3.0)
		{
			Card->SetNumberField(TEXT("cost"), FMath::Clamp(FMath::RoundToInt(RawCost), 0, 3));
			++Repairs;
		}

		auto NormalizeEffects = [&Repairs, &SafeCardRarity](const TArray<TSharedPtr<FJsonValue>>* Effects)
		{
			if (!Effects) return;
			const int32 Tier = RarityTier(SafeCardRarity);
			for (const TSharedPtr<FJsonValue>& EffectValue : *Effects)
			{
				const TSharedPtr<FJsonObject> Effect = EffectValue.IsValid() ? EffectValue->AsObject() : nullptr;
				if (!Effect.IsValid()) continue;
				FString Action = GetString(Effect, TEXT("action"));
				const FString OriginalAction = Action;
				Action.ToLowerInline();
				if (Action != OriginalAction)
				{
					Effect->SetStringField(TEXT("action"), Action);
					++Repairs;
				}
				if (!AuthoredCardActions().Contains(Action)) continue;

				const int32 Cap = AuthoredEffectCap(Action, Tier, false);
				double RawValue = Cap == 0 ? 0.0 : 1.0;
				const bool bHasValue = Effect->TryGetNumberField(TEXT("value"), RawValue);
				int32 Value = FMath::RoundToInt(RawValue);
				if (Action == TEXT("transfer")) Value = FMath::Clamp(Value, -10, 10);
				else if (Cap > 0)
				{
					int32 Suggested = 1;
					if (Action == TEXT("damage")) Suggested = 6;
					else if (Action == TEXT("damage_random") || Action == TEXT("block")) Suggested = 5;
					else if (Action == TEXT("damage_all") || Action == TEXT("heal")) Suggested = 4;
					else if (Action == TEXT("discover_draw")) Suggested = 2;
					if (!bHasValue || Value <= 0) Value = FMath::Min(Suggested, Cap);
					else Value = FMath::Clamp(Value, 1, Cap);
				}
				else Value = 0;
				if (!bHasValue || Value != FMath::RoundToInt(RawValue))
				{
					Effect->SetNumberField(TEXT("value"), Value);
					++Repairs;
				}

				const bool bSelfAction = Action == TEXT("block") || Action == TEXT("draw")
					|| Action == TEXT("discover_draw") || Action == TEXT("gain_spirit")
					|| Action == TEXT("lose_spirit") || Action == TEXT("spirit_next_turn")
					|| Action == TEXT("heal") || Action == TEXT("self_damage")
					|| Action == TEXT("discard_random") || Action == TEXT("discard_hand")
					|| Action == TEXT("gain_gold") || Action == TEXT("gain_max_hp")
					|| Action == TEXT("cleanse_toxicity") || Action == TEXT("apply_temp_strength")
					|| Action == TEXT("cost_free_basic_gongfa") || Action == TEXT("defense_to_strength")
					|| Action == TEXT("move_cards") || Action == TEXT("copy_cards")
					|| Action == TEXT("modify_card_cost") || Action == TEXT("upgrade_cards")
					|| Action == TEXT("transform_cards") || Action == TEXT("shuffle_zone")
					|| Action == TEXT("transfer");
				FString Target = GetString(Effect, TEXT("target"));
				static const TArray<FString> Targets = {TEXT("enemy"), TEXT("self"), TEXT("all_enemies"), TEXT("random_enemy")};
				if (!Targets.Contains(Target))
				{
					Target = Action == TEXT("damage_random") ? TEXT("random_enemy")
						: (Action == TEXT("damage_all") || Action.StartsWith(TEXT("damage_all_"))) ? TEXT("all_enemies")
						: bSelfAction ? TEXT("self") : TEXT("enemy");
					Effect->SetStringField(TEXT("target"), Target);
					++Repairs;
				}

				double RawTimes = 1.0;
				if (!Effect->TryGetNumberField(TEXT("times"), RawTimes) || RawTimes < 1.0
					|| RawTimes > ((Action == TEXT("damage") || Action == TEXT("damage_random")) ? 1 + Tier : 1))
				{
					Effect->SetNumberField(TEXT("times"), FMath::Clamp(FMath::RoundToInt(RawTimes), 1,
						(Action == TEXT("damage") || Action == TEXT("damage_random")) ? 1 + Tier : 1));
					++Repairs;
				}
				double RawChance = 1.0;
				if (!Effect->TryGetNumberField(TEXT("chance"), RawChance) || RawChance < 0.25 || RawChance > 1.0)
				{
					Effect->SetNumberField(TEXT("chance"), FMath::Clamp(RawChance, 0.25, 1.0));
					++Repairs;
				}

				const bool bNeedsStatus = Action == TEXT("apply_status") || Action == TEXT("remove_status")
					|| Action == TEXT("set_status") || Action == TEXT("apply_temp_strength")
					|| Action == TEXT("damage_per_status") || Action == TEXT("damage_all_per_status")
					|| Action == TEXT("block_per_status") || Action == TEXT("amplify_status");
				if (bNeedsStatus)
				{
					FString Status = GetString(Effect, TEXT("status"));
					if (!AuthoredStatuses().Contains(Status))
					{
						Effect->SetStringField(TEXT("status"), Target == TEXT("self") ? TEXT("strength") : TEXT("poison"));
						++Repairs;
					}
					double RawStacks = 1.0;
					if (!Effect->TryGetNumberField(TEXT("stacks"), RawStacks) || RawStacks < 1.0 || RawStacks > 1 + Tier)
					{
						Effect->SetNumberField(TEXT("stacks"), FMath::Clamp(FMath::RoundToInt(RawStacks), 1, 1 + Tier));
						++Repairs;
					}
				}
				if (Action == TEXT("modify_card_cost"))
				{
					double RawStacks = -1.0;
					if (!Effect->TryGetNumberField(TEXT("stacks"), RawStacks) || FMath::IsNearlyZero(RawStacks)
						|| RawStacks < -3.0 || RawStacks > 3.0)
					{
						Effect->SetNumberField(TEXT("stacks"), -1);
						++Repairs;
					}
				}
				const FString ScaleBy = GetString(Effect, TEXT("scale_by"));
				if (!ScaleBy.IsEmpty() && !IsValidScaleSource(ScaleBy))
				{
					Effect->RemoveField(TEXT("scale_by"));
					Effect->RemoveField(TEXT("scale_factor"));
					Effect->RemoveField(TEXT("scale_divisor"));
					++Repairs;
				}
				else if (!ScaleBy.IsEmpty())
				{
					double Factor = 1.0;
					double Divisor = 1.0;
					if (!Effect->TryGetNumberField(TEXT("scale_factor"), Factor) || Factor < 1.0 || Factor > 1 + Tier)
					{
						Effect->SetNumberField(TEXT("scale_factor"), FMath::Clamp(FMath::RoundToInt(Factor), 1, 1 + Tier));
						++Repairs;
					}
					if (!Effect->TryGetNumberField(TEXT("scale_divisor"), Divisor) || Divisor < 1.0 || Divisor > 10.0)
					{
						Effect->SetNumberField(TEXT("scale_divisor"), FMath::Clamp(FMath::RoundToInt(Divisor), 1, 10));
						++Repairs;
					}
				}
				const FString Condition = GetString(Effect, TEXT("condition"));
				if (!Condition.IsEmpty() && !IsValidCondition(Condition))
				{
					// A thematic qualifier is optional precision, not a reason to discard an
					// otherwise executable card. The forge prompt teaches the finite condition
					// vocabulary; this repair keeps provider variation playable.
					Effect->RemoveField(TEXT("condition"));
					++Repairs;
				}
			}
		};

		const TArray<TSharedPtr<FJsonValue>>* Effects = nullptr;
		if (Card->TryGetArrayField(TEXT("effects"), Effects)) NormalizeEffects(Effects);
		const TSharedPtr<FJsonObject>* Upgrade = nullptr;
		if (Card->TryGetObjectField(TEXT("upgrade"), Upgrade) && Upgrade && Upgrade->IsValid())
		{
			const TArray<TSharedPtr<FJsonValue>>* UpgradeEffects = nullptr;
			if ((*Upgrade)->TryGetArrayField(TEXT("effects"), UpgradeEffects)) NormalizeEffects(UpgradeEffects);
		}
		return Repairs;
	}

	FString SafeEffectDescription(const FCardEffect& Effect)
	{
		auto StatusName = [](const FString& StatusId) -> FString
		{
			static const TMap<FString, FString> Names = {
				{TEXT("burn"), TEXT("灼烧")}, {TEXT("poison"), TEXT("中毒")},
				{TEXT("weak"), TEXT("虚弱")}, {TEXT("vulnerable"), TEXT("易伤")},
				{TEXT("strength"), TEXT("力量")}, {TEXT("dexterity"), TEXT("敏捷")},
				{TEXT("nightmare"), TEXT("梦魇")}, {TEXT("temp_strength"), TEXT("临时力量")}
			};
			if (const FString* Name = Names.Find(StatusId)) return *Name;
			return TEXT("特殊状态");
		};
		auto ScriptValueName = [&StatusName](const FString& Source) -> FString
		{
			static const TMap<FString, FString> Names = {
				{TEXT("counter"), TEXT("当前计数")}, {TEXT("self_block"), TEXT("当前罡气")},
				{TEXT("missing_hp"), TEXT("已损气血")}, {TEXT("hand_size"), TEXT("手牌数")},
				{TEXT("draw_pile"), TEXT("抽牌堆牌数")}, {TEXT("discard_pile"), TEXT("弃牌堆牌数")},
				{TEXT("cards_played_this_turn"), TEXT("本回合已打出牌数")},
				{TEXT("basic_gongfa_played"), TEXT("本场已打出的基础牌与功法牌数")},
				{TEXT("event_value"), TEXT("本次触发数值")}, {TEXT("self_hp"), TEXT("当前气血")},
				{TEXT("self_spirit"), TEXT("当前灵力")}, {TEXT("turn"), TEXT("当前回合数")},
				{TEXT("enemy_count"), TEXT("敌人数")}, {TEXT("exhaust_pile"), TEXT("消耗牌堆牌数")},
				{TEXT("last_card_cost"), TEXT("上一张牌的费用")}, {TEXT("target_hp"), TEXT("目标气血")},
				{TEXT("target_missing_hp"), TEXT("目标已损气血")}, {TEXT("target_block"), TEXT("目标罡气")}
			};
			if (const FString* Name = Names.Find(Source)) return *Name;
			if (Source.StartsWith(TEXT("self_status:"))) return TEXT("自身") + StatusName(Source.Mid(12)) + TEXT("层数");
			if (Source.StartsWith(TEXT("target_status:"))) return TEXT("目标") + StatusName(Source.Mid(14)) + TEXT("层数");
			if (Source.StartsWith(TEXT("var:")))
			{
				static const TMap<FString, FString> VariableNames = {
					{TEXT("ink"), TEXT("墨痕")}, {TEXT("void"), TEXT("虚无")},
					{TEXT("flame"), TEXT("火种")}, {TEXT("mark"), TEXT("印记")},
					{TEXT("charge"), TEXT("蓄势")}, {TEXT("momentum"), TEXT("势")},
					{TEXT("echo"), TEXT("回响")}, {TEXT("blood"), TEXT("血契")}
				};
				if (const FString* Name = VariableNames.Find(Source.Mid(4))) return *Name + TEXT("计数");
				return TEXT("专属计数");
			}
			return TEXT("相关数值");
		};
		auto EventTagName = [](const FString& Tag) -> FString
		{
			static const TMap<FString, FString> Names = {
				{TEXT("spell"), TEXT("法术牌")}, {TEXT("sword"), TEXT("剑诀牌")},
				{TEXT("body"), TEXT("炼体牌")}, {TEXT("talisman"), TEXT("符箓牌")},
				{TEXT("skill"), TEXT("技艺牌")}, {TEXT("gongfa"), TEXT("功法牌")},
				{TEXT("basic"), TEXT("基础牌")}, {TEXT("damage"), TEXT("伤害")}
			};
			if (const FString* Name = Names.Find(Tag)) return *Name;
			return TEXT("指定类型的牌");
		};
		auto ConditionAtom = [&StatusName, &ScriptValueName, &EventTagName](FString Atom) -> FString
		{
			Atom.TrimStartAndEndInline();
			auto Suffix = [&Atom](const FString& Prefix) { return Atom.Mid(Prefix.Len()); };
			if (Atom.StartsWith(TEXT("self_hp_below:"))) return TEXT("自身气血低于") + Suffix(TEXT("self_hp_below:")) + TEXT("%");
			if (Atom.StartsWith(TEXT("self_hp_above:"))) return TEXT("自身气血高于") + Suffix(TEXT("self_hp_above:")) + TEXT("%");
			if (Atom.StartsWith(TEXT("counter_at_least:"))) return TEXT("当前计数至少为") + Suffix(TEXT("counter_at_least:"));
			if (Atom.StartsWith(TEXT("hand_size_at_least:"))) return TEXT("手牌至少有") + Suffix(TEXT("hand_size_at_least:")) + TEXT("张");
			if (Atom.StartsWith(TEXT("draw_pile_at_most:"))) return TEXT("抽牌堆至多有") + Suffix(TEXT("draw_pile_at_most:")) + TEXT("张牌");
			if (Atom.StartsWith(TEXT("discard_pile_at_least:"))) return TEXT("弃牌堆至少有") + Suffix(TEXT("discard_pile_at_least:")) + TEXT("张牌");
			if (Atom.StartsWith(TEXT("self_has_status:"))) return TEXT("自身拥有") + StatusName(Suffix(TEXT("self_has_status:")));
			if (Atom.StartsWith(TEXT("self_missing_status:"))) return TEXT("自身没有") + StatusName(Suffix(TEXT("self_missing_status:")));
			if (Atom.StartsWith(TEXT("target_has_status:"))) return TEXT("目标拥有") + StatusName(Suffix(TEXT("target_has_status:")));
			if (Atom.StartsWith(TEXT("target_missing_status:"))) return TEXT("目标没有") + StatusName(Suffix(TEXT("target_missing_status:")));
			if (Atom.StartsWith(TEXT("event_tag_is:"))) return TEXT("本次触发来自") + EventTagName(Suffix(TEXT("event_tag_is:")));
			for (const FString& Prefix : {TEXT("source_at_least:"), TEXT("source_at_most:"), TEXT("source_equals:")})
			{
				if (!Atom.StartsWith(Prefix)) continue;
				const FString Comparison = Atom.Mid(Prefix.Len());
				int32 Separator = INDEX_NONE;
				if (!Comparison.FindLastChar(TEXT('='), Separator)) break;
				const FString Relation = Prefix == TEXT("source_at_least:") ? TEXT("至少为")
					: Prefix == TEXT("source_at_most:") ? TEXT("至多为") : TEXT("等于");
				return ScriptValueName(Comparison.Left(Separator)) + Relation + Comparison.Mid(Separator + 1);
			}
			return TEXT("特殊条件满足");
		};
		auto ConditionName = [&ConditionAtom](const FString& Condition) -> FString
		{
			TArray<FString> OrGroups;
			Condition.ParseIntoArray(OrGroups, TEXT("||"), true);
			TArray<FString> LocalizedGroups;
			for (const FString& Group : OrGroups)
			{
				TArray<FString> AndTerms;
				Group.ParseIntoArray(AndTerms, TEXT("&&"), true);
				TArray<FString> LocalizedTerms;
				for (const FString& Term : AndTerms) LocalizedTerms.Add(ConditionAtom(Term));
				LocalizedGroups.Add(FString::Join(LocalizedTerms, TEXT("且")));
			}
			return FString::Join(LocalizedGroups, TEXT("，或"));
		};
		auto ZoneName = [](const FString& Zone) -> FString
		{
			static const TMap<FString, FString> Names = {
				{TEXT("hand"), TEXT("手牌")}, {TEXT("draw"), TEXT("抽牌堆")},
				{TEXT("draw_top"), TEXT("抽牌堆顶")}, {TEXT("draw_random"), TEXT("抽牌堆随机位置")},
				{TEXT("discard"), TEXT("弃牌堆")}, {TEXT("exhaust"), TEXT("消耗牌堆")},
				{TEXT("last_played"), TEXT("上一张打出的牌")}
			};
			if (const FString* Name = Names.Find(Zone)) return *Name;
			return TEXT("指定牌区");
		};
		auto SelectorName = [&StatusName](const FString& Selector) -> FString
		{
			static const TMap<FString, FString> Names = {
				{TEXT("any"), TEXT("任意牌")}, {TEXT("random"), TEXT("随机牌")},
				{TEXT("highest_cost"), TEXT("费用最高的牌")}, {TEXT("lowest_cost"), TEXT("费用最低的牌")},
				{TEXT("upgraded"), TEXT("已升级牌")}, {TEXT("non_upgraded"), TEXT("未升级牌")},
				{TEXT("retained"), TEXT("保留牌")}, {TEXT("exhausting"), TEXT("消耗牌")}
			};
			if (const FString* Name = Names.Find(Selector)) return *Name;
			if (Selector.StartsWith(TEXT("type:")))
			{
				static const TMap<FString, FString> Types = {
					{TEXT("spell"), TEXT("法术牌")}, {TEXT("sword"), TEXT("剑诀牌")},
					{TEXT("body"), TEXT("炼体牌")}, {TEXT("talisman"), TEXT("符箓牌")},
					{TEXT("skill"), TEXT("技艺牌")}, {TEXT("gongfa"), TEXT("功法牌")}
				};
				if (const FString* Name = Types.Find(Selector.Mid(5))) return *Name;
			}
			if (Selector.StartsWith(TEXT("cost_at_most:"))) return TEXT("费用不高于") + Selector.Mid(13) + TEXT("的牌");
			if (Selector.StartsWith(TEXT("rarity:"))) return TEXT("指定品阶的牌");
			return TEXT("符合条件的牌");
		};
		FString Text;
		if (Effect.Action == TEXT("damage")) Text = FString::Printf(TEXT("对一名敌人造成%d点伤害"), Effect.Value);
		else if (Effect.Action == TEXT("damage_random")) Text = FString::Printf(TEXT("对随机敌人造成%d点伤害"), Effect.Value);
		else if (Effect.Action == TEXT("damage_all")) Text = FString::Printf(TEXT("对所有敌人造成%d点伤害"), Effect.Value);
		else if (Effect.Action == TEXT("damage_per_block")) Text = FString::Printf(TEXT("造成%d点加当前罡气的伤害"), Effect.Value);
		else if (Effect.Action == TEXT("damage_per_status")) Text = FString::Printf(TEXT("造成%d点伤害，每层%s额外+%d"), Effect.Value, *StatusName(Effect.StatusId), Effect.StatusStacks);
		else if (Effect.Action == TEXT("damage_all_per_status")) Text = FString::Printf(TEXT("对所有敌人造成%d点伤害，每层%s额外+%d"), Effect.Value, *StatusName(Effect.StatusId), Effect.StatusStacks);
		else if (Effect.Action == TEXT("damage_all_per_repeat")) Text = FString::Printf(TEXT("按当前计数器次数，对所有敌人每次造成%d点伤害"), Effect.Value);
		else if (Effect.Action == TEXT("damage_all_per_basic_gongfa")) Text = FString::Printf(TEXT("本场每打出过一张基础或功法牌，对所有敌人造成%d点伤害"), Effect.Value);
		else if (Effect.Action == TEXT("block")) Text = FString::Printf(TEXT("获得%d点罡气"), Effect.Value);
		else if (Effect.Action == TEXT("block_per_status")) Text = FString::Printf(TEXT("获得%d点罡气，每层%s额外+%d"), Effect.Value, *StatusName(Effect.StatusId), Effect.StatusStacks);
		else if (Effect.Action == TEXT("draw")) Text = FString::Printf(TEXT("抽%d张牌"), Effect.Value);
		else if (Effect.Action == TEXT("discover_draw")) Text = FString::Printf(TEXT("从抽牌堆随机展示%d张牌，选择1张加入手牌"), Effect.Value);
		else if (Effect.Action == TEXT("gain_spirit")) Text = FString::Printf(TEXT("获得%d点灵力"), Effect.Value);
		else if (Effect.Action == TEXT("lose_spirit")) Text = FString::Printf(TEXT("失去%d点灵力"), Effect.Value);
		else if (Effect.Action == TEXT("spirit_next_turn")) Text = FString::Printf(TEXT("下回合额外获得%d点灵力"), Effect.Value);
		else if (Effect.Action == TEXT("heal")) Text = FString::Printf(TEXT("恢复%d点气血"), Effect.Value);
		else if (Effect.Action == TEXT("gain_gold")) Text = FString::Printf(TEXT("获得%d枚灵石"), Effect.Value);
		else if (Effect.Action == TEXT("gain_max_hp")) Text = FString::Printf(TEXT("最大气血提高%d点"), Effect.Value);
		else if (Effect.Action == TEXT("self_damage")) Text = FString::Printf(TEXT("失去%d点气血"), Effect.Value);
		else if (Effect.Action == TEXT("discard_random")) Text = FString::Printf(TEXT("随机弃%d张牌"), Effect.Value);
		else if (Effect.Action == TEXT("discard_hand")) Text = TEXT("弃掉全部非保留手牌");
		else if (Effect.Action == TEXT("cleanse_toxicity")) Text = TEXT("清除全部丹毒");
		else if (Effect.Action == TEXT("create_card")) Text = FString::Printf(TEXT("将%d张【%s】置入手牌"), Effect.Value, *Effect.Param);
		else if (Effect.Action == TEXT("add_card_to_draw")) Text = FString::Printf(TEXT("将%d张【%s】置入抽牌堆"), Effect.Value, *Effect.Param);
		else if (Effect.Action == TEXT("add_card_to_discard")) Text = FString::Printf(TEXT("将%d张【%s】置入弃牌堆"), Effect.Value, *Effect.Param);
		else if (Effect.Action == TEXT("move_cards")) Text = FString::Printf(TEXT("从%s选择%d张%s移至%s"), *ZoneName(Effect.Source), Effect.Value, *SelectorName(Effect.Param), *ZoneName(Effect.Destination));
		else if (Effect.Action == TEXT("copy_cards")) Text = FString::Printf(TEXT("从%s复制%d张%s至%s"), *ZoneName(Effect.Source), Effect.Value, *SelectorName(Effect.Param), *ZoneName(Effect.Destination));
		else if (Effect.Action == TEXT("modify_card_cost")) Text = FString::Printf(TEXT("令%s中%d张%s本场费用%+d"), *ZoneName(Effect.Source), Effect.Value, *SelectorName(Effect.Param), Effect.StatusStacks);
		else if (Effect.Action == TEXT("upgrade_cards")) Text = FString::Printf(TEXT("本场强化%s中%d张%s"), *ZoneName(Effect.Source), Effect.Value, *SelectorName(Effect.Param));
		else if (Effect.Action == TEXT("transform_cards")) Text = FString::Printf(TEXT("将%s中%d张%s变化为【%s】"), *ZoneName(Effect.Source), Effect.Value, *SelectorName(Effect.Param), *Effect.Destination);
		else if (Effect.Action == TEXT("shuffle_zone")) Text = FString::Printf(TEXT("重新打乱%s"), *ZoneName(Effect.Source));
		else if (Effect.Action == TEXT("cost_free_basic_gongfa")) Text = TEXT("本回合基础牌与功法牌费用变为0");
		else if (Effect.Action == TEXT("defense_to_strength")) Text = FString::Printf(TEXT("获得%d层力量，本回合获得过罡气则加倍"), Effect.Value);
		else if (Effect.Action == TEXT("transfer"))
		{
			const FString When = Effect.Trigger == TEXT("on_play") || Effect.Trigger.IsEmpty()
				? TEXT("打出时") : FString::Printf(TEXT("触发%s时"), *Effect.Trigger);
			Text = FString::Printf(TEXT("%s，将%s按比例转化为%s%s"), *When, *ScriptValueName(Effect.Source),
				*ScriptValueName(Effect.Destination), Effect.bConsumeSource ? TEXT("并消耗来源") : TEXT(""));
		}
		else if (Effect.Action == TEXT("power")) Text = FString::Printf(TEXT("运转功法【%s】"), *Effect.StatusId);
		else if (Effect.Action == TEXT("apply_status") || Effect.Action == TEXT("remove_status")
			|| Effect.Action == TEXT("set_status") || Effect.Action == TEXT("apply_temp_strength")
			|| Effect.Action == TEXT("amplify_status"))
		{
			const FString LocalStatusName = StatusName(Effect.StatusId);
			const FString TargetName = Effect.Target == TEXT("self") ? TEXT("自身")
				: Effect.Target == TEXT("all_enemies") ? TEXT("所有敌人") : TEXT("目标");
			if (Effect.Action == TEXT("remove_status")) Text = FString::Printf(TEXT("%s移除%d层%s"), *TargetName, Effect.StatusStacks, *LocalStatusName);
			else if (Effect.Action == TEXT("set_status")) Text = FString::Printf(TEXT("将%s的%s设为%d层"), *TargetName, *LocalStatusName, Effect.StatusStacks);
			else if (Effect.Action == TEXT("amplify_status")) Text = FString::Printf(TEXT("令%s的%s层数翻倍"), *TargetName, *LocalStatusName);
			else Text = FString::Printf(TEXT("%s获得%d层%s"), *TargetName, Effect.StatusStacks, *LocalStatusName);
		}
		if (!Effect.ScaleBy.IsEmpty())
			Text += FString::Printf(TEXT("，每%d点%s额外+%d"), FMath::Max(1, Effect.ScaleDivisor), *ScriptValueName(Effect.ScaleBy), Effect.ScaleFactor);
		if (!Effect.Condition.IsEmpty() && Effect.Condition != TEXT("always"))
			Text = FString::Printf(TEXT("若%s，%s"), *ConditionName(Effect.Condition), *Text);
		if (Effect.Times > 1) Text += FString::Printf(TEXT("，重复%d次"), Effect.Times);
		if (Effect.Chance < 0.999f) Text = FString::Printf(TEXT("有%d%%概率%s"), FMath::RoundToInt(Effect.Chance * 100.f), *Text);
		if (Text.IsEmpty()) Text = TEXT("无主动效果");
		return Text;
	}

	bool BuildAuthoredCard(const TSharedPtr<FJsonObject>& Object, int32 Cycle, int32 ChoiceIndex,
		FCardData& OutCard, FString& OutError)
	{
		if (!Object.IsValid()) { OutError = TEXT("created_cards[0] 必须是对象"); return false; }
		static const TArray<FString> Rarities = {TEXT("common"), TEXT("uncommon"), TEXT("rare"), TEXT("legendary")};
		static const TArray<FString> Types = {TEXT("spell"), TEXT("sword"), TEXT("body"), TEXT("talisman"), TEXT("skill"), TEXT("gongfa")};
		static const TArray<FString> Classes = {TEXT(""), TEXT("sword"), TEXT("danxiu"), TEXT("fuxiu")};
		static const TArray<FString> Counters = {
			TEXT(""), TEXT("on_basic_play"), TEXT("on_basic_gongfa_play"), TEXT("on_any_card_play"),
			TEXT("on_same_type_play"), TEXT("on_sword_play"), TEXT("on_spell_play"),
			TEXT("on_damage_dealt"), TEXT("on_draw"), TEXT("on_turn_start")
		};
		OutCard.Name = GetString(Object, TEXT("name")).Left(18);
		OutCard.Rarity = GetString(Object, TEXT("rarity"), TEXT("common"));
		OutCard.Type = GetString(Object, TEXT("type"));
		OutCard.Class = GetString(Object, TEXT("class"));
		OutCard.CounterCondition = GetString(Object, TEXT("counter_condition"));
		OutCard.Cost = GetInt(Object, TEXT("cost"), 1);
		if (OutCard.Name.IsEmpty()) { OutError = TEXT("原创卡缺少 name"); return false; }
		if (!Rarities.Contains(OutCard.Rarity)) { OutError = FString::Printf(TEXT("原创卡 rarity=%s 非法"), *OutCard.Rarity); return false; }
		if (!Types.Contains(OutCard.Type)) { OutError = FString::Printf(TEXT("原创卡 type=%s 非法；剑诀应使用 sword"), *OutCard.Type); return false; }
		if (!Classes.Contains(OutCard.Class)) { OutError = FString::Printf(TEXT("原创卡 class=%s 非法"), *OutCard.Class); return false; }
		if (!Counters.Contains(OutCard.CounterCondition)) { OutError = FString::Printf(TEXT("原创卡 counter_condition=%s 未实现"), *OutCard.CounterCondition); return false; }
		if (OutCard.Cost < 0 || OutCard.Cost > 3) { OutError = TEXT("原创卡 cost 必须在0~3"); return false; }
		// design_note 只供登记校验和修复使用，绝不能出现在卡面。flavor 是唯一允许显示的世界内小字。
		OutCard.Flavor = SanitizeAuthoredFlavor(GetString(Object, TEXT("flavor")));
		const FString ProposedArt = GetString(Object, TEXT("art"));
		OutCard.ArtPath = ResolveRuntimeArt(ProposedArt, TEXT("card"));
		Object->TryGetBoolField(TEXT("exhaust"), OutCard.bExhaust);
		Object->TryGetBoolField(TEXT("retain"), OutCard.bRetain);
		const TArray<TSharedPtr<FJsonValue>>* Effects = nullptr;
		if (!Object->TryGetArrayField(TEXT("effects"), Effects) || Effects->Num() == 0) { OutError = TEXT("原创卡至少需要1个 effects"); return false; }
		const int32 MaxEffects = 8;
		if (Effects->Num() > MaxEffects) { OutError = FString::Printf(TEXT("%s 品阶最多允许%d个 effects"), *OutCard.Rarity, MaxEffects); return false; }
		for (const TSharedPtr<FJsonValue>& EffectValue : *Effects)
		{
			FString EffectError;
			if (!ValidateAuthoredEffect(EffectValue->AsObject(), OutCard.Rarity, false, EffectError))
			{
				OutError = FString::Printf(TEXT("效果%d非法：%s"), OutCard.Effects.Num() + 1, *EffectError);
				return false;
			}
			FCardEffect Effect;
			ParseSafeEffect(EffectValue->AsObject(), OutCard.Rarity, false, Effect);
			OutCard.Effects.Add(Effect);
		}
		const TSharedPtr<FJsonObject>* Upgrade = nullptr;
		if (Object->TryGetObjectField(TEXT("upgrade"), Upgrade))
		{
			OutCard.UpgradedCost = GetInt(*Upgrade, TEXT("cost"), -1);
			OutCard.UpgradedCounterCondition = GetString(*Upgrade, TEXT("counter_condition"));
			if (OutCard.UpgradedCost < -1 || OutCard.UpgradedCost > 3) { OutError = TEXT("upgrade.cost 必须为-1~3"); return false; }
			if (!Counters.Contains(OutCard.UpgradedCounterCondition)) { OutError = TEXT("upgrade.counter_condition 未实现"); return false; }
			const TArray<TSharedPtr<FJsonValue>>* UpgradeEffects = nullptr;
			if ((*Upgrade)->TryGetArrayField(TEXT("effects"), UpgradeEffects) && UpgradeEffects->Num() > 0)
			{
				if (UpgradeEffects->Num() > MaxEffects) { OutError = TEXT("upgrade.effects 数量非法"); return false; }
				for (const TSharedPtr<FJsonValue>& Value : *UpgradeEffects)
				{
					FString EffectError;
					if (!ValidateAuthoredEffect(Value->AsObject(), OutCard.Rarity, false, EffectError)) { OutError = TEXT("升级效果非法：") + EffectError; return false; }
					FCardEffect Effect; ParseSafeEffect(Value->AsObject(), OutCard.Rarity, false, Effect); OutCard.UpgradedEffects.Add(Effect);
				}
			}
		}
		if (OutCard.UpgradedEffects.Num() == 0)
		{
			OutCard.UpgradedEffects = OutCard.Effects;
			FCardEffect& First = OutCard.UpgradedEffects[0];
			if (!First.StatusId.IsEmpty()) First.StatusStacks += 1;
			else if (First.Value > 0 && First.Action != TEXT("draw") && First.Action != TEXT("gain_spirit")) First.Value += 2;
		}
		TArray<FString> Descriptions;
		for (const FCardEffect& Effect : OutCard.Effects) Descriptions.Add(SafeEffectDescription(Effect));
		OutCard.Description = FString::Join(Descriptions, TEXT("；"));
		Descriptions.Reset();
		for (const FCardEffect& Effect : OutCard.UpgradedEffects) Descriptions.Add(SafeEffectDescription(Effect));
		OutCard.UpgradedDescription = FString::Join(Descriptions, TEXT("；"));
		OutCard.Visual.Animation = OutCard.Type == TEXT("sword") ? TEXT("slash") : OutCard.Effects[0].Action.Contains(TEXT("damage")) ? TEXT("impact") : TEXT("none");
		OutCard.Visual.Sound = OutCard.Type == TEXT("sword") ? TEXT("sword_slash") : TEXT("none");
		const TSharedPtr<FJsonObject>* Visual = nullptr;
		if (Object->TryGetObjectField(TEXT("visual"), Visual))
		{
			static const TArray<FString> Animations = {TEXT("none"), TEXT("slash"), TEXT("fireball"), TEXT("impact"), TEXT("block"), TEXT("heal"), TEXT("draw")};
			static const TArray<FString> Sounds = {TEXT("none"), TEXT("sword_slash"), TEXT("fireball"), TEXT("block"), TEXT("heal"), TEXT("draw")};
			const FString Animation = GetString(*Visual, TEXT("animation"), OutCard.Visual.Animation);
			const FString Sound = GetString(*Visual, TEXT("sound"), OutCard.Visual.Sound);
			if (!Animations.Contains(Animation) || !Sounds.Contains(Sound)) { OutError = TEXT("visual.animation/sound 非法"); return false; }
			OutCard.Visual.Animation = Animation; OutCard.Visual.Sound = Sound;
			OutCard.Visual.Accent = GetString(*Visual, TEXT("accent"), TEXT("#FFFFFF"));
			OutCard.Visual.Duration = FMath::Clamp(GetFloat(*Visual, TEXT("duration"), 0.42f), 0.1f, 2.f);
			OutCard.Visual.Intensity = FMath::Clamp(GetFloat(*Visual, TEXT("intensity"), 4.f), 0.f, 20.f);
			OutCard.Visual.Count = FMath::Clamp(GetInt(*Visual, TEXT("count"), 1), 1, 8);
		}
		OutCard.Id = MakeRuntimeContentId(TEXT("llm_card"), OutCard.Name, Cycle, ChoiceIndex);
		return true;
	}

	FString SafeRelicDescription(const FRelicData& Relic)
	{
		FString Trigger;
		if (Relic.Trigger == TEXT("combat_start")) Trigger = TEXT("战斗开始时");
		else if (Relic.Trigger == TEXT("turn_start")) Trigger = TEXT("每回合开始时");
		else if (Relic.Trigger == TEXT("on_kill")) Trigger = TEXT("击杀敌人时");
		else if (Relic.Trigger == TEXT("on_victory")) Trigger = TEXT("战斗胜利时");
		else if (Relic.Trigger == TEXT("on_card_played")) Trigger = FString::Printf(TEXT("每打出%d张牌时"), FMath::Max(1, Relic.Counter));
		else if (Relic.Trigger == TEXT("on_player_turn_end")) Trigger = TEXT("回合结束时");
		else if (Relic.Trigger == TEXT("on_damage_dealt")) Trigger = FString::Printf(TEXT("每造成%d次伤害时"), FMath::Max(1, Relic.Counter));
		else if (Relic.Trigger == TEXT("on_cards_discarded")) Trigger = TEXT("每弃一张牌时");
		else if (Relic.Trigger == TEXT("on_reshuffle")) Trigger = TEXT("洗牌时");
		return Trigger + TEXT("，") + SafeEffectDescription(Relic.Effect);
	}
}

void UInfiniteNarrativeService::Generate(const FInfiniteNarrativeSettings& Settings,
	const FInfiniteNarrativeRequestContext& Context, FOnInfiniteNarrativeReady Completion,
	FOnInfiniteNarrativeStreamUpdate StreamUpdate)
{
	// A single service owns both ordinary RP and combat-prefetch requests. Invalidate the
	// previous chain before installing the new callbacks, so a late completion can never
	// paint an obsolete screen over the current one.
	CancelGeneration();
	PendingCompletion = MoveTemp(Completion);
	PendingStreamUpdate = MoveTemp(StreamUpdate);
	PendingContext = Context;
	PendingSettings = Settings;
	PendingDraftBeat = FInfiniteNarrativeBeat();
	bHasPendingDraftBeat = false;
	PendingDraftJson.Reset();
	TokenCapAttempt = 0;
	EffectiveMaxOutputTokens = FMath::Clamp(Settings.MaxOutputTokens, 1024, 262144);
	MvuTokenCapAttempt = 0;
	MvuSemanticRetryAttempt = 0;
	TotalModelRequestCount = 0;
	LastMvuValidationError.Reset();
	EffectiveMvuMaxOutputTokens = FMath::Clamp(Settings.MvuMaxOutputTokens, 2048, 262144);
	RequestPhase = ERequestPhase::Generation;
	bWriterStreamFallbackAttempted = false;
	ResetWriterStreamState();
	PrepareChoiceRoutePlan();

	if (Settings.Endpoint.TrimStartAndEnd().IsEmpty() || Settings.Model.TrimStartAndEnd().IsEmpty())
	{
		CompleteWithError(TEXT("未配置 LLM 接口或模型名称"));
		return;
	}
	// On Apple, NSURLRequest.timeoutInterval is initialized from UE's global
	// HttpConnectionTimeout (30s by default). IHttpRequest::SetTimeout only controls
	// the total request timer and does not override that platform interval. Refresh
	// both globals at runtime as well as shipping them in DefaultEngine.ini, so a
	// staged build with stale config cannot silently cut an LLM request off at 30s.
	const float TransportTimeout = FMath::Max(180.f, Settings.TimeoutSeconds + 5.f);
	if (GConfig)
	{
		GConfig->SetFloat(TEXT("HTTP"), TEXT("HttpConnectionTimeout"), TransportTimeout, GEngineIni);
		GConfig->SetFloat(TEXT("HTTP"), TEXT("HttpActivityTimeout"), TransportTimeout, GEngineIni);
		FHttpModule::Get().UpdateConfigs();
	}
	UE_LOG(LogTemp, Display, TEXT("[InfiniteRP] HTTP transport connection=%.0fs activity=%.0fs request_total=%.0fs"),
		FHttpModule::Get().GetHttpConnectionTimeout(), FHttpModule::Get().GetHttpActivityTimeout(),
		Settings.TimeoutSeconds);
	FString PresetError;
	if (!FNarrativePromptManager::LoadPreset(TEXT("Data/rp_prompt_preset.json"),
		Settings.NarrativePromptPresetPath, Settings.NarrativePromptPresetOverride,
		NarrativePreset, PresetError))
	{
		CompleteWithError(PresetError);
		return;
	}
	// MVU used to be a mandatory second model call.  The writer now authors the small,
	// allowlisted engine delta directly, so old MVU settings remain load-compatible but
	// are intentionally absent from the runtime request chain.
	IssueRequest();
}

void UInfiniteNarrativeService::PrepareChoiceRoutePlan()
{
	PendingChoiceRoutePlan.Reset();
	PendingChoiceRouteValue.Reset();
	PendingChoiceRoutePayload.Reset();
	const int32 RouteSeed = PendingSettings.Seed >= 0
		? PendingSettings.Seed ^ (PendingContext.Cycle * 196613)
		: FMath::Rand();
	FRandomStream Random(RouteSeed);
	auto AddRoute = [this](const FString& Route, int32 Value = 0, const FString& Payload = FString())
	{
		PendingChoiceRoutePlan.Add(Route);
		PendingChoiceRouteValue.Add(Value);
		PendingChoiceRoutePayload.Add(Payload);
	};

	struct FRoutePoolEntry
	{
		const TCHAR* Route;
		int32 BaseWeight;
		int32 Quality;
		bool bAvailable;
	};
	// One shared pool for A/B/C. Base weights total 100 when every conditional route is
	// available. Quality affects only locally computed luck modifiers; it is never shown
	// to or chosen by the narrative model.
	const TArray<FRoutePoolEntry> Pool = {
		{TEXT("combat"),       20,  0, true},
		{TEXT("card_forge"),    16,  2, true},
		{TEXT("relic_reward"),  10,  3, PendingContext.AvailableFixedRelicIds.Num() > 0},
		{TEXT("reward"),         9,  2, true},
		{TEXT("shop"),           7,  1, true},
		{TEXT("rest"),           5,  1, true},
		{TEXT("upgrade"),        7,  2, PendingContext.UpgradeableCardCount > 0},
		{TEXT("remove"),         5,  0, PendingContext.DeckSize > 1},
		{TEXT("hurt"),           5, -1, PendingContext.HP > 6},
		{TEXT("lose_gold"),      3, -1, PendingContext.Gold > 0},
		{TEXT("heal"),           4,  1, PendingContext.HP < PendingContext.MaxHP},
		{TEXT("gain_gold"),      5,  1, true},
		{TEXT("continue_rp"),    4,  0, true}
	};
	const float Luck = FMath::Clamp(PendingContext.RouteRewardBias, 0.f, 1.f);
	for (int32 ChoiceSlot = 0; ChoiceSlot < 3; ++ChoiceSlot)
	{
		TArray<int32> Weights;
		int32 TotalWeight = 0;
		for (const FRoutePoolEntry& Entry : Pool)
		{
			int32 Weight = 0;
			if (Entry.bAvailable)
			{
				const float QualityFactor = Entry.Quality >= 0
					? 1.f + Luck * Entry.Quality
					: 1.f / (1.f + Luck * -Entry.Quality);
				Weight = FMath::Max(1, FMath::RoundToInt(Entry.BaseWeight * QualityFactor * 100.f));
			}
			Weights.Add(Weight);
			TotalWeight += Weight;
		}
		int32 Roll = Random.RandRange(1, FMath::Max(1, TotalWeight));
		int32 SelectedIndex = 0;
		for (; SelectedIndex < Pool.Num(); ++SelectedIndex)
		{
			Roll -= Weights[SelectedIndex];
			if (Roll <= 0) break;
		}
		const FString Route = Pool[FMath::Clamp(SelectedIndex, 0, Pool.Num() - 1)].Route;
		if (Route == TEXT("hurt"))
			AddRoute(Route, -FMath::Min(Random.RandRange(5, 15), PendingContext.HP - 1));
		else if (Route == TEXT("lose_gold"))
			AddRoute(Route, -FMath::Min(Random.RandRange(3, 18), PendingContext.Gold));
		else if (Route == TEXT("heal"))
			AddRoute(Route, FMath::Min(Random.RandRange(4, 12), PendingContext.MaxHP - PendingContext.HP));
		else if (Route == TEXT("gain_gold")) AddRoute(Route, Random.RandRange(5, 18));
		else if (Route == TEXT("relic_reward"))
		{
			const FString RelicId = PendingContext.AvailableFixedRelicIds[
				Random.RandRange(0, PendingContext.AvailableFixedRelicIds.Num() - 1)];
			AddRoute(Route, 0, RelicId);
		}
		else AddRoute(Route);
	}

	if (!bSuppressRoutePlanLog)
	{
		UE_LOG(LogTemp, Display, TEXT("[InfiniteRP] shared weighted pool luck=%.2f routes A=%s(%d) B=%s(%d) C=%s(%d)"),
			Luck,
			*PendingChoiceRoutePlan[0], PendingChoiceRouteValue[0],
			*PendingChoiceRoutePlan[1], PendingChoiceRouteValue[1],
			*PendingChoiceRoutePlan[2], PendingChoiceRouteValue[2]);
	}
}

FString UInfiniteNarrativeService::DescribeChoiceRoutePlanForPrompt() const
{
	TArray<FString> Lines;
	for (int32 Index = 0; Index < 3; ++Index)
	{
		const FString Route = PendingChoiceRoutePlan.IsValidIndex(Index)
			? PendingChoiceRoutePlan[Index] : TEXT("continue_rp");
		const int32 Value = PendingChoiceRouteValue.IsValidIndex(Index) ? PendingChoiceRouteValue[Index] : 0;
		const FString Payload = PendingChoiceRoutePayload.IsValidIndex(Index)
			? PendingChoiceRoutePayload[Index] : FString();
		FString Instruction;
		if (Route == TEXT("combat"))
			Instruction = TEXT("next=combat；局面必须在选中后立刻进入战斗，并填写encounter，停在第一击结算前");
		else if (Route == TEXT("card_forge"))
			Instruction = TEXT("next=card_forge；玩家当场获得一个值得化为原创卡的事物，只填写card_concept，不写卡牌规则");
		else if (Route == TEXT("relic_reward"))
		{
			const FString RelicName = PendingContext.FixedRelicIdToName.FindRef(Payload);
			Instruction = FString::Printf(TEXT("next=relic_reward；玩家当场获得固有法宝【%s】，必须按其名称与意象编造可信来历，不得改名或改效果"),
				RelicName.IsEmpty() ? TEXT("未知法宝") : *RelicName);
		}
		else if (Route == TEXT("shop")) Instruction = TEXT("next=shop；编造能立即进入商店的合理理由");
		else if (Route == TEXT("reward")) Instruction = TEXT("next=reward；编造能立即进入三选一奖励的合理理由");
		else if (Route == TEXT("rest")) Instruction = TEXT("next=rest；编造能立即进入休息界面的合理理由");
		else if (Route == TEXT("upgrade")) Instruction = TEXT("next=upgrade；编造能立即选择升级一张卡的合理理由");
		else if (Route == TEXT("remove")) Instruction = TEXT("next=remove；编造能立即选择剔除一张卡的合理理由");
		else if (Route == TEXT("hurt"))
			Instruction = FString::Printf(TEXT("next=continue_rp；玩家当场气血%d，result_summary必须写清受伤原因"), Value);
		else if (Route == TEXT("lose_gold"))
			Instruction = FString::Printf(TEXT("next=continue_rp；玩家当场灵石%d，result_summary必须写清损失原因"), Value);
		else if (Route == TEXT("heal"))
			Instruction = FString::Printf(TEXT("next=continue_rp；玩家当场气血+%d，result_summary必须写清恢复原因"), Value);
		else if (Route == TEXT("gain_gold"))
			Instruction = FString::Printf(TEXT("next=continue_rp；玩家当场灵石+%d，result_summary必须写清来源"), Value);
		else Instruction = TEXT("next=continue_rp；结果在剧情中立即完成");
		Lines.Add(FString::Printf(TEXT("%c：%s"), TCHAR(TEXT('A') + Index), *Instruction));
	}
	return FString::Join(Lines, TEXT("\n"));
}

void UInfiniteNarrativeService::ApplyChoiceRoutePlan(FInfiniteNarrativeChoice& Choice, int32 ChoiceSlot) const
{
	if (!PendingChoiceRoutePlan.IsValidIndex(ChoiceSlot)) return;
	const FString Route = PendingChoiceRoutePlan[ChoiceSlot];
	const int32 Value = PendingChoiceRouteValue.IsValidIndex(ChoiceSlot) ? PendingChoiceRouteValue[ChoiceSlot] : 0;
	const FString Payload = PendingChoiceRoutePayload.IsValidIndex(ChoiceSlot)
		? PendingChoiceRoutePayload[ChoiceSlot] : FString();
	const TArray<FInfiniteCardForgeJob> ParsedForgeJobs = Choice.CardForgeJobs;
	Choice.Operations.Reset();
	Choice.Reward = FInfiniteNarrativeReward();
	Choice.CardForgeJobs.Reset();
	Choice.Next = Route;

	auto AddOperation = [&Choice](const FString& Op, int32 OperationValue = 0)
	{
		FInfiniteGameOperation Operation;
		Operation.Op = Op;
		Operation.Value = OperationValue;
		Choice.Operations.Add(Operation);
	};
	if (Route == TEXT("combat"))
	{
		if (Choice.Enemy.TemplateId.IsEmpty()) Choice.Enemy.TemplateId = TEXT("mountain_imp");
		if (Choice.Enemy.Name.IsEmpty()) Choice.Enemy.Name = TEXT("剧情敌人");
		if (Choice.Enemy.Story.IsEmpty()) Choice.Enemy.Story = Choice.ResultSummary;
	}
	else
	{
		Choice.Enemy = FInfiniteEnemySpec();
		if (Route == TEXT("card_forge"))
		{
			FInfiniteCardForgeJob Job;
			if (ParsedForgeJobs.Num() > 0) Job = ParsedForgeJobs[0];
			if (Job.SourceFact.IsEmpty()) Job.SourceFact = Choice.ResultSummary;
			if (Job.Concept.IsEmpty()) Job.Concept = Choice.ResultSummary.Left(80);
			if (Job.MechanicIntent.IsEmpty())
				Job.MechanicIntent = TEXT("根据概念的材质、用途、取得方式与情绪设计独特机制");
			Choice.CardForgeJobs.Add(Job);
		}
		else if (Route == TEXT("relic_reward"))
		{
			Choice.Next = TEXT("continue_rp");
			if (!Payload.IsEmpty()) Choice.Reward.RelicIds.AddUnique(Payload);
		}
		else if (Route == TEXT("shop")) AddOperation(TEXT("open_shop"));
		else if (Route == TEXT("reward")) AddOperation(TEXT("open_reward"));
		else if (Route == TEXT("rest")) AddOperation(TEXT("open_rest"));
		else if (Route == TEXT("upgrade")) AddOperation(TEXT("choose_upgrade_card"));
		else if (Route == TEXT("remove")) AddOperation(TEXT("choose_remove_card"));
		else if (Route == TEXT("hurt") || Route == TEXT("heal"))
		{
			Choice.Next = TEXT("continue_rp");
			Choice.Reward.HPChange = Value;
			AddOperation(TEXT("hp"), Value);
		}
		else if (Route == TEXT("lose_gold") || Route == TEXT("gain_gold"))
		{
			Choice.Next = TEXT("continue_rp");
			Choice.Reward.GoldChange = Value;
			AddOperation(TEXT("gold"), Value);
		}
	}
}

void UInfiniteNarrativeService::CancelGeneration()
{
	++GenerationSerial;
	PendingCompletion.Unbind();
	PendingStreamUpdate.Unbind();
	PendingCardForgeCompletion.Unbind();
	if (ActiveRequest.IsValid())
	{
		// Unbind before cancellation: some platform HTTP implementations complete the
		// request synchronously from CancelRequest().
		ActiveRequest->OnProcessRequestComplete().Unbind();
		ActiveRequest->CancelRequest();
		ActiveRequest.Reset();
	}
	bHasPendingDraftBeat = false;
	PendingDraftBeat = FInfiniteNarrativeBeat();
	PendingDraftJson.Reset();
	RequestPhase = ERequestPhase::Generation;
	ResetWriterStreamState();
}

void UInfiniteNarrativeService::ForgeCard(const FInfiniteNarrativeSettings& Settings,
	const FInfiniteNarrativeRequestContext& Context, const FInfiniteCardForgeJob& Job,
	FOnInfiniteCardForgeReady Completion)
{
	CancelGeneration();
	PendingSettings = Settings;
	PendingContext = Context;
	PendingCardForgeJob = Job;
	PendingCardForgeCompletion = MoveTemp(Completion);
	CardForgeAttempt = 0;
	LastCardForgeError.Reset();
	RequestPhase = ERequestPhase::CardForge;
	IssueCardForgeRequest();
}

void UInfiniteNarrativeService::IssueCardForgeRequest()
{
	if (!PendingCardForgeCompletion.IsBound()) return;
	if (PendingSettings.Endpoint.TrimStartAndEnd().IsEmpty() || PendingSettings.Model.TrimStartAndEnd().IsEmpty())
	{
		CompleteCardForge(false, FCardData(), TEXT("原创卡工坊缺少接口地址或模型"));
		return;
	}

	const FString Repair = LastCardForgeError.IsEmpty() ? TEXT("")
		: TEXT("\n[上一份短脚本无法编译]\n") + LastCardForgeError
			+ TEXT("\n保留物品意象，重写出一份更短、只使用手册词汇的完整脚本。\n");
	const FString SystemPrompt = TEXT(
		"你是独立的原创卡牌设计师，不负责剧情裁决、状态更新或页面跳转。剧情已经确认玩家获得了某个事物；"
		"你的唯一任务是理解这个事物，并把它设计成一张有明确玩法身份、强度适当且能直接运行的原创卡。"
		"先在心里分析物品表现、玩法身份和强度，但不要输出分析。不要读取或延续此前卡牌或剧情效果。"
		"不要把示例当模板；至少让两个具体意象体现在动作、钩子、费用、保留或消耗上。"
		"最终只能输出几行卡牌脚本。\n[卡牌脚本手册]\n")
		+ FCardScriptCompiler::PromptReference() + Repair;
	const FString UserPrompt = FString::Printf(TEXT(
		"[已确认的选中分支生卡任务]\nsource_fact=%s\nconcept=%s\nmechanic_intent=%s\nacquisition=%s\n"
		"当前轮次=%d；当前能力名称仅用于避免同名：%s\n请设计恰好一张牌并只写短脚本。"),
		*PendingCardForgeJob.SourceFact, *PendingCardForgeJob.Concept,
		*PendingCardForgeJob.MechanicIntent, *PendingCardForgeJob.Acquisition,
		PendingContext.Cycle, *FString::Join(PendingContext.AbilityNames, TEXT("、")));

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("model"), PendingSettings.MvuVerifierModel.TrimStartAndEnd().IsEmpty()
		? PendingSettings.Model : PendingSettings.MvuVerifierModel.TrimStartAndEnd());
	Root->SetNumberField(TEXT("temperature"), 0.9);
	Root->SetNumberField(TEXT("top_p"), 0.98);
	Root->SetNumberField(TEXT("max_tokens"), 2048);
	const FString ForgeModel = PendingSettings.MvuVerifierModel.TrimStartAndEnd().IsEmpty()
		? PendingSettings.Model.TrimStartAndEnd() : PendingSettings.MvuVerifierModel.TrimStartAndEnd();
	if (ForgeModel.StartsWith(TEXT("deepseek-v4"), ESearchCase::IgnoreCase))
	{
		TSharedPtr<FJsonObject> Thinking = MakeShared<FJsonObject>();
		Thinking->SetStringField(TEXT("type"), TEXT("disabled"));
		Root->SetObjectField(TEXT("thinking"), Thinking);
	}
	TArray<TSharedPtr<FJsonValue>> Messages;
	for (const TPair<FString, FString>& Pair : TArray<TPair<FString, FString>>{
		{TEXT("system"), SystemPrompt}, {TEXT("user"), UserPrompt}})
	{
		TSharedPtr<FJsonObject> Message = MakeShared<FJsonObject>();
		Message->SetStringField(TEXT("role"), Pair.Key);
		Message->SetStringField(TEXT("content"), Pair.Value);
		Messages.Add(MakeShared<FJsonValueObject>(Message));
	}
	Root->SetArrayField(TEXT("messages"), Messages);
	FString Payload;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Payload);
	FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);

	ActiveRequest = FHttpModule::Get().CreateRequest();
	ActiveRequest->SetURL(ResolveChatCompletionsUrl(PendingSettings.Endpoint));
	ActiveRequest->SetVerb(TEXT("POST"));
	ActiveRequest->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	ActiveRequest->SetHeader(TEXT("User-Agent"), TEXT("AscendSpire/1.0"));
	if (!PendingSettings.ApiKey.IsEmpty())
		ActiveRequest->SetHeader(TEXT("Authorization"), FString::Printf(TEXT("Bearer %s"), *PendingSettings.ApiKey));
	ActiveRequest->SetTimeout(FMath::Clamp(PendingSettings.TimeoutSeconds, 5.f, 180.f));
	ActiveRequest->SetContentAsString(Payload);
	ActiveRequest->OnProcessRequestComplete().BindUObject(this,
		&UInfiniteNarrativeService::HandleCardForgeComplete);
	UE_LOG(LogTemp, Display, TEXT("[CardForge] CardScript request attempt=%d/3 concept=%s payload_chars=%d"),
		CardForgeAttempt + 1, *PendingCardForgeJob.Concept, Payload.Len());
	if (!ActiveRequest->ProcessRequest())
		CompleteCardForge(false, FCardData(), TEXT("原创卡工坊请求未能启动"));
}

void UInfiniteNarrativeService::HandleCardForgeComplete(FHttpRequestPtr Request,
	FHttpResponsePtr Response, bool bSucceeded)
{
	if (!ActiveRequest.IsValid() || Request != ActiveRequest)
	{
		UE_LOG(LogTemp, Display, TEXT("[CardForge] ignored stale/cancelled completion"));
		return;
	}
	ActiveRequest.Reset();
	FCardData Card;
	FString Error;
	if (!bSucceeded || !Response.IsValid()) Error = TEXT("连接失败或超时");
	else if (Response->GetResponseCode() < 200 || Response->GetResponseCode() >= 300)
		Error = FString::Printf(TEXT("HTTP %d"), Response->GetResponseCode());
	else if (ParseForgedCard(Response->GetContentAsString(), Card, Error))
	{
		UE_LOG(LogTemp, Display, TEXT("[CardForge] success card=%s id=%s attempt=%d"),
			*Card.Name, *Card.Id, CardForgeAttempt + 1);
		CompleteCardForge(true, Card, TEXT("原创卡已由独立工坊编译完成"));
		return;
	}
	if (CardForgeAttempt < 2)
	{
		++CardForgeAttempt;
		LastCardForgeError = Error.Left(1600);
		UE_LOG(LogTemp, Warning, TEXT("[CardForge] retry attempt=%d error=%s"), CardForgeAttempt + 1, *LastCardForgeError);
		IssueCardForgeRequest();
		return;
	}
	CompleteCardForge(false, FCardData(), TEXT("原创卡短脚本三次均无法编译：") + Error);
}

bool UInfiniteNarrativeService::ParseForgedCard(const FString& ResponseBody,
	FCardData& OutCard, FString& OutError) const
{
	OutCard = FCardData();
	OutError.Reset();
	const FString Content = ExtractTransportContent(ResponseBody);
	TSharedPtr<FJsonObject> ScriptCard;
	FString ScriptError;
	if (FCardScriptCompiler::CompileToAuthoredObject(Content, ScriptCard, ScriptError))
	{
		const int32 Repairs = NormalizeForgedCardForRuntime(ScriptCard);
		if (Repairs > 0) UE_LOG(LogTemp, Display, TEXT("[CardForge] CardScript compiler completed %d safe default(s)"), Repairs);
		return BuildAuthoredCard(ScriptCard, PendingContext.Cycle, 0, OutCard, OutError);
	}
	// Old JSON forge responses remain readable for existing tests, providers and saves.
	TSharedPtr<FJsonObject> Root;
	if (!ParseModelJsonObject(Content, Root))
	{
		OutError = TEXT("短脚本无法编译：") + ScriptError;
		return false;
	}
	TSharedPtr<FJsonObject> CardObject;
	const TSharedPtr<FJsonObject>* FoundCard = nullptr;
	if (!Root->TryGetObjectField(TEXT("card"), FoundCard))
		Root->TryGetObjectField(TEXT("created_card"), FoundCard);
	if (FoundCard && FoundCard->IsValid()) CardObject = *FoundCard;
	if (!CardObject.IsValid())
	{
		const TArray<TSharedPtr<FJsonValue>>* Cards = nullptr;
		if (Root->TryGetArrayField(TEXT("created_cards"), Cards) && Cards && Cards->Num() > 0)
			CardObject = (*Cards)[0]->AsObject();
	}
	if (!CardObject.IsValid())
	{
		OutError = TEXT("工坊输出缺少card对象");
		return false;
	}
	const int32 Repairs = NormalizeForgedCardForRuntime(CardObject);
	if (Repairs > 0)
		UE_LOG(LogTemp, Display, TEXT("[CardForge] locally completed %d mechanical default(s)"), Repairs);
	return BuildAuthoredCard(CardObject, PendingContext.Cycle, 0, OutCard, OutError);
}

bool UInfiniteNarrativeService::ParseForgedCardForAutomationTest(const FString& ResponseBody,
	int32 Cycle, FCardData& OutCard, FString& OutError) const
{
	UInfiniteNarrativeService* MutableThis = const_cast<UInfiniteNarrativeService*>(this);
	const int32 PreviousCycle = MutableThis->PendingContext.Cycle;
	MutableThis->PendingContext.Cycle = Cycle;
	const bool bParsed = ParseForgedCard(ResponseBody, OutCard, OutError);
	MutableThis->PendingContext.Cycle = PreviousCycle;
	return bParsed;
}

void UInfiniteNarrativeService::CompleteCardForge(bool bSuccess, const FCardData& Card,
	const FString& Diagnostic)
{
	ActiveRequest.Reset();
	RequestPhase = ERequestPhase::Generation;
	if (!PendingCardForgeCompletion.IsBound()) return;
	FOnInfiniteCardForgeReady Completion = MoveTemp(PendingCardForgeCompletion);
	Completion.Execute(bSuccess, Card, Diagnostic);
}

void UInfiniteNarrativeService::IssueRequest()
{
	if (!ReserveModelRequest(TEXT("writer"))) return;
	const FInfiniteNarrativeSettings& Settings = PendingSettings;
	ResetWriterStreamState();
	bWriterStreaming = Settings.bStreamResponse;

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("model"), Settings.Model);
	ApplyGenerationControls(Root, NarrativePreset, false);
	Root->SetNumberField(TEXT("max_tokens"), EffectiveMaxOutputTokens);
	Root->SetObjectField(TEXT("response_format"), MakeShared<FJsonObject>());
	Root->GetObjectField(TEXT("response_format"))->SetStringField(TEXT("type"), TEXT("json_object"));
	Root->SetBoolField(TEXT("stream"), bWriterStreaming);

	TArray<TSharedPtr<FJsonValue>> Messages;
	FString PromptDiagnostic;
	for (const FNarrativePromptMessage& PromptMessage : BuildNarrativeMessages(PromptDiagnostic))
	{
		TSharedPtr<FJsonObject> Message = MakeShared<FJsonObject>();
		Message->SetStringField(TEXT("role"), PromptMessage.Role);
		Message->SetStringField(TEXT("content"), PromptMessage.Content);
		Messages.Add(MakeShared<FJsonValueObject>(Message));
	}
	Root->SetArrayField(TEXT("messages"), Messages);

	FString Payload;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Payload);
	FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);

	ActiveRequest = FHttpModule::Get().CreateRequest();
	ActiveRequest->SetURL(ResolveChatCompletionsUrl(Settings.Endpoint));
	ActiveRequest->SetVerb(TEXT("POST"));
	ActiveRequest->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	ActiveRequest->SetHeader(TEXT("User-Agent"), TEXT("AscendSpire/1.0"));
	if (!Settings.ApiKey.IsEmpty())
	{
		ActiveRequest->SetHeader(TEXT("Authorization"), FString::Printf(TEXT("Bearer %s"), *Settings.ApiKey));
	}
	const float RequestTimeout = FMath::Clamp(Settings.TimeoutSeconds, 5.f, 180.f);
	ActiveRequest->SetTimeout(RequestTimeout);
	ActiveRequest->SetContentAsString(Payload);
	WriterRequestStartedAt = FPlatformTime::Seconds();
	UE_LOG(LogTemp, Log, TEXT("[InfiniteRP] writer endpoint=%s model=%s stream=%s reasoning_requested=%s payload_chars=%d messages=%d max_output_tokens=%d timeout=%.0fs %s"),
		*Settings.Endpoint, *Settings.Model, bWriterStreaming ? TEXT("true") : TEXT("false"),
		Settings.bRequestReasoning ? TEXT("true") : TEXT("false"), Payload.Len(), Messages.Num(),
		EffectiveMaxOutputTokens, RequestTimeout, *PromptDiagnostic);
	if (bWriterStreaming)
	{
		const uint64 StreamGeneration = GenerationSerial;
		TWeakObjectPtr<UInfiniteNarrativeService> WeakThis(this);
		const bool bReceiveStreamReady = ActiveRequest->SetResponseBodyReceiveStreamDelegateV2(
			FHttpRequestStreamDelegateV2::CreateLambda(
				[WeakThis, StreamGeneration](void* Ptr, int64& InOutLength)
				{
					if (WeakThis.IsValid() && WeakThis->GenerationSerial == StreamGeneration)
						WeakThis->HandleWriterStreamBytes(Ptr, InOutLength);
				}));
		if (!bReceiveStreamReady)
		{
			UE_LOG(LogTemp, Warning, TEXT("[InfiniteRP] platform refused response stream; retrying non-streaming"));
			bWriterStreaming = false;
			Root->SetBoolField(TEXT("stream"), false);
			Payload.Reset();
			const TSharedRef<TJsonWriter<>> FallbackWriter = TJsonWriterFactory<>::Create(&Payload);
			FJsonSerializer::Serialize(Root.ToSharedRef(), FallbackWriter);
			ActiveRequest->SetContentAsString(Payload);
		}
	}
	ActiveRequest->OnProcessRequestComplete().BindUObject(this, &UInfiniteNarrativeService::HandleHttpComplete);
	if (!ActiveRequest->ProcessRequest())
	{
		CompleteWithError(TEXT("LLM 请求未能启动"));
	}
}

void UInfiniteNarrativeService::HandleHttpComplete(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bSucceeded)
{
	if (!ActiveRequest.IsValid() || Request != ActiveRequest)
	{
		UE_LOG(LogTemp, Display, TEXT("[InfiniteRP] ignored stale/cancelled writer completion"));
		return;
	}
	ActiveRequest.Reset();
	if (!bSucceeded || !Response.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("[InfiniteRP] request failed succeeded=%s response_valid=%s"),
			bSucceeded ? TEXT("true") : TEXT("false"), Response.IsValid() ? TEXT("true") : TEXT("false"));
		FInfiniteNarrativeBeat Fallback = BuildFallbackBeat(PendingContext, TEXT("LLM连接失败，本轮使用本地应急分支"));
		for (int32 Index = 0; Index < Fallback.Choices.Num(); ++Index) ApplyChoiceRoutePlan(Fallback.Choices[Index], Index);
		CompleteSuccess(MoveTemp(Fallback));
		return;
	}
	const FString ResponseBody = bWriterStreaming ? BuildWriterStreamTransportResponse()
		: Response->GetContentAsString();
	UE_LOG(LogTemp, Log, TEXT("[InfiniteRP] response status=%d body_chars=%d streamed=%s"),
		Response->GetResponseCode(), ResponseBody.Len(), bWriterStreaming ? TEXT("true") : TEXT("false"));
	LogTransportMetrics(TEXT("writer"), ResponseBody,
		FMath::Max(0.0, FPlatformTime::Seconds() - WriterRequestStartedAt));
	if (Response->GetResponseCode() < 200 || Response->GetResponseCode() >= 300)
	{
		UE_LOG(LogTemp, Warning, TEXT("[InfiniteRP] single narrative call returned HTTP %d; using local fallback"),
			Response->GetResponseCode());
		FInfiniteNarrativeBeat Fallback = BuildFallbackBeat(PendingContext,
			FString::Printf(TEXT("LLM HTTP %d，本轮使用本地应急分支"), Response->GetResponseCode()));
		for (int32 Index = 0; Index < Fallback.Choices.Num(); ++Index) ApplyChoiceRoutePlan(Fallback.Choices[Index], Index);
		CompleteSuccess(MoveTemp(Fallback));
		return;
	}

	PendingDraftJson = ExtractTransportContent(ResponseBody).TrimStartAndEnd();
	if (PendingDraftJson.IsEmpty())
	{
		FInfiniteNarrativeBeat Fallback = BuildFallbackBeat(PendingContext, TEXT("剧情响应为空，本轮使用本地应急分支"));
		for (int32 Index = 0; Index < Fallback.Choices.Num(); ++Index) ApplyChoiceRoutePlan(Fallback.Choices[Index], Index);
		CompleteSuccess(MoveTemp(Fallback));
		return;
	}
	FString DraftError;
	PendingDraftBeat = FInfiniteNarrativeBeat();
	bHasPendingDraftBeat = ParseResponse(ResponseBody, PendingDraftBeat, DraftError);
	if (!bHasPendingDraftBeat)
	{
		UE_LOG(LogTemp, Warning, TEXT("[InfiniteRP] writer response is not renderable: %s"), *DraftError);
		FInfiniteNarrativeBeat Fallback = BuildFallbackBeat(PendingContext,
			TEXT("剧情响应格式异常，本轮使用本地应急分支；日志已记录：") + DraftError.Left(240));
		for (int32 Index = 0; Index < Fallback.Choices.Num(); ++Index) ApplyChoiceRoutePlan(Fallback.Choices[Index], Index);
		CompleteSuccess(MoveTemp(Fallback));
		return;
	}
	PendingDraftBeat.Diagnostic = TEXT("剧情、页面路由与状态差分由单轮导演直接生成；MVU已停用");
	UE_LOG(LogTemp, Display, TEXT("[InfiniteRP] direct writer beat accepted chars=%d choices=%d; mvu_requests=0"),
		PendingDraftJson.Len(), PendingDraftBeat.Choices.Num());
	CompleteSuccess(PendingDraftBeat);
}

void UInfiniteNarrativeService::ResetWriterStreamState()
{
	FScopeLock Lock(&WriterStreamCriticalSection);
	WriterStreamPendingBytes.Reset();
	WriterStreamAllBytes.Reset();
	WriterStreamContent.Reset();
	WriterStreamReasoning.Reset();
	WriterStreamFinishReason.Reset();
	bWriterStreamSawSse = false;
	bWriterStreamDone = false;
	bWriterStreamUpdateQueued = false;
	LastWriterStreamUpdateAt = 0.0;
}

void UInfiniteNarrativeService::HandleWriterStreamBytes(void* Ptr, int64& InOutLength)
{
	if (!Ptr || InOutLength <= 0) return;
	TArray<FString> CompleteLines;
	{
		FScopeLock Lock(&WriterStreamCriticalSection);
		const uint8* Bytes = static_cast<const uint8*>(Ptr);
		WriterStreamAllBytes.Append(Bytes, InOutLength);
		WriterStreamPendingBytes.Append(Bytes, InOutLength);
		int32 LineStart = 0;
		for (int32 Index = 0; Index < WriterStreamPendingBytes.Num(); ++Index)
		{
			if (WriterStreamPendingBytes[Index] != static_cast<uint8>('\n')) continue;
			int32 LineLength = Index - LineStart;
			if (LineLength > 0 && WriterStreamPendingBytes[LineStart + LineLength - 1] == static_cast<uint8>('\r'))
				--LineLength;
			if (LineLength > 0)
			{
				const FUTF8ToTCHAR Converted(reinterpret_cast<const ANSICHAR*>(
					WriterStreamPendingBytes.GetData() + LineStart), LineLength);
				CompleteLines.Add(FString(Converted.Length(), Converted.Get()));
			}
			LineStart = Index + 1;
		}
		if (LineStart > 0) WriterStreamPendingBytes.RemoveAt(0, LineStart, EAllowShrinking::No);
	}
	for (const FString& Line : CompleteLines) ProcessWriterSseLine(Line);
}

void UInfiniteNarrativeService::ProcessWriterSseLine(const FString& Line)
{
	FString Data = Line;
	Data.TrimStartAndEndInline();
	if (!Data.StartsWith(TEXT("data:"), ESearchCase::IgnoreCase)) return;
	Data.RightChopInline(5);
	Data.TrimStartAndEndInline();
	if (Data.IsEmpty()) return;
	if (Data == TEXT("[DONE]"))
	{
		FScopeLock Lock(&WriterStreamCriticalSection);
		bWriterStreamSawSse = true;
		bWriterStreamDone = true;
		return;
	}

	TSharedPtr<FJsonObject> Event;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Data);
	if (!FJsonSerializer::Deserialize(Reader, Event) || !Event.IsValid()) return;
	const TArray<TSharedPtr<FJsonValue>>* Choices = nullptr;
	if (!Event->TryGetArrayField(TEXT("choices"), Choices) || !Choices || Choices->Num() == 0) return;
	const TSharedPtr<FJsonObject> Choice = (*Choices)[0].IsValid() ? (*Choices)[0]->AsObject() : nullptr;
	if (!Choice.IsValid()) return;
	const TSharedPtr<FJsonObject>* Delta = nullptr;
	if (!Choice->TryGetObjectField(TEXT("delta"), Delta) || !Delta || !Delta->IsValid()) return;

	FString ContentDelta;
	FString ReasoningDelta;
	(*Delta)->TryGetStringField(TEXT("content"), ContentDelta);
	if (!(*Delta)->TryGetStringField(TEXT("reasoning_content"), ReasoningDelta))
	{
		if (!(*Delta)->TryGetStringField(TEXT("reasoning"), ReasoningDelta))
			(*Delta)->TryGetStringField(TEXT("thinking"), ReasoningDelta);
	}
	FString FinishReason;
	Choice->TryGetStringField(TEXT("finish_reason"), FinishReason);
	{
		FScopeLock Lock(&WriterStreamCriticalSection);
		bWriterStreamSawSse = true;
		WriterStreamContent += ContentDelta;
		WriterStreamReasoning += ReasoningDelta;
		if (!FinishReason.IsEmpty()) WriterStreamFinishReason = FinishReason;
	}
	if (!ContentDelta.IsEmpty() || !ReasoningDelta.IsEmpty()) QueueWriterStreamUpdate();
}

void UInfiniteNarrativeService::QueueWriterStreamUpdate()
{
	{
		FScopeLock Lock(&WriterStreamCriticalSection);
		if (bWriterStreamUpdateQueued) return;
		bWriterStreamUpdateQueued = true;
	}
	TWeakObjectPtr<UInfiniteNarrativeService> WeakThis(this);
	AsyncTask(ENamedThreads::GameThread, [WeakThis]()
	{
		if (WeakThis.IsValid()) WeakThis->EmitWriterStreamUpdate();
	});
}

void UInfiniteNarrativeService::EmitWriterStreamUpdate()
{
	const double Now = FPlatformTime::Seconds();
	if (RequestPhase == ERequestPhase::Generation && LastWriterStreamUpdateAt > 0.0
		&& Now - LastWriterStreamUpdateAt < 0.04)
	{
		FScopeLock Lock(&WriterStreamCriticalSection);
		bWriterStreamUpdateQueued = false;
		return;
	}
	LastWriterStreamUpdateAt = Now;
	FString Content;
	int32 ReasoningChars = 0;
	{
		FScopeLock Lock(&WriterStreamCriticalSection);
		Content = WriterStreamContent;
		ReasoningChars = WriterStreamReasoning.Len();
		bWriterStreamUpdateQueued = false;
	}
	if (!PendingStreamUpdate.IsBound()) return;
	FInfiniteNarrativeStreamUpdate Update;
	Update.Preview = ParseStreamingScenePreview(Content);
	Update.ReasoningChars = ReasoningChars;
	Update.bHasVisibleContent = !Update.Preview.Title.IsEmpty() || !Update.Preview.Narration.IsEmpty()
		|| Update.Preview.DialogueLines.Num() > 0;
	Update.Stage = RequestPhase == ERequestPhase::StateCompilation
		? EInfiniteNarrativeStreamStage::Compiling
		: (Update.bHasVisibleContent || !Content.IsEmpty()
			? EInfiniteNarrativeStreamStage::Writing : EInfiniteNarrativeStreamStage::Thinking);
	PendingStreamUpdate.Execute(Update);
}

void UInfiniteNarrativeService::EmitStreamStage(EInfiniteNarrativeStreamStage Stage)
{
	if (!PendingStreamUpdate.IsBound()) return;
	FInfiniteNarrativeStreamUpdate Update;
	{
		FScopeLock Lock(&WriterStreamCriticalSection);
		Update.Preview = ParseStreamingScenePreview(WriterStreamContent);
		Update.ReasoningChars = WriterStreamReasoning.Len();
	}
	Update.Stage = Stage;
	Update.bHasVisibleContent = !Update.Preview.Title.IsEmpty() || !Update.Preview.Narration.IsEmpty()
		|| Update.Preview.DialogueLines.Num() > 0;
	PendingStreamUpdate.Execute(Update);
}

FString UInfiniteNarrativeService::BuildWriterStreamTransportResponse()
{
	TArray<uint8> TrailingBytes;
	{
		FScopeLock Lock(&WriterStreamCriticalSection);
		TrailingBytes = WriterStreamPendingBytes;
		WriterStreamPendingBytes.Reset();
	}
	if (TrailingBytes.Num() > 0)
	{
		const FUTF8ToTCHAR Converted(reinterpret_cast<const ANSICHAR*>(TrailingBytes.GetData()),
			TrailingBytes.Num());
		ProcessWriterSseLine(FString(Converted.Length(), Converted.Get()));
	}

	FString RawBody;
	FString Content;
	FString Reasoning;
	FString FinishReason;
	bool bSawSse = false;
	{
		FScopeLock Lock(&WriterStreamCriticalSection);
		if (WriterStreamAllBytes.Num() > 0)
		{
			const FUTF8ToTCHAR Converted(reinterpret_cast<const ANSICHAR*>(WriterStreamAllBytes.GetData()),
				WriterStreamAllBytes.Num());
			RawBody = FString(Converted.Length(), Converted.Get());
		}
		Content = WriterStreamContent;
		Reasoning = WriterStreamReasoning;
		FinishReason = WriterStreamFinishReason;
		bSawSse = bWriterStreamSawSse;
	}
	if (!bSawSse) return RawBody;

	TSharedPtr<FJsonObject> Message = MakeShared<FJsonObject>();
	Message->SetStringField(TEXT("content"), Content);
	if (!Reasoning.IsEmpty()) Message->SetStringField(TEXT("reasoning_content"), Reasoning);
	TSharedPtr<FJsonObject> Choice = MakeShared<FJsonObject>();
	Choice->SetObjectField(TEXT("message"), Message);
	Choice->SetStringField(TEXT("finish_reason"), FinishReason.IsEmpty() ? TEXT("stop") : FinishReason);
	TArray<TSharedPtr<FJsonValue>> Choices;
	Choices.Add(MakeShared<FJsonValueObject>(Choice));
	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetArrayField(TEXT("choices"), Choices);
	return JsonString(Root);
}

void UInfiniteNarrativeService::IssueStateCompilation(const FString& DraftJson)
{
	if (!ReserveModelRequest(TEXT("mvu"))) return;
	RequestPhase = ERequestPhase::StateCompilation;
	const FString CompilerModel = PendingSettings.MvuVerifierModel.TrimStartAndEnd().IsEmpty()
		? PendingSettings.Model : PendingSettings.MvuVerifierModel.TrimStartAndEnd();

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("model"), CompilerModel);
	ApplyGenerationControls(Root, MvuPreset, true);
	Root->SetNumberField(TEXT("max_tokens"), EffectiveMvuMaxOutputTokens);
	TSharedPtr<FJsonObject> ResponseFormat = MakeShared<FJsonObject>();
	ResponseFormat->SetStringField(TEXT("type"), TEXT("json_object"));
	Root->SetObjectField(TEXT("response_format"), ResponseFormat);

	TArray<TSharedPtr<FJsonValue>> Messages;
	FString PromptDiagnostic;
	for (const FNarrativePromptMessage& PromptMessage : BuildMvuMessages(DraftJson, PromptDiagnostic))
	{
		TSharedPtr<FJsonObject> Message = MakeShared<FJsonObject>();
		Message->SetStringField(TEXT("role"), PromptMessage.Role);
		Message->SetStringField(TEXT("content"), PromptMessage.Content);
		Messages.Add(MakeShared<FJsonValueObject>(Message));
	}
	Root->SetArrayField(TEXT("messages"), Messages);

	FString Payload;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Payload);
	FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);
	ActiveRequest = FHttpModule::Get().CreateRequest();
	ActiveRequest->SetURL(ResolveChatCompletionsUrl(PendingSettings.Endpoint));
	ActiveRequest->SetVerb(TEXT("POST"));
	ActiveRequest->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	ActiveRequest->SetHeader(TEXT("User-Agent"), TEXT("AscendSpire/1.0"));
	if (!PendingSettings.ApiKey.IsEmpty())
		ActiveRequest->SetHeader(TEXT("Authorization"), FString::Printf(TEXT("Bearer %s"), *PendingSettings.ApiKey));
	ActiveRequest->SetTimeout(FMath::Clamp(PendingSettings.TimeoutSeconds, 5.f, 180.f));
	ActiveRequest->SetContentAsString(Payload);
	MvuRequestStartedAt = FPlatformTime::Seconds();
	UE_LOG(LogTemp, Log, TEXT("[InfiniteRP] MVU compiler model=%s reasoning_requested=%s historyless=true choices_only=true payload_chars=%d messages=%d %s"),
		*CompilerModel, PendingSettings.bRequestMvuReasoning ? TEXT("true") : TEXT("false"),
		Payload.Len(), Messages.Num(), *PromptDiagnostic);
	ActiveRequest->OnProcessRequestComplete().BindUObject(this,
		&UInfiniteNarrativeService::HandleStateCompilationComplete);
	if (!ActiveRequest->ProcessRequest())
		CompleteBestEffort(TEXT("MVU 状态编译请求未能启动"));
}

void UInfiniteNarrativeService::HandleStateCompilationComplete(FHttpRequestPtr Request,
	FHttpResponsePtr Response, bool bSucceeded)
{
	if (!ActiveRequest.IsValid() || Request != ActiveRequest)
	{
		UE_LOG(LogTemp, Display, TEXT("[InfiniteRP] ignored stale/cancelled MVU completion"));
		return;
	}
	ActiveRequest.Reset();
	if (!bSucceeded || !Response.IsValid())
	{
		CompleteBestEffort(TEXT("MVU 状态编译连接失败或超时"));
		return;
	}
	UE_LOG(LogTemp, Log, TEXT("[InfiniteRP] MVU compiler response status=%d body_chars=%d"),
		Response->GetResponseCode(), Response->GetContentAsString().Len());
	LogTransportMetrics(TEXT("mvu"), Response->GetContentAsString(),
		FMath::Max(0.0, FPlatformTime::Seconds() - MvuRequestStartedAt));
	if (Response->GetResponseCode() < 200 || Response->GetResponseCode() >= 300)
	{
		const FString ErrorBody = Response->GetContentAsString();
		if ((Response->GetResponseCode() == 400 || Response->GetResponseCode() == 422)
			&& LooksLikeTokenLimitError(ErrorBody) && EffectiveMvuMaxOutputTokens > 4096 && MvuTokenCapAttempt < 3)
		{
			static const int32 FallbackCaps[] = {16384, 8192, 4096};
			EffectiveMvuMaxOutputTokens = FMath::Min(EffectiveMvuMaxOutputTokens - 1,
				FallbackCaps[MvuTokenCapAttempt]);
			++MvuTokenCapAttempt;
			UE_LOG(LogTemp, Warning, TEXT("[InfiniteRP] provider rejected MVU max_tokens; retrying with cap=%d"),
				EffectiveMvuMaxOutputTokens);
			IssueStateCompilation(PendingDraftJson);
			return;
		}
		CompleteBestEffort(FString::Printf(TEXT("MVU 状态编译 HTTP %d"), Response->GetResponseCode()));
		return;
	}

	FInfiniteNarrativeBeat Beat;
	FString Error;
	if (!MergeCompilerResponse(Response->GetContentAsString(), Beat, Error))
	{
		UE_LOG(LogTemp, Warning, TEXT("[InfiniteRP] MVU compiler output unusable: %s"), *Error);
		// Once phase one is parseable it is immutable. Every semantic or tool error from
		// phase two is repaired by phase two itself, so the player never sees the accepted
		// story replaced merely because its first mechanical compilation was invalid.
		if (MvuSemanticRetryAttempt < 2 && TotalModelRequestCount < 4)
		{
			++MvuSemanticRetryAttempt;
			LastMvuValidationError = Error.Left(2000);
			UE_LOG(LogTemp, Display, TEXT("[InfiniteRP] targeted MVU repair attempt=%d error=%s"),
				MvuSemanticRetryAttempt, *LastMvuValidationError);
			IssueStateCompilation(PendingDraftJson);
			return;
		}
		CompleteBestEffort(FString::Printf(TEXT("MVU 状态编译格式错误：%s"), *Error));
		return;
	}
	Beat.Diagnostic = TEXT("剧情生成与 MVU 状态编译均完成");
	CompleteSuccess(Beat);
}

void UInfiniteNarrativeService::ApplyGenerationControls(const TSharedPtr<FJsonObject>& Root,
	const FNarrativeGenerationPreset& Preset, bool bMvu) const
{
	if (!Root.IsValid()) return;
	const float Temperature = bMvu ? Preset.Temperature : PendingSettings.Temperature;
	Root->SetNumberField(TEXT("temperature"), FMath::Clamp(Temperature, 0.f, 2.f));
	Root->SetNumberField(TEXT("top_p"), FMath::Clamp(bMvu ? Preset.TopP : PendingSettings.TopP, 0.f, 1.f));
	Root->SetNumberField(TEXT("frequency_penalty"), FMath::Clamp(
		bMvu ? Preset.FrequencyPenalty : PendingSettings.FrequencyPenalty, -2.f, 2.f));
	Root->SetNumberField(TEXT("presence_penalty"), FMath::Clamp(
		bMvu ? Preset.PresencePenalty : PendingSettings.PresencePenalty, -2.f, 2.f));
	const float TopK = bMvu ? Preset.TopK : PendingSettings.TopK;
	const float TopA = bMvu ? Preset.TopA : PendingSettings.TopA;
	const float MinP = bMvu ? Preset.MinP : PendingSettings.MinP;
	const float RepetitionPenalty = bMvu ? Preset.RepetitionPenalty : PendingSettings.RepetitionPenalty;
	const int32 Seed = bMvu ? Preset.Seed : PendingSettings.Seed;
	if (TopK > 0.f) Root->SetNumberField(TEXT("top_k"), TopK);
	if (TopA > 0.f) Root->SetNumberField(TEXT("top_a"), TopA);
	if (MinP > 0.f) Root->SetNumberField(TEXT("min_p"), FMath::Clamp(MinP, 0.f, 1.f));
	if (!FMath::IsNearlyEqual(RepetitionPenalty, 1.f))
		Root->SetNumberField(TEXT("repetition_penalty"), FMath::Clamp(RepetitionPenalty, 0.f, 2.f));
	if (Seed >= 0) Root->SetNumberField(TEXT("seed"), Seed);

	TArray<FString> Stops = Preset.StopStrings;
	TArray<FString> UserStops;
	PendingSettings.StopStrings.ParseIntoArrayLines(UserStops, true);
	for (FString Stop : UserStops)
	{
		Stop.TrimStartAndEndInline();
		if (!Stop.IsEmpty()) Stops.AddUnique(Stop);
	}
	if (Stops.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> Values;
		for (const FString& Stop : Stops) Values.Add(MakeShared<FJsonValueString>(Stop));
		Root->SetArrayField(TEXT("stop"), Values);
	}

	FString Effort = bMvu ? PendingSettings.MvuReasoningEffort : PendingSettings.ReasoningEffort;
	if (Effort.IsEmpty()) Effort = Preset.ReasoningEffort;
	Effort.TrimStartAndEndInline();
	Effort.ToLowerInline();
	bool bRequestReasoning = bMvu ? PendingSettings.bRequestMvuReasoning
		: PendingSettings.bRequestReasoning;
	if (Effort == TEXT("disabled") || Effort == TEXT("off") || Effort == TEXT("none"))
		bRequestReasoning = false;
	const FString EffectiveModel = bMvu && !PendingSettings.MvuVerifierModel.TrimStartAndEnd().IsEmpty()
		? PendingSettings.MvuVerifierModel.TrimStartAndEnd() : PendingSettings.Model.TrimStartAndEnd();
	if (EffectiveModel.StartsWith(TEXT("deepseek-v4"), ESearchCase::IgnoreCase))
	{
		TSharedPtr<FJsonObject> Thinking = MakeShared<FJsonObject>();
		Thinking->SetStringField(TEXT("type"), bRequestReasoning ? TEXT("enabled") : TEXT("disabled"));
		Root->SetObjectField(TEXT("thinking"), Thinking);
		if (bRequestReasoning && !Effort.IsEmpty() && Effort != TEXT("auto"))
		{
			// DeepSeek V4 maps low/medium/high to high; xhigh/max map to max.
			Root->SetStringField(TEXT("reasoning_effort"),
				(Effort == TEXT("max") || Effort == TEXT("maximum") || Effort == TEXT("xhigh"))
					? TEXT("max") : TEXT("high"));
		}
		return;
	}
	// Generic OpenAI-compatible endpoints are not guaranteed to accept a thinking toggle.
	// Keep Auto as pass-through and only send effort when thinking was explicitly requested.
	if (!bRequestReasoning && (Effort == TEXT("disabled") || Effort == TEXT("off") || Effort == TEXT("none")))
	{
		TSharedPtr<FJsonObject> Thinking = MakeShared<FJsonObject>();
		Thinking->SetStringField(TEXT("type"), TEXT("disabled"));
		Root->SetObjectField(TEXT("thinking"), Thinking);
	}
	else if (bRequestReasoning && !Effort.IsEmpty() && Effort != TEXT("auto"))
	{
		Root->SetStringField(TEXT("reasoning_effort"), Effort);
	}
}

TArray<FNarrativePromptMessage> UInfiniteNarrativeService::BuildNarrativeMessages(FString& OutDiagnostic) const
{
	FNarrativePromptBuildContext Build;
	Build.GenerationType = PendingContext.bCombatPrefetch ? TEXT("combat_prefetch") : TEXT("normal");
	Build.ChatHistory = PendingContext.ChatHistory;
	if (Build.ChatHistory.Num() == 0 && !PendingContext.RecentRawContext.IsEmpty())
		Build.ChatHistory.Add({TEXT("system"), TEXT("[兼容旧存档历史]\n") + PendingContext.RecentRawContext});
	const FString UserOrBuiltInWorldBook = PendingSettings.WorldBookOverride.TrimStartAndEnd().IsEmpty()
		? LoadWorldBook() : PendingSettings.WorldBookOverride.TrimStartAndEnd();
	FString EngineRouteWorldBook;
	FFileHelper::LoadFileToString(EngineRouteWorldBook,
		*(FPaths::ProjectContentDir() / TEXT("Data/rp_route_worldbook.json")));
	Build.WorldBookJson = MergeWorldBookEntries(UserOrBuiltInWorldBook, EngineRouteWorldBook);
	Build.CharacterRegistryJson = PendingSettings.CharacterRegistryOverride.TrimStartAndEnd().IsEmpty()
		? LoadCharacterRegistry() : PendingSettings.CharacterRegistryOverride.TrimStartAndEnd();
	Build.TokenBudget = FMath::Clamp(PendingSettings.InputContextTokens, 8192, 2000000);
	Build.WorldInfoScanDepth = FMath::Max(0, PendingSettings.WorldInfoScanDepth);

	FString TurnMode;
	if (PendingContext.bCombatPrefetch && PendingContext.bAssumeCombatVictoryWithoutLog)
		TurnMode = TEXT("战中预演模式B：请求虽在战斗中发出，但下一幕的时间点必须位于预定胜利之后；不得虚构具体战况。");
	else if (PendingContext.bCombatPrefetch)
		TurnMode = TEXT("战后模式A：玩家已胜利，可依据完整战斗日志自然回应战况。");
	else TurnMode = TEXT("普通RP续写：承接玩家刚才的选择，不替玩家决定下一步。");
	const FString ResolutionBlock = PendingContext.CombatResolutionFact.IsEmpty()
		? TEXT("")
		: TEXT("\n[不可覆盖的战斗结算事实]\n") + PendingContext.CombatResolutionFact
			+ TEXT("\n下一幕从该结论之后继续，不得重演本场战斗；scene、memory、state_patch和三个选项都不得与其矛盾。");
	const FString CurrentTurn = FString::Printf(TEXT(
		"生成第%d轮下一幕。%s%s\n玩家本轮自由行动：%s\n"
		"[本轮三个选项的引擎结果已经在调用前抽签确定]\n%s\n"
		"先服从这些结果，再倒推并编造自然、具体、有趣的选项行动与即时原因；不得改签、交换或淡化。\n"
		"最终回复必须是可解析的JSON对象：所有文学正文必须放入scene.narration或scene.messages，"
		"不得在JSON对象之外直接续写散文、标题或解释。"), PendingContext.Cycle, *TurnMode, *ResolutionBlock,
		PendingContext.FreeformAction.IsEmpty() ? TEXT("无，按既有上下文自然续写") : *PendingContext.FreeformAction,
		*DescribeChoiceRoutePlanForPrompt());
	const FString Abilities = PendingContext.AbilityNames.Num() > 0
		? FString::Join(PendingContext.AbilityNames, TEXT("、")) : TEXT("无");
	const FString Relics = PendingContext.RelicNames.Num() > 0
		? FString::Join(PendingContext.RelicNames, TEXT("、")) : TEXT("无");
	FString GameState = FString::Printf(TEXT(
		"玩家气血：%d/%d\n玩家灵石：%d\n玩家能力：%s\n玩家法宝与伙伴：%s"),
		PendingContext.HP, PendingContext.MaxHP, PendingContext.Gold, *Abilities, *Relics);
	GameState += TEXT("\n[开放世界连续状态]\n")
		+ (PendingContext.WorldStateJson.IsEmpty() ? TEXT("{}") : PendingContext.WorldStateJson);
	GameState += TEXT("\n[引擎权威的类型化剧情变量]\n")
		+ (PendingContext.EngineVariableContext.IsEmpty() ? TEXT("尚无") : PendingContext.EngineVariableContext);
	GameState += TEXT("\n[肉鸽节奏]\n本轮页面与数值结果已由引擎提前抽签；不要延续上一轮的机械效果。" );
	TArray<FString> RouteActivationLines;
	for (int32 Index = 0; Index < 3; ++Index)
	{
		const FString Route = PendingChoiceRoutePlan.IsValidIndex(Index)
			? PendingChoiceRoutePlan[Index] : TEXT("continue_rp");
		FString WorldBookKey = Route;
		if (Route == TEXT("hurt")) WorldBookKey = TEXT("hp_loss");
		else if (Route == TEXT("heal")) WorldBookKey = TEXT("hp_gain");
		else if (Route == TEXT("lose_gold")) WorldBookKey = TEXT("gold_loss");
		else if (Route == TEXT("gain_gold")) WorldBookKey = TEXT("gold_gain");
		RouteActivationLines.Add(FString::Printf(TEXT("%c=[[route.%s]]"),
			TCHAR(TEXT('A') + Index), *WorldBookKey));
	}
	GameState += TEXT("\n[仅供世界书召回的本轮引擎路由触发词]\n")
		+ FString::Join(RouteActivationLines, TEXT("\n"));
	if (PendingSettings.bEnableStructuredMemory && !PendingContext.RecalledMemoryContext.IsEmpty())
		GameState += TEXT("\n[长期记忆召回]\n") + PendingContext.RecalledMemoryContext;
	Build.GameState = GameState;
	Build.CombatContext = TEXT("[引擎权威战斗结算事实——优先于历史与开放世界连续状态]\n")
		+ (PendingContext.CombatResolutionFact.IsEmpty() ? TEXT("尚无已结算战斗") : PendingContext.CombatResolutionFact)
		+ TEXT("\n[当前或刚结束的战斗对象]\n")
		+ (PendingContext.CombatSetup.IsEmpty() ? TEXT("无") : PendingContext.CombatSetup)
		+ TEXT("\n[战斗日志]\n")
		+ (PendingContext.CombatDigest.IsEmpty() ? TEXT("本模式不提供本场日志") : PendingContext.CombatDigest);
	Build.AuthorNote = PendingSettings.AuthorNote;
	if (Build.AuthorNote.IsEmpty() && PendingSettings.bEnableContinuityChecklist)
	{
		Build.AuthorNote = PendingSettings.CustomContinuityChecklist.TrimStartAndEnd().IsEmpty()
			? TEXT("人物只知道其应知信息；承诺、伤势、物品归属和关系阶段不跳变。不要复述上一幕，结尾给出三个真正不同的可行动选项。")
			: PendingSettings.CustomContinuityChecklist.TrimStartAndEnd();
	}
	Build.OutputContract = BuildNarrativeOutputContract();
	Build.Macros.Add(TEXT("user"), TEXT("玩家"));
	Build.Macros.Add(TEXT("char"), TEXT("世界与NPC"));
	Build.Macros.Add(TEXT("persona"), PendingSettings.PersonaDescription);
	Build.Macros.Add(TEXT("description"), PendingSettings.CharacterDescription.IsEmpty()
		? Build.CharacterRegistryJson : PendingSettings.CharacterDescription);
	Build.Macros.Add(TEXT("personality"), PendingSettings.CharacterPersonality);
	FString Scenario = PendingSettings.Scenario;
	if (!PendingSettings.CustomWorldBook.TrimStartAndEnd().IsEmpty())
		Scenario += TEXT("\n[兼容旧版用户追加世界书]\n") + PendingSettings.CustomWorldBook.TrimStartAndEnd();
	Build.Macros.Add(TEXT("scenario"), Scenario);
	Build.Macros.Add(TEXT("mesExamples"), PendingSettings.DialogueExamples);
	Build.Macros.Add(TEXT("userInput"), PendingContext.FreeformAction);
	Build.Macros.Add(TEXT("currentTurn"), CurrentTurn);
	Build.Macros.Add(TEXT("capabilityManifest"), LoadCapabilityManifest());
	return FNarrativePromptManager::BuildMessages(NarrativePreset, Build, OutDiagnostic);
}

TArray<FNarrativePromptMessage> UInfiniteNarrativeService::BuildMvuMessages(
	const FString& DraftJson, FString& OutDiagnostic) const
{
	FNarrativePromptBuildContext Build;
	Build.GenerationType = TEXT("mvu");
	// MVU is deliberately historyless. It compiles the current locked draft against
	// a provenance-free authoritative snapshot; story history, prior MVU patches,
	// pacing counters and narrative worldbook are writer-only inputs.
	Build.WorldBookJson.Reset();
	const FString Abilities = PendingContext.AbilityNames.Num() > 0
		? FString::Join(PendingContext.AbilityNames, TEXT("、")) : TEXT("无");
	const FString Relics = PendingContext.RelicNames.Num() > 0
		? FString::Join(PendingContext.RelicNames, TEXT("、")) : TEXT("无");
	Build.GameState = FString::Printf(TEXT(
		"[当前权威数值]\nHP=%d/%d; gold=%d; deck_size=%d\n"
		"[已拥有能力名称]\n%s\n[已拥有法宝/伙伴名称]\n%s\n"
		"[引擎权威类型变量]\n%s"),
		PendingContext.HP, PendingContext.MaxHP, PendingContext.Gold, PendingContext.DeckSize,
		*Abilities, *Relics,
		PendingContext.EngineVariableContext.IsEmpty() ? TEXT("尚无") : *PendingContext.EngineVariableContext);
	if (!PendingContext.CombatResolutionFact.IsEmpty())
	{
		Build.GameState += TEXT("\n[不可覆盖的引擎战斗结算事实]\n")
			+ PendingContext.CombatResolutionFact
			+ TEXT("\n旧叙事变量中与此冲突的受伤、回防、战斗未定或仍在交战状态已经失效。"
				"不得把同一敌人编译为任一选项的新遭遇；memory与差分状态必须承认本遭遇已经结束。");
	}
	Build.OutputContract = BuildMvuOutputContract();
	if (MvuSemanticRetryAttempt > 0)
	{
		Build.OutputContract += TEXT("\n[本地沙箱定向修复]\n上一次编译结果被拒绝，具体错误：")
			+ (LastMvuValidationError.IsEmpty() ? TEXT("输出未满足结构或效果脚本约束") : LastMvuValidationError)
			+ TEXT("\n第一阶段剧情已经永久锁定，绝不要求或尝试重写；只纠正MVU中导致错误的字段并重新输出完整A/B/C。"
				"允许删除本来就不成立或领域误判的item/skill变量；若锁定结果没有引擎可执行或可持久化的状态转移，"
				"必须返回compile_status=no_executable_effect与原因，不要硬造环境、好感、警戒、奖励或无关抽牌卡。" );
	}
	Build.TokenBudget = FMath::Clamp(PendingSettings.InputContextTokens, 8192, 2000000);
	Build.WorldInfoScanDepth = 0;
	// The scene prose begins by settling the previously selected branch and can repeat
	// its old MVU-visible effects. Strip prose/root memory/state before compilation;
	// the current A/B/C facts are the compiler's complete narrative input.
	FString CurrentChoicesJson = DraftJson;
	TSharedPtr<FJsonObject> FullDraft;
	if (ParseModelJsonObject(DraftJson, FullDraft) && FullDraft.IsValid())
	{
		const TArray<TSharedPtr<FJsonValue>>* CurrentChoices = nullptr;
		if (FullDraft->TryGetArrayField(TEXT("choices"), CurrentChoices) && CurrentChoices)
		{
			TSharedPtr<FJsonObject> CompilerView = MakeShared<FJsonObject>();
			CompilerView->SetStringField(TEXT("schema_version"), TEXT("2.0-writer-choices-only"));
			CompilerView->SetArrayField(TEXT("choices"), *CurrentChoices);
			CurrentChoicesJson = JsonString(CompilerView);
		}
	}
	Build.Macros.Add(TEXT("draft"), CurrentChoicesJson);
	Build.Macros.Add(TEXT("authoringRules"), TEXT(
		"[职责边界] MVU不编写卡牌或法宝脚本。准确匹配目录名称时使用grant_card/grant_relic；"
		"否则只输出content_jobs，把已完成的source_fact、concept和mechanic_intent交给玩家选中后的独立工坊。"));
	Build.Macros.Add(TEXT("capabilityManifest"), LoadCapabilityManifest());
	FString ContentCatalog = BuildContentCatalog();
	Build.Macros.Add(TEXT("contentCatalog"), ContentCatalog);
	return FNarrativePromptManager::BuildMessages(MvuPreset, Build, OutDiagnostic);
}

FString UInfiniteNarrativeService::BuildNarrativeOutputContract() const
{
	return FString::Printf(TEXT(
		"只输出一个JSON对象，不要Markdown或思考。可见正文目标%d~%d中文字符。"
		"这是本轮唯一一次剧情调用：正文、三个选项、每个选项的即时结果和下一页面都在这里完成；没有MVU、GM裁决、任务系统或第二轮剧情修订。"
		"严格结构：{\"schema_version\":\"4.0-direct-route\",\"scene\":{\"title\":\"\",\"narration\":\"\","
		"\"messages\":[{\"speaker\":\"\",\"portrait_id\":\"\",\"expression\":\"neutral\",\"text\":\"\"}]},"
		"\"state_patch\":{},\"memory\":{\"title\":\"\",\"summary\":\"\",\"participants\":[],\"facts\":[],\"unresolved\":[],\"keywords\":[],\"importance\":1},"
		"\"choices\":[{\"choice_id\":\"A\",\"text\":\"玩家要做的事\",\"result_summary\":\"选中后立即发生的简短自然语言结果\","
		"\"next\":\"continue_rp|combat|card_forge|relic_reward|shop|reward|rest|upgrade|remove\",\"card_concept\":\"仅card_forge填写卡面概念名\","
		"\"variable_updates\":[],\"state_patch\":{},\"encounter\":{}},{\"choice_id\":\"B\",...},{\"choice_id\":\"C\",...}]}。"
		"choices恰好三项且顺序固定A/B/C。引擎已经在当前用户消息中逐项给出了不可更改的抽签结果；next必须逐字匹配。"
		"先接受抽签，再倒推一个符合前后文、具体而有戏剧性的原因。若抽到气血-10，就写踩中暗器、遭到反噬或类似明确事故；"
		"若抽到商店、休息、奖励、升级或剔除，就让result_summary明确说明为何此刻立刻进入该界面。不要输出operations或content_jobs。"
		"card_forge的result_summary必须明确玩家当场已经获得了某件物品、技艺、异常灵感或其他可玩概念，禁止写未来、准备、将会或等待锻造。"
		"card_concept只能是2~12个中文字符的卡面名称，不得有冒号、标点、解释或任何机制；不要写费用、效果、JSON卡牌、CardScript或生卡任务说明。"
		"选中后界面会离开RP，独立工坊只负责生卡并加入卡组，不会生成任何剧情。"
		"combat必须填写encounter：template_name从敌人模板名称逐字选择，并写faction_id/name/story/tier/hp_scale/intent_scale/abilities；"
		"结果停在第一击结算前。非combat的encounter留空。"
		"variable_updates是可选的长期人物/世界变化，只在结果明确支持时写；可用body/condition、item/ownership、skill/knowledge、"
		"relationship/affinity、relationship/bond、environment/location、environment/trait、environment/combat_edge、faction/alert、faction/trait。"
		"A/B/C没有气质、风险、收益或玩法含义上的区别，三项都从完全相同的本地带权池独立抽取；不得根据字母赋予固定风格。实际去向始终以本轮抽签为准。"
		"正文前1/3结算上一行动，随后迅速出现转折并铺垫三个抽签结果都能成立的因果抓手。上一轮机械效果不是模板。"
		"本游戏没有任务系统，禁止写当前任务、任务进度、objective或quest。结果可以正面、负面或混合，但必须已经发生，不能写可能、试图、若成功。"),
		FMath::Clamp(PendingSettings.NarrativeMinChars, 200, 20000),
		FMath::Max(PendingSettings.NarrativeMinChars, PendingSettings.NarrativeMaxChars));
#if 0 // Retained temporarily as migration reference; the direct contract above is authoritative.
	return FString::Printf(TEXT(
		"只输出一个JSON对象，不要Markdown或思考过程。可见正文目标%d~%d个中文字符，是软目标，不得为了字数破坏JSON。"
		"你是第一阶段剧情作者：只负责文学剧情、人物行动和分支事实，不设计奖励数值、卡牌字段或引擎操作。严格结构："
		"{\"schema_version\":\"2.0-writer\",\"scene\":{\"title\":\"\",\"narration\":\"\","
		"\"messages\":[{\"speaker\":\"\",\"portrait_id\":\"\",\"expression\":\"neutral\",\"text\":\"\"}]},"
		"\"state_patch\":{},\"memory\":{\"title\":\"\",\"summary\":\"\",\"participants\":[],\"facts\":[],\"unresolved\":[],\"keywords\":[],\"importance\":1},"
		"\"choices\":[{\"choice_id\":\"A\",\"text\":\"\",\"result_summary\":\"选择后立即揭晓的简短因果\","
		"\"consequence_intent\":\"后台使用的自然语言事实：谁完成了什么、所有权或状态如何改变、哪些相近变化没有发生\","
		"\"resolved_impact\":{\"kind\":\"narrative|acquire_item|lose_item|learn_ability|improve_owned_content|heal|hurt|gain_gold|lose_gold|trade|deck_edit|rest|reward|combat\",\"subject\":\"player\",\"object\":\"\",\"completed\":true,\"persistent\":true},"
		"\"next\":\"continue_rp|combat（仅为作者建议，第二阶段GM可纠正）\","
		"\"encounter_hint\":\"仅combat时说明敌人从何而来\"},"
		"{\"choice_id\":\"B\",...},{\"choice_id\":\"C\",...}]}。choices必须正好三项。"
		"A/B/C必须是三种真正不同且会推动剧情的行动，并长期保持以下倾向但不机械套模板："
		"A偏激进，优先主动破局、升级冲突或进入有明确来源的战斗，并允许在因果成立时同时取得内容、承担代价或改变战术态势；"
		"B偏搜刮与成长，优先取得、领悟、强化、交易、修整或编辑牌组，收益可以伴随副作用，但副作用不能吞掉成长本身；只丢物品、受伤、逃跑、战斗或得到情报不算B成长。明确获得可战斗招式、器物或持续能力时，必须把题材特征、主动用途、触发方式或代价写清，供后台原创内容；"
		"C偏戏剧化、反常规和富想象力，提供玩家可以主动选择的大胆、荒诞或出人意料行动，并让NPC依性格与关系真实回应；不得替玩家强制越界，也不得保证正面回报。C的result_summary必须写出这个疯狂行动已经造成的当场结算：例如交易确已成交或破裂、物品确已转手或损坏、关系态度确已改变、势力确已响应、身体确已受影响，或敌人已封路进入战斗。只有提出条件、发出邀请、披露秘密、口头威胁、将来可能或物品可能受损，都不算结算。"
		"三项倾向是行动与因果方向，不是固定奖励槽。上一轮的灵石、气血、好感、物品或其他机械结果只属于已发生历史，绝不是本轮应继续模仿的范例；本轮三个分支的机械结果应由当前新行动分别决定，避免三项落到同一种效果。"
		"A的主要效果不能只靠好感或普通环境充数；B必须至少明确发生一项取得可玩内容、学习/强化能力、资源交易、牌组编辑、商店/奖励/休整功能或身体修炼，单纯得到情报、好感、警戒或环境变化不算B的成长结果；C可以影响关系或任何其他真实工具，但三项不得共享同一种主要效果。"
		"每个result_summary都必须自然支持至少一项真实引擎变化：身体、物品所有权、能力、关系、势力警戒、下一战战术态势、牌组/商店/休整功能、资源或战斗；普通环境、地点、所谓任务和线索记录本身不足以冒充游戏效果，本游戏没有任务系统。"
		"后果不是奖励清单：允许正面、负面或混合结果。调查、谈判、拒绝、承诺与路线选择也必须在result_summary里写清实际发生的身体、物品、技能、好感、战术态势或敌对势力变化，供第二阶段编译；不得生成‘当前任务’一类系统回执。"
		"每幕在正文前1/3内结算上一选择，再加入反制、暴露、限时机会、代价或冲突。若真正事件受时辰、路程或等待门槛阻挡，正文应一次跳到有事发生的时刻；不得把空档拆成观察守卫、读告示、摸路线、打听、确认或等待等多个填充回合。"
		"观察、调查、交涉和等待可以是result_summary的手段，但后半句必须落到已经完成的机械事实。写完每项后检查：第二阶段GM能否不发明新剧情就直接调用工具或登记变量；不能就重写该项。后果不等于奖励，受伤、资源损失、关系恶化、敌对反制、战术劣势和战斗同样有效。"
		"成功脱身、暂时放行或安然通过若没有代价、内容、关系/势力响应或战术态势，仍然只是叙事。B的成长必须来自已出现的NPC、商店、战利品、可拆解旧物、亲历后的领悟或当前明确资源；不得在沟底、草丛、空箱或路边临时刷新恰好可捡的器物。"
		"消耗旧物逃走不是B成长，除非同一结果还明确得到新内容、强化或服务。若C靠NPC解围，必须让该NPC明确保护/结盟/决裂，或让敌对势力明确撤销/升级追捕；只赶走眼前敌人后恢复原状仍是叙事。"
		"输出前先为A/B/C选定互不重复的主要结果族；若B已经获得物品或地图，C不得再以获得另一物品为主要结果，应转向关系、势力反制、资源代价、身体、战术态势或战斗。交易中已经支付的通用货币必须写清数量，供GM映射为灵石。"
		"禁止只换语气或只问同义问题。result_summary写清行动后的直接因果；consequence_intent用自然语言准确陈述已完成事实，不填写数值和卡牌定义。第一阶段严禁写faction_alert、relationship/affinity、rp.items、variable_updates、operations、HP loss等内部字段，也不要直接指定好感±N或警戒±N；只写人物态度或势力响应如何实际改变，让第二阶段GM独立判断变量与幅度。resolved_impact只是给GM的可选语义提示，不是裁决；不确定时kind写narrative。"
		"若涉及获得或失去，明确行为是否完成、获得者/失去者是谁、是否持续持有；玩家把草药、木牌、信件、地图等实体收入怀中或带走时，consequence_intent必须明确写‘玩家已经取得并持续持有该物品’，不能只写‘获得线索’。不得把看见、尝试、拒绝、假设或他人的遭遇写成玩家已获得或受损。具体游戏实现由后台事件解释器决定。"
		"刺激只能来自已有历史、当前可见场景或NPC有因果的主动行动。内容名称目录只是设计目录，不代表玩家拥有；玩家所有权只以已拥有能力、已拥有法宝/伙伴和明确叙事状态为准。不得凭空增加或使用玩家未持有的传讯玉简、烟雾弹、毒药、信物、能力或后手，不得让脚下恰好藏着万能机关或道具，也不得把已经持有的同一剧情物品再次写成新获得。"
		"combat选项必须在result_summary或encounter_hint说明冲突与敌人来源，并停在双方真正交手之前；可以写拔剑、包围、弓已拉满或第一击将发，但任何一方的攻击命中、落空、格挡、闪避、伤害、武器脱手或成功脱离接触都已经越界。result_summary不得使用‘若成功’‘可能’‘试图’‘随时可能’；把悬而未决的威胁改写成已经发生的封路、追到门口、拔剑或限时命令。"
		"边界对照（只学习边界，禁止照抄情节）：错误A-result‘你斩断弓弦、逼退弓手，随后被包围’已经结算交手；正确A-result‘弓手拉满弓，六人从两侧包围，第一击将发’才交给战斗引擎。错误B-result‘获得敌人人数情报并暂时逃开’没有成长；正确B-result‘玩家已取得三粒丹药和一张藏宝图，丹药用途与地图寻宝概念明确’，副作用另写追兵逼近。错误C-result‘提出荒诞交易，对方尚未回答’没有结算；正确C-result必须写对方当场接受/拒绝、物品转手、态度改变、势力响应、身体变化或封路开战中的至少一项。"),
		FMath::Clamp(PendingSettings.NarrativeMinChars, 200, 20000),
		FMath::Max(PendingSettings.NarrativeMinChars, PendingSettings.NarrativeMaxChars));
#endif
}

FString UInfiniteNarrativeService::BuildMvuOutputContract() const
{
	return TEXT(
		"你是卡牌肉鸽的第二阶段GM、系统设计师与安全脚本编译器。你没有也不需要任何上一轮MVU输出或机械效果历史；只根据当前锁定草稿与无来源的当前权威快照，独立裁决本轮真正发生了什么，再把它编译为引擎可运行的差分。"
		"只输出一个JSON对象，不要生成scene、memory或根state_patch，不要改写text/result_summary/consequence_intent，不重写完整状态。你可以纠正第一阶段建议的next与resolved_impact。严格结构："
		"{\"schema_version\":\"3.1-engine-state\","
		"\"choices\":[{\"choice_id\":\"A\",\"compile_status\":\"compiled|no_executable_effect\",\"no_effect_reason\":\"仅no_executable_effect时说明锁定结果为何无法映射\",\"gm_judgement\":\"直接因果、正负性与幅度审计\",\"settlement_key\":\"同一事实的稳定短键\",\"resolved_impact\":{\"kind\":\"narrative|acquire_item|lose_item|learn_ability|improve_owned_content|heal|hurt|gain_gold|lose_gold|trade|deck_edit|rest|reward|combat\",\"subject\":\"player\",\"object\":\"\",\"completed\":true,\"persistent\":true},\"next\":\"continue_rp|combat\",\"state_patch\":{},"
		"\"variable_updates\":[{\"domain\":\"body|item|skill|relationship|environment|faction\",\"target\":\"stable_id_or_name\",\"field\":\"condition|ownership|knowledge|affinity|bond|location|trait|combat_edge|alert\",\"op\":\"add|set|remove|gain|lose|learn|upgrade|forget\",\"value\":\"\",\"amount\":0,\"duration\":0,\"causality\":\"direct|incidental\",\"valence\":\"positive|negative|mixed|neutral\",\"magnitude\":\"minor|moderate|major\"}],\"requirements\":[{\"type\":\"gold_at_least|hp_above|deck_at_least\",\"value\":1}],"
			"\"operations\":[{\"op\":\"hp|gold|grant_card|grant_relic|remove_card|remove_relic|choose_remove_card|choose_upgrade_card|open_shop|open_reward|open_rest\","
			"\"name\":\"已有内容名称\",\"value\":0,\"count\":1,\"rarity\":\"legendary\",\"price_multiplier\":1.5}],"
			"\"content_jobs\":[{\"kind\":\"card\",\"source_fact\":\"已完成的获得事实\",\"concept\":\"物品或能力意象\",\"mechanic_intent\":\"供独立工坊使用的题材特征与玩法方向\",\"acquisition\":\"gain\"}],"
			"\"rewards\":{\"created_cards\":[],\"created_relics\":[]},"
		"\"encounter\":{\"template_name\":\"已有敌人名称\",\"faction_id\":\"敌对势力稳定ID\",\"name\":\"剧情显示名\",\"story\":\"敌人来源\",\"tier\":\"normal\","
		"\"hp_scale\":1.0,\"intent_scale\":1.0,\"abilities\":[],\"ability_desc\":\"\"}},同结构B和C]}。"
		"裁决顺序：只读当前场景与选项，再读result_summary、consequence_intent和resolved_impact提示；区分已经发生、尝试、将来可能与拒绝，不盲从第一阶段标签，也不猜测当前状态由哪一轮造成。普通environment/trait、location和state_patch只维持连续性，不算活跃游戏效果。明确地形、伏击、暴露、陷阱或布防使用environment/combat_edge，amount为-3至+3，正数利于玩家、负数不利，下一场战斗后消耗。好感、身体和势力警戒必须用variable_updates，state_patch只保留其他世界事实。游戏没有任务系统，禁止输出progress/task、objective或‘当前任务’。已有卡牌、法宝和敌人只按名称引用，不输出ID。"
		"每项variable_update必须标注causality、valence和magnitude，只编译锁定结果的直接因果。禁止为了凑效果数量或让选项显得活跃而添加环境、好感、警戒、气血、灵石、物品、卡牌或战斗。若某个锁定result确实无法支撑任何真实引擎变化，必须令compile_status=no_executable_effect，填写具体no_effect_reason，并让variable_updates、operations、rewards保持空；本地若否决只会携带原因重试MVU，第一阶段剧情永远保持锁定。不得自行补发小好处或虚构代价。其余合法分支写compile_status=compiled。"
			"只有事实确实授予持久能力或可用物品时才处理内容。先查已有名称；能准确复用才grant_card，不能准确复用就输出content_jobs，不得在MVU中编写created_cards。content_jobs只描述已完成事实、概念和机制意向；独立工坊会在玩家真正选中该分支后生卡。"
		"同一已完成事实必须生成稳定settlement_key，例如acquire:雨庙木牌:雨庙初遇；同一事实跨轮再次出现仍使用同一键。它只防重复结算，不代表全局同名卡只能有一张。"
			"路线、暗号、消息、秘密和口头情报不是item；除非有地图、信件、纸卷、令牌等实体媒介真正转手，否则不能用ownership伪装，也不能单独输出content_job。只有实体媒介转手或玩家明确学会可持续使用的能力时才能送工坊。只要玩家在锁定结果中确实获得并持续持有一个或多个实体物品，就必须逐项登记item/ownership，并且复用准确现有卡或输出至少一个card content_job。多个同时获得的物品可合并为一项工坊任务。MVU禁止自行写卡牌effects。当前权威快照显示已持有、但不在已拥有能力/法宝名称中的东西，失去时只用item/ownership lose。若锁定结果只是口头情报或提到某技能，不得误判为item/skill。"
		"combat分支停在交手前；负数hp必须由锁定result_summary明确支持。真实气血增减只能使用operations中的hp，绝不能把hp_loss、hp_gain或数值伤害伪装成body/condition；prose没有给数值时由你根据minor/moderate/major选择克制幅度。追踪印记、中毒等非伤害body状态只有锁定结果明确写出该症状或印记时才可登记，不能另起名字并附会新机制；实际战斗伤害由战斗引擎结算。"
		"combat时先根据锁定结果中的敌人身份、战斗方式和危险程度，对照敌人模板原型指南选择语义最接近的已有战斗骨架；encounter.template_name必须从现有敌人模板名称目录逐字复制一个名称。剧情中的新敌人称号只写encounter.name和story，绝不能把新称号填进template_name。模板是战斗骨架而非剧情身份，选择已有模板不等于把剧情敌人改写成该模板角色。"
		"hp和gold使用有符号value；支付代价写负数。只说‘剔除一张牌’时用choose_remove_card，不替玩家选；指定失去某牌时用remove_card。"
		"剧情中灵石、铜钱、银钱等通用货币都映射到gold；result_summary明确写了支付、花费或被夺走多少货币时，必须输出对应负数gold并按需给gold_at_least要求，不能只编译所得物品。"
			"特殊仙品商店使用open_shop、rarity=legendary、price_multiplier=1.5。玩家已明确取得或学会的持久卡牌内容必须先查现有名称目录；有准确对象就复用，没有就输出content_jobs交给独立工坊。"
		"三个选项的未来变化只能放在各自choice中；本幕共同事实与记忆已经由第一阶段保存，不属于你的职责。第一阶段一旦锁定便不可修改；输出会被本地沙箱校验，若MVU字段无效，本地只把具体错误发回给MVU重做，最多总计四次模型请求，绝不无限循环。" );
}

bool UInfiniteNarrativeService::MergeCompilerResponse(const FString& ResponseBody,
	FInfiniteNarrativeBeat& OutBeat, FString& OutError) const
{
	auto ParseObject = [](const FString& Json, TSharedPtr<FJsonObject>& Out) -> bool
	{
		return ParseModelJsonObject(Json, Out);
	};

	TSharedPtr<FJsonObject> Draft;
	TSharedPtr<FJsonObject> Compiler;
	if (!ParseObject(PendingDraftJson, Draft))
	{
		OutError = TEXT("内部锁定剧情不是有效JSON");
		return false;
	}
	if (!ParseObject(ExtractTransportContent(ResponseBody), Compiler))
	{
		OutError = TEXT("编译器content不是有效JSON");
		return false;
	}
	FString CompilerSchema;
	if (Compiler->TryGetStringField(TEXT("schema_version"), CompilerSchema) && !CompilerSchema.IsEmpty())
		Draft->SetStringField(TEXT("schema_version"), CompilerSchema);

	const TArray<TSharedPtr<FJsonValue>>* DraftChoices = nullptr;
	const TArray<TSharedPtr<FJsonValue>>* CompiledChoices = nullptr;
	Draft->TryGetArrayField(TEXT("choices"), DraftChoices);
	Compiler->TryGetArrayField(TEXT("choices"), CompiledChoices);
	if (!DraftChoices || DraftChoices->Num() < 3 || !CompiledChoices || CompiledChoices->Num() < 3)
	{
		OutError = TEXT("编译器必须按A/B/C返回三个choices");
		return false;
	}
	static const TArray<FString> OverlayFields = {
		TEXT("compile_status"), TEXT("no_effect_reason"), TEXT("gm_judgement"),
		TEXT("settlement_key"), TEXT("resolved_impact"), TEXT("next"),
		TEXT("reward_timing"), TEXT("inventory_intent"), TEXT("inventory_item"),
		TEXT("state_patch"), TEXT("variable_updates"), TEXT("engine_updates"), TEXT("state_updates"),
			TEXT("requirements"), TEXT("operations"), TEXT("content_jobs"),
		TEXT("rewards"), TEXT("encounter")
	};
	for (int32 DraftIndex = 0; DraftIndex < 3; ++DraftIndex)
	{
		const TSharedPtr<FJsonObject> DraftChoice = (*DraftChoices)[DraftIndex]->AsObject();
		if (!DraftChoice.IsValid()) continue;
		const FString DraftId = GetString(DraftChoice, TEXT("choice_id"),
			FString::Chr(static_cast<TCHAR>(TEXT('A') + DraftIndex)));
		TSharedPtr<FJsonObject> CompiledChoice;
		for (const TSharedPtr<FJsonValue>& CandidateValue : *CompiledChoices)
		{
			const TSharedPtr<FJsonObject> Candidate = CandidateValue.IsValid() ? CandidateValue->AsObject() : nullptr;
			if (Candidate.IsValid() && GetString(Candidate, TEXT("choice_id")).Equals(DraftId, ESearchCase::IgnoreCase))
			{
				CompiledChoice = Candidate;
				break;
			}
		}
		if (!CompiledChoice.IsValid() && CompiledChoices->IsValidIndex(DraftIndex))
			CompiledChoice = (*CompiledChoices)[DraftIndex]->AsObject();
		if (!CompiledChoice.IsValid()) continue;
		for (const FString& Field : OverlayFields)
		{
			const TSharedPtr<FJsonValue> Value = CompiledChoice->TryGetField(Field);
			if (Value.IsValid()) DraftChoice->SetField(Field, Value);
		}
	}

	FString MergedJson;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&MergedJson);
	FJsonSerializer::Serialize(Draft.ToSharedRef(), Writer);
	if (!ParseResponse(MergedJson, OutBeat, OutError)) return false;
	// Belt-and-suspenders protection: parser-visible prose always comes from phase one.
	OutBeat.Title = PendingDraftBeat.Title;
	OutBeat.Speaker = PendingDraftBeat.Speaker;
	OutBeat.PortraitId = PendingDraftBeat.PortraitId;
	OutBeat.Expression = PendingDraftBeat.Expression;
	OutBeat.Narration = PendingDraftBeat.Narration;
	OutBeat.Dialogue = PendingDraftBeat.Dialogue;
	OutBeat.DialogueLines = PendingDraftBeat.DialogueLines;
	for (int32 Index = 0; Index < FMath::Min(OutBeat.Choices.Num(), PendingDraftBeat.Choices.Num()); ++Index)
	{
		OutBeat.Choices[Index].Text = PendingDraftBeat.Choices[Index].Text;
		OutBeat.Choices[Index].ResultSummary = PendingDraftBeat.Choices[Index].ResultSummary;
		OutBeat.Choices[Index].ConsequenceIntent = PendingDraftBeat.Choices[Index].ConsequenceIntent;
		if (OutBeat.Choices[Index].SettlementKey.IsEmpty())
		{
			const FString Object = OutBeat.Choices[Index].ResolvedImpactObject.IsEmpty()
				? OutBeat.Choices[Index].ConsequenceIntent : OutBeat.Choices[Index].ResolvedImpactObject;
			if (!OutBeat.Choices[Index].ResolvedImpactKind.IsEmpty() && !Object.IsEmpty())
				OutBeat.Choices[Index].SettlementKey = OutBeat.Choices[Index].ResolvedImpactKind.Left(48)
					+ TEXT(":") + Object.Left(96);
		}
	}
	return true;
}

void UInfiniteNarrativeService::CompleteSuccess(FInfiniteNarrativeBeat Beat)
{
	RequestPhase = ERequestPhase::Generation;
	PendingStreamUpdate.Unbind();
	if (PendingCompletion.IsBound())
	{
		FOnInfiniteNarrativeReady Completion = MoveTemp(PendingCompletion);
		Completion.Execute(true, Beat);
	}
}

void UInfiniteNarrativeService::CompleteBestEffort(const FString& Diagnostic)
{
	if (bHasPendingDraftBeat)
	{
		FInfiniteNarrativeBeat Beat = PendingDraftBeat;
		Beat.bFallback = true;
		Beat.Diagnostic = TEXT("MVU异常已记录，本轮使用保守剧情结算并继续游戏");
		for (int32 Index = 0; Index < Beat.Choices.Num(); ++Index)
		{
			FInfiniteNarrativeChoice& Choice = Beat.Choices[Index];
			const bool bHasReward = Choice.Reward.HPChange != 0 || Choice.Reward.GoldChange != 0
				|| Choice.Reward.Cards.Num() > 0 || Choice.Reward.RelicIds.Num() > 0
				|| Choice.Reward.RemovedCardIds.Num() > 0 || Choice.Reward.RemovedRelicIds.Num() > 0
				|| Choice.Reward.CreatedCards.Num() > 0 || Choice.Reward.CreatedRelics.Num() > 0;
			if (Choice.Next == TEXT("combat") || Choice.Operations.Num() > 0 || bHasReward
				|| Choice.VariableUpdates.Num() > 0)
				continue;

			FString Fact = Choice.ResolvedImpactObject.TrimStartAndEnd();
			if (Fact.IsEmpty()) Fact = Choice.ResultSummary.TrimStartAndEnd();
			if (Fact.IsEmpty()) Fact = Choice.ConsequenceIntent.TrimStartAndEnd();
			if (Fact.IsEmpty()) Fact = FString::Printf(TEXT("继续推进第%d项选择引发的剧情"), Index + 1);

			// A failed compiler must not invent a catch-all engine variable. The locked prose
			// remains playable as a narrative-only fallback, while every mechanical array
			// stays empty and the next turn receives no fictional task-system state.
			Choice.GmJudgement = TEXT("MVU未能编译出合法工具；本轮仅保留锁定叙事，不捏造任务、环境、好感、警戒、资源或内容奖励。" );
			if (Choice.SettlementKey.IsEmpty())
				Choice.SettlementKey = TEXT("fallback_fact:") + Fact.Left(140);
		}
		UE_LOG(LogTemp, Warning, TEXT("[InfiniteRP] MVU failed; preserving writer beat with conservative local settlement. diagnostic=%s"),
			*Diagnostic);
		CompleteSuccess(MoveTemp(Beat));
		return;
	}
	CompleteWithError(Diagnostic);
}

bool UInfiniteNarrativeService::ValidateStatePatch(const FString& PatchJson, FString& OutError) const
{
	OutError.Reset();
	if (PatchJson.IsEmpty() || PatchJson == TEXT("{}")) return true;
	if (PatchJson.Len() > 24000) { OutError = TEXT("state_patch 过长"); return false; }
	TSharedPtr<FJsonObject> Patch;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(PatchJson);
	if (!FJsonSerializer::Deserialize(Reader, Patch) || !Patch.IsValid())
	{
		OutError = TEXT("state_patch 不是 JSON 对象");
		return false;
	}
	static const TSet<FString> ProtectedRoots = {
		TEXT("hp"), TEXT("max_hp"), TEXT("gold"), TEXT("deck"), TEXT("cards"), TEXT("relics"),
		TEXT("combat"), TEXT("enemy"), TEXT("damage"), TEXT("rewards"), TEXT("save")
	};
	for (const auto& Pair : Patch->Values)
	{
		FString Lower(Pair.Key);
		Lower.ToLowerInline();
		if (ProtectedRoots.Contains(Lower))
		{
			OutError = FString::Printf(TEXT("不得改写游戏权威字段：%s"), *Pair.Key);
			return false;
		}
	}
	return true;
}

bool UInfiniteNarrativeService::ParseResponseForAutomationTest(const FString& ResponseBody,
	FInfiniteNarrativeBeat& OutBeat, FString& OutError) const
{
	return ParseResponse(ResponseBody, OutBeat, OutError);
}

FInfiniteNarrativeBeat UInfiniteNarrativeService::ParseStreamingPreviewForAutomationTest(
	const FString& PartialContent) const
{
	return ParseStreamingScenePreview(PartialContent);
}

bool UInfiniteNarrativeService::MergeCompilerResponseForAutomationTest(const FString& DraftJson,
	const FString& CompilerResponse, FInfiniteNarrativeBeat& OutBeat, FString& OutError)
{
	PendingDraftJson = DraftJson;
	bHasPendingDraftBeat = ParseResponse(DraftJson, PendingDraftBeat, OutError);
	if (!bHasPendingDraftBeat) return false;
	const ERequestPhase PreviousPhase = RequestPhase;
	RequestPhase = ERequestPhase::StateCompilation;
	const bool bMerged = MergeCompilerResponse(CompilerResponse, OutBeat, OutError);
	RequestPhase = PreviousPhase;
	return bMerged;
}

FString UInfiniteNarrativeService::ResolveAuthoringKnowledgeForAutomationTest(const FString& DraftJson)
{
	PendingDraftJson = DraftJson;
	return LoadTriggeredAuthoringKnowledge(FInfiniteNarrativeRequestContext(), false);
}

void UInfiniteNarrativeService::SetRecentNarrativeContextForAutomationTest(
	const FString& RecentRawContext)
{
	PendingContext.RecentRawContext = RecentRawContext;
}

TArray<FString> UInfiniteNarrativeService::PlanRoutesForAutomationTest(
	const FInfiniteNarrativeRequestContext& Context, int32 Seed, TArray<FString>* OutPayloads)
{
	const FInfiniteNarrativeRequestContext SavedContext = PendingContext;
	const FInfiniteNarrativeSettings SavedSettings = PendingSettings;
	const TArray<FString> SavedRoutes = PendingChoiceRoutePlan;
	const TArray<int32> SavedValues = PendingChoiceRouteValue;
	const TArray<FString> SavedPayloads = PendingChoiceRoutePayload;
	PendingContext = Context;
	PendingSettings.Seed = Seed;
	bSuppressRoutePlanLog = true;
	PrepareChoiceRoutePlan();
	bSuppressRoutePlanLog = false;
	const TArray<FString> Result = PendingChoiceRoutePlan;
	if (OutPayloads) *OutPayloads = PendingChoiceRoutePayload;
	PendingContext = SavedContext;
	PendingSettings = SavedSettings;
	PendingChoiceRoutePlan = SavedRoutes;
	PendingChoiceRouteValue = SavedValues;
	PendingChoiceRoutePayload = SavedPayloads;
	return Result;
}

bool UInfiniteNarrativeService::ParseResponse(const FString& ResponseBody, FInfiniteNarrativeBeat& OutBeat,
	FString& OutError) const
{
	OutBeat = FInfiniteNarrativeBeat();
	OutError.Reset();
	TSharedPtr<FJsonObject> Transport;
	const TSharedRef<TJsonReader<>> TransportReader = TJsonReaderFactory<>::Create(ResponseBody);
	if (!FJsonSerializer::Deserialize(TransportReader, Transport) || !Transport.IsValid())
	{
		OutError = TEXT("响应不是 JSON");
		return false;
	}

	FString Content;
	FString FinishReason;
	const TArray<TSharedPtr<FJsonValue>>* Choices = nullptr;
	if (Transport->TryGetArrayField(TEXT("choices"), Choices) && Choices->Num() > 0)
	{
		const TSharedPtr<FJsonObject> First = (*Choices)[0]->AsObject();
		const TSharedPtr<FJsonObject>* Message = nullptr;
		if (First.IsValid() && First->TryGetObjectField(TEXT("message"), Message))
		{
			(*Message)->TryGetStringField(TEXT("content"), Content);
		}
		if (First.IsValid()) First->TryGetStringField(TEXT("finish_reason"), FinishReason);
	}
	if (Content.IsEmpty())
	{
		// A direct schema object is useful for local gateways and tests.
		if (Transport->HasField(TEXT("scene")) && Transport->HasField(TEXT("choices"))) Content = ResponseBody;
	}
	if (Content.IsEmpty())
	{
		OutError = TEXT("缺少 choices[0].message.content");
		return false;
	}

	TSharedPtr<FJsonObject> Root;
	FString CleanContent;
	const bool bParsed = ParseModelJsonObject(Content, Root, &CleanContent);
	if (!bParsed)
	{
		FString Preview = CleanContent.Left(600);
		Preview.ReplaceInline(TEXT("\n"), TEXT("\\n"));
		UE_LOG(LogTemp, Warning, TEXT("[InfiniteRP] invalid content finish_reason=%s chars=%d preview=%s"),
			*FinishReason, CleanContent.Len(), *Preview);
		OutError = FString::Printf(TEXT("content 不是有效 JSON 对象（finish_reason=%s，长度=%d）"),
			FinishReason.IsEmpty() ? TEXT("unknown") : *FinishReason, CleanContent.Len());
		return false;
	}
	const FString SchemaVersion = GetString(Root, TEXT("schema_version"));
	const bool bRequireEngineTransition = SchemaVersion.StartsWith(TEXT("3.1"))
		|| SchemaVersion.StartsWith(TEXT("3.2"));

	const TSharedPtr<FJsonObject>* Scene = nullptr;
	if (!Root->TryGetObjectField(TEXT("scene"), Scene))
	{
		OutError = TEXT("缺少 scene");
		return false;
	}
	OutBeat.Title = GetString(*Scene, TEXT("title"), TEXT("山海余音"));
	OutBeat.Speaker = GetString(*Scene, TEXT("speaker"));
	OutBeat.PortraitId = GetString(*Scene, TEXT("portrait_id"));
	OutBeat.Expression = GetString(*Scene, TEXT("expression"), TEXT("neutral"));
	OutBeat.Narration = GetString(*Scene, TEXT("narration"));
	OutBeat.Dialogue = GetString(*Scene, TEXT("dialogue"));
	const TArray<TSharedPtr<FJsonValue>>* Messages = nullptr;
	if ((*Scene)->TryGetArrayField(TEXT("messages"), Messages) && Messages)
	{
		for (const TSharedPtr<FJsonValue>& Value : *Messages)
		{
			if (OutBeat.DialogueLines.Num() >= 16) break;
			const TSharedPtr<FJsonObject> Message = Value.IsValid() ? Value->AsObject() : nullptr;
			if (!Message.IsValid()) continue;
			FInfiniteDialogueLine Line;
			Line.Speaker = GetString(Message, TEXT("speaker"), OutBeat.Speaker);
			Line.PortraitId = GetString(Message, TEXT("portrait_id"), OutBeat.PortraitId);
			Line.Expression = GetString(Message, TEXT("expression"), TEXT("neutral"));
			Line.Text = GetString(Message, TEXT("text"));
			if (!Line.Text.IsEmpty()) OutBeat.DialogueLines.Add(Line);
		}
	}
	if (OutBeat.DialogueLines.Num() == 0 && !OutBeat.Dialogue.IsEmpty())
	{
		FInfiniteDialogueLine Line;
		Line.Speaker = OutBeat.Speaker;
		Line.PortraitId = OutBeat.PortraitId;
		Line.Expression = OutBeat.Expression;
		Line.Text = OutBeat.Dialogue;
		OutBeat.DialogueLines.Add(Line);
	}
	if (OutBeat.DialogueLines.Num() > 0)
	{
		if (OutBeat.Speaker.IsEmpty()) OutBeat.Speaker = OutBeat.DialogueLines[0].Speaker;
		if (OutBeat.PortraitId.IsEmpty()) OutBeat.PortraitId = OutBeat.DialogueLines[0].PortraitId;
		OutBeat.Expression = OutBeat.DialogueLines[0].Expression;
		TArray<FString> HistoryLines;
		for (const FInfiniteDialogueLine& Line : OutBeat.DialogueLines)
			HistoryLines.Add((Line.Speaker.IsEmpty() ? TEXT("旁白") : Line.Speaker) + TEXT("：") + Line.Text);
		OutBeat.Dialogue = FString::Join(HistoryLines, TEXT("\n"));
	}
	if (OutBeat.Narration.IsEmpty() && OutBeat.DialogueLines.Num() == 0)
	{
		OutError = TEXT("scene 没有可显示文本");
		return false;
	}
	int32 VisibleChars = OutBeat.Narration.Len();
	for (const FInfiniteDialogueLine& Line : OutBeat.DialogueLines) VisibleChars += Line.Text.Len();
	const int32 MinChars = FMath::Clamp(PendingSettings.NarrativeMinChars, 200, 20000);
	const int32 MaxChars = FMath::Max(MinChars, FMath::Clamp(PendingSettings.NarrativeMaxChars, 300, 30000));
	const bool bEnforcePresentationLength = !PendingSettings.Endpoint.TrimStartAndEnd().IsEmpty();
	if (bEnforcePresentationLength && VisibleChars < FMath::FloorToInt(MinChars * 0.75f))
	{
		UE_LOG(LogTemp, Warning, TEXT("[InfiniteRP] visible narrative is shorter than soft target: %d chars, target=%d~%d; accepting structurally valid response"),
			VisibleChars, MinChars, MaxChars);
	}
	if (bEnforcePresentationLength && VisibleChars > FMath::CeilToInt(MaxChars * 1.25f))
	{
		UE_LOG(LogTemp, Warning, TEXT("[InfiniteRP] visible narrative exceeds soft target: %d chars, target=%d~%d; accepting structurally valid response"),
			VisibleChars, MinChars, MaxChars);
	}

	const TSharedPtr<FJsonObject>* StatePatch = nullptr;
	if (Root->TryGetObjectField(TEXT("state_patch"), StatePatch)) OutBeat.StatePatchJson = JsonString(*StatePatch);
	{
		FString StatePatchError;
		if (!ValidateStatePatch(OutBeat.StatePatchJson, StatePatchError))
		{
			// Narrative is the primary channel. A malformed or over-broad world patch is
			// discarded locally so it cannot turn an otherwise readable beat into an error.
			UE_LOG(LogTemp, Warning, TEXT("[InfiniteRP] ignored invalid state_patch: %s"), *StatePatchError);
			OutBeat.StatePatchJson = TEXT("{}");
		}
	}
	const TSharedPtr<FJsonObject>* Memory = nullptr;
	if (Root->TryGetObjectField(TEXT("memory"), Memory)) OutBeat.MemoryJson = JsonString(*Memory);

	const TArray<TSharedPtr<FJsonValue>>* ChoiceArray = nullptr;
	if (!Root->TryGetArrayField(TEXT("choices"), ChoiceArray) || !ChoiceArray)
	{
		UE_LOG(LogTemp, Warning, TEXT("[InfiniteRP] choices missing; supplying safe continue choices"));
	}
	const int32 SourceChoiceCount = ChoiceArray ? ChoiceArray->Num() : 0;
	TMap<FString, FString> CardNameToId = PendingContext.CardNameToId;
	TMap<FString, FString> RelicNameToId = PendingContext.RelicNameToId;
	TMap<FString, FString> EnemyNameToId;
	{
		FString LoadError;
		TArray<FCardData> KnownCards;
		TArray<FRelicData> KnownRelics;
		TArray<FEnemyData> KnownEnemies;
		UGameDataLibrary::LoadCards(KnownCards, LoadError);
		UGameDataLibrary::LoadRelics(KnownRelics, LoadError);
		UGameDataLibrary::LoadEnemies(KnownEnemies, LoadError);
		for (const FCardData& Card : KnownCards) CardNameToId.FindOrAdd(Card.Name, Card.Id);
		for (const FRelicData& Relic : KnownRelics) RelicNameToId.FindOrAdd(Relic.Name, Relic.Id);
		for (const FEnemyData& Enemy : KnownEnemies) EnemyNameToId.FindOrAdd(Enemy.Name, Enemy.Id);
	}
	for (int32 ChoiceSlot = 0; ChoiceSlot < 3; ++ChoiceSlot)
	{
		const TSharedPtr<FJsonObject> ChoiceObject = (ChoiceArray && ChoiceSlot < SourceChoiceCount
			&& (*ChoiceArray)[ChoiceSlot].IsValid()) ? (*ChoiceArray)[ChoiceSlot]->AsObject() : nullptr;
		FInfiniteNarrativeChoice Choice;
		FString CompileStatus = GetString(ChoiceObject, TEXT("compile_status"), TEXT("compiled"));
		CompileStatus.TrimStartAndEndInline();
		CompileStatus.ToLowerInline();
		const FString NoEffectReason = GetString(ChoiceObject, TEXT("no_effect_reason")).TrimStartAndEnd().Left(800);
		Choice.Text = GetString(ChoiceObject, TEXT("text"));
		Choice.ResultSummary = GetString(ChoiceObject, TEXT("result_summary"));
		Choice.ConsequenceIntent = GetString(ChoiceObject, TEXT("consequence_intent"));
		Choice.GmJudgement = GetString(ChoiceObject, TEXT("gm_judgement")).Left(1000);
		Choice.SettlementKey = GetString(ChoiceObject, TEXT("settlement_key")).Left(192);
		const TSharedPtr<FJsonObject>* ResolvedImpact = nullptr;
		if (ChoiceObject.IsValid() && ChoiceObject->TryGetObjectField(TEXT("resolved_impact"), ResolvedImpact)
			&& ResolvedImpact && ResolvedImpact->IsValid())
		{
			Choice.ResolvedImpactKind = GetString(*ResolvedImpact, TEXT("kind"));
			Choice.ResolvedImpactKind.TrimStartAndEndInline();
			Choice.ResolvedImpactKind.ToLowerInline();
			Choice.ResolvedImpactSubject = GetString(*ResolvedImpact, TEXT("subject"), TEXT("player"));
			Choice.ResolvedImpactObject = GetString(*ResolvedImpact, TEXT("object"));
			(*ResolvedImpact)->TryGetBoolField(TEXT("completed"), Choice.bResolvedImpactCompleted);
			(*ResolvedImpact)->TryGetBoolField(TEXT("persistent"), Choice.bResolvedImpactPersistent);
		}
		const TSharedPtr<FJsonObject>* ChoiceStatePatch = nullptr;
		if (ChoiceObject.IsValid() && ChoiceObject->TryGetObjectField(TEXT("state_patch"), ChoiceStatePatch)
			&& ChoiceStatePatch && ChoiceStatePatch->IsValid())
		{
			Choice.StatePatchJson = JsonString(*ChoiceStatePatch);
			FString ChoicePatchError;
			if (!ValidateStatePatch(Choice.StatePatchJson, ChoicePatchError))
			{
				UE_LOG(LogTemp, Warning, TEXT("[InfiniteRP] ignored invalid branch state_patch: %s"), *ChoicePatchError);
				Choice.StatePatchJson = TEXT("{}");
			}
		}

		const TArray<TSharedPtr<FJsonValue>>* VariableUpdates = nullptr;
		bool bHasVariableUpdates = ChoiceObject.IsValid()
			&& ChoiceObject->TryGetArrayField(TEXT("variable_updates"), VariableUpdates);
		if (!bHasVariableUpdates && ChoiceObject.IsValid())
			bHasVariableUpdates = ChoiceObject->TryGetArrayField(TEXT("engine_updates"), VariableUpdates);
		if (!bHasVariableUpdates && ChoiceObject.IsValid())
			bHasVariableUpdates = ChoiceObject->TryGetArrayField(TEXT("state_updates"), VariableUpdates);
		if (bHasVariableUpdates && VariableUpdates)
		{
			static const TSet<FString> AllowedDomains = {
				TEXT("body"), TEXT("item"), TEXT("skill"), TEXT("relationship"),
				TEXT("environment"), TEXT("faction")
			};
			for (const TSharedPtr<FJsonValue>& UpdateValue : *VariableUpdates)
			{
				const TSharedPtr<FJsonObject> UpdateObject = UpdateValue.IsValid() ? UpdateValue->AsObject() : nullptr;
				if (!UpdateObject.IsValid() || Choice.VariableUpdates.Num() >= 8) continue;
				FInfiniteVariableUpdate Update;
				Update.Domain = GetString(UpdateObject, TEXT("domain")).TrimStartAndEnd().ToLower();
				Update.Target = GetString(UpdateObject, TEXT("target")).TrimStartAndEnd().Left(80);
				Update.Field = GetString(UpdateObject, TEXT("field")).TrimStartAndEnd().ToLower();
				Update.Op = GetString(UpdateObject, TEXT("op")).TrimStartAndEnd().ToLower();
				Update.Value = GetString(UpdateObject, TEXT("value")).TrimStartAndEnd().Left(160);
				Update.Amount = FMath::Clamp(GetInt(UpdateObject, TEXT("amount")), -100, 100);
				Update.Duration = FMath::Clamp(GetInt(UpdateObject, TEXT("duration")), 0, 12);
				Update.Causality = GetString(UpdateObject, TEXT("causality"), TEXT("direct")).ToLower();
				Update.Valence = GetString(UpdateObject, TEXT("valence"), TEXT("mixed")).ToLower();
				Update.Magnitude = GetString(UpdateObject, TEXT("magnitude"), TEXT("minor")).ToLower();
				if (Update.Domain == TEXT("character") || Update.Domain == TEXT("companion"))
					Update.Domain = TEXT("relationship");
				else if (Update.Domain == TEXT("enemy_faction") || Update.Domain == TEXT("hostile_faction"))
					Update.Domain = TEXT("faction");
				else if (Update.Domain == TEXT("location")) Update.Domain = TEXT("environment");
				if (Update.Field == TEXT("favor") || Update.Field == TEXT("affection")
					|| Update.Field == TEXT("trust")) Update.Field = TEXT("affinity");
				else if (Update.Field == TEXT("alertness") || Update.Field == TEXT("warning"))
					Update.Field = TEXT("alert");
				else if (Update.Field == TEXT("current_location")) Update.Field = TEXT("location");
				else if (Update.Domain == TEXT("body") && Update.Field == TEXT("status"))
					Update.Field = TEXT("condition");
				if (Update.Op == TEXT("increase") || Update.Op == TEXT("change")) Update.Op = TEXT("add");
				else if (Update.Op == TEXT("decrease"))
				{
					Update.Op = TEXT("add");
					Update.Amount = -FMath::Abs(Update.Amount == 0 ? 1 : Update.Amount);
				}
				else if (Update.Op == TEXT("update")) Update.Op = TEXT("set");
				else if (Update.Op == TEXT("discover") || Update.Op == TEXT("reveal")) Update.Op = TEXT("add");
				const bool bAllowedShape =
					(Update.Domain == TEXT("body") && Update.Field == TEXT("condition")
						&& (Update.Op == TEXT("add") || Update.Op == TEXT("set") || Update.Op == TEXT("remove")))
					|| (Update.Domain == TEXT("item") && Update.Field == TEXT("ownership")
						&& (Update.Op == TEXT("gain") || Update.Op == TEXT("lose") || Update.Op == TEXT("add") || Update.Op == TEXT("remove")))
					|| (Update.Domain == TEXT("skill") && Update.Field == TEXT("knowledge")
						&& (Update.Op == TEXT("learn") || Update.Op == TEXT("upgrade") || Update.Op == TEXT("forget")))
					|| (Update.Domain == TEXT("relationship") && Update.Field == TEXT("affinity")
						&& (Update.Op == TEXT("add") || Update.Op == TEXT("set")))
					|| (Update.Domain == TEXT("relationship") && Update.Field == TEXT("bond") && Update.Op == TEXT("set"))
					|| (Update.Domain == TEXT("environment") && Update.Field == TEXT("location") && Update.Op == TEXT("set"))
					|| (Update.Domain == TEXT("environment") && Update.Field == TEXT("trait")
						&& (Update.Op == TEXT("add") || Update.Op == TEXT("remove")))
					|| (Update.Domain == TEXT("environment") && Update.Field == TEXT("combat_edge")
						&& (Update.Op == TEXT("add") || Update.Op == TEXT("set")) && Update.Amount != 0)
					|| (Update.Domain == TEXT("faction") && Update.Field == TEXT("alert")
						&& (Update.Op == TEXT("add") || Update.Op == TEXT("set")))
					|| (Update.Domain == TEXT("faction") && Update.Field == TEXT("trait")
						&& (Update.Op == TEXT("add") || Update.Op == TEXT("remove")));
				if (!AllowedDomains.Contains(Update.Domain) || !bAllowedShape) continue;
				if (Update.Causality != TEXT("direct") && Update.Causality != TEXT("incidental")) continue;
				if (Update.Valence != TEXT("positive") && Update.Valence != TEXT("negative")
					&& Update.Valence != TEXT("mixed") && Update.Valence != TEXT("neutral")) continue;
				if (Update.Magnitude != TEXT("minor") && Update.Magnitude != TEXT("moderate")
					&& Update.Magnitude != TEXT("major")) continue;
				Choice.VariableUpdates.Add(Update);
			}
		}
		const TArray<TSharedPtr<FJsonValue>>* ContentJobs = nullptr;
		if (ChoiceObject.IsValid() && ChoiceObject->TryGetArrayField(TEXT("content_jobs"), ContentJobs)
			&& ContentJobs)
		{
			for (const TSharedPtr<FJsonValue>& JobValue : *ContentJobs)
			{
				if (Choice.CardForgeJobs.Num() >= 2) break;
				const TSharedPtr<FJsonObject> JobObject = JobValue.IsValid() ? JobValue->AsObject() : nullptr;
				if (!JobObject.IsValid()) continue;
				FString Kind = GetString(JobObject, TEXT("kind"), TEXT("card")).TrimStartAndEnd().ToLower();
				if (Kind != TEXT("card")) continue;
				FInfiniteCardForgeJob Job;
				Job.SourceFact = GetString(JobObject, TEXT("source_fact")).TrimStartAndEnd().Left(600);
				Job.Concept = GetString(JobObject, TEXT("concept")).TrimStartAndEnd().Left(160);
				Job.MechanicIntent = GetString(JobObject, TEXT("mechanic_intent")).TrimStartAndEnd().Left(600);
				Job.Acquisition = GetString(JobObject, TEXT("acquisition"), TEXT("gain")).TrimStartAndEnd().ToLower();
				if (Job.Acquisition != TEXT("gain")) continue;
				if (Job.SourceFact.IsEmpty()) Job.SourceFact = Choice.ResultSummary;
				if (Job.Concept.IsEmpty()) Job.Concept = Choice.ResolvedImpactObject;
				if (Job.Concept.IsEmpty()) Job.Concept = TEXT("本轮所得之物留下的道痕");
				Choice.CardForgeJobs.Add(MoveTemp(Job));
			}
		}
		// Schema 4 carries only a concept seed. It is deliberately not a card job or card
		// definition inside RP; the selected route turns it into a forge request locally.
		FString CardConcept = GetString(ChoiceObject, TEXT("card_concept")).TrimStartAndEnd();
		// The RP director sometimes keeps writing mechanics after a concept name despite
		// the contract. Keep only the title-like prefix; the independent forge must be the
		// first and only component that designs card rules.
		int32 ConceptCut = INDEX_NONE;
		for (const FString& Separator : {FString(TEXT("：")), FString(TEXT(":")), FString(TEXT("\n")), FString(TEXT("；"))})
		{
			const int32 Found = CardConcept.Find(Separator);
			if (Found != INDEX_NONE && (ConceptCut == INDEX_NONE || Found < ConceptCut)) ConceptCut = Found;
		}
		if (ConceptCut > 0) CardConcept = CardConcept.Left(ConceptCut);
		CardConcept = CardConcept.TrimQuotes().TrimStartAndEnd().Left(24);
		if (!CardConcept.IsEmpty())
		{
			FInfiniteCardForgeJob Job;
			Job.SourceFact = Choice.ResultSummary;
			Job.Concept = CardConcept;
			Job.MechanicIntent = TEXT("根据卡面概念的意象与取得方式独立设计玩法");
			Choice.CardForgeJobs.Add(Job);
		}
		Choice.Next = GetString(ChoiceObject, TEXT("next"));
		Choice.Next.TrimStartAndEndInline();
		Choice.Next.ToLowerInline();

		const TArray<TSharedPtr<FJsonValue>>* Requirements = nullptr;
		if (ChoiceObject.IsValid() && ChoiceObject->TryGetArrayField(TEXT("requirements"), Requirements))
		{
			for (const TSharedPtr<FJsonValue>& RequirementValue : *Requirements)
			{
				const TSharedPtr<FJsonObject> RequirementObject = RequirementValue.IsValid()
					? RequirementValue->AsObject() : nullptr;
				if (!RequirementObject.IsValid() || Choice.Requirements.Num() >= 4) continue;
				FInfiniteChoiceRequirement Requirement;
				Requirement.Type = GetString(RequirementObject, TEXT("type"));
				Requirement.Type.ToLowerInline();
				Requirement.Value = FMath::Clamp(GetInt(RequirementObject, TEXT("value")), 0, 99999);
				if (Requirement.Type == TEXT("gold_at_least") || Requirement.Type == TEXT("hp_above")
					|| Requirement.Type == TEXT("deck_at_least"))
					Choice.Requirements.Add(Requirement);
			}
		}

		const TArray<TSharedPtr<FJsonValue>>* Operations = nullptr;
		if (ChoiceObject.IsValid() && ChoiceObject->TryGetArrayField(TEXT("operations"), Operations))
		{
			for (const TSharedPtr<FJsonValue>& OperationValue : *Operations)
			{
				const TSharedPtr<FJsonObject> OperationObject = OperationValue.IsValid()
					? OperationValue->AsObject() : nullptr;
				if (!OperationObject.IsValid() || Choice.Operations.Num() >= 8) continue;
				FInfiniteGameOperation Operation;
				Operation.Op = GetString(OperationObject, TEXT("op"));
				if (Operation.Op.IsEmpty()) Operation.Op = GetString(OperationObject, TEXT("tool"));
				Operation.Op.ToLowerInline();
				Operation.Name = GetString(OperationObject, TEXT("name"));
				Operation.Rarity = GetString(OperationObject, TEXT("rarity"));
				Operation.Rarity.ToLowerInline();
				Operation.Value = FMath::Clamp(GetInt(OperationObject, TEXT("value")), -5000, 5000);
				Operation.Count = FMath::Clamp(GetInt(OperationObject, TEXT("count"), 1), 1, 3);
				Operation.PriceMultiplier = FMath::Clamp(GetFloat(OperationObject, TEXT("price_multiplier")), 0.5f, 3.f);
				OperationObject->TryGetBoolField(TEXT("allow_starter"), Operation.bAllowStarter);
				OperationObject->TryGetBoolField(TEXT("allow_curse"), Operation.bAllowCurse);
				const TSharedPtr<FJsonObject>* Arguments = nullptr;
				bool bHasArguments = OperationObject->TryGetObjectField(TEXT("arguments"), Arguments);
				if (!bHasArguments) bHasArguments = OperationObject->TryGetObjectField(TEXT("args"), Arguments);
				if (bHasArguments
					&& Arguments && Arguments->IsValid())
				{
					if (Operation.Name.IsEmpty()) Operation.Name = GetString(*Arguments, TEXT("name"));
					if (Operation.Rarity.IsEmpty()) Operation.Rarity = GetString(*Arguments, TEXT("rarity"));
					Operation.Value = FMath::Clamp(GetInt(*Arguments, TEXT("value"), Operation.Value), -5000, 5000);
					Operation.Count = FMath::Clamp(GetInt(*Arguments, TEXT("count"), Operation.Count), 1, 3);
					Operation.PriceMultiplier = FMath::Clamp(
						GetFloat(*Arguments, TEXT("price_multiplier"), Operation.PriceMultiplier), 0.5f, 3.f);
					(*Arguments)->TryGetBoolField(TEXT("allow_starter"), Operation.bAllowStarter);
					(*Arguments)->TryGetBoolField(TEXT("allow_curse"), Operation.bAllowCurse);
				}

				if (Operation.Op == TEXT("hp"))
				{
					Choice.Reward.HPChange = FMath::Clamp(Choice.Reward.HPChange + Operation.Value, -50, 50);
					Choice.Operations.Add(Operation);
				}
				else if (Operation.Op == TEXT("gold"))
				{
					Choice.Reward.GoldChange = FMath::Clamp(Choice.Reward.GoldChange + Operation.Value, -200, 500);
					Choice.Operations.Add(Operation);
				}
				else if (Operation.Op == TEXT("grant_card") || Operation.Op == TEXT("remove_card"))
				{
					if (const FString* Id = CardNameToId.Find(Operation.Name))
					{
						if (Operation.Op == TEXT("grant_card"))
						{
							FInfiniteRewardCard Card;
							Card.CardId = *Id;
							Choice.Reward.Cards.Add(Card);
						}
						else Choice.Reward.RemovedCardIds.Add(*Id);
						Choice.Operations.Add(Operation);
					}
				}
				else if (Operation.Op == TEXT("grant_relic") || Operation.Op == TEXT("remove_relic"))
				{
					if (const FString* Id = RelicNameToId.Find(Operation.Name))
					{
						if (Operation.Op == TEXT("grant_relic")) Choice.Reward.RelicIds.Add(*Id);
						else Choice.Reward.RemovedRelicIds.Add(*Id);
						Choice.Operations.Add(Operation);
					}
				}
				else if (Operation.Op == TEXT("choose_remove_card")
					|| Operation.Op == TEXT("choose_upgrade_card") || Operation.Op == TEXT("open_shop")
					|| Operation.Op == TEXT("open_reward") || Operation.Op == TEXT("open_rest"))
				{
					Choice.Operations.Add(Operation);
				}
				else
				{
					const FString MisplacedAction = GetString(OperationObject, TEXT("action"));
					UE_LOG(LogTemp, Warning, TEXT("[InfiniteRP] dropped unsupported narrative operation choice=%c op=%s action=%s"),
						static_cast<TCHAR>(TEXT('A') + ChoiceSlot),
						Operation.Op.IsEmpty() ? TEXT("missing") : *Operation.Op,
						MisplacedAction.IsEmpty() ? TEXT("-") : *MisplacedAction);
				}
			}
		}

		const TSharedPtr<FJsonObject>* Rewards = nullptr;
		if (ChoiceObject.IsValid() && ChoiceObject->TryGetObjectField(TEXT("rewards"), Rewards)
			&& Rewards && Rewards->IsValid())
		{
			Choice.Reward.GoldChange = FMath::Clamp(
				Choice.Reward.GoldChange + GetInt(*Rewards, TEXT("gold")), -200, 500);
			Choice.Reward.HPChange = FMath::Clamp(
				Choice.Reward.HPChange + GetInt(*Rewards, TEXT("hp")), -50, 50);
			const TArray<TSharedPtr<FJsonValue>>* Cards = nullptr;
			if ((*Rewards)->TryGetArrayField(TEXT("cards"), Cards))
			{
				for (const TSharedPtr<FJsonValue>& CardValue : *Cards)
				{
					FInfiniteRewardCard Card;
					if (CardValue->Type == EJson::String) Card.CardId = CardValue->AsString();
					else if (const TSharedPtr<FJsonObject> CardObject = CardValue->AsObject())
					{
						Card.CardId = GetString(CardObject, TEXT("id"));
						CardObject->TryGetBoolField(TEXT("upgraded"), Card.bUpgraded);
					}
					if (!Card.CardId.IsEmpty() && Choice.Reward.Cards.Num() < 3) Choice.Reward.Cards.Add(Card);
				}
			}
			const TArray<TSharedPtr<FJsonValue>>* Relics = nullptr;
			if ((*Rewards)->TryGetArrayField(TEXT("relics"), Relics))
			{
				for (const TSharedPtr<FJsonValue>& RelicValue : *Relics)
				{
					FString Id;
					if (RelicValue->TryGetString(Id) && !Id.IsEmpty() && Choice.Reward.RelicIds.Num() < 2)
						Choice.Reward.RelicIds.Add(Id);
				}
			}
			const TArray<TSharedPtr<FJsonValue>>* RemovedCards = nullptr;
			if ((*Rewards)->TryGetArrayField(TEXT("removed_cards"), RemovedCards))
			{
				for (const TSharedPtr<FJsonValue>& CardValue : *RemovedCards)
				{
					FString Id;
					if (CardValue->TryGetString(Id) && !Id.IsEmpty() && Choice.Reward.RemovedCardIds.Num() < 3)
						Choice.Reward.RemovedCardIds.Add(Id);
				}
			}
			const TArray<TSharedPtr<FJsonValue>>* RemovedRelics = nullptr;
			if ((*Rewards)->TryGetArrayField(TEXT("removed_relics"), RemovedRelics))
			{
				for (const TSharedPtr<FJsonValue>& RelicValue : *RemovedRelics)
				{
					FString Id;
					if (RelicValue->TryGetString(Id) && !Id.IsEmpty() && Choice.Reward.RemovedRelicIds.Num() < 2)
						Choice.Reward.RemovedRelicIds.Add(Id);
				}
			}

				// A branch may actually hand over several persistent objects. Register every
				// valid one; malformed authored items are dropped locally instead of rejecting
				// the whole narrative beat.
				const int32 ChoiceIndex = OutBeat.Choices.Num();
			const TArray<TSharedPtr<FJsonValue>>* CreatedCards = nullptr;
			const TArray<TSharedPtr<FJsonValue>>* CreatedRelics = nullptr;
			const bool bHasCreatedCards = (*Rewards)->TryGetArrayField(TEXT("created_cards"), CreatedCards) && CreatedCards->Num() > 0;
			const bool bHasCreatedRelics = (*Rewards)->TryGetArrayField(TEXT("created_relics"), CreatedRelics) && CreatedRelics->Num() > 0;
				if (bHasCreatedCards)
				{
					for (int32 CreatedIndex = 0; CreatedIndex < CreatedCards->Num() && Choice.Reward.CreatedCards.Num() < 3; ++CreatedIndex)
					{
					const TSharedPtr<FJsonValue>& CardValue = (*CreatedCards)[CreatedIndex];
					const TSharedPtr<FJsonObject> CardObject = CardValue.IsValid() ? CardValue->AsObject() : nullptr;
					FCardData Card;
						FString CardError;
					if (!BuildAuthoredCard(CardObject, PendingContext.Cycle, ChoiceIndex, Card, CardError))
					{
						UE_LOG(LogTemp, Warning, TEXT("[InfiniteRP] dropped invalid authored card choice=%d index=%d: %s"),
							ChoiceIndex + 1, CreatedIndex + 1, *CardError);
						if (RequestPhase == ERequestPhase::StateCompilation)
						{
							OutError = FString::Printf(TEXT("选项%c原创卡%d无效：%s"),
								static_cast<TCHAR>(TEXT('A') + ChoiceSlot), CreatedIndex + 1, *CardError);
							return false;
						}
						continue;
					}
					Choice.Reward.CreatedCards.Add(Card);
					FInfiniteRewardCard RewardCard;
					RewardCard.CardId = Card.Id;
					Choice.Reward.Cards.Add(RewardCard);
				}
			}

				if (bHasCreatedRelics)
				{
					for (int32 CreatedIndex = 0; CreatedIndex < CreatedRelics->Num() && Choice.Reward.CreatedRelics.Num() < 3; ++CreatedIndex)
					{
						const TSharedPtr<FJsonValue>& RelicValue = (*CreatedRelics)[CreatedIndex];
						const TSharedPtr<FJsonObject> RelicObject = RelicValue.IsValid() ? RelicValue->AsObject() : nullptr;
						if (!RelicObject.IsValid())
						{
							UE_LOG(LogTemp, Warning, TEXT("[InfiniteRP] dropped non-object authored relic choice=%d index=%d"),
								ChoiceIndex + 1, CreatedIndex);
							continue;
						}
						FRelicData Relic;
						Relic.Name = GetString(RelicObject, TEXT("name")).Left(18);
						if (Relic.Name.IsEmpty())
						{
							UE_LOG(LogTemp, Warning, TEXT("[InfiniteRP] dropped authored relic without name choice=%d index=%d"),
								ChoiceIndex + 1, CreatedIndex);
							continue;
						}
						const FString ProposedRarity = GetString(RelicObject, TEXT("rarity"), TEXT("common"));
						Relic.Rarity = SafeRarity(ProposedRarity);
					Relic.Trigger = GetString(RelicObject, TEXT("trigger"));
					Relic.Trigger.ToLowerInline();
					static const TArray<FString> RelicTriggers = {
						TEXT("combat_start"), TEXT("turn_start"), TEXT("on_kill"), TEXT("on_victory"),
						TEXT("on_card_played"), TEXT("on_player_turn_end"), TEXT("on_damage_dealt"),
						TEXT("on_cards_discarded"), TEXT("on_reshuffle")
					};
						if (!RelicTriggers.Contains(Relic.Trigger))
						{
							const FString InvalidTrigger = Relic.Trigger;
							Relic.Trigger = TEXT("combat_start");
							UE_LOG(LogTemp, Warning, TEXT("[InfiniteRP] normalized authored relic trigger '%s' -> combat_start choice=%d index=%d"),
								*InvalidTrigger, ChoiceIndex + 1, CreatedIndex);
						}
					Relic.Counter = FMath::Clamp(GetInt(RelicObject, TEXT("counter"), 0), 0, 10);
					if (Relic.Trigger == TEXT("on_card_played") || Relic.Trigger == TEXT("on_damage_dealt"))
						Relic.Counter = FMath::Max(3, Relic.Counter);
					if (Relic.Trigger == TEXT("on_cards_discarded")) Relic.Condition = TEXT("per_discarded_card");
					const TSharedPtr<FJsonObject>* EffectObject = nullptr;
					if (RelicObject->TryGetObjectField(TEXT("effect"), EffectObject))
						{
							FString EffectError;
							if (!ValidateAuthoredEffect(*EffectObject, Relic.Rarity, true, EffectError))
							{
								UE_LOG(LogTemp, Warning, TEXT("[InfiniteRP] dropped authored relic '%s' with invalid effect: %s"),
									*Relic.Name, *EffectError);
								continue;
							}
						ParseSafeEffect(*EffectObject, Relic.Rarity, true, Relic.Effect);
					}
						else
						{
							UE_LOG(LogTemp, Warning, TEXT("[InfiniteRP] dropped authored relic '%s' without effect"), *Relic.Name);
							continue;
						}
					// Damage caused by an on_damage_dealt relic would recursively trigger itself.
					if (Relic.Trigger == TEXT("on_damage_dealt") && Relic.Effect.Action.Contains(TEXT("damage")))
						Relic.Effect = FCardEffect();
					const FString ProposedArt = GetString(RelicObject, TEXT("art"));
					Relic.ArtPath = ResolveRuntimeArt(ProposedArt, TEXT("relic"));
					if (!Relic.Name.IsEmpty() && !Relic.Effect.Action.IsEmpty())
					{
						Relic.Description = SafeRelicDescription(Relic);
						Relic.Id = MakeRuntimeContentId(TEXT("llm_relic"), Relic.Name, PendingContext.Cycle, ChoiceIndex);
						Choice.Reward.CreatedRelics.Add(Relic);
						Choice.Reward.RelicIds.AddUnique(Relic.Id);
					}
				}
			}
		}

		const TSharedPtr<FJsonObject>* Encounter = nullptr;
		if (ChoiceObject.IsValid() && ChoiceObject->TryGetObjectField(TEXT("encounter"), Encounter)
			&& Encounter && Encounter->IsValid())
		{
			const FString TemplateName = GetString(*Encounter, TEXT("template_name"));
			Choice.Enemy.TemplateId = GetString(*Encounter, TEXT("template_id"));
			Choice.Enemy.FactionId = GetString(*Encounter, TEXT("faction_id")).TrimStartAndEnd().Left(80);
			if (Choice.Enemy.TemplateId.IsEmpty() && !TemplateName.IsEmpty())
			{
				if (const FString* ResolvedId = EnemyNameToId.Find(TemplateName))
					Choice.Enemy.TemplateId = *ResolvedId;
			}
			Choice.Enemy.Name = GetString(*Encounter, TEXT("name"));
			Choice.Enemy.Story = GetString(*Encounter, TEXT("story"));
			Choice.Enemy.Tier = GetString(*Encounter, TEXT("tier"), TEXT("normal"));
			Choice.Enemy.HPScale = FMath::Clamp(GetFloat(*Encounter, TEXT("hp_scale")), 0.7f, 2.5f);
			Choice.Enemy.IntentScale = FMath::Clamp(GetFloat(*Encounter, TEXT("intent_scale")), 0.75f, 1.8f);
			Choice.Enemy.AbilityDesc = GetString(*Encounter, TEXT("ability_desc"));
			const TArray<TSharedPtr<FJsonValue>>* Abilities = nullptr;
			if ((*Encounter)->TryGetArrayField(TEXT("abilities"), Abilities))
			{
				for (const TSharedPtr<FJsonValue>& Ability : *Abilities)
				{
					FString Token;
					if (Ability->TryGetString(Token) && Choice.Enemy.Abilities.Num() < 3) Choice.Enemy.Abilities.Add(Token);
				}
			}
		}
		ApplyChoiceRoutePlan(Choice, ChoiceSlot);

		if (Choice.Next.IsEmpty())
		{
			// Backward compatibility for older world-book responses.
			Choice.Next = Choice.Enemy.TemplateId.IsEmpty() ? TEXT("continue_rp") : TEXT("combat");
		}
		static const TSet<FString> AllowedDestinations = {
			TEXT("combat"), TEXT("continue_rp"), TEXT("card_forge"), TEXT("shop"),
			TEXT("relic_reward"), TEXT("reward"), TEXT("rest"), TEXT("upgrade"), TEXT("remove")
		};
		if (!AllowedDestinations.Contains(Choice.Next))
		{
			UE_LOG(LogTemp, Warning, TEXT("[InfiniteRP] unsupported choice next='%s'; treating it as continue_rp"), *Choice.Next);
			Choice.Next = TEXT("continue_rp");
		}
		if (Choice.Text.IsEmpty())
		{
			Choice.Text = FString::Printf(TEXT("继续观察眼前局势（选项%d）"), ChoiceSlot + 1);
		}
		if (Choice.ResultSummary.IsEmpty())
		{
			Choice.ResultSummary = TEXT("你暂时按兵不动，等待局势继续变化。");
		}
		if (Choice.Next == TEXT("combat") && Choice.Enemy.TemplateId.IsEmpty())
		{
			// Preserve a valid combat route even when the model misspells the template.
			// The generic local skeleton is safer than discarding every other branch.
			Choice.Enemy.TemplateId = TEXT("mountain_imp");
			if (Choice.Enemy.Name.IsEmpty()) Choice.Enemy.Name = TEXT("剧情敌人");
			if (Choice.Enemy.Story.IsEmpty()) Choice.Enemy.Story = Choice.ResultSummary;
			UE_LOG(LogTemp, Warning, TEXT("[InfiniteRP] combat choice %d normalized to local enemy skeleton"), ChoiceSlot + 1);
		}

		FString InventoryIntent = GetString(ChoiceObject, TEXT("inventory_intent"), TEXT("none"));
		InventoryIntent.TrimStartAndEndInline();
		InventoryIntent.ToLowerInline();
		FString RewardTiming = GetString(ChoiceObject, TEXT("reward_timing"), TEXT("immediate"));
		RewardTiming.TrimStartAndEndInline();
		RewardTiming.ToLowerInline();
		if (InventoryIntent != TEXT("none") && InventoryIntent != TEXT("gain_card")
			&& InventoryIntent != TEXT("gain_relic") && InventoryIntent != TEXT("temporary_story_item"))
		{
			UE_LOG(LogTemp, Warning, TEXT("[InfiniteRP] unsupported inventory_intent='%s'; ignoring it"), *InventoryIntent);
			InventoryIntent = TEXT("none");
		}
		if (Choice.Next == TEXT("combat"))
		{
			const bool bHasChoiceReward = Choice.Reward.Cards.Num() > 0 || Choice.Reward.RelicIds.Num() > 0
				|| Choice.Reward.CreatedCards.Num() > 0 || Choice.Reward.CreatedRelics.Num() > 0
				|| Choice.Reward.GoldChange != 0 || Choice.Reward.HPChange != 0;
			if (bHasChoiceReward)
			{
				if (RewardTiming != TEXT("immediate"))
				{
					UE_LOG(LogTemp, Warning, TEXT("[InfiniteRP] combat choice %d reward_timing='%s' normalized to immediate"),
						ChoiceSlot + 1, *RewardTiming);
				}
				Choice.bGrantRewardBeforeCombat = true;
			}
		}
		else
		{
			// A conversational choice must not silently create a queued battle.
			Choice.Enemy = FInfiniteEnemySpec();
		}
		if (RequestPhase == ERequestPhase::StateCompilation && bRequireEngineTransition)
		{
			const bool bExplicitItemGain = Choice.VariableUpdates.ContainsByPredicate(
				[](const FInfiniteVariableUpdate& Update)
				{
					return Update.Domain == TEXT("item") && Update.Field == TEXT("ownership")
						&& (Update.Op == TEXT("gain") || Update.Op == TEXT("add"));
				});
			const bool bExplicitSkillGain = Choice.VariableUpdates.ContainsByPredicate(
				[](const FInfiniteVariableUpdate& Update)
				{
					return Update.Domain == TEXT("skill") && Update.Field == TEXT("knowledge")
						&& (Update.Op == TEXT("learn") || Update.Op == TEXT("upgrade"));
				});
			if (Choice.ResolvedImpactKind == TEXT("acquire_item") && Choice.bResolvedImpactCompleted
				&& Choice.bResolvedImpactPersistent && !bExplicitItemGain)
			{
				OutError = FString::Printf(TEXT("选项%c声称玩家已经持有物品，却没有同时写item/ownership gain；补齐同名物品状态"),
					static_cast<TCHAR>(TEXT('A') + ChoiceSlot));
				return false;
			}
			if (Choice.ResolvedImpactKind == TEXT("learn_ability") && Choice.bResolvedImpactCompleted
				&& Choice.bResolvedImpactPersistent && !bExplicitSkillGain)
			{
				OutError = FString::Printf(TEXT("选项%c声称玩家已学会能力，却没有同时写skill/knowledge learn；补齐同名技能状态"),
					static_cast<TCHAR>(TEXT('A') + ChoiceSlot));
				return false;
			}
		}
		// Ownership and playable representation are one transaction. Keep this repair
		// compatible with older saves/providers too; the direct 3.2 path is not the only
		// place from which a persisted item fact can be replayed.
		const bool bHasDirectCardGain = Choice.Reward.Cards.Num() > 0
			|| Choice.Reward.CreatedCards.Num() > 0
			|| Choice.CardForgeJobs.Num() > 0
			|| Choice.Operations.ContainsByPredicate([](const FInfiniteGameOperation& Operation)
			{
				return Operation.Op == TEXT("grant_card");
			});
		const bool bHasItemGain = Choice.VariableUpdates.ContainsByPredicate(
			[](const FInfiniteVariableUpdate& Update)
			{
				return Update.Domain == TEXT("item") && Update.Field == TEXT("ownership")
					&& (Update.Op == TEXT("gain") || Update.Op == TEXT("add"));
			});
		const bool bClaimsItemAcquisition = bHasItemGain
			|| (Choice.ResolvedImpactKind == TEXT("acquire_item")
				&& Choice.bResolvedImpactCompleted && Choice.bResolvedImpactPersistent);
		if (RequestPhase == ERequestPhase::StateCompilation && bClaimsItemAcquisition && !bHasDirectCardGain)
		{
			FInfiniteCardForgeJob Job;
			Job.SourceFact = Choice.ResultSummary + TEXT(" ") + Choice.ConsequenceIntent;
			Job.Concept = Choice.ResolvedImpactObject.IsEmpty()
				? TEXT("本轮获得的实体物品") : Choice.ResolvedImpactObject;
			Job.MechanicIntent = TEXT("从物品材质、用途、取得方式与当前情境中提炼至少两个特征");
			Choice.CardForgeJobs.Add(Job);
			UE_LOG(LogTemp, Warning, TEXT("[InfiniteRP] choice %c auto-routed acquired item to independent card forge"),
				static_cast<TCHAR>(TEXT('A') + ChoiceSlot));
		}
		if (RequestPhase == ERequestPhase::StateCompilation && bRequireEngineTransition)
		{
			const bool bHasReward = Choice.Reward.HPChange != 0 || Choice.Reward.GoldChange != 0
				|| Choice.Reward.Cards.Num() > 0 || Choice.Reward.RelicIds.Num() > 0
				|| Choice.Reward.RemovedCardIds.Num() > 0 || Choice.Reward.RemovedRelicIds.Num() > 0
				|| Choice.Reward.CreatedCards.Num() > 0 || Choice.Reward.CreatedRelics.Num() > 0;
			const bool bHasPositiveReward = Choice.Reward.HPChange > 0 || Choice.Reward.GoldChange > 0
				|| Choice.Reward.Cards.Num() > 0 || Choice.Reward.RelicIds.Num() > 0
				|| Choice.Reward.CreatedCards.Num() > 0 || Choice.Reward.CreatedRelics.Num() > 0;
			const bool bHasExecutableTransition = Choice.VariableUpdates.Num() > 0
				|| Choice.Operations.Num() > 0 || Choice.CardForgeJobs.Num() > 0
				|| bHasReward || Choice.Next == TEXT("combat");
			if (CompileStatus == TEXT("no_executable_effect") && bHasExecutableTransition)
			{
				OutError = FString::Printf(TEXT("选项%c同时声明no_executable_effect并输出了引擎效果；删除臆造效果，或仅在锁定结果直接支持时改为compiled"),
					static_cast<TCHAR>(TEXT('A') + ChoiceSlot));
				return false;
			}
			if (ChoiceSlot == 1)
			{
				const bool bHasGrowthVariable = Choice.VariableUpdates.ContainsByPredicate(
					[](const FInfiniteVariableUpdate& Update)
					{
						if (Update.Domain == TEXT("skill"))
							return Update.Op == TEXT("learn") || Update.Op == TEXT("upgrade")
							|| Update.Op == TEXT("gain") || Update.Op == TEXT("add");
						if (Update.Domain == TEXT("body"))
							return Update.Valence == TEXT("positive") || Update.Op == TEXT("upgrade")
							|| Update.Op == TEXT("learn");
						return false;
					});
					const bool bHasGrowthOperation = Choice.CardForgeJobs.Num() > 0
						|| Choice.Operations.ContainsByPredicate(
					[](const FInfiniteGameOperation& Operation)
					{
						return Operation.Op == TEXT("grant_card") || Operation.Op == TEXT("grant_relic")
							|| Operation.Op == TEXT("choose_upgrade_card") || Operation.Op == TEXT("choose_remove_card")
							|| Operation.Op == TEXT("remove_card") || Operation.Op == TEXT("open_shop")
							|| Operation.Op == TEXT("open_reward") || Operation.Op == TEXT("open_rest")
							|| ((Operation.Op == TEXT("hp") || Operation.Op == TEXT("gold")) && Operation.Value > 0);
					});
					if (!bHasPositiveReward && !bHasGrowthOperation && !bHasGrowthVariable)
					{
						OutError = TEXT("选项B没有完成成长或打开成长功能；重写B及其直接引擎结果");
						return false;
				}
			}
			const FString LockedResult = PendingDraftBeat.Choices.IsValidIndex(ChoiceSlot)
				? PendingDraftBeat.Choices[ChoiceSlot].ResultSummary + TEXT(" ")
					+ PendingDraftBeat.Choices[ChoiceSlot].ConsequenceIntent
				: FString();
			auto ContainsAnyLockedTerm = [&LockedResult](const TArray<FString>& Terms)
			{
				return Terms.ContainsByPredicate([&LockedResult](const FString& Term)
					{ return LockedResult.Contains(Term, ESearchCase::IgnoreCase); });
			};
			// Accept a descriptive modifier around an otherwise explicit noun (for example
			// "仿契黄纸卷" vs "黄纸卷"). This is only evidence tolerance: the model still
			// decides whether the fact is an item/skill change and which operation applies.
			auto LockedTextNames = [&LockedResult](const FString& Candidate)
			{
				const FString Trimmed = Candidate.TrimStartAndEnd();
				if (Trimmed.IsEmpty()) return false;
				if (LockedResult.Contains(Trimmed, ESearchCase::IgnoreCase)) return true;
				if (Trimmed.Len() < 3) return false;
			for (int32 Start = 0; Start <= Trimmed.Len() - 3; ++Start)
				{
					if (LockedResult.Contains(Trimmed.Mid(Start, 3), ESearchCase::IgnoreCase))
						return true;
			}
			static const TArray<FString> ShortArtifactNouns = {
				TEXT("腰牌"), TEXT("地图"), TEXT("铜牌"), TEXT("木牌"), TEXT("令牌"),
				TEXT("纸卷"), TEXT("卷轴"), TEXT("玉简"), TEXT("信件"), TEXT("丹药"),
				TEXT("血痕"), TEXT("灼痕"), TEXT("伤口"), TEXT("划伤"), TEXT("中毒"),
				TEXT("眩晕"), TEXT("昏沉")
			};
			if (ShortArtifactNouns.ContainsByPredicate([&Trimmed, &LockedResult](const FString& Noun)
				{ return Trimmed.Contains(Noun) && LockedResult.Contains(Noun); }))
				return true;
			return false;
		};
			for (const FInfiniteVariableUpdate& Update : Choice.VariableUpdates)
			{
				const bool bTargetOrValueNamed = LockedTextNames(Update.Target)
					|| LockedTextNames(Update.Value);
				bool bExplicitlySupported = true;
				if (Update.Domain == TEXT("item") || Update.Domain == TEXT("skill") || Update.Domain == TEXT("body"))
					bExplicitlySupported = bTargetOrValueNamed;
				else if (Update.Domain == TEXT("relationship"))
					bExplicitlySupported = ContainsAnyLockedTerm({TEXT("好感"), TEXT("信任"), TEXT("关系"),
						TEXT("亲近"), TEXT("疏离"), TEXT("决裂"), TEXT("态度"), TEXT("道侣"), TEXT("羁绊")});
				else if (Update.Domain == TEXT("faction") && Update.Field == TEXT("alert"))
					bExplicitlySupported = ContainsAnyLockedTerm({TEXT("警戒"), TEXT("通缉"), TEXT("追捕"),
						TEXT("搜捕"), TEXT("敌意"), TEXT("起疑"), TEXT("暴露"), TEXT("列为目标"), TEXT("优先目标")});
				else if (Update.Domain == TEXT("environment") && Update.Field == TEXT("combat_edge"))
					bExplicitlySupported = ContainsAnyLockedTerm({TEXT("优势"), TEXT("劣势"), TEXT("先机"),
						TEXT("伏击"), TEXT("地形"), TEXT("遮蔽"), TEXT("破绽"), TEXT("受制"), TEXT("包围"),
						TEXT("退路"), TEXT("视线"), TEXT("偷袭")});
				if (!bExplicitlySupported)
				{
					OutError = FString::Printf(TEXT("选项%c的%s/%s没有得到锁定result_summary或consequence_intent的明确支持；删除该填充效果，不得从普通叙事动作推断"),
						static_cast<TCHAR>(TEXT('A') + ChoiceSlot), *Update.Domain, *Update.Field);
					return false;
				}
				if (Update.Domain == TEXT("body")
					&& (Update.Value.Contains(TEXT("hp_loss"), ESearchCase::IgnoreCase)
						|| Update.Value.Contains(TEXT("hp_gain"), ESearchCase::IgnoreCase)))
				{
					OutError = FString::Printf(TEXT("选项%c把气血变化伪装成body/condition=%s；真实气血增减必须使用operations中的hp，body只登记原文明确命名的伤势或状态"),
						static_cast<TCHAR>(TEXT('A') + ChoiceSlot), *Update.Value);
					return false;
				}
				if (Update.Domain == TEXT("item")
					&& (Update.Op == TEXT("gain") || Update.Op == TEXT("add")))
				{
					const FString ItemName = Update.Target + TEXT(" ") + Update.Value;
					const bool bLooksLikeIntangibleInfo = ItemName.Contains(TEXT("情报"))
						|| ItemName.Contains(TEXT("路线")) || ItemName.Contains(TEXT("暗号"))
						|| ItemName.Contains(TEXT("消息")) || ItemName.Contains(TEXT("秘密"))
						|| ItemName.Contains(TEXT("线索")) || ItemName.Contains(TEXT("位置"));
					const bool bPhysicalMediumTransferred = ContainsAnyLockedTerm({TEXT("地图"), TEXT("信件"),
						TEXT("纸卷"), TEXT("卷轴"), TEXT("令牌"), TEXT("木牌"), TEXT("铜牌"),
						TEXT("玉简"), TEXT("册页"), TEXT("图纸"), TEXT("画像"), TEXT("画卷"),
						TEXT("纸页"), TEXT("抛给"), TEXT("递给"), TEXT("交给"), TEXT("塞进"), TEXT("收进")});
					if (bLooksLikeIntangibleInfo && !bPhysicalMediumTransferred)
					{
						OutError = FString::Printf(TEXT("选项%c把路线、暗号或口头情报误写成item/ownership；没有实体媒介转手时删除该物品更新，并按原文重新裁决其他真实后果"),
							static_cast<TCHAR>(TEXT('A') + ChoiceSlot));
						return false;
					}
				}
			}
			const bool bLockedResultSupportsHarm = LockedResult.Contains(TEXT("受伤"))
				|| LockedResult.Contains(TEXT("负伤")) || LockedResult.Contains(TEXT("失血"))
				|| LockedResult.Contains(TEXT("划伤")) || LockedResult.Contains(TEXT("血痕"))
				|| LockedResult.Contains(TEXT("流血")) || LockedResult.Contains(TEXT("出血"))
				|| LockedResult.Contains(TEXT("中毒")) || LockedResult.Contains(TEXT("灼伤"))
				|| LockedResult.Contains(TEXT("伤口")) || LockedResult.Contains(TEXT("气血受损"))
				|| LockedResult.Contains(TEXT("气血下降")) || LockedResult.Contains(TEXT("剧痛"))
				|| LockedResult.Contains(TEXT("眼前发黑")) || LockedResult.Contains(TEXT("眩晕"))
				|| LockedResult.Contains(TEXT("昏沉"));
			const bool bAddsUnsupportedHarm = Choice.Reward.HPChange < 0
				|| Choice.VariableUpdates.ContainsByPredicate([&LockedTextNames](const FInfiniteVariableUpdate& Update)
				{
					const bool bConditionNamedInResult = LockedTextNames(Update.Value);
					return !bConditionNamedInResult && Update.Domain == TEXT("body")
						&& (Update.Op == TEXT("add") || Update.Op == TEXT("set"))
						&& (Update.Valence == TEXT("negative") || Update.Valence == TEXT("mixed"));
				});
			if (bAddsUnsupportedHarm && !bLockedResultSupportsHarm)
			{
				OutError = FString::Printf(TEXT("选项%c擅自增加了锁定剧情没有发生的玩家伤害；删除hp/body伤害或依据原result_summary改用正确后果"),
					static_cast<TCHAR>(TEXT('A') + ChoiceSlot));
				return false;
			}
			if (Choice.VariableUpdates.Num() == 0 && Choice.Operations.Num() == 0
				&& Choice.CardForgeJobs.Num() == 0
				&& !bHasReward && Choice.Next != TEXT("combat"))
			{
				if (RequestPhase == ERequestPhase::Generation)
				{
					OutError = FString::Printf(TEXT("选项%c没有任何可执行变化；直接重写该选项的行动、结果与工具字段，不得用纯信息或环境记录过关"),
						static_cast<TCHAR>(TEXT('A') + ChoiceSlot));
					return false;
				}
				if (Choice.SettlementKey.IsEmpty())
					Choice.SettlementKey = TEXT("narrative_fact:") + Choice.ResultSummary.Left(140);
				Choice.GmJudgement = NoEffectReason.IsEmpty()
					? TEXT("本分支仅保留叙事结果；其他分支的有效引擎操作不受影响")
					: TEXT("本分支仅保留叙事结果：") + NoEffectReason;
				UE_LOG(LogTemp, Warning, TEXT("[InfiniteRP] choice %c has no executable effect; accepted branch-locally"),
					static_cast<TCHAR>(TEXT('A') + ChoiceSlot));
			}
		}
		TArray<FString> CompiledOperations;
		if (Choice.SettlementKey.IsEmpty())
		{
			const FString StableFact = Choice.ResolvedImpactKind + TEXT("|")
				+ Choice.ResolvedImpactObject + TEXT("|") + Choice.ResultSummary;
			Choice.SettlementKey = FString::Printf(TEXT("direct:%s:%08x"),
				Choice.ResolvedImpactKind.IsEmpty() ? TEXT("effect") : *Choice.ResolvedImpactKind.Left(40),
				FCrc::StrCrc32(*StableFact));
		}
		for (const FInfiniteGameOperation& Operation : Choice.Operations)
			CompiledOperations.Add(Operation.Op);
		TArray<FString> CreatedNames;
		for (const FCardData& Card : Choice.Reward.CreatedCards) CreatedNames.Add(Card.Name);
		for (const FRelicData& Relic : Choice.Reward.CreatedRelics) CreatedNames.Add(Relic.Name);
		UE_LOG(LogTemp, Display, TEXT(
			"[InfiniteRP] compiled choice=%c impact=%s object=%s next=%s ops=[%s] grant_cards=%d grant_relics=%d created=[%s] hp=%+d gold=%+d"),
			TCHAR(TEXT('A') + ChoiceSlot),
			Choice.ResolvedImpactKind.IsEmpty() ? TEXT("missing") : *Choice.ResolvedImpactKind,
			Choice.ResolvedImpactObject.IsEmpty() ? TEXT("-") : *Choice.ResolvedImpactObject, *Choice.Next,
			*FString::Join(CompiledOperations, TEXT(",")), Choice.Reward.Cards.Num(),
			Choice.Reward.RelicIds.Num(), *FString::Join(CreatedNames, TEXT("、")),
			Choice.Reward.HPChange, Choice.Reward.GoldChange);
		if (RequestPhase == ERequestPhase::StateCompilation)
		{
			const FString NaturalFactText = Choice.ResultSummary + TEXT(" ") + Choice.ConsequenceIntent;
			static const TArray<FString> ForbiddenWriterEngineTerms = {
				TEXT("faction_alert"), TEXT("relationship/affinity"), TEXT("rp.items"),
				TEXT("variable_updates"), TEXT("operations"), TEXT("hp_loss"), TEXT("hp_gain"),
				TEXT("好感+"), TEXT("好感-"), TEXT("关系值+"), TEXT("关系值-"),
				TEXT("警戒+"), TEXT("警戒-")
			};
			if (ForbiddenWriterEngineTerms.ContainsByPredicate([&NaturalFactText](const FString& Term)
				{ return NaturalFactText.Contains(Term, ESearchCase::IgnoreCase); }))
			{
				OutError = FString::Printf(TEXT("选项%c把内部字段或数值标签混进了给玩家看的结果文字；把这些字段留在同一选项的variable_updates/operations中，result_summary只写自然语言事实"),
					static_cast<TCHAR>(TEXT('A') + ChoiceSlot));
				return false;
			}
			const bool bResolvedAttempt = Choice.ResultSummary.Contains(TEXT("但"))
				|| Choice.ResultSummary.Contains(TEXT("却")) || Choice.ResultSummary.Contains(TEXT("随后"))
				|| Choice.ResultSummary.Contains(TEXT("失败")) || Choice.ResultSummary.Contains(TEXT("成功"))
				|| Choice.ResultSummary.Contains(TEXT("确认"));
			const bool bUncertainResult = Choice.ResultSummary.Contains(TEXT("若成功"))
				|| Choice.ResultSummary.Contains(TEXT("可能"))
				|| (Choice.ResultSummary.Contains(TEXT("试图")) && !bResolvedAttempt)
				|| Choice.ResultSummary.Contains(TEXT("随时可能"));
			if (bUncertainResult)
			{
				OutError = FString::Printf(TEXT("选项%c的result_summary仍使用可能/试图等未结算措辞；必须写选择后立即确定发生的结果"),
					static_cast<TCHAR>(TEXT('A') + ChoiceSlot));
				return false;
			}
			if (Choice.Next == TEXT("combat"))
			{
				static const TArray<FString> PreResolvedCombatTerms = {
					TEXT("交手"), TEXT("避让"), TEXT("挡下"), TEXT("格挡"), TEXT("躲开"), TEXT("闪避"),
					TEXT("横劈"), TEXT("还招"), TEXT("回剑"), TEXT("斩断"), TEXT("擦破"),
					TEXT("且战且退"), TEXT("击中"), TEXT("受伤"), TEXT("负伤"), TEXT("灼痕"),
					TEXT("已经突围"), TEXT("顺势翻出"), TEXT("逼退"), TEXT("击退"),
					TEXT("砸中"), TEXT("命中"), TEXT("落空"), TEXT("脱手"), TEXT("闷哼"),
					TEXT("擦着"), TEXT("躲过"), TEXT("射空"), TEXT("削断"), TEXT("断裂"),
					TEXT("撞开"), TEXT("挡开"), TEXT("成功脱离"), TEXT("夺路而逃")
				};
				if (PreResolvedCombatTerms.ContainsByPredicate([&Choice](const FString& Term)
					{ return Choice.ResultSummary.Contains(Term); }))
				{
					OutError = FString::Printf(TEXT("选项%c的combat结果提前写了交手、受伤或突围；必须停在武器接触和战斗结算之前"),
						static_cast<TCHAR>(TEXT('A') + ChoiceSlot));
					return false;
				}
			}
		}
		OutBeat.Choices.Add(Choice);
	}
	return OutBeat.Choices.Num() == 3;
}

FString UInfiniteNarrativeService::BuildSystemPrompt(const FInfiniteNarrativeRequestContext& Context) const
{
	const int32 ContextBudget = FMath::Clamp(PendingSettings.InputContextTokens, 8192, 2000000);
	const int32 WorldBookBudget = FMath::Min(FMath::Clamp(PendingSettings.WorldBookTokenBudget, 1000, 100000),
		FMath::Max(1000, ContextBudget / 5));
	FString WorldBook = PendingSettings.WorldBookOverride.TrimStartAndEnd().IsEmpty()
		? LoadWorldBook() : PendingSettings.WorldBookOverride.TrimStartAndEnd();
	if (!PendingSettings.CustomWorldBook.TrimStartAndEnd().IsEmpty())
		WorldBook += TEXT("\n\n[用户追加世界书]\n") + PendingSettings.CustomWorldBook.TrimStartAndEnd();
	WorldBook = ClampPromptSection(WorldBook, WorldBookBudget);
	const FString Authoring = ClampPromptSection(LoadTriggeredAuthoringKnowledge(Context),
		FMath::Max(1800, ContextBudget / 12));
	const FString Catalog = ClampPromptSection(BuildContentCatalog(), FMath::Max(2500, ContextBudget / 10));
	const FString CharacterRegistrySource = PendingSettings.CharacterRegistryOverride.TrimStartAndEnd().IsEmpty()
		? LoadCharacterRegistry() : PendingSettings.CharacterRegistryOverride.TrimStartAndEnd();
	const FString CharacterRegistry = ClampPromptSection(CharacterRegistrySource,
		FMath::Max(800, ContextBudget / 30));
	const FString Checklist = PendingSettings.bEnableContinuityChecklist
		? (PendingSettings.CustomContinuityChecklist.TrimStartAndEnd().IsEmpty()
			? TEXT("连续性检查：人物只知道其应知信息；承诺、伤势、物品归属与关系阶段不跳变；战斗前后顺序正确；正文与数值后果一致；三个分支互不串线。")
			: PendingSettings.CustomContinuityChecklist.TrimStartAndEnd())
		: TEXT("只做最基本的格式与规则检查。");
	return FString::Printf(TEXT(
		"你是一个可由用户世界书完全定义题材、时代和风格的长期 RP 叙事导演，也是卡牌 Roguelike 的规则裁判。世界观只以本次世界书为准；不得自行假设修仙、古代、现代或任何固定题材。你要像成熟的长期 RP 前端一样维持人物、关系、伏笔和数值的一致性。\n"
		"按照世界书自然续写，不要输出分析、思考过程、Markdown 或代码围栏，只输出一个 JSON 对象。连续性和奖励规则只是写作参考，不是拒绝本轮剧情的理由。%s\n"
		"可见剧情（scene.narration + scene.messages[*].text）目标为 %d~%d 个中文字符；这个范围不计算 JSON、选项、变量、奖励和 memory。不要为了凑字重复，不得用截断破坏 JSON。\n"
		"scene.narration 只写环境、动作、心理和叙述；所有直接说出口的台词按出现顺序写入 scene.messages，每条包含 speaker、portrait_id、expression、text，界面会将其显示成即时通讯气泡。严禁把 messages 台词在 narration 中再重复。旧字段 scene.dialogue 留空。\n"
		"若说话人出现在角色头像注册表，每条 message.portrait_id 必须使用对应 id；临时路人可留空，由界面按姓名生成颜色占位。expression 使用角色注册表中的差分键；没有特殊情绪时使用 neutral。\n"
		"推进人物关系或世界冲突；每项选择结果必须落到真实引擎可表达的事实。不要求奖励，负面效果、战斗、交易、关系/势力响应或战术态势同样有效；线索、地点、环境和所谓任务本身不算结果。\n"
		"最近历史只用于自然衔接，不是本轮效果模板；避免重复上一轮的场景、动作、威胁和机械结果。若目标受时辰或路程阻挡，可在正文中直接跳到有效时刻，不得用多个回合填充等待。\n"
		"只能使用内容目录中存在的 card/relic/template_id。敌人 abilities 只能使用世界书列出的受支持 token。\n"
		"三个选项要有不同情绪、风险与可执行后果。每项必须用 next 明确标记 combat 或 continue_rp；next只是路由，交涉、试探、调查和退让也必须在同一result_summary内完成一次回应与真实后果，不能把裁决推给下一轮。\n"
		"combat 选项通常提供 encounter，并尽量说明敌人为何出现；描述不足也可以继续显示。不要提前宣告胜负、血战结束、缴获或机缘；胜利奖励由游戏战斗结算生成。若玩家在开战前已经捡起、收下或失去物品，可写对应 rewards 并设置 reward_timing=immediate，游戏会尽力结算。\n"
		"continue_rp 选项不得提供 encounter，但仍必须当场结算可由第二阶段编译的事实；第一阶段只写自然事实，不直接分配卡牌字段或奖励结构。\n"
		"每个选择必须形成会被后续读取的明确后果；用result_summary写直接因果，用consequence_intent自然说明主体、完成性、所有权与持续状态，不预分类为卡牌、法宝或数值。\n"
		"三个分支严格隔离。资源、持有物、关系、线索、承诺、地点、时间、势力状态、游戏服务或战斗都可构成剧情影响，但只有真实引擎工具才算活跃游戏效果；本游戏没有任务系统，不得生成‘当前任务’或同类系统回执。不得为了凑影响虚构正文没有发生的后果。\n"
		"涉及获得或失去时把事实写清楚，由第二轮事件解释器查询永久内容库、选择复用或原创并登记。\n"
		"原创内容先按剧情身份判定品阶，再按费用、触发频率和作用范围控制强度：common 只给稳定小收益，uncommon 可有条件协同，rare 才允许显著构筑核心；description 只写可执行机制，flavor 只写世界内来历或意境，design_note 只供后台参考且绝不能出现在卡面。只能使用按需知识列出的字段、动作与触发器；本地会尽力裁剪非法字段，不要求你反复修订整幕。art 只可引用合法内容目录里的现有路径，不确定时留空，游戏会自动使用占位图。\n"
		"state_patch 使用 MVU 思路，只输出本幕发生变化的叙事字段；不得覆盖未变化状态，更不得写 hp、gold、deck、cards、relics、combat、damage、rewards 等游戏权威字段。\n"
		"同一 JSON 中必须附带 memory={title,summary,participants,facts,unresolved,keywords,importance}，只记录本幕已发生事实与仍未解决的线索，用于以后压缩历史；不要把选项中尚未发生的未来当成事实。\n\n"
		"[世界书]\n%s\n\n[角色头像注册表]\n%s\n\n[按需激活的内容设计知识]\n%s\n\n[合法内容目录]\n%s"),
		*Checklist, FMath::Clamp(PendingSettings.NarrativeMinChars, 200, 20000),
		FMath::Max(PendingSettings.NarrativeMinChars, PendingSettings.NarrativeMaxChars),
		*WorldBook, *CharacterRegistry, *Authoring, *Catalog);
}

FString UInfiniteNarrativeService::BuildUserPrompt(const FInfiniteNarrativeRequestContext& Context) const
{
	const int32 ContextBudget = FMath::Clamp(PendingSettings.InputContextTokens, 8192, 2000000);
	const int32 MemoryBudget = FMath::Min(FMath::Clamp(PendingSettings.MemoryTokenBudget, 1000, 100000),
		FMath::Max(1000, ContextBudget / 5));
	FString Recent = Context.RecentRawContext;
	if (Recent.IsEmpty() && Context.RecentHistory.Num() > 0) Recent = FString::Join(Context.RecentHistory, TEXT("\n- "));
	if (Recent.IsEmpty()) Recent = TEXT("尚无历史");
	Recent = ClampPromptSection(Recent, FMath::Max(2500, ContextBudget / 4), true);
	const FString Memories = PendingSettings.bEnableStructuredMemory
		? ClampPromptSection(Context.RecalledMemoryContext, MemoryBudget) : TEXT("结构化记忆已关闭");
	const FString Relics = Context.RelicNames.Num() > 0 ? FString::Join(Context.RelicNames, TEXT("、")) : TEXT("无");
	const FString CombatContext = ClampPromptSection(Context.CombatDigest,
		FMath::Max(2500, ContextBudget / 5), true);
	FString GenerationInstruction;
	if (Context.bCombatPrefetch && Context.bAssumeCombatVictoryWithoutLog)
	{
		GenerationInstruction = TEXT(
			"这是模式B的战中后台预演：请求虽然在战斗中发出，但输出时间点必须位于预定胜利之后，生成胜利后的下一幕。"
			"本模式没有本场战斗日志；不得擅自声称玩家无伤、濒死、中毒、使用了某张牌或以某招终结敌人。");
	}
	else if (Context.bCombatPrefetch)
	{
		GenerationInstruction = TEXT(
			"这是模式A的战后精确推演：玩家已经取得胜利。完整战斗日志是可信上下文的一部分；如何让人物理解和回应战况，由你依照世界书、人物性格与剧情需要自行决定。");
	}
	else
	{
		GenerationInstruction = TEXT("根据最近历史延续刚才选择的行动；除非选项明确触发战斗，不要跳过尚未实际发生的战斗。");
	}
	return FString::Printf(TEXT(
		"生成第 %d 轮中的下一幕 RP 场景。%s\n"
		"[不可覆盖的引擎战斗结算事实]\n%s\n"
		"若本条非空，下一幕必须从该结论之后继续；不得重演战斗、复活同一敌人，旧MVU中的战斗中状态均已过期。\n"
		"玩家状态：HP %d/%d，货币 %d，卡组 %d 张，持久物品/伙伴(relic) [%s]。\n"
		"当前 MVU 状态：%s\n"
		"[从旧历史按人物、关键词和重要度召回的长期记忆]\n%s\n\n"
		"[仍保留原文的最近完整轮次]\n%s\n\n"
		"[当前/刚结束的战斗对象]\n%s\n\n"
		"[本场战斗上下文与日志]\n%s\n"
		"玩家自由行动：%s\n"
		"输出严格遵循schema_version=2.0-writer，choices必须正好三项，每项包含next、result_summary与consequence_intent。"),
		Context.Cycle, *GenerationInstruction,
		Context.CombatResolutionFact.IsEmpty() ? TEXT("无") : *Context.CombatResolutionFact,
		Context.HP, Context.MaxHP, Context.Gold, Context.DeckSize, *Relics,
		Context.WorldStateJson.IsEmpty() ? TEXT("{}") : *Context.WorldStateJson,
		Memories.IsEmpty() ? TEXT("尚无长期记忆") : *Memories, *Recent,
		Context.CombatSetup.IsEmpty() ? TEXT("无") : *Context.CombatSetup,
		CombatContext.IsEmpty() ? TEXT("本模式不提供本场日志") : *CombatContext,
		Context.FreeformAction.IsEmpty() ? TEXT("未使用，自然续写") : *Context.FreeformAction);
}

FString UInfiniteNarrativeService::BuildContentCatalog() const
{
	FString Error;
	TArray<FCardData> Cards;
	TArray<FRelicData> Relics;
	TArray<FEnemyData> Enemies;
	UGameDataLibrary::LoadCards(Cards, Error);
	UGameDataLibrary::LoadRelics(Relics, Error);
	UGameDataLibrary::LoadEnemies(Enemies, Error);

	TArray<FString> CardNames;
	for (const FCardData& Card : Cards)
	{
		if (Card.Id.StartsWith(TEXT("curse_"))) continue;
		CardNames.AddUnique(Card.Name);
	}
	TArray<FString> RelicNames;
	for (const FRelicData& Relic : Relics)
		RelicNames.AddUnique(Relic.Name);
	TArray<FString> EnemyNames;
	TArray<FString> EnemyTemplateGuide;
	for (const FEnemyData& Enemy : Enemies)
	{
		if (Enemy.Id.StartsWith(TEXT("proc_"))) continue;
		EnemyNames.AddUnique(Enemy.Name);
		FString Guide = Enemy.Name + TEXT("【原型：") + Enemy.Story.Left(90);
		if (Enemy.Abilities.Num() > 0)
			Guide += TEXT("；机制：") + FString::Join(Enemy.Abilities, TEXT(","));
		Guide += TEXT("】");
		EnemyTemplateGuide.AddUnique(Guide);
	}
	CardNames.Sort();
	RelicNames.Sort();
	EnemyNames.Sort();
	EnemyTemplateGuide.Sort();
	return TEXT("现有卡牌名称：") + FString::Join(CardNames, TEXT("、"))
		+ TEXT("\n现有法宝/伙伴名称：") + FString::Join(RelicNames, TEXT("、"))
		+ TEXT("\n现有敌人模板名称：") + FString::Join(EnemyNames, TEXT("、"))
		+ TEXT("\n敌人模板原型指南：") + FString::Join(EnemyTemplateGuide, TEXT("；"))
		+ TEXT("\n引用已有内容时只输出名称；内部ID由本地引擎解析。剧情敌人不必与模板同名：根据叙事身份、战斗风格和强度选择语义最接近的模板名，把剧情称号另写在encounter.name。名称不存在时才原创卡牌或法宝；敌人暂不原创模板。");
}

FString UInfiniteNarrativeService::LoadCapabilityManifest() const
{
	FString Content;
	const FString Path = FPaths::ProjectContentDir() / TEXT("Data/rp_capability_manifest.json");
	if (FFileHelper::LoadFileToString(Content, *Path)) return Content;
	return TEXT(
		"游戏权威能力：hp与gold只能通过同名差分操作修改；卡牌与法宝只能通过grant/remove或创建内容后授予；"
		"战斗使用next=combat和encounter；商店、奖励、休息、选牌剔除与升级使用对应open/choose操作；"
		"任意叙事事实写入state_patch，但不得覆盖权威数值、卡组、法宝或战斗状态。");
}

FString UInfiniteNarrativeService::LoadWorldBook() const
{
	FString Content;
	const FString Path = FPaths::ProjectContentDir() / TEXT("Data/rp_worldbook.json");
	if (FFileHelper::LoadFileToString(Content, *Path)) return Content;
	return TEXT("世界书缺失。保持严谨因果并延续用户已有上下文，不擅自指定题材；输出schema_version=2.0-writer JSON，每项选择须用next区分combat与continue_rp，并用consequence_intent明确行动完成后的事实。");
}

FString UInfiniteNarrativeService::LoadCharacterRegistry() const
{
	FString Content;
	const FString Path = FPaths::ProjectContentDir() / TEXT("Data/rp_characters.json");
	if (FFileHelper::LoadFileToString(Content, *Path)) return Content;
	return TEXT("{\"characters\":[]}");
}

FString UInfiniteNarrativeService::LoadTriggeredAuthoringKnowledge(
	const FInfiniteNarrativeRequestContext& Context, bool bForceFullManual, const FString& DraftOverride) const
{
	FString Content;
	const FString Path = FPaths::ProjectContentDir() / TEXT("Data/rp_authoring_worldbook.json");
	if (!FFileHelper::LoadFileToString(Content, *Path))
		return TEXT("原创内容只能使用世界书明确列出的字段；不确定时不要原创。");

	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Content);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
		return TEXT("原创内容只能使用世界书明确列出的字段；不确定时不要原创。");

	// The authoring lorebook is scanned before the MVU request. Regex entries aim for
	// high recall only: activation inserts a manual, while the model still judges the
	// subject, completion, negation and ownership semantics from the full locked draft.
	const FString& DraftCorpus = DraftOverride.IsEmpty() ? PendingDraftJson : DraftOverride;
	FString Corpus = DraftCorpus + TEXT("\n") + Context.FreeformAction + TEXT("\n")
		+ Context.WorldStateJson + TEXT("\n") + Context.RecentRawContext + TEXT("\n")
		+ FString::Join(Context.RecentHistory, TEXT("\n"));

	struct FAuthoringEntry
	{
		FString Id;
		FString Content;
		TArray<FString> Triggers;
		TArray<FString> TriggerRegex;
		TArray<FString> Activates;
		bool bConstant = false;
	};
	TArray<FAuthoringEntry> ParsedEntries;
	TMap<FString, int32> EntryById;
	const TArray<TSharedPtr<FJsonValue>>* Entries = nullptr;
	if (Root->TryGetArrayField(TEXT("entries"), Entries) && Entries)
	{
		for (const TSharedPtr<FJsonValue>& EntryValue : *Entries)
		{
			const TSharedPtr<FJsonObject> EntryObject = EntryValue.IsValid() ? EntryValue->AsObject() : nullptr;
			if (!EntryObject.IsValid()) continue;
			FAuthoringEntry Entry;
			Entry.Id = GetString(EntryObject, TEXT("id"));
			Entry.Content = GetString(EntryObject, TEXT("content"));
			EntryObject->TryGetBoolField(TEXT("constant"), Entry.bConstant);
			auto ReadStrings = [&EntryObject](const TCHAR* Field, TArray<FString>& Out)
			{
				const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
				if (!EntryObject->TryGetArrayField(Field, Values) || !Values) return;
				for (const TSharedPtr<FJsonValue>& Value : *Values)
				{
					FString TextValue;
					if (Value.IsValid() && Value->TryGetString(TextValue) && !TextValue.IsEmpty()) Out.Add(TextValue);
				}
			};
			ReadStrings(TEXT("triggers"), Entry.Triggers);
			ReadStrings(TEXT("trigger_regex"), Entry.TriggerRegex);
			ReadStrings(TEXT("activate"), Entry.Activates);
			if (Entry.Id.IsEmpty()) Entry.Id = FString::Printf(TEXT("entry_%d"), ParsedEntries.Num());
			const FString EntryId = Entry.Id;
			EntryById.Add(EntryId, ParsedEntries.Add(MoveTemp(Entry)));
		}
	}

	auto Matches = [](const FAuthoringEntry& Entry, const FString& ScanText) -> bool
	{
		for (const FString& Trigger : Entry.Triggers)
		{
			if (!Trigger.StartsWith(TEXT("__")) && ScanText.Contains(Trigger, ESearchCase::IgnoreCase)) return true;
		}
		for (const FString& PatternText : Entry.TriggerRegex)
		{
			if (PatternText.IsEmpty()) continue;
			const FRegexPattern Pattern(PatternText);
			FRegexMatcher Matcher(Pattern, ScanText);
			if (Matcher.FindNext()) return true;
		}
		return false;
	};

	TSet<FString> ActiveIds;
	TFunction<void(const FString&)> ActivateById;
	ActivateById = [&ParsedEntries, &EntryById, &ActiveIds, &ActivateById](const FString& Id)
	{
		if (Id.IsEmpty() || ActiveIds.Contains(Id)) return;
		const int32* Index = EntryById.Find(Id);
		if (!Index || !ParsedEntries.IsValidIndex(*Index)) return;
		ActiveIds.Add(Id);
		for (const FString& Dependency : ParsedEntries[*Index].Activates) ActivateById(Dependency);
	};
	for (const FAuthoringEntry& Entry : ParsedEntries)
	{
		if (bForceFullManual || Entry.bConstant || Matches(Entry, Corpus)) ActivateById(Entry.Id);
	}

	// SillyTavern-style recursive scanning: newly injected entry content may contain
	// routing markers that activate a dependent entry on the next local scan pass.
	for (int32 Pass = 0; Pass < 8; ++Pass)
	{
		FString RecursiveCorpus = Corpus;
		for (const FAuthoringEntry& Entry : ParsedEntries)
			if (ActiveIds.Contains(Entry.Id)) RecursiveCorpus += TEXT("\n") + Entry.Content;
		const int32 Before = ActiveIds.Num();
		for (const FAuthoringEntry& Entry : ParsedEntries)
			if (!ActiveIds.Contains(Entry.Id) && Matches(Entry, RecursiveCorpus)) ActivateById(Entry.Id);
		if (ActiveIds.Num() == Before) break;
	}

	TArray<FString> Active;
	const FString Router = GetString(Root, TEXT("router"));
	if (!Router.IsEmpty()) Active.Add(Router);
	TArray<FString> ActiveNames;
	for (const FAuthoringEntry& Entry : ParsedEntries)
	{
		if (!ActiveIds.Contains(Entry.Id) || Entry.Content.IsEmpty()) continue;
		Active.Add(Entry.Content);
		ActiveNames.Add(Entry.Id);
	}
	UE_LOG(LogTemp, Verbose, TEXT("[InfiniteRP] authoring lorebook active=%s"),
		ActiveNames.Num() > 0 ? *FString::Join(ActiveNames, TEXT(",")) : TEXT("none"));
	return FString::Join(Active, TEXT("\n\n"));
}

bool UInfiniteNarrativeService::ReserveModelRequest(const TCHAR* PhaseLabel)
{
	constexpr int32 MaxTotalModelRequests = 4;
	if (TotalModelRequestCount >= MaxTotalModelRequests)
	{
		const FString Diagnostic = FString::Printf(TEXT("本轮模型请求已达到安全上限%d次（最后阶段：%s）"),
			MaxTotalModelRequests, PhaseLabel ? PhaseLabel : TEXT("unknown"));
		if (bHasPendingDraftBeat) CompleteBestEffort(Diagnostic);
		else CompleteWithError(Diagnostic);
		return false;
	}
	++TotalModelRequestCount;
	UE_LOG(LogTemp, Display, TEXT("[InfiniteRP] model request %d/%d phase=%s"),
		TotalModelRequestCount, MaxTotalModelRequests, PhaseLabel ? PhaseLabel : TEXT("unknown"));
	return true;
}

void UInfiniteNarrativeService::CompleteWithError(const FString& Diagnostic)
{
	ActiveRequest.Reset();
	RequestPhase = ERequestPhase::Generation;
	PendingStreamUpdate.Unbind();
	if (!PendingCompletion.IsBound()) return;
	FInfiniteNarrativeBeat Beat;
	Beat.bError = true;
	Beat.Diagnostic = Diagnostic;
	FOnInfiniteNarrativeReady Completion = MoveTemp(PendingCompletion);
	Completion.Execute(false, Beat);
}

FInfiniteNarrativeBeat UInfiniteNarrativeService::BuildFallbackBeat(const FInfiniteNarrativeRequestContext& Context,
	const FString& Diagnostic)
{
	FInfiniteNarrativeBeat Beat;
	Beat.bFallback = true;
	Beat.Diagnostic = Diagnostic;
	Beat.Title = TEXT("雨夜 · 旧驿");
	Beat.Speaker = TEXT("沈照璃");
	Beat.PortraitId = TEXT("shen_zhaoli");
	Beat.Expression = TEXT("concerned");
	Beat.Narration = FString::Printf(TEXT(
		"第%d次死里逃生后，你与沈照璃在废弃山驿中避雨。檐角铜铃被山风吹得轻响，"
		"她替你重新包好裂开的护腕，却没有立刻收回手。远处三股妖气正沿不同山道逼近。"),
		FMath::Max(1, Context.Cycle));
	Beat.Dialogue = TEXT("“伤还没好全，别再一个人逞强。你选路，我陪你走。”");
	FInfiniteDialogueLine DialogueLine;
	DialogueLine.Speaker = Beat.Speaker;
	DialogueLine.PortraitId = Beat.PortraitId;
	DialogueLine.Expression = Beat.Expression;
	DialogueLine.Text = Beat.Dialogue;
	Beat.DialogueLines.Add(DialogueLine);
	Beat.StatePatchJson = TEXT("{\"companion\":{\"name\":\"沈照璃\",\"affinity_delta\":1},\"location\":\"旧驿\"}");

	struct FFallbackOption
	{
		const TCHAR* Text;
		const TCHAR* Summary;
		const TCHAR* Next;
		const TCHAR* Card;
		const TCHAR* Enemy;
		const TCHAR* EnemyName;
		const TCHAR* Ability;
	};
	static const FFallbackOption Options[] = {
		{TEXT("接过她递来的药布，坦言自己也害怕失去同伴"), TEXT("你第一次没有用玩笑掩饰恐惧。沈照璃沉默片刻，把一式护身剑诀写入你的玉简。"), TEXT("continue_rp"), TEXT("defend"), TEXT(""), TEXT(""), TEXT("")},
		{TEXT("推开驿门，沿着最浓烈的妖气主动迎上去"), TEXT("你以行动打破沉默。她将灵力渡入剑锋，雨幕中的狼妖随即扑至，战斗一触即发。"), TEXT("combat"), TEXT(""), TEXT("wolf_demon"), TEXT("逐月狼妖"), TEXT("first_strike")},
		{TEXT("查看驿站留下的血字，尝试找出妖气的真正来源"), TEXT("你辨出血字并非遗言，而是一道被人故意留给后来者的警示。沈照璃示意你继续说下去。"), TEXT("continue_rp"), TEXT(""), TEXT(""), TEXT(""), TEXT("")}
	};
	for (const FFallbackOption& Option : Options)
	{
		FInfiniteNarrativeChoice Choice;
		Choice.Text = Option.Text;
		Choice.ResultSummary = Option.Summary;
		Choice.Next = Option.Next;
		if (FCString::Strlen(Option.Card) > 0)
		{
			FInfiniteRewardCard Card;
			Card.CardId = Option.Card;
			Choice.Reward.Cards.Add(Card);
		}
		if (Choice.Next == TEXT("combat"))
		{
			Choice.Enemy.TemplateId = Option.Enemy;
			Choice.Enemy.Name = Option.EnemyName;
			Choice.Enemy.Story = TEXT("山驿雨幕中凝成的敌影，与方才主动迎战的选择存在直接因果。");
			Choice.Enemy.HPScale = 1.f + FMath::Min(1.0f, Context.Cycle * 0.035f);
			Choice.Enemy.IntentScale = 1.f + FMath::Min(0.6f, Context.Cycle * 0.02f);
			Choice.Enemy.Abilities.Add(Option.Ability);
			Choice.Enemy.AbilityDesc = TEXT("受剧情与轮次影响的异变敌人");
		}
		Beat.Choices.Add(Choice);
	}
	return Beat;
}
