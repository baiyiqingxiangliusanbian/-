#include "RunManager.h"
#include "GameDataLibrary.h"
#include "InfiniteNarrativeService.h"
#include "Map/MapGenerator.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "JsonObjectConverter.h"

namespace
{
	TSharedPtr<FJsonObject> ParseRPObject(const FString& Json)
	{
		TSharedPtr<FJsonObject> Object;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
		FJsonSerializer::Deserialize(Reader, Object);
		return Object;
	}

	FString SerializeRPObject(const TSharedPtr<FJsonObject>& Object)
	{
		FString Result;
		if (!Object.IsValid()) return Result;
		const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Result);
		FJsonSerializer::Serialize(Object.ToSharedRef(), Writer);
		return Result;
	}

	bool ValidateRPValue(const TSharedPtr<FJsonValue>& Value, int32 Depth, FString& OutError)
	{
		if (!Value.IsValid() || Depth > 8)
		{
			OutError = Depth > 8 ? TEXT("state_patch 嵌套超过 8 层") : TEXT("state_patch 含空值节点");
			return false;
		}
		if (Value->Type == EJson::Number)
		{
			const double Number = Value->AsNumber();
			if (!FMath::IsFinite(Number) || FMath::Abs(Number) > 1000000.0)
			{
				OutError = TEXT("state_patch 含非法或异常大的数值");
				return false;
			}
		}
		else if (Value->Type == EJson::String && Value->AsString().Len() > 2000)
		{
			OutError = TEXT("state_patch 单个字符串超过 2000 字");
			return false;
		}
		else if (Value->Type == EJson::Array)
		{
			if (Value->AsArray().Num() > 100) { OutError = TEXT("state_patch 单个数组超过 100 项"); return false; }
			for (const TSharedPtr<FJsonValue>& Child : Value->AsArray())
				if (!ValidateRPValue(Child, Depth + 1, OutError)) return false;
		}
		else if (Value->Type == EJson::Object)
		{
			for (const auto& Pair : Value->AsObject()->Values)
				if (!ValidateRPValue(Pair.Value, Depth + 1, OutError)) return false;
		}
		return true;
	}

	void MergeRPObject(const TSharedPtr<FJsonObject>& Target, const TSharedPtr<FJsonObject>& Delta)
	{
		for (const auto& Pair : Delta->Values)
		{
			const TSharedPtr<FJsonObject> DeltaObject = Pair.Value.IsValid() && Pair.Value->Type == EJson::Object
				? Pair.Value->AsObject() : nullptr;
			const TSharedPtr<FJsonValue> ExistingValue = Target->TryGetField(Pair.Key);
			const TSharedPtr<FJsonObject> ExistingObject = ExistingValue.IsValid()
				&& ExistingValue->Type == EJson::Object ? ExistingValue->AsObject() : nullptr;
			if (DeltaObject.IsValid() && ExistingObject.IsValid()) MergeRPObject(ExistingObject, DeltaObject);
			else Target->SetField(Pair.Key, Pair.Value);
		}
	}

	int32 ApproxRPTokens(const FString& Text)
	{
		// 中文通常接近 1~1.5 字/token，英文更疏；这里保守估算，避免上下文顶满。
		return FMath::Max(1, FMath::CeilToInt(Text.Len() * 0.72f));
	}

	FString ClampRPToTokenBudget(const FString& Text, int32 TokenBudget, bool bKeepTail)
	{
		if (TokenBudget <= 0 || ApproxRPTokens(Text) <= TokenBudget) return Text;
		const int32 MaxChars = FMath::Max(200, FMath::FloorToInt(TokenBudget / 0.72f));
		return bKeepTail ? TEXT("……\n") + Text.Right(MaxChars) : Text.Left(MaxChars) + TEXT("\n……");
	}

	void ReadStringArray(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field, TArray<FString>& Out)
	{
		const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
		if (!Object.IsValid() || !Object->TryGetArrayField(Field, Values)) return;
		for (const TSharedPtr<FJsonValue>& Value : *Values)
		{
			FString Item;
			if (Value->TryGetString(Item) && !Item.TrimStartAndEnd().IsEmpty() && Out.Num() < 24)
				Out.Add(Item.Left(160));
		}
	}
}

void URunManager::Log(const FString& Msg) const
{
	UE_LOG(LogTemp, Display, TEXT("[Run] %s"), *Msg);
	OnLog.Broadcast(Msg);
}

bool URunManager::LoadAllData(FString& OutError)
{
	TArray<FCardData> Cards;
	if (!UGameDataLibrary::LoadCards(Cards, OutError)) return false;
	for (const FCardData& C : Cards) CardTable.Add(C.Id, C);

	TArray<FEnemyData> Enemies;
	if (!UGameDataLibrary::LoadEnemies(Enemies, OutError)) return false;
	for (const FEnemyData& E : Enemies)
	{
		EnemyTable.Add(E.Id, E);
		if (E.Tier == TEXT("normal")) NormalEnemyIds.Add(E.Id);
		else if (E.Tier == TEXT("elite")) EliteEnemyIds.Add(E.Id);
	}

	TArray<FRelicData> Relics;
	if (!UGameDataLibrary::LoadRelics(Relics, OutError)) return false;
	for (const FRelicData& R : Relics) RelicTable.Add(R.Id, R);

	TArray<FPillData> Pills;
	if (!UGameDataLibrary::LoadPills(Pills, OutError)) return false;
	for (const FPillData& P : Pills) PillTable.Add(P.Id, P);

	TArray<FEventData> Events;
	if (!UGameDataLibrary::LoadEvents(Events, OutError)) return false;
	for (const FEventData& E : Events) EventTable.Add(E.Id, E);

	LoadPersistentAuthoredContent();
	for (const FCardData& Card : PersistentAuthoredCards)
		if (!Card.Id.IsEmpty()) CardTable.Add(Card.Id, Card);
	for (const FRelicData& Relic : PersistentAuthoredRelics)
		if (!Relic.Id.IsEmpty()) RelicTable.Add(Relic.Id, Relic);

	return true;
}

FString URunManager::GetAuthoredContentSavePath()
{
	return FPaths::ProjectSavedDir() / TEXT("SaveGames/authored_content_library.json");
}

void URunManager::LoadPersistentAuthoredContent()
{
	PersistentAuthoredCards.Reset();
	PersistentAuthoredRelics.Reset();
	if (!bPersistentAuthoredContentEnabled) return;
	FString Content;
	if (!FFileHelper::LoadFileToString(Content, *GetAuthoredContentSavePath())) return;
	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Content);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid()) return;
	const TArray<TSharedPtr<FJsonValue>>* Cards = nullptr;
	if (Root->TryGetArrayField(TEXT("cards"), Cards))
	{
		for (const TSharedPtr<FJsonValue>& Value : *Cards)
		{
			const TSharedPtr<FJsonObject> Object = Value.IsValid() ? Value->AsObject() : nullptr;
			FCardData Card;
			if (Object.IsValid() && FJsonObjectConverter::JsonObjectToUStruct(Object.ToSharedRef(), &Card)
				&& !Card.Id.IsEmpty() && !Card.Name.IsEmpty())
				PersistentAuthoredCards.Add(Card);
		}
	}
	const TArray<TSharedPtr<FJsonValue>>* Relics = nullptr;
	if (Root->TryGetArrayField(TEXT("relics"), Relics))
	{
		for (const TSharedPtr<FJsonValue>& Value : *Relics)
		{
			const TSharedPtr<FJsonObject> Object = Value.IsValid() ? Value->AsObject() : nullptr;
			FRelicData Relic;
			if (Object.IsValid() && FJsonObjectConverter::JsonObjectToUStruct(Object.ToSharedRef(), &Relic)
				&& !Relic.Id.IsEmpty() && !Relic.Name.IsEmpty())
				PersistentAuthoredRelics.Add(Relic);
		}
	}
}

void URunManager::SavePersistentAuthoredContent() const
{
	if (!bPersistentAuthoredContentEnabled) return;
	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetNumberField(TEXT("schema_version"), 1);
	TArray<TSharedPtr<FJsonValue>> Cards;
	for (const FCardData& Card : PersistentAuthoredCards)
		Cards.Add(MakeShared<FJsonValueObject>(FJsonObjectConverter::UStructToJsonObject(Card)));
	Root->SetArrayField(TEXT("cards"), Cards);
	TArray<TSharedPtr<FJsonValue>> Relics;
	for (const FRelicData& Relic : PersistentAuthoredRelics)
		Relics.Add(MakeShared<FJsonValueObject>(FJsonObjectConverter::UStructToJsonObject(Relic)));
	Root->SetArrayField(TEXT("relics"), Relics);
	FString Content;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Content);
	if (!FJsonSerializer::Serialize(Root.ToSharedRef(), Writer)) return;
	IFileManager::Get().MakeDirectory(*(FPaths::ProjectSavedDir() / TEXT("SaveGames")), true);
	FFileHelper::SaveStringToFile(Content, *GetAuthoredContentSavePath());
}

bool URunManager::DeletePersistentAuthoredCard(const FString& CardId)
{
	const FString TargetId = CardId;
	const int32 Removed = PersistentAuthoredCards.RemoveAll([&TargetId](const FCardData& Card)
		{ return Card.Id == TargetId; });
	if (Removed <= 0) return false;
	SavePersistentAuthoredContent();
	return true;
}

bool URunManager::DeletePersistentAuthoredRelic(const FString& RelicId)
{
	const FString TargetId = RelicId;
	const int32 Removed = PersistentAuthoredRelics.RemoveAll([&TargetId](const FRelicData& Relic)
		{ return Relic.Id == TargetId; });
	if (Removed <= 0) return false;
	SavePersistentAuthoredContent();
	return true;
}

// -----------------------------------------------------------
// 元进程（跨局成就）
// -----------------------------------------------------------

int32 URunManager::RarityToTier(const FString& Rarity)
{
	if (Rarity == TEXT("uncommon"))  return 1;
	if (Rarity == TEXT("rare"))      return 2;
	if (Rarity == TEXT("legendary")) return 3;
	return 0;
}

FString URunManager::GetMetaSavePath()
{
	return FPaths::ProjectSavedDir() / TEXT("SaveGames/meta_save.json");
}

void URunManager::EnsureMetaLoaded()
{
	if (bMetaLoaded) return;
	bMetaLoaded = true;

	FString Content;
	if (!FFileHelper::LoadFileToString(Content, *GetMetaSavePath()))
	{
		return;
	}

	TSharedPtr<FJsonObject> Root;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Content);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid()) return;

	Meta.HighestFloor = Root->GetIntegerField(TEXT("highest_floor"));

	const TArray<TSharedPtr<FJsonValue>>* KillsArr;
	if (Root->TryGetArrayField(TEXT("defeated_enemy_ids"), KillsArr))
	{
		for (const TSharedPtr<FJsonValue>& V : *KillsArr)
		{
			FString Id;
			if (V->TryGetString(Id)) Meta.DefeatedEnemyIds.Add(Id);
		}
	}

	// 品级由斩妖图鉴数推导（3种→中品 6种→上品 10种→仙品）
	const int32 N = Meta.DefeatedEnemyIds.Num();
	Meta.UnlockLevel = (N >= 10 ? 3 : N >= 6 ? 2 : N >= 3 ? 1 : 0);
}

void URunManager::SaveMetaToDisk() const
{
	TSharedPtr<FJsonObject> Root = MakeShareable(new FJsonObject);
	Root->SetNumberField(TEXT("highest_floor"), Meta.HighestFloor);
	Root->SetNumberField(TEXT("unlock_level"), Meta.UnlockLevel);

	TArray<TSharedPtr<FJsonValue>> KillsArr;
	for (const FString& Id : Meta.DefeatedEnemyIds)
	{
		KillsArr.Add(MakeShareable(new FJsonValueString(Id)));
	}
	Root->SetArrayField(TEXT("defeated_enemy_ids"), KillsArr);

	FString Content;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Content);
	FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);

	const FString Dir = FPaths::ProjectSavedDir() / TEXT("SaveGames");
	IFileManager::Get().MakeDirectory(*Dir, true);
	FFileHelper::SaveStringToFile(Content, *GetMetaSavePath());
}

void URunManager::RegisterEnemyKills(const TArray<FString>& EnemyIds)
{
	EnsureMetaLoaded();

	bool bChanged = false;
	for (const FString& RuntimeId : EnemyIds)
	{
		FString Id = RuntimeId;
		FString RuntimePrefix;
		FString TemplateId;
		if (RuntimeId.Split(TEXT("__template__"), &RuntimePrefix, &TemplateId) && !TemplateId.IsEmpty())
		{
			Id = TemplateId;
		}
		if (Id.IsEmpty() || Meta.DefeatedEnemyIds.Contains(Id)) continue;
		Meta.DefeatedEnemyIds.Add(Id);
		bChanged = true;

		const FEnemyData* E = EnemyTable.Find(Id);
		GainNotifications.Add(FString::Printf(TEXT("首次斩杀【%s】！斩妖图鉴 %d/%d"),
			E ? *E->Name : *Id, Meta.DefeatedEnemyIds.Num(), 10));
	}
	if (!bChanged) return;

	// 品级解锁判定
	const int32 N = Meta.DefeatedEnemyIds.Num();
	auto Grant = [&](int32 Level, const FString& What)
	{
		if (Meta.UnlockLevel < Level)
		{
			Meta.UnlockLevel = Level;
			GainNotifications.Add(FString::Printf(TEXT("成就解锁：%s 已进入今后的随机池"), *What));
		}
	};
	if (N >= 3) Grant(1, TEXT("【中品】法器与进阶卡牌"));
	if (N >= 6) Grant(2, TEXT("【上品】法器与高阶卡牌"));
	if (N >= 10) Grant(3, TEXT("【仙品】法器与传说卡牌"));

	SaveMetaToDisk();
}

