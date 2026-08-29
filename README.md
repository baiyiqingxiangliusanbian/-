# AscendSpire / 登仙牌

《登仙牌》是一个基于 Unreal Engine 5.8 的中文修仙题材回合制卡牌 Roguelike。当前工程包含卡牌战斗、随机地图、叙事路线、可编译卡牌内容和可选的 LLM 剧情导演。

## 当前工程结构

- `Content/Art`：运行时美术资源、UI 贴图和字体
- `Content/Data`：卡牌、敌人、遗物、事件、世界书与叙事配置
- `Source/AscendSpire/Combat`：战斗引擎、敌人 AI 与战斗模拟
- `Source/AscendSpire/Core`：运行管理、数据读取、卡牌脚本编译、叙事系统
- `Source/AscendSpire/Map`：地图生成
- `Source/AscendSpire/UI`：UMG/Slate 界面与表现层
- `Tools/ContentWorkbench`：内容编辑器工具
- `Tools/NarrativeSim`：不启动 Unreal 的叙事回放与校验工具
- `Build`：平台打包所需的源码和资源

## 打开与构建

1. 安装与项目一致的 Unreal Engine 5.8.x。
2. 打开 `AscendSpire.uproject`，生成对应平台的工程文件。
3. 编译 `AscendSpire` 模块后运行编辑器或目标平台构建。

Windows 打包步骤见 [`BUILD_WINDOWS.md`](BUILD_WINDOWS.md)。

## 数据与叙事服务

卡牌和敌人等可编辑内容位于 `Content/Data`。叙事服务的 Endpoint、模型和 API Key 由游戏内设置读取；仓库不应保存任何真实 API Key。`Tools/NarrativeSim` 需要本机的运行存档和叙事服务配置，默认不会修改游戏存档。

## 版本库约定

仓库只跟踪源代码、配置、数据、运行时资源和必要的平台文件。`Binaries`、`DerivedDataCache`、`Intermediate`、`Saved` 以及本地美术替换备份均由 `.gitignore` 排除；这些目录可以在本机重新生成。
