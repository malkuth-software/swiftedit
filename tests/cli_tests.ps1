param([Parameter(Mandatory=$true)][string]$Executable)
$ErrorActionPreference = 'Stop'
$fixture = Join-Path ([IO.Path]::GetTempPath()) ('swiftedit-cli-' + [guid]::NewGuid().ToString('N'))
[IO.Directory]::CreateDirectory($fixture) | Out-Null
$process = [Diagnostics.Process]::new()
$started = $false
try {
    $process.StartInfo.FileName = [IO.Path]::GetFullPath($Executable)
    $process.StartInfo.UseShellExecute = $false
    $process.StartInfo.CreateNoWindow = $true
    $process.StartInfo.RedirectStandardInput = $true
    $process.StartInfo.RedirectStandardOutput = $true
    $process.StartInfo.RedirectStandardError = $true
    $started = $process.Start()
    if (-not $started) { throw 'CLI did not start' }
    function Read-Line([Diagnostics.Process]$Process) {
        $pending = $process.StandardOutput.ReadLineAsync()
        [bool]$completed = $pending.Wait(5000)
        if (-not $completed) { throw 'CLI response timeout' }
        if ($null -eq $pending.Result) { throw 'Unexpected CLI EOF' }
        return $pending.Result
    }
    function Send-Request([Diagnostics.Process]$Process, [string[]]$Fields) {
        $process.StandardInput.WriteLine(($Fields -join "`t"))
        $process.StandardInput.Flush()
        $rows = [Collections.Generic.List[string]]::new()
        do { $row = Read-Line -Process $Process; $rows.Add($row) } while (-not ($row.StartsWith("ok`t") -or $row.StartsWith("error`t")))
        $result = $rows.ToArray()
        return ,$result
    }
    function Assert([bool]$Value,[string]$Message) { if (-not $Value) { throw $Message } }
    [string]$greeting = Read-Line -Process $process
    Assert ($greeting -eq "ready`tSwiftEdit`t1") 'Protocol greeting'
    $response = Send-Request -Process $process -Fields @('preview','','','','','extra')
    Assert ($response[-1].StartsWith("error`tToo many")) 'Request field storage is bounded'
    $response = Send-Request -Process $process -Fields @('preview','','','','hello\nworld')
    $preview = $response[0].Split("`t")
    Assert ($preview[0] -eq 'preview') 'Explicit preview returned'
    $response = Send-Request -Process $process -Fields @('commit',$preview[1],$preview[2])
    Assert ($response[-1].StartsWith("ok`tcommit")) 'Preview commit accepted'
    $response = Send-Request -Process $process -Fields @('commit',$preview[1],$preview[2])
    Assert ($response[-1].StartsWith("error`tStale")) 'Stale commit refused'
    $response = Send-Request -Process $process -Fields @('page','0','4096')
    Assert ($response[0] -eq "page`t0`t11`t11`thello\x0aworld") 'Exact escaped page'
    $response = Send-Request -Process $process -Fields @('word-count')
    Assert ($response[0] -eq "word-count`t2") 'Shared word count observes working text'
    $response = Send-Request -Process $process -Fields @('word-count-start')
    Assert ($response[0] -eq "word-count-progress`t0`t11") 'Count start performs no source scan'
    $response = Send-Request -Process $process -Fields @('word-count-next','6')
    Assert ($response[0] -eq "word-count-progress`t6`t11") 'Count advances within explicit budget'
    $response = Send-Request -Process $process -Fields @('word-count-next','6')
    Assert ($response[0] -eq "word-count`t2") 'Incremental count publishes completed result'
    $response = Send-Request -Process $process -Fields @('word-count-start')
    $response = Send-Request -Process $process -Fields @('word-count-cancel')
    Assert ($response[-1].StartsWith("ok`tword-count-cancel`t")) 'Explicit count cancellation'
    $response = Send-Request -Process $process -Fields @('word-count-next','1')
    Assert ($response[-1].StartsWith("error`tStart a word count")) 'Cancelled count cannot continue'
    $response = Send-Request -Process $process -Fields @('search-start','WORLD','0','0','')
    Assert ($response[0] -eq "search-progress`t0`t11") 'Search start performs no source scan'
    $response = Send-Request -Process $process -Fields @('search-next','0')
    Assert ($response[-1].StartsWith("error`tSearch work budget")) 'Zero search work budget refused'
    $response = Send-Request -Process $process -Fields @('search-start','world','0','2','')
    Assert ($response[-1].StartsWith("error`tSearch match-case")) 'Invalid search mode refused'
    for ($searchStep = 0; $searchStep -lt 100; ++$searchStep) {
        $response = Send-Request -Process $process -Fields @('search-next','1')
        Assert ($response[-1].StartsWith("ok`tsearch-next`t")) 'Search advances with one-operation budget'
        if ($response[0].StartsWith("search-match`t")) { break }
    }
    Assert ($response[0] -eq "search-match`t6`t5") 'Case-folded search publishes exact byte range'
    $response = Send-Request -Process $process -Fields @('search-start','world','11','1','')
    $response = Send-Request -Process $process -Fields @('search-next','1')
    Assert ($response[0] -eq 'search-not-found') 'Protocol search at EOF does not implicitly wrap'
    $response = Send-Request -Process $process -Fields @('search-start','?','0','0','x')
    Assert ($response[-1].StartsWith("error`tSearch flags")) 'Invalid wildcard flag refused'
    $response = Send-Request -Process $process -Fields @('search-cancel')
    $response = Send-Request -Process $process -Fields @('search-next','1')
    Assert ($response[-1].StartsWith("error`tStart a search")) 'Cancelled search cannot continue'
    $target = (Join-Path $fixture 'new.txt').Replace('\','\\')
    $response = Send-Request -Process $process -Fields @('save-as',$target)
    Assert ($response[-1].StartsWith("ok`tsave-as")) 'Save creates new file'
    [string]$savedPath = Join-Path $fixture 'new.txt'
    [string]$savedText = [IO.File]::ReadAllText($savedPath)
    Assert ($savedText -eq "hello`nworld") 'File bytes match explicit edit'
    $response = Send-Request -Process $process -Fields @('undo')
    Assert ($response[-1].StartsWith("error`tAt last-save")) 'Save boundary through process'
    $response = Send-Request -Process $process -Fields @('restore-opened')
    $response = Send-Request -Process $process -Fields @('open',$target)
    Assert ($response[-1].StartsWith("error`tUnsaved")) 'Open cannot silently discard'
    $response = Send-Request -Process $process -Fields @('discard')
    $blankFile = Join-Path $fixture 'blank.txt'
    [IO.File]::WriteAllText($blankFile,"a`r`n`r`n `n`n")
    $response = Send-Request -Process $process -Fields @('open',$blankFile.Replace('\','\\'))
    Assert ($response[0].StartsWith("context`t")) 'Open emits stamped context'
    Assert ($response -contains "blank`t3`t2`t\x0d\x0dL2\x0d\x0d") 'True blank has escaped marker metadata'
    Assert ($response -contains "blank`t7`t4`t\x0d\x0dL4\x0d\x0d") 'Whitespace-only line does not get marker'
    $response = Send-Request -Process $process -Fields @('context','2')
    Assert ($response[1] -eq "page`t0`t2`t8`ta\x0d") 'Bounded context restart'
    $response = Send-Request -Process $process -Fields @('context-next','2')
    Assert ($response -contains "blank`t3`t2`t\x0d\x0dL2\x0d\x0d") 'Split CRLF preserves line metadata'
    $response = Send-Request -Process $process -Fields @('preview','','a','','\r\rL2\r\r')
    Assert ($response[-1].StartsWith("error`t")) 'Metadata echoed into replacement is refused'
    $response = Send-Request -Process $process -Fields @('preview','','a','','b')
    $blankPreview = $response[0].Split("`t")
    $response = Send-Request -Process $process -Fields @('commit',$blankPreview[1],$blankPreview[2])
    $response = Send-Request -Process $process -Fields @('context-next','2')
    Assert ($response[-1].StartsWith("error`tContext changed")) 'Context traversal rejects stale revision'
    $response = Send-Request -Process $process -Fields @('discard')
    $csv = Join-Path $fixture 'table.csv'
    [IO.File]::WriteAllText($csv,"0.1,0.2`n4,text")
    $response = Send-Request -Process $process -Fields @('open',$csv.Replace('\','\\'))
    $response = Send-Request -Process $process -Fields @('csv-calculate','SUM(A1:B1)')
    Assert ($response[0] -eq "calculation`t0.3`tA1`tB1") 'Exact decimal result with references'
    $response = Send-Request -Process $process -Fields @('csv-calculate','SUM(A1:B2)')
    Assert ($response[-1].StartsWith("error`tB2:")) 'Invalid referenced cell identified'
    $response = Send-Request -Process $process -Fields @('csv-set','B2','7')
    $preview = $response[0].Split("`t")
    [string]$previewDiskText = [IO.File]::ReadAllText($csv)
    Assert ($previewDiskText.EndsWith('text')) 'CSV preview does not write disk'
    $response = Send-Request -Process $process -Fields @('commit',$preview[1],$preview[2])
    $response = Send-Request -Process $process -Fields @('csv-calculate','SUM(A1:B2)')
    Assert ($response[0].StartsWith("calculation`t11.3`t")) 'CSV commit updates calculation source'
    $response = Send-Request -Process $process -Fields @('csv-set','B2','=A2*2')
    $preview = $response[0].Split("`t")
    $response = Send-Request -Process $process -Fields @('commit',$preview[1],$preview[2])
    $response = Send-Request -Process $process -Fields @('csv-value','B2')
    Assert ($response[0] -eq "value`t8`tA2") 'Formula is evaluated with reference metadata'
    $response = Send-Request -Process $process -Fields @('csv-convert-to-value','B2')
    $preview = $response[0].Split("`t")
    $response = Send-Request -Process $process -Fields @('commit',$preview[1],$preview[2])
    $response = Send-Request -Process $process -Fields @('csv-get','B2')
    Assert ($response[0] -eq "cell`tB2`t8") 'Conversion stores a literal value'
    $response = Send-Request -Process $process -Fields @('undo')
    $response = Send-Request -Process $process -Fields @('csv-get','B2')
    Assert ($response[0] -eq "cell`tB2`t=A2*2") 'Conversion undo restores the formula'
    $response = Send-Request -Process $process -Fields @('discard')
    $largeCountPath = Join-Path $fixture 'large-count.txt'
    $largeCountStream = [IO.File]::Open($largeCountPath,[IO.FileMode]::CreateNew)
    try {
        $countBlock = [Text.Encoding]::UTF8.GetBytes(('x ' * 32768))
        for ($blockIndex = 0; $blockIndex -lt 256; ++$blockIndex) {
            $largeCountStream.Write($countBlock,0,$countBlock.Length)
        }
    } finally { $largeCountStream.Dispose() }
    $response = Send-Request -Process $process -Fields @('open',$largeCountPath.Replace('\','\\'))
    Assert ($response[-1].StartsWith("ok`topen`t")) 'Open real paged count fixture'
    $largeTextCopy = Join-Path $fixture 'large-text-copy.txt'
    $response = Send-Request -Process $process -Fields @('save-text-copy',$largeTextCopy.Replace('\','\\'))
    Assert ($response[-1].StartsWith("ok`tsave-text-copy`t")) 'Paged Save Text Copy succeeds through CLI'
    $sourceHash = (Get-FileHash -LiteralPath $largeCountPath -Algorithm SHA256).Hash
    $copyHash = (Get-FileHash -LiteralPath $largeTextCopy -Algorithm SHA256).Hash
    Assert ($sourceHash -eq $copyHash) 'Paged CLI text copy preserves valid source bytes'
    $response = Send-Request -Process $process -Fields @('save-text-copy',$largeTextCopy.Replace('\','\\'))
    Assert ($response[-1].StartsWith("error`t")) 'Paged CLI text copy refuses an existing destination'
    $cooperativeCopy = Join-Path $fixture 'cooperative-copy.txt'
    $response = Send-Request -Process $process -Fields @('text-copy-start',$cooperativeCopy.Replace('\','\\'))
    Assert ($response[0] -eq "text-copy-progress`t0`t16777216`t0") 'Text copy starts without source reads'
    $response = Send-Request -Process $process -Fields @('text-copy-publish')
    Assert ($response[-1].StartsWith("error`tText copy is not ready")) 'Unfinished text copy cannot publish'
    $response = Send-Request -Process $process -Fields @('text-copy-next','0')
    Assert ($response[-1].StartsWith("error`tText copy read budget")) 'Invalid copy budget preserves pending task'
    for ($textStep = 0; $textStep -lt 256; ++$textStep) {
        $response = Send-Request -Process $process -Fields @('text-copy-next','65536')
        Assert ($response[-1].StartsWith("ok`ttext-copy-next`t")) 'Text copy advances cooperatively'
    }
    Assert ($response[0] -eq "text-copy-ready`t16777216`t16777216`t0") 'Complete text copy waits for publication'
    Assert (-not [IO.File]::Exists($cooperativeCopy)) 'Ready copy has not installed its destination'
    $response = Send-Request -Process $process -Fields @('text-copy-publish')
    Assert ($response[0] -eq "text-copy-saved`t16777216`t0") 'Explicit publish installs the complete copy'
    Assert ((Get-FileHash -LiteralPath $cooperativeCopy -Algorithm SHA256).Hash -eq $sourceHash) 'Cooperative CLI copy is byte faithful'
    $response = Send-Request -Process $process -Fields @('word-count-start')
    Assert ($response[0] -eq "word-count-progress`t0`t16777216") 'Paged count begins without scanning'
    for ($countStep = 0; $countStep -lt 256; ++$countStep) {
        $response = Send-Request -Process $process -Fields @('word-count-next','65536')
        Assert ($response[-1].StartsWith("ok`tword-count-next`t")) 'Paged count step succeeds'
    }
    Assert ($response[0] -eq "word-count`t8388608") 'Paged whole-document count through CLI'
    $response = Send-Request -Process $process -Fields @('search-start',' ? X','65535','0','0100')
    Assert ($response[0] -eq "search-progress`t0`t16777216") 'Paged search starts without whole-file allocation'
    for ($searchStep = 0; $searchStep -lt 100; ++$searchStep) {
        $response = Send-Request -Process $process -Fields @('search-next','65536')
        Assert ($response[-1].StartsWith("ok`tsearch-next`t")) 'Paged search step succeeds'
        if ($response[0].StartsWith("search-match`t")) { break }
    }
    Assert ($response[0] -eq "search-match`t65535`t4") 'Paged wildcard search respects requested byte start'
    $response = Send-Request -Process $process -Fields @('copy-start','65535','4')
    Assert ($response[0] -eq "copy-progress`t0`t4") 'Paged copy starts without reading'
    $response = Send-Request -Process $process -Fields @('copy-next','2')
    Assert ($response[0] -eq "copy-progress`t2`t4") 'Copy yields after bounded work'
    $response = Send-Request -Process $process -Fields @('clipboard-page','0','4')
    Assert ($response[0] -eq "clipboard`t0`t0`t0`t") 'Partial copy never published'
    $response = Send-Request -Process $process -Fields @('copy-next','2')
    Assert ($response[0] -eq "copied`t4") 'Complete paged copy publishes once'
    $response = Send-Request -Process $process -Fields @('clipboard-page','0','4')
    Assert ($response[0] -eq "clipboard`t0`t4`t4`t x x") 'Copy preserves exact range across boundary'
    $response = Send-Request -Process $process -Fields @('copy-start','0','100')
    $response = Send-Request -Process $process -Fields @('copy-next','1')
    $response = Send-Request -Process $process -Fields @('copy-cancel')
    $response = Send-Request -Process $process -Fields @('clipboard-page','0','4')
    Assert ($response[0] -eq "clipboard`t0`t4`t4`t x x") 'Cancelled copy preserves previous clipboard'
    $response = Send-Request -Process $process -Fields @('copy-start','0','100')
    $response = Send-Request -Process $process -Fields @('search-start','absent','0','0','')
    $response = Send-Request -Process $process -Fields @('discard')
    $response = Send-Request -Process $process -Fields @('search-next','1')
    Assert ($response[-1].StartsWith("error`tSearch belongs to an older document")) 'Search refuses changed document identity'
    $response = Send-Request -Process $process -Fields @('copy-next','1')
    Assert ($response[-1].StartsWith("error`tCopy document changed")) 'Copy rejects changed document identity'
    $response = Send-Request -Process $process -Fields @('clipboard-page','0','4')
    Assert ($response[0] -eq "clipboard`t0`t4`t4`t x x") 'Failed copy preserves previous clipboard'
    $invalidCopySource = Join-Path $fixture 'invalid-copy-source.txt'
    [IO.File]::WriteAllBytes($invalidCopySource,[byte[]](240,159,152,128,255,226,130))
    $response = Send-Request -Process $process -Fields @('open',$invalidCopySource.Replace('\','\\'))
    $invalidCopyTarget = Join-Path $fixture 'invalid-copy-target.txt'
    $response = Send-Request -Process $process -Fields @('text-copy-start',$invalidCopyTarget.Replace('\','\\'))
    $response = Send-Request -Process $process -Fields @('text-copy-start',$invalidCopySource.Replace('\','\\'))
    Assert ($response[-1].StartsWith("error`t")) 'Invalid replacement copy request preserves prior task'
    for ($byteStep = 0; $byteStep -lt 7; ++$byteStep) {
        $response = Send-Request -Process $process -Fields @('text-copy-next','1')
    }
    Assert ($response[0] -eq "text-copy-ready`t7`t7`t3") 'Split emoji preserved and three illegal bytes counted'
    $response = Send-Request -Process $process -Fields @('text-copy-cancel')
    Assert (-not [IO.File]::Exists($invalidCopyTarget)) 'Cancelling a ready copy leaves destination absent'
    $response = Send-Request -Process $process -Fields @('text-copy-start',$invalidCopyTarget.Replace('\','\\'))
    $response = Send-Request -Process $process -Fields @('text-copy-next','65536')
    $response = Send-Request -Process $process -Fields @('discard')
    $response = Send-Request -Process $process -Fields @('text-copy-publish')
    Assert ($response[-1].StartsWith("error`tText copy source changed")) 'Ready copy refuses changed source identity'
    Assert (-not [IO.File]::Exists($invalidCopyTarget)) 'Stale publication leaves destination absent'
    $response = Send-Request -Process $process -Fields @('text-copy-cancel')
    $response = Send-Request -Process $process -Fields @('quit')
    [bool]$exited = $process.WaitForExit(5000)
    Assert ($exited -and $process.ExitCode -eq 0) 'Clean process exit'
    [string]$quitDiskText = [IO.File]::ReadAllText($csv)
    Assert ($quitDiskText.EndsWith('text')) 'Quit does not autosave'
    Write-Output 'CLI integration passed: transport, preview/commit, stale guard, file publication, undo boundary, dirty-open refusal and exact CSV calculation.'
} finally {
    if ($started -and -not $process.HasExited) { $process.Kill(); $process.WaitForExit() }
    $process.Dispose()
    $resolved = [IO.Path]::GetFullPath($fixture)
    $tempRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
    if (-not $resolved.StartsWith($tempRoot,[StringComparison]::OrdinalIgnoreCase) -or -not ([IO.Path]::GetFileName($resolved)).StartsWith('swiftedit-cli-')) { throw 'Fixture cleanup path validation failed' }
    Remove-Item -LiteralPath $resolved -Recurse -Force
}
