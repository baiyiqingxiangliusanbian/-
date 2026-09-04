"""Generate the AscendSpire scene music pack with ACE-Step.

The model generates a short instrumental bed for each game state.  We keep the
semantic filenames used by AscendAudioRouter and apply a small boundary blend so
the result is safe to loop in Unreal's 2D music player.
"""

from __future__ import annotations

import argparse
from pathlib import Path

import numpy as np
import soundfile as sf
from acestep.pipeline_ace_step import ACEStepPipeline


BGM_PROMPTS = {
    "bgm_title": "instrumental Chinese xianxia fantasy title theme, guqin and xiao bamboo flute, warm cinematic strings, jade bell accents, hopeful and spacious, elegant modern game soundtrack, 72 BPM, seamless loop, no vocals, no lyrics, no dramatic ending",
    "bgm_map": "instrumental wandering mountain map music for a Chinese xianxia roguelike, guqin, xiao flute, soft hand percussion, airy strings, curious and adventurous, calm forward motion, 84 BPM, seamless loop, no vocals, no lyrics, no dramatic ending",
    "bgm_narrative": "instrumental intimate xianxia narrative underscore, sparse guqin, breathy xiao flute, distant bowed strings, mystical and reflective, restrained tension, 68 BPM, seamless loop, no vocals, no lyrics, no dramatic ending",
    "bgm_shop": "instrumental whimsical fantasy shop theme, plucked guzheng, light wooden percussion, jade bells, warm playful chamber strings, charming but polished modern game soundtrack, 96 BPM, seamless loop, no vocals, no lyrics, no dramatic ending",
    "bgm_rest": "instrumental serene cultivation rest theme, slow guqin, soft xiao flute, gentle ambient strings, floating jade chimes, peaceful meditation after battle, 58 BPM, seamless loop, no vocals, no lyrics, no dramatic ending",
    "bgm_combat": "instrumental cinematic Chinese fantasy card battle music, tense guqin ostinato, low taiko and wooden percussion, sharp xiao flute accents, driving modern game soundtrack, 122 BPM, seamless loop, no vocals, no lyrics, no dramatic ending",
    "bgm_combat_elite": "instrumental elite encounter music for a dark xianxia roguelike, aggressive guqin patterns, deep war drums, metallic ritual percussion, ominous low strings, escalating but controlled, 132 BPM, seamless loop, no vocals, no lyrics, no dramatic ending",
    "bgm_combat_boss": "instrumental xianxia boss battle theme, ominous ritual drums, dark guqin, piercing xiao flute, massive low strings, ancient mountain demon atmosphere, intense cinematic modern game soundtrack, 108 BPM, seamless loop, no vocals, no lyrics, no dramatic ending",
}


def make_loopable(source: np.ndarray, crossfade_seconds: float = 0.75) -> np.ndarray:
    """Reduce a hard seam at the loop boundary without changing duration."""
    if source.ndim == 1:
        source = source[:, None]
    length = source.shape[0]
    fade = min(int(crossfade_seconds * 48_000), length // 4)
    if fade < 2:
        return source
    t = np.linspace(0.0, 1.0, fade, dtype=np.float32)[:, None]
    head = source[:fade]
    tail = source[-fade:]
    # Blend the beginning toward the end of the generated phrase. The final
    # sample is explicitly matched to the first to avoid a click at repeat.
    source = source.copy()
    source[:fade] = head * (1.0 - t) + tail * t
    source[-1] = source[0]
    peak = float(np.max(np.abs(source)))
    if peak > 0.0:
        source = np.clip(source / peak * 0.72, -1.0, 1.0)
    return source


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--checkpoint-dir", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--only", nargs="*", default=None)
    parser.add_argument("--duration", type=float, default=32.0)
    parser.add_argument("--steps", type=int, default=27)
    parser.add_argument("--seed", type=int, default=20260831)
    parser.add_argument("--cpu-offload", action="store_true")
    args = parser.parse_args()

    names = args.only if args.only else list(BGM_PROMPTS)
    unknown = [name for name in names if name not in BGM_PROMPTS]
    if unknown:
        raise SystemExit(f"Unknown BGM ids: {', '.join(unknown)}")

    print(f"Loading ACE-Step from {args.checkpoint_dir}...")
    pipeline = ACEStepPipeline(
        checkpoint_dir=str(args.checkpoint_dir),
        dtype="float32",
        torch_compile=False,
        cpu_offload=args.cpu_offload,
        overlapped_decode=False,
    )

    args.output_dir.mkdir(parents=True, exist_ok=True)
    raw_dir = args.output_dir / ".raw"
    raw_dir.mkdir(parents=True, exist_ok=True)
    for index, name in enumerate(names):
        output_path = args.output_dir / f"{name}.wav"
        if output_path.exists() and output_path.stat().st_size > 4096:
            print(f"[{index + 1}/{len(names)}] skip {name} (exists)")
            continue
        raw_path = raw_dir / f"{name}.wav"
        print(f"[{index + 1}/{len(names)}] generating {name}")
        pipeline(
            audio_duration=args.duration,
            prompt=BGM_PROMPTS[name],
            lyrics="",
            infer_step=args.steps,
            guidance_scale=15.0,
            scheduler_type="euler",
            cfg_type="apg",
            omega_scale=10.0,
            manual_seeds=[args.seed + index * 97],
            guidance_interval=0.5,
            guidance_interval_decay=0.0,
            min_guidance_scale=3.0,
            use_erg_tag=True,
            use_erg_lyric=False,
            use_erg_diffusion=True,
            oss_steps=None,
            guidance_scale_text=0.0,
            guidance_scale_lyric=0.0,
            save_path=str(raw_path),
        )
        audio, sample_rate = sf.read(str(raw_path), always_2d=True, dtype="float32")
        if sample_rate != 48_000:
            raise RuntimeError(f"{name}: expected 48000 Hz, got {sample_rate}")
        audio = make_loopable(audio)
        sf.write(str(output_path), audio, sample_rate, subtype="PCM_16", format="WAV")
        print(f"  wrote {output_path}")


if __name__ == "__main__":
    main()
