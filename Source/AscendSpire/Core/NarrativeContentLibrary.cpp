#include "NarrativeContentLibrary.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/FileManager.h"
#include "Misc/Base64.h"
#include "Misc/Crc.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Containers/StringConv.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
	FString ReadString(const TSharedPtr<FJsonObject>& Object, const TCHAR* Key,
		const FString& Default = TEXT(""))
	{
		if (!Object.IsValid()) return Default;
		FString Value;
		if (Object->TryGetStringField(Key, Value)) return Value;
		const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
		if (Object->TryGetArrayField(Key, Values) && Values)
		{
			TArray<FString> Parts;
			for (const TSharedPtr<FJsonValue>& Item : *Values)
			{
				FString Part;
				if (Item.IsValid() && Item->TryGetString(Part) && !Part.IsEmpty()) Parts.Add(Part);
			}
			if (Parts.Num() > 0) return FString::Join(Parts, TEXT("\n"));
		}
		return Default;
	}

	void ReadStringArray(const TSharedPtr<FJsonObject>& Object, const TCHAR* Key,
		TArray<FString>& Out)
	{
		if (!Object.IsValid()) return;
		const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
		if (!Object->TryGetArrayField(Key, Values) || !Values) return;
		for (const TSharedPtr<FJsonValue>& Item : *Values)
		{
			FString Value;
			if (Item.IsValid() && Item->TryGetString(Value) && !Value.IsEmpty()) Out.Add(Value);
		}
	}

	TSharedPtr<FJsonObject> ParseObject(const FString& Json)
	{
		TSharedPtr<FJsonObject> Root;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
		return FJsonSerializer::Deserialize(Reader, Root) && Root.IsValid() ? Root : nullptr;
	}

	FString ObjectToString(const TSharedPtr<FJsonObject>& Object)
	{
		if (!Object.IsValid()) return TEXT("{}");
		FString Result;
		const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Result);
		FJsonSerializer::Serialize(Object.ToSharedRef(), Writer);
		return Result;
	}

	FString FileStem(const FString& Path, const FString& Fallback)
	{
		FString Stem = FPaths::GetBaseFilename(Path);
		if (Stem.IsEmpty()) Stem = Fallback;
		return Stem;
	}

	bool IsSafeStoredId(const FString& Id)
	{
		if (Id.IsEmpty() || Id.Len() > 96 || Id.Contains(TEXT(".."))
			|| Id.Contains(TEXT("/")) || Id.Contains(TEXT("\\"))) return false;
		for (const TCHAR Ch : Id)
		{
			if (!(FChar::IsAlnum(Ch) || Ch == TEXT('_') || Ch == TEXT('-'))) return false;
		}
		return true;
	}

	uint32 ReadBigEndian32(const TArray<uint8>& Bytes, int32 Offset)
	{
		if (Offset < 0 || Offset + 4 > Bytes.Num()) return 0;
		return (static_cast<uint32>(Bytes[Offset]) << 24)
			| (static_cast<uint32>(Bytes[Offset + 1]) << 16)
			| (static_cast<uint32>(Bytes[Offset + 2]) << 8)
			| static_cast<uint32>(Bytes[Offset + 3]);
	}

	FString BytesToUtf8(const TArray<uint8>& Bytes)
	{
		if (Bytes.Num() == 0) return FString();
		const FUTF8ToTCHAR Converted(reinterpret_cast<const ANSICHAR*>(Bytes.GetData()), Bytes.Num());
		return FString(Converted.Length(), Converted.Get());
	}

	FString DecodeMetadataValue(const FString& Value)
	{
		FString Trimmed = Value;
		Trimmed.TrimStartAndEndInline();
		if (Trimmed.StartsWith(TEXT("{")) || Trimmed.StartsWith(TEXT("["))) return Trimmed;
		TArray<uint8> Decoded;
		if (FBase64::Decode(Trimmed, Decoded) && Decoded.Num() > 0)
		{
			const FString Json = BytesToUtf8(Decoded).TrimStartAndEnd();
			if (Json.StartsWith(TEXT("{")) || Json.StartsWith(TEXT("["))) return Json;
		}
		return FString();
	}

	void ReplaceObjectStrings(const TSharedPtr<FJsonObject>& Object, const FString& Character,
		const FString& User)
	{
		if (!Object.IsValid()) return;
		for (auto& Pair : Object->Values)
		{
			if (!Pair.Value.IsValid()) continue;
			if (Pair.Value->Type == EJson::String)
			{
				Pair.Value = MakeShared<FJsonValueString>(FNarrativeContentLibrary::ReplaceMacros(
					Pair.Value->AsString(), Character, User));
			}
			else if (Pair.Value->Type == EJson::Object)
			{
				ReplaceObjectStrings(Pair.Value->AsObject(), Character, User);
			}
			else if (Pair.Value->Type == EJson::Array)
			{
				const TArray<TSharedPtr<FJsonValue>>* Array = nullptr;
				if (!Pair.Value->TryGetArray(Array) || !Array) continue;
				for (const TSharedPtr<FJsonValue>& Item : *Array)
				{
					if (!Item.IsValid()) continue;
					if (Item->Type == EJson::Object) ReplaceObjectStrings(Item->AsObject(), Character, User);
				}
			}
		}
	}
}

