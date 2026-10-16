# Checks that a build of the game still simulates exactly like the build that recorded the replays.
#
# For every .rep file, runs  generalszh.exe -replayCheck <file>  which plays the replay without
# drawing, compares the game state CRCs with those stored in the replay, and exits with:
#   0 PASS         every CRC matched
#   1 MISMATCH     the simulation diverged (first_mismatch_frame in ReplayCheck.txt)
#   2 NO_CRCS      the replay holds no CRCs to compare (too short?)
#   3 LOAD_FAILED  the replay could not be opened
#
# Replays recorded with the original EA executable will not match: this fork changes the
# simulation's math on purpose (see NETWORK_AND_DETERMINISM.md). Record reference replays with
# this fork's executable, then use them to check later builds.
#
# Usage (from the Zero Hour installation folder, where generalszh.exe lives):
#   powershell -ExecutionPolicy Bypass -File check-replays.ps1 -Replays "$env:USERPROFILE\Documents\Command and Conquer Generals Zero Hour Data\Replays"

param(
	[string]$Exe = ".\generalszh.exe",
	[Parameter(Mandatory = $true)][string]$Replays,
	[int]$TimeoutMinutes = 30
)

$names = @{ 0 = "PASS"; 1 = "MISMATCH"; 2 = "NO_CRCS"; 3 = "LOAD_FAILED" }
$exePath = (Resolve-Path $Exe).Path
$workDir = Split-Path $exePath
$files = Get-ChildItem -Path $Replays -Filter *.rep | Sort-Object Name
if ($files.Count -eq 0) { Write-Error "No .rep files in $Replays"; exit 3 }

$failed = 0
foreach ($f in $files) {
	$start = Get-Date
	$p = Start-Process -FilePath $exePath -ArgumentList @("-replayCheck", "`"$($f.FullName)`"") `
		-WorkingDirectory $workDir -PassThru
	if (-not $p.WaitForExit($TimeoutMinutes * 60 * 1000)) {
		$p.Kill()
		$result = "TIMEOUT"
	} else {
		$result = $names[$p.ExitCode]
		if (-not $result) { $result = "CRASH (exit code $($p.ExitCode))" }
	}
	$seconds = [int]((Get-Date) - $start).TotalSeconds
	"{0,-12} {1,5}s  {2}" -f $result, $seconds, $f.Name
	if ($result -ne "PASS") { $failed++ }
}

""
"{0} of {1} replays passed. Details: ReplayCheck.txt in the game's user data folder." -f ($files.Count - $failed), $files.Count
exit [int]($failed -gt 0)
