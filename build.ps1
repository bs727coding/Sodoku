<#
.SYNOPSIS
    Builds Sudoku.exe as a native Windows-on-ARM64 executable with the LLVM-MinGW clang toolchain.

.DESCRIPTION
    1. Compiles every src\**\*.cpp to AArch64 objects in parallel (one clang process per core),
       incrementally - a file is rebuilt only when it or a header it includes changed (-MMD).
    2. Compiles res\app.rc (icon, application manifest, version info) with llvm-windres.
    3. Links one self-contained executable with lld: libc++/libunwind are linked statically and the
       C runtime is the Universal CRT that ships with Windows 10/11, so there is nothing to install.

.EXAMPLE
    .\build.ps1                  # optimized release build -> build\release\Sudoku.exe
    .\build.ps1 -Run             # build, then launch
    .\build.ps1 -Test            # build and run the engine test-suite + benchmarks
    .\build.ps1 -Config debug    # unoptimized build with CodeView debug info and a PDB
    .\build.ps1 -Native          # generate code for the Snapdragon X (Oryon) cores specifically
    .\build.ps1 -Clean
#>
[CmdletBinding()]
param(
    [ValidateSet('release', 'debug')] [string]$Config = 'release',
    [switch]$Test,
    [switch]$Run,
    [switch]$Clean,
    [switch]$Native,
    [string]$TestArgs = ''
)

$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
Set-Location -LiteralPath $root

if ($Clean) {
    if (Test-Path build) { Remove-Item build -Recurse -Force }
    Write-Host 'Cleaned build output.'
    if (-not ($Test -or $Run)) { return }
}

# ---------------------------------------------------------------- toolchain

function Find-Tool([string]$name) {
    $cmd = Get-Command $name -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($cmd) { return $cmd.Source }
    $pkgRoot = Join-Path $env:LOCALAPPDATA 'Microsoft\WinGet\Packages'
    $pkg = Get-ChildItem $pkgRoot -Directory -Filter 'MartinStorsjo.LLVM-MinGW*' -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($pkg) {
        $exe = Get-ChildItem $pkg.FullName -Recurse -Filter "$name.exe" -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($exe) { return $exe.FullName }
    }
    throw "Could not find '$name'. Install the toolchain with:  winget install MartinStorsjo.LLVM-MinGW.UCRT"
}

$cxx = Find-Tool 'clang++'
$windres = Find-Tool 'llvm-windres'

# ------------------------------------------------------------------- flags

$variant = if ($Native) { "$Config-native" } else { $Config }
$outDir = "build/$variant"
$objDir = "$outDir/obj"

$target = '--target=aarch64-w64-mingw32'
$cpu = if ($Native) { @('-mcpu=oryon-1') } else { @('-march=armv8.2-a', '-mtune=oryon-1') }
$cflags = @($target, '-std=c++23', '-Wall', '-Wextra', '-Wno-missing-field-initializers',
    '-fno-exceptions', '-fno-rtti', '-Isrc',
    '-DUNICODE', '-D_UNICODE', '-DWIN32_LEAN_AND_MEAN', '-DNOMINMAX', '-D_WIN32_WINNT=0x0A00', '-DWINVER=0x0A00') + $cpu
$lflags = @($target, '-static')
if ($Config -eq 'release') {
    $cflags += @('-O2', '-flto=thin', '-ffunction-sections', '-fdata-sections', '-DNDEBUG')
    $lflags += @('-O2', '-flto=thin', '-Wl,--gc-sections', '-s')
}
else {
    $cflags += @('-O0', '-g', '-gcodeview', '-D_DEBUG')
    $lflags += @('-g', '-Wl,--pdb=')
}
$libs = @('-ld2d1', '-ldwrite', '-ld3d11', '-ldxgi', '-ldcomp', '-ldwmapi', '-ldxguid', '-lole32', '-luuid',
    '-lshell32', '-lwinmm', '-lwindowscodecs', '-ladvapi32', '-luser32', '-lgdi32', '-lkernel32')