FString FNarrativeContentLibrary::GetNarrativeRootDirectory()
{
	return FPaths::ProjectSavedDir() / TEXT("Narrative");
}

FString FNarrativeContentLibrary::GetWorldBooksDirectory()
{
	return GetNarrativeRootDirectory() / TEXT("WorldBooks");
}

FString FNarrativeContentLibrary::GetCharacterCardsDirectory()
{
	return GetNarrativeRootDirectory() / TEXT("CharacterCards");
}

FString FNarrativeContentLibrary::SanitizeId(const FString& Candidate, const FString& Prefix)
{
	FString Clean;
	for (const TCHAR Ch : Candidate)
	{
		if (FChar::IsAlnum(Ch) || Ch == TEXT('_') || Ch == TEXT('-')) Clean.AppendChar(Ch);
	}
	Clean = Clean.Left(56);
	if (Clean.IsEmpty()) Clean = TEXT("asset");
	return Prefix + Clean;
}

FString FNarrativeContentLibrary::MakeWorldBookId(const FString& RawJson, const FString& SourcePath)
{
	return FString::Printf(TEXT("worldbook_%08x"), FCrc::StrCrc32(*(SourcePath + TEXT("\n") + RawJson)));
}

FString FNarrativeContentLibrary::MakeCharacterCardId(const FString& RawJson, const FString& SourcePath)
{
	return FString::Printf(TEXT("character_%08x"), FCrc::StrCrc32(*(SourcePath + TEXT("\n") + RawJson)));
}

FString FNarrativeContentLibrary::ReplaceMacros(const FString& Source, const FString& CharacterName,
	const FString& UserName)
{
	FString Result = Source;
	const FString Character = CharacterName.IsEmpty() ? FString(TEXT("角色")) : CharacterName;
	const FString User = UserName.IsEmpty() ? FString(TEXT("玩家")) : UserName;
	const TArray<FString> CharacterTokens = {
		FString(TEXT("{{char}}")), FString(TEXT("{{CHAR}}")), FString(TEXT("<char>")),
		FString(TEXT("<CHAR>")), FString(TEXT("<bot>")), FString(TEXT("<BOT>"))};
	for (const FString& Token : CharacterTokens)
		Result.ReplaceInline(*Token, *Character, ESearchCase::IgnoreCase);
	const TArray<FString> UserTokens = {
		FString(TEXT("{{user}}")), FString(TEXT("{{USER}}")), FString(TEXT("<user>")), FString(TEXT("<USER>"))};
	for (const FString& Token : UserTokens)
		Result.ReplaceInline(*Token, *User, ESearchCase::IgnoreCase);
	return Result;
}

