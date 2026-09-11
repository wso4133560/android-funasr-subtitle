# Android FunASR Subtitle

Android 10 及以上设备上的离线系统音频实时字幕。应用使用
`AudioPlaybackCapture` 采集允许被捕获的媒体音频，在同一个应用进程内通过
FSMN-VAD 和 SenseVoice Small GGUF 模型完成 CPU 推理，并将结果显示在透明悬浮字幕栏中。

## 当前实现

- 单应用进程：Activity、前台服务、音频采集、JNI 推理线程和字幕悬浮层均在默认进程。
- Android 10+ 系统播放音频采集，支持媒体和游戏用途；源应用禁止捕获时无法取得音频。
- ARM64 NDK 构建，复用 Windows 版本的 SenseVoice、FSMN-VAD、分段和有界任务队列。
- 构建时把本机两个模型放入 APK，首次启动自动校验并安装到应用目录；模型二进制仍不提交到 Git。
- 可调 CPU 线程数、字幕字号和透明度；最终字幕保存到应用私有目录。
- 前台服务通知提供停止入口；屏幕锁定、系统撤销投屏或采集失败时主动释放资源。

详细范围、阶段和验收条件见 [实施计划](PLAN.md)。

## 构建环境

项目锁定以下版本：

- Android Gradle Plugin 8.7.3
- Gradle 8.10.2
- compileSdk / targetSdk 35，minSdk 29
- Android NDK 27.2.12479018
- llama.cpp / ggml commit `803b7fcae893e9caaee3921779628fef83ac0965`

初始化项目内依赖和 Android SDK：

```powershell
pwsh -File scripts/bootstrap.ps1 -AcceptAndroidLicenses
```

如果已经有 Android SDK，可以不运行 SDK 下载部分，设置 `ANDROID_HOME` 后直接构建。
模型可从网络下载到项目的 `models/` 目录，随后通过手机界面导入：

```powershell
pwsh -File scripts/download-models.ps1
```

构建与测试：

```powershell
pwsh -File scripts/build.ps1
```

APK 输出到 `app/build/outputs/apk/debug/app-debug.apk`。`scripts/build.ps1` 会从 `models/` 目录
把 `sensevoice-small-q8.gguf` 和 `fsmn-vad.gguf` 打包进 APK，首次启动时自动校验并安装，APK
预计约 260 MB，应用私有目录还需要约 260 MB 可用空间。若只直接执行 Gradle 而未运行构建脚本，
则不会生成内置模型资源。

## 使用

1. 安装 APK，打开应用并导入两个模型。
2. 选择 CPU 线程数、字幕字号和透明度。
3. 点击“开始字幕”，允许录音、悬浮窗和系统音频捕获权限。
4. 切换到视频或会议应用，字幕会显示在屏幕底部，可拖动调整位置。
5. 从应用或常驻通知停止字幕。

Android 只允许捕获目标应用明确允许共享的播放音频。DRM、通话、部分会议软件以及主动禁用
playback capture 的应用不会提供音频，这是平台限制，不是识别错误。

## 模型校验

| 文件 | SHA256 |
| --- | --- |
| `sensevoice-small-q8.gguf` | `4ae45c94422de949b387e2e0fb10d7e14e4c42c69db30c3444ecc7d4b844b7c5` |
| `fsmn-vad.gguf` | `1270f2559c495f4e7b6e739541151027d360761a3fda43fc147034f5719f5479` |

第三方来源和许可证见 [NOTICE](NOTICE.md)。
