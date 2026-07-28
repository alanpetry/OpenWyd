[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\.."))
$toolPath = Join-Path $repoRoot "tools\build_windows_servers_from_source.ps1"
$artifactsRoot = Join-Path $repoRoot "artifacts"
$testId = "$PID-$([Guid]::NewGuid().ToString('N'))"
$tempRoot = Join-Path ([IO.Path]::GetTempPath()) "openwyd-server-build-test-$testId"
$ignoredFixture = Join-Path $artifactsRoot "server-build-safety-fixture-$testId"

function Test-PathWithin {
    param([string]$Candidate, [string]$Parent)
    $candidatePath = [IO.Path]::GetFullPath($Candidate).TrimEnd('\', '/')
    $parentPath = [IO.Path]::GetFullPath($Parent).TrimEnd('\', '/')
    return $candidatePath.Equals($parentPath, [StringComparison]::OrdinalIgnoreCase) -or
        $candidatePath.StartsWith(
            $parentPath + [IO.Path]::DirectorySeparatorChar,
            [StringComparison]::OrdinalIgnoreCase
        )
}

function Invoke-Tool {
    param([string[]]$Arguments)
    $savedPreference = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try {
        $lines = @(
            & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $toolPath @Arguments 2>&1 |
                ForEach-Object { [string]$_ }
        )
        $exitCode = $LASTEXITCODE
    }
    finally {
        $ErrorActionPreference = $savedPreference
    }
    return [ordered]@{
        exitCode = $exitCode
        output = $lines -join [Environment]::NewLine
    }
}

function Assert-Succeeds {
    param([string]$Name, [string[]]$Arguments)
    $result = Invoke-Tool $Arguments
    if ($result.exitCode -ne 0) {
        throw "$Name failed unexpectedly ($($result.exitCode)):`n$($result.output)"
    }
    Write-Host "PASS: $Name"
}

function Assert-Fails {
    param([string]$Name, [string[]]$Arguments, [string]$Pattern)
    $result = Invoke-Tool $Arguments
    if ($result.exitCode -eq 0) {
        throw "$Name unexpectedly succeeded."
    }
    if ($result.output -notmatch $Pattern) {
        throw "$Name failed for the wrong reason. Expected /$Pattern/:`n$($result.output)"
    }
    Write-Host "PASS: $Name"
}

function New-MinimalBundle {
    param([string]$Root)
    foreach ($directory in @(
        "Source\Code\DBSrv",
        "Source\Code\TMSrv",
        "Server\Common",
        "Server\DBSrv\run",
        "Server\TMSrv\run"
    )) {
        New-Item -ItemType Directory -Force -Path (Join-Path $Root $directory) | Out-Null
    }
    $project = @"
<?xml version="1.0" encoding="utf-8"?>
<Project DefaultTargets="Build" xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
  <ItemGroup><ClCompile Include="Server.cpp" /></ItemGroup>
</Project>
"@
    [IO.File]::WriteAllText((Join-Path $Root "Source\Code\DBSrv\DBSrv.vcxproj"), $project)
    [IO.File]::WriteAllText((Join-Path $Root "Source\Code\TMSrv\TMSrv.vcxproj"), $project)
}

try {
    New-MinimalBundle $tempRoot
    New-MinimalBundle $ignoredFixture
    $safeOutput = Join-Path $artifactsRoot "server-build-safety-output-$testId"

    Assert-Succeeds "safe external bundle validates" @(
        "-ValidateOnly",
        "-RepoRoot", $repoRoot,
        "-ServerSourceRoot", $tempRoot,
        "-OutputRoot", $safeOutput
    )

    Assert-Fails "filesystem root is rejected as source" @(
        "-ValidateOnly",
        "-RepoRoot", $repoRoot,
        "-ServerSourceRoot", ([IO.Path]::GetPathRoot($repoRoot)),
        "-RuntimeDataRoot", (Join-Path $tempRoot "Server"),
        "-OutputRoot", $safeOutput
    ) "filesystem root"

    Assert-Fails "repository root is rejected as source" @(
        "-ValidateOnly",
        "-RepoRoot", $repoRoot,
        "-ServerSourceRoot", $repoRoot,
        "-RuntimeDataRoot", (Join-Path $tempRoot "Server"),
        "-OutputRoot", $safeOutput
    ) "repository root"

    Assert-Fails "repository root is rejected as output" @(
        "-ValidateOnly",
        "-RepoRoot", $repoRoot,
        "-ServerSourceRoot", $tempRoot,
        "-OutputRoot", $repoRoot
    ) "strict descendant"

    Assert-Fails "artifacts root itself is rejected as output" @(
        "-ValidateOnly",
        "-RepoRoot", $repoRoot,
        "-ServerSourceRoot", $tempRoot,
        "-OutputRoot", $artifactsRoot
    ) "strict descendant"

    Assert-Fails "overlapping source and output are rejected" @(
        "-ValidateOnly",
        "-RepoRoot", $repoRoot,
        "-ServerSourceRoot", $ignoredFixture,
        "-OutputRoot", (Join-Path $ignoredFixture "output")
    ) "must not overlap"

    Assert-Fails "tracked source location is rejected" @(
        "-ValidateOnly",
        "-RepoRoot", $repoRoot,
        "-ServerSourceRoot", (Join-Path $repoRoot "tools"),
        "-RuntimeDataRoot", (Join-Path $tempRoot "Server"),
        "-OutputRoot", $safeOutput
    ) "below ignored artifacts"

    Assert-Fails "duplicate test accounts are rejected" @(
        "-ValidateOnly",
        "-RepoRoot", $repoRoot,
        "-ServerSourceRoot", $tempRoot,
        "-OutputRoot", $safeOutput,
        "-TestAccountOne", "SAMEACCOUNT",
        "-TestAccountTwo", "sameaccount"
    ) "must be distinct"

    Assert-Fails "oversized official password is rejected" @(
        "-ValidateOnly",
        "-RepoRoot", $repoRoot,
        "-ServerSourceRoot", $tempRoot,
        "-OutputRoot", $safeOutput,
        "-TestAccountPassword", "twelvechars!"
    ) "1-11 printable ASCII"
}
finally {
    if ((Test-Path -LiteralPath $tempRoot) -and
        (Test-PathWithin $tempRoot ([IO.Path]::GetTempPath()))) {
        Remove-Item -LiteralPath $tempRoot -Recurse -Force
    }
    if ((Test-Path -LiteralPath $ignoredFixture) -and
        (Test-PathWithin $ignoredFixture $artifactsRoot)) {
        Remove-Item -LiteralPath $ignoredFixture -Recurse -Force
    }
}

Write-Host "All server build safety tests passed."
