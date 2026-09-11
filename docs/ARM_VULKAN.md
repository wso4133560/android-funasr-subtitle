# ARM CPU / Vulkan 优化验证（2026-09-11）

当前安装版使用 ARM CPU，11 秒固定英文音频的平均推理时间由 1538.214 ms 降至
915.204 ms，本轮同机测量减少约 40.5%。Vulkan 已完成构建和安全拒绝不兼容设备的验证，
**没有获得本机 GPU 加速成功的结果**。本轮变更暂不提交和推送。

## 实现范围

- 通过构建参数选择 `armv8.2-a+dotprod+fp16`，启用这台手机支持的 dot-product / FP16 指令。
  默认值仍为 `armv8-a`；专用 APK 不支持缺少这些指令的旧 ARM64 CPU。
- SenseVoice、FSMN-VAD 分别保留线程池和计算缓冲，避免每个窗口重新创建工作线程和分配大缓冲。
  工作线程在窗口间休眠（`poll=0`）。计算图仍为每个完整音频窗口重新构建，未复用编码结果或删去语音。
- 取消临时推理后清除取消回调，下一次最终推理继续使用同一实例；窗口可缩短再增长。
- 可选编译 Vulkan 后端、显式基准后端选择、执行前算子支持检查。应用 JNI 的默认调用仍走 CPU。
- 修复 Windows 下 Vulkan 着色器生成器路径中的反斜杠问题；正常执行编译器探测，未伪造 ABI 或指令支持。

## 固定输入 CPU 测量

设备：22041211AC / MT6895Z/TCZA，Android 14/API 34。SenseVoice 4 线程、VAD 2 线程，
同一段 11 秒单声道 16 kHz PCM16 英文 WAV，每组 6 次。程序链接从对应 APK 解出的实际原生库。

| 构建 | 平均 ASR（ms） | P50（ms） | P95（ms） | 平均 1 秒 VAD（ms） |
| --- | ---: | ---: | ---: | ---: |
| 本轮优化前，通用 ARM64 CPU | 1538.214 | 1540.931 | 1548.293 | 9.681 |
| 启用 ARM 指令，不复用资源 | 1219.979 | 1218.197 | 1230.783 | 9.775 |
| ARM + 资源复用，含 Vulkan 库但使用 CPU | 945.322 | 935.440 | 985.392 | 24.265 |
| ARM + 资源复用，最终 CPU 安装包 | 915.204 | 911.403 | 943.364 | 25.022 |

分位数采用 nearest-rank；仅 6 次样本，P95 等于样本最大值。组间顺序运行，未固定 CPU 频率、
温度或文件缓存。含 Vulkan 构建和最终 CPU 构建的微小差异不能归因于 GPU，二者均使用 CPU。

VAD 在此测试中的耗时增加了约 15 ms，资源复用并非所有阶段均加速。保留线程池但让线程休眠可能
影响短任务的唤醒成本；这只是待进一步测量的解释。ASR 的总体收益和实际采集队列另行验证。

最终安装包 RTF 均值约 0.083（915 ms / 11 秒音频）。这表示固定窗口的解码速度，**不等于
声音到字幕的端到端延迟**，不含音频积累、VAD 断句、排队和屏幕实际呈现。

英文参考共 22 词，所有组的 6 次输出在转小写、去标点后完全匹配，WER 为 0。该结论只针对这一段
JFK 固定样本，不代表新闻或中英混合素材的整体准确率。模型权重及分段参数未在本轮改变。

最终安装库的取消测试耗时 52.168 ms，随后 6 次识别正常；同一实例依次处理 1、3、8、2 秒窗口，
再识别完整音频，文本保持一致。扩展的回归入口还检查 VAD 概率有效性以及变长窗口后的一致性。

结构化数据与原始日志路径见 [测量记录](benchmarks/2026-09-11-arm-vulkan.json)。
本节的“优化前”是本轮重新运行的约 1.54 秒 CPU 基线，不是早期 C 内核未优化的约 58 秒版本。

