# Third-party notices

The native SenseVoice and FSMN-VAD implementation is derived from
[modelscope/FunASR](https://github.com/modelscope/FunASR), commit
`a57c05bfe2a91b5e0cb0983479634eba3e28ede5` (`runtime-llamacpp-v0.2.6`).
It is distributed under the MIT license in `LICENSE-FunASR`.

The Android build uses ggml from [ggml-org/llama.cpp](https://github.com/ggml-org/llama.cpp),
commit `803b7fcae893e9caaee3921779628fef83ac0965`. Its MIT license is in
`LICENSE-ggml`. The bootstrap script downloads the source archive and verifies its SHA256.

SenseVoice Small and FSMN-VAD model files are published separately by FunAudioLLM. They are
downloaded only when requested, are verified by SHA256, and are not committed to this repository
as binaries. The build script embeds the verified models in the APK for automatic installation
on first launch.

The optional Vulkan build also uses Khronos Vulkan C / C++ headers and SPIR-V headers.
`scripts/prepare-vulkan.ps1` copies them into the ignored `.android-vulkan` directory and
preserves their embedded copyright and license notices. The tested Vulkan-Hpp headers use
Apache-2.0 OR MIT; Vulkan C headers use Apache-2.0 OR MIT; SPIR-V headers carry the Khronos
permission notice. The script records header and shader-compiler hashes in its local manifest.
These optional dependencies are not needed for the default CPU-only build.