FString FNarrativeContentLibrary::JsonObjectToString(const TSharedPtr<FJsonObject>& Object)
{
	return ObjectToString(Object);
}

bool FNarrativeContentLibrary::ParseWorldBookJson(const FString& Json,
	FNarrativeWorldBookAsset& OutAsset, FString& OutError, const FString& SourcePath)
{
	OutAsset = FNarrativeWorldBookAsset();
	OutAsset.RawJson = Json;
	OutAsset.NormalizedJson = Json;
	OutAsset.SourcePath = SourcePath;
	TSharedPtr<FJsonObject> Root = ParseObject(Json);
	if (!Root.IsValid())
	{
		OutError = TEXT("世界书不是有效 JSON 对象");
		return false;
	}
	OutAsset.Name = ReadString(Root, TEXT("name"), ReadString(Root, TEXT("world_name")));
	if (OutAsset.Name.IsEmpty()) OutAsset.Name = FileStem(SourcePath, TEXT("未命名世界书"));
	const TArray<TSharedPtr<FJsonValue>>* ArrayEntries = nullptr;
	if (Root->TryGetArrayField(TEXT("entries"), ArrayEntries) && ArrayEntries)
	{
		OutAsset.EntryCount = ArrayEntries->Num();
		for (const TSharedPtr<FJsonValue>& Value : *ArrayEntries)
		{
			const TSharedPtr<FJsonObject> Entry = Value.IsValid() ? Value->AsObject() : nullptr;
			bool bConstant = false;
			if (Entry.IsValid() && ReadString(Entry, TEXT("content")).IsEmpty() == false
				&& Entry->TryGetBoolField(TEXT("constant"), bConstant) && bConstant)
				++OutAsset.ConstantEntryCount;
		}
	}
	else
	{
		const TSharedPtr<FJsonObject>* ObjectEntries = nullptr;
		if (Root->TryGetObjectField(TEXT("entries"), ObjectEntries) && ObjectEntries && ObjectEntries->IsValid())
		{
			OutAsset.EntryCount = (*ObjectEntries)->Values.Num();
			for (const auto& Pair : (*ObjectEntries)->Values)
			{
				const TSharedPtr<FJsonObject> Entry = Pair.Value.IsValid() ? Pair.Value->AsObject() : nullptr;
				bool bConstant = false;
				if (Entry.IsValid() && Entry->TryGetBoolField(TEXT("constant"), bConstant) && bConstant)
					++OutAsset.ConstantEntryCount;
			}
		}
	}
	if (OutAsset.EntryCount == 0 && ReadString(Root, TEXT("setting")).IsEmpty()
		&& ReadString(Root, TEXT("description")).IsEmpty())
	{
		OutAsset.bHasWarnings = true;
		OutAsset.Diagnostic = TEXT("世界书没有 entries、setting 或 description；仍可作为空世界书导入");
	}
	OutAsset.Id = MakeWorldBookId(Json, SourcePath);
	OutAsset.bValid = true;
	if (OutAsset.Diagnostic.IsEmpty())
		OutAsset.Diagnostic = FString::Printf(TEXT("已解析 %d 个世界书词条，其中常驻 %d 个"),
			OutAsset.EntryCount, OutAsset.ConstantEntryCount);
	return true;
}

bool FNarrativeContentLibrary::SaveWorldBookAsset(const FNarrativeWorldBookAsset& Asset,
	FString& OutError)
{
	if (!Asset.bValid || !IsSafeStoredId(Asset.Id))
	{
		OutError = TEXT("世界书资产 ID 无效");
		return false;
	}
	IFileManager::Get().MakeDirectory(*GetWorldBooksDirectory(), true);
	const FString Base = GetWorldBooksDirectory() / Asset.Id;
	if (!FFileHelper::SaveStringToFile(Asset.RawJson, *(Base + TEXT(".source.json"))))
	{
		OutError = TEXT("无法保存世界书原始文件");
		return false;
	}
	if (!FFileHelper::SaveStringToFile(Asset.RawJson, *(Base + TEXT(".json"))))
	{
		OutError = TEXT("无法保存世界书运行文件");
		return false;
	}
	return true;
}

