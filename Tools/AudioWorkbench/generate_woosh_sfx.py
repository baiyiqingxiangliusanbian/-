"""Generate the AscendSpire gameplay SFX pack with Sony AI's Woosh-DFlow.

Woosh is used for short sound effects rather than music.  The output is normalized
to the project's 48 kHz mono WAV convention and trimmed to the semantic event's
expected duration so it can replace the procedural prototype pack atomically.
"""

from __future__ import annotations

import argparse
import random
from pathlib import Path

import torch
import soundfile as sf

from woosh.components.base import LoadConfig
from woosh.inference.flowmap_sampler import sample_euler
from woosh.model.flowmap_from_pretrained import FlowMapFromPretrained


SFX_PROMPTS = {
    "ui_hover": "single short polished fantasy game UI hover tick, airy jade glass chime, clean transient, subtle tail, no music, no voice",
    "ui_confirm": "single satisfying modern fantasy game UI confirm sound, warm jade bell with a precise click transient, short elegant tail, no music, no voice",
    "ui_back": "single modern fantasy game UI back button sound, soft descending jade chime, restrained and clean, no music, no voice",
    "ui_deny": "single modern fantasy game UI error sound, muted low magical pulse and soft descending tone, not harsh, no music, no voice",
    "target_lock": "single soft deep target lock confirmation for a modern fantasy card game, rounded low wooden pulse with a warm muted jade tone, no bright ping, no sharp transient, no music, no voice",
    "card_pick": "single gentle tactile fantasy trading card pickup, muted felt table tap and warm paper movement, rounded low-mid body, no bright click, no sharp high frequencies, no music, no voice",
    "card_drag": "single very soft short tactile card drag, muted paper friction and low warm body, restrained close-listener UI feedback, no hiss, no bright high frequencies, no music, no voice",
    "card_play": "single premium fantasy card placed on a wooden table, crisp paper slap, warm low body, brief jade magical shimmer, no music, no voice",
    "card_invalid": "single modern fantasy card game invalid action sound, soft low magical wobble and muted click, restrained, no music, no voice",
    "end_turn": "single end turn confirmation sound for a Chinese xianxia card game, low wooden temple resonance with a small jade chime, no music, no voice",
    "sword_slash": "single sharp magical sword slash, fast steel whoosh with a bright clean edge and tiny impact sparkle, modern AAA fantasy game SFX, no music, no voice",
    "sword_heavy": "single heavy enchanted greatsword strike, deep cinematic impact, wide steel whoosh, stone resonance, powerful but clean, no music, no voice",
    "sword_flurry": "rapid six-hit magical sword flurry, layered precise steel swishes with escalating bright impacts, modern fantasy action game SFX, no music, no voice",
    "sword_wave": "single wide sword energy wave, long sweeping crystalline whoosh ending in a deep magical impact, modern fantasy game SFX, no music, no voice",
    "thunder_crack": "single close thunder crack spell impact, bright electric snap, deep rolling sub impact, short cinematic tail, no music, no voice",
    "fire_burst": "single magical fire burst spell, hot flame ignition, rushing ember burst, low punch and crisp sparks, no music, no voice",
    "poison_hiss": "single toxic green magic hiss, wet bubbling vapor, subtle descending dark tone, modern fantasy game SFX, no music, no voice",
    "ward_raise": "single protective jade ward raised, crystalline shield bloom, resonant low magical hum, polished game feedback, no music, no voice",
    "block": "single strong magical shield block, bright metal-like deflection, compact low impact, clear transient, no music, no voice",
    "heal_chime": "single restorative healing spell, warm ascending jade bell arpeggio, soft breathy sparkle, comforting and short, no music, no voice",
    "spirit_chime": "single ethereal spirit chime, delicate porcelain and jade resonance, mystical airy tail, no music, no voice",
    "power_surge": "single restrained spiritual power surge, rising low magical pulse with bright energy crackle, modern fantasy game SFX, no music, no voice",
    "talisman_cast": "single talisman spell cast, paper seal flutter, ink brush flick, bright magical activation, no music, no voice",
    "seal_stamp": "single ancient seal stamp impact, wooden block and stone resonance, deep authoritative magical thump, no music, no voice",
    "curse_whisper": "single dark curse whisper texture without intelligible words, breathy spectral movement, low ominous pulse, no music, no voice",
    "impact_hit": "single clean fantasy combat hit, tight low punch with metallic magical crack, short tail, modern game SFX, no music, no voice",
    "player_hit": "single player damage feedback, deep body impact with muted dark pulse, readable but not painful, no music, no voice",
    "enemy_death": "single supernatural enemy defeat, collapsing dark energy, descending resonant impact and fading particles, no music, no voice",
    "draw": "single card draw from a deck, crisp paper slide and elegant airy magical flick, readable short game SFX, no music, no voice",
    "discard": "single card discard into a pile, soft paper cascade and low wooden tap, clean and restrained, no music, no voice",
    "reshuffle": "deck reshuffle, many layered paper cards swept together with a subtle magical vortex, one short game SFX, no music, no voice",
    "potion": "single fantasy potion use, glass clink, liquid swirl, bright restorative sparkle, no music, no voice",
    "enemy_turn": "single enemy turn transition, ominous low pulse with restrained wooden temple hit, no music, no voice",
    "combat_enter": "single combat encounter reveal, deep cinematic low hit, rising jade tension shimmer, modern fantasy card game SFX, no music, no voice",
    "reward": "single reward reveal, bright ascending jade and gold chime, premium fantasy game feedback, no music, no voice",
    "gold": "single handful of magical coins, crisp metallic jade-gold chimes, short satisfying reward sound, no music, no voice",
    "map_node": "single map node selection, soft ink brush flick followed by a focused jade chime, no music, no voice",
    "forge_start": "single magical card forging start, paper seal ignition, low alchemical hum and rising crystal tone, no music, no voice",
    "forge_success": "single magical card forging success, bright layered jade-gold chime, crystalline bloom and warm low resolve, no music, no voice",
    "victory_stinger": "short fantasy card game victory stinger, three-note ascending jade-gold chord, confident cinematic resolve, no music bed, no voice",
    "defeat_stinger": "short fantasy card game defeat stinger, two descending dark temple tones, solemn and restrained, no music bed, no voice",
    "breakthrough": "single cultivation breakthrough, deep spiritual resonance rising into a bright crystalline bloom, triumphant but compact, no music, no voice",
}

