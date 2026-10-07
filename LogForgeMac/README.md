# LogForge iOS — 1.3.1 (26107B)

[English project README](https://github.com/HongHuo0728/LogForge/blob/main/README.md) · [简体中文主说明](https://github.com/HongHuo0728/LogForge/blob/main/README.zh-CN.md)

This is the native Swift / SwiftUI / Metal iOS 26+ application in the main
LogForge repository. `LogForgeMac` is a historical directory name. iOS patch
versions now come from the Xcode project independently of the Windows version.
The homepage and settings show **1.3.1 (26107B)**; Apple’s numeric bundle build remains
internal. Windows stays at 1.3.0 (26929A).

本次 iOS 1.3.1 修复 AAC / 时码轨道复制差异，并改进设置中的开源许可页面。
发生轨道验证差异时，从原 MOV 保留音频样本、预滚及编辑列表，再重新严格核验；
不跳过验证。保留 Swift / SwiftUI / Metal、CPU/Metal 色彩处理和软件 ProRes 回退。
输入仍为 ProRes 422/HQ BT.2020 HLG MOV，输出为 Apple Log ProRes 422/HQ。

[构建与安装](docs/GITHUB_BUILD.md) · [1.3.1 发布说明](../docs/RELEASE_1.3.1_IOS.md)

从主仓库的 **iOS build and tests** 成功构建中获取
`LogForge-1.3.1-iOS-26107B-unsigned.ipa`、SHA-256 和匹配的 RelinkKit。
IPA 必须使用有效 Apple 签名和配置文件后安装。构建成功不等于 Release 已发布。
历史 v1.3.0 的 iOS 附件仍对应 4b9b777，不能用旧 IPA 验证本轮修复。

新增夹具覆盖 111 个 AAC 样本、2048/48000 秒音频起点、时码和编辑列表。
Windows 仅进行静态检查；原始视频、真机闪烁、老设备性能及编辑器识别需独立验证。
