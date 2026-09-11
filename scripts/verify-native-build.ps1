param(
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Debug',
    [Parameter(Mandatory)][string]$ApkPath
)

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$commandsFile = Get-ChildItem (Join-Path $root "app/.cxx/$Configuration") -Recurse -Filter compile_commands.json |
    Where-Object { $_.Directory.Name -eq 'arm64-v8a' } |
    Sort-Object LastWriteTimeUtc -Descending | Select-Object -First 1
if (-not $commandsFile) { throw 'Missing ARM64 compilation database; run the native build.' }
$commands = Get-Content -LiteralPath $commandsFile.FullName -Raw | ConvertFrom-Json
$cCount = 0
foreach ($entry in $commands) {
    $flags = [regex]::Matches($entry.command, '(?<!\S)-O(?:[0-3sgz]|fast)(?!\S)')
    if ($flags.Count -eq 0 -or $flags[$flags.Count - 1].Value -ne '-O3') {
        throw "Native source is not optimized with -O3: $($entry.file)"
    }
    if ($entry.file -match '\.c$') { $cCount++ }
}
if ($cCount -eq 0) { throw 'No C kernels found in compilation database.' }
$metadata = Get-Content (Join-Path $commandsFile.DirectoryName 'android_gradle_build.json') -Raw | ConvertFrom-Json
$library = $metadata.libraries.PSObject.Properties.Value | Where-Object { $_.artifactName -eq 'funasr_engine' }
$unstripped = Get-Item -LiteralPath $library.output
$sources = Get-ChildItem (Join-Path $root 'app/src/main/cpp') -File -Recurse |
    Where-Object { $_.Extension -in '.c', '.cpp', '.h' -or $_.Name -eq 'CMakeLists.txt' }
if ($sources | Where-Object { $_.LastWriteTimeUtc -gt $unstripped.LastWriteTimeUtc }) {
    throw 'Native library is older than its sources. Do not skip CMake/native build tasks.'
}
$variant = $Configuration.ToLowerInvariant()
$stripped = Join-Path $root "app/build/intermediates/stripped_native_libs/$variant/strip${Configuration}DebugSymbols/out/lib/arm64-v8a/libfunasr_engine.so"
$readelf = Join-Path (Split-Path $metadata.toolchains.toolchain.cppCompilerExecutable) 'llvm-readelf.exe'
function Get-BuildId([string]$LibraryPath) {
    $notes = & $readelf -n $LibraryPath
    if ($LASTEXITCODE -ne 0) { throw "Cannot inspect ELF: $LibraryPath" }
    $match = [regex]::Match(($notes -join "`n"), 'Build ID: ([0-9a-f]+)')
    if (-not $match.Success) { throw "ELF build ID missing: $LibraryPath" }
    return $match.Groups[1].Value
}
$buildId = Get-BuildId $unstripped.FullName
if ($buildId -ne (Get-BuildId $stripped)) { throw 'Stripped library does not match the current native build.' }

Add-Type -AssemblyName System.IO.Compression.FileSystem
$zip = [System.IO.Compression.ZipFile]::OpenRead((Resolve-Path -LiteralPath $ApkPath).Path)
try {
    $expected = @{
        'lib/arm64-v8a/libfunasr_engine.so' = (Get-FileHash -LiteralPath $stripped -Algorithm SHA256).Hash.ToLowerInvariant()
        'assets/models/sensevoice-small-q8.gguf' = '4ae45c94422de949b387e2e0fb10d7e14e4c42c69db30c3444ecc7d4b844b7c5'
        'assets/models/fsmn-vad.gguf' = '1270f2559c495f4e7b6e739541151027d360761a3fda43fc147034f5719f5479'
    }
    if (-not $zip.GetEntry('lib/arm64-v8a/libc++_shared.so')) { throw 'APK is missing the C++ runtime.' }
    foreach ($name in $expected.Keys) {
        $entry = $zip.GetEntry($name)
        if (-not $entry) { throw "APK is missing $name" }
        $stream = $entry.Open()
        $sha = [System.Security.Cryptography.SHA256]::Create()
        try { $hash = [BitConverter]::ToString($sha.ComputeHash($stream)).Replace('-', '').ToLowerInvariant() }
        finally { $sha.Dispose(); $stream.Dispose() }
        if ($hash -ne $expected[$name]) { throw "APK content mismatch: $name" }
    }
    Write-Host "Verified $cCount optimized C kernels, ELF build ID $buildId, APK native library and both bundled model hashes."
} finally { $zip.Dispose() }
