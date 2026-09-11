param([string]$HeadersRoot)

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$destination = Join-Path $root '.android-vulkan'
$include = Join-Path $destination 'include'
$ndk = Join-Path $root '.android-sdk/ndk/27.2.12479018'
if (-not $HeadersRoot) {
    if ($env:VULKAN_SDK -and (Test-Path (Join-Path $env:VULKAN_SDK 'Include/vulkan/vulkan.hpp'))) {
        $HeadersRoot = Join-Path $env:VULKAN_SDK 'Include'
    } elseif (Test-Path 'C:/msys64/ucrt64/include/vulkan/vulkan.hpp') {
        $HeadersRoot = 'C:/msys64/ucrt64/include'
    } else {
        throw 'Provide -HeadersRoot pointing to an include directory containing vulkan/vulkan.hpp and vk_video. Vulkan SDK or MSYS2 Vulkan headers can supply these.'
    }
}
$HeadersRoot = (Resolve-Path -LiteralPath $HeadersRoot).Path
foreach ($relative in @('vulkan/vulkan.hpp', 'vulkan/vulkan_core.h', 'vk_video')) {
    if (-not (Test-Path (Join-Path $HeadersRoot $relative))) { throw "Missing Vulkan dependency: $relative" }
}
$spirv = Join-Path $ndk 'sources/third_party/shaderc/third_party/spirv-tools/external/spirv-headers/include/spirv'
if (-not (Test-Path (Join-Path $spirv 'unified1/spirv.hpp'))) { throw 'Pinned NDK SPIR-V headers are missing.' }
if (-not (Test-Path (Join-Path $ndk 'shader-tools/windows-x86_64/glslc.exe'))) { throw 'Pinned NDK glslc is missing.' }
New-Item -ItemType Directory -Force -Path $include | Out-Null
foreach ($name in @('vulkan', 'vk_video')) {
    Copy-Item -LiteralPath (Join-Path $HeadersRoot $name) -Destination $include -Recurse -Force
}
Copy-Item -LiteralPath $spirv -Destination $include -Recurse -Force

# These are header-only dependencies. Package them locally for ggml's required
# CMake discovery without changing the host installation or the NDK.
$package = Join-Path $destination 'cmake/SPIRV-Headers'
New-Item -ItemType Directory -Force -Path $package | Out-Null
@'
if(NOT TARGET SPIRV-Headers::SPIRV-Headers)
    add_library(SPIRV-Headers::SPIRV-Headers INTERFACE IMPORTED)
    set_target_properties(SPIRV-Headers::SPIRV-Headers PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_LIST_DIR}/../../include")
endif()
'@ | Set-Content -LiteralPath (Join-Path $package 'SPIRV-HeadersConfig.cmake') -Encoding ascii
$versionLine = (Select-String -LiteralPath (Join-Path $include 'vulkan/vulkan_core.h') -Pattern '^#define VK_HEADER_VERSION ').Line
[ordered]@{
    headersSource = $HeadersRoot
    ndkVersion = '27.2.12479018'
    vulkanHeaderVersion = $versionLine
    vulkanHeaderSha256 = (Get-FileHash (Join-Path $include 'vulkan/vulkan.hpp')).Hash
    spirvHeaderSha256 = (Get-FileHash (Join-Path $include 'spirv/unified1/spirv.hpp')).Hash
    glslcSha256 = (Get-FileHash (Join-Path $ndk 'shader-tools/windows-x86_64/glslc.exe')).Hash
} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $destination 'manifest.json') -Encoding utf8
Write-Host "Vulkan dependencies prepared in $destination"
