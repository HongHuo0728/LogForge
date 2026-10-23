# 主 LogForge 仓库：iOS 云端构建与安装

Windows 开发环境也可以编辑原生工程并通过 GitHub 云端 Mac 编译。正式版本使用
主仓库 [HongHuo0728/LogForge](https://github.com/HongHuo0728/LogForge)，不依赖独立测试仓库。

## 获取正式版

当前 iOS 开发版本为 **1.3.2 (261023A)**。从成功的主仓库 Actions 获取
`LogForge-1.3.2-iOS-261023A-unsigned.ipa`、对应 `.sha256`，以及
`LogForge-1.3.2-iOS-261023A-RelinkKit.zip` 和校验文件。分支构建只生成 Actions 附件；
正式 tag 从同一提交已成功的 main 构建取回产物，复核来源和哈希后发布六个附件，
不会再次构建或重复模拟器测试；其中包括从 v1.3.1 原样复用的两份 Windows
1.3.0 文件。版本号配置不代表线上 Release 已更新，历史附件保留原始版本。
见 [1.3.2 发布说明](../../docs/RELEASE_1.3.2_IOS.md)。

## 运行 Actions

1. 将本地源码和根目录工作流上传到主仓库后，打开
   [Actions / iOS build and tests](https://github.com/HongHuo0728/LogForge/actions/workflows/build-ios.yml)。
2. 点击 **Run workflow**，选择目标分支并运行。
   正式发布前必须有同一提交的 main push 构建成功；随后推送版本 tag 即可发布该产物。
3. 等待 **Test and package iOS** 成功：先归档、验包并上传候选 IPA，再运行 Duo
   必要检查；手动勾选 `extended_checks` 时才增加完整原生／交互、iOS 26 和 iPad
   检查。每次只启动一个模拟器，避免云端内存不足。
   发行检查覆盖真实 MOV 转换、导入取消、布局和主要页面操作，以及展开横纵向、
   书本、桌面、外屏横纵向六个真实布局。必要检查通过后才生成 `verification.json`。
   帐篷、分屏、分体键盘和多窗口各做一次补充诊断，Release Notes 按实际结果说明。
   可选的完整测试保留，但不反复扩大本次发行验证范围。
4. 从底部 Artifacts 下载 `LogForge-1.3.2-iOS-261023A-unsigned-运行编号`。
   解压获得 IPA、RelinkKit、两份 SHA-256、`release-info.json`、`READ-ME.txt`
   、`package-audit.json`、`verification.json` 和 `xcode-version.txt`。下载通常需要登录 GitHub。

`main` 的相关修改及 `v*` tag 会触发构建；iOS 工作流的 tag 检查对应 Xcode 版本（如 `v1.3.2`），不是 Windows CMake 版本。
构建 job 使用只读仓库权限。完成分支构建、截图和产物检查后，将新 `v1.3.2` tag
指向已验证的最终提交。tag 构建重新执行验证，仅发布 job 有写入 Release 的权限。
发布先创建草稿，核对六个附件上传后的字节和 SHA-256，再公开；失败保留草稿。
不得用旧提交的验证记录替代当前产物验证。Actions 诊断保留 14 天。

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

应用首页和设置页显示 `1.3.2 (261023A)`。发行构建号为 `261023A`。
`CFBundleShortVersionString` 为 `1.3.2`；`CFBundleVersion` 保持数字，
以 Actions 运行编号标识具体构建，仅用于内部诊断。
脚本从原生 Xcode 工程读取 iOS 版本，在打包前核验实际应用 Info.plist。
Windows 版本不覆盖 iOS patch 版本。
独立打开原生工程时使用 Xcode 项目中的版本默认值。

成功转换报告包含 `appVersion`、`releaseBuild`（`261023A`）、内部 `bundleBuild` 和轨道表恢复状态。
批量报告只记录每条视频最后一次尝试的版本；旧任务缺少新增字段时保留 `unknown`，
不会自动冒充新版测试。安装后可重试失败视频生成新的记录。

## 编译和故障定位

- 使用 GitHub 官方 `xcode-27` 镜像，明确选择 `/Applications/Xcode_27.1.app`，核验
  Xcode **27.1 (27A9275)**、iOS **27.1 SDK** 和 Metal 工具链。缺少组件时通过
  `xcodebuild -downloadComponent metalToolchain`、`-downloadPlatform iOS` 官方命令补齐。
- 安装 iOS 27.1 Duo runtime；扩展检查另外安装 iOS 26.5 runtime。最低部署仍为 iOS 26。
- 真实姿态测试固定 Appium 3.8.0／XCUITest Driver 12.19.0，等待铰链、活动区域和
  实际窗口稳定，并验证当前显示屏。截图屏幕选择不等于应用已切屏。帐篷形态无法
  实际进入时记录未验证，不能用外屏截图或注入几何冒充该姿态已通过。
  设置 `LOGFORGE_DUO_SUITE=full` 可运行完整交互矩阵；`LOGFORGE_NATIVE_SUITE=full`
  可运行完整原生单元测试。本次发行默认采用精简的必要检查。
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
`LogForge-iOS-diagnostics-运行编号`，包含测试/归档日志、`.xcresult`、Duo 截图、
可访问性树、角度／显示屏／几何记录与 `results.json`。`verification.json` 汇总
当前提交的真实姿态及 XCTest 结果；`package-audit.json` 记录实际产物身份和哈希。
系统硬件能力、液态玻璃对比度、触感、老设备耗时/温度和编辑器识别仍需真机检查。
各次执行的数量以对应 `.xcresult` 和验证清单为准，旧提交通过不代表新修改已验证，
也不替代用户原视频测试。Duo 接口使用及验证范围见 [Duo 适配清单](DUO_SUPPORT.md)。

Apple 文档：[原始样本读取](https://developer.apple.com/documentation/avfoundation/avassetreadertrackoutput/outputsettings)、
[样本 duration](https://developer.apple.com/documentation/coremedia/cmsamplebuffergetduration(_:))、
[Apple 构建号](https://developer.apple.com/documentation/bundleresources/information-property-list/cfbundleversion)。
软件解码 API：[libavcodec](https://www.ffmpeg.org/doxygen/8.1/group__lavc__decoding.html)。