function Join-Args([string[]]$list) {
    ($list | ForEach-Object { if ($_ -match '[\s"]') { '"' + ($_ -replace '"', '\"') + '"' } else { $_ } }) -join ' '
}

New-Item -ItemType Directory -Force $objDir | Out-Null

# Rebuild everything when the compiler flags change.
$flagsFile = "$objDir/flags.txt"
$flagsText = "$cxx`n" + (Join-Args $cflags)
if (-not (Test-Path $flagsFile) -or (Get-Content $flagsFile -Raw) -ne $flagsText) {
    Get-ChildItem $objDir -Recurse -Include *.o, *.d -ErrorAction SilentlyContinue | Remove-Item -Force
    Set-Content -Path $flagsFile -Value $flagsText -NoNewline
}

# --------------------------------------------------------------- utilities

function Test-UpToDate([string]$src, [string]$obj) {
    if (-not (Test-Path $obj)) { return $false }
    $objTime = (Get-Item $obj).LastWriteTimeUtc
    if ((Get-Item $src).LastWriteTimeUtc -gt $objTime) { return $false }
    $dep = "$obj.d"
    if (-not (Test-Path $dep)) { return $false }
    $text = (Get-Content $dep -Raw) -replace '\\\r?\n', ' '
    $parts = $text -split '\s+' | Where-Object { $_ -and -not $_.EndsWith(':') }
    foreach ($p in $parts) {
        if (-not (Test-Path -LiteralPath $p)) { return $false }
        if ((Get-Item -LiteralPath $p).LastWriteTimeUtc -gt $objTime) { return $false }
    }
    return $true
}

# Runs jobs (@{ Exe; Args; Label }) with up to one process per core; returns $true if all succeeded.
function Invoke-Jobs($jobs, [string]$verb) {
    $max = [Environment]::ProcessorCount
    $queue = New-Object System.Collections.Queue
    foreach ($j in $jobs) { $queue.Enqueue($j) }
    $running = New-Object System.Collections.ArrayList
    $ok = $true
    while ($queue.Count -gt 0 -or $running.Count -gt 0) {
        while ($running.Count -lt $max -and $queue.Count -gt 0) {
            $j = $queue.Dequeue()
            $psi = New-Object System.Diagnostics.ProcessStartInfo
            $psi.FileName = $j.Exe
            $psi.Arguments = $j.Args
            $psi.WorkingDirectory = $root
            $psi.UseShellExecute = $false
            $psi.RedirectStandardError = $true
            $psi.RedirectStandardOutput = $true
            $psi.CreateNoWindow = $true
            $p = [System.Diagnostics.Process]::Start($psi)
            [void]$running.Add([pscustomobject]@{
                    P = $p; Err = $p.StandardError.ReadToEndAsync(); Out = $p.StandardOutput.ReadToEndAsync(); Label = $j.Label
                })
            Write-Host ("  {0,-8} {1}" -f $verb, $j.Label)
        }
        $done = @($running | Where-Object { $_.P.HasExited })
        if ($done.Count -eq 0) { Start-Sleep -Milliseconds 10; continue }
        foreach ($d in $done) {
            $d.P.WaitForExit()
            $msg = ($d.Out.Result + $d.Err.Result).Trim()
            if ($msg) { Write-Host $msg }
            if ($d.P.ExitCode -ne 0) { $ok = $false }
            [void]$running.Remove($d)
        }
    }
    return $ok
}

function Get-ObjPath([string]$src) {
    $rel = $src -replace '\\', '/'
    $rel = $rel -replace '^(src|tests)/', ''
    $prefix = if ($src -match '^tests') { 'tests/' } else { '' }
    return "$objDir/$prefix" + ($rel -replace '\.cpp$', '.o')
}

function Get-Sources([string]$dir) {
    if (-not (Test-Path $dir)) { return @() }
    Get-ChildItem $dir -Recurse -Filter *.cpp | ForEach-Object {
        $_.FullName.Substring($root.Length + 1) -replace '\\', '/'
    } | Sort-Object
}

# ----------------------------------------------------------------- compile

$sources = @(Get-Sources 'src')
if ($Test) { $sources += @(Get-Sources 'tests') }

