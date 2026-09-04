#pragma once

#include "CoreMinimal.h"
#include "Templates/Function.h"

/**
 * Runs deterministic, request-free checks for the initial relic roll and its
 * in-memory pending-opening lock. The callback owns result accounting so this
 * helper can be called from the existing commandlet without another framework.
 */
using FOpeningRelicRandomRegressionCheck = TFunctionRef<void(bool, const FString&)>;

ASCENDSPIRE_API int32 RunOpeningRelicRandomRegressionTests(
	FOpeningRelicRandomRegressionCheck Check);
