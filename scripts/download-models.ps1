param(
    [string]$ModelSource,
    [string]$ModelDir = (Join-Path (Split-Path $PSScriptRoot -Parent) 'models'),
    [switch]$Force
)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

$models = @(
    [pscustomobject]@{
        Name = 'sensevoice-small-q8.gguf'
        Url = 'https://huggingface.co/FunAudioLLM/SenseVoiceSmall-GGUF/resolve/main/sensevoice-small-q8.gguf'
        Sha256 = '4ae45c94422de949b387e2e0fb10d7e14e4c42c69db30c3444ecc7d4b844b7c5'
    },
    [pscustomobject]@{
        Name = 'fsmn-vad.gguf'
        Url = 'https://huggingface.co/FunAudioLLM/fsmn-vad-GGUF/resolve/main/fsmn-vad.gguf'
        Sha256 = '1270f2559c495f4e7b6e739541151027d360761a3fda43fc147034f5719f5479'
    }
)

function Invoke-Download([string]$Uri, [string]$Destination) {
    $parameters = @{ Uri = $Uri; OutFile = $Destination }
    if ((Get-Command Invoke-WebRequest).Parameters.ContainsKey('NoProxy')) {
        $parameters.NoProxy = $true
    }
    Invoke-WebRequest @parameters
}

New-Item -ItemType Directory -Force -Path $ModelDir | Out-Null
foreach ($model in $models) {
    $destination = Join-Path $ModelDir $model.Name
    $part = "$destination.part"

    if ((Test-Path -LiteralPath $destination) -and -not $Force) {
        $hash = (Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($hash -ne $model.Sha256) {
            throw "Existing model hash mismatch: $($model.Name). Use -Force to replace it."
        }
        Write-Host "Validated $($model.Name)"
        continue
    }

    Remove-Item -LiteralPath $part -Force -ErrorAction SilentlyContinue
    if ($ModelSource) {
        $source = Join-Path $ModelSource $model.Name
        if (-not (Test-Path -LiteralPath $source)) {
            throw "ModelSource does not contain $($model.Name): $source"
        }
        Write-Host "Copying $($model.Name) from local source..."
        Copy-Item -LiteralPath $source -Destination $part
    } else {
        Write-Host "Downloading $($model.Name)..."
        for ($attempt = 1; $attempt -le 3; $attempt++) {
            try {
                Invoke-Download $model.Url $part
                break
            } catch {
                Remove-Item -LiteralPath $part -Force -ErrorAction SilentlyContinue
                if ($attempt -eq 3) { throw }
                Write-Warning "Download attempt $attempt failed; retrying..."
            }
        }
    }

    $actual = (Get-FileHash -LiteralPath $part -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($actual -ne $model.Sha256) {
        Remove-Item -LiteralPath $part -Force -ErrorAction SilentlyContinue
        throw "Model hash mismatch: $($model.Name). Expected $($model.Sha256), got $actual"
    }
    Move-Item -LiteralPath $part -Destination $destination -Force
    Write-Host "Installed $destination"
}

Write-Host 'All models are ready.'
