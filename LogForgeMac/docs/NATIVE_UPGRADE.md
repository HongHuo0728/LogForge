# LogForge 原生 iOS 26 / 27 升级

更新：2026-10-05。所有实现都在现有 Swift / SwiftUI / Metal 工程中；没有迁移到 React Native / Expo，没有修改 Windows 工程或 `.github` 工作流。原有未接入的辅助模块和 `__MACOSX` 均保留。初始架构与问题清单见 [原始审查](NATIVE_ARCHITECTURE_REVIEW_2026-10-04.md)，该审查描述的是改动前的状态。

## 已接入的功能

| 用户功能 | 实际实现入口 |
| --- | --- |
| 启动自动评估、每个视频评估 ProRes 编码 | `ConversionQueue.assess` → `NativeCapabilities.supports`，按分辨率和 Standard/HQ 缓存实际三帧编码、MOV metadata 与解码验证结果 |
| 自动 / Metal / CPU 色彩处理 | `ConversionOptions` → `NativePipeline.run` → `MetalColorProcessor` / `V210Converter` |
| 系统编码与软件编码自动切换 | 合格配置优先 AVAssetWriter；探测失败或实际系统 writer 失败时使用 FFmpeg `prores_ks`，实际失败时从首帧完整重跑一次 |
| 相册、文件、递归文件夹导入 | `ContentView` 选择来源弹窗 → `ImportedMovie` 文件传输 / `ImportStorage` 安全作用域、文件协调、沙盒副本 |
| 批量队列 | 排序、去重、逐项预检、原因提示、串行处理、失败继续、取消、失败重试、重启恢复；不会静默修补无效时间戳 |
| 导出与报告 | 验证后的 MOV 和逐视频 JSON 保存在 Documents/Exports；可复制到选定文件夹；视频、逐视频报告、批量报告均有系统分享入口 |
| Windows 的曝光与创意调整 | 曝光 −4…+4 EV，独立创意开关、阴影、高光、饱和度；默认关闭创意调整，参数和 GPU ABI 统一 |
| 系统语言设置 | 左上角地球按钮调用公开 `UIApplication.openSettingsURLString`；英语、简中、繁中、法语、西班牙语及权限文案进入 Bundle |
| 透明 Liquid Glass | 页面卡片、队列条目、按钮、菜单入口、来源和设置弹窗使用 `Glass.clear` / `GlassEffectContainer`；工具栏按钮使用相同 clear 样式，隐藏额外共享背景 |
| 可读性与辅助功能 | 文字保留正常不透明度、主次语义颜色、玻璃下轻薄明暗衬底；增加对比度 / 减少透明度时提高衬底，减少动态效果时关闭按压缩放 |
| 触感 | Core Haptics 三档强度，以及成功 / 失败双脉冲；可关闭；引擎重置和停止恢复，设备不支持时静默处理 |
| 原生分层图标 | `LogForge.icon` 中三条彩色带和白色 Log 曲线，玻璃 / 反光 / 透射参数；生成的位图效果稿、1024 PNG 另行保留 |

这里的“小组件”是应用内控件，没有新增 WidgetKit 扩展。系统 Photos / Files / 分享面板和菜单弹出层使用 iOS 自身的组件与外观，应用不替换系统面板。

系统语言按钮打开本应用的设置页。语言选项的显示与最终语言由 iOS 管理；没有使用私有 URL 跳到语言子页，也没有另一套应用内语言存储。

## 输入和调用链

按照确认后的范围，仅接受 MOV、单主视频轨、ProRes 422 Standard (`apcn`) / HQ (`apch`)、10-bit、BT.2020 HLG、偶数宽度。显式冲突的 primaries / matrix、非 HLG、交错信息、异常 chroma 均拒绝。primaries / matrix 缺失时按 BT.2020 解释，range 缺失按视频范围，chroma 缺失按 left，都会记录提示。HEVC / PQ / SDR 没有新增为输入或输出路径。

```text
ContentView → ConversionQueue → VideoProcessor.process(options:)
  → InputContract.inspect
  → NativeCapabilities 实际编码探测
  → NativePipeline.run
      AVAssetReader → 解码 V210 / 逐帧解码后色彩描述
      → Metal 资格检查 + 第一实际帧 CPU 比对，或 CPU
      → AVAssetWriter 原始样本，或 prores_ks 压缩样本透传
      + 保留音频 / timecode / 视频关联的章节轨和关联关系
      → 完成临时 MOV
      → AppleLogMOV → OutputValidator → TrackIntegrity
      → 写报告 → 不覆盖地发布最终 MOV
```

色彩处理和编码是两个独立维度。选择 CPU 只改变色彩处理，编码仍自动选择；选择 Metal 时失败会明确报错，自动模式才允许回退 CPU。界面中的“系统 ProRes”不宣称必然是硬件编码；现有 VideoToolbox 诊断模块保留，但不再作为决定可用性的唯一依据。

## 修复和数据约束

