[CmdletBinding()]
param(
    [ValidateSet("dev", "verify", "assets")]
    [string]$Action = "dev",
    [int]$Jobs = 8,
    [switch]$ForceAssets
)

$ErrorActionPreference = "Stop"
$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..\..")).Path
$Arguments = @(
    (Join-Path $PSScriptRoot "build_wasm_dev.py"),
    $Action,
    "--repo-root",
    $RepoRoot,
    "--jobs",
    $Jobs
)
if ($ForceAssets) {
    $Arguments += "--force-assets"
}
& python @Arguments
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}
