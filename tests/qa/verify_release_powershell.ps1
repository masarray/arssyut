# Parse every inline PowerShell run block in the stable distribution workflow.
# PowerShell's own AST parser catches missing quotes, braces and truncated
# expressions before a release is attempted from GitHub Actions.
$ErrorActionPreference = 'Stop'
$workflow = Join-Path $PSScriptRoot '../../.github/workflows/windows-release-exes.yml'
$lines = [System.IO.File]::ReadAllLines((Resolve-Path $workflow).Path)
$blockCount = 0
for ($i = 0; $i -lt $lines.Length; $i++) {
    if ($lines[$i] -ne '        run: |') { continue }
    $body = [System.Collections.Generic.List[string]]::new()
    for ($j = $i + 1; $j -lt $lines.Length; $j++) {
        $line = $lines[$j]
        if ($line.StartsWith('          ')) {
            $body.Add($line.Substring(10))
        } elseif ([string]::IsNullOrWhiteSpace($line)) {
            $body.Add('')
        } else {
            break
        }
    }
    if ($body.Count -eq 0) { throw "Release step at line $($i + 1) is empty" }
    $tokens = $null
    $errors = $null
    [void][System.Management.Automation.Language.Parser]::ParseInput(
        ($body -join [Environment]::NewLine), [ref]$tokens, [ref]$errors)
    if ($null -ne $errors -and $errors.Count -gt 0) {
        foreach ($errorItem in $errors) {
            Write-Error "Release step line $($i + 1): $($errorItem.Message)"
        }
        throw "Release PowerShell AST syntax invalid near line $($i + 1)"
    }
    $blockCount++
}
if ($blockCount -lt 6) {
    throw "Only $blockCount release PowerShell blocks found; expected at least 6"
}
Write-Host "PASS: parsed $blockCount release PowerShell scripts without syntax errors"
