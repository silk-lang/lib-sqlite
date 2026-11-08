#Requires -Version 7.2
<#
.SYNOPSIS
Build the pinned SQLite amalgamation and shim for Silk on Windows, without Python.
.DESCRIPTION
Run in an x64 Visual Studio developer PowerShell with LLVM 22 on PATH, or set SILK_CLANG.
The manifest is the source of the archive pin and SQLite feature flags.
#>
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if (-not $IsWindows) { throw 'SQLite Windows preparation must run on Windows.' }
if (-not $env:LIB -or -not $env:INCLUDE) {
    throw 'Run from an x64 Visual Studio developer PowerShell with the Windows SDK configured.'
}

function Invoke-Native {
    param([string]$Program, [string[]]$Arguments)
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Program failed with exit code $LASTEXITCODE" }
}

$packageDir = Split-Path $PSScriptRoot -Parent
$manifest = Get-Content (Join-Path $packageDir 'silk.toml') -Raw
$archiveTable = [regex]::Match($manifest, '(?ms)^\[native\.archive\.sqlite\][ \t]*\r?\n(.*?)(?=^\[|\z)')
if (-not $archiveTable.Success) { throw 'silk.toml has no [native.archive.sqlite] table.' }
$archiveFields = @{}
foreach ($key in @('url', 'sha256')) {
    $field = [regex]::Match($archiveTable.Groups[1].Value, "(?m)^$key = `"([^`"]+)`"")
    if (-not $field.Success) { throw "silk.toml is missing SQLite's $key." }
    $archiveFields[$key] = $field.Groups[1].Value
}
$strip = [regex]::Match($archiveTable.Groups[1].Value, '(?m)^strip = (\d+)')
if (-not $strip.Success) { throw 'silk.toml is missing SQLite archive strip.' }
$source = [regex]::Match($manifest, '(?m)^\s*\{ c = "sqlite:sqlite3\.c", flags = (\[[^\r\n]*?\])')
if (-not $source.Success) { throw 'silk.toml has no sqlite:sqlite3.c source flags.' }
$defines = @($source.Groups[1].Value | ConvertFrom-Json | Where-Object { $_.StartsWith('-D') })
if ($defines.Count -eq 0) { throw 'SQLite source flags contain no feature defines.' }

$clang = if ($env:SILK_CLANG) { (Get-Command $env:SILK_CLANG).Source } else { (Get-Command clang.exe).Source }
$llvmLib = Join-Path (Split-Path $clang -Parent) 'llvm-lib.exe'
if (-not (Test-Path $llvmLib)) { throw "Missing LLVM archiver: $llvmLib" }
$outDir = Join-Path $packageDir '.cache/sqlite/windows'
$unpacked = Join-Path $outDir 'source'
New-Item -ItemType Directory -Force $unpacked | Out-Null
$archive = Join-Path $outDir ($archiveFields.sha256 + '.tar.gz')
if (-not (Test-Path $archive)) {
    $download = $archive + '.download'
    Invoke-WebRequest -Uri $archiveFields.url -OutFile $download
    if ((Get-FileHash $download -Algorithm SHA256).Hash.ToLowerInvariant() -ne $archiveFields.sha256) {
        Remove-Item $download
        throw 'SQLite archive SHA-256 does not match silk.toml.'
    }
    Move-Item $download $archive
}
if ((Get-FileHash $archive -Algorithm SHA256).Hash.ToLowerInvariant() -ne $archiveFields.sha256) {
    throw "Cached SQLite archive is corrupt; remove $archive and retry."
}
Invoke-Native 'tar.exe' @('-xf', $archive, '-C', $unpacked, "--strip-components=$($strip.Groups[1].Value)")
foreach ($name in @('sqlite3.c', 'sqlite3.h')) {
    if (-not (Test-Path (Join-Path $unpacked $name))) { throw "SQLite archive contains no $name." }
}

# Silk has no CRT startup: use the DLL runtime, whose DLL initializes itself.
$commonFlags = @('--target=x86_64-pc-windows-msvc', '-fms-runtime-lib=dll', '-O2', '-D_CRT_SECURE_NO_WARNINGS')
$sqliteObject = Join-Path $outDir 'sqlite3.obj'
$shimObject = Join-Path $outDir 'shim.obj'
Invoke-Native $clang ($commonFlags + @('-w') + $defines + @('-c', (Join-Path $unpacked 'sqlite3.c'), '-o', $sqliteObject))
Invoke-Native $clang ($commonFlags + @('-std=c11', '-Wall', '-Wextra', '-Werror', "-I$unpacked", '-c', (Join-Path $packageDir 'native/shim.c'), '-o', $shimObject))
$library = Join-Path $outDir 'silksqlite.lib'
if (Test-Path $library) { Remove-Item $library }
Invoke-Native $llvmLib @("/out:$library", $sqliteObject, $shimObject)

foreach ($name in @('kernel32', 'ucrt', 'vcruntime')) {
    $found = $false
    foreach ($directory in ($env:LIB -split ';')) {
        if (-not $directory) { continue }
        $candidate = Join-Path $directory "$name.lib"
        if (Test-Path $candidate) {
            Copy-Item $candidate (Join-Path $outDir "$name.lib") -Force
            $found = $true
            break
        }
    }
    if (-not $found) { throw "No $name.lib found on LIB; configure the x64 Windows SDK and MSVC tools." }
}

$silkHomeDir = if ($env:SILK_HOME) { $env:SILK_HOME } else { Join-Path $HOME '.silk' }
New-Item -ItemType Directory -Force $silkHomeDir | Out-Null
$overrides = Join-Path $silkHomeDir 'native-overrides.toml'
[string]$text = if (Test-Path $overrides) { Get-Content $overrides -Raw } else { '' }
# Silk uses /C:/... as an absolute Windows path. JSON string escaping is also valid in TOML.
$silkPath = '/' + (Resolve-Path $outDir).Path.Replace('\', '/').TrimStart('/')
$entry = "[native.override.sqlite]`npath = $($silkPath | ConvertTo-Json -Compress)`n`n"
$tablePattern = '(?ms)^\[native\.override\.sqlite\][ \t]*\r?\n.*?(?=^\[|\z)'
if ([regex]::IsMatch($text, $tablePattern)) {
    $text = [regex]::Replace($text, $tablePattern, [System.Text.RegularExpressions.MatchEvaluator]{ param($match) $entry })
} else {
    $text = $text.TrimEnd() + "`n`n" + $entry
}
Set-Content -Path $overrides -Value $text -Encoding utf8NoBOM -NoNewline
Write-Host "Built $library"
Write-Host "Configured SQLite in $overrides"
