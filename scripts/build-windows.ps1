# Builds generalszh.exe on Windows without installing anything system-wide.
#
# Started by build.cmd in the repository root.  Downloads a fixed version of each tool into
# .tools\ (checked against its SHA-256), then configures and builds with CMake and Ninja.
# The compiler is the same version as the one CI uses (GCC 13.2, MinGW-w64 11.0.1, MSVCRT,
# i686, POSIX threads, DWARF-2 exceptions); the resulting exe still differs from CI's byte for
# byte, so players should all use the same build (see BUILDING.md).
#
#   build.cmd                    build\win32\generalszh.exe
#   build.cmd -Install           ... then install it into the Zero Hour folder
#   build.cmd -Clean             delete the build folder first
#
# Keep this file plain ASCII: Windows PowerShell 5.1 reads scripts without a BOM as ANSI.

[CmdletBinding()]
param(
	[switch]$Clean,
	[switch]$Install,
	[string]$BuildDir
)

Set-StrictMode -Version 2
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'	# the progress bar slows Invoke-WebRequest down a lot

$Root = Split-Path -Parent $PSScriptRoot
$ToolsDir = Join-Path $Root '.tools'
if (-not $BuildDir) { $BuildDir = Join-Path $Root 'build\win32' }

$Tools = @(
	@{
		Name   = 'MinGW-w64 GCC 13.2.0 (WinLibs, i686, POSIX threads, MSVCRT)'
		Url    = 'https://github.com/brechtsanders/winlibs_mingw/releases/download/13.2.0posix-18.1.5-11.0.1-msvcrt-r8/winlibs-i686-posix-dwarf-gcc-13.2.0-mingw-w64msvcrt-11.0.1-r8.zip'
		Sha256 = 'c29b540409ffe3769407a2e58b76c391ebafd7e7eca8b2b17e71dffe43db419b'
		Dir    = 'mingw'		# the archive holds mingw32\...
		Bin    = 'mingw\mingw32\bin'
	},
	@{
		Name   = 'CMake 3.30.5'
		Url    = 'https://github.com/Kitware/CMake/releases/download/v3.30.5/cmake-3.30.5-windows-x86_64.zip'
		Sha256 = '5ab6e1faf20256ee4f04886597e8b6c3b1bd1297b58a68a58511af013710004b'
		Dir    = 'cmake'		# the archive holds cmake-3.30.5-windows-x86_64\...
		Bin    = 'cmake\cmake-3.30.5-windows-x86_64\bin'
	},
	@{
		Name   = 'Ninja 1.12.1'
		Url    = 'https://github.com/ninja-build/ninja/releases/download/v1.12.1/ninja-win.zip'
		Sha256 = 'f550fec705b6d6ff58f2db3c374c2277a37691678d6aba463adcbb129108467a'
		Dir    = 'ninja'
		Bin    = 'ninja'
	}
)

function Say([string]$text, [string]$color = 'Cyan') { Write-Host $text -ForegroundColor $color }

function Invoke-Checked([string]$exe, [string[]]$arguments) {
	& $exe @arguments
	if ($LASTEXITCODE -ne 0) { throw "$(Split-Path -Leaf $exe) failed (exit code $LASTEXITCODE)." }
}

function Expand-Zip([string]$zip, [string]$dest) {
	# Windows 10's tar.exe extracts zip files far faster than Expand-Archive.  Use it by full path:
	# a tar.exe from Git or MSYS2 earlier on PATH cannot read zip files.
	$tar = Join-Path $env:WINDIR 'System32\tar.exe'
	if (Test-Path -LiteralPath $tar) {
		Invoke-Checked $tar @('-xf', $zip, '-C', $dest)
	} else {
		Expand-Archive -LiteralPath $zip -DestinationPath $dest -Force
	}
}

