#pragma once

#include "CoreMinimal.h"
#include "Templates/Function.h"

/**
 * Runs deterministic, request-free checks for the user-authored story-direction
 * prompt path. The callback owns result accounting so this helper can be called
 * from an existing commandlet without introducing a second test framework.
 *
 * Return value is the number of checks emitted through Check. No settings file,
 * save, network request, UObject world, or private prefill resource is touched.
 */
using FNarrativeGuidanceRegressionCheck = TFunctionRef<void(bool, const FString&)>;

ASCENDSPIRE_API int32 RunNarrativeGuidanceRegressionTests(
	FNarrativeGuidanceRegressionCheck Check);