void URunManager::UpdateMetaOnRunEnd()
{
	EnsureMetaLoaded();
	LastUnlockMessages.Reset();

	const int32 ThisRun = State.HighestFloorThisRun;
	if (ThisRun > Meta.HighestFloor)
	{
		Meta.HighestFloor = ThisRun;
		LastUnlockMessages.Add(FString::Printf(TEXT("新纪录：首次到达第 %d 层！"), ThisRun));
		SaveMetaToDisk();
	}
	else if (Meta.HighestFloor > 0)
	{
		LastUnlockMessages.Add(FString::Printf(TEXT("历史最高纪录：第 %d 层（本次第 %d 层）"),
			Meta.HighestFloor, ThisRun));
	}
}

TArray<FString> URunManager::ConsumeGainNotifications()
{
	TArray<FString> Result = GainNotifications;
	GainNotifications.Reset();
	return Result;
}

bool URunManager::StartNewRun(int32 Seed, bool bInfiniteNarrative)
{
	RunSeed = Seed;
	Rng.Initialize(Seed);
	EnsureMetaLoaded();

	CardTable.Reset(); EnemyTable.Reset(); RelicTable.Reset();
	PillTable.Reset(); EventTable.Reset();
	NormalEnemyIds.Reset(); EliteEnemyIds.Reset();

	FString Err;
	if (!LoadAllData(Err))
	{
		Log(FString::Printf(TEXT("数据加载失败: %s"), *Err));
		return false;
	}

	// 初始状态
	State = FRunState();
	State.bRunActive = true;
	State.bInfiniteNarrativeMode = bInfiniteNarrative;
	State.DynamicCards = PersistentAuthoredCards;
	State.DynamicRelics = PersistentAuthoredRelics;
	if (bInfiniteNarrative)
	{
		State.RPCurrentAct = TEXT("act_1");
		State.RPCurrentLocation = TEXT("雨夜破庙");
		FRPRelationshipState Shen;
		Shen.CharacterId = TEXT("shen_zhaoli");
		Shen.DisplayName = TEXT("沈照璃");
		Shen.Affinity = 5;
		Shen.Bond = TEXT("ally");
		State.RPRelationships.Add(Shen);
	}

	TArray<FString> StarterIds;
	if (!UGameDataLibrary::LoadStarterDeck(StarterIds, Err)) return false;
	for (const FString& Id : StarterIds)
	{
		FDeckCard DC;
		DC.CardId = Id;
		State.Deck.Add(DC);
	}

	// 迷雾探索初始化
	CurrentChoices.Reset();
	GainNotifications.Reset();
	LastCombatEnemyIds.Reset();
	if (!State.bInfiniteNarrativeMode) EnsureFloorChoices();

	Log(TEXT("====== 踏上登仙路 ======"));
	Log(FString::Printf(TEXT("境界: %s | 气血 %d/%d | 灵石 %d | 卡组 %d 张"),
		*State.Realm, State.HP, State.MaxHP, State.Gold, State.Deck.Num()));
	Log(FString::Printf(TEXT("本局种子: %d"), Seed));

	return true;
}

// -----------------------------------------------------------
// 迷雾探索（未知路径 + 精英软保底）
// -----------------------------------------------------------

int32 URunManager::EnemyLevelForFloor(int32 Floor)
{
	if (Floor >= 9) return 3;
	if (Floor >= 6) return 2;
	if (Floor >= 3) return 1;
	return 0;
}

FString URunManager::MakeProceduralEnemyVariant(const FEnemyData& Template, int32 Floor,
	int32 ChoiceIndex, int32 SlotIndex, bool bElite, FRandomStream& Stream)
{
	FEnemyData Variant = Template;

	// 同一 seed + 层数 + 选项会得到同一变体，读档后重新生成选项时不会换怪。
	Variant.Id = FString::Printf(TEXT("proc_f%d_c%d_s%d_%s"), Floor, ChoiceIndex, SlotIndex,
		bElite ? TEXT("elite") : TEXT("normal"));
	Variant.Tier = bElite ? TEXT("elite") : TEXT("normal");

	// 层数越高，随机强度区间越往上移动；EnemyLevel 仍负责「凶/厉/煞」的阶段性大倍率。
	const float FloorMin = (bElite ? 0.98f : 0.88f) + Floor * (bElite ? 0.020f : 0.018f);
	const float FloorMax = (bElite ? 1.18f : 1.12f) + Floor * (bElite ? 0.045f : 0.040f);
	const float Power = Stream.FRandRange(FloorMin, FloorMax);
	Variant.MaxHP = FMath::Max(1, FMath::RoundToInt(static_cast<float>(Template.MaxHP) * Power));

	// 意图也随同一强度轻微浮动，避免只有血量变大。
	const float IntentScale = 0.86f + Power * 0.14f;
	for (FEnemyIntent& Intent : Variant.Intents)
	{
		Intent.Value = FMath::Max(1, FMath::RoundToInt(static_cast<float>(Intent.Value) * IntentScale));
		if (Intent.StatusStacks > 0)
		{
			Intent.StatusStacks = FMath::Max(1,
				FMath::RoundToInt(static_cast<float>(Intent.StatusStacks) * (0.92f + Power * 0.08f)));
		}
	}

	// 立绘与模板内容故意解耦：从现有敌人立绘池中随机取一张，验证卡面数据可以独立组合。
	TArray<FString> ArtPool;
	for (const TPair<FString, FEnemyData>& Pair : EnemyTable)
	{
		if (Pair.Key.StartsWith(TEXT("proc_")) || Pair.Value.ArtPath.IsEmpty()) continue;
		ArtPool.AddUnique(Pair.Value.ArtPath);
	}
	if (ArtPool.Num() > 0)
	{
		Variant.ArtPath = ArtPool[Stream.RandRange(0, ArtPool.Num() - 1)];
	}

	static const TCHAR* NamePrefixes[] = {
		TEXT("赤焰"), TEXT("玄铁"), TEXT("幽冥"), TEXT("流云"), TEXT("噬灵"),
		TEXT("裂地"), TEXT("天机"), TEXT("血月"), TEXT("青冥"), TEXT("雾隐")
	};
	static const TCHAR* NameSuffixes[] = {
		TEXT("异种"), TEXT("幻化"), TEXT("变体"), TEXT("煞影"), TEXT("凶灵")
	};
	const int32 PrefixIndex = Stream.RandRange(0, UE_ARRAY_COUNT(NamePrefixes) - 1);
	const int32 SuffixIndex = Stream.RandRange(0, UE_ARRAY_COUNT(NameSuffixes) - 1);
	Variant.Name = FString::Printf(TEXT("%s·%s%s"), NamePrefixes[PrefixIndex], *Template.Name,
		NameSuffixes[SuffixIndex]);

	// 只从战斗引擎已经支持的机制中抽取词条，避免生成只显示不生效的假能力。
	struct FProceduralTrait
	{
		FString Id;
		int32 MinFloor = 0;
	};
	TArray<FProceduralTrait> TraitPool;
	auto AddTrait = [&TraitPool](const TCHAR* Id, int32 MinFloor)
	{
		FProceduralTrait Trait;
		Trait.Id = Id;
		Trait.MinFloor = MinFloor;
		TraitPool.Add(Trait);
	};
	AddTrait(TEXT("first_strike"), 1);
	AddTrait(TEXT("thorns"), 0);
	AddTrait(TEXT("enrage_half"), 0);
	AddTrait(TEXT("drain"), 2);
	AddTrait(TEXT("regen"), 3);
	AddTrait(TEXT("block_aura"), 2);
	AddTrait(TEXT("poison_aura"), 3);
	AddTrait(TEXT("start_weak"), 1);
	AddTrait(TEXT("start_vulnerable"), 2);
	AddTrait(TEXT("hand_limit"), 5);
	AddTrait(TEXT("nightmare"), 6);
	AddTrait(TEXT("gold_drop"), 0);

	auto HasAbility = [&Variant](const FString& AbilityId)
	{
		for (const FString& Ability : Variant.Abilities)
		{
			FString Left, Right;
			if (Ability.Split(TEXT(":"), &Left, &Right))
			{
				if (Left == AbilityId) return true;
			}
			else if (Ability == AbilityId)
			{
				return true;
			}
		}
		return false;
	};

	TArray<FString> TraitLabels;
	const int32 DesiredTraitCount = bElite ? (Floor >= 6 ? 2 : 1)
		: (Floor >= 6 && Stream.FRand() < 0.35f ? 2 : 1);
	for (int32 TraitIndex = 0; TraitIndex < DesiredTraitCount; ++TraitIndex)
	{
		TArray<int32> Eligible;
		for (int32 i = 0; i < TraitPool.Num(); ++i)
		{
			if (Floor >= TraitPool[i].MinFloor && !HasAbility(TraitPool[i].Id)) Eligible.Add(i);
		}
		if (Eligible.Num() == 0) break;

		const FProceduralTrait& Trait = TraitPool[Eligible[Stream.RandRange(0, Eligible.Num() - 1)]];
		int32 Value = 1;
		FString Token = Trait.Id;
		FString Label;
		if (Trait.Id == TEXT("first_strike"))
		{
			Label = TEXT("先发：战斗开始时抢先行动");
		}
		else if (Trait.Id == TEXT("thorns"))
		{
			Value = 1 + Floor / 5;
			Token = FString::Printf(TEXT("thorns:%d"), Value);
			Label = FString::Printf(TEXT("反震：受到攻击反噬%d"), Value);
		}
		else if (Trait.Id == TEXT("enrage_half"))
		{
			Value = 1 + Floor / 4 + Stream.RandRange(0, 1);
			Token = FString::Printf(TEXT("enrage_half:%d"), Value);
			Label = FString::Printf(TEXT("狂性：半血时力量+%d"), Value);
		}
		else if (Trait.Id == TEXT("drain"))
		{
			Value = 1 + Floor / 7;
			Token = FString::Printf(TEXT("drain:%d"), Value);
			Label = FString::Printf(TEXT("噬血：攻击汲取%d点气血"), Value);
		}
		else if (Trait.Id == TEXT("regen"))
		{
			Value = 2 + Floor / 4;
			Token = FString::Printf(TEXT("regen:%d"), Value);
			Label = FString::Printf(TEXT("再生：每回合恢复%d点气血"), Value);
		}
		else if (Trait.Id == TEXT("block_aura"))
		{
			Value = 2 + Floor / 3;
			Token = FString::Printf(TEXT("block_aura:%d"), Value);
			Label = FString::Printf(TEXT("护体：每回合获得%d点罡气"), Value);
		}
		else if (Trait.Id == TEXT("poison_aura"))
		{
			Value = 1 + Floor / 6;
			Token = FString::Printf(TEXT("poison_aura:%d"), Value);
			Label = FString::Printf(TEXT("毒雾：每回合施加%d层中毒"), Value);
		}
		else if (Trait.Id == TEXT("start_weak"))
		{
			Value = 1 + (Floor >= 8 ? 1 : 0);
			Token = FString::Printf(TEXT("start_weak:%d"), Value);
			Label = FString::Printf(TEXT("符镇：开场虚弱%d"), Value);
		}
		else if (Trait.Id == TEXT("start_vulnerable"))
		{
			Value = 1 + (Floor >= 8 ? 1 : 0);
			Token = FString::Printf(TEXT("start_vulnerable:%d"), Value);
			Label = FString::Printf(TEXT("破绽：开场易伤%d"), Value);
		}
		else if (Trait.Id == TEXT("hand_limit"))
		{
			Value = 1 + (Floor >= 9 ? 1 : 0);
			Token = FString::Printf(TEXT("hand_limit:%d"), Value);
			Label = FString::Printf(TEXT("知彼：手牌上限-%d"), Value);
		}
		else if (Trait.Id == TEXT("nightmare"))
		{
			Label = TEXT("梦魇：每回合加深梦魇，攻击它也会加深");
		}
		else if (Trait.Id == TEXT("gold_drop"))
		{
			Value = 2 + Floor / 3;
			Token = FString::Printf(TEXT("gold_drop:%d"), Value);
			Label = FString::Printf(TEXT("贪财：受击掉落%d枚灵石"), Value);
		}

		Variant.Abilities.Add(Token);
		TraitLabels.Add(Label);
	}

	FString GeneratedDesc = FString::Printf(TEXT("随机变异 · 强度 %.2fx"), Power);
	if (!TraitLabels.IsEmpty())
	{
		GeneratedDesc += TEXT("；");
		GeneratedDesc += FString::Join(TraitLabels, TEXT("；"));
	}
	if (!Variant.AbilityDesc.IsEmpty())
	{
		GeneratedDesc += TEXT("。原生机制：");
		GeneratedDesc += Variant.AbilityDesc;
	}
	Variant.AbilityDesc = GeneratedDesc;

	EnemyTable.Add(Variant.Id, Variant);
	Log(FString::Printf(TEXT("  随机敌人生成: %s | 模板 %s | 强度 %.2fx | 词条 %s | 立绘 %s"),
		*Variant.Name, *Template.Name, Power,
		TraitLabels.Num() > 0 ? *FString::Join(TraitLabels, TEXT(" / ")) : TEXT("无"),
		*Variant.ArtPath));
	return Variant.Id;
}