SFX_LENGTHS = {
    "ui_hover": 0.18, "ui_confirm": 0.30, "ui_back": 0.28, "ui_deny": 0.28,
    "target_lock": 0.34, "card_pick": 0.22, "card_drag": 0.18, "card_play": 0.34,
    "card_invalid": 0.30, "end_turn": 0.42, "sword_slash": 0.34, "sword_heavy": 0.72,
    "sword_flurry": 0.78, "sword_wave": 0.55, "thunder_crack": 0.62, "fire_burst": 0.52,
    "poison_hiss": 0.55, "ward_raise": 0.45, "block": 0.34, "heal_chime": 0.52,
    "spirit_chime": 0.48, "power_surge": 0.60, "talisman_cast": 0.48, "seal_stamp": 0.40,
    "curse_whisper": 0.60, "impact_hit": 0.30, "player_hit": 0.42, "enemy_death": 0.70,
    "draw": 0.34, "discard": 0.34, "reshuffle": 0.72, "potion": 0.48, "enemy_turn": 0.42,
    "combat_enter": 0.65, "reward": 0.75, "gold": 0.32, "map_node": 0.30,
    "forge_start": 0.58, "forge_success": 0.80, "victory_stinger": 0.95,
    "defeat_stinger": 0.95, "breakthrough": 1.20,
}


def select_device() -> str:
    if torch.cuda.is_available():
        return "cuda"
    if torch.backends.mps.is_available():
        return "mps"
    return "cpu"


def save_project_wav(audio: torch.Tensor, path: Path, duration: float) -> None:
    audio = audio.detach().to(torch.float32).cpu()
    if audio.ndim == 1:
        audio = audio.unsqueeze(0)
    if audio.shape[0] > 1:
        audio = audio.mean(dim=0, keepdim=True)
    target_samples = max(1, int(duration * 48_000))
    if audio.shape[-1] < target_samples:
        audio = torch.nn.functional.pad(audio, (0, target_samples - audio.shape[-1]))
    audio = audio[..., :target_samples]
    fade_samples = min(int(0.008 * 48_000), target_samples // 4)
    if fade_samples:
        audio[..., :fade_samples] *= torch.linspace(0, 1, fade_samples)
        audio[..., -fade_samples:] *= torch.linspace(1, 0, fade_samples)
    peak = audio.abs().max().item()
    if peak > 0:
        audio = (audio / peak * 0.86).clamp(-1, 1)
    path.parent.mkdir(parents=True, exist_ok=True)
    sf.write(str(path), audio.numpy().T, 48_000, subtype="PCM_16", format="WAV")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--checkpoint-dir", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--only", nargs="*", default=None)
    parser.add_argument("--steps", type=int, default=4)
    parser.add_argument("--seed", type=int, default=20260831)
    args = parser.parse_args()

    names = args.only if args.only else list(SFX_PROMPTS)
    unknown = [name for name in names if name not in SFX_PROMPTS]
    if unknown:
        raise SystemExit(f"Unknown SFX ids: {', '.join(unknown)}")
    device = select_device()
    print(f"Loading Woosh-DFlow from {args.checkpoint_dir} on {device}...")
    model = FlowMapFromPretrained(LoadConfig(path=str(args.checkpoint_dir))).eval().to(device)

    for index, name in enumerate(names):
        path = args.output_dir / f"{name}.wav"
        if path.exists() and path.stat().st_size > 4096:
            print(f"[{index + 1}/{len(names)}] skip {name} (exists)")
            continue
        generator = torch.Generator(device=device)
        generator.manual_seed(args.seed + index * 31)
        noise = torch.randn((1, 128, 501), generator=generator, device=device)
        cond = model.get_cond(
            {"audio": None, "description": [SFX_PROMPTS[name]]},
            no_dropout=True,
            device=device,
        )
        print(f"[{index + 1}/{len(names)}] generating {name}")
        with torch.inference_mode():
            audio = model.autoencoder.inverse(
                sample_euler(
                    model=model,
                    noise=noise,
                    cond=cond,
                    num_steps=args.steps,
                    renoise=[0, 0.5, 0.5, 0.3],
                    cfg=4.5,
                )
            )
        save_project_wav(audio[0], path, SFX_LENGTHS[name])
        if device == "mps":
            torch.mps.empty_cache()
        print(f"  wrote {path}")


if __name__ == "__main__":
    main()
