# 主仓库 1.3.0：iOS 26106A 发布步骤

本轮只准备本地修改，没有暂存、提交、推送、创建 tag、编辑线上 Release 或删除测试仓库。
以下操作由维护者在审查后执行。

## 源码审查与上传

1. 检查主仓库的 README、`README.zh-CN.md`、发布说明、许可声明、Windows 文档
   打包清单，以及根目录 `.github/workflows/build-ios.yml`。
2. 检查 `LogForgeMac/` 中原生源码、项目、资源、语言、许可证、构建脚本和静态检查。
   当前该目录尚未加入主仓库跟踪；只添加源码，不添加本地 `build/`、IPA、视频、
   DerivedData、`.xcresult`、个人编辑器状态或 `.codex-work/`。
3. 由维护者自行提交所需修改并推送到主仓库。主 CMake 的版本仍为 `1.3.0`，
   Windows build 仍为 `26929A`，iOS 发行 build 为 `26106A`。

## 构建与检查

在主仓库运行 **iOS build and tests**。模拟器测试、iPhone archive 和 Info.plist
版本检查全部通过后下载附件。检查 `release-info.json`、SHA-256、Xcode 版本、
测试/归档日志；确认应用内显示发行版本及独立数字构建号。

IPA 是 Release 配置的 arm64 应用，未使用 Apple 签名。安装测试需自行签名。
签名后在实际 iPhone 上确认导入、CPU/Metal 转换、系统/软件回退、音频、时间、
方向及导出结果，另检查液态玻璃阅读性、五语言设置和触感。
原视频和老设备性能的验证结果应按实际记录，不以模拟器结果代替。

## 补充现有 Release

编辑主仓库已有的 **v1.3.0 Release**，保留 Windows 26929A 的附件和历史说明，
增加 iOS 26106A 内容及四个附件：

- `LogForge-1.3.0-iOS-26106A-unsigned.ipa`
- `LogForge-1.3.0-iOS-26106A-unsigned.ipa.sha256`
- `LogForge-1.3.0-iOS-26106A-RelinkKit.zip`
- `LogForge-1.3.0-iOS-26106A-RelinkKit.zip.sha256`

Release notes 可采用主 `docs/RELEASE_1.3.0.md` 新增的 iOS 段落，附上本次主仓库
构建链接和实际验证范围。明确 IPA 未签名及安装方式；分发签名版时重新计算其哈希。
不要移动已有 `v1.3.0` tag 或覆盖旧 Windows 附件；本次 iOS 源码的提交和构建链接
应单独记录，避免将原 tag 的 Windows 验证误当作新增 iOS 的证据。

### iOS 26106A 的源码与构建记录

- `v1.3.0` tag 保留在 2026 年 9 月 29 日的 Windows 提交
  `913e4b9417fa0f32b88629c39062b54589d9dd21`。
- Release 自动生成的 **Source code (zip)** 和 **Source code (tar.gz)**
  跟随原 tag，**不包含 iOS 工程**。
- iOS 正式附件对应提交
  [`4b9b7771be97663cd3a3cb2dc3074a4cd1675fa0`](https://github.com/HongHuo0728/LogForge/commit/4b9b7771be97663cd3a3cb2dc3074a4cd1675fa0)，
  原生工程位于该提交的 `LogForgeMac/`。
- 下载该提交的完整源码：
  [ZIP](https://github.com/HongHuo0728/LogForge/archive/4b9b7771be97663cd3a3cb2dc3074a4cd1675fa0.zip)
  或 [tar.gz](https://github.com/HongHuo0728/LogForge/archive/4b9b7771be97663cd3a3cb2dc3074a4cd1675fa0.tar.gz)。
- [主仓库 iOS 第 2 次构建](https://github.com/HongHuo0728/LogForge/actions/runs/37495072326)
  已通过 5 项打包检查、28 项模拟器测试（无失败或跳过），并成功归档和生成 IPA。
  真机性能和编辑器识别仍需单独验证。

确认主仓库可独立构建、正式附件及源码已保存后，再由维护者决定测试仓库的移除。
正式说明和工作流不依赖测试仓库，删除它不应影响主项目构建。