void URunManager::EnsureFloorChoices()
{
	if (!State.bRunActive || CurrentChoices.Num() > 0) return;
	if (State.bInfiniteNarrativeMode) return;
	if (State.CurrentFloor >= TotalFloors) return;

	// 叙事模式：当前 Beat 的选择作为路径选项
	if (bNarrativeMode && NarrativeSys)
	{
		const FNarrativeBeat Beat = GetCurrentNarrativeBeat();
		if (Beat.Choices.Num() > 0)
		{
			CurrentChoices.Reset();
			for (int32 i = 0; i < Beat.Choices.Num(); ++i)
			{
				FMysteryChoice MC;
				MC.bRevealed = true;
				// 使用叙事选项作为纯文本展示
				MC.StoryChoiceIndex = i;
				CurrentChoices.Add(MC);
			}
			return;
		}
	}

	// 选项使用独立随机流（由 种子+层数 决定），保证存档/读档后一致
	FRandomStream ChoiceRng(RunSeed * 31 + State.CurrentFloor * 7919 + 17);
	const int32 F = State.CurrentFloor;
	const int32 Level = EnemyLevelForFloor(F);
	const bool bBossFloor = (F == TotalFloors - 1);

	auto MakeCombatChoice = [&](bool bElite, bool bRevealed, int32 ChoiceIndex)
	{
		FMysteryChoice C;
		C.Type = bElite ? EMapNodeType::Elite : EMapNodeType::Combat;
		C.bRevealed = bRevealed;
		C.EnemyLevel = Level;
		// 敌人构成
		if (bElite)
		{
			const FString TemplateId = EliteEnemyIds[ChoiceRng.RandRange(0, EliteEnemyIds.Num() - 1)];
			if (const FEnemyData* Template = EnemyTable.Find(TemplateId))
			{
				C.EnemyIds.Add(MakeProceduralEnemyVariant(*Template, F, ChoiceIndex, 0, true, ChoiceRng));
			}
		}
		else
		{
			const FString TemplateId = NormalEnemyIds[ChoiceRng.RandRange(0, NormalEnemyIds.Num() - 1)];
			if (const FEnemyData* Template = EnemyTable.Find(TemplateId))
			{
				C.EnemyIds.Add(MakeProceduralEnemyVariant(*Template, F, ChoiceIndex, 0, false, ChoiceRng));
			}
			if (ChoiceRng.FRand() < 0.3f)
			{
				const FString ExtraTemplateId = NormalEnemyIds[ChoiceRng.RandRange(0, NormalEnemyIds.Num() - 1)];
				if (const FEnemyData* ExtraTemplate = EnemyTable.Find(ExtraTemplateId))
				{
					C.EnemyIds.Add(MakeProceduralEnemyVariant(*ExtraTemplate, F, ChoiceIndex, 1, false, ChoiceRng));
				}
			}
		}
		return C;
	};

	if (bBossFloor)
	{
		FMysteryChoice Boss;
		Boss.Type = EMapNodeType::Boss;
		Boss.bRevealed = true;
		Boss.EnemyLevel = Level;
		Boss.EnemyIds.Add(TEXT("blood_ancestor"));
		CurrentChoices.Add(Boss);
		return;
	}

	// ---- 精英软保底：第5层(索引4)起 30% 起步，每层无精英 +30% ----
	bool bEliteAppears = false;
	if (F >= 4)
	{
		const float EliteChance = 0.30f * (State.ElitePity + 1);
		if (ChoiceRng.FRand() < EliteChance) bEliteAppears = true;
	}
	else if (F >= 1 && ChoiceRng.FRand() < 0.08f)
	{
		bEliteAppears = true; // 前四层小概率偶遇
	}
	if (bEliteAppears) State.ElitePity = 0;
	else if (F >= 4) State.ElitePity += 1;

	// ---- 选项数量：第1层2个，其后3个 ----
	const int32 NumOptions = (F == 0) ? 2 : 3;
	for (int32 i = 0; i < NumOptions; ++i)
	{
		if (bEliteAppears && i == 0)
		{
			// 精英固定已揭示（软保底，玩家可见强敌）
			CurrentChoices.Add(MakeCombatChoice(true, true, i));
			continue;
		}

		const float Roll = ChoiceRng.FRand();
		const bool bRevealed = ChoiceRng.FRand() < 0.45f;
		if (Roll < 0.58f)
		{
			CurrentChoices.Add(MakeCombatChoice(false, bRevealed, i));
		}
		else
		{
			FMysteryChoice C;
			C.bRevealed = bRevealed;
			C.EnemyLevel = Level;
			if (Roll < 0.72f)      C.Type = EMapNodeType::Event;
			else if (Roll < 0.82f) C.Type = EMapNodeType::Shop;
			else                   C.Type = EMapNodeType::Rest;
			CurrentChoices.Add(C);
		}
	}

	// ---- 保命项：若全部为已揭示战斗/精英，追加「绕道而行」（隐藏、非精英） ----
	bool bAnyRevealedCombat = false;
	for (const FMysteryChoice& C : CurrentChoices)
	{
		if (C.bRevealed && (C.Type == EMapNodeType::Combat || C.Type == EMapNodeType::Elite)) bAnyRevealedCombat = true;
	}
	if (bAnyRevealedCombat)
	{
		FMysteryChoice Escape;
		Escape.bRevealed = false;
		Escape.EnemyLevel = Level;
		const float Roll = ChoiceRng.FRand();
		if (Roll < 0.60f)      Escape.Type = EMapNodeType::Combat;
		else if (Roll < 0.75f) Escape.Type = EMapNodeType::Event;
		else if (Roll < 0.87f) Escape.Type = EMapNodeType::Rest;
		else                   Escape.Type = EMapNodeType::Shop;
		if (Escape.Type == EMapNodeType::Combat)
		{
			const FString TemplateId = NormalEnemyIds[ChoiceRng.RandRange(0, NormalEnemyIds.Num() - 1)];
			if (const FEnemyData* Template = EnemyTable.Find(TemplateId))
			{
				Escape.EnemyIds.Add(MakeProceduralEnemyVariant(*Template, F, CurrentChoices.Num(), 0, false, ChoiceRng));
			}
		}
		CurrentChoices.Add(Escape);
	}
}

FNodeEncounter URunManager::ChooseOption(int32 Index)
{
	FNodeEncounter Enc;
	if (!State.bRunActive || !CurrentChoices.IsValidIndex(Index)) return Enc;

	const FMysteryChoice Choice = CurrentChoices[Index];
	CurrentChoices.Reset();

	// 叙事模式：推进剧情
	if (bNarrativeMode && Choice.StoryChoiceIndex >= 0)
	{
		Enc.bIsNarrative = true;
		Enc.NarrativeChoiceIndex = Choice.StoryChoiceIndex;
		// 不推进层数，叙事剧情后返回同一层
		return Enc;
	}

	State.CurrentFloor += 1;
	State.HighestFloorThisRun = FMath::Max(State.HighestFloorThisRun, State.CurrentFloor);
	Enc.Type = Choice.Type;
	Enc.EnemyHPBonus = State.KillStreak * 5;
	Enc.EnemyLevel = Choice.EnemyLevel;

	static const TMap<EMapNodeType, FString> TypeNames = {
		{EMapNodeType::Combat, TEXT("战斗")}, {EMapNodeType::Elite, TEXT("精英")},
		{EMapNodeType::Rest, TEXT("打坐调息")}, {EMapNodeType::Shop, TEXT("坊市")},
		{EMapNodeType::Event, TEXT("奇遇")}, {EMapNodeType::Boss, TEXT("BOSS")}
	};
	Log(FString::Printf(TEXT("--- 进入第 %d 层 [%s] ---"), State.CurrentFloor, *TypeNames[Choice.Type]));

	switch (Choice.Type)
	{
	case EMapNodeType::Combat:
	case EMapNodeType::Elite:
	case EMapNodeType::Boss:
		Enc.EnemyIds = Choice.EnemyIds;
		LastCombatNodeType = Choice.Type;
		LastCombatEnemyIds = Choice.EnemyIds;
		for (const FString& Id : Enc.EnemyIds)
		{
			if (const FEnemyData* E = EnemyTable.Find(Id))
			{
				if (!E->Story.IsEmpty()) Enc.StoryText = E->Story;
				Log(FString::Printf(TEXT("  遭遇: %s"), *E->Name));
			}
		}
		break;

	case EMapNodeType::Event:
	{
		TArray<FString> EventIds;
		EventTable.GetKeys(EventIds);
		CurrentEventId = Choice.EventId.IsEmpty()
			? EventIds[Rng.RandRange(0, EventIds.Num() - 1)]
			: Choice.EventId;
		Enc.EventId = CurrentEventId;
		if (const FEventData* Ev = EventTable.Find(CurrentEventId))
		{
			Log(FString::Printf(TEXT("  【%s】%s"), *Ev->Title, *Ev->Text));
		}
		break;
	}

	case EMapNodeType::Shop:
		GenerateShopStock();
		break;

	case EMapNodeType::Rest:
		Log(TEXT("  此处灵气充裕，宜打坐调息"));
		break;
	}

	return Enc;
}

void URunManager::ApplyInfiniteNarrativeReward(const FInfiniteNarrativeReward& Reward)
{
	// Register authored content before resolving its reward IDs. The service has already
	// constrained all actions and values; these definitions remain local to this run.
	bool bPersistentLibraryChanged = false;
	for (const FCardData& Card : Reward.CreatedCards)
	{
		if (Card.Id.IsEmpty()) continue;
		const bool bKnownByName = PersistentAuthoredCards.ContainsByPredicate([&Card](const FCardData& Known)
			{ return Known.Name.Equals(Card.Name, ESearchCase::CaseSensitive); });
		if (!bKnownByName)
		{
			PersistentAuthoredCards.Add(Card);
			bPersistentLibraryChanged = true;
		}
		if (CardTable.Contains(Card.Id)) continue;
		CardTable.Add(Card.Id, Card);
		if (!State.DynamicCards.ContainsByPredicate([&Card](const FCardData& Known) { return Known.Id == Card.Id; }))
			State.DynamicCards.Add(Card);
		Log(FString::Printf(TEXT("LLM 原创卡牌已登记：【%s】"), *Card.Name));
	}
	for (const FRelicData& Relic : Reward.CreatedRelics)
	{
		if (Relic.Id.IsEmpty()) continue;
		const bool bKnownByName = PersistentAuthoredRelics.ContainsByPredicate([&Relic](const FRelicData& Known)
			{ return Known.Name.Equals(Relic.Name, ESearchCase::CaseSensitive); });
		if (!bKnownByName)
		{
			PersistentAuthoredRelics.Add(Relic);
			bPersistentLibraryChanged = true;
		}
		if (RelicTable.Contains(Relic.Id)) continue;
		RelicTable.Add(Relic.Id, Relic);
		if (!State.DynamicRelics.ContainsByPredicate([&Relic](const FRelicData& Known) { return Known.Id == Relic.Id; }))
			State.DynamicRelics.Add(Relic);
		Log(FString::Printf(TEXT("LLM 原创法宝已登记：【%s】"), *Relic.Name));
	}
	if (bPersistentLibraryChanged) SavePersistentAuthoredContent();
	if (Reward.HPChange != 0)
	{
		State.HP = FMath::Clamp(State.HP + Reward.HPChange, 1, State.MaxHP);
		Log(FString::Printf(TEXT("剧情影响：气血 %+d（%d/%d）"), Reward.HPChange, State.HP, State.MaxHP));
	}
	if (Reward.GoldChange != 0)
	{
		State.Gold = FMath::Max(0, State.Gold + Reward.GoldChange);
		Log(FString::Printf(TEXT("剧情影响：灵石 %+d（共 %d）"), Reward.GoldChange, State.Gold));
	}
	for (const FInfiniteRewardCard& RewardCard : Reward.Cards)
	{
		if (!CardTable.Contains(RewardCard.CardId))
		{
			Log(FString::Printf(TEXT("忽略 LLM 未知卡牌：%s"), *RewardCard.CardId));
			continue;
		}
		FDeckCard Card;
		Card.CardId = RewardCard.CardId;
		Card.bUpgraded = RewardCard.bUpgraded;
		State.Deck.Add(Card);
		if (const FCardData* Data = CardTable.Find(Card.CardId))
		{
			GainNotifications.Add(FString::Printf(TEXT("剧情获得卡牌【%s%s】"), *Data->Name,
				Card.bUpgraded ? TEXT("+") : TEXT("")));
			Log(GainNotifications.Last());
		}
	}
	for (const FString& RelicId : Reward.RelicIds)
	{
		if (!RelicTable.Contains(RelicId))
		{
			Log(FString::Printf(TEXT("忽略 LLM 未知法宝：%s"), *RelicId));
			continue;
		}
		if (State.RelicIds.Contains(RelicId))
		{
			Log(FString::Printf(TEXT("剧情法宝已拥有，未重复添加：%s"), *RelicId));
			continue;
		}
		State.RelicIds.Add(RelicId);
		if (const FRelicData* Data = RelicTable.Find(RelicId))
		{
			GainNotifications.Add(FString::Printf(TEXT("剧情获得法宝【%s】"), *Data->Name));
			Log(GainNotifications.Last());
		}
	}
	for (const FString& CardId : Reward.RemovedCardIds)
	{
		if (State.Deck.Num() <= 1)
		{
			Log(TEXT("忽略剧情移除卡牌：卡组至少保留1张牌"));
			break;
		}
		const int32 Index = State.Deck.IndexOfByPredicate([&CardId](const FDeckCard& Card)
			{ return Card.CardId == CardId; });
		if (Index == INDEX_NONE) continue;
		const FCardData* Data = CardTable.Find(CardId);
		State.Deck.RemoveAt(Index);
		Log(FString::Printf(TEXT("剧情失去卡牌【%s】"), Data ? *Data->Name : *CardId));
	}
	for (const FString& RelicId : Reward.RemovedRelicIds)
	{
		if (!State.RelicIds.RemoveSingle(RelicId)) continue;
		const FRelicData* Data = RelicTable.Find(RelicId);
		Log(FString::Printf(TEXT("剧情失去法宝【%s】"), Data ? *Data->Name : *RelicId));
	}
}

bool URunManager::TryCommitNarrativeSettlement(const FString& SettlementKey)
{
	FString Normalized = SettlementKey.TrimStartAndEnd().Left(192);
	Normalized.ToLowerInline();
	if (Normalized.IsEmpty()) return true;
	if (State.RPSettledFactKeys.Contains(Normalized))
	{
		Log(FString::Printf(TEXT("剧情事实已结算，跳过重复影响：%s"), *Normalized));
		return false;
	}
	State.RPSettledFactKeys.Add(Normalized);
	if (State.RPSettledFactKeys.Num() > 256)
		State.RPSettledFactKeys.RemoveAt(0, State.RPSettledFactKeys.Num() - 256);
	return true;
}