bool FNarrativeContentLibrary::ImportWorldBook(const FString& SourcePath,
	FNarrativeWorldBookAsset& OutAsset, FString& OutError)
{
	FString Json;
	if (!FFileHelper::LoadFileToString(Json, *SourcePath))
	{
		OutError = FString::Printf(TEXT("无法读取世界书：%s"), *SourcePath);
		return false;
	}
	return ImportWorldBookText(Json, SourcePath, OutAsset, OutError);
}

bool FNarrativeContentLibrary::ImportWorldBookText(const FString& Json, const FString& SourcePath,
	FNarrativeWorldBookAsset& OutAsset, FString& OutError)
{
	if (!ParseWorldBookJson(Json, OutAsset, OutError, SourcePath)) return false;
	return SaveWorldBookAsset(OutAsset, OutError);
}

bool FNarrativeContentLibrary::LoadWorldBook(const FString& Id,
	FNarrativeWorldBookAsset& OutAsset, FString& OutError)
{
	OutAsset = FNarrativeWorldBookAsset();
	if (!IsSafeStoredId(Id))
	{
		OutError = TEXT("世界书 ID 无效");
		return false;
	}
	const FString Path = GetWorldBooksDirectory() / (Id + TEXT(".json"));
	FString Json;
	if (!FFileHelper::LoadFileToString(Json, *Path))
	{
		OutError = FString::Printf(TEXT("找不到世界书资产：%s"), *Id);
		return false;
	}
	return ParseWorldBookJson(Json, OutAsset, OutError, Path);
}

void FNarrativeContentLibrary::ListWorldBooks(TArray<FNarrativeWorldBookAsset>& OutAssets)
{
	OutAssets.Reset();
	TArray<FString> Files;
	IFileManager::Get().FindFiles(Files, *(GetWorldBooksDirectory() / TEXT("*.json")), true, false);
	for (const FString& Filename : Files)
	{
		if (Filename.EndsWith(TEXT(".source.json"))) continue;
		const FString Id = FPaths::GetBaseFilename(Filename);
		FNarrativeWorldBookAsset Asset;
		FString Error;
		if (LoadWorldBook(Id, Asset, Error)) OutAssets.Add(MoveTemp(Asset));
	}
	OutAssets.Sort([](const FNarrativeWorldBookAsset& A, const FNarrativeWorldBookAsset& B)
		{ return A.Name < B.Name; });
}

