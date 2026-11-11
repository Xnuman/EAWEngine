<#
.SYNOPSIS
Recounts docs/xml-tag-status.md: rewrites its summary tables and each family's count line from the
status column of the tag rows.
#>
param(
    [string] $Path = (Join-Path $PSScriptRoot '..\docs\xml-tag-status.md')
)

$ErrorActionPreference = 'Stop'
$lines = [System.Collections.Generic.List[string]](Get-Content -LiteralPath $Path -Encoding UTF8)
$order = @('done', 'partial', 'gap', 'needs', 'n/a')

function Get-Kind([string] $status) {
    if ($status.StartsWith('needs ')) { return 'needs' }
    return $status
}

# Count rows per family (## headings after the summary) and in total.
$total = @{}
$systems = @{}
$families = @{}
$family = $null
foreach ($line in $lines) {
    if ($line -match '^## (.+)$' -and $Matches[1] -ne 'Summary') {
        $family = $Matches[1]
        $families[$family] = @{}
    }
    elseif ($family -and $line -match '^\| `[^`]+` \| `([^`]+)` \|') {
        $status = $Matches[1]
        $kind = Get-Kind $status
        if ($order -notcontains $kind) { throw "unknown status '$status' in: $line" }
        $families[$family][$kind] = 1 + [int]$families[$family][$kind]
        $total[$kind] = 1 + [int]$total[$kind]
        if ($kind -eq 'needs') {
            $system = $status.Substring(6)
            $systems[$system] = 1 + [int]$systems[$system]
        }
    }
}

$output = [System.Collections.Generic.List[string]]::new()
$index = 0
while ($index -lt $lines.Count) {
    $line = $lines[$index]
    if ($line -eq '| Status | Tags |') {
        $output.Add($line); $output.Add('| --- | --- |')
        foreach ($kind in $order) { $output.Add("| ``$kind`` | $([int]$total[$kind]) |") }
        $output.Add("| total | $(($total.Values | Measure-Object -Sum).Sum) |")
        $index += 2
        while ($index -lt $lines.Count -and $lines[$index].StartsWith('|')) { $index++ }
        continue
    }
    if ($line -eq '| System | Tags |') {
        $output.Add($line); $output.Add('| --- | --- |')
        foreach ($entry in ($systems.GetEnumerator() | Sort-Object @{ Expression = 'Value'; Descending = $true }, Name)) {
            $output.Add("| $($entry.Name) | $($entry.Value) |")
        }
        $index += 2
        while ($index -lt $lines.Count -and $lines[$index].StartsWith('|')) { $index++ }
        continue
    }
    $output.Add($line)
    if ($line -match '^## (.+)$' -and $families.ContainsKey($Matches[1])) {
        $counts = $families[$Matches[1]]
        $parts = foreach ($kind in $order) { if ($counts[$kind]) { "$($counts[$kind]) $kind" } }
        # The family's count line follows its heading after one blank line.
        $output.Add('')
        $output.Add(($parts -join ', ') + '.')
        $index += 3
        continue
    }
    $index++
}

[System.IO.File]::WriteAllText((Resolve-Path -LiteralPath $Path), (($output -join "`n") + "`n"), [System.Text.UTF8Encoding]::new($false))
Write-Host "Tag status: $([int]$total['done']) done, $([int]$total['partial']) partial, $([int]$total['gap']) gap, $([int]$total['needs']) needs, $([int]$total['n/a']) n/a"
