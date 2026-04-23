$root = "D:\Workspace\AngelscriptProject\Plugins\Angelscript\Source\AngelscriptTest"
$files = Get-ChildItem -Path $root -Recurse -Include "*.cpp","*.h"
$count = 0
foreach ($file in $files) {
    $content = [System.IO.File]::ReadAllText($file.FullName, [System.Text.Encoding]::UTF8)

    $newContent = $content `
        -replace 'FAngelscriptScenario([A-Za-z])', 'FAngelscriptTest$1' `
        -replace 'class AScenario([A-Za-z])', 'class ATest$1' `
        -replace 'class UScenario([A-Za-z])', 'class UTest$1' `
        -replace '"AScenario([A-Za-z])', '"ATest$1' `
        -replace '"UScenario([A-Za-z])', '"UTest$1' `
        -replace 'TEXT\("AScenario([A-Za-z])', 'TEXT("ATest$1' `
        -replace 'TEXT\("UScenario([A-Za-z])', 'TEXT("UTest$1' `
        -replace 'FPrefixedLiteralBoundaryScenario', 'FPrefixedLiteralBoundaryTestCase' `
        -replace 'RunPrefixedLiteralBoundaryScenario', 'RunPrefixedLiteralBoundaryTestCase' `
        -replace 'struct FScenario([^a-zA-Z])', 'struct FTestCase$1' `
        -replace 'TArray<([A-Za-z:]+::)?FScenario>', 'TArray<$1FTestCase>' `
        -replace 'const ([A-Za-z:]+::)?FScenario&', 'const $1FTestCase&' `
        -replace '([A-Za-z:]+::)?FScenario& ([A-Za-z])', '$1FTestCase& $2' `
        -replace 'FScenario ([A-Za-z])', 'FTestCase $1' `
        -replace 'BuildScenarios\(\)', 'BuildTestCases()' `
        -replace 'const FString& ScenarioLabel', 'const FString& TestCaseLabel' `
        -replace 'ScenarioLabel([^s])', 'TestCaseLabel$1' `
        -replace '\.ScenarioLabel\b', '.TestCaseLabel' `
        -replace '([A-Za-z:>]+ )Scenario([^a-zA-Z])', '$1TestCase$2' `
        -replace '([A-Za-z:>]+& )Scenario([^a-zA-Z])', '$1TestCase$2' `
        -replace '([A-Za-z:>]+\*\* )Scenario([^a-zA-Z])', '$1TestCase$2' `
        -replace '\bScenario\.', 'TestCase.' `
        -replace 'F([A-Za-z]*)Scenario([A-Za-z]+)', 'F$1TestCase$2' `
        -replace 'Run([A-Za-z]*)Scenario([A-Za-z]*)\(', 'Run$1TestCase$2(' `
        -replace 'Initialize([A-Za-z]*)Scenario([A-Za-z]*)\(', 'Initialize$1TestCase$2(' `
        -replace 'TArray<F([A-Za-z]*)Scenario>', 'TArray<F$1TestCase>' `
        -replace 'ScenarioName([^s])', 'TestCaseName$1' `
        -replace 'TArray<([A-Za-z:]+::)?F([A-Za-z]*)Scenario>', 'TArray<$1F$2TestCase>' `
        -replace 'AngelscriptTest_([A-Za-z]+)_Angelscript([A-Za-z]+)ScenarioTests_Private', 'AngelscriptTest_$1_Angelscript$2Tests_Private' `
        -replace '([A-Za-z]+)Scenarios\b', '$1TestCases' `
        -replace '"Scenario ([a-z])', '"Test $1'

    if ($newContent -ne $content) {
        [System.IO.File]::WriteAllText($file.FullName, $newContent, [System.Text.Encoding]::UTF8)
        Write-Output "Updated: $($file.Name)"
        $count++
    }
}
Write-Output "Total files updated: $count"

