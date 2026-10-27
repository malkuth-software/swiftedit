param(
    [string]$Toolchain = $(if ($env:SWIFTEDIT_TOOLCHAIN) { $env:SWIFTEDIT_TOOLCHAIN } else { Split-Path (Get-Command clang -ErrorAction Stop).Source }),
    [string]$GuiSdk = (Join-Path $PSScriptRoot '../.build/native-windows-x64/sdk/gui-forms-sdk'),
    [string]$PickerSdk = (Join-Path $PSScriptRoot '../.build/native-windows-x64/sdk/picker-sdk'),
    [string]$BuildDirectory = '',
    [string]$StageDirectory = '',
    [switch]$NativeTests
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$build = if ($BuildDirectory) { [IO.Path]::GetFullPath($BuildDirectory) } else { Join-Path $repo '.build/native-windows-x64/app' }
$stage = if ($StageDirectory) { [IO.Path]::GetFullPath($StageDirectory) } else { Join-Path $repo 'dist/SwiftEdit' }
$env:PATH = "$Toolchain;$GuiSdk/bin;$PickerSdk/bin;$env:PATH"
function Assert-NotRunning([string]$directory) {
    $prefix = [IO.Path]::GetFullPath($directory).TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
    foreach ($process in (Get-CimInstance Win32_Process -Filter "Name = 'notepad.exe' OR Name = 'SwiftEdit.exe' OR Name = 'swiftedit-cli.exe' OR Name = 'swiftedit-session-tests.exe' OR Name = 'notepad-native-tests.exe' OR Name = 'notepad-editor-tests.exe'")) {
        if ($process.ExecutablePath -and $process.ExecutablePath.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) {
            throw "Process $($process.ProcessId) is using $directory. Choose another build/staging directory; the running copy was preserved."
        }
    }
}
function Get-SdkFingerprint {
    $roots = @("$GuiSdk/include", "$GuiSdk/lib", "$GuiSdk/bin", "$PickerSdk/include", "$PickerSdk/lib")
    $lines = [System.Collections.Generic.List[string]]::new()
    foreach ($root in $roots) {
        $files = @(Get-ChildItem -LiteralPath $root -Recurse -File | Sort-Object FullName)
        foreach ($file in $files) {
            $fileHash = Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256
            $entry = $file.FullName + ':' + $fileHash.Hash
            $lines.Add($entry)
        }
    }
    $sha = [System.Security.Cryptography.SHA256]::Create()
    try {
        $manifest = $lines -join "`n"
        $manifestBytes = [Text.Encoding]::UTF8.GetBytes($manifest)
        $digest = $sha.ComputeHash($manifestBytes)
        $fingerprint = [Convert]::ToHexString($digest)
        return $fingerprint
    } finally { $sha.Dispose() }
}
Push-Location -LiteralPath $repo
try {
    Assert-NotRunning $build
    Assert-NotRunning $stage
    & "$Toolchain/python.exe" -B tools/prepare_dependencies.py --platform windows-x64
    if ($LASTEXITCODE) { throw 'Dependency build failed' }
    $env:CCACHE_DIR = Join-Path $repo '.ccache'
    $env:CCACHE_BASEDIR = $repo
    $env:CCACHE_COMPILERCHECK = 'content'
    $sdkFingerprint = Get-SdkFingerprint
    $stamp = Join-Path $build 'sdk-fingerprint.txt'
    $nativeFlag = if ($NativeTests) { 'ON' } else { 'OFF' }
    & "$Toolchain/cmake.exe" -S $repo -B $build -G Ninja -DCMAKE_BUILD_TYPE=Release '-DNOTEPAD_BUILD_UI=ON' "-DNOTEPAD_NATIVE_TESTS=$nativeFlag" "-DCMAKE_PREFIX_PATH=$GuiSdk;$PickerSdk" "-DCMAKE_TOOLCHAIN_FILE=$repo/gui_forms/cmake/llvm22.cmake" -DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache
    if ($LASTEXITCODE) { throw 'Configure failed' }
    & "$Toolchain/cmake.exe" --build $build --parallel 2
    if ($LASTEXITCODE) { throw 'Build failed' }
    & "$Toolchain/ctest.exe" --test-dir $build --output-on-failure
    if ($LASTEXITCODE) { throw 'Tests failed' }
    if ((Get-SdkFingerprint) -ne $sdkFingerprint) { throw 'SDK changed during build/tests; rerun against a coherent checkpoint.' }
    Assert-NotRunning $stage
    New-Item -ItemType Directory -Path $stage -Force | Out-Null
    Copy-Item -LiteralPath "$build/SwiftEdit.exe" -Destination $stage -Force
    Copy-Item -LiteralPath "$build/swiftedit-cli.exe" -Destination $stage -Force
    Copy-Item -LiteralPath "$build/swiftedit-terminal.exe" -Destination $stage -Force
    Copy-Item -LiteralPath "$repo/README.md" -Destination $stage -Force
    Copy-Item -LiteralPath "$repo/docs" -Destination $stage -Recurse -Force
    $licenseStage = Join-Path $stage 'licenses'
    New-Item -ItemType Directory -Path $licenseStage -Force | Out-Null
    Copy-Item -LiteralPath "$repo/third_party/unicode/LICENSE.txt" -Destination (Join-Path $licenseStage 'Unicode.txt') -Force
    Copy-Item -LiteralPath "$repo/third_party/md4c/LICENSE.md" -Destination (Join-Path $licenseStage 'MD4C.md') -Force
    foreach ($package in @($GuiSdk, $PickerSdk)) {
        $noticeRoot = Join-Path $package 'share/licenses'
        foreach ($notice in (Get-ChildItem -LiteralPath $noticeRoot -Directory)) {
            Copy-Item -LiteralPath $notice.FullName -Destination $licenseStage -Recurse -Force
        }
    }
    foreach ($component in @('libc++', 'libunwind', 'compiler-rt', 'crt', 'headers', 'libwinpthread')) {
        $notice = Join-Path $Toolchain "../share/licenses/$component"
        if (Test-Path -LiteralPath $notice) {
            Copy-Item -LiteralPath $notice -Destination $licenseStage -Recurse -Force
        }
    }
    Copy-Item -LiteralPath "$GuiSdk/share/GUIForms/fonts" -Destination $stage -Recurse -Force
    $queue = [System.Collections.Generic.Queue[string]]::new()
    $queue.Enqueue("$stage/SwiftEdit.exe")
    $queue.Enqueue("$stage/swiftedit-cli.exe")
    $queue.Enqueue("$stage/swiftedit-terminal.exe")
    $seen = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
    while ($queue.Count) {
        $binary = $queue.Dequeue()
        $imports = & "$Toolchain/objdump.exe" -p $binary
        if ($LASTEXITCODE) { throw "Cannot inspect imports: $binary" }
        foreach ($line in $imports) {
            if ($line -notmatch 'DLL Name:\s*(\S+)') { continue }
            $name = $Matches[1]
            $unseen = $seen.Add($name)
            if (-not $unseen) { continue }
            $dependency = $null
            foreach ($dir in @("$GuiSdk/bin", "$PickerSdk/bin", $Toolchain)) {
                $candidate = Join-Path $dir $name
                if (Test-Path -LiteralPath $candidate) { $dependency = $candidate; break }
            }
            if ($dependency) {
                Copy-Item -LiteralPath $dependency -Destination $stage -Force
                $queue.Enqueue((Join-Path $stage $name))
            } elseif ($name -notmatch '^(api-ms-|ext-ms-)' -and -not (Test-Path -LiteralPath (Join-Path "$env:SystemRoot/System32" $name))) {
                throw "Missing runtime dependency: $name"
            }
        }
    }
    Get-ChildItem -LiteralPath $stage -File | Get-FileHash -Algorithm SHA256 | Format-Table -AutoSize
    if ((Get-SdkFingerprint) -ne $sdkFingerprint) { throw 'SDK changed during packaging; this stage is not verified.' }
    Set-Content -LiteralPath $stamp -Value $sdkFingerprint
    Set-Content -LiteralPath (Join-Path $stage 'sdk-fingerprint.txt') -Value $sdkFingerprint
    if ($NativeTests) { Write-Output "Staged $stage/SwiftEdit.exe. Coordinated self-closing native smoke passed." }
    else { Write-Output "Staged $stage/SwiftEdit.exe. No desktop window was launched." }
} finally { Pop-Location }
