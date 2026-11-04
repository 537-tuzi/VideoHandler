# monitor.ps1 - log video_handler process CPU + watched file state timeline
param(
    [string]$WatchFile,
    [string]$OutLog,
    [int]$TargetPid = 0
)
$lines = New-Object System.Collections.Generic.List[string]
$sw = [System.Diagnostics.Stopwatch]::StartNew()
$idleCount = 0
while ($true) {
    $p = $null
    if ($TargetPid -gt 0) { $p = Get-Process -Id $TargetPid -ErrorAction SilentlyContinue }
    else { $p = Get-Process -Name video_handler -ErrorAction SilentlyContinue | Select-Object -First 1 }
    $fi = Get-Item -LiteralPath $WatchFile -ErrorAction SilentlyContinue
    $fileY = "-"; $bytes = 0
    if ($fi) { $fileY = "Y"; $bytes = $fi.Length }
    if ($p) {
        $idleCount = 0
        $cpu = $p.TotalProcessorTime.TotalSeconds
        $lines.Add(("{0} | pid={1} | cpu={2:F1}s | file={3} | bytes={4}" -f $sw.Elapsed.ToString("mm\:ss\.fff"), $p.Id, $cpu, $fileY, $bytes))
    }
    else {
        $lines.Add(("{0} | pid=- | cpu=- | file={1} | bytes={2}" -f $sw.Elapsed.ToString("mm\:ss\.fff"), $fileY, $bytes))
        $idleCount++
        if ($idleCount -ge 25) { break }
    }
    [System.IO.File]::WriteAllLines($OutLog, $lines)
    Start-Sleep -Milliseconds 200
}
