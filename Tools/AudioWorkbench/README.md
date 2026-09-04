# AscendSpire 音频工作台

`Content/Audio/` 当前已替换为可运行的现代音频素材：牌面、UI、物理打击和战斗反馈优先使用 Kenney CC0 素材并经过本地统一处理；法术层仍使用现有的非运行时生成素材。`generate_soft_ui_sfx.py`、`compose_melodic_bgm.py` 和 `generate_audio.py` 仅作为离线原型工具保留，不会在游戏运行时生成声音。

## 命名与规格

- SFX：`Content/Audio/SFX/<事件ID>.wav`，单声道、48 kHz、16-bit PCM，建议 0.08–1.2 秒，保留清晰起音和短尾音。
- BGM：每个逻辑场景使用一首完整曲目 `Content/Audio/Music/<状态ID>.wav`，立体声、48 kHz、16-bit PCM，并在导入 Unreal 后设置为循环；同一场景的 UI 重绘不会重启音乐。
- 当前 BGM 状态为 `bgm_title`、`bgm_map`、`bgm_narrative`、`bgm_event`、`bgm_shop`、`bgm_rest`、`bgm_combat`、`bgm_combat_elite`、`bgm_combat_boss`、`bgm_reward`、`bgm_card_discovery`、`bgm_upgrade`、`bgm_deckbuilding`、`bgm_victory`、`bgm_defeat`。旧的 `<状态ID>.wav` 会作为过渡期回退素材保留。
- 不要改逻辑状态名：游戏通过逻辑事件 ID 自动加载；缺失的 SFX 只回退到共享的 `impact_hit` 资产，不会回到运行时程序化音效。

## 已完成的生成流程

1. Kenney CC0 素材按事件语义挑选牌面、UI、物理打击和战斗转场，统一为 48 kHz / mono / PCM16，并按事件时长裁切、低通和自然淡出。
2. 近距离 UI 音效保留低频主体和短释放尾音，避免尖锐点击疲劳；`ui_hover` 和法术层暂不强行替换，避免把合适的声音换成更差的素材。
3. BGM 的正式来源可以是 Stable Audio、AIVA 等在线生成服务导出的完整曲目；导入前统一为 48 kHz / stereo / PCM16，保留自然尾音，不要硬切收尾。每个场景只保留一首完整作品，避免短循环或 A/B 变体导致的突兀切换。
4. 生成结果先写入 `Tools/AudioWorkbench/staging/`，通过规格校验后覆盖 `Content/Audio/`；模型权重只存在于本地 `model_cache/`，不会随游戏发布。

## 重跑命令

在 Apple Silicon 上，Woosh 使用 MPS：

```bash
cd /Users/audezest/AscendSpire/Tools/AudioWorkbench/model_cache/woosh
../../.venv-woosh/bin/python ../../generate_woosh_sfx.py \
  --checkpoint-dir checkpoints/Woosh-DFlow \
  --output-dir /Users/audezest/AscendSpire/Tools/AudioWorkbench/staging/woosh_sfx
```

MusicGen 使用公开的 `facebook/musicgen-small` 权重：

```bash
cd /Users/audezest/AscendSpire
HF_HOME=Tools/AudioWorkbench/model_cache/musicgen \
  Tools/AudioWorkbench/.venv-ace/bin/python \
  Tools/AudioWorkbench/generate_musicgen_bgm.py \
  --output-dir Tools/AudioWorkbench/staging/musicgen_bgm
```

发布用的长循环（不依赖模型、生成速度快且循环确定）：

```bash
cd /Users/audezest/AscendSpire
Tools/AudioWorkbench/.venv-ace/bin/python \
  Tools/AudioWorkbench/compose_melodic_bgm.py \
  --output-dir Tools/AudioWorkbench/staging/composed_bgm
```

## 模型来源

- Woosh：Sony Research [Woosh](https://github.com/SonyResearch/Woosh)，短音效权重为 CC-BY-NC；本项目当前为非商业 MIT 项目。
- MusicGen：Meta [AudioCraft / MusicGen](https://github.com/facebookresearch/audiocraft)，模型权重为 CC-BY-NC；本项目当前为非商业 MIT 项目。
- Stable Audio Open Small 未用于本次资产生成，因为其 Hugging Face 仓库需要单独授权访问。

当前路由已接入：标题/设置、地图、战斗（普通/精英/Boss）、叙事、商店、休息、抽牌/弃牌/洗牌、卡牌拖拽和出牌、目标锁定、回合结束、伤害/格挡/治疗/击杀、奖励/灵石、药丸、锻造、胜负结算，以及按钮悬停和确认/拒绝。

## 外部 BGM 曲目导入

AIVA、Stable Audio 等服务导出的完整曲目下载后按下表改名，再将 WAV/MP3 转成 48 kHz / stereo / PCM16 WAV，导入 Unreal 的 `Content/Audio/Music/`，保持资产名与文件名一致；同一场景的 UI 重绘不会重启音乐：

| 场景 | 资产名 |
| --- | --- |
| 标题/设置 | `bgm_title` |
| 地图探索 | `bgm_map` |
| 普通事件 | `bgm_event` |
| 叙事 | `bgm_narrative` |
| 商店/坊市 | `bgm_shop` |
| 休息 | `bgm_rest` |
| 普通/精英/Boss 战斗 | `bgm_combat` |
| 奖励/胜利/失败 | `bgm_reward`、`bgm_victory`、`bgm_defeat` |
| 生卡/升级/牌组构筑 | `bgm_card_discovery`、`bgm_upgrade`、`bgm_deckbuilding` |