- HLG 使用带负值延伸的反 OETF，超过 1 的输入不提前剪裁；以 HLG 0.75 参考白归一化到线性 1；Apple Log 使用 Apple 提供的 IDT 常量和分段公式。曝光、创意调整发生在线性域、Log 编码之前。
- 左 / 居中 chroma 重建与输出 left chroma 下采样使用 Spline36；CPU 使用 Double 行缓存，GPU 使用 Float。GPU fast math 关闭；浮点测试向量绝对误差要求 `2e-6`，实际首帧打包值允许最多两个 10-bit code 的差异。
- 每帧优先读取解码后样本的 range / chroma 和色彩描述，缺失时才沿用输入预检结果；解码器是否在各机型规范化 range / phase 仍需用实际样片检验。
- Metal 改为自己拥有的共享 MTLBuffer；避免非页对齐的 `bytesNoCopy`；命令完成前保留 job、输入与输出；完成结果在解锁 CVPixelBuffer 后才发布。
- 输出使用最多四个待保留图像的 CVPixelBufferPool，缓冲池只回收已被 writer 释放的图像。避免边编码边覆写上一帧；分配等待支持取消。软件编码使用 slice threading，每次提交都必须立即返回对应帧的压缩包。
- 视频 PTS 保持严格递增；重复、倒退和非数值时间戳拒绝。有效帧 duration 保留；缺失 duration 由下一 PTS / 最后轨道边界推导，无法得到正值则拒绝。负起点按保留轨道共同偏移；不强制 CFR。movie / 非音频 track 时间刻度取可表示的精确公倍数，溢出拒绝，避免默认刻度造成舍入。音频 `mediaTimeScale` 不设置，遵守 Apple API 限制。
- 音频、timecode、章节使用压缩或 PCM 样本透传；验证 SHA-256 payload、样本数量、连续时间区间、音频格式 / 布局与语言。旋转保持轨道 transform。未保留的其它辅助轨明确提示；GPS / location 不写入输出，容器 metadata 使用创建时间、make / model、语言白名单。
- 清除旧帧色彩附件、写入 BT.2020 / Apple Log 标记；输出 MOV 必须有 `colr nclc 9/2/9` 与准确的 `logs` identifier。Atom 处理限制 moov 大小、盒数量和深度，只修改程序自己的未发布文件，保持 mdat 与媒体偏移不动；冲突 HDR / gamma / Dolby atom 拒绝。
- 验证分层进行：像素算法、CV 附件、压缩格式描述、真实 MOV atom、全视频解码及逐帧时间、保留轨道。全部通过才发布；文件名碰撞改用后缀，失败 / 取消清理自己的 partial。选择外部文件夹后，副本经完整 SHA-256 回读再发布，本地验证文件仍可分享。
- 相册使用文件传输避免视频级 Data 常驻内存；Files / iCloud 读写使用安全作用域及文件协调。任务离开前台只申请有限 background task，系统要求结束时取消；没有宣称可在后台无限运行。

“苹果所有机型都能导出”的实现是软件编码作为可用路径，不依赖设备拥有 ProRes 硬件编码器。最低系统已设 iOS / iPadOS 26，旧系统不能安装；尚未逐机型验证解码、资源限制或速度。Windows / Metal 的媒体结果逐像素对照也未在此环境完成，不能把参数一致当作实测一致。

## Apple 平台构建

工程：`LogForge For iPhone/LogForge For iPhone.xcodeproj`；共享 scheme：`LogForge For iPhone`。最低部署 26.0，保留现有 Swift 5 语言模式，使用具有 iOS 26 或更新 SDK 的 Xcode。iOS 27 使用同一套公开 API，不添加虚构的 27 专属接口。

工程自己的 `Build LGPL software ProRes` phase 在 Sources 前运行 `NativeCodec/build-codec.sh`：从 FFmpeg 官网获取固定 **8.1.2** 源码并校验 SHA-256，按 SDK / 架构构建静态库和头文件，启用的编码器仅 `prores_ks`。禁止 GPL / nonfree，关闭不需要的媒体、网络和 Apple codec 模块，避免与 AVFoundation 解码重复。首次构建需要网络；源码缓存默认 `~/Library/Caches/LogForge`，可用 `LOGFORGE_SOURCE_CACHE` 改位置。脚本关闭汇编以减少交叉工具依赖，性能需要实机确认。

可在 macOS / 自己的 Actions 构建环境中执行以下命令；本文没有运行这些命令，也没有代改 workflow：

```sh
cd '<checkout>/LogForgeMac/LogForge For iPhone'
xcodebuild -project 'LogForge For iPhone.xcodeproj' \
  -scheme 'LogForge For iPhone' -configuration Debug \
  -destination 'generic/platform=iOS Simulator' build

# 用实际安装的模拟器 ID 替换占位符
xcodebuild -project 'LogForge For iPhone.xcodeproj' \
  -scheme 'LogForge For iPhone' -destination 'id=<simulator-UDID>' test
```

