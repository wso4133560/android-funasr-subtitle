param(
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Debug',
    [ValidateSet('armv8-a', 'armv8.2-a+dotprod+fp16')][string]$CpuArchitecture = 'armv8-a',
    [switch]$Vulkan
)

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$localSdk = Join-Path $root '.android-sdk'
$sdkRoot = if (Test-Path (Join-Path $localSdk 'platforms\android-35')) {
    $localSdk
} elseif ($env:ANDROID_HOME) {
    $env:ANDROID_HOME
} elseif ($env:ANDROID_SDK_ROOT) {
    $env:ANDROID_SDK_ROOT
} else {
    throw 'Android SDK not found. Run scripts/bootstrap.ps1 -AcceptAndroidLicenses first.'
}

$escapedSdk = $sdkRoot.Replace('\', '\\').Replace(':', '\:')
Set-Content -LiteralPath (Join-Path $root 'local.properties') -Value "sdk.dir=$escapedSdk" -Encoding ascii
$env:ANDROID_HOME = $sdkRoot
$env:ANDROID_SDK_ROOT = $sdkRoot
$env:GRADLE_USER_HOME = Join-Path $root '.gradle-home'
$env:ANDROID_USER_HOME = Join-Path $root '.android'
New-Item -ItemType Directory -Force $env:ANDROID_USER_HOME | Out-Null
Remove-Item Env:HTTP_PROXY, Env:HTTPS_PROXY, Env:ALL_PROXY -ErrorAction SilentlyContinue
& (Join-Path $PSScriptRoot 'apply-ggml-patches.ps1')
if ($Vulkan -and -not (Test-Path (Join-Path $root '.android-vulkan/manifest.json'))) {
    & (Join-Path $PSScriptRoot 'prepare-vulkan.ps1')
}

# 模型不进 Git，但完整 APK 必须自带模型。先把当前目录中已下载且校验过的模型
# staged 到 generated assets；Gradle 会把这些文件原样放入 APK，应用首次启动再复制
# 到可供 native mmap 的私有文件目录。
$modelRoot = Join-Path $root 'models'
$bundledModelRoot = Join-Path $root 'app\build\generated\bundled-assets\models'
$requiredModels = @('sensevoice-small-q8.gguf', 'fsmn-vad.gguf')
if (Test-Path -LiteralPath $bundledModelRoot) {
    Remove-Item -LiteralPath $bundledModelRoot -Recurse -Force
}
New-Item -ItemType Directory -Path $bundledModelRoot -Force | Out-Null
foreach ($modelName in $requiredModels) {
    $source = Join-Path $modelRoot $modelName
    if (-not (Test-Path -LiteralPath $source)) {
        throw "Missing model for bundled APK: $source. Run scripts/download-models.ps1 first."
    }
    Copy-Item -LiteralPath $source -Destination (Join-Path $bundledModelRoot $modelName)
}
Write-Host "Bundled models staged: $bundledModelRoot"

$hostBuild = Join-Path $root 'build\host-tests-make'
cmake -G 'MinGW Makefiles' -S (Join-Path $root 'tests') -B $hostBuild
if ($LASTEXITCODE -ne 0) { throw 'Host test configuration failed' }
cmake --build $hostBuild --config Release
if ($LASTEXITCODE -ne 0) { throw 'Host test build failed' }
ctest --test-dir $hostBuild -C Release --output-on-failure
if ($LASTEXITCODE -ne 0) { throw 'Host tests failed' }

$taskSuffix = if ($Configuration -eq 'Release') { 'Release' } else { 'Debug' }
Push-Location $root
try {
    $backendProperty = if ($Vulkan) { '-PfunasrVulkan=true' } else { '-PfunasrVulkan=false' }
    & .\gradlew.bat --no-daemon $backendProperty "-PfunasrArmArch=$CpuArchitecture" "test${taskSuffix}UnitTest" "lint$taskSuffix" "assemble$taskSuffix"
    if ($LASTEXITCODE -ne 0) { throw "Gradle build failed with exit code $LASTEXITCODE" }
} finally {
    Pop-Location
}

$apkName = if ($Configuration -eq 'Release') { 'app-release-unsigned.apk' } else { 'app-debug.apk' }
$apk = Join-Path $root "app\build\outputs\apk\$($Configuration.ToLowerInvariant())\$apkName"
if (-not (Test-Path -LiteralPath $apk)) { throw "Expected APK was not created: $apk" }
& (Join-Path $PSScriptRoot 'verify-native-build.ps1') -Configuration $Configuration -ApkPath $apk
$publishedApk = Join-Path (Split-Path $apk -Parent) 'FunASR-Subtitle-v0.1.0-arm64-v8a.apk'
Copy-Item -LiteralPath $apk -Destination $publishedApk -Force
Write-Host "Build passed: $apk"
Write-Host "User APK: $publishedApk"
