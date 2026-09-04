#pragma once

#include "CoreMinimal.h"

/**
 * Reads one explicitly configured, allow-listed string field as opaque data.
 *
 * The loader intentionally does not discover files, enumerate keys, print the
 * value, or interpret its contents.  A source path and dotted field path must
 * be supplied by the caller after the owning project has been identified.
 */
class FPrivateReasoningPrefillLoader
{
public:
	static bool LoadJsonField(const FString& SourcePath, const FString& FieldPath,
		FString& OutPayload, int32& OutUtf8Bytes);
	/** Synthetic/headless equivalent used by the commandlet; still returns opaque data only. */
	static bool ExtractJsonFieldForAutomationTest(const FString& JsonText, const FString& FieldPath,
		FString& OutPayload, int32& OutUtf8Bytes);

private:
	static bool IsAllowedFieldPath(const FString& FieldPath);
};
