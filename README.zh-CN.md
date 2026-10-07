# LogForge

[English](README.md) · [简体中文](README.zh-CN.md)

原生 **Windows 与 iOS** 视频转换应用，将 **BT.2020 HLG ProRes** 转换为 **Apple Log / BT.2020 ProRes**，用于统一调色工作流。

LogForge 使用公开的色彩数学改变像素。它无法恢复已裁切的高光、压死的阴影、被色调映射丢弃的细节或相机 ISP 已丢失的信息，也不能把经过处理的手机视频还原为原始传感器采集。

**Windows 1.3.0（26929A）· iOS 1.3.1（26107B）。** Windows 导出 ProRes 422 HQ；iOS 可选 ProRes 422 或 422 HQ。输出使用 **Apple Log / Rec.2020、视频范围（Video levels）**。识别字段曾在 **Windows 1.1.0、DaVinci Resolve Studio 20.3.2.9、DaVinci YRGB Color Managed** 下验证。Windows 的验证范围见 [VALIDATION](docs/VALIDATION.md)；这些历史证据不能当作新 iOS 输出的编辑器导入验证。这里使用 Apple Log，不是 Apple Log 2 / Apple Wide Gamut。见[原生参考及真实导入证据](docs/APPLE_LOG_IDENTIFICATION.md)。

## 平台

| | Windows | iOS |
| --- | --- | --- |
| 应用架构 | 原生 Win32 / C++ | 原生 Swift / SwiftUI / Metal |
| 系统 | Windows 10 22H2 / Windows 11 x64 | iOS 26 或更新版本 |
| 色彩处理 | CPU 或经过运行时验证的 NVIDIA CUDA | CPU 或经过运行时验证的 Apple Metal |
| ProRes 编解码 | CPU，通过已批准的外部 FFmpeg 工具 | 系统编解码，不可用时自动回退内嵌软件编解码 |
| 导出 | ProRes 422 HQ MOV | ProRes 422 / 422 HQ MOV |
| 界面语言 | 英语、简体中文 | 英语、简体中文、繁体中文、法语、西班牙语 |
| 分发 | Windows 便携 ZIP | iPhone arm64 IPA，安装前需要签名 |