Write-Host "Sudoku build ($variant, ARM64)"
Write-Host "  clang++ $(Join-Args $cflags)"
$sw = [System.Diagnostics.Stopwatch]::StartNew()

$jobs = @()
foreach ($src in $sources) {
    $obj = Get-ObjPath $src
    if (Test-UpToDate $src $obj) { continue }
    New-Item -ItemType Directory -Force (Split-Path $obj) | Out-Null
    $jobs += @{ Exe = $cxx; Label = $src; Args = (Join-Args ($cflags + @('-MMD', '-MF', "$obj.d", '-c', $src, '-o', $obj))) }
}
if ($jobs.Count -gt 0) {
    if (-not (Invoke-Jobs $jobs 'compile')) { throw 'Compilation failed.' }
}
else {
    Write-Host '  objects up to date'
}

# --------------------------------------------------------------- resources

$resObj = "$outDir/app.res.o"
if (Test-Path 'res/app.rc') {
    $resNewest = (Get-ChildItem res -File | Measure-Object -Property LastWriteTimeUtc -Maximum).Maximum
    if (-not (Test-Path $resObj) -or (Get-Item $resObj).LastWriteTimeUtc -lt $resNewest) {
        $ok = Invoke-Jobs @(@{ Exe = $windres; Label = 'res/app.rc'; Args = (Join-Args @('--target=aarch64-w64-mingw32', '-I', 'res', 'res/app.rc', '-O', 'coff', '-o', $resObj)) }) 'rc'
        if (-not $ok) { throw 'Resource compilation failed.' }
    }
}

# -------------------------------------------------------------------- link

$appObjs = @($sources | Where-Object { $_ -like 'src/*' } | ForEach-Object { Get-ObjPath $_ })
$exe = "$outDir/Sudoku.exe"
if (Test-Path 'src/main.cpp') {
    $linkArgs = $lflags + @('-municode', '-mwindows', '-o', $exe) + $appObjs
    if (Test-Path $resObj) { $linkArgs += $resObj }
    $linkArgs += $libs
    $needLink = -not (Test-Path $exe) -or $jobs.Count -gt 0 -or ((Test-Path $resObj) -and (Get-Item $resObj).LastWriteTimeUtc -gt (Get-Item $exe).LastWriteTimeUtc)
    if ($needLink) {
        if (-not (Invoke-Jobs @(@{ Exe = $cxx; Label = $exe; Args = (Join-Args $linkArgs) }) 'link')) { throw 'Link failed.' }
    }
    $bytes = [System.IO.File]::ReadAllBytes((Resolve-Path $exe))
    $peOffset = [BitConverter]::ToInt32($bytes, 0x3C)
    $machine = [BitConverter]::ToUInt16($bytes, $peOffset + 4)
    $arch = switch ($machine) { 0xAA64 { 'ARM64' } 0x8664 { 'x64' } default { '0x{0:X4}' -f $machine } }
    Write-Host ("Built {0}  ({1:N0} KB, {2}) in {3:N1} s" -f $exe, ($bytes.Length / 1KB), $arch, $sw.Elapsed.TotalSeconds)
}

# ------------------------------------------------------------------- tests

if ($Test) {
    $testExe = "$outDir/engine_tests.exe"
    $coreObjs = @($sources | Where-Object { $_ -like 'src/engine/*' -or $_ -like 'src/game/*' -or $_ -like 'tests/*' } |
        ForEach-Object { Get-ObjPath $_ })
    $linkArgs = $lflags + @('-mconsole', '-o', $testExe) + $coreObjs + $libs
    if (-not (Invoke-Jobs @(@{ Exe = $cxx; Label = $testExe; Args = (Join-Args $linkArgs) }) 'link')) { throw 'Test link failed.' }
    Write-Host ''
    $testArgList = if ($TestArgs) { $TestArgs.Split(' ') } else { @() }
    & $testExe @testArgList
    if ($LASTEXITCODE -ne 0) { throw "Tests failed (exit code $LASTEXITCODE)." }
}

if ($Run -and (Test-Path $exe)) {
    Start-Process -FilePath (Resolve-Path $exe)
}