void URunManager::ApplyRPWorldStatePatch(const FString& PatchJson)
{
	FString Error;
	if (!ApplyRPWorldStatePatchTransactional(PatchJson, Error) && !Error.IsEmpty())
		Log(FString::Printf(TEXT("忽略非法 RP 状态补丁：%s"), *Error));
}

bool URunManager::ApplyRPWorldStatePatchTransactional(const FString& PatchJson, FString& OutError)
{
	OutError.Reset();
	const FString Trimmed = PatchJson.TrimStartAndEnd();
	if (Trimmed.IsEmpty() || Trimmed == TEXT("{}")) return true;
	if (Trimmed.Len() > 24000) { OutError = TEXT("state_patch 超过 24000 字符"); return false; }

	TSharedPtr<FJsonObject> Patch = ParseRPObject(Trimmed);
	if (!Patch.IsValid()) { OutError = TEXT("state_patch 不是 JSON 对象"); return false; }
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
			OutError = FString::Printf(TEXT("state_patch 不得改写游戏权威字段：%s"), *Pair.Key);
			return false;
		}
		if (!ValidateRPValue(Pair.Value, 1, OutError)) return false;
	}

	TSharedPtr<FJsonObject> Base = ParseRPObject(State.RPWorldStateJson);
	if (!Base.IsValid()) Base = MakeShared<FJsonObject>();
	const FString Snapshot = SerializeRPObject(Base);
	MergeRPObject(Base, Patch);
	const FString Merged = SerializeRPObject(Base);
	if (Merged.IsEmpty()) { OutError = TEXT("state_patch 合并后无法序列化"); return false; }

	if (!Snapshot.IsEmpty())
	{
		State.RPStateSnapshots.Add(Snapshot);
		while (State.RPStateSnapshots.Num() > 20) State.RPStateSnapshots.RemoveAt(0);
	}
	State.RPWorldStateJson = Merged;
	// Older prompts and saves kept location only in the open MVU object. Keep the
	// typed engine location authoritative without discarding that compatibility path.
	FString PatchedLocation;
	if ((Patch->TryGetStringField(TEXT("location"), PatchedLocation)
		|| Patch->TryGetStringField(TEXT("current_location"), PatchedLocation))
		&& !PatchedLocation.TrimStartAndEnd().IsEmpty())
	{
		State.RPCurrentLocation = PatchedLocation.TrimStartAndEnd().Left(160);
	}
	return true;
}

bool URunManager::RestorePreviousRPWorldState(FString& OutError)
{
	OutError.Reset();
	if (State.RPStateSnapshots.Num() == 0) { OutError = TEXT("没有可恢复的 RP 状态快照"); return false; }
	const FString Snapshot = State.RPStateSnapshots.Pop();
	if (!ParseRPObject(Snapshot).IsValid()) { OutError = TEXT("RP 状态快照损坏"); return false; }
	State.RPWorldStateJson = Snapshot;
	return true;
}

void URunManager::AdvanceRPVariableDurations()
{
	for (int32 Index = State.RPFactionAlerts.Num() - 1; Index >= 0; --Index)
	{
		FRPFactionAlertState& Alert = State.RPFactionAlerts[Index];
		if (Alert.RemainingRPTurns > 0 && --Alert.RemainingRPTurns <= 0)
		{
			Log(FString::Printf(TEXT("势力警戒消退：%s"), *Alert.FactionId));
			State.RPFactionAlerts.RemoveAt(Index);
		}
	}
	for (int32 Index = State.RPBodyConditions.Num() - 1; Index >= 0; --Index)
	{
		FRPBodyCondition& Condition = State.RPBodyConditions[Index];
		if (Condition.RemainingRPTurns > 0 && --Condition.RemainingRPTurns <= 0)
			State.RPBodyConditions.RemoveAt(Index);
	}
}

void URunManager::ApplyInfiniteVariableUpdates(const TArray<FInfiniteVariableUpdate>& Updates,
	TArray<FString>& OutReceipts)
{
	OutReceipts.Reset();
	if (Updates.ContainsByPredicate([](const FInfiniteVariableUpdate& Update)
		{ return Update.Causality.Equals(TEXT("incidental"), ESearchCase::IgnoreCase); }))
		State.LastRPIncidentalTurn = State.RPTurnSerial + 1;
	for (const FInfiniteVariableUpdate& Raw : Updates)
	{
		FString Domain = Raw.Domain.TrimStartAndEnd().ToLower();
		FString Field = Raw.Field.TrimStartAndEnd().ToLower();
		FString Op = Raw.Op.TrimStartAndEnd().ToLower();
		const FString Target = Raw.Target.TrimStartAndEnd().Left(80);
		const FString Value = Raw.Value.TrimStartAndEnd().Left(160);
		if (Domain == TEXT("relationship") && Field == TEXT("affinity") && !Target.IsEmpty())
		{
			FRPRelationshipState* Relationship = State.RPRelationships.FindByPredicate(
				[&Target](const FRPRelationshipState& Existing)
				{ return Existing.CharacterId.Equals(Target, ESearchCase::IgnoreCase)
					|| Existing.DisplayName.Equals(Target, ESearchCase::IgnoreCase); });
			if (!Relationship)
			{
				FRPRelationshipState Created;
				Created.CharacterId = Target;
				Created.DisplayName = Target;
				State.RPRelationships.Add(Created);
				Relationship = &State.RPRelationships.Last();
			}
			const int32 Before = Relationship->Affinity;
			Relationship->Affinity = FMath::Clamp(Op == TEXT("set") ? Raw.Amount : Before + Raw.Amount, -100, 100);
			OutReceipts.Add(FString::Printf(TEXT("【%s】好感 %d→%d"),
				*Relationship->DisplayName, Before, Relationship->Affinity));
		}
		else if (Domain == TEXT("relationship") && Field == TEXT("bond") && !Target.IsEmpty() && !Value.IsEmpty())
		{
			if (FRPRelationshipState* Relationship = State.RPRelationships.FindByPredicate(
				[&Target](const FRPRelationshipState& Existing)
				{ return Existing.CharacterId.Equals(Target, ESearchCase::IgnoreCase)
					|| Existing.DisplayName.Equals(Target, ESearchCase::IgnoreCase); }))
			{
				Relationship->Bond = Value.ToLower();
				OutReceipts.Add(FString::Printf(TEXT("【%s】关系：%s"), *Relationship->DisplayName, *Value));
			}
		}
		else if (Domain == TEXT("faction") && Field == TEXT("alert") && !Target.IsEmpty())
		{
			FRPFactionAlertState* Alert = State.RPFactionAlerts.FindByPredicate(
				[&Target](const FRPFactionAlertState& Existing)
				{ return Existing.FactionId.Equals(Target, ESearchCase::IgnoreCase); });
			if (!Alert)
			{
				FRPFactionAlertState Created;
				Created.FactionId = Target;
				State.RPFactionAlerts.Add(Created);
				Alert = &State.RPFactionAlerts.Last();
			}
			const int32 Before = Alert->AlertLevel;
			Alert->AlertLevel = FMath::Clamp(Op == TEXT("set") ? Raw.Amount : Before + Raw.Amount, 0, 5);
			Alert->RemainingRPTurns = Alert->AlertLevel > 0 ? FMath::Clamp(Raw.Duration, 1, 8) : 0;
			OutReceipts.Add(FString::Printf(TEXT("【%s】警戒 %d→%d（%d轮）"), *Target,
				Before, Alert->AlertLevel, Alert->RemainingRPTurns));
			if (Alert->AlertLevel <= 0) State.RPFactionAlerts.RemoveAll([&Target](const FRPFactionAlertState& Existing)
				{ return Existing.FactionId.Equals(Target, ESearchCase::IgnoreCase); });
		}
		else if (Domain == TEXT("faction") && Field == TEXT("trait") && !Target.IsEmpty() && !Value.IsEmpty())
		{
			FRPFactionAlertState* Alert = State.RPFactionAlerts.FindByPredicate(
				[&Target](const FRPFactionAlertState& Existing)
				{ return Existing.FactionId.Equals(Target, ESearchCase::IgnoreCase); });
			if (!Alert)
			{
				FRPFactionAlertState Created;
				Created.FactionId = Target;
				State.RPFactionAlerts.Add(Created);
				Alert = &State.RPFactionAlerts.Last();
			}
			if (Op == TEXT("remove")) Alert->Traits.Remove(Value);
			else Alert->Traits.AddUnique(Value);
			OutReceipts.Add(FString::Printf(TEXT("【%s】特征%s：%s"), *Target,
				Op == TEXT("remove") ? TEXT("消失") : TEXT("揭示"), *Value));
		}
		else if (Domain == TEXT("body") && Field == TEXT("condition") && !Value.IsEmpty())
		{
			const int32 ExistingIndex = State.RPBodyConditions.IndexOfByPredicate(
				[&Value](const FRPBodyCondition& Existing) { return Existing.Name.Equals(Value, ESearchCase::IgnoreCase); });
			if (Op == TEXT("remove"))
			{
				if (ExistingIndex != INDEX_NONE) State.RPBodyConditions.RemoveAt(ExistingIndex);
				OutReceipts.Add(TEXT("身体状态解除：") + Value);
			}
			else
			{
				FRPBodyCondition* Condition = ExistingIndex == INDEX_NONE ? nullptr : &State.RPBodyConditions[ExistingIndex];
				if (!Condition)
				{
					FRPBodyCondition Created;
					Created.Name = Value;
					State.RPBodyConditions.Add(Created);
					Condition = &State.RPBodyConditions.Last();
				}
				Condition->Severity = FMath::Clamp(Raw.Amount == 0 ? 1 : Raw.Amount, 1, 5);
				Condition->RemainingRPTurns = FMath::Clamp(Raw.Duration, 0, 12);
				OutReceipts.Add(FString::Printf(TEXT("身体状态：%s（%d级）"), *Value, Condition->Severity));
			}
		}
		else if (Domain == TEXT("environment") && Field == TEXT("location") && !Value.IsEmpty())
		{
			State.RPCurrentLocation = Value;
			OutReceipts.Add(TEXT("所在地：") + Value);
		}
		else if (Domain == TEXT("environment") && Field == TEXT("trait") && !Value.IsEmpty())
		{
			if (Op == TEXT("remove")) State.RPEnvironmentTraits.Remove(Value);
			else
			{
				State.RPEnvironmentTraits.AddUnique(Value);
				while (State.RPEnvironmentTraits.Num() > 8) State.RPEnvironmentTraits.RemoveAt(0);
			}
			OutReceipts.Add(TEXT("环境变化：") + Value);
		}
		else if (Domain == TEXT("environment") && Field == TEXT("combat_edge"))
		{
			const int32 Before = State.RPCombatEdge;
			const int32 Delta = FMath::Clamp(Raw.Amount, -3, 3);
			State.RPCombatEdge = FMath::Clamp(Op == TEXT("set") ? Delta : Before + Delta, -3, 3);
			if (!Value.IsEmpty()) State.RPCombatEdgeSource = Value;
			else if (State.RPCombatEdge == 0) State.RPCombatEdgeSource.Reset();
			OutReceipts.Add(FString::Printf(TEXT("下一战战术态势 %d→%d%s"), Before, State.RPCombatEdge,
				State.RPCombatEdgeSource.IsEmpty() ? TEXT("") : *FString::Printf(TEXT("（%s）"), *State.RPCombatEdgeSource)));
		}
		else if (Domain == TEXT("skill") && !Target.IsEmpty())
		{
			FRPNarrativeSkillState* Skill = State.RPSkills.FindByPredicate([&Target](const FRPNarrativeSkillState& Existing)
				{ return Existing.Name.Equals(Target, ESearchCase::IgnoreCase); });
			if (!Skill)
			{
				FRPNarrativeSkillState Created;
				Created.Name = Target;
				State.RPSkills.Add(Created);
				Skill = &State.RPSkills.Last();
			}
			if (Op == TEXT("forget")) Skill->bKnown = false;
			else if (Op == TEXT("upgrade")) { Skill->bKnown = true; Skill->Level = FMath::Clamp(Skill->Level + 1, 1, 10); }
			else { Skill->bKnown = true; Skill->Level = FMath::Max(1, Raw.Amount); }
			OutReceipts.Add(FString::Printf(TEXT("技能：%s（%s）"), *Target,
				Skill->bKnown ? *FString::Printf(TEXT("%d级"), Skill->Level) : TEXT("已遗忘")));
		}
		else if (Domain == TEXT("item") && !Target.IsEmpty())
		{
			if (Op == TEXT("lose") || Op == TEXT("remove")) State.RPNarrativeItems.Remove(Target);
			else
			{
				State.RPNarrativeItems.AddUnique(Target);
				while (State.RPNarrativeItems.Num() > 24) State.RPNarrativeItems.RemoveAt(0);
			}
			OutReceipts.Add(FString::Printf(TEXT("物品%s：%s"),
				(Op == TEXT("lose") || Op == TEXT("remove")) ? TEXT("失去") : TEXT("变化"), *Target));
		}
	}
}