bool FNarrativeContentLibrary::ParseCharacterCardObject(const TSharedPtr<FJsonObject>& Root,
	const FString& RawJson, const FString& SourcePath, FNarrativeCharacterCardAsset& OutAsset,
	FString& OutError, const FString& UserName)
{
	OutAsset = FNarrativeCharacterCardAsset();
	if (!Root.IsValid())
	{
		OutError = TEXT("角色卡不是有效 JSON 对象");
		return false;
	}
	const TSharedPtr<FJsonObject>* DataPointer = nullptr;
	const TSharedPtr<FJsonObject> Data = Root->TryGetObjectField(TEXT("data"), DataPointer)
		&& DataPointer && DataPointer->IsValid() ? *DataPointer : Root;
	const FString Spec = ReadString(Root, TEXT("spec"));
	OutAsset.SourceFormat = Spec.Equals(TEXT("chara_card_v3"), ESearchCase::IgnoreCase) ? TEXT("v3")
		: Spec.Equals(TEXT("chara_card_v2"), ESearchCase::IgnoreCase) || Data != Root ? TEXT("v2") : TEXT("v1");
	OutAsset.Name = ReadString(Data, TEXT("name"), ReadString(Root, TEXT("name"))).TrimStartAndEnd();
	if (OutAsset.Name.IsEmpty())
	{
		OutError = TEXT("角色卡缺少 name");
		return false;
	}
	OutAsset.Description = ReplaceMacros(ReadString(Data, TEXT("description"), ReadString(Root, TEXT("description"))), OutAsset.Name, UserName);
	OutAsset.Personality = ReplaceMacros(ReadString(Data, TEXT("personality"), ReadString(Root, TEXT("personality"))), OutAsset.Name, UserName);
	OutAsset.Scenario = ReplaceMacros(ReadString(Data, TEXT("scenario"), ReadString(Root, TEXT("scenario"))), OutAsset.Name, UserName);
	OutAsset.FirstMessage = ReplaceMacros(ReadString(Data, TEXT("first_mes"), ReadString(Root, TEXT("first_mes"))), OutAsset.Name, UserName);
	OutAsset.MessageExamples = ReplaceMacros(ReadString(Data, TEXT("mes_example"), ReadString(Root, TEXT("mes_example"))), OutAsset.Name, UserName);
	OutAsset.SystemPrompt = ReplaceMacros(ReadString(Data, TEXT("system_prompt"), ReadString(Root, TEXT("system_prompt"))), OutAsset.Name, UserName);
	OutAsset.PostHistoryInstructions = ReplaceMacros(ReadString(Data, TEXT("post_history_instructions"), ReadString(Root, TEXT("post_history_instructions"))), OutAsset.Name, UserName);
	OutAsset.Avatar = ReadString(Data, TEXT("avatar"), ReadString(Root, TEXT("avatar")));
	ReadStringArray(Data, TEXT("alternate_greetings"), OutAsset.AlternateGreetings);
	if (OutAsset.AlternateGreetings.Num() == 0) ReadStringArray(Data, TEXT("alternateGreetings"), OutAsset.AlternateGreetings);
	for (FString& Greeting : OutAsset.AlternateGreetings)
		Greeting = ReplaceMacros(Greeting, OutAsset.Name, UserName);
	const TSharedPtr<FJsonObject>* BookPointer = nullptr;
	TSharedPtr<FJsonObject> Book = Data->TryGetObjectField(TEXT("character_book"), BookPointer)
		&& BookPointer && BookPointer->IsValid() ? *BookPointer : nullptr;
	if (!Book.IsValid())
	{
		BookPointer = nullptr;
		if (Data->TryGetObjectField(TEXT("characterBook"), BookPointer) && BookPointer && BookPointer->IsValid())
			Book = *BookPointer;
	}
	if (!Book.IsValid())
	{
		BookPointer = nullptr;
		if (Root->TryGetObjectField(TEXT("character_book"), BookPointer) && BookPointer && BookPointer->IsValid())
			Book = *BookPointer;
	}
	if (Book.IsValid())
	{
		const TSharedPtr<FJsonObject> MutableBook = MakeShared<FJsonObject>();
		for (const auto& Pair : Book->Values) MutableBook->SetField(Pair.Key, Pair.Value);
		ReplaceObjectStrings(MutableBook, OutAsset.Name, UserName);
		OutAsset.CharacterBookJson = ObjectToString(MutableBook);
		const TArray<TSharedPtr<FJsonValue>>* Entries = nullptr;
		OutAsset.bHasEmbeddedWorldBook = MutableBook->TryGetArrayField(TEXT("entries"), Entries) && Entries && Entries->Num() > 0;
	}
	OutAsset.RawJson = RawJson;
	OutAsset.NormalizedJson = ObjectToString(Root);
	OutAsset.SourcePath = SourcePath;
	OutAsset.Id = MakeCharacterCardId(RawJson, SourcePath);
	OutAsset.bHasEmbeddedAvatar = !OutAsset.Avatar.IsEmpty();
	OutAsset.bHasWarnings = OutAsset.FirstMessage.IsEmpty();
	if (OutAsset.FirstMessage.IsEmpty()) OutAsset.Diagnostic = TEXT("角色卡没有 first_mes；仍可作为角色定义使用");
	OutAsset.RegistryJson = BuildRegistryJson(OutAsset);
	OutAsset.bValid = true;
	if (OutAsset.Diagnostic.IsEmpty())
		OutAsset.Diagnostic = FString::Printf(TEXT("已解析 SillyTavern V%s 角色卡：%s"),
			*OutAsset.SourceFormat, *OutAsset.Name);
	return true;
}

