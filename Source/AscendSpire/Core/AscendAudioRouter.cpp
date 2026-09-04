#include "AscendAudioRouter.h"

#include "Components/AudioComponent.h"
#include "Engine/Engine.h"
#include "GameFramework/Actor.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundWave.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	constexpr float MinimumAudibleSetting = 0.001f;
	constexpr float SilenceDb = -60.f;

	FString SanitizeAssetName(FString Value)
	{
		Value.ReplaceInline(TEXT("/"), TEXT("_"));
		Value.ReplaceInline(TEXT("\\"), TEXT("_"));
		Value.ReplaceInline(TEXT("."), TEXT("_"));
		Value.ReplaceInline(TEXT(" "), TEXT("_"));
		return Value;
	}
}

UAscendAudioRouter::UAscendAudioRouter()
{
#define ADD_BUNDLED_AUDIO(Path) \
	{ \
		ConstructorHelpers::FObjectFinder<USoundBase> AssetFinder(Path); \
		if (AssetFinder.Succeeded()) BundledAudioAssets.Add(AssetFinder.Object); \
	}
    ADD_BUNDLED_AUDIO(TEXT("/Game/Audio/SFX/block.block"));
    ADD_BUNDLED_AUDIO(TEXT("/Game/Audio/SFX/breakthrough.breakthrough"));
    ADD_BUNDLED_AUDIO(TEXT("/Game/Audio/SFX/card_drag.card_drag"));
    ADD_BUNDLED_AUDIO(TEXT("/Game/Audio/SFX/card_invalid.card_invalid"));
    ADD_BUNDLED_AUDIO(TEXT("/Game/Audio/SFX/card_pick.card_pick"));
    ADD_BUNDLED_AUDIO(TEXT("/Game/Audio/SFX/card_play.card_play"));
    ADD_BUNDLED_AUDIO(TEXT("/Game/Audio/SFX/combat_enter.combat_enter"));
    ADD_BUNDLED_AUDIO(TEXT("/Game/Audio/SFX/curse_whisper.curse_whisper"));
    ADD_BUNDLED_AUDIO(TEXT("/Game/Audio/SFX/defeat_stinger.defeat_stinger"));
    ADD_BUNDLED_AUDIO(TEXT("/Game/Audio/SFX/discard.discard"));
    ADD_BUNDLED_AUDIO(TEXT("/Game/Audio/SFX/draw.draw"));
    ADD_BUNDLED_AUDIO(TEXT("/Game/Audio/SFX/end_turn.end_turn"));
    ADD_BUNDLED_AUDIO(TEXT("/Game/Audio/SFX/enemy_death.enemy_death"));
    ADD_BUNDLED_AUDIO(TEXT("/Game/Audio/SFX/enemy_turn.enemy_turn"));
    ADD_BUNDLED_AUDIO(TEXT("/Game/Audio/SFX/fire_burst.fire_burst"));
    ADD_BUNDLED_AUDIO(TEXT("/Game/Audio/SFX/forge_start.forge_start"));
    ADD_BUNDLED_AUDIO(TEXT("/Game/Audio/SFX/forge_success.forge_success"));
    ADD_BUNDLED_AUDIO(TEXT("/Game/Audio/SFX/gold.gold"));
    ADD_BUNDLED_AUDIO(TEXT("/Game/Audio/SFX/heal_chime.heal_chime"));
    ADD_BUNDLED_AUDIO(TEXT("/Game/Audio/SFX/impact_hit.impact_hit"));
    ADD_BUNDLED_AUDIO(TEXT("/Game/Audio/SFX/map_node.map_node"));
    ADD_BUNDLED_AUDIO(TEXT("/Game/Audio/SFX/player_hit.player_hit"));
    ADD_BUNDLED_AUDIO(TEXT("/Game/Audio/SFX/poison_hiss.poison_hiss"));
    ADD_BUNDLED_AUDIO(TEXT("/Game/Audio/SFX/potion.potion"));
    ADD_BUNDLED_AUDIO(TEXT("/Game/Audio/SFX/power_surge.power_surge"));
    ADD_BUNDLED_AUDIO(TEXT("/Game/Audio/SFX/reshuffle.reshuffle"));
    ADD_BUNDLED_AUDIO(TEXT("/Game/Audio/SFX/reward.reward"));
    ADD_BUNDLED_AUDIO(TEXT("/Game/Audio/SFX/seal_stamp.seal_stamp"));
    ADD_BUNDLED_AUDIO(TEXT("/Game/Audio/SFX/spirit_chime.spirit_chime"));
    ADD_BUNDLED_AUDIO(TEXT("/Game/Audio/SFX/sword_flurry.sword_flurry"));
    ADD_BUNDLED_AUDIO(TEXT("/Game/Audio/SFX/sword_heavy.sword_heavy"));
    ADD_BUNDLED_AUDIO(TEXT("/Game/Audio/SFX/sword_slash.sword_slash"));
    ADD_BUNDLED_AUDIO(TEXT("/Game/Audio/SFX/sword_wave.sword_wave"));
    ADD_BUNDLED_AUDIO(TEXT("/Game/Audio/SFX/talisman_cast.talisman_cast"));
    ADD_BUNDLED_AUDIO(TEXT("/Game/Audio/SFX/target_lock.target_lock"));
    ADD_BUNDLED_AUDIO(TEXT("/Game/Audio/SFX/thunder_crack.thunder_crack"));
    ADD_BUNDLED_AUDIO(TEXT("/Game/Audio/SFX/ui_back.ui_back"));
    ADD_BUNDLED_AUDIO(TEXT("/Game/Audio/SFX/ui_confirm.ui_confirm"));
    ADD_BUNDLED_AUDIO(TEXT("/Game/Audio/SFX/ui_deny.ui_deny"));
    ADD_BUNDLED_AUDIO(TEXT("/Game/Audio/SFX/ui_hover.ui_hover"));
    ADD_BUNDLED_AUDIO(TEXT("/Game/Audio/SFX/victory_stinger.victory_stinger"));
    ADD_BUNDLED_AUDIO(TEXT("/Game/Audio/SFX/ward_raise.ward_raise"));
    ADD_BUNDLED_AUDIO(TEXT("/Game/Audio/Music/bgm_combat.bgm_combat"));
    ADD_BUNDLED_AUDIO(TEXT("/Game/Audio/Music/bgm_combat_boss.bgm_combat_boss"));
    ADD_BUNDLED_AUDIO(TEXT("/Game/Audio/Music/bgm_combat_elite.bgm_combat_elite"));
    ADD_BUNDLED_AUDIO(TEXT("/Game/Audio/Music/bgm_map.bgm_map"));
    ADD_BUNDLED_AUDIO(TEXT("/Game/Audio/Music/bgm_narrative.bgm_narrative"));
    ADD_BUNDLED_AUDIO(TEXT("/Game/Audio/Music/bgm_rest.bgm_rest"));
    ADD_BUNDLED_AUDIO(TEXT("/Game/Audio/Music/bgm_shop.bgm_shop"));
    ADD_BUNDLED_AUDIO(TEXT("/Game/Audio/Music/bgm_title.bgm_title"));
#undef ADD_BUNDLED_AUDIO
}

