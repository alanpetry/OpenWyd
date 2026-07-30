<#
.SYNOPSIS
Prepares an isolated 800x600 runtime for a Win32 client built from source.

.DESCRIPTION
Copies only game data/assets from v769ClientRelease (or another explicit asset
root), excludes every executable-code file shipped with that data, writes a
copy-local Config.bin selecting the official 800x600 windowed mode, and finally
installs the explicitly supplied source-built x86 client as WYD.exe.

The destination must be empty and must live below artifacts/. The script never
starts a process.

.PARAMETER BuiltClient
Path to the WYD.exe produced by tools/build_windows_source.ps1.

.PARAMETER AssetRoot
Game data/assets root. Defaults to v769ClientRelease in this checkout.

.PARAMETER RuntimeRoot
Empty destination. Defaults to artifacts/openwyd_compare/native-runtime.

.PARAMETER UpdateBuiltClient
Replaces only WYD.exe in a runtime previously created by this script. The
existing manifest, client hash, Config.bin, PE inventory, and asset root must
all validate before the replacement.

.PARAMETER Help
Prints usage without creating files.
#>
[CmdletBinding()]
param(
    [string]$BuiltClient = "",

    [string]$AssetRoot = "",

    [string]$RuntimeRoot = "",

    [switch]$UpdateBuiltClient,

    [Alias("h")]
    [switch]$Help
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

if ($Help) {
    @"
Usage:
  powershell -ExecutionPolicy Bypass -File tools/prepare_windows_client_runtime.ps1 `
    -BuiltClient artifacts/native-build/TMProject/Debug-compare/bin/WYD.exe `
    [-AssetRoot v769ClientRelease] `
    [-RuntimeRoot artifacts/openwyd_compare/native-runtime] `
    [-UpdateBuiltClient]

Safety:
  - BuiltClient must be PE32 x86, outside AssetRoot, and match the adjacent
    successful Debug|Win32 OPENWYD_COMPARE build-metadata.json.
  - The metadata's exact source-input/toolchain contract is rehashed from the
    current checkout before the client is accepted.
  - RuntimeRoot must be empty and below this checkout's artifacts directory.
  - Existing .exe, .dll, .sys, and .vxd files are never copied from AssetRoot.
  - The completed runtime must contain exactly one PE image: BuiltClient.
  - UpdateBuiltClient only replaces a manifest-verified source-built WYD.exe.
"@
    return
}

function Get-FullPath {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path,

        [Parameter(Mandatory = $true)]
        [string]$BasePath
    )

    if ([IO.Path]::IsPathRooted($Path)) {
        return [IO.Path]::GetFullPath($Path)
    }
    return [IO.Path]::GetFullPath((Join-Path $BasePath $Path))
}