两端均属于主 [LogForge 仓库](https://github.com/HongHuo0728/LogForge)。当前 Windows 为 1.3.0，iOS patch 为 1.3.1。可下载的文件以 [Releases](https://github.com/HongHuo0728/LogForge/releases/tag/v1.3.0) 实际上传的附件为准；准备好正式版配置不代表 IPA 已发布。目前没有 macOS 或 Linux 应用。

## iOS 1.3.1

- 原生透明液态玻璃界面、分层玻璃图标、辅助功能对比度适配及触感反馈。
- 从相册、文件或文件夹导入；选择导出目录、处理队列、取消任务、重试失败视频。
- 系统 ProRes 解码或编码不可用时自动回退内嵌软件编解码器；色彩处理可选择 CPU 或 Metal。
- 将支持的 **ProRes 422 / 422 HQ、BT.2020 HLG MOV** 转为 Apple Log。当前输入合同不接受 HEVC、PQ、SDR 或其他 ProRes profile。手机能否拍摄 ProRes 与能否通过软件转换是不同能力。
- 保留并独立验证帧时间、音频、辅助轨道和显示旋转。iOS 保留有效的可变帧时间；下文 Windows 的固定帧节奏要求仅适用于 Windows。
- 点击左上角语言按钮打开 Apple 系统中的本应用设置。系统提供首选语言选项时，可选择英语、简体中文、繁体中文、法语或西班牙语。
- 应用内显示 `1.3.1 (26107B)`；报告保留每次转换的版本、发行构建号及内部诊断信息。

正式附件上传后，从主仓库 Release 下载 iOS IPA 及匹配的 RelinkKit。构建流程生成的是**未签名 IPA**，安装前需使用自己的有效 Apple 签名和 provisioning profile。当前没有配置 App Store 或 TestFlight 发布。分发应用时，请保留对应源码、许可及重新链接材料。

云端构建使用主仓库 [Actions](https://github.com/HongHuo0728/LogForge/actions/workflows/build-ios.yml) 中的 **iOS build and tests**。模拟器测试通过后才会归档并打包 iPhone 应用。详见 [iOS 构建与安装指南](https://github.com/HongHuo0728/LogForge/blob/main/LogForgeMac/docs/GITHUB_BUILD.md)。

iOS 1.3.1 修复 AAC / 时码轨道保留差异，并改进开源许可页面导航。[本次发布说明](https://github.com/HongHuo0728/LogForge/blob/main/docs/RELEASE_1.3.1_IOS.md)区分新增回归测试、云端构建与真机验证范围。用户原始视频尚未提供用于回归测试，模拟器结果不能证明真机性能或编辑器识别。

![LogForge Windows 界面](docs/images/LogForge.png)

截图使用空工作区，不包含个人视频或相机画面。

## Windows：1.3.0 更新

- **分数帧率 CFR 兼容性：**在所有数据包边界验证共用的整数时间戳量化模型。生成的 309 帧回归素材重现了反馈中的 59.94/59.970888 情况及旧版 1.06 tick 拒绝。名义 FPS 不再否决已经验证的固定时钟；真实变速和损坏时间戳仍拒绝。见[时间合同](docs/CADENCE_1.3.0.md)。
- **明确的输入解释：**缺少色域、矩阵、范围或色度位置标签时，按公开默认值解释并警告。显式 BT.709 冲突默认拒绝，除非主动覆盖。CLI 探测、GUI 详情和报告区分声明、假设与覆盖。见[输入合同](docs/INPUT_CONTRACT_1.3.0.md)。
- **保留相机兼容性：**继续接受 iPhone 的辅助 mebx 轨道和缺少时间码的素材；保留 Blackmagic 音频偏移/PCM 复制及 float32 竖屏旋转。
- **可预测的队列输出：**预先检查所有文件名，使用稳定的 `_2`、`_3` 后缀避开冲突，并在详情中预览输入/输出对应关系。已有文件不覆盖。
- **准确的完成报告：**区分媒体验证和最终发布；即使编码媒体已验证，重命名失败的原因也会保留。
- **音频与恢复保护：**逐流计算复制音频载荷的哈希；失败清理和崩溃恢复使用文件身份。附加图片、数据、字幕轨道明确报告为省略；多个主要视频流拒绝。
- **CUDA 工程改进：**确定性设备排序及进程内验证缓存，绑定设备、驱动、kernel、构建和创意参数。色彩 kernel 与数值容差不变。缓冲报告区分单个槽位、总桥接内存与 GPU 分配。
- **严格 CLI 整数解析：**工具发现前拒绝尾随垃圾字符、溢出、空值及非正的 `--cancel-after-frames`。

见 [1.3.0 发布详情](docs/RELEASE_1.3.0.md)和[验证记录](docs/VALIDATION.md)。`AcademicPapers` 中的双语论文已更新到 Windows 1.3.0，区分当前发布证据与历史测量。

## 之前的版本：1.2.1

- **竖屏方向：**编码前直接对 float32 像素应用正交旋转。旋转标记为 90 度的 3840×2160 输入变为 2160×3840、单位方向的输出，播放不再依赖旋转标签支持。
- **简化相机准入：**要求 ProRes Standard/HQ 与 HLG；不要求时间码或辅助元数据。FFmpeg 解释 edit list，不按相机模式白名单拒绝；iPhone `mebx` 元数据轨道省略。
- **音频同步：**保留源电影时钟并直接复制音频，包括 Blackmagic Cam 起始偏移；保留可用的源时间码与创建元数据。
- **文档纠正：**1.2.0 引入的 CUDA 色彩处理和多文件队列继续保留；Windows 的 ProRes 解码/编码仍运行于 CPU。

见 [1.2.1 发布详情](docs/RELEASE_1.2.1.md)和[最小回归结果](docs/VALIDATION.md)。色彩数学、容差及其他功能不变。

## 之前的版本：1.2.0

- **竖屏 MOV 修复：**所有支持的旋转下保留电影/视频/音频创建时间与源 creationdate，按含义比较时间戳；重封装时保留丢帧时间码。
- **媒体保护：**明确 QuickTime 容器与 edit-list 策略，验证章节，拒绝不支持的显示矩阵，结构化写入识别字段，并检查首帧、中间帧、末帧的像素。
- **处理后端：**Auto、CPU、NVIDIA RTX CUDA。GPU 加速精确色彩数学，Windows ProRes 解码/编码仍在 CPU。Auto 在 GPU 失败后回退，强制 CUDA 明确失败。
- **CPU 管线测量：**自适应线程分配和三个受限 Standard 缓冲区重叠解码、转换、编码；报告 CPU、色彩、管道等待、重封装、元数据、验证和 GPU 拷贝/kernel 时间。见[基准](docs/BENCHMARK_1.2.0.md)。
- **批量转换：**选择/拖入多个视频，每个视频单独验证并报告；失败项不阻止后续项。
- **主窗口布局：**使用测量后的可滚动布局划分输入/输出/状态/页脚，状态文本只渲染一次，重新布局时完整失效重绘。
- **存储可靠性：**跨进程设置/信任锁，按所有权恢复遗留临时文件，限制保留数量，预检查报告目录；报告失败时不发布输出。
- **发布完整性：**ZIP 附带真实 `.zip.sha256`，打包审计检查内容和可执行文件身份。

见 [1.2.0 发布详情及限制](docs/RELEASE_1.2.0.md)。Apple Log/HLG 方程、BT.2408 缩放、float32 中间值、BT.2020、nclc 9/2/9、视频范围、包时间节奏检查及色彩容差不变。

## 之前的版本：1.1.1

- **快速启动 FFmpeg 发现：**Quick 共享三秒文件系统预算；已验证的托管/保存工具可避免后续搜索。其他来源包括 PATH、Windows App Paths、软件包安装、受限常见目录和现有 Windows Search 索引。
- **深度搜索必须主动触发：**按需使用 **Deep search all drives** 或 `LogForge-cli --detect --deep-search`；启动不会自动遍历全盘。
- **直接审查候选：**选择发现的路径，检查真实工具对及哈希，批准后才运行。缺失探测器、文件变化和不兼容均保留原因；取消或失败后恢复下载/手动/重试操作。
- **受监督的发现：**可能阻塞的文件系统/索引查询在 LogForge 隐藏辅助模式运行，受 Job Object 取消与期限控制；分别报告发现和验证时间，仍执行哈希与数值验证。
- **可靠性修复：**后代进程占用管道时仍可取消；输出过量或读取异常安全失败；修复混合大小写 MOV 扩展名、数值元数据范围、临时信任文件清理和缓冲分配。
- **回归审计：**见[已确认缺陷及限制](docs/BUG_AUDIT_1.1.1.md)和[实际验证](docs/VALIDATION.md)。Apple Log/HLG、参考缩放、数值容差和 Resolve 识别写入不变。

## 之前的版本：1.1.0

**1.1.0（26923C）**包含从 29.99 fps CFR 修复开始的全部改动及已验证的 Resolve Apple Log 识别。Apple Log 方程、逆 HLG OETF 与 BT.2408 缩放保持不变；完整范围见 [CHANGELOG](CHANGELOG.md)。

- **误判 VFR 修复：**平均/名义速率标签不同时，接受经过验证的 29.99、29.98、29.9701 fps 固定节奏。逐包检查 PTS、时长、间隔和累计相位；真实 VFR、间断、重复/倒退和持续漂移以包级原因拒绝。
- **安全发现 FFmpeg：**只发现路径，不执行未知程序；必须使用验证过的托管下载或主动批准的工具。批准绑定两个工具的路径和 SHA-256，文件变化后需重新批准。执行锁定规范工具对，避免别名旁未批准 ffprobe。
- **数值资格检查：**小型参考信号验证矩阵/范围、左/中央色度相位及 ProRes 编解码。只有 **Verified FFmpeg** 可以转换；**Compatible but unverified FFmpeg** 只允许检查。
- **元数据白名单：**容器、视频、音频遵循安全复制策略；可保留源创建时间、真实相机品牌/型号及时间码；移除并报告冲突的 HLG/HDR/Dolby/PQ/自定义 gamma。
- **明确色度处理：**向转换滤镜传递左/中央位置；该历史版本缺失位置时要求用户声明，不支持的位置拒绝，输出验证记录声明及数值相位证据。
- **参考 MOV 分析：**解析 `meta/keys/ilst/mdta/data` 的键名和类型值，检查 ProRes 样本条目扩展，通过 `--analyze A B` 比较语义元数据和流参数，不以文件大小或字节偏移作证。
- **Resolve 识别验证：**编码后由独立写入器增加原生参考中的 `logs` 标识；真实 Resolve A/B 导入确立最小字段。色彩数学、nclc 9/2/9、视频范围、编码器身份和原始相机元数据不变；正确解析原生视频描述终止字段。
- **低内存并行 CPU：**标准路径通过持久 worker 流式处理最多 4 MiB 的 float 样本；创意路径并行 RGB tile；与标量实现测试逐值 float 相等。
- **输出验证：**JSON 包含平均/名义速率、节奏、最大时间误差、FFmpeg 身份/信任、色度证据及保留/移除元数据；发布时拒绝转换过程中出现的目标文件。
- **回归覆盖：**时间边界、未知程序不执行、哈希变化、元数据解析/冲突、色度位置、丢帧时间码、多音频流、180/270 度旋转、编码后数值抽样和 240 帧 4K120 转换。

自动识别曾使用未修改的 iPhone 15 Pro Max / Blackmagic Camera 原片、LogForge 负对照、隔离元数据候选及真实转换输出进行检查。不伪造相机型号或 Apple 编码器。其他 Resolve 版本/版本类型、Premiere 和 Final Cut 未验证。见[元数据合同](docs/METADATA.md)及[验证记录](docs/VALIDATION.md)。

## Windows 功能

- 原生 Win32 GUI、文件对话框、Unicode 路径和拖放。
- 有限 Quick 工具发现及手动 Deep 搜索；外部工具运行前明确批准路径/哈希，提供锁定版本 HTTPS 安装器。
- FFmpeg 数值验证：矩阵、范围、左/中央色度相位、ProRes 编码后码值。
- 英语（默认）与简体中文，深色（默认）/浅色主题，设置持久化。
- 原创光谱与曲线图标；Windows 资源版本 1.3.0.0，应用显示 1.3.0 (26929A)。
- 运行时验证 CPU/CUDA、串行多文件队列及详细单任务报告。
- 双精度 Apple Log 和逆 HLG 数学，32 位 float RGB 传输。
- ProRes HQ 10-bit 4:2:2 MOV，复制音频包，保留帧率与图像规模。
- 时间码、创建元数据、真实 Make/Model 与方向处理。
- 实际 FFmpeg 帧/时间进度、取消、子进程监督。
- 自动输出检查、本地诊断与 JSON 报告。
- 可选阴影提升、高光压缩和饱和度调节，独立开关；调节后采用不变的 Apple Log 编码。
- 逐帧逐分量记录信号范围，警告 Apple Log 底部裁切及超过名义白的信号。
- 开发者 CLI 和 MOV 原子/元数据对照工具。

## Windows 安装

1. 发布后从 GitHub Releases 下载 `LogForge-1.3.0-Windows-x64.zip` 及 `.sha256`。
2. 解压并运行 `LogForge.exe`，无需安装器或管理员权限。
3. 等待发现 FFmpeg 路径；未批准程序不会执行。用 **Review FFmpeg...** 选择候选，审查两个 SHA-256 并主动批准。没有可用工具时可重试 Quick、主动启动 Deep、手动选文件或使用 **Download FFmpeg** 下载锁定版本。数值验证通过后相关按钮消失；旧保存路径不等于执行批准。
4. 打开或拖入支持的视频。此 ProRes 相机流程缺失色度位置时默认左侧，CLI 可显式覆盖。单文件选择新的 `.mov`，队列选择输出目录，点击 **Convert to Apple Log**。
5. 等待验证与最终发布；不覆盖已有输出。成功重命名后若最终报告更新失败，保留有效视频并报告错误。两个文件事务的限制见发布说明。

Windows 可执行文件未签名，系统可能提示未知发布者。Release 使用静态 MSVC runtime（`/MT`），LogForge 不另需 VC++ runtime。Windows 目标为 Windows 10 22H2 / Windows 11 x64，不支持 Windows ARM64。iOS arm64 应用单独以 IPA 分发。

源代码构建也生成 `LogForge-cli.exe`，便携 GUI 不要求使用此开发工具。

曝光默认保持 **0 EV**；正偏移在 Apple Log 编码前提升场景曝光，不修复未知相机渲染，也不证明与原生相机匹配。CLI 例如用 `--exposure-ev 1` 增加一档。

## Windows 设置

点击右上角 **Settings**：

- **Language：**英语或简体中文；首启默认英语，不跟随 Windows 显示语言。
- **Appearance：**Dark 或 Light；首启默认深色。
- **Processing backend：**Auto（默认）、CPU 或 NVIDIA RTX CUDA。Auto 尝试可用 CUDA，失败后记录原因并回退；强制 CUDA 在不可用/未通过验证时失败。使用安装的 NVIDIA 驱动，无需分发 CUDA runtime 或 Toolkit。
- **Creative adjustments：**默认关闭；启用后可提升阴影、压缩高光、调整饱和度，初始 **3 EV / 1 EV / 85%**；关闭后保留详细参数。

**Save** 即刻应用并保存到下次启动；**Cancel** 丢弃改动。发现、下载、探测和转换时设置不可操作。系统文件选择器和错误文字可能使用 Windows 语言。

创意调节是 Apple Log 编码前的主动调色，不是原生相机外观匹配。关闭恢复标准转换，见[方程与限制](docs/CREATIVE_ADJUSTMENTS.md)。曝光仍在主窗口独立控制。状态、错误、进度、许可和本地处理说明在左下；录制要求与编辑器设置在右下。

## Windows 支持的输入

| 属性 | Windows 要求 |
| --- | --- |
| 容器 | 非分片 QuickTime MOV，`qt  ` 主品牌，或无品牌的旧 MOV |
| 编码 | ProRes 422（Standard）或 ProRes 422 HQ |
| 像素 | `yuv422p10le`，10-bit 4:2:2 |
| 色域/矩阵解释 | BT.2020 / BT.2020 NCL；缺失时假定并警告，显式冲突需主动覆盖 |
| 传递函数 | 显式 HLG（`arib-std-b67`） |
| 范围 | 声明 full/limited；未指定时假定 limited 并警告，其他明确标签拒绝 |
| 时间 | 单主要视频流，逐包 PTS、时长、间隔和累计相位验证固定节奏 |
| 色度位置 | 源声明或显式覆盖，缺失默认左侧 |
| 分辨率 | 偶数宽，最多 8192×8192，不缩放 |
| 帧率 | 有效有理帧率，最多 120 fps |

录制目标机型列举为 **iPhone 13 Pro、13 Pro Max、14 Pro、14 Pro Max**。请录制 **ProRes 422 HDR 或 ProRes 422 HQ HDR（10-bit BT.2020 HLG）**。上述限制内可选择分辨率/帧率，不强制 4K 或 24 fps；准入看实际媒体参数，不按机型白名单。Windows 拒绝 VFR、HEVC/Dolby Vision、PQ、SDR、ProRes LT/Proxy/4444、缺失或非 HLG 传递标签。面向逐行相机素材，无自动去隔行。缺失辅助色彩标签按上述解释。章节复制并验证，非必要元数据/数据轨道移除并报告；正交旋转应用于 float32 像素，90/270 度交换宽高，不缩放；其他显示矩阵保留为元数据。Edit list 形态、缺少时间码、辅助元数据不阻止准入。FFmpeg 解释视频/音频播放时间轴；MOV 支持的额外音频流直接复制，否则明确失败。

完全相同的包时长/间隔可建立准确有理帧率；非均匀 floor/ceil 时间戳须在整个序列共享一个量化单元，包括末包端点。名义/平均标签不能覆盖数据包证据；变速漂移、间断、重复/倒退会以 time base、微秒和帧比例诊断拒绝。float 管线归一化时间戳量化，不保留任意 VFR 时间戳。见[时间合同及限制](docs/CADENCE_1.3.0.md)。

CLI `--force-bt2020-interpretation` 只覆盖已知错误的色域/矩阵声明，不做色域转换，实际 BT.709 素材使用它会产生错误颜色。GUI 单文件提供默认拒绝的确认；不能绕过 PQ/非 HLG、色度/范围、codec/profile 或容器要求。GUI 批量任务每项正常准入，CLI 标志只用于指定作业/批次。

## Windows 输出及 Apple Log 工作流

- QuickTime MOV、ProRes `apch` / 422 HQ、10-bit 4:2:2、BT.2020。
- 保留像素规模、有理帧率、帧数；90/270 度旋转交换宽高；音频不重编码。
- Apple Log RGB 映射到视频范围 YCbCr：10-bit 名义 Y 64–940、C 64–960。应用 Apple Log LUT 前先正确归一化。
- `colr` 为 `nclc 9 / 2 / 9`：BT.2020 / 未指定传递函数 / BT.2020 矩阵。`2` 本身不是 Apple Log 标识；`logforge.transfer=Apple Log` 只记录意图，不是 Apple 私有字段。
- ProRes 样本条目另含 `logs = com.apple.rec2020.apple-log`，来自相机原片观察并经 Resolve 导入独立验证。
- 在已测试的 **DaVinci YRGB Color Managed** 中自动识别 **Apple Log（Rec.2020）**；真实渲染中 Auto 数据范围与手动 Video 一致。只使用一次技术输入转换，避免管理转换后重复增加 CST 或显示 LUT。
- 非色彩管理的 DaVinci YRGB 仍需主动设置观看变换，例如 CST 输入 Rec.2020 / Apple Log。识别与显示转换不同；其他编辑器/版本需另行验证。

**曝光参考：**按 BT.2408 名义参考，75% 归一化 HLG 解释为 100% 场景反射率。0 EV 时，18% 灰对应 HLG 0.378259 → Apple Log 0.488272；这不证明手机原始测光或 ISP。曝光只用于主动调节，默认零。标准路径使用逆 OETF 和场景线性增益，不应用显示 OOTF、色调映射、饱和度变化或色域转换；创意渲染只有显式启用时应用并记录。见[色彩数学与监测](docs/COLOR_PIPELINE.md)。

## Windows FFmpeg 工具

Windows ZIP 不含 FFmpeg 二进制。Quick 搜索托管/保存路径、应用旁工具、PATH、App Paths、实际 WinGet/Scoop/Chocolatey 安装、有限常见目录和现有 Windows Search 索引，不自动全盘搜索。Deep 是主动操作，只发现未知路径不运行。仅锁定版本且 SHA-256 通过的下载或用户明确批准的工具对可执行；批准保存 ffmpeg 与 ffprobe 的规范路径和哈希，变化需重新批准。使用时通过禁止写入/删除句柄保护镜像。磁盘遍历排除网络共享、不可访问/离线目录和 reparse 子目录，报告跳过项。

兼容功能清单不足以授权转换。**Verified FFmpeg** 还要通过整数信号矩阵/范围、半像素色度和 ProRes HQ 编解码抽样。**Compatible but unverified FFmpeg** 可检查但不能转换；数值失败是错误，不降级为警告。见[信任和提供者说明](docs/FFMPEG_PROVIDER.md)。

Standard 用三个各不超过 4 MiB 的 chunk 重叠 CPU 解码、色彩和编码；Creative 保留一帧 planar float32，保持 RGB 对应。CPU worker 使用不变标量方程。CUDA 使用精确 double 表达式和 float32 传输，须通过独立参考验证。无 GPU 仍可运行 CPU。Windows 不使用 fast math、FP16 或 LUT 近似。

安装器锁定 **Gyan.dev FFmpeg 8.1.2 essentials**，第三方 Windows **GPLv3** 构建，109,728,040 字节（约 105 MiB）。提供 HTTPS、内嵌 SHA-256、有限重试、字节进度、解压和安装后能力检查；不改 PATH，不安装系统组件。保存于：

```text
%LOCALAPPDATA%\LogForge\tools\ffmpeg\ffmpeg-8.1.2-essentials_build\
```

提供者接口允许未来受控更换版本/来源，不使用浮动 latest。见[提供者及许可](docs/FFMPEG_PROVIDER.md)。FFmpeg.org 提供源码与第三方 Windows 构建链接，不发布这里的二进制。

## 从源码构建 Windows

要求 Visual Studio 2022 C++ 桌面工作负载、Windows SDK 10.0.22621 或更新、CMake 3.24+、Git。Python 3 仅用于可选生成媒体集成测试。nlohmann/json 已随源码提供，配置/构建不下载依赖。

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
.\build\Release\LogForge.exe
```

完整集成测试需自行选择可信 FFmpeg，测试在隔离配置中主动批准所选工具对，不批准任意扫描结果。测试生成信号，包括 240 帧 4K120，需要数分钟及磁盘空间：

```powershell
cmake -S . -B build -DLOGFORGE_TEST_FFMPEG="C:/ffmpeg/bin/ffmpeg.exe"
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

生成便携包：

```powershell
cpack --config build/CPackConfig.cmake -C Release -B dist
python tests/package_audit.py --zip dist/LogForge-1.3.0-Windows-x64.zip --exe build/Release/LogForge.exe
```

Push 和 PR 在 Windows 配置、构建、测试，每次生成 ZIP；版本 tag 必须与 CMake 一致，工作流不会自动发布 GitHub Release。见[验证](docs/VALIDATION.md)和[架构](docs/ARCHITECTURE.md)。

在交互式 Windows 桌面运行原生设置/主题/语言检查：

```powershell
python tests/gui_smoke.py --exe build/Release/LogForge.exe --ffmpeg C:/ffmpeg/bin/ffmpeg.exe --work build/gui-check
```

集成测试生成素材后，可增加 `--input build/integration/standard.mov` 检查 Standard/Creative 的真实拖放转换。测试只捕获 LogForge 窗口，使用隔离 `LOGFORGE_DATA_DIR`。注入缺少 FFmpeg 的 UI 情况不替代真实工具发现/能力检查，不需要个人录像。

## Windows 本地诊断及参考分析

日志、设置、临时文件及验证 JSON 保存于 `%LOCALAPPDATA%\LogForge`，不上传。日志含文件名、命令和媒体属性，分享前请检查。可用 `LOGFORGE_DATA_DIR` 指定隔离测试目录。

```powershell
.\build\Release\LogForge-cli.exe --detect
.\build\Release\LogForge-cli.exe --probe "input.mov"
.\build\Release\LogForge-cli.exe --convert "input.mov" "output_AppleLog.mov"
.\build\Release\LogForge-cli.exe --analyze "output_AppleLog.mov" "real_AppleLog_reference.mov" > comparison.json
```

CLI 还提供 `--version`、`--help`、`--language en|zh-CN`，默认英语。用 `--ffmpeg "C:\path\ffmpeg.exe"` 选择已批准工具；`--install-ffmpeg` 使用同一安装器。分析包括原始 atom/probe 及解析后的语义元数据/流参数差异，不猜未知私有字段含义，不声称二进制等价。

## Windows 限制与后续验证

- 历史修复曾使用私有 iPhone HDR 视频；派生素材已移除，不进入源码/包。常规回归使用生成媒体；Resolve 元数据验证的公开相机参考也不打包。
- 不声称原生传感器动态范围、裁切恢复、Apple 编码器身份、相机认证或精确 iPhone MOV 原子等价。
- 已验证 Resolve Studio 20.3.2.9 管理导入；Premiere、Final Cut、其他 Resolve 版本及原生相机外观匹配需另验。
- CPU、可选运行时验证 NVIDIA CUDA 及串行队列可用；Windows ProRes 解码/编码仍在 CPU。没有预览播放器或 HDR 显示管线。
- 大型/长时素材、少见 edit list、特殊音频布局需增加覆盖；必要保留检查不一致时验证失败。

优先在其他 Resolve、Premiere、Final Cut 重复受控导入，获得配对 HLG/原生 Log 拍摄以比较曝光和相机渲染。

## Windows 开发者 CLI 批准及分析

```powershell
.\build\Release\LogForge-cli.exe --approve-ffmpeg --ffmpeg C:\ffmpeg\bin\ffmpeg.exe
.\build\Release\LogForge-cli.exe --detect --ffmpeg C:\ffmpeg\bin\ffmpeg.exe
.\build\Release\LogForge-cli.exe --convert input.mov output.mov --ffmpeg C:\ffmpeg\bin\ffmpeg.exe --input-chroma-location left
.\build\Release\LogForge-cli.exe --analyze output.mov reference.mov --ffmpeg C:\ffmpeg\bin\ffmpeg.exe
```

`--approve-ffmpeg` 是明确执行授权，先审查文件。ffprobe 已报告支持的位置时省略 `--input-chroma-location`；覆盖用于已知但无标签来源，不是猜测。`--check-compatible --ffmpeg PATH` 只检查能力，不证明数值正确。分析解析键索引、值、ProRes 扩展，语义比较忽略 `mdat` 偏移和文件大小。不附带 Apple 原生样本。

## 许可与商标

LogForge 源码采用 [MIT](LICENSE)，见[第三方声明](THIRD_PARTY_NOTICES.md)。Windows 单独获取外部 GPL FFmpeg；iOS 静态链接独立许可的 LGPL-only FFmpeg 子集，随分发提供对应源码和重新链接材料。第三方组件保留自身许可，不因 LogForge MIT 而重新许可。

LogForge 是独立开源项目，与 Apple Inc. 没有隶属或背书关系。

Apple、ProRes、Apple Log 属于各自权利人的商标，本项目不使用 Apple 标志。软件不附带保证，请按实际交付要求评估。MIT 不授予专利或商标权。
