# LogForge 原生 iOS 工程完整静态分析

分析日期：2026-10-04。工程根目录：`D:\LogForge\LogForgeMac\LogForge For iPhone`。

本轮阅读全部实现源码、测试源码、Xcode 工程、共享 scheme、workspace、语言资源和资源清单；对二进制图标检查 PNG 尺寸，对编辑器状态及 `__MACOSX` sidecar 只做清点。没有修改任何已有实现、资源或工程文件。本文记录当前代码行为，不表示已完成 iOS 构建或视频实测。

`D:\LogForge` 是外层 Git 仓库，已有 Windows/C++ 工程；本次对象是用户指定的原生 iOS 子目录。开始分析时该子目录显示为未跟踪，未执行提交、暂存或清理。父工程的 FFmpeg/CUDA/metadata 实测不能直接当作本 iOS 工程的验证结果。

## 1. 工程与文件覆盖

实际工程原有 34 个文件：19 个 Swift、1 个 Metal，以及项目配置、语言/资源文件和编辑器状态。19 个 Swift 中，16 个属于应用 target，1 个属于单元测试，2 个属于 UI 测试。应用 Sources 共 17 项，包含 Metal shader。

工程：`LogForge For iPhone.xcodeproj/project.pbxproj`；共享 scheme：`xcshareddata/xcschemes/LogForge For iPhone.xcscheme`。设置包括：

- `SDKROOT = iphoneos`，最低 iOS 17.2，`SWIFT_VERSION = 5.0`。这是 Swift 语言模式，不是本机编译器版本证据。
- 工程记录创建工具 Xcode 15.2；面向 iPhone / iPad，Debug / Release 两套配置。
- 自动生成 Info.plist，未发现独立应用 Info.plist、entitlements、Swift Package、Pods、第三方包或自定义构建脚本。
- `MTL_FAST_MATH = YES`；GPU 的数值结果需要在该实际编译设置下验证。
- Assets 包含 1024×1024 PNG 图标、AccentColor、Preview Assets；资源文件引用存在。
- `__MACOSX` 是解压产生的资源 sidecar，不是另一个工程，不参与编译。

以下路径均相对上述工程根目录，行号对应本轮未修改的源码快照。

| 文件 | 职责 | 当前入口可达性 |
| --- | --- | --- |
| `LogForge For iPhone/LogForge_For_iPhoneApp.swift:4` | `@main`、WindowGroup、ContentView | 应用入口 |
| `LogForge For iPhone/ContentView.swift:6` | 全部主界面、导入、基础信息、转换任务、分享 | 主界面 |
| `LogForge For iPhone/VideoProcessor.swift:230` | 输入诊断、解码、逐帧处理、ProRes 写出 | 主处理链 |
| `MetalColorProcessor.swift:263` | Metal 初始化、共享内存映射、GPU Job | 主处理链 |
| `LogForgeShaders.metal:579` | 唯一计算 kernel，HLG → Apple Log、V210 打包 | 主处理链 |
| `LogForge For iPhone/V210Converter.swift:434` | 与 shader 对应的 CPU 逐像素回退 | 主处理链 |
| `LogForge For iPhone/AppleLogProcessor.swift:26` | Double 版 Apple Log 编解码 | 已编译，主链无调用 |
| `LogForge For iPhone/HLGProcessor.swift:11` | Double 版 HLG 反 OETF 与参考白缩放 | 已编译，主链无调用 |
| `LogForge For iPhone/ColorConverter.swift:8` | Double 版 BT.2020 YCbCr/RGB 矩阵 | 已编译，主链无调用 |
| `LogForge For iPhone/VideoAnalyzer.swift:7` | 详细格式、色彩和 ProRes 信息分析 | 已编译，主界面无调用 |
| `LogForge For iPhone/VideoInfo.swift:3` | 详细分析模型，isSupported 要求 ProRes+HLG+BT.2020 | 被 Analyzer 使用，主链无调用 |
| `LogForge For iPhone/VideoInfoView.swift:3` | 详细信息卡片 | 已编译，未嵌入主界面 |
| `LogForge For iPhone/VideoDecoder.swift:13` | 名义上的首帧解码辅助类 | 已编译，主链无调用 |
| `LogForge For iPhone/VideoPicker.swift:4` | UIKit document picker，asCopy=true | 已编译，主界面使用 fileImporter |
| `LogForge For iPhone/LanguageManager.swift:4` | ObservableObject、系统语言与 Locale | 已编译，主界面使用独立 AppStorage |
| `LogForge For iPhone/OutputValidator.swift:39` | 导出文件格式、帧数、PTS 验证 | 已编译，转换结束后无调用 |
| `ProResEncoderDiagnostics.swift:72` | VideoToolbox / Writer 能力诊断及报告 | 已编译，主界面无调用 |

