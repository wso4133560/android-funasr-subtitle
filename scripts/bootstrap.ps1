param(
    [switch]$AcceptAndroidLicenses,
    [switch]$SkipAndroidSdk
)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
$root = Split-Path $PSScriptRoot -Parent
$llamaCommit = '803b7fcae893e9caaee3921779628fef83ac0965'
$llamaSha256 = '8431e10c4df5877dfc9a0fb6ffe1e88bf4f1f96d93b5d112e90556281584452a'
$commandLineToolsRevision = '13114758'
$commandLineToolsSha256 = '98b565cb657b012dae6794cefc0f66ae1efb4690c699b78a614b4a6a3505b003'
$sdkRoot = Join-Path $root '.android-sdk'
$thirdParty = Join-Path $root 'third_party'

function Invoke-Download([string]$Uri, [string]$Destination) {
    $parameters = @{ Uri = $Uri; OutFile = $Destination }
    if ((Get-Command Invoke-WebRequest).Parameters.ContainsKey('NoProxy')) {
        $parameters.NoProxy = $true
    }
    Invoke-WebRequest @parameters
}

New-Item -ItemType Directory -Force -Path $thirdParty | Out-Null
$llamaDirectory = Join-Path $thirdParty "llama.cpp-$llamaCommit"
if (-not (Test-Path (Join-Path $llamaDirectory 'ggml\CMakeLists.txt'))) {
    $archive = Join-Path $thirdParty 'llama.cpp.zip'
    Write-Host 'Downloading pinned ggml source...'
    Invoke-Download "https://codeload.github.com/ggml-org/llama.cpp/zip/$llamaCommit" $archive
    $actual = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($actual -ne $llamaSha256) {
        Remove-Item -LiteralPath $archive -Force
        throw "llama.cpp archive hash mismatch. Expected $llamaSha256, got $actual"
    }
    Expand-Archive -LiteralPath $archive -DestinationPath $thirdParty -Force
    Remove-Item -LiteralPath $archive -Force
}

& (Join-Path $PSScriptRoot 'apply-ggml-patches.ps1')

if ($SkipAndroidSdk) {
    Write-Host 'Pinned ggml source is ready; Android SDK installation was skipped.'
    exit 0
}

# The local Codex shell may inject a deliberately unreachable proxy. sdkmanager reads these
# variables itself, so remove them only in this child process before contacting Google.
Remove-Item Env:HTTP_PROXY, Env:HTTPS_PROXY, Env:ALL_PROXY -ErrorAction SilentlyContinue

$sdkManager = Join-Path $sdkRoot 'cmdline-tools\latest\bin\sdkmanager.bat'
if (-not (Test-Path -LiteralPath $sdkManager)) {
    $toolsArchive = Join-Path $root 'commandlinetools.zip'
    $extractDirectory = Join-Path $root '.commandlinetools-extract'
    if (-not (Test-Path -LiteralPath $toolsArchive)) {
        Write-Host 'Downloading Android command-line tools...'
        Invoke-Download "https://dl.google.com/android/repository/commandlinetools-win-${commandLineToolsRevision}_latest.zip" $toolsArchive
    }
    $actual = (Get-FileHash -LiteralPath $toolsArchive -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($actual -ne $commandLineToolsSha256) {
        Remove-Item -LiteralPath $toolsArchive -Force
        throw "Android command-line tools hash mismatch. Expected $commandLineToolsSha256, got $actual"
    }
    Remove-Item -LiteralPath $extractDirectory -Recurse -Force -ErrorAction SilentlyContinue
    Expand-Archive -LiteralPath $toolsArchive -DestinationPath $extractDirectory -Force
    $latest = Join-Path $sdkRoot 'cmdline-tools\latest'
    New-Item -ItemType Directory -Force -Path $latest | Out-Null
    Copy-Item -Path (Join-Path $extractDirectory 'cmdline-tools\*') -Destination $latest -Recurse -Force
    Remove-Item -LiteralPath $extractDirectory -Recurse -Force
    Remove-Item -LiteralPath $toolsArchive -Force
}

if ($AcceptAndroidLicenses) {
    Write-Host 'Accepting Android SDK package licenses...'
    $processInfo = [System.Diagnostics.ProcessStartInfo]::new()
    $processInfo.FileName = $env:ComSpec
    $processInfo.Arguments = "/d /s /c `"$sdkManager`" --sdk_root=$sdkRoot --licenses"
    $processInfo.UseShellExecute = $false
    $processInfo.CreateNoWindow = $true
    $processInfo.RedirectStandardInput = $true
    $processInfo.RedirectStandardOutput = $true
    $processInfo.RedirectStandardError = $true
    $process = [System.Diagnostics.Process]::new()
    $process.StartInfo = $processInfo
    $null = $process.Start()
    $answers = (1..200 | ForEach-Object { "y`r`n" }) -join ''
    $process.StandardInput.Write($answers)
    $process.StandardInput.Close()
    $output = $process.StandardOutput.ReadToEnd()
    $errors = $process.StandardError.ReadToEnd()
    $process.WaitForExit()
    if ($process.ExitCode -ne 0) {
        throw "sdkmanager --licenses failed with exit code $($process.ExitCode): $errors"
    }
    if ($output -match 'not accepted|Skipping following packages') {
        throw 'sdkmanager did not accept all Android SDK package licenses'
    }
    Write-Host 'All SDK package licenses accepted.'
}

Write-Host 'Installing pinned Android SDK packages...'
& $sdkManager --sdk_root=$sdkRoot `
    'platform-tools' `
    'platforms;android-35' `
    'build-tools;34.0.0' `
    'build-tools;35.0.0' `
    'cmake;3.22.1' `
    'ndk;27.2.12479018'
if ($LASTEXITCODE -ne 0) { throw "sdkmanager failed with exit code $LASTEXITCODE" }

$escapedSdk = $sdkRoot.Replace('\', '\\').Replace(':', '\:')
Set-Content -LiteralPath (Join-Path $root 'local.properties') -Value "sdk.dir=$escapedSdk" -Encoding ascii
Write-Host 'Dependencies and the project-local Android SDK are ready.'
