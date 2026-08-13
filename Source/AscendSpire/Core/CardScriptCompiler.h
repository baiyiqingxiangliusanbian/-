#pragma once

#include "CoreMinimal.h"

class FJsonObject;

/** Lenient line-oriented source language compiled into the existing card runtime IR. */
class FCardScriptCompiler
{
public:
	static bool CompileToAuthoredObject(const FString& Source,
		TSharedPtr<FJsonObject>& OutCard, FString& OutError);
	static FString PromptReference();
};
