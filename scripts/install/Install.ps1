# Installs generalszh.exe into an existing Command & Conquer Generals Zero Hour installation.
#
# Started by Install.cmd / Uninstall.cmd.  Runs on Windows PowerShell 5.1 (Windows 10 and 11).
#
#   - finds the Zero Hour folder (EA App, Origin, older retail installs; or asks for it)
#   - copies generalszh.exe next to game.dat; the game's own files are never changed
#   - allows the game through Windows Firewall for computers on the local network
#   - installs Microsoft's DirectX 9 runtime if d3dx9_43.dll is missing
#   - points the game at the base Generals data if that registry entry is missing
#   - adds a "Zero Hour (LAN)" desktop shortcut
#
# Steps that need administrator rights run in a second, elevated copy of this script, so there
# is a single UAC prompt.
#
# Keep this file plain ASCII: Windows PowerShell 5.1 reads scripts without a BOM as ANSI.

[CmdletBinding()]
param(
	[string]$GameDir,		# Zero Hour folder; found automatically when omitted
	[string]$Exe,			# the executable to install; default: generalszh.exe next to this script
	[switch]$Uninstall,
	[switch]$Unattended,	# never ask; fail instead (automated tests)
	[switch]$NoShortcut,
	[switch]$SkipDirectX,
	[switch]$ForceDirectX,	# install the DirectX runtime even if d3dx9_43.dll is present (tests)
	[switch]$ForceRelaunch,	# use the elevated second copy even when already elevated (tests)
	# Used by the elevated second copy:
	[switch]$Elevated,
	[string]$GeneralsDir,	# base Generals folder to write into HKLM
	[string]$LogFile
)

Set-StrictMode -Version 2
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'	# the progress bar slows Invoke-WebRequest down a lot

$ExeName = 'generalszh.exe'
$ShortcutName = 'Zero Hour (LAN).lnk'
$FirewallGroup = 'Zero Hour (LAN)'
$ZeroHourKey = 'SOFTWARE\Electronic Arts\EA Games\Command and Conquer Generals Zero Hour'
$GeneralsKey = 'SOFTWARE\Electronic Arts\EA Games\Generals'
$DirectXUrl = 'https://download.microsoft.com/download/8/4/A/84A35BF1-DAFE-4AE8-82AF-AD2AE20B6B14/directx_Jun2010_redist.exe'
$DirectXPage = 'https://www.microsoft.com/en-us/download/details.aspx?id=8109'
$OnWindows = $env:OS -eq 'Windows_NT'

# ------------------------------------------------------------------------------------------------
# Output
# ------------------------------------------------------------------------------------------------

function Say([string]$text, [string]$color = '') {
	if ($color) { Write-Host $text -ForegroundColor $color } else { Write-Host $text }
	if ($LogFile) { Add-Content -LiteralPath $LogFile -Value $text }
}
function Ok([string]$text)   { Say "  OK    $text" 'Green' }
function Warn([string]$text) { Say "  NOTE  $text" 'Yellow' }
function Fail([string]$text) { throw $text }

# ------------------------------------------------------------------------------------------------
# Windows helpers (each returns a harmless value when not on Windows, so the file logic can be
# tested elsewhere)
# ------------------------------------------------------------------------------------------------

