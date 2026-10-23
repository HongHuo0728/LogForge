# LogForge iOS — 1.3.2 (261023A)

[English project README](https://github.com/HongHuo0728/LogForge/blob/main/README.md) · [简体中文主说明](https://github.com/HongHuo0728/LogForge/blob/main/README.zh-CN.md)

This is the native Swift / SwiftUI / Metal iOS 26+ application in the main
LogForge repository. `LogForgeMac` is a historical directory name. iOS patch
versions now come from the Xcode project independently of the Windows version.
The homepage and settings show **1.3.2 (261023A)**; Apple’s numeric bundle build remains
internal. Windows stays at 1.3.0 (26929A).

本次更新针对 iOS。双区域工作台在 Duo 展开横屏、展开竖屏、书本和桌面形态下
分别安排队列／进度／结果和转换参数；外屏（包括帐篷形态）使用完整单列。
布局根据窗口、size class 和活动保留区域变化，由 iOS 27.1 ArrangementView
避开折痕；iOS 26 保留自适应单列／双区域路径。页面级操作使用系统工具栏，
适用时在侧边排列；导入面板和参数行支持窄屏及大字体。

根视图持有队列、任务和设置草稿，保留五种语言、Liquid Glass 与触感。
保留 1.3.1 的 AAC／时码严格校验及轨道恢复，以及 CPU/Metal 和软件 ProRes 回退。
输入仍为 ProRes 422/HQ BT.2020 HLG MOV，输出为 Apple Log ProRes 422/HQ。

[构建与安装](docs/GITHUB_BUILD.md) · [1.3.2 发布说明](../docs/RELEASE_1.3.2_IOS.md)

从主仓库的 **iOS build and tests** 成功构建中获取
`LogForge-1.3.2-iOS-261023A-unsigned.ipa`、SHA-256 和匹配的 RelinkKit。
IPA 必须使用有效 Apple 签名和配置文件后安装。构建成功不等于 Release 已发布。
历史 v1.3.0 的 iOS 附件仍对应 4b9b777，不能用旧 IPA 验证本轮修复。

工作流固定 Xcode **27.1 (27A9275)** 和 iOS **27.1 SDK**，安装 Duo 及 iOS 26
模拟器。原生测试、真实姿态检查、截图及 IPA 验包通过后才允许正式发布。
几何单元测试不代表真实姿态验证；未完成的关键验证会阻止发布。
Windows 静态检查不编译 Apple 框架；真机性能、触感和编辑器识别仍需独立验证。

v1.3.2 的发布流程从已发布 v1.3.1 复用两份 Windows 1.3.0 附件，保留原文件名
和字节，共六个正式附件；不重建 Windows。历史 Release 和 tag 保持原样。
