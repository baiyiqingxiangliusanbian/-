#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "CombatSimCommandlet.generated.h"

/**
 * 战斗模拟 Commandlet —— 无头跑战斗，验证引擎与数值平衡
 * 用法: UnrealEditor-Cmd AscendSpire.uproject -run=AscendCombatSim -runs=10 [-enemy=mountain_imp] [-relics=a+b] [-pills=x+y] [-enemies=a+b] -unattended -nullrhi -stdout
 */
UCLASS()
class UAscendCombatSimCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	virtual int32 Main(const FString& Params) override;
};
