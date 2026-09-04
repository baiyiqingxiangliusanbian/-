#pragma once

#include "Combat/CombatTypes.h"

/**
 * Central card-to-feedback routing.
 *
 * Static cards and runtime-authored cards both pass through this resolver, so a
 * card never falls back to the old placeholder simply because its JSON omitted
 * presentation metadata. Exact signature cards win first; the remaining cards
 * share a readable visual family based on their name, type and executable effects.
 */
namespace AscendCardVisual
{
	inline bool HasAction(const FCardInstance& Card, const TCHAR* Action)
	{
		for (const FCardEffect& Effect : Card.GetEffects())
			if (Effect.Action == Action) return true;
		return false;
	}

	inline bool HasAnyAction(const FCardInstance& Card, std::initializer_list<const TCHAR*> Actions)
	{
		for (const TCHAR* Action : Actions)
			if (HasAction(Card, Action)) return true;
		return false;
	}

	inline bool IsDamageAction(const FString& Action)
	{
		return Action == TEXT("damage") || Action == TEXT("damage_all")
			|| Action == TEXT("damage_random") || Action == TEXT("damage_per_block")
			|| Action == TEXT("damage_per_status") || Action == TEXT("damage_all_per_status")
			|| Action == TEXT("damage_all_per_repeat") || Action == TEXT("damage_all_per_basic_gongfa")
			|| Action == TEXT("one_sword_strike") || Action == TEXT("wan_jian_damage");
	}

	inline bool DealsDamage(const FCardInstance& Card)
	{
		for (const FCardEffect& Effect : Card.GetEffects())
			if (IsDamageAction(Effect.Action)) return true;
		return false;
	}

	inline bool TargetsAllEnemies(const FCardInstance& Card)
	{
		for (const FCardEffect& Effect : Card.GetEffects())
		{
			if (Effect.Target == TEXT("all_enemies") || Effect.Action == TEXT("damage_all")
				|| Effect.Action == TEXT("damage_all_per_status") || Effect.Action == TEXT("damage_all_per_repeat")
				|| Effect.Action == TEXT("damage_all_per_basic_gongfa") || Effect.Action == TEXT("one_sword_strike")
				|| Effect.Action == TEXT("wan_jian_damage")) return true;
		}
		return false;
	}

	inline bool TargetsEnemy(const FCardInstance& Card)
	{
		for (const FCardEffect& Effect : Card.GetEffects())
			if (Effect.Target == TEXT("enemy") || Effect.Target == TEXT("all_enemies")
				|| IsDamageAction(Effect.Action)) return true;
		return false;
	}

	inline FString ResolveAnimation(const FCardInstance& Card)
	{
		const FString& Id = Card.Data.Id;
		const FString& Name = Card.Data.Name;

		// Named signature cards retain a unique silhouette even when authored data
		// is incomplete or later regenerated.
		if (Id == TEXT("one_sword") || Name == TEXT("一剑") || Name.Contains(TEXT("一剑破万法")))
			return TEXT("greatsword");
		if (Id == TEXT("wan_jian_gui_zong") || Name.Contains(TEXT("万剑")))
			return TEXT("myriad_swords");
		if (Id == TEXT("sword_qi") || Id == TEXT("heaven_sword_art")
			|| Name.Contains(TEXT("剑气纵横")) || Name.Contains(TEXT("天剑")))
			return TEXT("sword_wave");
		if (Id == TEXT("heavenly_thunder") || Name.Contains(TEXT("天雷")) || Name.Contains(TEXT("雷")))
			return TEXT("thunder");
		if (Id == TEXT("wildfire") || Name.Contains(TEXT("火")) || Name.Contains(TEXT("炎")))
			return TEXT("flame_burst");
		if (Id == TEXT("poison_palm") || Id == TEXT("venom_burst")
			|| Name.Contains(TEXT("毒")) || Name.Contains(TEXT("蚀心")))
			return TEXT("poison");

		FString Explicit = Card.Data.Visual.Animation;
		if (!Explicit.IsEmpty() && Explicit != TEXT("none"))
		{
			// Legacy metadata aliases are promoted to the richer shared families.
			if (Explicit == TEXT("block")) return TEXT("ward");
			if (Explicit == TEXT("draw")) return TEXT("spirit_flow");
			return Explicit;
		}

		if (HasAnyAction(Card, {TEXT("self_damage")})) return TEXT("curse_burst");
		if (DealsDamage(Card))
		{
			if (TargetsAllEnemies(Card) && (Card.Data.Class == TEXT("sword")
				|| Card.Data.Type == TEXT("sword") || Card.Data.Type == TEXT("zhaoshi")))
				return TEXT("sword_wave");
			return Card.Data.Class == TEXT("sword") || Card.Data.Type == TEXT("sword")
				|| Card.Data.Type == TEXT("zhaoshi") || Card.Data.Type == TEXT("basic")
				? TEXT("slash") : TEXT("impact");
		}
		if (HasAnyAction(Card, {TEXT("block"), TEXT("block_per_status")})) return TEXT("ward");
		if (Card.Data.Type == TEXT("talisman")) return TEXT("talisman");
		if (HasAnyAction(Card, {TEXT("power"), TEXT("cost_free_basic_gongfa"),
			TEXT("enhance_one_sword"), TEXT("one_sword_free"), TEXT("one_sword_multiply")}))
			return TEXT("power_aura");
		if (HasAnyAction(Card, {TEXT("apply_status"), TEXT("amplify_status")})) return TEXT("seal");
		if (HasAnyAction(Card, {TEXT("gain_spirit"), TEXT("spirit_next_turn"), TEXT("draw"),
			TEXT("discard_random")})) return TEXT("spirit_flow");
		if (Card.Data.Type == TEXT("gongfa")) return TEXT("power_aura");
		return TEXT("spirit_flow");
	}