单元测试为空模板，UI 测试仅启动、启动性能和截图，没有颜色、V210、metadata、音频或编码路径的业务断言。不能据此认为转换正确。

## 2. SwiftUI 页面结构

```text
LogForgeForiPhoneApp
  WindowGroup
    ContentView
      NavigationStack → ScrollView → VStack
        标题、设备支持文字、中文/English 切换
        PhotosPicker + 文件导入按钮
        已选视频卡片：名称、naturalSize、fps、时长
        ProRes 422 HQ / ProRes 422 分段 Picker
        分析指示、转换进度、已处理帧数
        开始转换按钮
        完成卡片 + ShareLink
        错误卡片 + 关闭按钮
      fileImporter 回调
      selectedPhotoItem onChange 回调
```

这是单页状态驱动界面，没有 TabView、多页路由、播放器、Metal 预览、录制页面或独立导出设置页面。

`ContentView` 使用 `@State` 保存 URL、进度、帧数、处理/分析标志及错误；语言使用 `@AppStorage("appLanguage")`，值为 `zh/en`，默认中文。多数文案用三元表达式直接切换。

已有 `LanguageManager` 使用另一组 `AppLanguage` 存储键和 `zh-Hans/en` 值，未注入 App，也未被主界面使用；两种 Localizable.strings 各有 23 个键，但主界面没有按这些键查询文案。后续本地化修改应先确定接入哪条现有链。

基础导入分析只读取自然尺寸、时长和标称帧率；不会调用 `VideoAnalyzer` 显示编码/色彩信息。输入色彩资格检查发生在开始转换后。

## 3. 真实导入、转换与导出调用链

```text
文件：fileImporter → importVideo(url:)
相册：PhotosPicker onChange → importFromPhotos
        → loadTransferable(Data.self)
        → 整个文件 Data 写入 tmp/UUID.mov
        → importVideo(url:)

importVideo → AVAsset.load(.tracks)
            → 第一条视频轨道 naturalSize / nominalFrameRate
            → asset.duration → 更新界面

开始按钮 → Task → startProcessing
           → VideoProcessor().process(url:quality:progress:)
              → AVURLAsset / 第一条视频轨道
              → inspectColorInformation / detectRationalFrameRate
              → AVAssetReaderTrackOutput：要求 10-bit 4:2:2 V210
              → AVAssetWriter(.mov) + ProRes 视频 Input + Adaptor
              → startWriting / startSession(.zero) / startReading
              → 逐帧：检查格式尺寸、PTS、池分配、清除色彩附件
                 → Metal.submit → await Job.wait
                   失败或未初始化则 V210Converter.convert
                 → tagAsAppleLog
                 → 等待 writerInput 就绪 → adaptor.append
              → markAsFinished → finishWriting → 状态 completed
           → 更新 outputURL / frameCount / progress
           → ShareLink 分享临时 MOV
```

