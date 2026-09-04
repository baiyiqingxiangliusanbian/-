# `rp_scroll_vertical_v2.png` source record

- Asset: `rp_scroll_vertical_v2.png`
- Final path: `/Users/audezest/AscendSpire/Content/Art/ui/rp_scroll_vertical_v2.png`
- Generation mode: built-in `image_gen` tool (default built-in mode; no CLI/API fallback)
- Use case: `stylized-concept`
- Input images: none; this is a new generation, not an edit of the old horizontal scroll
- Generated source path: `/Users/audezest/.codex/generated_images/01a0622c-4067-7c12-98ee-f0887d607ce1/exec-1de309c5-003e-4c15-8b20-9652396ae83f.png`
- Source image was copied into the project and received only a deterministic alpha normalization. The central reading rectangle `(x=145..940, y=100..1348)` was made fully opaque while the generated outer silhouette alpha was preserved.
- A later image-gen alpha-edit attempt was rejected because it returned an RGB image with a baked checkerboard; it was not used for the final asset.

## Generation prompt

```text
Use case: stylized-concept
Asset type: game UI reading panel frame for a Chinese xianxia narrative RPG
Primary request: create a brand-new tall unfolded Chinese scroll frame for an in-game RP reading area, designed as a clean overlay over an existing scenic background
Scene/backdrop: no scene inside the asset; the area outside the scroll silhouette must be genuinely transparent so the game's background remains visible
Subject: one elegant vertically oriented unfolded scroll, approximately 3:4 portrait aspect ratio
Style/medium: restrained hand-painted xianxia game UI asset, aged artisan craft, crisp production-ready silhouette, subtle warm neutral palette
Composition/framing: portrait canvas around 3:4; narrow side silk borders; simple horizontal wooden rollers at the top and bottom; a large central continuous reading sheet with generous usable width and height; the central sheet must be a single uninterrupted opaque surface, not a cutout and not glass; keep border details small and close to the outer silhouette so they do not consume the reading width; symmetrical and straight-on
Lighting/mood: soft diffuse studio-like illumination, calm scholarly ancient atmosphere, low-contrast edge shading suitable behind high-contrast scrolling body text
Color palette: warm gray beige rice paper, muted parchment cream, subdued natural wood brown, restrained aged jade and bronze accents only on the frame edges
Materials/textures: very subtle handmade xuan paper fiber and cloth weave, lightly worn wood grain, fine silk edge texture; paper texture must remain quiet and even for text readability
Text (verbatim): none
Constraints: genuinely transparent alpha outside the scroll silhouette; central reading sheet must be fully opaque alpha 255 across the entire interior including corners within the frame; clean production asset; no text; no calligraphy; no symbols; no characters; no people; no portraits; no landscape; no mountain or water painting in the reading area; no decorative illustration in the reading area; no logos; no trademarks; no watermark; no holes; no transparent window; no glass panel; no smoky translucent center; no excessive ornament; no oversized borders; no horizontal/landscape composition
Avoid: any content printed on the paper, any interior artwork, any UI mockup, any buttons, any extra panels, any background scene inside the alpha area, any white matte outside the silhouette
```

## Validation

- PNG: RGBA, 8-bit, `1086x1448`
- Aspect ratio: `0.750000` (3:4)
- Alpha range: `0..255`
- Center reading rectangle sample minimum/maximum: `255/255`; all tested samples are opaque
- Four exterior corners: alpha `[0, 0, 0, 0]`
- Visual inspection: vertical open scroll, horizontal wood rollers, narrow silk borders, continuous blank warm paper, no text or interior illustration