void UAscendAudioRouter::Initialize(AActor* InOwner, float InSfxVolume, float InMusicVolume)
{
	Owner = InOwner;
	SetVolumes(InSfxVolume, InMusicVolume);
}

void UAscendAudioRouter::SetVolumes(float InSfxVolume, float InMusicVolume)
{
	SfxVolume = FMath::Clamp(InSfxVolume, 0.f, 1.f);
	MusicVolume = FMath::Clamp(InMusicVolume, 0.f, 1.f);
	if (MusicComponent)
	{
		MusicComponent->SetVolumeMultiplier(MusicSettingToGain(MusicVolume));
	}
}

bool UAscendAudioRouter::PlaySfx(const FString& EventId, float VolumeScale, float PitchMin, float PitchMax)
{
	if (!Owner || EventId.IsEmpty()) return false;
	const FString ResolvedEventId = ResolveSfxEventId(EventId);
	USoundBase* Sound = LoadSfx(ResolvedEventId);
	if (!Sound && ResolvedEventId != TEXT("impact_hit"))
	{
		// A future content author can add a new logical id without reintroducing
		// the old procedural/chiptune fallback or a per-event allocation spike.
		Sound = LoadSfx(TEXT("impact_hit"));
	}
	if (!Sound) return false;

	const double Now = FPlatformTime::Seconds();
	const float Interval = MinimumInterval(ResolvedEventId);
	if (const double* LastTime = LastSfxTimes.Find(ResolvedEventId))
	{
		if (Now - *LastTime < Interval)
		{
			// The event is still handled, but repeated UI/log bursts are coalesced.
			return true;
		}
	}
	LastSfxTimes.Add(ResolvedEventId, Now);

	if (SfxVolume <= MinimumAudibleSetting) return true;
	const float Pitch = FMath::FRandRange(FMath::Min(PitchMin, PitchMax), FMath::Max(PitchMin, PitchMax));
	const float EventGain = EventMixMultiplier(ResolvedEventId);
	UGameplayStatics::PlaySound2D(Owner, Sound,
		SettingToGain(SfxVolume) * EventGain * FMath::Max(0.f, VolumeScale), Pitch);
	return true;
}