文件入口 `ContentView.swift:520` 直接使用返回 URL；没有安全作用域访问、文件协调或复制到沙盒。另一文件中的 `VideoPicker(asCopy:true)` 不在这条链中，不能据它推断当前导入已经复制。Apple 官方要求访问 fileImporter 返回的安全作用域 URL 时管理 start/stop 调用；当前 Files/iCloud/外部 provider 导入存在访问失败风险。[Apple fileImporter](https://developer.apple.com/documentation/SwiftUI/View/fileImporter%28isPresented%3AallowedContentTypes%3AonCompletion%3A%29)

相册入口 `ContentView.swift:674` 把完整视频载入 Data，大型 4K/ProRes 文件会带来文件大小级别的内存压力；同步 Data.write 还需在 Apple 平台确认执行位置及界面响应。固定 `.mov` 后缀不证明传入字节一定是 MOV。PhotosPicker 未设置保留原表示的策略，实际返回的 codec/metadata 是否保留需实测。

主处理器只创建视频 ReaderOutput 和 WriterInput，没有音频、timecode 或其它轨道的读取/写入。因此当前导出会遗漏输入音频，不保留其它轨道。已有 `preferredTransform` 被复制到 WriterInput，旋转信息通过轨道 transform 保留，不是 shader 旋转像素。

输出写到 `temporaryDirectory/sourceName_AppleLog_ProRes422HQ.mov` 或 `_ProRes422.mov`，同名重跑先删除旧输出。没有独立保存相册、fileExporter、自动持久化或临时文件清理。ShareLink 是用户发起分享。

## 4. AVFoundation / VideoToolbox 使用

主解码和编码使用 AVFoundation：异步加载 asset/track 属性，AVAssetReader 解码成 CVPixelBuffer，AVAssetWriterInputPixelBufferAdaptor 写入处理后的 CVPixelBuffer。没有直接创建 VTDecompressionSession 或 VTCompressionSession。是否硬件解码/编码由 Apple 系统路径决定，不能从当前源码认定。

Reader 和 Adaptor 都要求 `kCVPixelFormatType_422YpCbCr10`，启用 MetalCompatibility 与 IOSurfaceProperties，Reader 设置 alwaysCopiesSampleData=false。该请求规定处理中间格式，不证明源文件本来是 10-bit 4:2:2，也不会凭空增加源 8-bit/4:2:0 的有效信息。

VideoToolbox 的直接调用只在 `ProResEncoderDiagnostics`：

1. `VTCopyVideoEncoderList` 枚举 ProRes 422 / HQ、编码器名称及硬件标志。
2. `VTCopySupportedPropertyDictionaryForEncoder` 检查固定 3840×2160 配置。
3. 两个 AVAssetWriter.canApply 检查同分辨率设置。

诊断没有 startWriting、提交帧或完成实际编码，而且主界面没有调用它；结果只能代表基础能力查询。主转换流程则先 canApply，再执行实际写入，遇到失败会输出 writer error/underlying error 信息。

`VideoDecoder.swift:31` 使用 outputSettings=nil，但之后期望 CMSampleBufferGetImageBuffer 返回 pixel buffer。对于压缩视频，nil 意味着保持存储的原始样本，跳过解码；因此该辅助类对常见压缩 MOV 可能最终得到 noFrame。此问题当前不影响主链，但以后接入首帧功能前需修正。该行为由官方文档明确说明。[Apple outputSettings](https://developer.apple.com/documentation/avfoundation/avassetreadertrackoutput/outputsettings?changes=_7)

## 5. Metal 与 GPU 图像处理

`MetalColorProcessor` 初始化 default device、command queue、default library 和唯一 `logForgeV210Kernel` pipeline。shader 已列入应用 Sources，可以被 Xcode 编译进默认 Metal library。

每帧首先通过 CVPixelBufferLockBaseAddress 获取内存，失败时尝试 IOSurface lock。随后以 bytesNoCopy + storageModeShared 建立输入和输出 MTLBuffer。当前没有 MTLTexture、CVMetalTextureCache、Core Image、渲染 pipeline、LUT 或额外中间图像。

| kernel buffer | Swift 绑定 | Metal 类型 |
| --- | --- | --- |
| 0 | 输入 MTLBuffer | device const uint* |
| 1 | 输出 MTLBuffer | device uint* |
| 2 | width32 | constant uint& |
| 3 | height32 | constant uint& |
| 4 | inputStride32 | constant uint& |
| 5 | outputStride32 | constant uint& |

grid = `(ceil(width/6), height, 1)`；每个线程读取 16 字节，处理 6 像素。每行独立按输入/输出 stride 定位。V210 四个 UInt32 布局是：

```text
w0 = Cb0 | Y0<<10 | Cr0<<20
w1 = Y1  | Cb2<<10 | Y2<<20
w2 = Cr2 | Y3<<10 | Cb4<<20
w3 = Y4  | Cr4<<10 | Y5<<20
```

每个通道 mask 为 0x3ff；相邻两像素共享输入色度。转换后取每对输出 Cb/Cr 的平均，重新量化打包。末尾不足 6 像素时重复最后有效转换值来填充该组；两条实现都验证最小 stride 为 `ceil(width/6)*16`。

Job 用 NSLock 和 CheckedContinuation 处理 GPU 已完成/仍执行的等待，command buffer 完成时释放 pixel buffer 锁。主链每提交一帧就等待一帧，因此当前是串行帧处理，没有多帧 GPU/编码流水并行。

GPU 初始化失败即用 CPU；某帧 submit/wait 失败也用 CPU。但失败后只把 usingMetal=false，metalProcessor 本身仍存在，后续每帧仍会尝试 GPU。此状态不代表永久切换。初始化错误被 try? 隐去，帧级 GPU 失败也未进入返回 diagnostics，主界面仍显示启动 GPU 文案。

需要 Apple 平台重点验证的内存边界：

- bytesNoCopy 要求起始地址和区域页对齐且位于单个 VM 区域；当前只使用 baseAddress 和 stride*height，没有检查这些条件。不能在 Windows 断言该映射在所有系统分配的 buffer 上有效。[Apple makeBuffer](https://developer.apple.com/documentation/metal/mtldevice/makebuffer%28bytesnocopy%3Alength%3Aoptions%3Adeallocator%3A%29?changes=la)
- IOSurfaceLock 返回状态未检查；submit 自身未验证输出尺寸与输入一致，当前主调用者在外部做了验证。
- completion handler 弱引用 Job，cleanup 只在 Job.complete 执行；当前主调用者立即等待，未来若引入丢弃 Job/取消并发任务，必须保持完成回调与解锁的生命周期保证。
- CPU 和 GPU 顺序与常量一致，但 Float、GPU fast math、exp/log2 和量化可能产生差异，尚无逐像素执行对比。

## 6. Apple Log 色彩转换算法

实际生产实现位于 shader 和 V210Converter；独立 Double 辅助类当前没有被调用。

```text
10-bit V210
 → 视频范围解码：Y=(code-64)/876，Cb/Cr=(code-512)/896
 → BT.2020 非恒定亮度 YCbCr 转 RGB
 → 每通道 clamp 到 [0,1]
 → HLG inverse OETF
 → 线性值除以 0.26496256
 → Apple Log 分段 OETF
 → BT.2020 RGB 转 YCbCr
 → 每对像素色度平均
 → Y*876+64、Cb/Cr*896+512，round 并 clamp 到 [0,1023]
 → V210
```

HLG 反变换：`E<=0.5` 时 `L=E²/3`；否则 `L=(exp((E-c)/a)+b)/12`。常量 a=0.17883277，b=0.28466892，c=0.55991073。这里使用 scene-linear 反 OETF，没有显式应用显示 OOTF、显示峰值亮度或 PQ 路径。

`inverseOETF(0.75)=0.26496255978640015`，所以除以 0.26496256 将 75% HLG 映射到线性参考白 1。此标称参考白与 BT.2408 的制作参考相符，但不证明任意 iPhone 片段曝光或相机内部处理可以逆向恢复。[ITU-R BT.2408-6，§2.1、Table 1](https://www.itu.int/dms_pub/itu-r/opb/rep/R-REP-BT.2408-6-2023-PDF-E.pdf)

Apple Log 分段式及常量：

```text
R < R0：             P = 0
R0 <= R < Rt：       P = c*(R-R0)^2
R >= Rt：            P = gamma*log2(R+beta)+delta

R0=-0.05641088；Rt=0.01；c=47.28711236
beta=0.00964052；gamma=0.08550479；delta=0.69336945
```

常量与分段关系可由 Apple 提供给 ACES 的解码变换核对；工程的编码是该解码的代数逆。保持 BT.2020，而没有执行 ACES IDT 中后续的 BT.2020→ACES 矩阵步骤。[Apple vendor-supplied ACES IDT，固定 commit](https://github.com/aces-aswf/aces-core/blob/528c78fe2c0f4e7eb322581e98aba05de79466cb/transforms/ctl/idt/vendorSupplied/apple/IDT.Apple.AppleLog_BT2020.ctl)

BT.2020 正向矩阵：`Y=0.2627R+0.6780G+0.0593B`，`Cb=(B-Y)/1.8814`，`Cr=(R-Y)/1.4746`。反向 R=Y+1.4746Cr、B=Y+1.8814Cb，G 使用 0.16455/0.57135 系数。GPU 与 CPU fallback 一致；独立 Double ColorConverter 使用更精细的反向系数及展开的正向系数，并不与生产实现逐位一致。

Windows 用 Python Double 对源码公式进行独立计算，得到以下中性 RGB 点；它们不是已执行的 Swift/Metal 输出测量：

| 输入 HLG | 归一化线性 | Apple Log | 视频范围 10-bit Y 参考 |
| --- | --- | --- | --- |
| 0 | 0 | 0.1504764523 | 196 |
| 0.5 | 0.3145098437 | 0.5544015158 | 550 |
| 0.75 | 约 1 | 0.6945529830 | 672 |
| 1 | 3.7741182164 | 0.8575232646 | 815 |

另有线性 18% 灰：Apple Log 约 0.4882724585。8 个分段/参考点的编码解码回环误差最大约 1.78e-15，仅验证独立 Double 公式计算。

必须保留的算法边界：输入固定按视频范围解释，未读取 full-range 标记；生产实现会裁掉 RGB 负值和超过 1 的 HLG 分量，可能丢失超白或矩阵转换后的越界信息；未处理 chroma location，当前共享/平均色度并不验证输入/输出采样位置；HLG 已有的 tone mapping、降噪和压缩损失无法通过此变换恢复。以后改这些行为需先定义输入契约并测量，不能只更换一个常量。

## 7. Metadata 读取、写入与验证

输入读取在 `VideoProcessor.swift:1068`，从视频轨道 formatDescriptions 扩展取 ColorPrimaries、TransferFunction、YCbCrMatrix，并与 CoreVideo 常量字符串比较。要求 BT.2020 primaries + HLG transfer；无颜色信息、BT.709 或其他类型会拒绝。

已确认的逻辑缺口：`VideoProcessor.swift:1190` 在判断 primaries+HLG 后直接返回支持。matrix=nil 会产生警告，但非空且错误的 matrix 也未拒绝。例如 BT.2020 primaries + HLG + BT.709 matrix 会被当作 BT.2020 NCL 处理；后面的 BT.709 冲突分支不会被执行。源码中还生成了 bt2020Matrix 变量但未用于校验。

输出每帧在转换前清理 primaries、普通 transfer、matrix 和 log transfer；转换后设置：

- ColorPrimaries = ITU_R_2020
- YCbCrMatrix = ITU_R_2020
- LogTransferFunction = AppleLog
- 全部使用 shouldPropagate，移除普通 TransferFunction。

makeWriterSettings 只有 codec、width、height，没有 AVVideoColorPropertiesKey；也没有 writer.metadata / input.metadata、sourceFormatHint 或独立 MOV metadata 写入代码。不保留输入创建时间、位置、相机信息、HDR 动态信息、timecode 等。

帧附件是一层声明；最终编码格式描述符、MOV 落盘以及 Resolve/Final Cut 等软件识别是后续不同层。当前代码没有重读导出结果，也未提供样片，故不能宣称导出文件已被识别为 Apple Log，不能推断 Apple 私有 atom 的真实写法。

官方 CoreVideo/CoreMedia DocC 元数据已核对：此处使用的 LogTransferFunctionKey / AppleLog 标识最低 iOS 17.2，与工程部署目标一致。这只确认官方 API 版本关系，仍需实际 Apple SDK 编译。[CoreVideo log key](https://developer.apple.com/documentation/corevideo/kcvimagebufferlogtransferfunctionkey)、[CoreMedia log extension](https://developer.apple.com/documentation/coremedia/kcmformatdescriptionextension_logtransferfunction)

已有 OutputValidator 检查 ProRes、10-bit 4:2:2、BT.2020 primaries、Apple Log format extension、帧数与 PTS，但主链未调用它。它自身也有边界：

- passed 不比较 duration 与 expectedDuration。
- duration 计算为最后 PTS 减首 PTS，未包含最后一帧的 duration。
- ptsContinuous 仅拒绝下降，重复 PTS、无效 PTS 与较大间隔未被判失败。
- 没有像素变换、输出矩阵、range/chroma location、音频或编辑器导入验证。
- ffmpegAvailable 硬编码 false，不是探测结果。

## 8. ProRes / HEVC 编码路径

| 路径 | 当前实现状态 |
| --- | --- |
| ProRes 422 HQ 输出 | 主路径，默认选项；quality.codec=.proRes422HQ，MOV 容器 |
| ProRes 422 输出 | 主路径，显式选择；quality.codec=.proRes422，MOV 容器 |
| HQ 失败自动降到 422 | 没有；当前选择失败后返回错误 |
| HEVC 输出 | 没有 codec 枚举、writer 设置、Main10、码率或 VTCompressionSession 分支 |
| HEVC 输入 | 无专用实现或 codec 识别；满足颜色条件且系统 Reader 能解码成 V210 时可能走通用路径，尚未实测 |
| H.264 / PQ / SDR 输出 | 没有 |
| 直接 VideoToolbox 编码 | 没有；只有独立能力诊断 |

全工程 Swift 搜索 HEVC/.hevc、VTCompressionSession、VTDecompressionSession 均为 0。VideoAnalyzer 的 codecName 也只识别 ProRes 系列；HEVC 会显示 Unknown（该详细分析当前未连接 UI）。主 VideoProcessor 不检查源 codec 是否 ProRes，而是检查颜色和 Reader 实际输出。不要用未接入的 VideoInfo.isSupported 条件替代主链真实输入条件。

当前编码没有显式帧率、码率或压缩属性。尺寸使用源 naturalSize，保留 preferredTransform。Writer.canApply 不能证明设备支持所有分辨率或实际编码成功，主界面的“支持 iPhone 13 及以上”文字也没有对应设备能力检测。

## 9. 时间戳、并发与失败边界

保留有效且非负的源 PTS；无效值用 nextFallbackPTS。重复或倒退 PTS 被改成 lastPTS+fallbackFrameDuration。候选帧率包含 24000/1001、30000/1001、60000/1001、120000/1001 及整数 24/25/30/50/60/120，匹配来自 nominalFrameRate；其它率用 timescale=600 的回退间隔。

CFR 仅是诊断：计算相邻 PTS 与候选间隔差异，超过容差的比例低于 1% 视为 likely CFR。没有重采样，也不把全部输入强制转成 CFR。返回 warnings 未显示在 ContentView 中。

Writer session 从 0 开始，但合法首 PTS 未减去初始偏移；非零起点、VFR、坏时间戳及最后一帧时长须用合成片实测。返回 result.duration 是输入 asset 时长，而非重读输出时长。时间戳修复可能改变播放节奏。

界面导入按钮与质量 Picker 在处理时仍可交互；多个导入 Task 没有互斥、generation ID 或任务取消。用户在转换途中重新导入，会清空/改写当前 UI 状态，之前任务的回调仍可更新它；快速连续相册选择也可能让较早结果覆盖较新选择。同步像素处理的具体执行器位置还需实际 Swift 工具链核对，async 关键字本身不证明所有工作都不在主线程。

代码有 Task.checkCancellation，但界面没有保存 Task 句柄或取消入口；checkCancellation、CPU convert、sleep 抛错未全部纳入统一 reader/writer cancel 和残留文件清理。0 帧输入也没有专门的 processedFrames>0 检查。改取消功能前要先补全资源清理调用链。

## 10. 本轮验证结果与待验证项

机器证据位于同目录 `STATIC_AUDIT_2026-10-04.json`，包含原有 34 文件的 SHA-256、86 个 PBX 对象的解析结果、逐 target 源文件清单、语言键数量、shader 常量与公式参考点。

Windows 已完成：

- 解析 OpenStep PBX 工程并检查对象 ID，无悬空引用；按 group 解析源/资源路径，未发现缺失文件。
- 所有 19 Swift + 1 Metal 均列入对应 target，无漏列源码。
- 4 个 JSON、2 个 XML plist、scheme/workspace/breakpoints XML（合计 9 个结构化文件）可解析。
- 两种 Localizable.strings 各 23 键，无重复且集合相同。
- CPU/Metal 10 个核心常量相同，kernel 名称及 0–5 buffer 绑定对应；人工核对 pack/unpack、tail、stride 和数学顺序。
- PNG 头与图标尺寸检查；独立 Double 公式计算；全库查找实际调用、未连接模块和不存在的编码分支。

本机 PATH 未发现 swift/swiftc、xcodebuild、plutil 或 clang。本轮没有 Swift 语法/类型编译、Metal 编译、XCTest 或真实编码测试；结构化配置可解析不等于工程可成功构建。

Apple 平台后续验证矩阵：

| 验证 | 应覆盖内容 | 当前状态 |
| --- | --- | --- |
| Xcode 构建 | iOS 17.2 部署目标，Debug/Release，shader 默认库，全部辅助源码 | 未验证 |
| GPU/CPU 数值一致性 | 合成中性 ramp、饱和色、toe/knee、range、非 6 整倍数宽度、不同 stride | 未验证 |
| 导入 | Files/iCloud/provider、相册原表示、大体积输入、HEVC HLG、ProRes HLG、缺失/冲突 metadata | 未验证 |
| 编码 | 不同设备、分辨率和帧率，HQ/422，实际 append 和 finish | 未验证 |
| 时序 | 23.976/29.97/59.94、VFR、非零起点、重复/无效 PTS、帧数和末帧 duration | 未验证 |
| 资源/并发 | GPU 不可用、submit 失败、取消、反复导入、同名重跑、低内存及磁盘不足 | 未验证 |
| 输出一致性 | transform、颜色格式描述符、MOV metadata、音频与其它轨道需求 | 未验证；源码已确认只写视频 |
| 编辑器识别 | 实际生成文件在 Resolve/Final Cut 中的导入、自动色彩识别及像素解释 | 未验证 |

本轮不做功能删除或算法修复。后续修改应从对应真实调用链开始，在现有 Swift + Metal 工程中完成，并按上表区分静态检查和 Apple 平台验证。
