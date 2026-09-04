#include "PrivateReasoningPrefillLoader.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Containers/StringConv.h"

namespace
{
	bool IsSensitiveFieldSegment(const FString& Segment)
	{
		FString Lower = Segment;
		Lower.ToLowerInline();
		static const TCHAR* const Forbidden[] = {
			TEXT("api"), TEXT("key"), TEXT("token"), TEXT("password"), TEXT("secret"),
			TEXT("auth"), TEXT("header"), TEXT("cookie"), TEXT("url"), TEXT("endpoint"),
			TEXT("model"), TEXT("proxy")
		};
		for (const TCHAR* Word : Forbidden)
			if (Lower.Contains(Word)) return true;
		return false;
	}

	bool ExtractJsonField(const FString& JsonText, const FString& FieldPath,
		FString& OutPayload, int32& OutUtf8Bytes)
	{
		OutPayload.Reset();
		OutUtf8Bytes = 0;
		TSharedPtr<FJsonObject> Current;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
		if (!FJsonSerializer::Deserialize(Reader, Current) || !Current.IsValid()) return false;
		TArray<FString> Segments;
		FieldPath.ParseIntoArray(Segments, TEXT("."), true);
		for (int32 Index = 0; Index < Segments.Num() - 1; ++Index)
		{
			const TSharedPtr<FJsonObject>* Next = nullptr;
			if (!Current->TryGetObjectField(Segments[Index], Next) || !Next || !Next->IsValid()) return false;
			Current = *Next;
		}
		FString Payload;
		if (!Current->TryGetStringField(Segments.Last(), Payload)) return false;
		if (Payload.TrimStartAndEnd().IsEmpty() || Payload.Len() > 262144) return false;
		OutUtf8Bytes = FTCHARToUTF8(*Payload).Length();
		if (OutUtf8Bytes <= 0) return false;
		OutPayload = MoveTemp(Payload);
		return true;
	}
}

bool FPrivateReasoningPrefillLoader::IsAllowedFieldPath(const FString& FieldPath)
{
	TArray<FString> Segments;
	FieldPath.ParseIntoArray(Segments, TEXT("."), true);
	if (Segments.Num() == 0 || Segments.Num() > 12) return false;
	for (const FString& Segment : Segments)
	{
		if (Segment.IsEmpty() || Segment.Len() > 96 || IsSensitiveFieldSegment(Segment)) return false;
	}
	const FString& Leaf = Segments.Last();
	FString LowerLeaf = Leaf;
	LowerLeaf.ToLowerInline();
	return LowerLeaf.Contains(TEXT("reason")) || LowerLeaf.Contains(TEXT("prefill"))
		|| LowerLeaf.Contains(TEXT("think")) || LowerLeaf.Contains(TEXT("chain"))
		|| LowerLeaf.Contains(TEXT("cot"));
}

bool FPrivateReasoningPrefillLoader::LoadJsonField(const FString& SourcePath,
	const FString& FieldPath, FString& OutPayload, int32& OutUtf8Bytes)
{
	OutPayload.Reset();
	OutUtf8Bytes = 0;
	if (SourcePath.TrimStartAndEnd().IsEmpty() || !IsAllowedFieldPath(FieldPath)) return false;

	FString JsonText;
	if (!FFileHelper::LoadFileToString(JsonText, *SourcePath)) return false;
	return ExtractJsonField(JsonText, FieldPath, OutPayload, OutUtf8Bytes);
}

bool FPrivateReasoningPrefillLoader::ExtractJsonFieldForAutomationTest(const FString& JsonText,
	const FString& FieldPath, FString& OutPayload, int32& OutUtf8Bytes)
{
	OutPayload.Reset();
	OutUtf8Bytes = 0;
	if (!IsAllowedFieldPath(FieldPath)) return false;
	return ExtractJsonField(JsonText, FieldPath, OutPayload, OutUtf8Bytes);
}
