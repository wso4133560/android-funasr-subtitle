param()

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$source = 'third_party/llama.cpp-803b7fcae893e9caaee3921779628fef83ac0965'
if (-not (Test-Path (Join-Path $root "$source/ggml/CMakeLists.txt"))) {
    throw 'Pinned ggml source is missing. Run scripts/bootstrap.ps1 first.'
}
foreach ($patch in Get-ChildItem (Join-Path $root 'third_party/patches') -Filter '*.patch') {
    # Reverse checking makes this safe to run before every build. A different
    # upstream revision or conflicting edit must fail instead of being replaced.
    & git -C $root apply --reverse --check "--directory=$source" $patch.FullName 2>$null
    if ($LASTEXITCODE -eq 0) { continue }
    & git -C $root apply --check "--directory=$source" $patch.FullName
    if ($LASTEXITCODE -ne 0) { throw "ggml patch cannot be applied: $($patch.Name)" }
    & git -C $root apply "--directory=$source" $patch.FullName
    if ($LASTEXITCODE -ne 0) { throw "ggml patch failed: $($patch.Name)" }
    Write-Host "Applied ggml compatibility patch: $($patch.Name)"
}
