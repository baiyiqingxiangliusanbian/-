"""Generate AscendSpire scene music with the public MusicGen Small checkpoint."""

from __future__ import annotations

import argparse
from pathlib import Path

import numpy as np
import soundfile as sf
import torch
import torchaudio
from transformers import AutoProcessor, MusicgenForConditionalGeneration


MODEL_ID = "facebook/musicgen-small"
BGM_PROMPTS = {
    "bgm_title": "instrumental Chinese xianxia fantasy title theme, memorable 8-bar pentatonic melody on warm guqin and gentle xiao bamboo flute, soft cinematic strings, jade bell accents, hopeful spacious intro then a clear return, elegant modern game soundtrack, steady 92 BPM, no vocals, no abrupt ending, no ambient-only texture",
    "bgm_map": "instrumental wandering mountain map music for a Chinese xianxia roguelike, memorable guqin melody answered by xiao flute, light plucked ostinato, soft hand percussion, airy strings, curious adventurous forward motion, clear 8-bar phrase with a gentle variation and return, modern game soundtrack, 104 BPM, no vocals, no abrupt ending",
    "bgm_narrative": "instrumental intimate xianxia narrative underscore, lyrical 8-bar guqin and breathy xiao melody, warm bowed strings underneath, small jade chime replies, mystical reflective emotion with restrained tension and a clear musical cadence that can repeat, modern game soundtrack, 76 BPM, no vocals, no abrupt ending",
    "bgm_shop": "instrumental whimsical fantasy shop theme, catchy 8-bar plucked guzheng melody, light wooden percussion, jade bells, warm playful chamber strings, charming polished modern game soundtrack, clear A and B phrases with a return, 112 BPM, no vocals, no abrupt ending",
    "bgm_rest": "instrumental serene cultivation rest theme, memorable slow guqin melody and soft xiao flute answer, gentle ambient strings, floating jade chimes, peaceful meditation after battle, clear 8-bar phrase, warm low dynamics, polished modern game soundtrack, 68 BPM, no vocals, no abrupt ending",
    "bgm_combat": "instrumental modern Chinese xianxia roguelike card battle theme, memorable minor pentatonic 8-bar main melody on guqin and low xiao flute, deep taiko and wooden percussion groove, repeating low ostinato, clear verse lift and return, controlled intensity, cinematic polished game soundtrack, 112 BPM, no vocals, no random ambient texture, no cinematic ending, seamless loop",
    "bgm_combat_elite": "instrumental elite encounter theme for a dark xianxia roguelike, memorable ominous 8-bar guqin motif, deep war drum groove, restrained metallic ritual percussion, low strings, a clear escalating B phrase returning to the motif, intense but controlled modern game soundtrack, 118 BPM, no vocals, no abrupt ending",
    "bgm_combat_boss": "instrumental xianxia boss battle theme, memorable dark 8-bar guqin and low xiao melody, ominous ritual drums, massive low strings, ancient mountain demon atmosphere, strong A section and escalating B section that returns cleanly, intense cinematic modern game soundtrack, 108 BPM, no vocals, no random ambient texture, no cinematic ending",
}


def select_device() -> str:
    if torch.backends.mps.is_available():
        return "mps"
    if torch.cuda.is_available():
        return "cuda"
    return "cpu"


def make_loopable(audio: np.ndarray, sample_rate: int) -> np.ndarray:
    if audio.ndim == 1:
        audio = audio[:, None]
    fade = min(int(0.55 * sample_rate), audio.shape[0] // 4)
    if fade >= 2:
        t = np.linspace(0.0, 1.0, fade, dtype=np.float32)[:, None]
        audio = audio.copy()
        audio[:fade] = audio[:fade] * (1.0 - t) + audio[-fade:] * t
        audio[-1] = audio[0]
    peak = float(np.max(np.abs(audio)))
    if peak > 0.0:
        audio = np.clip(audio / peak * 0.72, -1.0, 1.0)
    return audio


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--only", nargs="*", default=None)
    parser.add_argument("--seconds", type=float, default=10.0)
    parser.add_argument("--tokens", type=int, default=512)
    parser.add_argument("--seed", type=int, default=20260831)
    args = parser.parse_args()

    names = args.only if args.only else list(BGM_PROMPTS)
    unknown = [name for name in names if name not in BGM_PROMPTS]
    if unknown:
        raise SystemExit(f"Unknown BGM ids: {', '.join(unknown)}")
    device = select_device()
    print(f"Loading {MODEL_ID} on {device}...")
    processor = AutoProcessor.from_pretrained(MODEL_ID)
    model = MusicgenForConditionalGeneration.from_pretrained(MODEL_ID).to(device).eval()
    source_rate = int(model.config.audio_encoder.sampling_rate)
    print(f"Model sample rate={source_rate}, count={len(names)}")

    args.output_dir.mkdir(parents=True, exist_ok=True)
    for index, name in enumerate(names):
        path = args.output_dir / f"{name}.wav"
        if path.exists() and path.stat().st_size > 4096:
            print(f"[{index + 1}/{len(names)}] skip {name} (exists)")
            continue
        torch.manual_seed(args.seed + index * 97)
        print(f"[{index + 1}/{len(names)}] generating {name}")
        inputs = processor(text=[BGM_PROMPTS[name]], padding=True, return_tensors="pt")
        inputs = {key: value.to(device) for key, value in inputs.items()}
        with torch.inference_mode():
            audio_values = model.generate(
                **inputs,
                do_sample=True,
                guidance_scale=3.0,
                max_new_tokens=args.tokens,
            )
        audio = audio_values[0].detach().to(torch.float32).cpu()
        if audio.ndim == 1:
            audio = audio.unsqueeze(0)
        audio = torchaudio.functional.resample(audio, source_rate, 48_000)
        audio = audio.numpy().T
        if audio.shape[1] == 1:
            # MusicGen Small emits a centered mono bed. Add a tiny cyclic delay
            # to the right channel so the game keeps a stereo music bus without
            # introducing a discontinuity at the loop boundary.
            delay = int(0.007 * 48_000)
            mono = audio[:, 0]
            audio = np.stack([mono, np.roll(mono, delay)], axis=1)
        target = int(args.seconds * 48_000)
        if audio.shape[0] < target:
            audio = np.pad(audio, ((0, target - audio.shape[0]), (0, 0)))
        audio = make_loopable(audio[:target], 48_000)
        sf.write(str(path), audio, 48_000, subtype="PCM_16", format="WAV")
        print(f"  wrote {path}")
        if device == "mps":
            torch.mps.empty_cache()


if __name__ == "__main__":
    main()
