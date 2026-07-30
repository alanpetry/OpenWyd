[CmdletBinding(PositionalBinding = $false)]
param(
    [Parameter(Position = 0, ValueFromRemainingArguments = $true)]
    [string[]]$Arguments
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$launcher = Join-Path $PSScriptRoot "openwyd_lab\lab.py"
if (-not (Test-Path -LiteralPath $launcher -PathType Leaf)) {
    throw "OpenWyd Lab launcher not found: $launcher"
}

$python = Get-Command python.exe -ErrorAction SilentlyContinue
if ($null -eq $python) {
    $python = Get-Command python -ErrorAction Stop
}

& $python.Source $launcher --repo-root $repoRoot @Arguments
exit $LASTEXITCODE