## Vulkan 真机边界

`adb shell cmd gpu vkjson` 显示：

| 属性 | 实测 |
| --- | --- |
| GPU | Mali-G610 MC6 |
| 实例 / loader Vulkan API | 1.3.0 |
| GPU 物理设备 Vulkan API | 1.1.177 |
| 当前锁定的 ggml 后端所用物理设备 API | 至少 1.2 |

只查看实例 API 会误判支持情况。原后端让这台物理设备进入 1.2 功能链，实际测试先后在缓冲地址查询
和队列提交处出现原生崩溃。本轮补丁在功能查询前检查物理设备 API，并让默认选择、回退选择和
`GGML_VK_VISIBLE_DEVICES=0` 显式选择都遵守该检查。

修复后，默认与显式选择测试均输出 `device Vulkan 1.1.177; 1.2 required`，以错误码 1 正常退出，
不再出现 SIGSEGV。CPU 仍可使用。**当前手机没有成功执行 Vulkan 转写，不能声称 GPU 性能提升。**
即使另一台手机支持 1.2，也仍需验证本模型的算子、数值、内存和速度，才能在应用中启用 GPU。

## 构建与复现

完整 CPU 构建（打包两个模型，执行主机 C++ 测试、lint、原生库及模型校验）：

```powershell
# 通用 ARM64
./scripts/build.ps1
# 仅用于已确认支持 dotprod / fp16 的设备
./scripts/build.ps1 -CpuArchitecture armv8.2-a+dotprod+fp16
```

用户安装包输出为 `app/build/outputs/apk/debug/FunASR-Subtitle-v0.1.0-arm64-v8a.apk`。
Debug 指调试签名和可调试配置，
原生 C / C++ 内核已使用 `-O3`。当前安装版约 250,040,685 字节，自动安装内置模型。

Vulkan 实验构建：

```powershell
# 指向包含 vulkan/vulkan.hpp、vulkan/vulkan_core.h 和 vk_video 的 include 目录
./scripts/prepare-vulkan.ps1 -HeadersRoot C:/msys64/ucrt64/include
./scripts/build.ps1 -Vulkan -CpuArchitecture armv8.2-a+dotprod+fp16
```

脚本也可自动发现 `VULKAN_SDK/Include`。C/C++ 头文件复制到本项目 `.android-vulkan`；
SPIR-V 头文件和 `glslc` 来自锁定的本地 NDK 27.2.12479018。
本次验证使用 Vulkan 头文件版本 341，具体 SHA256 保存在测量记录和本地依赖 manifest 中。
CPU 默认构建不需要这组 Vulkan 依赖。干净上游源的补丁正向应用与反向检查均已通过。

```powershell
./scripts/benchmark-device.ps1 -AudioPath D:/code/funasr/english-test.wav `
    -Threads 4 -Repeats 6 -Backend cpu -AbortAfterMs 50 -CheckLifecycle -Label cpu-check
# 需要先构建 Vulkan APK；当前测试手机预期安全拒绝，不会自动改用 CPU 冒充 GPU
./scripts/benchmark-device.ps1 -AudioPath D:/code/funasr/english-test.wav `
    -Threads 4 -Repeats 1 -Backend vulkan -Label vulkan-check
```

Java unit test 任务仍是 `NO-SOURCE`；实际执行了主机 C++ 测试、Android lint 和真机原生基准。
这台 Windows 环境中，沙箱内 Ninja 子进程曾挂起；使用正常本机构建权限后编译器检查和构建通过。

## 安装与真实播放

2026-09-11 已重新安装 CPU 优化 APK 并启动。应用自动解包两个模型，手机私有目录中的长度与 SHA256
均匹配模型清单；已启动系统音频捕获和透明悬浮窗。真实播放测试使用 YouTube 的 ABC 英文新闻。
本次真实播放的统计、运行时间及局限记录在同名结构化测量文件的 `live` 字段中。

当前版本仍待更长时间稳定性与功耗验收；本轮没有提交、推送或发布。