FString URunManager::BuildRPVariableContext() const
{
	TArray<FString> Lines;
	Lines.Add(FString::Printf(TEXT("叙事阶段=%s；位置=%s"),
		State.RPCurrentAct.IsEmpty() ? TEXT("未设定") : *State.RPCurrentAct,
		State.RPCurrentLocation.IsEmpty() ? TEXT("未设定") : *State.RPCurrentLocation));
	if (State.RPEnvironmentTraits.Num() > 0) Lines.Add(TEXT("环境特征=") + FString::Join(State.RPEnvironmentTraits, TEXT("、")));
	if (State.RPNarrativeItems.Num() > 0) Lines.Add(TEXT("叙事持有物=") + FString::Join(State.RPNarrativeItems, TEXT("、")));
	if (State.RPCombatEdge != 0)
		Lines.Add(FString::Printf(TEXT("下一战战术态势=%+d（正数利于玩家、负数不利；来源=%s）"),
			State.RPCombatEdge, State.RPCombatEdgeSource.IsEmpty() ? TEXT("未注明") : *State.RPCombatEdgeSource));
	for (const FRPBodyCondition& Condition : State.RPBodyConditions)
		Lines.Add(FString::Printf(TEXT("身体=%s/严重%d/剩余%d轮"), *Condition.Name,
			Condition.Severity, Condition.RemainingRPTurns));
	for (const FRPRelationshipState& Relationship : State.RPRelationships)
	{
		FString Stage = TEXT("清冷认真的同盟");
		if (Relationship.Affinity < -30) Stage = TEXT("疏离或决裂边缘");
		else if (Relationship.Affinity >= 80) Stage = TEXT("道侣候选：可触发明确结缘事件，不得自动成为道侣");
		else if (Relationship.Affinity >= 50) Stage = TEXT("亲近：更多笨拙关心与可爱一面");
		else if (Relationship.Affinity >= 20) Stage = TEXT("信任：允许展现脆弱和私下关心");
		if (Relationship.Bond == TEXT("dao_partner"))
			Stage = TEXT("已结道侣：更亲密可爱，但保留核心性格、判断与自主性");
		Lines.Add(FString::Printf(TEXT("关系=%s[%s] affinity=%d bond=%s；当前表现=%s"),
			*Relationship.DisplayName, *Relationship.CharacterId, Relationship.Affinity,
			*Relationship.Bond, *Stage));
	}
	for (const FRPFactionAlertState& Alert : State.RPFactionAlerts)
		Lines.Add(FString::Printf(TEXT("势力=%s alert=%d 剩余%d轮 特征=%s"), *Alert.FactionId,
			Alert.AlertLevel, Alert.RemainingRPTurns,
			Alert.Traits.Num() > 0 ? *FString::Join(Alert.Traits, TEXT("、")) : TEXT("未知")));
	return FString::Join(Lines, TEXT("\n"));
}

int32 URunManager::GetFactionAlertLevel(const FString& FactionId) const
{
	if (FactionId.IsEmpty()) return 0;
	if (const FRPFactionAlertState* Alert = State.RPFactionAlerts.FindByPredicate(
		[&FactionId](const FRPFactionAlertState& Existing)
		{ return Existing.FactionId.Equals(FactionId, ESearchCase::IgnoreCase); }))
		return Alert->AlertLevel;
	return 0;
}

int32 URunManager::GetInfiniteEnemyHPBonus(const FString& FactionId) const
{
	return State.KillStreak * 5;
}

void URunManager::AddRPHistory(const FString& Entry)
{
	if (Entry.TrimStartAndEnd().IsEmpty()) return;
	State.RPHistory.Add(Entry.Left(1600));
	while (State.RPHistory.Num() > 12) State.RPHistory.RemoveAt(0);
}

void URunManager::AddRPNarrativeTurn(const FString& Title, const FString& Speaker, const FString& Narration,
	const FString& Dialogue, const FString& ChoiceText, const FString& ResultSummary,
	const FString& Next, const FString& MemoryJson, int32 CompressThreshold, int32 KeepRawRounds,
	int32 UnsummarizedTokenThreshold)
{
	FRPNarrativeTurn Turn;
	Turn.TurnId = ++State.RPTurnSerial;
	Turn.Cycle = State.InfiniteCycle;
	Turn.Title = Title.Left(120);
	Turn.Speaker = Speaker.Left(80);
	Turn.Narration = Narration.Left(12000);
	Turn.Dialogue = Dialogue.Left(6000);
	Turn.ChoiceText = ChoiceText.Left(800);
	Turn.ResultSummary = ResultSummary.Left(1800);
	Turn.Next = Next.Left(32);
	Turn.MemoryJson = MemoryJson.Left(12000);
	State.RPRecentTurns.Add(Turn);

	// The quality-first default mirrors SillyTavern: raw chat is durable and only the
	// request-time context packer drops oldest messages if the provider limit is reached.
	if (CompressThreshold <= 0 || KeepRawRounds <= 0) return;
	CompressThreshold = FMath::Clamp(CompressThreshold, 3, 24);
	KeepRawRounds = FMath::Clamp(KeepRawRounds, 2, CompressThreshold);
	UnsummarizedTokenThreshold = FMath::Clamp(UnsummarizedTokenThreshold, 4000, 200000);
	int32 RawTokens = 0;
	for (const FRPNarrativeTurn& Item : State.RPRecentTurns)
		RawTokens += ApproxRPTokens(Item.Narration + Item.Dialogue + Item.ChoiceText + Item.ResultSummary);

	const bool bShouldCompress = State.RPRecentTurns.Num() > CompressThreshold
		|| RawTokens > UnsummarizedTokenThreshold;
	while (bShouldCompress && State.RPRecentTurns.Num() > KeepRawRounds)
	{
		const FRPNarrativeTurn Old = State.RPRecentTurns[0];
		State.RPRecentTurns.RemoveAt(0);
		RawTokens -= ApproxRPTokens(Old.Narration + Old.Dialogue + Old.ChoiceText + Old.ResultSummary);

		FRPMemoryEvent Event;
		Event.EventId = FString::Printf(TEXT("rp_evt_%05d"), Old.TurnId);
		Event.SourceTurn = Old.TurnId;
		Event.Title = Old.Title;
		Event.Summary = FString::Printf(TEXT("%s%s玩家选择【%s】。%s"),
			*Old.Narration.Left(900), Old.Dialogue.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" %s说：%s。"), *Old.Speaker, *Old.Dialogue.Left(400)),
			*Old.ChoiceText, *Old.ResultSummary).Left(1800);
		Event.Importance = Old.Next == TEXT("combat") ? 0.75f : 0.55f;
		Event.Keywords = {Old.Title, Old.Speaker, Old.ChoiceText.Left(40)};

		const TSharedPtr<FJsonObject> Candidate = ParseRPObject(Old.MemoryJson);
		if (Candidate.IsValid())
		{
			Candidate->TryGetStringField(TEXT("title"), Event.Title);
			Candidate->TryGetStringField(TEXT("summary"), Event.Summary);
			double Importance = Event.Importance;
			Candidate->TryGetNumberField(TEXT("importance"), Importance);
			Event.Importance = FMath::Clamp(static_cast<float>(Importance), 0.f, 1.f);
			ReadStringArray(Candidate, TEXT("participants"), Event.Participants);
			ReadStringArray(Candidate, TEXT("facts"), Event.Facts);
			ReadStringArray(Candidate, TEXT("unresolved"), Event.Unresolved);
			ReadStringArray(Candidate, TEXT("keywords"), Event.Keywords);
		}
		State.RPMemoryEvents.Add(Event);
		while (State.RPMemoryEvents.Num() > 240) State.RPMemoryEvents.RemoveAt(0);
	}
}

FString URunManager::BuildRPRecentContext(int32 MaxRounds, int32 TokenBudget) const
{
	TArray<FString> Blocks;
	const int32 Start = MaxRounds <= 0 ? 0
		: FMath::Max(0, State.RPRecentTurns.Num() - FMath::Max(1, MaxRounds));
	for (int32 Index = Start; Index < State.RPRecentTurns.Num(); ++Index)
	{
		const FRPNarrativeTurn& Turn = State.RPRecentTurns[Index];
		Blocks.Add(FString::Printf(TEXT("[原文轮次 %d｜%s]\n%s%s\n玩家选择：%s\n即时发展：%s\n下一步：%s"),
			Turn.TurnId, *Turn.Title, *Turn.Narration,
			Turn.Dialogue.IsEmpty() ? TEXT("") : *FString::Printf(TEXT("\n%s：%s"), *Turn.Speaker, *Turn.Dialogue),
			*Turn.ChoiceText, *Turn.ResultSummary, *Turn.Next));
	}
	if (Blocks.Num() == 0 && State.RPHistory.Num() > 0) Blocks = State.RPHistory;
	return ClampRPToTokenBudget(FString::Join(Blocks, TEXT("\n\n")), TokenBudget, true);
}

FString URunManager::BuildRPMemoryContext(const FString& Query, int32 TokenBudget) const
{
	struct FScoredMemory { float Score = 0.f; int32 Index = 0; };
	TArray<FString> Terms;
	FString Normalized = Query;
	for (const TCHAR Separator : TArray<TCHAR>{TEXT('，'), TEXT('。'), TEXT('、'), TEXT('；'), TEXT(' '), TEXT('\n')})
		Normalized.ReplaceInline(*FString::Chr(Separator), TEXT("|"));
	Normalized.ParseIntoArray(Terms, TEXT("|"), true);
	Terms.RemoveAll([](const FString& Item) { return Item.TrimStartAndEnd().Len() < 2; });

	TArray<FScoredMemory> Scored;
	for (int32 Index = 0; Index < State.RPMemoryEvents.Num(); ++Index)
	{
		const FRPMemoryEvent& Event = State.RPMemoryEvents[Index];
		FScoredMemory Item;
		Item.Index = Index;
		Item.Score = Event.Importance * 2.f + static_cast<float>(Index) / FMath::Max(1, State.RPMemoryEvents.Num());
		const FString Haystack = Event.Title + TEXT(" ") + Event.Summary + TEXT(" ")
			+ FString::Join(Event.Participants, TEXT(" ")) + TEXT(" ") + FString::Join(Event.Keywords, TEXT(" "));
		for (const FString& Term : Terms) if (Haystack.Contains(Term, ESearchCase::IgnoreCase)) Item.Score += 3.f;
		Scored.Add(Item);
	}
	Scored.Sort([](const FScoredMemory& A, const FScoredMemory& B) { return A.Score > B.Score; });

	TArray<FString> Blocks;
	int32 UsedTokens = 0;
	for (const FScoredMemory& Item : Scored)
	{
		const FRPMemoryEvent& Event = State.RPMemoryEvents[Item.Index];
		const FString Block = FString::Printf(TEXT("[%s｜来源轮次%d｜重要度%.2f]\n%s\n人物：%s\n事实：%s\n未决：%s"),
			*Event.Title, Event.SourceTurn, Event.Importance, *Event.Summary,
			*FString::Join(Event.Participants, TEXT("、")), *FString::Join(Event.Facts, TEXT("；")),
			*FString::Join(Event.Unresolved, TEXT("；")));
		const int32 BlockTokens = ApproxRPTokens(Block);
		if (UsedTokens + BlockTokens > TokenBudget) continue;
		Blocks.Add(Block);
		UsedTokens += BlockTokens;
		if (Blocks.Num() >= 16) break;
	}
	return FString::Join(Blocks, TEXT("\n\n"));
}

void URunManager::RecordInfiniteCombatDigest(const TArray<FString>& EnemyIds, int32 TurnCount,
	int32 HPBefore, int32 HPAfter, const TArray<FString>& CombatLog)
{
	TArray<FString> EnemyNames;
	for (const FString& EnemyId : EnemyIds)
	{
		if (const FEnemyData* Enemy = EnemyTable.Find(EnemyId)) EnemyNames.Add(Enemy->Name);
	}
	TArray<FString> KeyLines;
	const int32 Start = FMath::Max(0, CombatLog.Num() - 14);
	for (int32 Index = Start; Index < CombatLog.Num(); ++Index)
	{
		const FString& Line = CombatLog[Index];
		if (Line.Contains(TEXT("伤害")) || Line.Contains(TEXT("击败")) || Line.Contains(TEXT("击杀"))
			|| Line.Contains(TEXT("死亡"))
			|| Line.Contains(TEXT("法宝")) || Line.Contains(TEXT("功法")) || Line.Contains(TEXT("罡气")))
			KeyLines.Add(Line.Left(180));
		if (KeyLines.Num() >= 8) break;
	}
	const FString ResolvedNames = EnemyNames.Num() > 0
		? FString::Join(EnemyNames, TEXT("、")) : TEXT("本场实际战斗对象");
	State.LastResolvedEncounterFact = FString::Printf(TEXT(
		"玩家已经战胜【%s】。这些具体敌人均已被击倒或击杀、失去继续战斗能力，"
		"本次遭遇已经彻底结束。旧历史或MVU状态中任何‘受伤’‘回防’‘战斗未定’‘仍在交战’"
		"的描述均已过期，以本条为准。不得让同一敌人实体再次起身、追击或成为下一场战斗对象；"
		"同类敌人只有在明确是新的个体或群体，并交代新的来源时才可再次出现。"), *ResolvedNames);
	State.LastCombatDigest = FString::Printf(TEXT(
		"最近战斗：对阵%s；共%d回合；气血%d→%d（变化%+d）；结果：胜利。"
		"结论：上述实际战斗对象均已被击倒或击杀，本次遭遇已结束。\n关键记录：%s"),
		*FString::Join(EnemyNames, TEXT("、")), TurnCount, HPBefore, HPAfter, HPAfter - HPBefore,
		KeyLines.Num() > 0 ? *FString::Join(KeyLines, TEXT("｜")) : TEXT("无额外关键记录"));
}

