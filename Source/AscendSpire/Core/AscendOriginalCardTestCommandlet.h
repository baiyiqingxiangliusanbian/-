#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "AscendOriginalCardTestCommandlet.generated.h"

/** End-to-end regression test for LLM-authored cards. Does not touch the player's save file. */
UCLASS()
class UAscendOriginalCardTestCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	virtual int32 Main(const FString& Params) override;
};
