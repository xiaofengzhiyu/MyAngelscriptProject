param(
    [string]$TestsRoot = (Join-Path (Split-Path (Split-Path (Split-Path $PSScriptRoot -Parent) -Parent) -Parent) 'Saved\Tests')
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$dirs = Get-ChildItem -LiteralPath $TestsRoot -Directory | Where-Object { $_.Name -match '^All_\d+_' }
$results = New-Object 'System.Collections.Generic.List[object]'

foreach ($d in $dirs) {
    $metaFile = Get-ChildItem -LiteralPath $d.FullName -Recurse -Filter 'RunMetadata.json' -ErrorAction SilentlyContinue | Select-Object -First 1
    $sumFile = Get-ChildItem -LiteralPath $d.FullName -Recurse -Filter 'Summary.json' -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($null -eq $metaFile) { continue }

    $meta = Get-Content -LiteralPath $metaFile.FullName -Raw | ConvertFrom-Json
    $total = $null
    $passed = $null
    if ($null -ne $sumFile) {
        $sum = Get-Content -LiteralPath $sumFile.FullName -Raw | ConvertFrom-Json
        $total = $sum.Total
        $passed = $sum.Passed
    }

    $durSec = [math]::Round([double]$meta.DurationMs / 1000.0, 1)
    $secPerTest = if ($total -gt 0) { [math]::Round($durSec / [double]$total, 2) } else { $null }
    $fast = $false
    if ($meta.PSObject.Properties.Name -contains 'FastLaunch') {
        $fast = [bool]$meta.FastLaunch
    }

    $results.Add([PSCustomObject]@{
            Prefix     = [string]$meta.Target
            DurSec     = $durSec
            Tests      = $total
            Passed     = $passed
            SecPerTest = $secPerTest
            Fast       = $fast
            Label      = [string]$meta.Label
        }) | Out-Null
}

$dedup = $results | Group-Object -Property Prefix | ForEach-Object { $_.Group | Select-Object -Last 1 } | Sort-Object DurSec -Descending
$serialSec = ($dedup | Measure-Object -Property DurSec -Sum).Sum

Write-Host '=== Per-prefix wall time (latest run) ==='
$dedup | Format-Table -AutoSize

Write-Host ''
Write-Host ('Unique prefixes :</> {0}' -f $dedup.Count)
Write-Host ('Serial 36-boot sum: {0} sec ({1:F1} min)' -f $serialSec, ($serialSec / 60.0))

$topLevel = @('Angelscript.Editor', 'Angelscript.GAS', 'Angelscript.Template')
$tmSum = ($dedup | Where-Object { $_.Prefix -like 'Angelscript.TestModule*' } | Measure-Object -Property DurSec -Sum).Sum
$coarseShards = @(
    ($dedup | Where-Object { $_.Prefix -like 'Angelscript.TestModule*' } | Measure-Object -Property DurSec -Sum).Sum
    ($dedup | Where-Object { $_.Prefix -eq 'Angelscript.Editor' } | Select-Object -ExpandProperty DurSec -ErrorAction SilentlyContinue)
    ($dedup | Where-Object { $_.Prefix -eq 'Angelscript.GAS' } | Select-Object -ExpandProperty DurSec -ErrorAction SilentlyContinue)
    ($dedup | Where-Object { $_.Prefix -eq 'Angelscript.Template' } | Select-Object -ExpandProperty DurSec -ErrorAction SilentlyContinue)
) | Where-Object { $_ -ne $null }

Write-Host ('Coarse TestModule shard (sum of 33 prefixes if run separately): {0:F1} min' -f ($tmSum / 60.0))
Write-Host ('Coarse parallel wall (max of 4 shards): {0:F1} min' -f ((($coarseShards | Measure-Object -Maximum).Maximum) / 60.0))

$monolithic = $results | Where-Object { $_.Prefix -eq 'Angelscript' } | Select-Object -Last 1
if ($null -ne $monolithic) {
    Write-Host ('Monolithic Angelscript prefix: {0:F1} sec ({1:F1} min), exit metadata label={2}' -f $monolithic.DurSec, ($monolithic.DurSec / 60.0), $monolithic.Label)
}

Write-Host ''
Write-Host '=== Startup overhead estimate (small prefixes) ==='
$small = $dedup | Where-Object { $_.Tests -gt 0 -and $_.Tests -le 15 } | Sort-Object Tests
if ($small.Count -gt 0) {
    $avgSecPerTest = ($small | Measure-Object -Property SecPerTest -Average).Average
    Write-Host ('Light prefixes (<=15 tests): avg {0:F2} sec/test (includes editor boot per run)' -f $avgSecPerTest)
}