FString URunManager::RegisterInfiniteEnemy(const FInfiniteEnemySpec& Spec, int32 ChoiceIndex)
{
	const FEnemyData* Template = EnemyTable.Find(Spec.TemplateId);
	if (!Template)
	{
		Template = EnemyTable.Find(TEXT("mountain_imp"));
		if (!Template)
		{
			auto FirstEnemy = EnemyTable.CreateConstIterator();
			if (FirstEnemy) Template = &FirstEnemy.Value();
		}
	}
	if (!Template) return TEXT("");

	FEnemyData Variant = *Template;
	const FString TemplateName = Template->Name;
	Variant.Id = FString::Printf(TEXT("llm_cycle_%d_choice_%d__template__%s"),
		State.InfiniteCycle + 1, ChoiceIndex, *Template->Id);
	if (!Spec.Name.IsEmpty()) Variant.Name = Spec.Name.Left(40);
	if (!Spec.Story.IsEmpty()) Variant.Story = Spec.Story.Left(240);
	if (Spec.Tier == TEXT("elite") || Spec.Tier == TEXT("boss")) Variant.Tier = Spec.Tier;
	else Variant.Tier = TEXT("normal");

	const int32 FactionAlert = GetFactionAlertLevel(Spec.FactionId);
	const int32 CombatEdge = FMath::Clamp(State.RPCombatEdge, -3, 3);
	const float CycleScale = 1.f + FMath::Min(3.f, static_cast<float>(State.InfiniteCycle) * 0.07f);
	const float AlertHPScale = 1.f + FactionAlert * 0.05f;
	const float EdgeHPScale = 1.f - CombatEdge * 0.05f;
	Variant.MaxHP = FMath::Clamp(FMath::RoundToInt(
		Variant.MaxHP * Spec.HPScale * CycleScale * AlertHPScale * EdgeHPScale), 1, 2500);
	const float IntentScale = Spec.IntentScale * (1.f + FMath::Min(1.25f, State.InfiniteCycle * 0.035f))
		* (1.f + FactionAlert * 0.03f) * (1.f - CombatEdge * 0.03f);
	for (FEnemyIntent& Intent : Variant.Intents)
	{
		if (Intent.Value > 0) Intent.Value = FMath::Clamp(FMath::RoundToInt(Intent.Value * IntentScale), 1, 250);
		if (Intent.StatusStacks > 0)
			Intent.StatusStacks = FMath::Clamp(FMath::RoundToInt(Intent.StatusStacks * FMath::Sqrt(IntentScale)), 1, 20);
	}

	static const TSet<FString> AllowedAbilities = {
		TEXT("first_strike"), TEXT("thorns"), TEXT("enrage_half"), TEXT("drain"), TEXT("regen"),
		TEXT("block_aura"), TEXT("poison_aura"), TEXT("start_weak"), TEXT("start_vulnerable"),
		TEXT("hand_limit"), TEXT("nightmare"), TEXT("gold_drop"), TEXT("curse_ritual")
	};
	for (const FString& RawToken : Spec.Abilities)
	{
		FString Id;
		FString Value;
		if (!RawToken.Split(TEXT(":"), &Id, &Value)) Id = RawToken;
		if (!AllowedAbilities.Contains(Id)) continue;
		FString SafeToken = Id;
		if (!Value.IsEmpty()) SafeToken += FString::Printf(TEXT(":%d"), FMath::Clamp(FCString::Atoi(*Value), 1, 20));
		Variant.Abilities.AddUnique(SafeToken);
	}
	if (!Spec.AbilityDesc.IsEmpty()) Variant.AbilityDesc = Spec.AbilityDesc.Left(180);
	EnemyTable.Add(Variant.Id, Variant);
	Log(FString::Printf(TEXT("LLM 敌人生成：%s | 模板 %s | HP %d | 轮次 %d | 势力 %s 警戒 %d | 战术态势 %+d"),
		*Variant.Name, *TemplateName, Variant.MaxHP, State.InfiniteCycle + 1,
		Spec.FactionId.IsEmpty() ? TEXT("无") : *Spec.FactionId, FactionAlert, CombatEdge));
	if (CombatEdge != 0)
	{
		State.RPCombatEdge = 0;
		State.RPCombatEdgeSource.Reset();
	}
	return Variant.Id;
}

void URunManager::PrepareInfiniteCombat(EMapNodeType NodeType, const TArray<FString>& EnemyIds)
{
	State.InfiniteCycle += 1;
	State.CurrentFloor = State.InfiniteCycle;
	State.HighestFloorThisRun = FMath::Max(State.HighestFloorThisRun, State.CurrentFloor);
	LastCombatNodeType = NodeType;
	LastCombatEnemyIds = EnemyIds;
}

void URunManager::SavePendingInfiniteCombat(EMapNodeType NodeType, const TArray<FEnemyData>& Enemies, int32 EnemyHPBonus,
	const FString& ResultSummary, const TArray<FDeckCard>& RewardCards, const TArray<FString>& RewardRelics)
{
	State.bInfiniteCombatPending = true;
	State.PendingInfiniteNodeType = NodeType;
	State.PendingInfiniteEnemies = Enemies;
	// Retain the first enemy for backward readers while new saves preserve the full group.
	State.PendingInfiniteEnemy = Enemies.Num() > 0 ? Enemies[0] : FEnemyData();
	State.PendingInfiniteEnemyHPBonus = EnemyHPBonus;
	State.PendingInfiniteResultSummary = ResultSummary.Left(600);
	State.PendingInfiniteRewardCards = RewardCards;
	State.PendingInfiniteRewardRelics = RewardRelics;
}

bool URunManager::RestorePendingInfiniteCombat(FNodeEncounter& OutEncounter, FString& OutResultSummary,
	TArray<FDeckCard>& OutRewardCards, TArray<FString>& OutRewardRelics)
{
	if (!State.bInfiniteNarrativeMode || !State.bInfiniteCombatPending)
		return false;

	TArray<FEnemyData> PendingEnemies = State.PendingInfiniteEnemies;
	if (PendingEnemies.Num() == 0 && !State.PendingInfiniteEnemy.Id.IsEmpty())
		PendingEnemies.Add(State.PendingInfiniteEnemy);
	if (PendingEnemies.Num() == 0) return false;
	for (const FEnemyData& Enemy : PendingEnemies)
	{
		if (Enemy.Id.IsEmpty()) continue;
		EnemyTable.Add(Enemy.Id, Enemy);
	}
	OutEncounter = FNodeEncounter();
	OutEncounter.Type = State.PendingInfiniteNodeType;
	for (const FEnemyData& Enemy : PendingEnemies)
		if (!Enemy.Id.IsEmpty()) OutEncounter.EnemyIds.Add(Enemy.Id);
	if (OutEncounter.EnemyIds.Num() == 0) return false;
	OutEncounter.EnemyHPBonus = State.PendingInfiniteEnemyHPBonus;
	OutEncounter.StoryText = PendingEnemies[0].Story;
	OutResultSummary = State.PendingInfiniteResultSummary;
	OutRewardCards = State.PendingInfiniteRewardCards;
	OutRewardRelics = State.PendingInfiniteRewardRelics;
	LastCombatNodeType = OutEncounter.Type;
	LastCombatEnemyIds = OutEncounter.EnemyIds;
	return true;
}

void URunManager::ClearPendingInfiniteCombat()
{
	State.bInfiniteCombatPending = false;
	State.PendingInfiniteEnemy = FEnemyData();
	State.PendingInfiniteEnemies.Reset();
	State.PendingInfiniteEnemyHPBonus = 0;
	State.PendingInfiniteResultSummary.Reset();
	State.PendingInfiniteRewardCards.Reset();
	State.PendingInfiniteRewardRelics.Reset();
}

FCombatReward URunManager::ResolveCombatVictory(bool bChoseKillLoot, int32 PlayerRemainingHP, int32 GoldFromCombat)
{
	FCombatReward Reward;
	const EMapNodeType NodeType = LastCombatNodeType;

	State.HP = PlayerRemainingHP;

	// 斩妖图鉴登记（新种类 → 成就）
	RegisterEnemyKills(LastCombatEnemyIds);

	// 基础灵石
	switch (NodeType)
	{
	case EMapNodeType::Elite: Reward.Gold = Rng.RandRange(25, 35); break;
	case EMapNodeType::Boss: Reward.Gold = 50; break;
	default: Reward.Gold = Rng.RandRange(10, 17); break;
	}
	Reward.Gold += GoldFromCombat; // 法宝等战斗内产出

	// 卡牌三选一（无重复，强度越高的敌人低品质卡越少）
	TSet<FString> UsedIds;
	for (int32 i = 0; i < 3; ++i)
	{
		FDeckCard DC = RollRandomRewardCard(NodeType);
		int32 Safety = 0;
		while (UsedIds.Contains(DC.CardId) && Safety < 30)
		{
			DC = RollRandomRewardCard(NodeType);
			Safety++;
		}
		UsedIds.Add(DC.CardId);
		Reward.CardChoices.Add(DC);
	}

	// 精英掉法宝
	if (NodeType == EMapNodeType::Elite)
	{
		Reward.RelicId = RollRandomRelic();
		Reward.bKillLootAvailable = true;
		Reward.KillLootGold = Reward.Gold; // 夺宝=翻倍灵石
	}

	Log(FString::Printf(TEXT("战斗胜利! 获得 %d 灵石"), Reward.Gold));
	State.Gold += Reward.Gold;

	// 杀人夺宝
	if (bChoseKillLoot && Reward.bKillLootAvailable)
	{
		float LootMult = 1.f;
		for (const FString& RId : State.RelicIds)
		{
			if (const FRelicData* R = RelicTable.Find(RId))
			{
				if (R->Trigger == TEXT("on_loot")) LootMult += R->Modifier;
			}
		}
		const int32 LootGold = FMath::RoundToInt(Reward.KillLootGold * LootMult);
		State.Gold += LootGold;
		State.KillStreak += 1;

		// 夺宝额外给一张牌
		const FDeckCard ExtraDC = RollRandomRewardCard(NodeType);
		State.Deck.Add(ExtraDC);
		Log(FString::Printf(TEXT("【杀人夺宝】搜刮尸身: +%d 灵石, 额外获得卡牌 [%s%s]"), LootGold, *ExtraDC.CardId, ExtraDC.bUpgraded ? TEXT("+") : TEXT("")));

		Log(FString::Printf(TEXT("魔道因果 +1 (当前 %d)——日后的敌人会更强"), State.KillStreak));
	}

	// 拾取法宝
	if (!Reward.RelicId.IsEmpty())
	{
		State.RelicIds.Add(Reward.RelicId);
		if (const FRelicData* R = RelicTable.Find(Reward.RelicId))
		{
			Log(FString::Printf(TEXT("获得法宝 【%s】: %s"), *R->Name, *R->Description));
			GainNotifications.Add(FString::Printf(TEXT("获得法宝【%s】"), *R->Name));
		}
	}

	PendingReward = Reward;

	// Boss 战胜利 -> 突破
	if (NodeType == EMapNodeType::Boss && !State.bInfiniteNarrativeMode)
	{
		Breakthrough();
	}

	return Reward;
}

void URunManager::PickRewardCard(const FDeckCard& Card)
{
	if (Card.CardId.IsEmpty())
	{
		Log(TEXT("跳过卡牌奖励"));
		return;
	}
	State.Deck.Add(Card);
	if (const FCardData* C = CardTable.Find(Card.CardId))
	{
		Log(FString::Printf(TEXT("获得卡牌 【%s%s】"), *C->Name, Card.bUpgraded ? TEXT("+") : TEXT("")));
	}
}

void URunManager::ResolveDefeat()
{
	State.bRunActive = false;
	State.bRunVictory = false;
	UpdateMetaOnRunEnd();
	Log(TEXT("====== 道消身殒，登仙路断 ======"));
	for (const FString& M : LastUnlockMessages) Log(M);
}

void URunManager::ResolveRest(bool bHeal)
{
	if (bHeal)
	{
		const int32 Healed = FMath::RoundToInt(State.MaxHP * 0.55f);
		State.HP = FMath::Min(State.MaxHP, State.HP + Healed);
		Log(FString::Printf(TEXT("打坐调息，恢复 %d 点气血 (HP %d/%d)"), Healed, State.HP, State.MaxHP));
	}
	else
	{
		// 随机升级一张未升级的牌
		TArray<int32> NotUpgraded;
		for (int32 i = 0; i < State.Deck.Num(); ++i)
		{
			if (!State.Deck[i].bUpgraded) NotUpgraded.Add(i);
		}
		if (NotUpgraded.Num() > 0)
		{
			const int32 Idx = NotUpgraded[Rng.RandRange(0, NotUpgraded.Num() - 1)];
			State.Deck[Idx].bUpgraded = true;
			if (const FCardData* C = CardTable.Find(State.Deck[Idx].CardId))
			{
				Log(FString::Printf(TEXT("悟道突破: 【%s】获得提升"), *C->Name));
			}
		}
	}
}

FString URunManager::ResolveEventChoice(int32 ChoiceIndex)
{
	const FEventData* Ev = EventTable.Find(CurrentEventId);
	if (!Ev || !Ev->Choices.IsValidIndex(ChoiceIndex)) return TEXT("");

	const FEventChoice& Choice = Ev->Choices[ChoiceIndex];
	Log(FString::Printf(TEXT("选择: %s"), *Choice.Text));

	for (const FEventEffect& Eff : Choice.Effects)
	{
		ApplyEventEffect(Eff);
	}

	Log(FString::Printf(TEXT("  %s"), *Choice.ResultText));
	return Choice.ResultText;
}

// -----------------------------------------------------------
// 叙事系统
// -----------------------------------------------------------

void URunManager::InitNarrative(UNarrativeSystem* Sys, const FString& ActId)
{
	NarrativeSys = Sys;
	CurrentActId = ActId;
	bNarrativeMode = true;
	CurrentBeatId = TEXT("");
	PendingNarrativeSummary = TEXT("");
}

FNarrativeBeat URunManager::GetCurrentNarrativeBeat() const
{
	if (NarrativeSys)
	{
		if (!CurrentBeatId.IsEmpty())
		{
			const FNarrativeBeat* B = NarrativeSys->FindBeat(CurrentBeatId);
			if (B) return *B;
		}
		return NarrativeSys->GetStartBeat(CurrentActId);
	}
	FNarrativeBeat Fallback;
	Fallback.BeatId = TEXT("fallback");
	Fallback.NarratorText = TEXT("前路未卜……");
	return Fallback;
}

