param([Parameter(Mandatory)][string]$LogPath, [string]$OutputPath)
$ErrorActionPreference = 'Stop'
$asr = [System.Collections.Generic.List[object]]::new()
$dispatch = [System.Collections.Generic.List[double]]::new()
foreach ($line in Get-Content -LiteralPath $LogPath) {
    if ($line -match 'asr id=(\d+) final=([01]) audio_ms=([\d.]+) queue_ms=([\d.]+) compute_ms=([\d.]+) capture_drops=(\d+) final_drops=(\d+)') {
        $asr.Add([pscustomobject]@{
            id = [long]$Matches[1]; final = [int]$Matches[2]
            audio_ms = [double]::Parse($Matches[3], [cultureinfo]::InvariantCulture)
            queue_ms = [double]::Parse($Matches[4], [cultureinfo]::InvariantCulture)
            compute_ms = [double]::Parse($Matches[5], [cultureinfo]::InvariantCulture)
            capture_drops = [long]$Matches[6]; final_drops = [long]$Matches[7]
        })
    } elseif ($line -match 'display id=\d+ final=(?:true|false) dispatch_ms=([\d.]+)') {
        $dispatch.Add([double]::Parse($Matches[1], [cultureinfo]::InvariantCulture))
    }
}
if ($asr.Count -eq 0) { throw 'No FunASRPerf samples. Enable log.tag.FunASRPerf=I before capturing.' }
function Get-Stats([double[]]$Values) {
    if ($Values.Count -eq 0) { return $null }
    $sorted = @($Values | Sort-Object)
    # Nearest-rank percentiles, also for small samples.
    return [ordered]@{
        count = $sorted.Count
        p50 = [math]::Round($sorted[[math]::Ceiling($sorted.Count * 0.50) - 1], 3)
        p95 = [math]::Round($sorted[[math]::Ceiling($sorted.Count * 0.95) - 1], 3)
        max = [math]::Round($sorted[-1], 3)
    }
}
$summary = [ordered]@{
    asr_jobs = $asr.Count
    final_jobs = @($asr | Where-Object final -eq 1).Count
    compute_ms = Get-Stats @($asr.compute_ms)
    queue_ms = Get-Stats @($asr.queue_ms)
    queued_to_decoded_ms = Get-Stats @($asr | ForEach-Object { $_.queue_ms + $_.compute_ms })
    rtf = Get-Stats @($asr | ForEach-Object { $_.compute_ms / $_.audio_ms })
    callback_to_setText_ms = Get-Stats @($dispatch)
    capture_drops = ($asr.capture_drops | Measure-Object -Maximum).Maximum
    final_drops = ($asr.final_drops | Measure-Object -Maximum).Maximum
    note = 'Queue starts after VAD segmentation. This does not measure sound-to-screen latency or words missed by VAD/ASR.'
}
$json = $summary | ConvertTo-Json -Depth 4
$json
if ($OutputPath) { Set-Content -LiteralPath $OutputPath -Value $json -Encoding utf8 }
