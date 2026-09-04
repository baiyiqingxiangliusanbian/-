# 音频第三方素材与授权记录

更新时间：2026-08-31

本文件只记录音频素材的来源和再分发注意事项。项目代码采用 MIT；音频素材的授权以本文件及各素材包内的原始许可文件为准。

## Kenney CC0 音效

本次导入的牌面、UI、物理打击和战斗反馈来自 Kenney 的以下官方素材包：

- [Interface Sounds](https://kenney.nl/assets/interface-sounds)
- [RPG Audio](https://kenney.nl/assets/rpg-audio)
- [Impact Sounds](https://kenney.nl/assets/impact-sounds)
- [Casino Audio](https://kenney.nl/assets/casino-audio)

Kenney 资产页标注这些素材为 CC0；下载包中的 `License.txt` 随源文件保存在 `Tools/AudioWorkbench/staging/cc0_sources/kenney/`，便于后续复核。原始素材没有被直接用于运行时播放，项目内的 WAV 是经过裁切、低通、响度控制和自然淡出的处理版本。

## 当前映射

| 项目事件 | 项目资产 | 素材包 | 原始素材 |
| --- | --- | --- | --- |
| 选中牌 | `card_pick` | Casino Audio | `card-slide-4.ogg` |
| 拖牌 | `card_drag` | Casino Audio | `card-shove-1.ogg` |
| 出牌 | `card_play` | Casino Audio | `card-place-2.ogg` |
| 抽牌 | `draw` | Casino Audio | `card-fan-1.ogg` |
| 弃牌 | `discard` | Casino Audio | `card-shove-2.ogg` |
| 洗牌 | `reshuffle` | Casino Audio | `card-shuffle.ogg` |
| 目标锁定 | `target_lock` | Interface Sounds | `bong_001.ogg` |
| 确认 / 返回 / 拒绝 | `ui_confirm` / `ui_back` / `ui_deny` | Interface Sounds | `confirmation_003.ogg` / `back_002.ogg` / `error_003.ogg` |
| 地图节点 | `map_node` | Interface Sounds | `pluck_002.ogg` |
| 药剂 | `potion` | Interface Sounds | `glass_003.ogg` |
| 命中 / 格挡 / 受击 / 死亡 | `impact_hit` / `block` / `player_hit` / `enemy_death` | Impact Sounds | `impactPunch_medium_001.ogg` / `impactMetal_light_002.ogg` / `impactSoft_medium_001.ogg` / `impactWood_heavy_002.ogg` |
| 轻击 / 重击 | `sword_slash` / `sword_heavy` | RPG Audio / Impact Sounds | `knifeSlice2.ogg` / `impactMetal_heavy_002.ogg` |
| 进入战斗 / 敌方回合 | `combat_enter` / `enemy_turn` | Impact Sounds | `impactBell_heavy_002.ogg` / `impactSoft_heavy_002.ogg` |
| 灵石 / 奖励 | `gold` / `reward` | Casino Audio | `chips-collide-2.ogg` / `chips-stack-3.ogg` |

## Stable Audio BGM

正式 BGM 已在 Stable Audio 浏览器会话中生成并导出为 WAV，于 2026-08-31 导入项目。每个场景只保留一首完整曲目，不再使用 A/B 变体；曲目与 Stable Audio 会话对应如下：

| 场景 | Stable Audio 会话 | 项目资产 |
| --- | --- | --- |
| 标题 | `Jade Throne Overture` | `bgm_title.wav` |
| 战斗 | `Dark Jade Duel` | `bgm_combat.wav` |
| 剧情 RP | `Dark Xianxia Descent` | `bgm_narrative.wav` |

导出文件保留在 `Tools/AudioWorkbench/staging/stableaudio-final/`，原有项目资源备份在对应时间戳目录中。发布前应按 Stable Audio 账户计划条款确认作品的再分发要求；本项目代码仍采用 MIT。
