#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "AscendDataCheckCommandlet.generated.h"

/**
 * 数据验证 Commandlet
 * 用法: UnrealEditor-Cmd AscendSpire.uproject -run=AscendDataCheck -unattended -nullrhi -stdout
 * 返回 0 = 全部数据有效, 1 = 有错误
 */
UCLASS()
class UAscendDataCheckCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	virtual int32 Main(const FString& Params) override;
};
