# AscendSpire / 登仙牌

> An experimental, LLM-driven card roguelike for exploring how AI may shape the games of the future.

《登仙牌》是一个由 LLM 驱动的中文修仙题材实验性卡牌 Roguelike。它的核心不是把 AI 当作一个聊天窗口，而是让模型参与构建可玩的内容：在受规则约束的前提下，持续生成新的关卡叙事、战斗路线和原创卡牌，并将卡牌设计编译为可以实际打出的游戏效果。

项目用于实践一个问题：如果未来的游戏可以由 AI 持续生成内容，游戏的关卡、卡牌、叙事和规则应该如何协作，才能既保持自由度，又不失去可玩性、可验证性和玩家选择的意义？

## 重点特性

- **LLM 驱动的无限内容实验**：叙事导演根据当前局面生成后续路线、冲突与选择，目标是让每一局都能继续向未知处展开。
- **无限生成关卡的受控路线**：敌人身份、数量、难度成长和结算由本地规则掌握，模型负责解释和扩展内容，避免文字生成偷偷改写游戏事实。
- **CardForge 原创卡牌工坊**：模型先提出卡牌设计目标，再编写 CardScript；本地编译器、硬检查和战斗 smoke test 通过后，还要对照真实基础/升级卡面复核，最后才接受并登记卡牌。
- **传统卡牌 Roguelike 骨架**：抽牌、弃牌、洗牌、能量、状态、法宝、随机地图、事件、商店、休息、奖励与升级。
- **修仙成长与叙事重置**：炼气至筑基的成长线、有限预写开场、动态剧情续写，以及可选的 SillyTavern 世界书/角色卡导入。
- **可观察的 AI 游戏工程**：请求上下文、状态补丁、战斗预演、生卡候选和接受复核均有明确边界，方便研究和复盘，而不是把结果藏在黑盒里。

## 下载最新实验版

当前 Release 提供：

- [macOS arm64 下载](https://github.com/baiyiqingxiangliusanbian/-/releases/latest/download/AscendSpire-macOS-arm64-0.2.0.zip)
- [Android arm64 APK 下载](https://github.com/baiyiqingxiangliusanbian/-/releases/latest/download/AscendSpire-Android-arm64-0.2.0.apk)
- [查看所有版本与校验信息](https://github.com/baiyiqingxiangliusanbian/-/releases)

这是开发中的实验版本，不代表最终商业发行品质。macOS 版本首次打开可能需要在系统安全提示中允许；Android 版本面向 arm64 设备。当前包的 LLM 功能需要玩家在游戏设置中填写自己的兼容服务配置。

## 安全与隐私

- 仓库和 Release **不包含测试 LLM API key、Authorization header、密码或其他凭据**。
- 游戏存档、Saved 配置、日志、崩溃报告、本地模型缓存和 staging 文件均不进入 Git。
- LLM Endpoint、模型名和 API key 只从本机游戏设置读取；请不要把个人密钥写入源码、截图、Issue 或日志。
- 发布包不附带开发机存档。测试时请把存档视为本地私有数据，不要上传。

## 开发环境

- Unreal Engine 5.8.x
- C++ / Unreal Engine HTTP、JSON、UMG、Slate
- 可选的 OpenAI-compatible LLM 服务

打开与构建：

1. 安装与项目一致的 Unreal Engine 5.8.x。
2. 打开 `AscendSpire.uproject`，生成对应平台的工程文件。
3. 编译 `AscendSpire` 模块，然后运行编辑器或目标平台构建。

Windows 打包步骤见 [`BUILD_WINDOWS.md`](BUILD_WINDOWS.md)。项目状态、已知限制和验证边界见 [`PROJECT_STATE_HANDOFF.md`](PROJECT_STATE_HANDOFF.md)。

## 工程结构

- `Content/Data`：卡牌、敌人、法宝、世界书与叙事配置
- `Content/Art`：运行时美术、UI 贴图、头像和字体
- `Content/Audio`：音频授权记录与导入说明；完整处理后音频随 Release 包提供
- `Source/AscendSpire/Combat`：战斗引擎、敌人 AI 与战斗模拟
- `Source/AscendSpire/Core`：运行管理、数据读取、CardScript 编译和叙事服务
- `Source/AscendSpire/UI`：UMG/Slate 界面、卡面和战斗表现
- `Tools/ContentWorkbench`：内容编辑与校验工具
- `Tools/AudioWorkbench`：音频处理和生成流程记录

## LLM 服务配置

游戏支持可配置的 OpenAI-compatible Endpoint。配置由游戏内设置读取，仓库只提供代码和公开内容，不提供任何服务账号或密钥。模型响应会经过结构化解析和规则验证；服务不可用时，游戏会沿本地安全路径回退，而不是把未经验证的文本直接写入游戏状态。

## 开源与素材许可

项目代码与原创项目内容按 [MIT License](LICENSE) 发布。第三方音频、模型和 Unreal Engine 本身不由本许可证重新授权；请阅读 [`Content/Audio/THIRD_PARTY_AUDIO.md`](Content/Audio/THIRD_PARTY_AUDIO.md) 及各素材包许可后再进行再分发。

## 研究方向

这是一个面向实践的原型，欢迎围绕以下方向讨论和贡献：

- AI 生成内容如何在本地规则约束下保持一致性？
- 如何衡量动态卡牌的强度、可读性与新鲜感？
- 玩家选择、模型自由度和游戏设计者意图之间如何建立清晰的契约？
- 未来的 AI-native 游戏需要怎样的编辑器、验证器和可观测性？

欢迎提交 Issue 分享体验、复现结果和设计想法。请不要在 Issue、日志或截图中粘贴 API key、存档或其他私人数据。
