# LogForge iOS — 1.3.0 (26106A)

[English project README](https://github.com/HongHuo0728/LogForge/blob/main/README.md) · [简体中文主说明](https://github.com/HongHuo0728/LogForge/blob/main/README.zh-CN.md)

This directory contains the native **Swift / SwiftUI / Metal** iOS application in
the main [LogForge repository](https://github.com/HongHuo0728/LogForge). The
directory name `LogForgeMac` is historical; this target is an iOS application.
Minimum system: **iOS 26**. Version follows the Windows project: **1.3.0**;
iOS release build: **26106A**. The numeric Apple bundle build is shown separately.

本目录是主 LogForge 项目中的原生 iOS 工程，与 Windows 端共同属于 1.3.0。
现有 Swift / SwiftUI / Metal 架构保留；CPU/Metal 负责色彩转换，系统 ProRes
编解码不可用时自动回退内嵌软件编解码。当前支持 ProRes 422/HQ BT.2020 HLG MOV，
导出 Apple Log ProRes 422/HQ，并独立验证帧时间、音频、旋转和输出元数据。

**[主仓库云端构建和安装教程](docs/GITHUB_BUILD.md)** · **[1.3.0 iOS 发布步骤](docs/RELEASE_1.3.0_IOS.md)**

正式发布从主仓库 [Release v1.3.0](https://github.com/HongHuo0728/LogForge/releases/tag/v1.3.0)
下载已上传的 iOS 附件；云端构建使用主仓库的 **iOS build and tests**。
测试通过后打包 `LogForge-1.3.0-iOS-26106A-unsigned.ipa`，并提供 SHA-256 和匹配的
RelinkKit。IPA 需要自己的有效 Apple 签名才能安装，当前没有 App Store/TestFlight 配置。

此前测试构建的 27 项模拟器检查全部通过，无失败或跳过。本次版本显示和正式项目
工作流更新还需新的 Apple 云端构建；液态玻璃可读性、触感、老设备性能及编辑器
识别仍需真机验证。Windows 静态检查不能替代这些检查。