bool FNarrativeContentLibrary::ParseCharacterCardJson(const FString& Json,
	FNarrativeCharacterCardAsset& OutAsset, FString& OutError, const FString& SourcePath,
	const FString& UserName)
{
	return ParseCharacterCardObject(ParseObject(Json), Json, SourcePath, OutAsset, OutError, UserName);
}

FString FNarrativeContentLibrary::BuildRegistryJson(const FNarrativeCharacterCardAsset& Asset)
{
	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> Characters;
	TSharedPtr<FJsonObject> Character = MakeShared<FJsonObject>();
	Character->SetStringField(TEXT("id"), Asset.Id);
	Character->SetStringField(TEXT("name"), Asset.Name);
	Character->SetStringField(TEXT("art"), Asset.Avatar);
	Character->SetStringField(TEXT("description"), Asset.Description.Left(12000));
	Character->SetStringField(TEXT("personality"), Asset.Personality.Left(12000));
	Character->SetStringField(TEXT("scenario"), Asset.Scenario.Left(12000));
	Character->SetStringField(TEXT("system_prompt"), Asset.SystemPrompt.Left(12000));
	Character->SetStringField(TEXT("post_history_instructions"), Asset.PostHistoryInstructions.Left(12000));
	TArray<TSharedPtr<FJsonValue>> Aliases;
	Aliases.Add(MakeShared<FJsonValueString>(Asset.Name));
	Character->SetArrayField(TEXT("aliases"), Aliases);
	Characters.Add(MakeShared<FJsonValueObject>(Character));
	Root->SetArrayField(TEXT("characters"), Characters);
	return ObjectToString(Root);
}

bool FNarrativeContentLibrary::SaveCharacterCardAsset(const FNarrativeCharacterCardAsset& Asset,
	const TArray<uint8>* OptionalSourceBytes, FString& OutError)
{
	if (!Asset.bValid || !IsSafeStoredId(Asset.Id))
	{
		OutError = TEXT("角色卡资产 ID 无效");
		return false;
	}
	IFileManager::Get().MakeDirectory(*GetCharacterCardsDirectory(), true);
	const FString Base = GetCharacterCardsDirectory() / Asset.Id;
	if (!FFileHelper::SaveStringToFile(Asset.RawJson, *(Base + TEXT(".source.json"))))
	{
		OutError = TEXT("无法保存角色卡原始 JSON");
		return false;
	}
	if (!FFileHelper::SaveStringToFile(Asset.RawJson, *(Base + TEXT(".json"))))
	{
		OutError = TEXT("无法保存角色卡运行文件");
		return false;
	}
	if (OptionalSourceBytes && OptionalSourceBytes->Num() > 0
		&& !FFileHelper::SaveArrayToFile(*OptionalSourceBytes, *(Base + TEXT(".source.png"))))
	{
		OutError = TEXT("无法保存角色卡 PNG 原始文件");
		return false;
	}
	return true;
}

bool FNarrativeContentLibrary::ImportCharacterCardJsonText(const FString& Json,
	const FString& SourcePath, FNarrativeCharacterCardAsset& OutAsset, FString& OutError,
	const FString& UserName)
{
	if (!ParseCharacterCardJson(Json, OutAsset, OutError, SourcePath, UserName)) return false;
	return SaveCharacterCardAsset(OutAsset, nullptr, OutError);
}

