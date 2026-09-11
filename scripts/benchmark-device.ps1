param(
    [Parameter(Mandatory)][string]$AudioPath,
    [string]$ApkPath,
    [string]$Serial,
    [ValidateRange(1, 16)][int[]]$Threads = @(4),
    [ValidateRange(1, 100)][int]$Repeats = 3,
    [ValidatePattern('^[a-zA-Z0-9_-]+$')][string]$Label = (Get-Date -Format 'yyyyMMdd-HHmmss')
)

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
if (-not $ApkPath) { $ApkPath = Join-Path $root 'app/build/outputs/apk/debug/app-debug.apk' }
$ApkPath = (Resolve-Path -LiteralPath $ApkPath).Path
$AudioPath = (Resolve-Path -LiteralPath $AudioPath).Path
$sdk = Join-Path $root '.android-sdk'
if (-not (Test-Path -LiteralPath $sdk)) {
    $sdk = if ($env:ANDROID_HOME) { $env:ANDROID_HOME } else { $env:ANDROID_SDK_ROOT }
}
$adb = Join-Path $sdk 'platform-tools/adb.exe'
$compiler = Join-Path $sdk 'ndk/27.2.12479018/toolchains/llvm/prebuilt/windows-x86_64/bin/clang++.exe'
$deviceArgs = if ($Serial) { @('-s', $Serial) } else { @() }
function Invoke-Adb {
    & $adb @deviceArgs @args
    if ($LASTEXITCODE -ne 0) { throw "ADB failed: $args" }
}
Invoke-Adb get-state
$outDir = Join-Path $root "build/perf/$Label"
New-Item -ItemType Directory -Path $outDir -Force | Out-Null
Add-Type -AssemblyName System.IO.Compression.FileSystem
$zip = [System.IO.Compression.ZipFile]::OpenRead($ApkPath)
try {
    foreach ($name in @('libfunasr_engine.so', 'libc++_shared.so')) {
        $entry = $zip.GetEntry("lib/arm64-v8a/$name")
        if (-not $entry) { throw "APK missing $name" }
        [System.IO.Compression.ZipFileExtensions]::ExtractToFile($entry, (Join-Path $outDir $name), $true)
    }
} finally { $zip.Dispose() }
$runner = Join-Path $outDir 'device_benchmark'
& $compiler --target=aarch64-linux-android29 -O3 -std=c++20 "-I$root/app/src/main/cpp" `
    (Join-Path $root 'tests/device_benchmark.cpp') "-L$outDir" -lfunasr_engine '-Wl,-rpath,$ORIGIN' -o $runner
if ($LASTEXITCODE -ne 0) { throw 'Benchmark harness build failed.' }
$remote = "/data/local/tmp/funasr-perf/$Label"
Invoke-Adb shell "mkdir -p $remote"
foreach ($name in @('libfunasr_engine.so', 'libc++_shared.so', 'device_benchmark')) {
    Invoke-Adb push (Join-Path $outDir $name) "$remote/$name"
}
foreach ($name in @('sensevoice-small-q8.gguf', 'fsmn-vad.gguf')) {
    Invoke-Adb push --sync (Join-Path $root "models/$name") "/data/local/tmp/funasr-perf/$name"
}
Invoke-Adb push $AudioPath "$remote/input.wav"
Invoke-Adb shell "chmod 700 $remote/device_benchmark"
$manifest = [ordered]@{
    capturedAt = (Get-Date).ToString('o')
    apk = $ApkPath
    apkSha256 = (Get-FileHash -LiteralPath $ApkPath).Hash
    nativeSha256 = (Get-FileHash (Join-Path $outDir 'libfunasr_engine.so')).Hash
    audio = $AudioPath
    audioSha256 = (Get-FileHash -LiteralPath $AudioPath).Hash
    device = (Invoke-Adb shell getprop ro.product.model | Out-String).Trim()
    soc = (Invoke-Adb shell getprop ro.soc.model | Out-String).Trim()
    threads = $Threads
    repeats = $Repeats
}
$manifest | ConvertTo-Json | Set-Content (Join-Path $outDir 'manifest.json') -Encoding utf8
Write-Host 'For comparable results, stop subtitles/playback before running. This script does not stop other apps.'
foreach ($count in $Threads) {
    $command = "LD_LIBRARY_PATH=$remote $remote/device_benchmark /data/local/tmp/funasr-perf/sensevoice-small-q8.gguf /data/local/tmp/funasr-perf/fsmn-vad.gguf $remote/input.wav $count $Repeats"
    $log = Join-Path $outDir "threads-$count.txt"
    & $adb @deviceArgs shell $command > $log 2>&1
    if ($LASTEXITCODE -ne 0) { throw "Benchmark failed; see $log" }
    Get-Content -LiteralPath $log | Where-Object { $_ -match '^(load_ms|iteration|\d+\t)' }
}
Write-Host "Raw results and provenance: $outDir"