	inline FString ResolveSound(const FCardInstance& Card, const FString& Animation)
	{
		if (Animation == TEXT("greatsword")) return TEXT("sword_heavy");
		if (Animation == TEXT("myriad_swords")) return TEXT("sword_flurry");
		if (Animation == TEXT("sword_wave")) return TEXT("sword_wave");
		if (Animation == TEXT("thunder")) return TEXT("thunder_crack");
		if (Animation == TEXT("flame_burst")) return TEXT("fire_burst");
		if (Animation == TEXT("poison")) return TEXT("poison_hiss");
		if (Animation == TEXT("ward")) return TEXT("ward_raise");
		if (Animation == TEXT("spirit_flow")) return TEXT("spirit_chime");
		if (Animation == TEXT("power_aura")) return TEXT("power_surge");
		if (Animation == TEXT("talisman")) return TEXT("talisman_cast");
		if (Animation == TEXT("seal")) return TEXT("seal_stamp");
		if (Animation == TEXT("curse_burst")) return TEXT("curse_whisper");
		if (!Card.Data.Visual.Sound.IsEmpty() && Card.Data.Visual.Sound != TEXT("none"))
		{
			// Preserve compatibility with older card JSON while keeping every
			// emitted event on the authored modern audio path.
			if (Card.Data.Visual.Sound == TEXT("block")) return TEXT("block");
			if (Card.Data.Visual.Sound == TEXT("heal")) return TEXT("heal_chime");
			if (Card.Data.Visual.Sound == TEXT("draw")) return TEXT("draw");
			if (Card.Data.Visual.Sound == TEXT("fireball")) return TEXT("fire_burst");
			if (Card.Data.Visual.Sound == TEXT("impact")) return TEXT("impact_hit");
			return Card.Data.Visual.Sound;
		}
		if (Animation == TEXT("slash")) return TEXT("sword_slash");
		if (Animation == TEXT("fireball")) return TEXT("fire_burst");
		if (Animation == TEXT("heal")) return TEXT("heal_chime");
		return TEXT("impact_hit");
	}

	inline FString ResolveAccent(const FCardInstance& Card, const FString& Animation)
	{
		if (!Card.Data.Visual.Accent.IsEmpty() && Card.Data.Visual.Accent != TEXT("#FFFFFF"))
			return Card.Data.Visual.Accent;
		if (Animation == TEXT("greatsword")) return TEXT("#FFE7A0");
		if (Animation == TEXT("myriad_swords")) return TEXT("#A8F4FF");
		if (Animation == TEXT("sword_wave")) return TEXT("#B8F2FF");
		if (Animation == TEXT("slash")) return TEXT("#EAF7FF");
		if (Animation == TEXT("thunder")) return TEXT("#BFD8FF");
		if (Animation == TEXT("flame_burst") || Animation == TEXT("fireball")) return TEXT("#FF8A32");
		if (Animation == TEXT("poison")) return TEXT("#9BE85B");
		if (Animation == TEXT("ward")) return TEXT("#78D8FF");
		if (Animation == TEXT("spirit_flow")) return TEXT("#72F2DE");
		if (Animation == TEXT("power_aura")) return TEXT("#FFD56A");
		if (Animation == TEXT("talisman") || Animation == TEXT("seal")) return TEXT("#F6C75D");
		if (Animation == TEXT("curse_burst")) return TEXT("#C94C78");
		return TEXT("#FFFFFF");
	}
}