void UAscendAudioRouter::SetMusicState(const FString& State, bool bSceneEntry, float FadeDuration)
{
	if (!Owner || State.IsEmpty()) return;
	if (State == CurrentMusicState && !bSceneEntry) return;

	// A scene transition must never leave the previous track audible underneath
	// the new one.  FadeOut used to leave the old persistent component alive for
	// the whole transition window, which was especially noticeable when the
	// title track carried into combat.  Stop and destroy the old component first;
	// the incoming track still fades in below for a soft handoff.
	if (MusicComponent)
	{
		const FString PreviousState = CurrentMusicState;
		MusicComponent->Stop();
		MusicComponent->DestroyComponent();
		MusicComponent = nullptr;
		UE_LOG(LogTemp, Display, TEXT("[AscendAudio] stopped previous music state=%s"), *PreviousState);
	}
	CurrentMusicState = State;

	const TArray<USoundBase*> Variants = LoadMusicVariants(State);
	if (Variants.Num() == 0)
	{
		UE_LOG(LogTemp, Verbose, TEXT("[AscendAudio] music asset missing state=%s"), *State);
		return;
	}

	USoundBase* Sound = Variants[0];
	if (!Sound) return;

	USoundWave* Wave = Cast<USoundWave>(Sound);
	if (Wave)
		Wave->bLooping = true;

	const float MusicGain = MusicSettingToGain(MusicVolume);
	MusicComponent = UGameplayStatics::CreateSound2D(Owner, Sound,
		1.f, 1.f, 0.f, nullptr, true, false);
	if (!MusicComponent) return;

	// Keep one component alive across level changes. The SoundWave itself owns the
	// loop flag; the persistent component prevents a short BGM asset from being
	// destroyed when its first pass reaches the end.
	MusicComponent->bAutoDestroy = false;
	MusicComponent->bIsUISound = true;
	MusicComponent->SetVolumeMultiplier(1.f);
	MusicComponent->FadeIn(FadeDuration, MusicGain, 0.f, EAudioFaderCurve::SCurve);
	CurrentMusicState = State;
	UE_LOG(LogTemp, Display, TEXT("[AscendAudio] music state=%s gain=%.3f looping=%d playing=%d"),
		*State, MusicGain, Wave && Wave->bLooping ? 1 : 0, MusicComponent->IsPlaying() ? 1 : 0);
}

void UAscendAudioRouter::StopMusic(float FadeDuration)
{
	if (MusicComponent)
	{
		MusicComponent->Stop();
		MusicComponent->DestroyComponent();
		MusicComponent = nullptr;
	}
	CurrentMusicState.Reset();
}

USoundBase* UAscendAudioRouter::LoadSfx(const FString& EventId)
{
	return LoadAsset(MakeSfxAssetPath(ResolveSfxEventId(EventId)), SfxCache);
}

USoundBase* UAscendAudioRouter::LoadMusic(const FString& State)
{
	return LoadAsset(MakeMusicAssetPath(State), MusicCache);
}

TArray<USoundBase*> UAscendAudioRouter::LoadMusicVariants(const FString& State)
{
	TArray<USoundBase*> Variants;
	if (State.IsEmpty()) return Variants;

	// Each logical state intentionally resolves to one complete track. Prefer the
	// designated scene track even when an older, more specific legacy asset still
	// exists (for example bgm_combat_elite/boss); only fall back to that legacy
	// state if the designated track is unavailable.
	const FString FallbackState = ResolveMusicFallbackState(State);
	const FString PreferredState = FallbackState.IsEmpty() ? State : FallbackState;
	if (USoundBase* Track = LoadMusic(PreferredState))
	{
		Variants.Add(Track);
		return Variants;
	}
	if (PreferredState != State)
	{
		if (USoundBase* LegacyTrack = LoadMusic(State))
		{
			Variants.Add(LegacyTrack);
			return Variants;
		}
	}
	return Variants;
}

USoundBase* UAscendAudioRouter::LoadAsset(const FString& AssetPath, TMap<FString, USoundBase*>& Cache)
{
	if (AssetPath.IsEmpty()) return nullptr;
	if (USoundBase** Existing = Cache.Find(AssetPath)) return *Existing;

	for (USoundBase* Bundled : BundledAudioAssets)
	{
		if (Bundled && Bundled->GetPathName() == AssetPath)
		{
			Cache.Add(AssetPath, Bundled);
			return Bundled;
		}
	}

	USoundBase* Sound = LoadObject<USoundBase>(nullptr, *AssetPath);
	if (Sound) Cache.Add(AssetPath, Sound);
	return Sound;
}

