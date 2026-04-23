$root = "D:\Workspace\AngelscriptProject\Plugins\Angelscript\Source\AngelscriptTest"
$files = Get-ChildItem -Path $root -Recurse -Include "*.cpp","*.h","*.md"
$count = 0
foreach ($file in $files) {
    $content = [System.IO.File]::ReadAllText($file.FullName, [System.Text.Encoding]::UTF8)
    $newContent = $content `
        -replace "AngelscriptScenarioTestUtils\.h", "AngelscriptFunctionalTestUtils.h" `
        -replace "AngelscriptScenarioTestUtils::", "AngelscriptFunctionalTestUtils::" `
        -replace "namespace AngelscriptScenarioTestUtils", "namespace AngelscriptFunctionalTestUtils"
    if ($newContent -ne $content) {
        [System.IO.File]::WriteAllText($file.FullName, $newContent, [System.Text.Encoding]::UTF8)
        Write-Output "Updated: $($file.Name)"
        $count++
    }
}
Write-Output "Total files updated: $count"