bool URunManager::AdvanceNarrative(int32 ChoiceIndex)
{
	if (!NarrativeSys) return false;

	const FNarrativeBeat Beat = GetCurrentNarrativeBeat();
	if (!Beat.Choices.IsValidIndex(ChoiceIndex)) return false;

	const FNarrativeOutcome& Outcome = Beat.Choices[ChoiceIndex].Outcome;

	// 应用效果
	if (Outcome.HPChange != 0)
	{
		State.HP = FMath::Clamp(State.HP + Outcome.HPChange, 0, State.MaxHP);
	}
	if (Outcome.GoldChange != 0)
	{
		State.Gold = FMath::Max(0, State.Gold + Outcome.GoldChange);
	}
	if (!Outcome.GainRelicId.IsEmpty())
	{
		State.RelicIds.Add(Outcome.GainRelicId);
	}
	if (!Outcome.GainCardId.IsEmpty())
	{
		FDeckCard DC;
		DC.CardId = Outcome.GainCardId;
		DC.bUpgraded = Outcome.bUpgraded;
		State.Deck.Add(DC);
	}

	PendingNarrativeSummary = Outcome.SummaryText;

	switch (Outcome.Type)
	{
	case ENarrativeOutcomeType::NextBeat:
		CurrentBeatId = Outcome.Param;
		return false; // 继续剧情（不进入游戏节点）

	case ENarrativeOutcomeType::Combat:
	case ENarrativeOutcomeType::Elite:
	case ENarrativeOutcomeType::Boss:
	case ENarrativeOutcomeType::Rest:
	case ENarrativeOutcomeType::Shop:
	case ENarrativeOutcomeType::Event:
		// 进入游戏节点，当前剧情段结束
		bNarrativeMode = false;
		return true;

	case ENarrativeOutcomeType::GameOver:
		State.HP = 0;
		return false;

	default:
		return false;
	}
}

void URunManager::ApplyEventEffect(const FEventEffect& Effect)
{
	const FString& A = Effect.Action;

	if (A == TEXT("gold"))
	{
		State.Gold = FMath::Max(0, State.Gold + Effect.Value);
		Log(FString::Printf(TEXT("  灵石 %+d (共 %d)"), Effect.Value, State.Gold));
	}
	else if (A == TEXT("hp"))
	{
		State.HP = FMath::Clamp(State.HP + Effect.Value, 1, State.MaxHP);
		Log(FString::Printf(TEXT("  气血 %+d (HP %d/%d)"), Effect.Value, State.HP, State.MaxHP));
	}
	else if (A == TEXT("max_hp"))
	{
		State.MaxHP += Effect.Value;
		State.HP += Effect.Value;
		Log(FString::Printf(TEXT("  气血上限 %+d (HP %d/%d)"), Effect.Value, State.HP, State.MaxHP));
	}
	else if (A == TEXT("add_relic"))
	{
		const FString RId = RollRandomRelic();
		if (!RId.IsEmpty())
		{
			State.RelicIds.Add(RId);
			if (const FRelicData* R = RelicTable.Find(RId))
			{
				Log(FString::Printf(TEXT("  获得法宝 【%s】"), *R->Name));
				GainNotifications.Add(FString::Printf(TEXT("获得法宝【%s】"), *R->Name));
			}
		}
	}
	else if (A == TEXT("add_pill"))
	{
		const FString PId = RollRandomPill();
		if (!PId.IsEmpty())
		{
			State.PillIds.Add(PId);
			if (const FPillData* P = PillTable.Find(PId))
			{
				Log(FString::Printf(TEXT("  获得丹药 【%s】"), *P->Name));
				GainNotifications.Add(FString::Printf(TEXT("获得丹药【%s】"), *P->Name));
			}
		}
	}
	else if (A == TEXT("upgrade_random"))
	{
		TArray<int32> NotUpgraded;
		for (int32 i = 0; i < State.Deck.Num(); ++i)
		{
			if (!State.Deck[i].bUpgraded) NotUpgraded.Add(i);
		}
		if (NotUpgraded.Num() > 0)
		{
			const int32 Idx = NotUpgraded[Rng.RandRange(0, NotUpgraded.Num() - 1)];
			State.Deck[Idx].bUpgraded = true;
			if (const FCardData* C = CardTable.Find(State.Deck[Idx].CardId))
			{
				Log(FString::Printf(TEXT("  【%s】获得提升"), *C->Name));
				GainNotifications.Add(FString::Printf(TEXT("卡牌升级【%s】"), *C->Name));
			}
		}
	}
	else if (A == TEXT("add_card"))
	{
		FString CId;
		if (Effect.Param == TEXT("random_rare")) CId = RollRandomCard(TEXT("rare"));
		else if (Effect.Param == TEXT("random")) CId = RollRandomCard();
		else CId = Effect.Param;

		if (!CId.IsEmpty())
		{
			FDeckCard DC;
			DC.CardId = CId;
			State.Deck.Add(DC);
			if (const FCardData* C = CardTable.Find(CId))
			{
				Log(FString::Printf(TEXT("  获得卡牌 【%s】"), *C->Name));
				GainNotifications.Add(FString::Printf(TEXT("获得卡牌【%s】"), *C->Name));
			}
		}
	}
	else if (A == TEXT("kill_streak"))
	{
		State.KillStreak += Effect.Value;
		Log(FString::Printf(TEXT("  魔道因果 %+d (当前 %d)"), Effect.Value, State.KillStreak));
	}
}

void URunManager::GenerateShopStock()
{
	CurrentShopStock.Reset();

	// 3 张卡
	for (int32 i = 0; i < 3; ++i)
	{
		FShopItem Item;
		Item.ItemType = TEXT("card");
		Item.ItemId = RollRandomCard();
		if (const FCardData* C = CardTable.Find(Item.ItemId)) Item.Price = CardPrice(C->Rarity);
		CurrentShopStock.Add(Item);
	}
	// 2 件法宝
	for (int32 i = 0; i < 2; ++i)
	{
		FShopItem Item;
		Item.ItemType = TEXT("relic");
		Item.ItemId = RollRandomRelic();
		if (const FRelicData* R = RelicTable.Find(Item.ItemId)) Item.Price = RelicPrice(R->Rarity);
		CurrentShopStock.Add(Item);
	}
	// 2 颗丹药
	for (int32 i = 0; i < 2; ++i)
	{
		FShopItem Item;
		Item.ItemType = TEXT("pill");
		Item.ItemId = RollRandomPill();
		Item.Price = 18;
		CurrentShopStock.Add(Item);
	}

	Log(TEXT("坊市货品:"));
	for (int32 i = 0; i < CurrentShopStock.Num(); ++i)
	{
		const FShopItem& It = CurrentShopStock[i];
		FString Name = It.ItemId;
		if (It.ItemType == TEXT("card")) { if (const FCardData* C = CardTable.Find(It.ItemId)) Name = C->Name; }
		else if (It.ItemType == TEXT("relic")) { if (const FRelicData* R = RelicTable.Find(It.ItemId)) Name = R->Name; }
		else if (const FPillData* P = PillTable.Find(It.ItemId)) Name = P->Name;
		Log(FString::Printf(TEXT("  [%d] %s - %d 灵石"), i, *Name, It.Price));
	}
}

void URunManager::GenerateNarrativeShopStock(const FString& Rarity, float PriceMultiplier, int32 Count)
{
	CurrentShopStock.Reset();
	TArray<FString> Pool;
	for (const TPair<FString, FCardData>& Pair : CardTable)
	{
		if (!Rarity.IsEmpty() && Pair.Value.Rarity != Rarity) continue;
		if (!Pair.Value.Class.IsEmpty() && Pair.Value.Class != State.CultivatorPathId) continue;
		if (Pair.Value.Type == TEXT("curse") || Pair.Key == TEXT("one_sword")
			|| Pair.Key == TEXT("strike") || Pair.Key == TEXT("defend")) continue;
		Pool.Add(Pair.Key);
	}
	for (int32 Index = 0; Index < FMath::Clamp(Count, 1, 9) && Pool.Num() > 0; ++Index)
	{
		const int32 Pick = Rng.RandRange(0, Pool.Num() - 1);
		FShopItem Item;
		Item.ItemType = TEXT("card");
		Item.ItemId = Pool[Pick];
		if (const FCardData* Card = CardTable.Find(Item.ItemId))
			Item.Price = FMath::Max(1, FMath::RoundToInt(CardPrice(Card->Rarity) * FMath::Clamp(PriceMultiplier, 0.5f, 3.f)));
		CurrentShopStock.Add(Item);
		Pool.RemoveAt(Pick);
	}
	Log(FString::Printf(TEXT("剧情坊市：品级=%s，价格倍率=%.2f，货品=%d"),
		Rarity.IsEmpty() ? TEXT("不限") : *Rarity, PriceMultiplier, CurrentShopStock.Num()));
}

bool URunManager::RemoveDeckCardAt(int32 DeckIndex, bool bAllowCurse)
{
	if (!State.Deck.IsValidIndex(DeckIndex) || State.Deck.Num() <= 1) return false;
	const FDeckCard Card = State.Deck[DeckIndex];
	const FCardData* Data = CardTable.Find(Card.CardId);
	if (!bAllowCurse && Data && Data->Type == TEXT("curse")) return false;
	State.Deck.RemoveAt(DeckIndex);
	Log(FString::Printf(TEXT("剧情代价：剔除卡牌【%s】"), Data ? *Data->Name : *Card.CardId));
	return true;
}

bool URunManager::RemoveDeckCardForGold(int32 DeckIndex, int32 GoldCost, FString& OutCardName)
{
	OutCardName.Reset();
	const int32 SafeCost = FMath::Max(0, GoldCost);
	if (!State.Deck.IsValidIndex(DeckIndex) || State.Gold < SafeCost) return false;

	const FDeckCard Card = State.Deck[DeckIndex];
	const FCardData* Data = CardTable.Find(Card.CardId);
	OutCardName = Data ? Data->Name : Card.CardId;
	State.Gold -= SafeCost;
	State.Deck.RemoveAt(DeckIndex);
	Log(FString::Printf(TEXT("牌组整理：花费 %d 灵石删除【%s】（余 %d 灵石）"),
		SafeCost, *OutCardName, State.Gold));
	SaveRun();
	return true;
}

bool URunManager::UpgradeDeckCardAt(int32 DeckIndex)
{
	if (!State.Deck.IsValidIndex(DeckIndex) || State.Deck[DeckIndex].bUpgraded) return false;
	State.Deck[DeckIndex].bUpgraded = true;
	const FCardData* Data = CardTable.Find(State.Deck[DeckIndex].CardId);
	Log(FString::Printf(TEXT("剧情机缘：升级卡牌【%s】"), Data ? *Data->Name : *State.Deck[DeckIndex].CardId));
	return true;
}

TArray<FShopItem> URunManager::GetShopStock()
{
	return CurrentShopStock;
}

bool URunManager::BuyShopItem(int32 ItemIndex)
{
	if (!CurrentShopStock.IsValidIndex(ItemIndex)) return false;
	FShopItem& Item = CurrentShopStock[ItemIndex];
	if (Item.bSold || State.Gold < Item.Price) return false;

	State.Gold -= Item.Price;
	Item.bSold = true;

	if (Item.ItemType == TEXT("card"))
	{
		FDeckCard DC;
		DC.CardId = Item.ItemId;
		State.Deck.Add(DC);
	}
	else if (Item.ItemType == TEXT("relic"))
	{
		State.RelicIds.Add(Item.ItemId);
	}
	else
	{
		State.PillIds.Add(Item.ItemId);
	}

	FString ItemName = Item.ItemId;
	if (Item.ItemType == TEXT("card")) { if (const FCardData* C = CardTable.Find(Item.ItemId)) ItemName = C->Name; }
	else if (Item.ItemType == TEXT("relic")) { if (const FRelicData* R = RelicTable.Find(Item.ItemId)) ItemName = R->Name; }
	else if (const FPillData* P = PillTable.Find(Item.ItemId)) ItemName = P->Name;
	Log(FString::Printf(TEXT("买下 [%s]，剩余灵石 %d"), *ItemName, State.Gold));
	GainNotifications.Add(FString::Printf(TEXT("购得【%s】"), *ItemName));
	return true;
}

const FEventData* URunManager::GetCurrentEvent() const
{
	return EventTable.Find(CurrentEventId);
}

bool IsCardExcludedFromRewards(const FString& CardId)
{
	return CardId == TEXT("one_sword") || CardId == TEXT("strike") || CardId == TEXT("defend");
}

// -----------------------------------------------------------
// 随机工具
// -----------------------------------------------------------