发布版本另行完成开发团队、签名和 provisioning 配置。FFmpeg 的 NOTICE 与 LGPL 文本已进入应用资源，可从设置中的开源许可查看。静态链接发布材料的辅助脚本为 `NativeCodec/package-relink.sh`；需要保留相应源码、应用对象、库、原始链接命令和 Xcode / SDK 版本，并实际验证可重链接。该辅助脚本未在 macOS 执行，源代码 URL 或一个脚本本身不代表发布材料已经准备完成。

## 已做和待做验证

Windows 已运行：[静态检查脚本](../tools/static_check.py)、两份 shell 脚本的 `bash -n`。结果见 [机器可读报告](NATIVE_UPGRADE_STATIC_CHECKS.json)：27 个 Swift 文件语法解析，121 个 Xcode 对象、路径及 target 成员解析，五语言 118 个键与格式参数一致，plist / JSON / XML / SVG 结构及四个图标图层路径检查，Metal 参数绑定与部署配置检查。没有删除初始 34 项文件。

新增的 `NativePipelineTests` 覆盖 HLG / Apple Log 参考点、负值和超白、创意开关、Spline36、V210 尾组 / 范围、Metal 浮点和带彩色 chroma 的打包对照、连续五帧软件编码、池保留样本、MOV 幂等性 / 冲突 / 截断、时间刻度、实际软件编码生成的 VFR + PCM + 旋转 MOV 端到端转换。`GlassFlowTests` 覆盖来源弹窗、设置保存 / 取消入口和语言按钮。原有测试保留。

**以上 XCTest / UI 测试尚未执行，Swift 语法解析不是 Swift / Apple SDK 类型检查。** 仍需 Apple 平台完成：

1. Debug / Release、真机 arm64 / 模拟器构建，C bridge / CF 所有权导入、FFmpeg 链接、Metal 编译与全部测试。
2. 强制 Metal / CPU / 自动，在 iOS 26 / 27、原生 ProRes 可用 / 不可用配置上转换；长时间 4K、资源压力、取消、后台过期、磁盘不足和重复文件名。
3. BT.2020 HLG full / video range、left / center、负值 / 超白、VFR / NTSC、非零 / 负起点、多个音轨、timecode / 章节真实样片，以及导出后解码与 Windows 参考文件比对。
4. Files / iCloud / 外部 provider 访问和目录副本；Photos 当前编码是否保留输入表示；队列重启恢复。
5. 明暗外观、动态字体、五种语言、增加对比度 / 减少透明度、VoiceOver，以及硬件触感。尚未声称达到实机测量的对比度。
6. `LogForge.icon` 用相应版本 Icon Composer / asset compiler 打开与编译，检查默认 / 深色 / 清透 / 着色。Apple 没有公开 icon.json schema，当前文本包依照已有 Icon Composer 文件结构构建；这是需要 Apple 工具确认的格式兼容点。位图备选见 IconArtwork，不能用静态 PNG 证明动态玻璃效果。
7. 在 Final Cut Pro / Resolve 等编辑器中检查 Apple Log 自动识别。写入标准 metadata 与 Core Media 重新读取通过，也不能代替编辑器实测。

## API 与算法依据

- [Apple：自定义 Liquid Glass](https://developer.apple.com/documentation/swiftui/applying-liquid-glass-to-custom-views)、[clear 玻璃与可读性](https://developer.apple.com/documentation/swiftui/glass/clear)、[工具栏共享背景](https://developer.apple.com/documentation/swiftui/toolbarcontent/sharedbackgroundvisibility%28_%3A%29)。
- [Apple：应用系统设置 URL](https://developer.apple.com/documentation/uikit/uiapplication/opensettingsurlstring)、[Icon Composer](https://developer.apple.com/documentation/xcode/creating-your-app-icon-using-icon-composer)。
- [Apple：Reader outputSettings](https://developer.apple.com/documentation/avfoundation/avassetreadertrackoutput/outputsettings)、[Writer outputSettings](https://developer.apple.com/documentation/avfoundation/avassetwriterinput/outputsettings)、[mediaTimeScale](https://developer.apple.com/documentation/avfoundation/avassetwriterinput/mediatimescale)、[movieTimeScale](https://developer.apple.com/documentation/avfoundation/avassetwriter/movietimescale)。
- [Apple：缓冲池分配阈值](https://developer.apple.com/documentation/corevideo/kcvpixelbufferpoolallocationthresholdkey)、[从图像生成格式描述](https://developer.apple.com/documentation/coremedia/cmvideoformatdescriptioncreateforimagebuffer%28allocator%3Aimagebuffer%3Aformatdescriptionout%3A%29)、[chroma phase](https://developer.apple.com/documentation/corevideo/image-buffer-chroma-location-constants)。
- [Apple 提供的 ACES Apple Log IDT](https://github.com/aces-aswf/aces-core/blob/528c78fe2c0f4e7eb322581e98aba05de79466cb/transforms/ctl/idt/vendorSupplied/apple/IDT.Apple.AppleLog_BT2020.ctl)。
- [FFmpeg 8.1.2 prores_ks 源码及线程能力](https://github.com/FFmpeg/FFmpeg/blob/n8.1.2/libavcodec/proresenc_kostya.c)，对应源码包和校验值记录在 `NativeCodec/NOTICE.txt`。
