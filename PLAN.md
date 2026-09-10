# Android 移植实施计划

## 目标

在不依赖服务器、Python、GPU 或额外识别进程的前提下，将 Windows CPU 实时字幕方案移植到
Android 10+ ARM64 设备。应用采集允许共享的系统播放音频，以透明悬浮栏显示中英文字幕。

## 架构边界

数据链路为：

`AudioPlaybackCapture -> AudioRecord(16 kHz/mono/PCM16) -> native ring buffer ->`
`FSMN-VAD -> bounded segment queue -> SenseVoice -> Java callback -> overlay + transcript log`

Activity、ForegroundService、AudioRecord、JNI 工作线程和悬浮窗都使用默认应用进程。C++ 工作线程
属于该进程，不创建本地服务或子进程。

## 阶段和验收条件

### M1：可构建的 ARM64 识别核心

- 建立 Android Gradle/NDK 工程，锁定 SDK、NDK、Gradle 和 ggml 版本。
- 将平台无关的 SenseVoice、FSMN-VAD、Segmenter 和有界任务队列移入原生库。
- JNI 提供创建、异步加载、推送 PCM、停止和字幕回调。
- 主机单元测试覆盖句首、句尾、强制切句、临时任务合并和队列上限。
- `assembleDebug`、单元测试和 lint 通过。

### M2：系统音频采集与生命周期

- 通过 MediaProjection 用户授权创建 AudioPlaybackCaptureConfiguration。
- 前台服务先进入 foreground，再消费一次性 MediaProjection token。
- 采集回调不执行推理，只把 PCM 写入有界环形缓冲区。
- 处理系统撤销投屏、录音设备错误、停止通知和服务销毁。
- 无音频、目标应用禁止捕获和缓冲溢出均有可理解的状态提示。

### M3：透明字幕与模型管理

- TYPE_APPLICATION_OVERLAY 字幕栏支持透明度、字号和拖动。
- 设置页可导入两个 GGUF 模型并验证文件大小与 SHA256。
- 线程数和显示参数持久化；最终字幕保存为 TSV/SRT。
- 所有组件保持在一个应用进程中。

### M4：真机性能和准确率验收

- 至少测试一台 ARMv8.2+ 设备及一台较旧 ARM64 设备。
- 测量声音到字幕显示的 P50/P95、推理耗时、队列等待、CPU、内存、功耗和 30 分钟温升。
- 分别验证 YouTube App、Chrome、媒体播放器；记录哪些源应用禁止捕获。
- 使用真实英语、中文和中英混合材料计算英文 WER、中文 CER，并记录快语速漏词。
- 连续运行 30 分钟不崩溃、不出现无界内存增长，熄屏/锁屏后能正确停止并再次授权恢复。

## 当前交付判定

M1-M3 可以在没有手机时完成编译和自动测试。M4 必须连接 Android 10+ ARM64 真机并由用户批准
系统投屏和悬浮窗权限后才能判定。没有真机数据前，不宣称满足实时性能或准确率目标。