function Test-PathWithin {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Candidate,

        [Parameter(Mandatory = $true)]
        [string]$Parent
    )

    $candidatePath = [IO.Path]::GetFullPath($Candidate).TrimEnd([char[]]@('\', '/'))
    $parentPath = [IO.Path]::GetFullPath($Parent).TrimEnd([char[]]@('\', '/'))
    if ($candidatePath.Equals($parentPath, [StringComparison]::OrdinalIgnoreCase)) {
        return $true
    }
    return $candidatePath.StartsWith(
        $parentPath + [IO.Path]::DirectorySeparatorChar,
        [StringComparison]::OrdinalIgnoreCase
    )
}

function Get-PeMachine {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path
    )

    $stream = [IO.File]::OpenRead($Path)
    try {
        if ($stream.Length -lt 64) {
            return $null
        }
        $reader = [IO.BinaryReader]::new($stream)
        if ($reader.ReadUInt16() -ne 0x5A4D) {
            return $null
        }
        $stream.Position = 0x3C
        $peOffset = $reader.ReadInt32()
        if ($peOffset -lt 0 -or ($peOffset + 6) -gt $stream.Length) {
            return $null
        }
        $stream.Position = $peOffset
        if ($reader.ReadUInt32() -ne 0x00004550) {
            return $null
        }
        return $reader.ReadUInt16()
    }
    finally {
        $stream.Dispose()
    }
}

$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$artifactsRoot = Join-Path $repoRoot "artifacts"
$contractScript = Join-Path $PSScriptRoot "windows_source_contract.ps1"
if (-not (Test-Path -LiteralPath $contractScript -PathType Leaf)) {
    throw "Windows source-contract implementation is missing: $contractScript"
}
. $contractScript

if (-not $AssetRoot) {
    $AssetRoot = Join-Path $repoRoot "v769ClientRelease"
}
else {
    $AssetRoot = Get-FullPath $AssetRoot $repoRoot
}
if (-not $BuiltClient) {
    throw "BuiltClient is required. Build it with tools/build_windows_source.ps1."
}
$BuiltClient = Get-FullPath $BuiltClient $repoRoot
if (-not $RuntimeRoot) {
    $RuntimeRoot = Join-Path $artifactsRoot "openwyd_compare\native-runtime"
}
else {
    $RuntimeRoot = Get-FullPath $RuntimeRoot $repoRoot
}

if (-not (Test-Path -LiteralPath $AssetRoot -PathType Container)) {
    throw "AssetRoot does not exist: $AssetRoot"
}
if (-not (Test-Path -LiteralPath $BuiltClient -PathType Leaf)) {
    throw "BuiltClient does not exist: $BuiltClient"
}
if (Test-PathWithin $BuiltClient $AssetRoot) {
    throw "BuiltClient must be a source-build output outside AssetRoot."
}
if (-not (Test-PathWithin $RuntimeRoot $artifactsRoot)) {
    throw "RuntimeRoot must resolve below the checkout's artifacts directory."
}
if (
    [IO.Path]::GetFullPath($RuntimeRoot).Equals(
        [IO.Path]::GetFullPath($artifactsRoot),
        [StringComparison]::OrdinalIgnoreCase
    )
) {
    throw "RuntimeRoot cannot be the artifacts root itself."
}
$runtimeExists = Test-Path -LiteralPath $RuntimeRoot
if ($UpdateBuiltClient) {
    if (-not $runtimeExists -or -not (Test-Path -LiteralPath $RuntimeRoot -PathType Container)) {
        throw "UpdateBuiltClient requires an existing runtime directory: $RuntimeRoot"
    }
    if (@(Get-ChildItem -LiteralPath $RuntimeRoot -Force).Count -eq 0) {
        throw "UpdateBuiltClient requires a non-empty prepared runtime: $RuntimeRoot"
    }
}
elseif ($runtimeExists) {
    if (-not (Test-Path -LiteralPath $RuntimeRoot -PathType Container)) {
        throw "RuntimeRoot exists and is not a directory: $RuntimeRoot"
    }
    if (@(Get-ChildItem -LiteralPath $RuntimeRoot -Force).Count -ne 0) {
        throw "RuntimeRoot must be empty: $RuntimeRoot"
    }
}
else {
    New-Item -ItemType Directory -Path $RuntimeRoot | Out-Null
}

$clientMachine = Get-PeMachine $BuiltClient
if ($clientMachine -ne 0x014C) {
    $actual = if ($null -eq $clientMachine) { "not a PE image" } else { "0x{0:X4}" -f $clientMachine }
    throw "BuiltClient must be PE32 x86 machine 0x014C; got $actual."
}

$buildMetadataPath = [IO.Path]::GetFullPath(
    (Join-Path (Split-Path -Parent $BuiltClient) "..\build-metadata.json")
)
if (-not (Test-Path -LiteralPath $buildMetadataPath -PathType Leaf)) {
    throw "BuiltClient is not accompanied by build-metadata.json: $buildMetadataPath"
}
try {
    $buildMetadata = Get-Content -LiteralPath $buildMetadataPath -Raw | ConvertFrom-Json
}
catch {
    throw "BuiltClient build metadata is invalid JSON: $buildMetadataPath"
}
$expectedProject = Join-Path $repoRoot "Projects\TMProject\TMProject.vcxproj"
$expectedCompareProps = Join-Path $repoRoot "tools\build_windows_source.compare.props"
$builtClientHash = (Get-FileHash -LiteralPath $BuiltClient -Algorithm SHA256).Hash
$builtClientSize = (Get-Item -LiteralPath $BuiltClient).Length
if (
    $buildMetadata.schemaVersion -ne 2 -or
    $buildMetadata.exitCode -ne 0 -or
    $buildMetadata.sourceBuildCertified -ne $true -or
    $buildMetadata.configuration -ne "Debug" -or
    $buildMetadata.platform -ne "Win32" -or
    $buildMetadata.openWydCompare -ne $true -or
    -not [IO.Path]::GetFullPath([string]$buildMetadata.repository).Equals(
        $repoRoot,
        [StringComparison]::OrdinalIgnoreCase
    ) -or
    -not [IO.Path]::GetFullPath([string]$buildMetadata.project).Equals(
        $expectedProject,
        [StringComparison]::OrdinalIgnoreCase
    ) -or
    -not [IO.Path]::GetFullPath([string]$buildMetadata.compareProps).Equals(
        $expectedCompareProps,
        [StringComparison]::OrdinalIgnoreCase
    ) -or
    -not [IO.Path]::GetFullPath([string]$buildMetadata.output.executable).Equals(
        $BuiltClient,
        [StringComparison]::OrdinalIgnoreCase
    ) -or
    -not ([string]$buildMetadata.output.sha256).Equals(
        $builtClientHash,
        [StringComparison]::OrdinalIgnoreCase
    ) -or
    [long]$buildMetadata.output.size -ne $builtClientSize -or
    [string]$buildMetadata.output.peMachine -ne "0x014C" -or
    [int]$buildMetadata.output.objectCount -le 0 -or
    [string]::IsNullOrWhiteSpace([string]$buildMetadata.sourceCommit)
) {
    throw (
        "BuiltClient metadata does not prove a successful Debug|Win32 " +
        "OPENWYD_COMPARE source build from this checkout."
    )
}

$expectedContractPath = [IO.Path]::GetFullPath(
    (Join-Path (Split-Path -Parent $buildMetadataPath) "windows-source-contract.json")
)
try {
    $metadataContractPath = [IO.Path]::GetFullPath(
        [string]$buildMetadata.sourceContract.manifest
    )
    $metadataToolsRoot = [IO.Path]::GetFullPath(
        [string]$buildMetadata.sourceContract.toolsRoot
    )
}
catch {
    throw "BuiltClient metadata has an invalid source-contract path."
}
if (
    -not $metadataContractPath.Equals(
        $expectedContractPath,
        [StringComparison]::OrdinalIgnoreCase
    ) -or
    [string]$buildMetadata.sourceContract.status -ne "verified-after-build" -or
    [int]$buildMetadata.sourceContract.schemaVersion -ne 2 -or
    [int]$buildMetadata.sourceContract.inputCount -le 0 -or
    [int]$buildMetadata.sourceContract.toolchainFileCount -le 0
) {
    throw "BuiltClient metadata does not contain a verified source contract."
}
$sourceContractValidation = Assert-OpenWydWindowsSourceContract `
    -ManifestPath $metadataContractPath `
    -RepoRoot $repoRoot `
    -ToolsRoot $metadataToolsRoot `
    -ExpectedManifestSha256 ([string]$buildMetadata.sourceContract.manifestSha256) `
    -ExpectedDigest ([string]$buildMetadata.sourceContract.digest)
$sourceContractManifest = $sourceContractValidation.manifest
$contractProject = Resolve-OpenWydContractPathIdentifier `
    -Identifier ([string]$sourceContractManifest.selection.project) `
    -RepoRoot $repoRoot `
    -ToolsRoot $metadataToolsRoot
$contractCompareProps = Resolve-OpenWydContractPathIdentifier `
    -Identifier ([string]$sourceContractManifest.selection.compareProps) `
    -RepoRoot $repoRoot `
    -ToolsRoot $metadataToolsRoot
if (
    -not $contractProject.Equals(
        $expectedProject,
        [StringComparison]::OrdinalIgnoreCase
    ) -or
    -not $contractCompareProps.Equals(
        $expectedCompareProps,
        [StringComparison]::OrdinalIgnoreCase
    ) -or
    [string]$sourceContractManifest.build.configuration -ne "Debug" -or
    [string]$sourceContractManifest.build.platform -ne "Win32" -or
    [string]$sourceContractManifest.build.platformToolset -ne "v142" -or
    $sourceContractManifest.build.openWydCompare -ne $true -or
    [string]$sourceContractManifest.build.msvcVersion -ne
        [string]$buildMetadata.toolchain.msvcVersion -or
    [string]$sourceContractManifest.build.windowsSdkVersion -ne
        [string]$buildMetadata.toolchain.windowsSdkVersion
) {
    throw "Source contract does not describe this required Win32 compare build."
}
$provenanceBindingSha256 = Get-OpenWydWindowsProvenanceBindingSha256 `
    -ExecutableSha256 $builtClientHash `
    -ExecutableSize $builtClientSize `
    -ContractDigest ([string]$sourceContractValidation.digest) `
    -ContractManifestSha256 ([string]$sourceContractValidation.manifestSha256)
if (
    -not ([string]$buildMetadata.output.sourceContractDigest).Equals(
        [string]$sourceContractValidation.digest,
        [StringComparison]::OrdinalIgnoreCase
    ) -or
    -not ([string]$buildMetadata.output.sourceContractManifestSha256).Equals(
        [string]$sourceContractValidation.manifestSha256,
        [StringComparison]::OrdinalIgnoreCase
    ) -or
    -not ([string]$buildMetadata.output.provenanceBindingSha256).Equals(
        $provenanceBindingSha256,
        [StringComparison]::OrdinalIgnoreCase
    )
) {
    throw "BuiltClient is not bound to the verified source-contract manifest."
}

$manifestPath = Join-Path $RuntimeRoot "source-runtime-manifest.json"
$runtimeClient = Join-Path $RuntimeRoot "WYD.exe"
if ($UpdateBuiltClient) {
    if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
        throw "Prepared runtime manifest is missing: $manifestPath"
    }
    $existingManifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
    if (
        $existingManifest.schema -ne "openwyd.native-runtime" -or
        $existingManifest.schema_version -ne 1
    ) {
        throw "Prepared runtime manifest has an unsupported schema."
    }
    if (
        -not [IO.Path]::GetFullPath([string]$existingManifest.runtime_root).Equals(
            [IO.Path]::GetFullPath($RuntimeRoot),
            [StringComparison]::OrdinalIgnoreCase
        )
    ) {
        throw "Prepared runtime manifest does not belong to RuntimeRoot."
    }
    if (
        -not [IO.Path]::GetFullPath([string]$existingManifest.asset_root).Equals(
            [IO.Path]::GetFullPath($AssetRoot),
            [StringComparison]::OrdinalIgnoreCase
        )
    ) {
        throw "Prepared runtime manifest asset root does not match AssetRoot."
    }
    if (-not (Test-Path -LiteralPath $runtimeClient -PathType Leaf)) {
        throw "Prepared runtime client is missing: $runtimeClient"
    }

    $runtimePeFiles = @(
        Get-ChildItem -LiteralPath $RuntimeRoot -File -Recurse |
            Where-Object { $null -ne (Get-PeMachine $_.FullName) }
    )
    if (
        $runtimePeFiles.Count -ne 1 -or
        -not $runtimePeFiles[0].FullName.Equals(
            $runtimeClient,
            [StringComparison]::OrdinalIgnoreCase
        )
    ) {
        throw "Prepared runtime no longer contains exactly one source-built PE image."
    }
    $existingHash = (Get-FileHash -LiteralPath $runtimeClient -Algorithm SHA256).Hash
    if (
        -not $existingHash.Equals(
            [string]$existingManifest.source_built_client.sha256,
            [StringComparison]::OrdinalIgnoreCase
        )
    ) {
        throw "Prepared runtime client hash does not match its manifest."
    }
    $configPath = Join-Path $RuntimeRoot "Config.bin"
    if (-not (Test-Path -LiteralPath $configPath -PathType Leaf)) {
        throw "Prepared runtime Config.bin is missing."
    }
    $configBytes = [IO.File]::ReadAllBytes($configPath)
    if (
        $configBytes.Length -ne 32 -or
        [BitConverter]::ToInt16($configBytes, 2) -ne 2
    ) {
        throw "Prepared runtime Config.bin is not the verified 800x600 copy."
    }

    $history = @()
    if ($existingManifest.PSObject.Properties.Name -contains "previous_source_built_clients") {
        $history += @($existingManifest.previous_source_built_clients)
    }
    $history += $existingManifest.source_built_client
    $newClient = [ordered]@{
        source = $BuiltClient
        destination = $runtimeClient
        pe_machine = "0x014C"
        size = $builtClientSize
        sha256 = $builtClientHash
        build_metadata = $buildMetadataPath
        build_metadata_sha256 = (
            Get-FileHash -LiteralPath $buildMetadataPath -Algorithm SHA256
        ).Hash
        source_commit = [string]$buildMetadata.sourceCommit
        object_count = [int]$buildMetadata.output.objectCount
        openwyd_compare = $true
        source_contract = $metadataContractPath
        source_contract_digest = [string]$sourceContractValidation.digest
        source_contract_manifest_sha256 = (
            [string]$sourceContractValidation.manifestSha256
        )
        provenance_binding_sha256 = $provenanceBindingSha256
    }

    Copy-Item -LiteralPath $BuiltClient -Destination $runtimeClient -Force
    $copiedHash = (Get-FileHash -LiteralPath $runtimeClient -Algorithm SHA256).Hash
    if (-not $copiedHash.Equals($newClient.sha256, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Updated runtime client hash does not match BuiltClient."
    }
    $existingManifest.source_built_client = $newClient
    $existingManifest |
        Add-Member -NotePropertyName previous_source_built_clients -NotePropertyValue $history -Force
    [IO.File]::WriteAllText(
        $manifestPath,
        (($existingManifest | ConvertTo-Json -Depth 12) + [Environment]::NewLine),
        [Text.UTF8Encoding]::new($false)
    )

    Write-Host "Updated source-built Win32 client: $runtimeClient"
    Write-Host "Installed client SHA-256: $copiedHash"
    Write-Host "Manifest: $manifestPath"
    return
}

$codeExtensions = @(".exe", ".dll", ".sys", ".vxd")
$excludedCode = @(
    Get-ChildItem -LiteralPath $AssetRoot -File -Recurse -ErrorAction Stop |
        Where-Object { $codeExtensions -contains $_.Extension.ToLowerInvariant() } |
        ForEach-Object {
            [ordered]@{
                path = $_.FullName.Substring($AssetRoot.Length).TrimStart('\', '/').Replace('\', '/')
                size = $_.Length
                sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash
            }
        }
)

$robocopyArguments = @(
    $AssetRoot,
    $RuntimeRoot,
    "/E",
    "/COPY:DAT",
    "/DCOPY:DAT",
    "/R:1",
    "/W:1",
    "/NFL",
    "/NDL",
    "/NJH",
    "/NJS",
    "/NP",
    "/XF",
    "*.exe",
    "*.dll",
    "*.sys",
    "*.vxd",
    "/XD",
    (Join-Path $AssetRoot "Adobe AIR"),
    (Join-Path $AssetRoot "ScreenShot")
)
& robocopy.exe @robocopyArguments | Out-Host
$robocopyExit = $LASTEXITCODE
if ($robocopyExit -gt 7) {
    throw "Asset copy failed with robocopy exit code $robocopyExit."
}

$unexpectedPe = @(
    Get-ChildItem -LiteralPath $RuntimeRoot -File -Recurse |
        Where-Object { $null -ne (Get-PeMachine $_.FullName) }
)
if ($unexpectedPe.Count -ne 0) {
    $paths = ($unexpectedPe.FullName -join ", ")
    throw "Copied asset tree unexpectedly contains PE images: $paths"
}

$configPath = Join-Path $RuntimeRoot "Config.bin"
if (-not (Test-Path -LiteralPath $configPath -PathType Leaf)) {
    throw "Copied asset tree does not contain Config.bin."
}
$configBytes = [IO.File]::ReadAllBytes($configPath)
if ($configBytes.Length -ne 32) {
    throw "Config.bin must match SaveUpdatAndConfig (32 bytes); got $($configBytes.Length)."
}
$resolutionIndex = [BitConverter]::ToInt16($configBytes, 2)
if ($resolutionIndex -lt 1 -or $resolutionIndex -gt 11) {
    throw "Config.bin has invalid source resolution index $resolutionIndex."
}
[BitConverter]::GetBytes([int16]2).CopyTo($configBytes, 2)
[IO.File]::WriteAllBytes($configPath, $configBytes)

Copy-Item -LiteralPath $BuiltClient -Destination $runtimeClient

$runtimePeFiles = @(
    Get-ChildItem -LiteralPath $RuntimeRoot -File -Recurse |
        Where-Object { $null -ne (Get-PeMachine $_.FullName) }
)
if (
    $runtimePeFiles.Count -ne 1 -or
    -not $runtimePeFiles[0].FullName.Equals(
        $runtimeClient,
        [StringComparison]::OrdinalIgnoreCase
    )
) {
    throw "Completed runtime must contain exactly the explicitly supplied source-built client."
}

$runtimeFiles = @(Get-ChildItem -LiteralPath $RuntimeRoot -File -Recurse)
$manifest = [ordered]@{
    schema = "openwyd.native-runtime"
    schema_version = 1
    asset_root = $AssetRoot
    runtime_root = $RuntimeRoot
    resolution = [ordered]@{
        width = 800
        height = 600
        config_index_before = $resolutionIndex
        config_index_after = 2
    }
    source_built_client = [ordered]@{
        source = $BuiltClient
        destination = $runtimeClient
        pe_machine = "0x014C"
        size = (Get-Item -LiteralPath $runtimeClient).Length
        sha256 = (Get-FileHash -LiteralPath $runtimeClient -Algorithm SHA256).Hash
        build_metadata = $buildMetadataPath
        build_metadata_sha256 = (
            Get-FileHash -LiteralPath $buildMetadataPath -Algorithm SHA256
        ).Hash
        source_commit = [string]$buildMetadata.sourceCommit
        object_count = [int]$buildMetadata.output.objectCount
        openwyd_compare = $true
        source_contract = $metadataContractPath
        source_contract_digest = [string]$sourceContractValidation.digest
        source_contract_manifest_sha256 = (
            [string]$sourceContractValidation.manifestSha256
        )
        provenance_binding_sha256 = $provenanceBindingSha256
    }
    copied_file_count = $runtimeFiles.Count
    copied_bytes = ($runtimeFiles | Measure-Object -Property Length -Sum).Sum
    excluded_existing_code = $excludedCode
}
[IO.File]::WriteAllText(
    $manifestPath,
    (($manifest | ConvertTo-Json -Depth 8) + [Environment]::NewLine),
    [Text.UTF8Encoding]::new($false)
)

Write-Host "Prepared source-only Win32 runtime: $RuntimeRoot"
Write-Host "Installed client SHA-256: $($manifest.source_built_client.sha256)"
Write-Host "Excluded existing executable-code files: $($excludedCode.Count)"
Write-Host "Manifest: $manifestPath"