bool FNarrativeContentLibrary::ImportCharacterCardPng(const FString& SourcePath,
	FNarrativeCharacterCardAsset& OutAsset, FString& OutError, const FString& UserName)
{
	TArray<uint8> Bytes;
	if (!FFileHelper::LoadFileToArray(Bytes, *SourcePath))
	{
		OutError = FString::Printf(TEXT("无法读取角色卡 PNG：%s"), *SourcePath);
		return false;
	}
	FString Json;
	if (!ExtractCharacterCardJsonFromPng(Bytes, Json, OutError)) return false;
	if (!ParseCharacterCardJson(Json, OutAsset, OutError, SourcePath, UserName)) return false;
	if (!SaveCharacterCardAsset(OutAsset, &Bytes, OutError)) return false;
	OutAsset.Avatar = GetCharacterCardsDirectory() / (OutAsset.Id + TEXT(".source.png"));
	OutAsset.bHasEmbeddedAvatar = true;
	OutAsset.RegistryJson = BuildRegistryJson(OutAsset);
	return true;
}

bool FNarrativeContentLibrary::ImportCharacterCard(const FString& SourcePath,
	FNarrativeCharacterCardAsset& OutAsset, FString& OutError, const FString& UserName)
{
	if (SourcePath.EndsWith(TEXT(".png"), ESearchCase::IgnoreCase))
		return ImportCharacterCardPng(SourcePath, OutAsset, OutError, UserName);
	FString Json;
	if (!FFileHelper::LoadFileToString(Json, *SourcePath))
	{
		OutError = FString::Printf(TEXT("无法读取角色卡：%s"), *SourcePath);
		return false;
	}
	return ImportCharacterCardJsonText(Json, SourcePath, OutAsset, OutError, UserName);
}

bool FNarrativeContentLibrary::ReadStoredCharacterCard(const FString& Path,
	FNarrativeCharacterCardAsset& OutAsset, FString& OutError, const FString& UserName)
{
	FString Json;
	if (!FFileHelper::LoadFileToString(Json, *Path))
	{
		OutError = TEXT("无法读取角色卡运行文件");
		return false;
	}
	if (!ParseCharacterCardJson(Json, OutAsset, OutError, Path, UserName)) return false;
	const FString Id = FPaths::GetBaseFilename(Path);
	OutAsset.Id = Id;
	const FString PngPath = GetCharacterCardsDirectory() / (Id + TEXT(".source.png"));
	if (FPaths::FileExists(PngPath))
	{
		OutAsset.Avatar = PngPath;
		OutAsset.bHasEmbeddedAvatar = true;
		OutAsset.RegistryJson = BuildRegistryJson(OutAsset);
	}
	return true;
}

bool FNarrativeContentLibrary::LoadCharacterCard(const FString& Id,
	FNarrativeCharacterCardAsset& OutAsset, FString& OutError, const FString& UserName)
{
	OutAsset = FNarrativeCharacterCardAsset();
	if (!IsSafeStoredId(Id))
	{
		OutError = TEXT("角色卡 ID 无效");
		return false;
	}
	return ReadStoredCharacterCard(GetCharacterCardsDirectory() / (Id + TEXT(".json")), OutAsset, OutError, UserName);
}

void FNarrativeContentLibrary::ListCharacterCards(TArray<FNarrativeCharacterCardAsset>& OutAssets,
	const FString& UserName)
{
	OutAssets.Reset();
	TArray<FString> Files;
	IFileManager::Get().FindFiles(Files, *(GetCharacterCardsDirectory() / TEXT("*.json")), true, false);
	for (const FString& Filename : Files)
	{
		if (Filename.EndsWith(TEXT(".source.json"))) continue;
		const FString Id = FPaths::GetBaseFilename(Filename);
		FNarrativeCharacterCardAsset Asset;
		FString Error;
		if (LoadCharacterCard(Id, Asset, Error, UserName)) OutAssets.Add(MoveTemp(Asset));
	}
	OutAssets.Sort([](const FNarrativeCharacterCardAsset& A, const FNarrativeCharacterCardAsset& B)
		{ return A.Name < B.Name; });
}

