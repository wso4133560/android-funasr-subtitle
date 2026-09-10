param([ValidateSet('Debug', 'Release')][string]$Configuration = 'Debug')

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
Remove-Item Env:HTTP_PROXY, Env:HTTPS_PROXY, Env:ALL_PROXY -ErrorAction SilentlyContinue

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
    & .\gradlew.bat --no-daemon "test${taskSuffix}UnitTest" "lint$taskSuffix" "assemble$taskSuffix"
    if ($LASTEXITCODE -ne 0) { throw "Gradle build failed with exit code $LASTEXITCODE" }
} finally {
    Pop-Location
}

$apkName = if ($Configuration -eq 'Release') { 'app-release-unsigned.apk' } else { 'app-debug.apk' }
$apk = Join-Path $root "app\build\outputs\apk\$($Configuration.ToLowerInvariant())\$apkName"
if (-not (Test-Path -LiteralPath $apk)) { throw "Expected APK was not created: $apk" }
Write-Host "Build passed: $apk"
