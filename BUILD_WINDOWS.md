Windows 打包指南
==================

前提条件（Windows PC）
----------------------
- Windows 10/11
- Unreal Engine 5.8（从 Epic Games Launcher 安装，与 Mac 相同版本号）
- Visual Studio 2022（工作负荷：.NET 桌面开发 + 使用 C++ 的游戏开发）
- 代码副本（将整个项目文件夹复制到 Windows 机器）

步骤
----
1. 右键 AscendSpire.uproject → Generate Visual Studio project files
2. 打开生成的 AscendSpire.sln，确认解决方案配置为「Development Editor」+ 「Win64」
3. 在解决方案管理器中右键 AscendSpire → 生成，确认编辑器编译通过
4. 关闭 VS，用 RunUAT 打包：

   `"C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\RunUAT.bat" BuildCookRun -project="C:\path\to\AscendSpire\AscendSpire.uproject" -noP4 -clientconfig=Shipping -platform=Win64 -cook -build -stage -pak -package`

   输出在 `AscendSpire\Saved\StagedBuilds\Windows\AscendSpire-Mac-Shipping\`（名称中的 Mac 是原始 Target 名，不影响内容）。

5. 输出的 `AscendSpire.exe` 可拷贝到其他 Windows 机器直接运行。

常见问题
--------
- 编译报错：确认 VS 安装了「使用 C++ 的游戏开发」工作负荷，且已通过 Epic Launcher 安装 UE 5.8
- 缺失 DLL：安装 Visual C++ Redistributable（通常 VS 已附带）
- 项目版本不匹配：Windows 和 Mac 上的 UE 版本必须一致（5.8.x 小版本号也要相同）