bool FNarrativeContentLibrary::ExtractCharacterCardJsonFromPng(const TArray<uint8>& PngBytes,
	FString& OutJson, FString& OutError)
{
	OutJson.Reset();
	OutError.Reset();
	static const uint8 Signature[] = {137, 80, 78, 71, 13, 10, 26, 10};
	if (PngBytes.Num() < 8 || FMemory::Memcmp(PngBytes.GetData(), Signature, 8) != 0)
	{
		OutError = TEXT("文件不是有效 PNG");
		return false;
	}
	FString Chara;
	FString Ccv3;
	int32 Offset = 8;
	while (Offset + 12 <= PngBytes.Num())
	{
		const uint32 Length = ReadBigEndian32(PngBytes, Offset);
		const int64 ChunkEnd = static_cast<int64>(Offset) + 12 + Length;
		if (Length > 64 * 1024 * 1024 || ChunkEnd > PngBytes.Num())
		{
			OutError = TEXT("角色卡 PNG 元数据损坏或过大");
			return false;
		}
		FString Type;
		for (int32 Index = 0; Index < 4; ++Index) Type.AppendChar(static_cast<TCHAR>(PngBytes[Offset + 4 + Index]));
		if (Type == TEXT("tEXt"))
		{
			FString Text;
			for (uint32 Index = 0; Index < Length; ++Index)
				Text.AppendChar(static_cast<TCHAR>(PngBytes[Offset + 8 + static_cast<int32>(Index)]));
			int32 Separator = INDEX_NONE;
			if (Text.FindChar(TEXT('\0'), Separator))
			{
				const FString Key = Text.Left(Separator);
				const FString Value = Text.Mid(Separator + 1);
				if (Key.Equals(TEXT("ccv3"), ESearchCase::IgnoreCase)) Ccv3 = Value;
				else if (Key.Equals(TEXT("chara"), ESearchCase::IgnoreCase)) Chara = Value;
			}
		}
		else if (Type == TEXT("iTXt"))
		{
			// Uncompressed iTXt is emitted by some modern card exporters.  Compressed
			// iTXt/zTXt is left untouched and reported as a warning by the caller rather
			// than guessed with an unrelated decompressor.
			const int32 DataStart = Offset + 8;
			int32 Cursor = DataStart;
			int32 KeywordEnd = INDEX_NONE;
			for (; Cursor < DataStart + static_cast<int32>(Length); ++Cursor)
				if (PngBytes[Cursor] == 0) { KeywordEnd = Cursor; break; }
			if (KeywordEnd > DataStart && KeywordEnd + 2 < DataStart + static_cast<int32>(Length))
			{
				FString Key;
				for (int32 Index = DataStart; Index < KeywordEnd; ++Index) Key.AppendChar(static_cast<TCHAR>(PngBytes[Index]));
				const uint8 CompressionFlag = PngBytes[KeywordEnd + 1];
				int32 TextStart = KeywordEnd + 3;
				for (int32 Nulls = 0; Nulls < 2 && TextStart < DataStart + static_cast<int32>(Length); ++Nulls)
				{
					while (TextStart < DataStart + static_cast<int32>(Length) && PngBytes[TextStart] != 0) ++TextStart;
					++TextStart;
				}
				if (CompressionFlag == 0 && TextStart <= DataStart + static_cast<int32>(Length))
				{
					FString Value;
					for (int32 Index = TextStart; Index < DataStart + static_cast<int32>(Length); ++Index)
						Value.AppendChar(static_cast<TCHAR>(PngBytes[Index]));
					if (Key.Equals(TEXT("ccv3"), ESearchCase::IgnoreCase)) Ccv3 = Value;
					else if (Key.Equals(TEXT("chara"), ESearchCase::IgnoreCase)) Chara = Value;
				}
			}
		}
		Offset = static_cast<int32>(ChunkEnd);
		if (Type == TEXT("IEND")) break;
	}
	for (const FString& Candidate : {Ccv3, Chara})
	{
		const FString Decoded = DecodeMetadataValue(Candidate);
		if (Decoded.IsEmpty()) continue;
		if (ParseObject(Decoded).IsValid())
		{
			OutJson = Decoded;
			return true;
		}
	}
	OutError = TEXT("PNG 中缺少可解析的 chara/ccv3 角色卡元数据");
	return false;
}
