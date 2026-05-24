param(
    [int]$StartIndex = 7,
    [int]$TimeoutMs = 900000
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Continue'

$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
$RunTests = Join-Path $ProjectRoot 'Tools\RunTests.ps1'

$AllEntries = @(
    @{ Prefix = 'Angelscript.Editor'; Label = 'Editor' },
    @{ Prefix = 'Angelscript.GAS'; Label = 'GAS' },
    @{ Prefix = 'Angelscript.Template'; Label = 'Template' },
    @{ Prefix = 'Angelscript.TestModule.Actor'; Label = 'Actor' },
    @{ Prefix = 'Angelscript.TestModule.AngelScriptSDK'; Label = 'AngelScriptSDK' },
    @{ Prefix = 'Angelscript.TestModule.Bindings'; Label = 'Bindings' },
    @{ Prefix = 'Angelscript.TestModule.Blueprint'; Label = 'Blueprint' },
    @{ Prefix = 'Angelscript.TestModule.ClassGenerator'; Label = 'ClassGenerator' },
    @{ Prefix = 'Angelscript.TestModule.Compiler'; Label = 'Compiler' },
    @{ Prefix = 'Angelscript.TestModule.Component'; Label = 'Component' },
    @{ Prefix = 'Angelscript.TestModule.Core'; Label = 'Core' },
    @{ Prefix = 'Angelscript.TestModule.Debugger'; Label = 'Debugger' },
    @{ Prefix = 'Angelscript.TestModule.Delegate'; Label = 'Delegate' },
    @{ Prefix = 'Angelscript.TestModule.Dump'; Label = 'Dump' },
    @{ Prefix = 'Angelscript.TestModule.Editor'; Label = 'TestModuleEditor' },
    @{ Prefix = 'Angelscript.TestModule.Engine'; Label = 'Engine' },
    @{ Prefix = 'Angelscript.TestModule.FileSystem'; Label = 'FileSystem' },
    @{ Prefix = 'Angelscript.TestModule.Functional'; Label = 'Functional' },
    @{ Prefix = 'Angelscript.TestModule.FunctionLibraries'; Label = 'FunctionLibraries' },
    @{ Prefix = 'Angelscript.TestModule.GameInstanceSubsystem'; Label = 'GameInstanceSubsystem' },
    @{ Prefix = 'Angelscript.TestModule.GC'; Label = 'GC' },
    @{ Prefix = 'Angelscript.TestModule.HotReload'; Label = 'HotReload' },
    @{ Prefix = 'Angelscript.TestModule.Inheritance'; Label = 'Inheritance' },
    @{ Prefix = 'Angelscript.TestModule.Interface'; Label = 'Interface' },
    @{ Prefix = 'Angelscript.TestModule.Learning'; Label = 'Learning' },
    @{ Prefix = 'Angelscript.TestModule.Memory'; Label = 'Memory' },
    @{ Prefix = 'Angelscript.TestModule.Networking'; Label = 'Networking' },
    @{ Prefix = 'Angelscript.TestModule.Parity'; Label = 'Parity' },
    @{ Prefix = 'Angelscript.TestModule.Performance'; Label = 'Performance' },
    @{ Prefix = 'Angelscript.TestModule.Preprocessor'; Label = 'Preprocessor' },
    @{ Prefix = 'Angelscript.TestModule.ScriptClass'; Label = 'ScriptClass' },
    @{ Prefix = 'Angelscript.TestModule.Shared'; Label = 'Shared' },
    @{ Prefix = 'Angelscript.TestModule.StaticJIT'; Label = 'StaticJIT' },
    @{ Prefix = 'Angelscript.TestModule.Syntax'; Label = 'Syntax' },
    @{ Prefix = 'Angelscript.TestModule.Validation'; Label = 'Validation' },
    @{ Prefix = 'Angelscript.TestModule.WorldSubsystem'; Label = 'WorldSubsystem' }
)

$Summary = @()
for ($Index = $StartIndex - 1; $Index -lt $AllEntries.Count; ++$Index) {
    $Entry = $AllEntries[$Index]
    $RunLabel = '{0}_{1:D2}_{2}' -f 'All', ($Index + 1), $Entry.Label
    Write-Host ('==== [{0}/{1}] {2} ====' -f ($Index + 1), $AllEntries.Count, $Entry.Prefix)

    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $RunTests `
        -TestPrefix $Entry.Prefix `
        -Label $RunLabel `
        -TimeoutMs $TimeoutMs

    $ExitCode = $LASTEXITCODE
    $Summary += [PSCustomObject]@{
        Index = $Index + 1
        Label = $RunLabel
        Prefix = $Entry.Prefix
        ExitCode = $ExitCode
    }
    Write-Host ("ExitCode={0}" -f $ExitCode)
}

Write-Host '==== REMAINING SUITE SUMMARY ===='
$Summary | Format-Table -AutoSize
