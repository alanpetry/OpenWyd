<#
.SYNOPSIS
Starts, inspects, or cleanly stops a source-built OpenWyd server stack.

.DESCRIPTION
Operates only on an output created by build_windows_servers_from_source.ps1.
Before startup it validates the output sentinel, executable hashes, baseline
test-account hashes, and required working directories. Readiness requires
DBSrv ports 7514/8895, TMSrv port 8281, and an established TMSrv-to-DBSrv
loopback connection.

.PARAMETER Action
Start, Status, or Stop.

.PARAMETER BuildRoot
Source-build output root. Defaults to artifacts/server-stack/source-build.

.PARAMETER RestoreBaseline
For Start only, restore the generated account/char/capsule baseline before
launching either server.

.PARAMETER ForceOnFailure
For Stop only, force-terminate a server if its official WM_CLOSE path fails.
Forced termination is reported and remains a failure-cleanup fallback.
#>
[CmdletBinding()]
param(
    [ValidateSet("Start", "Status", "Stop")]
    [string]$Action = "Status",

    [string]$BuildRoot = "",

    [string]$RepoRoot = "",

    [switch]$RestoreBaseline,

    [switch]$ForceOnFailure,

    [Alias("h")]
    [switch]$Help
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

if ($Help) {
    @"
Usage:
  powershell -ExecutionPolicy Bypass -File tools/run_windows_servers.ps1 [options]

Options:
  -Action Start|Status|Stop  Operation (default: Status).
  -BuildRoot <path>          Source-built server output.
  -RepoRoot <path>           Override detected repository root.
  -RestoreBaseline           Restore deterministic state before Start.
  -ForceOnFailure            Allow forced cleanup after failed clean Stop.
  -Help                      Print this message.
"@
    return
}

function Get-FullPath {
    param([string]$Path, [string]$BasePath = (Get-Location).Path)
    if ([IO.Path]::IsPathRooted($Path)) {
        return [IO.Path]::GetFullPath($Path)
    }
    return [IO.Path]::GetFullPath((Join-Path $BasePath $Path))
}

function Test-PathWithin {
    param([string]$Candidate, [string]$Parent)
    $candidatePath = (Get-FullPath $Candidate).TrimEnd('\', '/')
    $parentPath = (Get-FullPath $Parent).TrimEnd('\', '/')
    return $candidatePath.Equals($parentPath, [StringComparison]::OrdinalIgnoreCase) -or
        $candidatePath.StartsWith(
            $parentPath + [IO.Path]::DirectorySeparatorChar,
            [StringComparison]::OrdinalIgnoreCase
        )
}

function Assert-File {
    param([string]$Path, [string]$Description)
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "$Description not found: $Path"
    }
}

function Assert-Directory {
    param([string]$Path, [string]$Description)
    if (-not (Test-Path -LiteralPath $Path -PathType Container)) {
        throw "$Description not found: $Path"
    }
}

function Write-JsonFile {
    param([string]$Path, [object]$Value)
    $json = $Value | ConvertTo-Json -Depth 10
    [IO.File]::WriteAllText(
        $Path,
        $json + [Environment]::NewLine,
        (New-Object Text.UTF8Encoding($false))
    )
}

function Get-ListeningPorts {
    return @(
        [Net.NetworkInformation.IPGlobalProperties]::GetIPGlobalProperties().GetActiveTcpListeners() |
            ForEach-Object Port |
            Sort-Object -Unique
    )
}

function Wait-Ports {
    param(
        [Diagnostics.Process]$Process,
        [int[]]$Ports,
        [int]$TimeoutSeconds,
        [string]$Name
    )

    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    do {
        $Process.Refresh()
        if ($Process.HasExited) {
            throw "$Name exited before readiness with code $($Process.ExitCode)."
        }
        $listening = @(Get-ListeningPorts)
        $missing = @($Ports | Where-Object { $_ -notin $listening })
        if ($missing.Count -eq 0) {
            return
        }
        Start-Sleep -Milliseconds 250
    } while ([DateTime]::UtcNow -lt $deadline)

    throw "$Name readiness timeout; missing ports: $($missing -join ', ')"
}

function Get-EstablishedDbConnection {
    param([int]$TmPid)
    return @(
        Get-NetTCPConnection -State Established -ErrorAction SilentlyContinue |
            Where-Object {
                $_.OwningProcess -eq $TmPid -and
                $_.RemoteAddress -eq "127.0.0.1" -and
                $_.RemotePort -eq 7514
            }
    )
}

function Wait-DbConnection {
    param([Diagnostics.Process]$Process, [int]$TimeoutSeconds)
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    do {
        $Process.Refresh()
        if ($Process.HasExited) {
            throw "TMSrv exited before its DBSrv connection was verified."
        }
        $connections = @(Get-EstablishedDbConnection $Process.Id)
        if ($connections.Count -ne 0) {
            return $connections[0]
        }
        Start-Sleep -Milliseconds 250
    } while ([DateTime]::UtcNow -lt $deadline)

    throw "TMSrv did not establish a loopback connection to DBSrv:7514."
}

function Get-ValidatedProcess {
    param([int]$Id, [string]$ExpectedPath, [string]$Name)
    $process = Get-Process -Id $Id -ErrorAction SilentlyContinue
    if ($null -eq $process) {
        return $null
    }
    $actualPath = $process.Path
    if (-not $actualPath.Equals($ExpectedPath, [StringComparison]::OrdinalIgnoreCase)) {
        throw "$Name PID $Id does not point at the expected source build: $actualPath"
    }
    return $process
}

if ([string]::IsNullOrWhiteSpace($RepoRoot)) {
    $RepoRoot = Split-Path -Parent $PSScriptRoot
}
$RepoRoot = Get-FullPath $RepoRoot
$artifactsRoot = Join-Path $RepoRoot "artifacts"
if ([string]::IsNullOrWhiteSpace($BuildRoot)) {
    $BuildRoot = Join-Path $artifactsRoot "server-stack\source-build"
}
$BuildRoot = Get-FullPath $BuildRoot $RepoRoot
if (-not (Test-PathWithin $BuildRoot $artifactsRoot) -or
    $BuildRoot.Equals($artifactsRoot, [StringComparison]::OrdinalIgnoreCase)) {
    throw "BuildRoot must be a strict descendant of repository artifacts/: $BuildRoot"
}

$sentinelPath = Join-Path $BuildRoot ".openwyd-server-build-root.json"
$metadataPath = Join-Path $BuildRoot "manifests\build-metadata.json"
$statePath = Join-Path $BuildRoot "manifests\server-process-state.json"
Assert-File $sentinelPath "server build sentinel"
Assert-File $metadataPath "server build metadata"
$sentinel = Get-Content -LiteralPath $sentinelPath -Raw | ConvertFrom-Json
if ($sentinel.schema -ne "openwyd-server-source-build-root-v1" -or
    -not ([string]$sentinel.outputRoot).Equals($BuildRoot, [StringComparison]::OrdinalIgnoreCase)) {
    throw "BuildRoot is not owned by the source-build tool: $BuildRoot"
}
$metadata = Get-Content -LiteralPath $metadataPath -Raw | ConvertFrom-Json
if ($metadata.status -ne "succeeded" -or $metadata.projects.Count -ne 2) {
    throw "A successful All build is required before operating the stack: $metadataPath"
}

$dbBuild = @($metadata.projects | Where-Object name -eq "DBSrv")
$tmBuild = @($metadata.projects | Where-Object name -eq "TMSrv")
if ($dbBuild.Count -ne 1 -or $tmBuild.Count -ne 1) {
    throw "Build metadata must contain exactly one DBSrv and one TMSrv."
}
$dbRun = Join-Path $BuildRoot "runtime\Server\DBSrv\run"
$tmRun = Join-Path $BuildRoot "runtime\Server\TMSrv\run"
$dbExe = Join-Path $dbRun "DBSrv.exe"
$tmExe = Join-Path $tmRun "TMSrv.exe"
Assert-Directory $dbRun "DBSrv run directory"
Assert-Directory $tmRun "TMSrv run directory"
Assert-File $dbExe "DBSrv source build"
Assert-File $tmExe "TMSrv source build"
if (-not (Get-FileHash -Algorithm SHA256 -LiteralPath $dbExe).Hash.Equals(
        [string]$dbBuild[0].sha256,
        [StringComparison]::OrdinalIgnoreCase
    )) {
    throw "DBSrv runtime hash differs from build metadata."
}
if (-not (Get-FileHash -Algorithm SHA256 -LiteralPath $tmExe).Hash.Equals(
        [string]$tmBuild[0].sha256,
        [StringComparison]::OrdinalIgnoreCase
    )) {
    throw "TMSrv runtime hash differs from build metadata."
}

function Assert-TestAccounts {
    foreach ($account in @($metadata.testAccounts.accounts)) {
        $accountFile = Join-Path $dbRun ([string]$account.relativeFile)
        Assert-File $accountFile "official test account"
        $hash = (Get-FileHash -Algorithm SHA256 -LiteralPath $accountFile).Hash
        if (-not $hash.Equals([string]$account.sha256, [StringComparison]::OrdinalIgnoreCase)) {
            throw "Test account differs from the deterministic baseline: $accountFile"
        }
    }
}

if ($Action -eq "Status") {
    $state = if (Test-Path -LiteralPath $statePath) {
        Get-Content -LiteralPath $statePath -Raw | ConvertFrom-Json
    }
    else {
        $null
    }
    $dbProcess = if ($null -ne $state) {
        Get-ValidatedProcess ([int]$state.dbsrv.pid) $dbExe "DBSrv"
    }
    else {
        $null
    }
    $tmProcess = if ($null -ne $state) {
        Get-ValidatedProcess ([int]$state.tmsrv.pid) $tmExe "TMSrv"
    }
    else {
        $null
    }
    $ports = @(Get-ListeningPorts | Where-Object { $_ -in @(7514, 8281, 8895) })
    $connections = @()
    if ($null -ne $tmProcess) {
        $connections = @(Get-EstablishedDbConnection $tmProcess.Id)
    }
    [ordered]@{
        buildRoot = $BuildRoot
        dbsrv = if ($null -ne $dbProcess) { [ordered]@{ pid = $dbProcess.Id; running = $true } } else { $null }
        tmsrv = if ($null -ne $tmProcess) { [ordered]@{ pid = $tmProcess.Id; running = $true } } else { $null }
        listeningPorts = $ports
        internalConnectionEstablished = $connections.Count -ne 0
        testAccounts = @($metadata.testAccounts.accounts.account)
    } | ConvertTo-Json -Depth 5
    return
}

if ($Action -eq "Start") {
    if (Test-Path -LiteralPath $statePath) {
        $existing = Get-Content -LiteralPath $statePath -Raw | ConvertFrom-Json
        $existingDb = Get-ValidatedProcess ([int]$existing.dbsrv.pid) $dbExe "DBSrv"
        $existingTm = Get-ValidatedProcess ([int]$existing.tmsrv.pid) $tmExe "TMSrv"
        if ($null -ne $existingDb -or $null -ne $existingTm) {
            throw "The source-built stack is already running. Use -Action Status."
        }
    }
    $occupied = @(Get-ListeningPorts | Where-Object { $_ -in @(7514, 8281, 8895) })
    if ($occupied.Count -ne 0) {
        throw "Required server ports are already occupied: $($occupied -join ', ')"
    }

    if ($RestoreBaseline) {
        $builder = Join-Path $PSScriptRoot "build_windows_servers_from_source.ps1"
        Assert-File $builder "server build/restore tool"
        & powershell.exe -NoProfile -ExecutionPolicy Bypass `
            -File $builder `
            -RepoRoot $RepoRoot `
            -OutputRoot $BuildRoot `
            -RestoreBaseline
        if ($LASTEXITCODE -ne 0) {
            throw "Baseline restore failed with exit code $LASTEXITCODE."
        }
    }
    Assert-TestAccounts

    $logRoot = Join-Path $BuildRoot "logs\runtime"
    New-Item -ItemType Directory -Force -Path $logRoot | Out-Null
    $dbProcess = $null
    $tmProcess = $null
    try {
        $dbProcess = Start-Process -FilePath $dbExe `
            -WorkingDirectory $dbRun `
            -WindowStyle Hidden `
            -PassThru
        Wait-Ports $dbProcess @(7514, 8895) 120 "DBSrv"

        $tmProcess = Start-Process -FilePath $tmExe `
            -WorkingDirectory $tmRun `
            -WindowStyle Hidden `
            -PassThru
        Wait-Ports $tmProcess @(8281) 180 "TMSrv"
        $connection = Wait-DbConnection $tmProcess 30

        $state = [ordered]@{
            schemaVersion = 1
            startedUtc = [DateTime]::UtcNow.ToString("o")
            buildRoot = $BuildRoot
            dbsrv = [ordered]@{
                pid = $dbProcess.Id
                executable = $dbExe
                sha256 = [string]$dbBuild[0].sha256
                ports = @(7514, 8895)
            }
            tmsrv = [ordered]@{
                pid = $tmProcess.Id
                executable = $tmExe
                sha256 = [string]$tmBuild[0].sha256
                ports = @(8281)
            }
            internalConnection = [ordered]@{
                localAddress = $connection.LocalAddress
                localPort = $connection.LocalPort
                remoteAddress = $connection.RemoteAddress
                remotePort = $connection.RemotePort
                state = [string]$connection.State
            }
            testAccounts = @($metadata.testAccounts.accounts.account)
            credentialFile = [string]$metadata.testAccounts.localCredentialFile
        }
        Write-JsonFile $statePath $state
        $state | ConvertTo-Json -Depth 6
        return
    }
    catch {
        foreach ($process in @($tmProcess, $dbProcess)) {
            if ($null -ne $process) {
                $process.Refresh()
                if (-not $process.HasExited) {
                    Stop-Process -Id $process.Id -Force
                }
            }
        }
        throw
    }
}

Assert-File $statePath "running server state"
$runningState = Get-Content -LiteralPath $statePath -Raw | ConvertFrom-Json
$dbProcess = Get-ValidatedProcess ([int]$runningState.dbsrv.pid) $dbExe "DBSrv"
$tmProcess = Get-ValidatedProcess ([int]$runningState.tmsrv.pid) $tmExe "TMSrv"

Add-Type @'
using System;
using System.Runtime.InteropServices;
using System.Text;
public static class OpenWydServerWindows {
    public delegate bool EnumCallback(IntPtr window, IntPtr parameter);
    [DllImport("user32.dll")] static extern bool EnumWindows(EnumCallback callback, IntPtr parameter);
    [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr window, out uint process);
    [DllImport("user32.dll")] static extern int GetClassName(IntPtr window, StringBuilder value, int count);
    [DllImport("user32.dll")] static extern bool PostMessage(IntPtr window, uint message, IntPtr wParam, IntPtr lParam);
    public static IntPtr Find(int processId, string className) {
        IntPtr result = IntPtr.Zero;
        EnumWindows(delegate(IntPtr window, IntPtr parameter) {
            uint process;
            GetWindowThreadProcessId(window, out process);
            StringBuilder value = new StringBuilder(256);
            GetClassName(window, value, value.Capacity);
            if (process == processId && value.ToString() == className) {
                result = window;
                return false;
            }
            return true;
        }, IntPtr.Zero);
        return result;
    }
    public static bool Close(IntPtr window) {
        return PostMessage(window, 0x0010, IntPtr.Zero, IntPtr.Zero);
    }
    public static bool ConfirmYes(IntPtr window) {
        return PostMessage(window, 0x0111, (IntPtr)6, IntPtr.Zero);
    }
}
'@

$stopResults = New-Object Collections.Generic.List[object]
function Stop-Cleanly {
    param(
        [Diagnostics.Process]$Process,
        [string]$Name,
        [bool]$Confirm
    )
    if ($null -eq $Process) {
        $stopResults.Add([ordered]@{ name = $Name; result = "already-stopped" })
        return
    }

    $mainWindow = [OpenWydServerWindows]::Find($Process.Id, "MainClass")
    if ($mainWindow -eq [IntPtr]::Zero) {
        throw "$Name MainClass window was not found."
    }
    [void][OpenWydServerWindows]::Close($mainWindow)
    if ($Confirm) {
        $deadline = [DateTime]::UtcNow.AddSeconds(10)
        $dialog = [IntPtr]::Zero
        do {
            $dialog = [OpenWydServerWindows]::Find($Process.Id, "#32770")
            if ($dialog -ne [IntPtr]::Zero) {
                break
            }
            Start-Sleep -Milliseconds 100
        } while ([DateTime]::UtcNow -lt $deadline)
        if ($dialog -eq [IntPtr]::Zero) {
            throw "$Name shutdown confirmation was not found."
        }
        [void][OpenWydServerWindows]::ConfirmYes($dialog)
    }
    $clean = $Process.WaitForExit(30000)
    if (-not $clean) {
        throw "$Name did not exit through WM_CLOSE."
    }
    $stopResults.Add([ordered]@{
        name = $Name
        result = "clean"
        exitCode = $Process.ExitCode
    })
}

$forced = New-Object Collections.Generic.List[string]
try {
    Stop-Cleanly $tmProcess "TMSrv" $true
    Stop-Cleanly $dbProcess "DBSrv" $false
}
catch {
    if (-not $ForceOnFailure) {
        throw
    }
    foreach ($entry in @(
        [ordered]@{ process = $tmProcess; name = "TMSrv" },
        [ordered]@{ process = $dbProcess; name = "DBSrv" }
    )) {
        if ($null -ne $entry.process) {
            $entry.process.Refresh()
            if (-not $entry.process.HasExited) {
                Stop-Process -Id $entry.process.Id -Force
                $entry.process.WaitForExit()
                $forced.Add($entry.name)
            }
        }
    }
}

Start-Sleep -Milliseconds 500
$remainingPorts = @(Get-ListeningPorts | Where-Object { $_ -in @(7514, 8281, 8895) })
if ($remainingPorts.Count -ne 0) {
    throw "Server ports remain after shutdown: $($remainingPorts -join ', ')"
}
$stopReport = [ordered]@{
    stoppedUtc = [DateTime]::UtcNow.ToString("o")
    buildRoot = $BuildRoot
    results = $stopResults.ToArray()
    forced = $forced.ToArray()
    remainingPorts = $remainingPorts
}
Write-JsonFile (Join-Path $BuildRoot "manifests\last-stop.json") $stopReport
$stopReport | ConvertTo-Json -Depth 5
