#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "AscendAudioRouter.generated.h"

class AActor;
class UAudioComponent;
class USoundBase;

/**
 * Central audio entry point for the game.
 *
 * Gameplay only sends stable logical event ids. The router owns asset lookup,
 * perceptual volume conversion, light variation and music state transitions.
 * Legacy card aliases are normalized here so gameplay never falls back to
 * runtime-generated placeholder tones. Missing assets use the shared impact
 * asset as a safe final fallback, keeping playback allocation-free.
 */
UCLASS()
class ASCENDSPIRE_API UAscendAudioRouter : public UObject
{
	GENERATED_BODY()

public:
	UAscendAudioRouter();
	void Initialize(AActor* InOwner, float InSfxVolume, float InMusicVolume);
	void SetVolumes(float InSfxVolume, float InMusicVolume);

	/** Returns true when the event was handled by the asset router. */
	bool PlaySfx(const FString& EventId, float VolumeScale = 1.f,
		float PitchMin = 0.97f, float PitchMax = 1.03f);

	/**
	 * Enters a logical music state. Each scene uses one complete authored track;
	 * redraws of the same scene keep the current song.
	 */
	void SetMusicState(const FString& State, bool bSceneEntry, float FadeDuration = 0.65f);
	void StopMusic(float FadeDuration = 0.4f);

	const FString& GetCurrentMusicState() const { return CurrentMusicState; }

private:
	USoundBase* LoadSfx(const FString& EventId);
	USoundBase* LoadMusic(const FString& State);
	TArray<USoundBase*> LoadMusicVariants(const FString& State);
	USoundBase* LoadAsset(const FString& AssetPath, TMap<FString, USoundBase*>& Cache);
	static FString MakeSfxAssetPath(const FString& EventId);
	static FString ResolveSfxEventId(const FString& EventId);
	static FString MakeMusicAssetPath(const FString& State);
	static FString ResolveMusicFallbackState(const FString& State);
	static float SettingToGain(float Value);
	static float MusicSettingToGain(float Value);
	static float EventMixMultiplier(const FString& EventId);
	static float MinimumInterval(const FString& EventId);

	UPROPERTY()
	AActor* Owner = nullptr;

	UPROPERTY()
	UAudioComponent* MusicComponent = nullptr;

	UPROPERTY()
	TMap<FString, USoundBase*> SfxCache;

	UPROPERTY()
	TMap<FString, USoundBase*> MusicCache;

	// These hard references keep dynamically addressed audio packages in cooked builds.
	UPROPERTY()
	TArray<TObjectPtr<USoundBase>> BundledAudioAssets;

	TMap<FString, double> LastSfxTimes;
	FString CurrentMusicState;
	float SfxVolume = 0.65f;
	float MusicVolume = 0.35f;
};
