# 主 LogForge 仓库：iOS 云端构建与安装

Windows 开发环境也可以编辑原生工程并通过 GitHub 云端 Mac 编译。正式版本使用
主仓库 [HongHuo0728/LogForge](https://github.com/HongHuo0728/LogForge)，不依赖独立测试仓库。

## 获取正式版

当前 iOS 开发版本为 **1.3.1 (26107B)**。从成功的主仓库 Actions 获取
`LogForge-1.3.1-iOS-26107B-unsigned.ipa`、对应 `.sha256`，以及
`LogForge-1.3.1-iOS-26107B-RelinkKit.zip` 和校验文件。构建成功后仍需维护者发布附件；
版本号配置不代表线上 Release 已更新。历史 v1.3.0 附件保留原始版本。
见 [1.3.1 发布说明](../../docs/RELEASE_1.3.1_IOS.md)。

## 运行 Actions

1. 将本地源码和根目录工作流上传到主仓库后，打开
   [Actions / iOS build and tests](https://github.com/HongHuo0728/LogForge/actions/workflows/build-ios.yml)。
2. 点击 **Run workflow**，选择目标分支并运行。
3. 等待 **Test and package iOS** 成功：先运行模拟器回归，再归档并打包真机应用。
   正式工作流没有跳过测试选项，测试失败不产生新的 IPA。
4. 从底部 Artifacts 下载 `LogForge-1.3.1-iOS-26107B-unsigned-运行编号`。
   解压获得 IPA、RelinkKit、两份 SHA-256、`release-info.json`、`READ-ME.txt`
   和 `xcode-version.txt`。下载通常需要登录 GitHub。

`main` 的相关修改及 `v*` tag 会触发构建；iOS 工作流的 tag 检查对应 Xcode 版本（如 `v1.3.1`），不是 Windows CMake 版本。
工作流使用只读仓库权限，**不创建或编辑 GitHub Release**。Actions 附件保留 14 天；
作为正式版保存时，由维护者将所需文件上传到现有 Release。

根目录工作流：`.github/workflows/build-ios.yml`。运行目录：`LogForgeMac`。
构建脚本：`LogForgeMac/tools/build-ios.sh`。子目录内旧的 `.github` 配置不作为
主仓库 Actions 入口。

## 签名与安装

构建不需要 Apple 证书、账号或 Secrets，产物是 **未签名 iPhone arm64 IPA**。
不能直接点开安装；请使用自己的有效 Apple 签名和匹配的 provisioning profile。
App Store/TestFlight 还需要开发者账号和 App Store Connect 配置，目前未配置。
不要把私钥、证书密码或个人签名资料加入仓库。

签名后文件内容会变化；发布签名后的 IPA 时重新计算其校验值，并保留对应源码、
库、应用对象、许可声明及链接材料。不能把未签名文件的 SHA-256 用于签名后文件。

## 版本与报告

应用首页和设置页显示 `1.3.1 (26107B)`。发行构建号为 `26107B`。
`CFBundleShortVersionString` 为 `1.3.1`；`CFBundleVersion` 保持数字，
以 Actions 运行编号标识具体构建，仅用于内部诊断。
脚本从原生 Xcode 工程读取 iOS 版本，在打包前核验实际应用 Info.plist。
Windows 版本不覆盖 iOS patch 版本。
独立打开原生工程时使用 Xcode 项目中的版本默认值。

成功转换报告包含 `appVersion`、`releaseBuild`（`26107B`）、内部 `bundleBuild` 和轨道表恢复状态。
批量报告只记录每条视频最后一次尝试的版本；旧任务缺少新增字段时保留 `unknown`，
不会自动冒充新版测试。安装后可重试失败视频生成新的记录。

## 编译和故障定位

- 使用 `macos-26` 和云端 Xcode，脚本要求 iOS SDK 至少为 26。
- 编译原有 Swift / SwiftUI / Metal 及锁定版本的 LGPL 软件 ProRes 编解码库。
- 系统解码失败后从原始文件重启软件解码；系统编码失败独立回退软件编码。
  导出验证也有软件解码路径；不按手机型号硬编码能力名单。
- AVFoundation 读取 MOV 压缩包及辅助轨道，软件解码复制到应用拥有的 V210 内存；
  保留源时间戳、范围、色度位置，后续仍使用 CPU/Metal 转换。
- 输出验证先独立读取 MOV 存储的 PTS、duration、帧数和总时长，再解码所有帧检查
  V210、尺寸、数量及 PTS 对应；解码图像缺少 duration 不再代表文件时长为零。
  无效存储时间、重复/倒退、预期时间不匹配仍失败。
- 音频/辅助轨道零偏移直接提交原样本，非零偏移保留严格时间检查；覆盖纳秒视频
  时钟、48 kHz 音频、4K30 ProRes、末帧不等长及真实软件 MOV 集成测试。
- CoreMedia 可能为已知 10-bit ProRes 报告 `BitsPerComponent=12`，不能单凭此字段
  推断原始拍摄位深；必须检查实际解码布局为 10-bit V210。
- 输入仍要求 ProRes 422/HQ BT.2020 HLG MOV，没有扩展到 HEVC、PQ、SDR 或损坏/加密视频。

失败时查看日志中的 `error:`。无论成功与否，工作流尽可能上传
`LogForge-iOS-diagnostics-运行编号`，包含测试/归档日志和 `.xcresult`。
系统硬件能力、液态玻璃对比度、触感、老设备耗时/温度和编辑器识别仍需真机检查。
此前 27 项模拟器测试通过，不代表本次未上传的修改已编译，也不替代用户原视频测试。

Apple 文档：[原始样本读取](https://developer.apple.com/documentation/avfoundation/avassetreadertrackoutput/outputsettings)、
[样本 duration](https://developer.apple.com/documentation/coremedia/cmsamplebuffergetduration(_:))、
[Apple 构建号](https://developer.apple.com/documentation/bundleresources/information-property-list/cfbundleversion)。
软件解码 API：[libavcodec](https://www.ffmpeg.org/doxygen/8.1/group__lavc__decoding.html)。
