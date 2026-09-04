"""Generate AscendSpire short SFX with Stable Audio Open Small.

The model is used only for replaceable source assets. The output is converted to
the project's 48 kHz WAV convention and kept mono for gameplay SFX.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

import torch
import torchaudio
from einops import rearrange
from stable_audio_tools import get_pretrained_model
from stable_audio_tools.inference.generation import generate_diffusion_cond


PROMPTS = {
    "ui_hover": "single short polished fantasy game UI hover tick, airy jade glass chime, clean transient, subtle tail, no music, no voice",
    "ui_confirm": "single satisfying modern fantasy game UI confirm sound, warm jade bell with a precise click transient, short elegant tail, no music, no voice",
    "ui_back": "single modern fantasy game UI back button sound, soft descending jade chime, restrained and clean, no music, no voice",
    "ui_deny": "single modern fantasy game UI error sound, muted low magical pulse and soft descending tone, not harsh, no music, no voice",
    "target_lock": "single target lock confirmation sound for a modern fantasy card game, focused crystalline ping with a subtle low pulse, no music, no voice",
    "card_pick": "single tactile fantasy trading card pickup sound, crisp paper flick, soft leather table tap, tiny magical shimmer, no music, no voice",
    "card_drag": "single short tactile card drag sound, light paper movement and airy magical friction, clean and subtle, no music, no voice",
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


def select_device() -> str:
    if torch.backends.mps.is_available():
        return "mps"
    if torch.cuda.is_available():
        return "cuda"
    return "cpu"


def save_project_wav(audio: torch.Tensor, source_rate: int, path: Path, mono: bool) -> None:
    audio = audio.detach().to(torch.float32).cpu()
    if audio.ndim == 1:
        audio = audio.unsqueeze(0)
    if mono and audio.shape[0] > 1:
        audio = audio.mean(dim=0, keepdim=True)
    if source_rate != 48_000:
        audio = torchaudio.functional.resample(audio, source_rate, 48_000)
    peak = audio.abs().max().item()
    if peak > 0:
        audio = (audio / peak * 0.86).clamp(-1, 1)
    path.parent.mkdir(parents=True, exist_ok=True)
    torchaudio.save(str(path), audio, 48_000, encoding="PCM_S", bits_per_sample=16)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--only", nargs="*", default=None)
    parser.add_argument("--steps", type=int, default=8)
    parser.add_argument("--seed", type=int, default=20260831)
    args = parser.parse_args()

    unknown = [name for name in (args.only or []) if name not in PROMPTS]
    if unknown:
        raise SystemExit(f"Unknown SFX ids: {', '.join(unknown)}")
    names = args.only if args.only else list(PROMPTS)
    device = select_device()
    print(f"Loading Stable Audio Open Small on {device}...")
    model, model_config = get_pretrained_model("stabilityai/stable-audio-open-small")
    model = model.to(device)
    sample_rate = int(model_config["sample_rate"])
    sample_size = int(model_config["sample_size"])
    print(f"Model sample rate={sample_rate}, sample size={sample_size}, count={len(names)}")

    for index, name in enumerate(names):
        path = args.output_dir / f"{name}.wav"
        if path.exists() and path.stat().st_size > 4096:
            print(f"[{index + 1}/{len(names)}] skip {name} (exists)")
            continue
        conditioning = [{"prompt": PROMPTS[name], "seconds_total": 1.2}]
        print(f"[{index + 1}/{len(names)}] generating {name}")
        with torch.no_grad():
            output = generate_diffusion_cond(
                model,
                steps=args.steps,
                cfg_scale=1.0,
                conditioning=conditioning,
                sample_size=sample_size,
                sampler_type="pingpong",
                device=device,
                seed=args.seed + index * 31,
            )
        output = rearrange(output, "b d n -> d (b n)")
        save_project_wav(output, sample_rate, path, mono=True)
        if device == "mps":
            torch.mps.empty_cache()
        print(f"  wrote {path}")


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        sys.exit(130)