function Test-Admin {
	if (-not $OnWindows) { return $true }
	$id = [Security.Principal.WindowsIdentity]::GetCurrent()
	return ([Security.Principal.WindowsPrincipal]$id).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

# The game is a 32-bit program: on 64-bit Windows its HKLM\SOFTWARE is the 32-bit registry view.
function Open-RegistryKey([string]$hive, [string]$path, [switch]$Native, [switch]$Create) {
	if (-not $OnWindows) { return $null }
	$view = if ($Native) { [Microsoft.Win32.RegistryView]::Default } else { [Microsoft.Win32.RegistryView]::Registry32 }
	$root = [Microsoft.Win32.RegistryKey]::OpenBaseKey($hive, $view)
	if ($Create) { return $root.CreateSubKey($path) }
	return $root.OpenSubKey($path)
}

function Get-RegistryString([string]$hive, [string]$path, [string]$name, [switch]$Native) {
	try {
		$key = Open-RegistryKey $hive $path -Native:$Native
		if ($key) {
			try { $value = $key.GetValue($name) } finally { $key.Close() }
			if ($value -is [string] -and $value) { return $value }
		}
	} catch { }
	return $null
}

function Set-RegistryString([string]$hive, [string]$path, [string]$name, [string]$value) {
	if (-not $OnWindows) { return }
	$key = Open-RegistryKey $hive $path -Create
	if (-not $key) { Fail "Could not open the registry key $hive\$path." }
	try { $key.SetValue($name, $value, [Microsoft.Win32.RegistryValueKind]::String) } finally { $key.Close() }
}

# ------------------------------------------------------------------------------------------------
# Finding the game
# ------------------------------------------------------------------------------------------------

# A Zero Hour folder holds the Zero Hour archives (W3DZH.big, WindowZH.big, ...).
function Test-ZeroHourDir([string]$dir) {
	if (-not $dir -or -not (Test-Path -LiteralPath $dir -PathType Container)) { return $false }
	return @(Get-ChildItem -LiteralPath $dir -Filter '*ZH.big' -File -ErrorAction SilentlyContinue).Count -gt 0
}

# A base Generals folder holds INI.big or W3D.big (Zero Hour still loads the original game's data).
function Test-GeneralsDir([string]$dir) {
	if (-not $dir -or -not (Test-Path -LiteralPath $dir -PathType Container)) { return $false }
	if (Test-ZeroHourDir $dir) { return $false }
	foreach ($name in 'INI.big', 'W3D.big') {
		if (Test-Path -LiteralPath (Join-Path $dir $name) -PathType Leaf) { return $true }
	}
	return $false
}

# Folders in which the game may be installed, from the registry and the usual install locations.
function Get-SearchRoots {
	$roots = New-Object System.Collections.Generic.List[string]
	foreach ($hive in 'LocalMachine', 'CurrentUser') {
		foreach ($name in 'InstallPath', 'Install Dir') {
			$p = Get-RegistryString $hive $ZeroHourKey $name
			if ($p) { $roots.Add($p) }
		}
	}
	# Uninstall entries written by the EA App, Origin and the retail installers.
	if ($OnWindows) {
		foreach ($hive in 'LocalMachine', 'CurrentUser') {
			foreach ($native in $false, $true) {
				try {
					$key = Open-RegistryKey $hive 'SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall' -Native:$native
					if (-not $key) { continue }
					foreach ($sub in $key.GetSubKeyNames()) {
						$entry = $key.OpenSubKey($sub)
						if (-not $entry) { continue }
						$name = [string]$entry.GetValue('DisplayName')
						$location = [string]$entry.GetValue('InstallLocation')
						$entry.Close()
						if ($location -and $name -match 'Zero Hour|Generals|Ultimate Collection') { $roots.Add($location) }
					}
					$key.Close()
				} catch { }
			}
		}
	}
	$bases = @(@($env:ProgramFiles, ${env:ProgramFiles(x86)}, $env:ProgramW6432) | Where-Object { $_ })
	foreach ($drive in [IO.DriveInfo]::GetDrives()) {
		if ($drive.DriveType -eq 'Fixed' -and $drive.IsReady) { $bases += $drive.RootDirectory.FullName }
	}
	foreach ($base in $bases) {
		foreach ($sub in 'EA Games', 'Origin Games', 'EA Games\Origin Games', 'Games') {
			$roots.Add((Join-Path $base $sub))
		}
	}
	return $roots
}

# Zero Hour folders found within two levels below each search root.
function Find-ZeroHourDirs {
	$found = New-Object System.Collections.Generic.List[string]
	$seen = @{}
	foreach ($root in (Get-SearchRoots)) {
		if (-not (Test-Path -LiteralPath $root -PathType Container)) { continue }
		$dirs = @((Get-Item -LiteralPath $root).FullName)
		$level1 = @(Get-ChildItem -LiteralPath $root -Directory -ErrorAction SilentlyContinue)
		$dirs += $level1 | ForEach-Object { $_.FullName }
		foreach ($d in $level1) {
			$dirs += @(Get-ChildItem -LiteralPath $d.FullName -Directory -ErrorAction SilentlyContinue) | ForEach-Object { $_.FullName }
		}
		foreach ($d in $dirs) {
			$full = $d.TrimEnd('\', '/')
			if ($seen.ContainsKey($full.ToLowerInvariant())) { continue }
			$seen[$full.ToLowerInvariant()] = $true
			if (Test-ZeroHourDir $full) { $found.Add($full) }
		}
	}
	return $found
}

function Select-Folder {
	try {
		Add-Type -AssemblyName System.Windows.Forms
		$dialog = New-Object System.Windows.Forms.FolderBrowserDialog
		$dialog.Description = 'Select the Command & Conquer Generals Zero Hour folder (the one that contains game.dat and the .big files)'
		$dialog.ShowNewFolderButton = $false
		if ($dialog.ShowDialog() -eq [System.Windows.Forms.DialogResult]::OK) { return $dialog.SelectedPath }
		return $null
	} catch {
		return (Read-Host 'Path of the Zero Hour folder (empty to cancel)')
	}
}

function Resolve-GameDir {
	if ($GameDir) {
		if (-not (Test-ZeroHourDir $GameDir)) { Fail "'$GameDir' is not a Zero Hour folder: it has no *ZH.big files." }
		return (Get-Item -LiteralPath $GameDir).FullName.TrimEnd('\', '/')
	}
	Say 'Looking for Zero Hour...'
	$found = @(Find-ZeroHourDirs)
	if ($found.Count -eq 1) { return $found[0] }
	if ($found.Count -gt 1) {
		if ($Unattended) { Fail "Found several Zero Hour folders; pass -GameDir:`n  $($found -join "`n  ")" }
		Say 'Found more than one Zero Hour folder:'
		for ($i = 0; $i -lt $found.Count; ++$i) { Say "  $($i + 1). $($found[$i])" }
		while ($true) {
			$answer = Read-Host "Which one? (1-$($found.Count))"
			$n = 0
			if ([int]::TryParse($answer, [ref]$n) -and $n -ge 1 -and $n -le $found.Count) { return $found[$n - 1] }
		}
	}
	if ($Unattended) { Fail 'Zero Hour was not found; pass -GameDir.' }
	Say 'Zero Hour was not found automatically. Please select its folder.'
	while ($true) {
		$dir = Select-Folder
		if (-not $dir) { Fail 'No folder selected.' }
		if (Test-ZeroHourDir $dir) { return $dir.TrimEnd('\', '/') }
		Say "  '$dir' does not contain the Zero Hour files (*ZH.big). Please try again." 'Yellow'
	}
}

# Where the game will look for the base Generals data, and where that data really is.
function Get-GeneralsStatus([string]$zhDir) {
	$status = @{ Machine = $null; User = $null; Found = $null }
	$status.Machine = Get-RegistryString 'LocalMachine' $GeneralsKey 'InstallPath'
	$status.User = Get-RegistryString 'CurrentUser' $GeneralsKey 'InstallPath'
	# Usually installed next to Zero Hour (EA App: ...\Ultimate Collection\Command and Conquer
	# Generals; Origin: ...\Command and Conquer Generals Zero Hour\Command and Conquer Generals).
	$parent = Split-Path -Parent $zhDir
	foreach ($root in $parent, (Split-Path -Parent $parent)) {
		if (-not $root -or $status.Found) { continue }
		foreach ($d in @(Get-ChildItem -LiteralPath $root -Directory -ErrorAction SilentlyContinue)) {
			if (Test-GeneralsDir $d.FullName) { $status.Found = $d.FullName; break }
		}
	}
	return $status
}

# ------------------------------------------------------------------------------------------------
# The executable
# ------------------------------------------------------------------------------------------------

function Get-Sha256([string]$path) {
	return (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
}

function Resolve-Exe {
	$path = if ($Exe) { $Exe } else { Join-Path $PSScriptRoot $ExeName }
	if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
		Fail "$path was not found. Extract the whole zip file first, then run Install.cmd from the extracted folder."
	}
	$path = (Get-Item -LiteralPath $path).FullName
	# A release zip lists the executable's checksum; a mismatch means a damaged download.
	$sums = Join-Path (Split-Path -Parent $path) 'SHA256SUMS.txt'
	if (Test-Path -LiteralPath $sums) {
		$expected = $null
		foreach ($line in Get-Content -LiteralPath $sums) {
			if ($line -match '^([0-9a-fA-F]{64})\s+\*?(.+)$' -and $Matches[2].Trim() -eq (Split-Path -Leaf $path)) {
				$expected = $Matches[1].ToLowerInvariant()
			}
		}
		if ($expected) {
			if ((Get-Sha256 $path) -ne $expected) { Fail "$path is damaged (its checksum is wrong). Download the zip file again." }
			Ok "$ExeName checksum $expected"
		}
	}
	return $path
}

# ------------------------------------------------------------------------------------------------
# Steps that may need administrator rights
# ------------------------------------------------------------------------------------------------

function Test-Writable([string]$dir) {
	$probe = Join-Path $dir ('zh-install-' + [guid]::NewGuid().ToString('N') + '.tmp')
	try {
		[IO.File]::WriteAllText($probe, '')
		Remove-Item -LiteralPath $probe -Force
		return $true
	} catch {
		return $false
	}
}

function Get-D3DX9Path {
	$sys = if ($OnWindows -and [Environment]::Is64BitOperatingSystem) { 'SysWOW64' } else { 'System32' }
	return Join-Path (Join-Path $env:WINDIR $sys) 'd3dx9_43.dll'
}

function Test-D3DX9 {
	if (-not $OnWindows) { return $true }
	return Test-Path -LiteralPath (Get-D3DX9Path)
}

function Assert-MicrosoftSigned([string]$path) {
	$sig = Get-AuthenticodeSignature -LiteralPath $path
	if ($sig.Status -ne 'Valid' -or -not $sig.SignerCertificate -or $sig.SignerCertificate.Subject -notmatch 'O=Microsoft Corporation') {
		Fail "$(Split-Path -Leaf $path) is not signed by Microsoft (status: $($sig.Status)); not running it."
	}
}

function Install-DirectX {
	Say 'Installing the Microsoft DirectX 9 runtime (about 100 MB to download)...'
	$work = Join-Path $env:WINDIR ('Temp\zh-directx-' + [guid]::NewGuid().ToString('N'))
	New-Item -ItemType Directory -Path $work | Out-Null
	try {
		$redist = Join-Path $work 'directx_Jun2010_redist.exe'
		[Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12
		Invoke-WebRequest -Uri $DirectXUrl -OutFile $redist -UseBasicParsing
		Assert-MicrosoftSigned $redist
		$files = Join-Path $work 'files'
		$p = Start-Process -FilePath $redist -ArgumentList '/Q', "/T:`"$files`"" -Wait -PassThru
		if ($p.ExitCode -ne 0) { Fail "Unpacking the DirectX runtime failed (exit code $($p.ExitCode))." }
		$setup = Join-Path $files 'DXSETUP.exe'
		Assert-MicrosoftSigned $setup
		$p = Start-Process -FilePath $setup -ArgumentList '/silent' -Wait -PassThru
		if ($p.ExitCode -ne 0) { Fail "DirectX setup failed (exit code $($p.ExitCode))." }
	} finally {
		Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue
	}
	if (-not (Test-D3DX9)) { Fail 'DirectX setup finished, but d3dx9_43.dll is still missing.' }
	Ok 'DirectX 9 runtime installed'
}

function Set-FirewallRule([string]$exePath) {
	if (-not $OnWindows) { return }
	Get-NetFirewallRule -Group $FirewallGroup -ErrorAction SilentlyContinue | Remove-NetFirewallRule
	if (-not $exePath) { Ok 'Firewall rule removed'; return }
	# Only computers on the local network; any network profile, since home Wi-Fi is often "Public".
	New-NetFirewallRule -DisplayName $FirewallGroup -Group $FirewallGroup -Direction Inbound -Action Allow `
		-Program $exePath -Protocol UDP -RemoteAddress LocalSubnet -Profile Any | Out-Null
	Ok 'Windows Firewall allows LAN games (local network only)'
}

function Invoke-AdminSteps([string]$zhDir, [string]$exePath, [bool]$installDirectX, [string]$machineGeneralsDir) {
	$target = Join-Path $zhDir $ExeName
	if ($Uninstall) {
		if (Test-Path -LiteralPath $target) {
			Remove-Item -LiteralPath $target -Force
			Ok "Removed $target"
		} else {
			Ok "$ExeName was not installed in $zhDir"
		}
		Set-FirewallRule $null
		return
	}
	try {
		Copy-Item -LiteralPath $exePath -Destination $target -Force
	} catch {
		Fail "Could not copy $ExeName into $zhDir. Is the game still running? ($($_.Exception.Message))"
	}
	if ($OnWindows) { Unblock-File -LiteralPath $target }	# downloaded files carry a "from the internet" mark
	if ((Get-Sha256 $target) -ne (Get-Sha256 $exePath)) { Fail "The copy of $ExeName in $zhDir differs from the original." }
	Ok "Installed $target"
	Set-FirewallRule $target
	if ($machineGeneralsDir) {
		Set-RegistryString 'LocalMachine' $GeneralsKey 'InstallPath' ($machineGeneralsDir.TrimEnd('\') + '\')
		Ok "Base Generals data: $machineGeneralsDir"
	}
	if ($installDirectX) { Install-DirectX }
}

# Runs Invoke-AdminSteps in an elevated copy of this script and shows its output.
function Invoke-Elevated([string]$zhDir, [string]$exePath, [bool]$installDirectX, [string]$machineGeneralsDir) {
	$log = Join-Path ([IO.Path]::GetTempPath()) ('zh-install-' + [guid]::NewGuid().ToString('N') + '.log')
	# Quote each argument; a trailing backslash would escape the closing quote.
	function Q([string]$s) { return '"' + $s.TrimEnd('\') + '"' }
	$arguments = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', (Q $PSCommandPath), '-Elevated',
		'-GameDir', (Q $zhDir), '-LogFile', (Q $log))
	if ($Uninstall) { $arguments += '-Uninstall' } else { $arguments += @('-Exe', (Q $exePath)) }
	if ($installDirectX) { $arguments += '-ForceDirectX' } else { $arguments += '-SkipDirectX' }
	if ($machineGeneralsDir) { $arguments += @('-GeneralsDir', (Q $machineGeneralsDir)) }
	$powershell = (Get-Process -Id $PID).Path
	Say 'Windows will ask for administrator permission now.'
	if ($installDirectX) { Say '(Downloading DirectX can take a few minutes; the window stays quiet meanwhile.)' }
	try {
		$p = Start-Process -FilePath $powershell -ArgumentList $arguments -Verb RunAs -WindowStyle Hidden -Wait -PassThru
	} catch {
		Fail 'Administrator permission was not given, so nothing was installed.'
	}
	if (Test-Path -LiteralPath $log) {
		Get-Content -LiteralPath $log | ForEach-Object {
			if ($_ -match '^\s+OK\s') { Write-Host $_ -ForegroundColor Green }
			elseif ($_ -match '^ERROR') { Write-Host $_ -ForegroundColor Red }
			else { Write-Host $_ }
		}
		Remove-Item -LiteralPath $log -Force -ErrorAction SilentlyContinue
	}
	if ($p.ExitCode -ne 0) { Fail 'The administrator part did not finish (see above).' }
}

# ------------------------------------------------------------------------------------------------
# Steps that run as the user
# ------------------------------------------------------------------------------------------------

function Set-Shortcut([string]$zhDir, [string]$exePath) {
	if (-not $OnWindows -or $NoShortcut) { return }
	$link = Join-Path ([Environment]::GetFolderPath('Desktop')) $ShortcutName
	if (-not $exePath) {
		if (Test-Path -LiteralPath $link) { Remove-Item -LiteralPath $link -Force; Ok 'Removed the desktop shortcut' }
		return
	}
	$shell = New-Object -ComObject WScript.Shell
	$shortcut = $shell.CreateShortcut($link)
	$shortcut.TargetPath = $exePath
	$shortcut.WorkingDirectory = $zhDir
	$shortcut.IconLocation = "$exePath,0"
	$shortcut.Description = 'Command & Conquer Generals Zero Hour with the new LAN network code'
	$shortcut.Save()
	Ok "Desktop shortcut '$([IO.Path]::GetFileNameWithoutExtension($ShortcutName))'"
}

# ------------------------------------------------------------------------------------------------
# Main
# ------------------------------------------------------------------------------------------------

function Main {
	if ($Elevated) {
		# Second copy: GameDir and the decisions were made by the first one.
		$exePath = if ($Uninstall) { $null } else { (Get-Item -LiteralPath $Exe).FullName }
		Invoke-AdminSteps $GameDir $exePath ([bool]$ForceDirectX) $GeneralsDir
		return
	}

	Say ''
	if ($Uninstall) { Say 'Zero Hour (LAN) - uninstall' 'Cyan' } else { Say 'Zero Hour (LAN) - install' 'Cyan' }
	Say ''
	$exePath = if ($Uninstall) { $null } else { Resolve-Exe }
	$zhDir = Resolve-GameDir
	Ok "Zero Hour folder: $zhDir"

	$installDirectX = $false
	$machineGeneralsDir = $null
	$userGeneralsDir = $null
	if (-not $Uninstall) {
		if ($ForceDirectX -or (-not $SkipDirectX -and -not (Test-D3DX9))) {
			$installDirectX = $true
		} elseif (-not $SkipDirectX) {
			Ok 'DirectX 9 runtime is installed'
		}

		# Zero Hour also loads the original Generals data, from the folder in the registry
		# (HKLM first, then HKCU).  Some installs lack that entry or point to a removed folder.
		$g = Get-GeneralsStatus $zhDir
		if ($g.Machine -and (Test-GeneralsDir $g.Machine)) {
			Ok "Base Generals data: $($g.Machine)"
		} elseif (-not $g.Machine -and $g.User -and (Test-GeneralsDir $g.User)) {
			Ok "Base Generals data: $($g.User)"
		} elseif ($g.Found) {
			if ($g.Machine) { $machineGeneralsDir = $g.Found } else { $userGeneralsDir = $g.Found }
		} else {
			Warn 'The original Generals data was not found. Zero Hour needs it; install "Command & Conquer Generals" too.'
		}
	}

	# The firewall rule always needs administrator rights on Windows.
	$needAdmin = $OnWindows -or -not (Test-Writable $zhDir)
	if ($ForceRelaunch -or ($needAdmin -and -not (Test-Admin))) {
		Invoke-Elevated $zhDir $exePath $installDirectX $machineGeneralsDir
	} else {
		Invoke-AdminSteps $zhDir $exePath $installDirectX $machineGeneralsDir
	}

	if ($userGeneralsDir) {
		Set-RegistryString 'CurrentUser' $GeneralsKey 'InstallPath' ($userGeneralsDir.TrimEnd('\') + '\')
		Ok "Base Generals data: $userGeneralsDir"
	}
	if ($Uninstall) {
		Set-Shortcut $zhDir $null
		Say ''
		Say 'Done. The game itself was not changed.' 'Cyan'
		return
	}
	Set-Shortcut $zhDir (Join-Path $zhDir $ExeName)

	Say ''
	Say 'Done. Start the game with the "Zero Hour (LAN)" shortcut on your desktop.' 'Cyan'
	Say 'Everyone in a LAN game needs this same version.'
	Say 'To remove it again, run Uninstall.cmd (or delete generalszh.exe from the Zero Hour folder).'
}

try {
	Main
	exit 0
} catch {
	Say ''
	Say "ERROR: $($_.Exception.Message)" 'Red'
	exit 1
}