FDeckCard URunManager::RollRandomRewardCard(EMapNodeType NodeType) const
{
	// 敌人越强，roll 到低品质的概率越低
	float CommonChance = 0.60f;
	float UncommonChance = 0.30f;
	float RareChance = 0.10f;
	switch (NodeType)
	{
	case EMapNodeType::Elite: CommonChance = 0.35f; UncommonChance = 0.40f; RareChance = 0.25f; break;
	case EMapNodeType::Boss:  CommonChance = 0.15f; UncommonChance = 0.35f; RareChance = 0.50f; break;
	default: break;
	}

	FString Rarity;
	const float Roll = Rng.FRand();
	float Acc = 0.f;
	Acc += CommonChance;   if (Roll < Acc) Rarity = TEXT("common");
	if (Rarity.IsEmpty()) { Acc += UncommonChance; if (Roll < Acc) Rarity = TEXT("uncommon"); }
	if (Rarity.IsEmpty()) { Acc += RareChance;     if (Roll < Acc) Rarity = TEXT("rare"); }
	if (Rarity.IsEmpty()) Rarity = TEXT("legendary");

	const int32 Tier = FMath::Min(RarityToTier(Rarity), Meta.UnlockLevel);
	switch (Tier)
	{
	case 1: Rarity = TEXT("uncommon"); break;
	case 2: Rarity = TEXT("rare"); break;
	case 3: Rarity = TEXT("legendary"); break;
	default: Rarity = TEXT("common"); break;
	}

	// 按品级收集可用卡池，排除 one_sword、未强化 strike/defend
	TArray<FString> Pool;
	for (const auto& Pair : CardTable)
	{
		if (Pair.Value.Rarity != Rarity) continue;
		if (!Pair.Value.Class.IsEmpty() && Pair.Value.Class != TEXT("sword")) continue;
		if (Pair.Value.Type == TEXT("curse")) continue;
		if (IsCardExcludedFromRewards(Pair.Key)) continue;
		Pool.Add(Pair.Key);
	}
	if (Pool.Num() == 0)
	{
		// 回退到已解锁全部品级
		for (const auto& Pair : CardTable)
		{
			if (RarityToTier(Pair.Value.Rarity) > Meta.UnlockLevel) continue;
			if (IsCardExcludedFromRewards(Pair.Key)) continue;
			Pool.Add(Pair.Key);
		}
	}

	FDeckCard Result;
	if (Pool.Num() > 0)
	{
		Result.CardId = Pool[Rng.RandRange(0, Pool.Num() - 1)];
		// 40% 概率出现强化版
		Result.bUpgraded = Rng.FRand() < 0.4f;
	}
	return Result;
}

FString URunManager::RollRarity(FRandomStream& Stream) const
{
	// 品级按成就解锁：roll 出品级后压至已解锁上限
	const float Roll = Stream.FRand();
	int32 Tier = (Roll < 0.6f) ? 0 : (Roll < 0.9f ? 1 : 2);
	// 仙品解锁后小概率出现
	if (Meta.UnlockLevel >= 3 && Stream.FRand() < 0.05f) Tier = 3;
	Tier = FMath::Min(Tier, Meta.UnlockLevel);

	switch (Tier)
	{
	case 1: return TEXT("uncommon");
	case 2: return TEXT("rare");
	case 3: return TEXT("legendary");
	default: return TEXT("common");
	}
}

FString URunManager::RollRandomCard(const FString& RarityFilter) const
{
	FString Rarity = RarityFilter;
	if (Rarity.IsEmpty())
	{
		Rarity = RollRarity(Rng);
	}
	else
	{
		// 显式指定品级时同样受解锁上限约束
		const int32 Tier = FMath::Min(RarityToTier(Rarity), Meta.UnlockLevel);
		switch (Tier)
		{
		case 1: Rarity = TEXT("uncommon"); break;
		case 2: Rarity = TEXT("rare"); break;
		case 3: Rarity = TEXT("legendary"); break;
		default: Rarity = TEXT("common"); break;
		}
	}

	TArray<FString> Pool;
	for (const auto& Pair : CardTable)
	{
		if (Pair.Value.Rarity != Rarity) continue;
			// 职业过滤：当前仅剑修，非空且非 sword 的卡不出现
			if (!Pair.Value.Class.IsEmpty() && Pair.Value.Class != TEXT("sword")) continue;
			// 诅咒牌不进入正常奖励池
			if (Pair.Value.Type == TEXT("curse")) continue;
		Pool.Add(Pair.Key);
	}
	if (Pool.Num() == 0)
	{
		// 回退：全部已解锁品级
		for (const auto& Pair : CardTable)
		{
			if (RarityToTier(Pair.Value.Rarity) <= Meta.UnlockLevel) Pool.Add(Pair.Key);
		}
	}
	return Pool.Num() > 0 ? Pool[Rng.RandRange(0, Pool.Num() - 1)] : TEXT("");
}

FString URunManager::RollRandomRelic() const
{
	TArray<FString> Pool;
	for (const auto& Pair : RelicTable)
	{
		if (State.RelicIds.Contains(Pair.Key)) continue;
		if (RarityToTier(Pair.Value.Rarity) > Meta.UnlockLevel) continue;
		Pool.Add(Pair.Key);
	}
	return Pool.Num() > 0 ? Pool[Rng.RandRange(0, Pool.Num() - 1)] : TEXT("");
}

TArray<FString> URunManager::GetAvailableFixedNarrativeRelicIds() const
{
	TArray<FString> Result;
	for (const auto& Pair : RelicTable)
	{
		if (State.RelicIds.Contains(Pair.Key)) continue;
		// Authored relics have their own permanent-library lifecycle. This route exists to
		// make the shipped, hand-designed relic set reachable in infinite narrative mode.
		if (Pair.Key.StartsWith(TEXT("llm_relic_"))) continue;
		Result.Add(Pair.Key);
	}
	Result.Sort();
	return Result;
}

TArray<FString> URunManager::RollInitialRelicChoices(int32 Count)
{
	// 初始三选一法器 100% 为凡品（不受解锁进度影响）
	TArray<FString> Result;
	TArray<FString> Pool;
	for (const auto& Pair : RelicTable)
	{
		if (RarityToTier(Pair.Value.Rarity) == 0) Pool.Add(Pair.Key);
	}
	while (Result.Num() < Count && Pool.Num() > 0)
	{
		int32 Idx = Rng.RandRange(0, Pool.Num() - 1);
		Result.Add(Pool[Idx]);
		Pool.RemoveAt(Idx);
	}
	return Result;
}

FString URunManager::RollRandomPill() const
{
	TArray<FString> Pool;
	for (const auto& Pair : PillTable) Pool.Add(Pair.Key);
	return Pool.Num() > 0 ? Pool[Rng.RandRange(0, Pool.Num() - 1)] : TEXT("");
}

int32 URunManager::CardPrice(const FString& Rarity) const
{
	if (Rarity == TEXT("legendary")) return 120;
	if (Rarity == TEXT("rare")) return 75;
	if (Rarity == TEXT("uncommon")) return 40;
	return 25;
}

int32 URunManager::RelicPrice(const FString& Rarity) const
{
	if (Rarity == TEXT("legendary")) return 180;
	if (Rarity == TEXT("rare")) return 130;
	if (Rarity == TEXT("uncommon")) return 90;
	return 60;
}

// -----------------------------------------------------------
// 存档 / 读档
// -----------------------------------------------------------

bool URunManager::HasSaveFile()
{
	const FString SavePath = FPaths::ProjectSavedDir() / TEXT("SaveGames/run_save.json");
	FString Content;
	if (!FFileHelper::LoadFileToString(Content, *SavePath)) return false;

	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Content);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid()) return false;

	const TSharedPtr<FJsonObject>* StateJson = nullptr;
	if (!Root->TryGetObjectField(TEXT("state"), StateJson) || !StateJson || !StateJson->IsValid()) return false;

	FRunState SavedState;
	if (!FJsonObjectConverter::JsonObjectToUStruct(StateJson->ToSharedRef(), &SavedState)) return false;
	return SavedState.bRunActive;
}

bool URunManager::SaveRun() const
{
	// 主结构
	TSharedPtr<FJsonObject> Root = MakeShareable(new FJsonObject);
	Root->SetNumberField(TEXT("save_version"), 2);
	Root->SetNumberField(TEXT("seed"), RunSeed);

	// Run 状态（USTRUCT 自动序列化，含 CurrentFloor/ElitePity）
	TSharedPtr<FJsonObject> StateJson = FJsonObjectConverter::UStructToJsonObject(State);
	if (!StateJson.IsValid()) return false;
	Root->SetObjectField(TEXT("state"), StateJson);

	Root->SetStringField(TEXT("current_event"), CurrentEventId);

	// 坊市库存
	TArray<TSharedPtr<FJsonValue>> ShopArr;
	for (const FShopItem& It : CurrentShopStock)
	{
		TSharedPtr<FJsonObject> ItemJson = FJsonObjectConverter::UStructToJsonObject(It);
		if (ItemJson.IsValid()) ShopArr.Add(MakeShareable(new FJsonValueObject(ItemJson)));
	}
	Root->SetArrayField(TEXT("shop"), ShopArr);

	FString OutString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutString);
	if (!FJsonSerializer::Serialize(Root, Writer)) return false;

	const FString SavePath = FPaths::ProjectSavedDir() / TEXT("SaveGames/run_save.json");
	const bool bOk = FFileHelper::SaveStringToFile(OutString, *SavePath);
	if (bOk) Log(FString::Printf(TEXT("已存档: %s"), *SavePath));
	return bOk;
}

bool URunManager::LoadRun()
{
	const FString SavePath = FPaths::ProjectSavedDir() / TEXT("SaveGames/run_save.json");
	FString Content;
	if (!FFileHelper::LoadFileToString(Content, *SavePath)) return false;

	TSharedPtr<FJsonObject> Root;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Content);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid()) return false;

	// 存档版本检查（v1 全图地图存档与迷雾探索不兼容，废弃）
	int32 SaveVersion = 0;
	if (!Root->TryGetNumberField(TEXT("save_version"), SaveVersion) || SaveVersion < 2)
	{
		UE_LOG(LogTemp, Display, TEXT("[Run] 旧版本存档不兼容，开始新的征程"));
		return false;
	}

	EnsureMetaLoaded();

	// 数据表
	CardTable.Reset(); EnemyTable.Reset(); RelicTable.Reset();
	PillTable.Reset(); EventTable.Reset();
	NormalEnemyIds.Reset(); EliteEnemyIds.Reset();
	FString Err;
	if (!LoadAllData(Err)) return false;

	RunSeed = Root->GetIntegerField(TEXT("seed"));
	Rng.Initialize(RunSeed);

	const TSharedPtr<FJsonObject>* StateJson;
	if (Root->TryGetObjectField(TEXT("state"), StateJson))
	{
		if (!FJsonObjectConverter::JsonObjectToUStruct(StateJson->ToSharedRef(), &State)) return false;
	}
	else return false;
	for (const FCardData& Card : PersistentAuthoredCards)
	{
		if (!State.DynamicCards.ContainsByPredicate([&Card](const FCardData& Existing)
			{ return Existing.Id == Card.Id; })) State.DynamicCards.Add(Card);
	}
	for (const FRelicData& Relic : PersistentAuthoredRelics)
	{
		if (!State.DynamicRelics.ContainsByPredicate([&Relic](const FRelicData& Existing)
			{ return Existing.Id == Relic.Id; })) State.DynamicRelics.Add(Relic);
	}

	// Runtime-authored content must be restored into lookup tables before the deck and
	// relic list are used to start the next combat.
	for (const FCardData& Card : State.DynamicCards)
	{
		if (!Card.Id.IsEmpty()) CardTable.Add(Card.Id, Card);
	}
	for (const FRelicData& Relic : State.DynamicRelics)
	{
		if (!Relic.Id.IsEmpty()) RelicTable.Add(Relic.Id, Relic);
	}

	// One-time recovery for saves created before narrative/reward causality auditing existed.
	// This exact story fact was already committed to RP history, but the model omitted rewards.
	bool bRecoveredNarrativeReward = false;
	const bool bHistoryOwnsPoisonNeedles = State.RPHistory.ContainsByPredicate([](const FString& Entry)
	{
		return Entry.Contains(TEXT("淬毒飞针"))
			&& (Entry.Contains(TEXT("收好")) || Entry.Contains(TEXT("收入怀中")) || Entry.Contains(TEXT("取下")));
	});
	const bool bDeckHasPoisonNeedles = State.Deck.ContainsByPredicate([](const FDeckCard& Card)
	{
		return Card.CardId == TEXT("poison_needles");
	});
	if (bHistoryOwnsPoisonNeedles && !bDeckHasPoisonNeedles && CardTable.Contains(TEXT("poison_needles")))
	{
		FDeckCard RecoveredCard;
		RecoveredCard.CardId = TEXT("poison_needles");
		State.Deck.Add(RecoveredCard);
		GainNotifications.Add(TEXT("已补登记剧情所得卡牌【淬毒飞针】"));
		Log(TEXT("存档迁移：根据已发生剧情补登记【淬毒飞针】"));
		bRecoveredNarrativeReward = true;
	}

	// 战败或已通关的存档只保留结算记录，不允许从标题页继续复活。
	if (!State.bRunActive) return false;

	// 当前层选项（由 种子+层数 确定性重建）
	CurrentChoices.Reset();

	Root->TryGetStringField(TEXT("current_event"), CurrentEventId);

	// 坊市库存
	CurrentShopStock.Reset();
	const TArray<TSharedPtr<FJsonValue>>* ShopArr;
	if (Root->TryGetArrayField(TEXT("shop"), ShopArr))
	{
		for (const TSharedPtr<FJsonValue>& V : *ShopArr)
		{
			const TSharedPtr<FJsonObject>* ItemJson;
			if (V->TryGetObject(ItemJson))
			{
				FShopItem It;
				FJsonObjectConverter::JsonObjectToUStruct(ItemJson->ToSharedRef(), &It);
				CurrentShopStock.Add(It);
			}
		}
	}

	Log(FString::Printf(TEXT("读档成功: 境界 %s, HP %d/%d, 灵石 %d, 卡组 %d 张"),
		*State.Realm, State.HP, State.MaxHP, State.Gold, State.Deck.Num()));
	if (bRecoveredNarrativeReward) SaveRun();
	return true;
}

void URunManager::Breakthrough()
{
	State.Realm = TEXT("筑基期");
	State.MaxHP += 10;
	State.HP = State.MaxHP;
	State.bRunVictory = true;
	State.bRunActive = false;
	UpdateMetaOnRunEnd();

	Log(TEXT(""));
	Log(TEXT("=============================================="));
	Log(TEXT("  天雷淬体，道基初成！"));
	Log(TEXT("  恭喜突破至 【筑基期】"));
	Log(FString::Printf(TEXT("  气血上限 +10 (HP %d/%d)"), State.HP, State.MaxHP));
	Log(TEXT("  （第一章·完 —— 更多境界敬请期待）"));
	Log(TEXT("=============================================="));
}