# Downloads and unpacks a tool unless the same version is already there.
function Get-Tool($tool) {
	$dir = Join-Path $ToolsDir $tool.Dir
	$stamp = Join-Path $dir '.sha256'
	if ((Test-Path -LiteralPath $stamp) -and (Get-Content -LiteralPath $stamp -Raw).Trim() -eq $tool.Sha256) {
		return
	}
	Say "Downloading $($tool.Name)..."
	$downloads = Join-Path $ToolsDir 'downloads'
	New-Item -ItemType Directory -Force -Path $downloads | Out-Null
	$zip = Join-Path $downloads ([IO.Path]::GetFileName($tool.Url))
	if (-not (Test-Path -LiteralPath $zip) -or (Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash -ne $tool.Sha256) {
		[Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12
		Invoke-WebRequest -Uri $tool.Url -OutFile $zip -UseBasicParsing
		$hash = (Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash
		if ($hash -ne $tool.Sha256) {
			Remove-Item -LiteralPath $zip -Force
			throw "$($tool.Name): the download's SHA-256 is $hash, expected $($tool.Sha256)."
		}
	}
	if (Test-Path -LiteralPath $dir) { Remove-Item -LiteralPath $dir -Recurse -Force }
	New-Item -ItemType Directory -Force -Path $dir | Out-Null
	Say "Unpacking $($tool.Name)..."
	Expand-Zip $zip $dir
	Set-Content -LiteralPath $stamp -Value $tool.Sha256
	Remove-Item -LiteralPath $zip -Force
}

try {
	foreach ($tool in $Tools) { Get-Tool $tool }
	$paths = $Tools | ForEach-Object { Join-Path $ToolsDir $_.Bin }
	# Our tools first, so another compiler or CMake on PATH cannot get in the way.
	$env:PATH = ($paths -join ';') + ';' + $env:PATH
	$mingw = Join-Path $ToolsDir $Tools[0].Bin
	$cmake = Join-Path (Join-Path $ToolsDir $Tools[1].Bin) 'cmake.exe'
	$ninja = Join-Path (Join-Path $ToolsDir $Tools[2].Bin) 'ninja.exe'

	if ($Clean -and (Test-Path -LiteralPath $BuildDir)) {
		Say "Deleting $BuildDir..."
		Remove-Item -LiteralPath $BuildDir -Recurse -Force
	}

	Say 'Configuring...'
	$fwd = { param($p) $p -replace '\\', '/' }
	Invoke-Checked $cmake @('-S', (Join-Path $Root 'GeneralsMD\Code'), '-B', $BuildDir, '-G', 'Ninja',
		'-DCMAKE_BUILD_TYPE=Release',
		"-DCMAKE_MAKE_PROGRAM=$(& $fwd $ninja)",
		"-DCMAKE_C_COMPILER=$(& $fwd (Join-Path $mingw 'gcc.exe'))",
		"-DCMAKE_CXX_COMPILER=$(& $fwd (Join-Path $mingw 'g++.exe'))",
		"-DCMAKE_RC_COMPILER=$(& $fwd (Join-Path $mingw 'windres.exe'))")

	Say 'Building (10-20 minutes the first time)...'
	Invoke-Checked $cmake @('--build', $BuildDir)

	# Same packaging as CI: strip the symbols.
	$dist = Join-Path $BuildDir 'dist'
	New-Item -ItemType Directory -Force -Path $dist | Out-Null
	$exe = Join-Path $dist 'generalszh.exe'
	Invoke-Checked (Join-Path $mingw 'strip.exe') @('-o', $exe, (Join-Path $BuildDir 'generalszh.exe'))
	$hash = (Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash.ToLowerInvariant()
	Say ''
	Say "Built $exe" 'Green'
	Say "SHA-256 $hash" 'Green'

	if ($Install) {
		& (Join-Path $PSScriptRoot 'install\Install.ps1') -Exe $exe
		if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
	}
	exit 0
} catch {
	Write-Host ''
	Write-Host "ERROR: $($_.Exception.Message)" -ForegroundColor Red
	exit 1
}
