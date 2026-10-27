param([string]$Baseline = '')
$ErrorActionPreference = 'Stop'
[string]$repo = Split-Path -Parent $PSScriptRoot
function Get-CodeText([string]$Text) {
    [string]$literals = '(?s)/\*.*?\*/|//[^\r\n]*|"(?:\\.|[^"\\])*"|''(?:\\.|[^''\\])*'''
    [string]$code = [regex]::Replace($Text, $literals, ' ')
    return $code
}
[System.Collections.Generic.List[IO.FileInfo]]$files = [System.Collections.Generic.List[IO.FileInfo]]::new()
foreach ($directory in @('src','tests')) {
    [IO.FileInfo[]]$entries = @(Get-ChildItem -LiteralPath (Join-Path $repo $directory) -File)
    foreach ($entry in $entries) {
        if ($entry.Extension -eq '.cpp' -or $entry.Extension -eq '.hpp' -or $entry.Extension -eq '.mm') { $files.Add($entry) }
    }
}
[hashtable]$rules = @{
    inferred_type = '\bauto\b'
    arrow = '->'
    anonymous_execution = '\[[^\[\]]*\]\s*(?:\([^{};]*\))?\s*(?:mutable\s*)?\{'
    defaulted_comparison = 'operator\s*(?:==|<=>)[^;{}]*=\s*default'
    coroutine = '\bco_(?:await|yield|return)\b'
    ranges_pipeline = 'std::(?:ranges|views)::'
}
[int]$total = 0
foreach ($file in $files) {
    [string]$relative = [IO.Path]::GetRelativePath($repo,$file.FullName).Replace('\','/')
    [string]$source = ''
    if ($Baseline) {
        [string[]]$lines = @(& git -C $repo show ($Baseline + ':' + $relative))
        if ($LASTEXITCODE) { throw "Cannot read baseline $relative" }
        $source = $lines -join "`n"
    } else { $source = [IO.File]::ReadAllText($file.FullName) }
    [string]$code = Get-CodeText $source
    foreach ($rule in $rules.Keys) {
        [int]$count = [regex]::Matches($code,$rules[$rule]).Count
        if ($count -gt 0) { Write-Output "$relative $rule $count"; $total += $count }
    }
}
Write-Output "C++ spelling findings: $total; files: $($files.Count). Semantic review remains separate."
if (-not $Baseline -and $total -gt 0) { exit 1 }