FString UAscendAudioRouter::MakeSfxAssetPath(const FString& EventId)
{
	const FString Name = SanitizeAssetName(EventId);
	return FString::Printf(TEXT("/Game/Audio/SFX/%s.%s"), *Name, *Name);
}

FString UAscendAudioRouter::ResolveSfxEventId(const FString& EventId)
{
	FString Name = SanitizeAssetName(EventId);
	Name.ToLowerInline();

	// Card visual metadata historically used animation ids or short aliases.
	// Keep those ids valid, but always point them at the authored WAV pack.
	if (Name == TEXT("ward")) return TEXT("ward_raise");
	if (Name == TEXT("heal")) return TEXT("heal_chime");
	if (Name == TEXT("spirit_flow")) return TEXT("spirit_chime");
	if (Name == TEXT("fireball") || Name == TEXT("flame_burst")) return TEXT("fire_burst");
	if (Name == TEXT("impact") || Name == TEXT("hit")) return TEXT("impact_hit");
	if (Name == TEXT("slash")) return TEXT("sword_slash");
	if (Name == TEXT("greatsword")) return TEXT("sword_heavy");
	if (Name == TEXT("myriad_swords")) return TEXT("sword_flurry");
	if (Name == TEXT("thunder")) return TEXT("thunder_crack");
	if (Name == TEXT("poison")) return TEXT("poison_hiss");
	if (Name == TEXT("power_aura")) return TEXT("power_surge");
	if (Name == TEXT("talisman")) return TEXT("talisman_cast");
	if (Name == TEXT("seal")) return TEXT("seal_stamp");
	if (Name == TEXT("curse_burst")) return TEXT("curse_whisper");
	return Name;
}

FString UAscendAudioRouter::MakeMusicAssetPath(const FString& State)
{
	const FString Name = SanitizeAssetName(State);
	return FString::Printf(TEXT("/Game/Audio/Music/%s.%s"), *Name, *Name);
}

FString UAscendAudioRouter::ResolveMusicFallbackState(const FString& State)
{
	if (State == TEXT("bgm_combat_elite") || State == TEXT("bgm_combat_boss")) return TEXT("bgm_combat");
	if (State == TEXT("bgm_event")) return TEXT("bgm_narrative");
	if (State == TEXT("bgm_reward")) return TEXT("bgm_title");
	if (State == TEXT("bgm_card_discovery")) return TEXT("bgm_narrative");
	if (State == TEXT("bgm_upgrade")) return TEXT("bgm_rest");
	if (State == TEXT("bgm_deckbuilding")) return TEXT("bgm_title");
	if (State == TEXT("bgm_victory")) return TEXT("bgm_title");
	if (State == TEXT("bgm_defeat")) return TEXT("bgm_rest");
	return FString();
}

float UAscendAudioRouter::SettingToGain(float Value)
{
	const float Clamped = FMath::Clamp(Value, 0.f, 1.f);
	if (Clamped <= MinimumAudibleSetting) return 0.f;
	const float Db = FMath::Lerp(SilenceDb, 0.f, Clamped);
	return FMath::Pow(10.f, Db / 20.f);
}

float UAscendAudioRouter::MusicSettingToGain(float Value)
{
	const float Clamped = FMath::Clamp(Value, 0.f, 1.f);
	if (Clamped <= MinimumAudibleSetting) return 0.f;
	// Keep music on a separate headroom tier.  The SFX path is already attenuated
	// for short transients; without this ceiling, a sustained BGM waveform masks
	// card/impact feedback even when both settings show the same percentage.
	constexpr float MusicHeadroom = 0.18f;
	return Clamped * Clamped * MusicHeadroom;
}

float UAscendAudioRouter::EventMixMultiplier(const FString& EventId)
{
	// These events are frequent and close to the listener. Keep their transient
	// below the combat/reward tier so selection feedback feels tactile, not sharp.
	if (EventId == TEXT("card_pick")) return 0.72f;
	if (EventId == TEXT("target_lock")) return 0.70f;
	if (EventId == TEXT("card_drag")) return 0.50f;
	return 1.f;
}

float UAscendAudioRouter::MinimumInterval(const FString& EventId)
{
	if (EventId.StartsWith(TEXT("ui_"))) return 0.045f;
	if (EventId == TEXT("draw") || EventId == TEXT("discard") || EventId == TEXT("reshuffle")) return 0.06f;
	if (EventId.Contains(TEXT("impact")) || EventId.Contains(TEXT("damage"))) return 0.018f;
	return 0.025f;
}
