<#
.SYNOPSIS
Builds the OpenWyd Win32 client from source with the workspace-local MSVC toolchain.

.DESCRIPTION
Configures the portable MSVC v142, Windows SDK, MSBuild, C++ targets, and file
tracker installed under the workspace .tools directory, then builds
Projects/TMProject/TMProject.vcxproj.

All outputs, intermediate files, logs, and build metadata are written below
artifacts/native-build by default. The script refuses any output path inside
v769ClientRelease. Every successful output is source-certified, so every
invocation uses the MSBuild Rebuild target and recompiles all project objects.

.PARAMETER Configuration
Build configuration. Debug is the default.

.PARAMETER OpenWydCompare
Defines OPENWYD_COMPARE=1 for every C/C++ compilation. This flag is accepted
only for Debug builds and uses a separate output directory.

.PARAMETER Clean
Compatibility switch retained for existing callers. Certified builds always
run the MSBuild Rebuild target, with or without this switch. No directory is
deleted directly by this script.

.PARAMETER RepoRoot
OpenWyd repository root. Defaults to the parent of this script's directory.

.PARAMETER ToolsRoot
Workspace-local tools directory. Defaults to .tools beside the repository.

.PARAMETER OutputRoot
Explicit artifact directory. Defaults to
artifacts/native-build/TMProject/<variant>.

.PARAMETER Verbosity
Console verbosity passed to MSBuild. The file log is always diagnostic.

.PARAMETER Help
Prints command-line usage without probing or changing the toolchain.

.EXAMPLE
pwsh -File tools/build_windows_source.ps1

Build Debug|Win32 from source.

.EXAMPLE
pwsh -File tools/build_windows_source.ps1 -OpenWydCompare

Build Debug|Win32 with OPENWYD_COMPARE=1 in a separate artifact directory.

.EXAMPLE
powershell -ExecutionPolicy Bypass -File tools/build_windows_source.ps1 -Configuration Release

Build Release|Win32 without writing to v769ClientRelease.
#>
[CmdletBinding()]
param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Debug",

    [switch]$OpenWydCompare,

    [switch]$Clean,

    [string]$RepoRoot = "",

    [string]$ToolsRoot = "",

    [string]$OutputRoot = "",

    [ValidateSet("quiet", "minimal", "normal", "detailed", "diagnostic")]
    [string]$Verbosity = "minimal",

    [Alias("h")]
    [switch]$Help
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

if ($Help) {
    @"
Usage:
  powershell -ExecutionPolicy Bypass -File tools/build_windows_source.ps1 [options]

Options:
  -Configuration Debug|Release   Build configuration (default: Debug).
  -OpenWydCompare                Define OPENWYD_COMPARE=1; Debug only.
  -Clean                         Compatibility switch; all builds already Rebuild.
  -RepoRoot <path>               Override the detected OpenWyd repository root.
  -ToolsRoot <path>              Override the workspace-local .tools directory.
  -OutputRoot <path>             Override artifacts/native-build output.
  -Verbosity <level>             quiet|minimal|normal|detailed|diagnostic.
  -Help                          Print this message.

Outputs:
  WYD.exe, PDB/ILK, console.log, msbuild-diagnostic.log,
  msbuild-invocation.txt, windows-source-contract.json, and
  build-metadata.json.

Safety:
  OutputRoot, OutDir, and IntDir are rejected if they resolve inside
  v769ClientRelease.
  Source certification always uses MSBuild Rebuild; incremental objects are
  never accepted as the basis of a certified executable.
"@
    return
}

function Get-FullPath {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path,

        [string]$BasePath = (Get-Location).Path
    )

    if ([IO.Path]::IsPathRooted($Path)) {
        return [IO.Path]::GetFullPath($Path)
    }

    return [IO.Path]::GetFullPath((Join-Path $BasePath $Path))
}

function Add-TrailingSeparator {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path
    )

    return $Path.TrimEnd([char[]]@('\', '/')) + [IO.Path]::DirectorySeparatorChar
}

function Test-PathWithin {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Candidate,

        [Parameter(Mandatory = $true)]
        [string]$Parent
    )

    $candidatePath = (Get-FullPath $Candidate).TrimEnd([char[]]@('\', '/'))
    $parentPath = (Get-FullPath $Parent).TrimEnd([char[]]@('\', '/'))
    if ($candidatePath.Equals($parentPath, [StringComparison]::OrdinalIgnoreCase)) {
        return $true
    }

    $parentPrefix = $parentPath + [IO.Path]::DirectorySeparatorChar
    return $candidatePath.StartsWith($parentPrefix, [StringComparison]::OrdinalIgnoreCase)
}

function Assert-File {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path,

        [Parameter(Mandatory = $true)]
        [string]$Description
    )

    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "$Description not found: $Path"
    }
}

function Get-LatestVersionDirectory {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Parent,

        [Parameter(Mandatory = $true)]
        [scriptblock]$Predicate
    )

    $candidates = @(
        Get-ChildItem -LiteralPath $Parent -Directory -ErrorAction Stop |
            Where-Object $Predicate |
            Sort-Object { [version]$_.Name } -Descending
    )
    if ($candidates.Count -eq 0) {
        throw "No compatible version directory found below $Parent"
    }

    return $candidates[0]
}

function Get-PeMachine {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path
    )

    $stream = [IO.File]::Open(
        $Path,
        [IO.FileMode]::Open,
        [IO.FileAccess]::Read,
        [IO.FileShare]::ReadWrite
    )
    $reader = New-Object IO.BinaryReader($stream)
    try {
        if ($reader.ReadUInt16() -ne 0x5A4D) {
            throw "Output is not an MZ executable: $Path"
        }
        $stream.Position = 0x3C
        $peOffset = $reader.ReadInt32()
        $stream.Position = $peOffset
        if ($reader.ReadUInt32() -ne 0x00004550) {
            throw "Output does not contain a PE signature: $Path"
        }
        return $reader.ReadUInt16()
    }
    finally {
        $reader.Dispose()
        $stream.Dispose()
    }
}

if ($OpenWydCompare -and $Configuration -ne "Debug") {
    throw "OPENWYD_COMPARE is debug-only. Use -Configuration Debug."
}

if ([string]::IsNullOrWhiteSpace($RepoRoot)) {
    $RepoRoot = Split-Path -Parent $PSScriptRoot
}
$RepoRoot = Get-FullPath $RepoRoot

if ([string]::IsNullOrWhiteSpace($ToolsRoot)) {
    $ToolsRoot = Join-Path (Split-Path -Parent $RepoRoot) ".tools"
}
$ToolsRoot = Get-FullPath $ToolsRoot

$variant = if ($OpenWydCompare) { "Debug-compare" } else { $Configuration }
if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
    $OutputRoot = Join-Path $RepoRoot "artifacts\native-build\TMProject\$variant"
}
$OutputRoot = Get-FullPath $OutputRoot $RepoRoot

$forbiddenRelease = Join-Path $RepoRoot "v769ClientRelease"
$outDir = Join-Path $OutputRoot "bin"
$intDir = Join-Path $OutputRoot "obj"
foreach ($candidate in @($OutputRoot, $outDir, $intDir)) {
    if (Test-PathWithin $candidate $forbiddenRelease) {
        throw "Refusing to place native build output inside v769ClientRelease: $candidate"
    }
}

$projectPath = Join-Path $RepoRoot "Projects\TMProject\TMProject.vcxproj"
$compareProps = Join-Path $PSScriptRoot "build_windows_source.compare.props"
$contractScript = Join-Path $PSScriptRoot "windows_source_contract.ps1"
$directXInclude = Join-Path $RepoRoot "Dependencies\Directx\Include\d3dx9.h"
$directXLibrary = Join-Path $RepoRoot "Dependencies\Directx\Lib\d3dx9.lib"
Assert-File $projectPath "TMProject vcxproj"
Assert-File $contractScript "Windows source-contract implementation"
if ($OpenWydCompare) {
    Assert-File $compareProps "OPENWYD_COMPARE MSBuild property sheet"
}
Assert-File $directXInclude "DirectX 9 header"
Assert-File $directXLibrary "DirectX 9 x86 import library"

$portableMsvcRoot = Join-Path $ToolsRoot "portable-msvc-v142-x86\msvc"
$portableMsbuildRoot = Join-Path $ToolsRoot "portable-msbuild-vs2022-v142"
$msvcVersionsRoot = Join-Path $portableMsvcRoot "VC\Tools\MSVC"
$windowsKitRoot = Join-Path $portableMsvcRoot "Windows Kits\10"
$vCTargetsPath = Join-Path $portableMsbuildRoot "MSBuild\Microsoft\VC\v160"

$msvcVersionDirectory = Get-LatestVersionDirectory $msvcVersionsRoot {
    $_.Name.StartsWith("14.29.", [StringComparison]::Ordinal)
}
$msvcVersion = $msvcVersionDirectory.Name
$msvcToolsRoot = $msvcVersionDirectory.FullName

$sdkVersionDirectory = Get-LatestVersionDirectory (Join-Path $windowsKitRoot "Include") {
    (Test-Path -LiteralPath (Join-Path $_.FullName "shared\sdkddkver.h") -PathType Leaf) -and
    (Test-Path -LiteralPath (Join-Path $windowsKitRoot "Lib\$($_.Name)\um\x86\gdi32.lib") -PathType Leaf)
}
$sdkVersion = $sdkVersionDirectory.Name

$compiler = Join-Path $msvcToolsRoot "bin\Hostx64\x86\cl.exe"
$linker = Join-Path $msvcToolsRoot "bin\Hostx64\x86\link.exe"
$msbuild = Join-Path $portableMsbuildRoot "MSBuild\Current\Bin\amd64\MSBuild.exe"
$tracker = Join-Path $portableMsbuildRoot "MSBuild\Current\Bin\amd64\Tracker.exe"
$cppDefaultProps = Join-Path $vCTargetsPath "Microsoft.Cpp.Default.props"
$toolsetProps = Join-Path $vCTargetsPath "Platforms\Win32\PlatformToolsets\v142\Toolset.props"

Assert-File $compiler "MSVC v142 x86 compiler"
Assert-File $linker "MSVC v142 x86 linker"
Assert-File $msbuild "Portable MSBuild"
Assert-File $tracker "MSBuild file tracker"
Assert-File $cppDefaultProps "Visual C++ MSBuild targets"
Assert-File $toolsetProps "v142 Win32 MSBuild toolset properties"

$msvcInclude = Join-Path $msvcToolsRoot "include"
$sdkIncludeRoot = Join-Path $windowsKitRoot "Include\$sdkVersion"
$vcLibraryPath = Join-Path $msvcToolsRoot "lib\x86"
$sdkUcrtLibraryPath = Join-Path $windowsKitRoot "Lib\$sdkVersion\ucrt\x86"
$sdkUmLibraryPath = Join-Path $windowsKitRoot "Lib\$sdkVersion\um\x86"
$compilerBin = Join-Path $msvcToolsRoot "bin\Hostx64\x86"
$sdkBin = Join-Path $windowsKitRoot "bin\$sdkVersion\x64"
$msbuildBin = Split-Path -Parent $msbuild

foreach ($requiredDirectory in @(
    $msvcInclude,
    $sdkIncludeRoot,
    $vcLibraryPath,
    $sdkUcrtLibraryPath,
    $sdkUmLibraryPath,
    $compilerBin,
    $sdkBin
)) {
    if (-not (Test-Path -LiteralPath $requiredDirectory -PathType Container)) {
        throw "Required toolchain directory not found: $requiredDirectory"
    }
}

New-Item -ItemType Directory -Force -Path $outDir, $intDir | Out-Null

$consoleLog = Join-Path $OutputRoot "console.log"
$diagnosticLog = Join-Path $OutputRoot "msbuild-diagnostic.log"
$invocationLog = Join-Path $OutputRoot "msbuild-invocation.txt"
$metadataPath = Join-Path $OutputRoot "build-metadata.json"
$sourceContractPath = Join-Path $OutputRoot "windows-source-contract.json"
# A source-certified executable must never be linked from incremental objects
# that predate the captured source contract. Rebuild asks MSBuild to clean the
# project outputs and compile every object, without this script recursively
# deleting the caller-selected artifact directory. -Clean remains accepted
# only for command-line compatibility with earlier revisions of this script.
$target = "Rebuild"

$msbuildArguments = @(
    $projectPath,
    "/nologo",
    "/m:1",
    "/t:$target",
    "/verbosity:$Verbosity",
    "/p:Configuration=$Configuration",
    "/p:Platform=Win32",
    "/p:PlatformToolset=v142",
    "/p:VCTargetsPath=$(Add-TrailingSeparator $vCTargetsPath)",
    "/p:VCToolsInstallDir=$(Add-TrailingSeparator $msvcToolsRoot)",
    "/p:VCToolsVersion=$msvcVersion",
    "/p:VCInstallDir=$(Add-TrailingSeparator (Join-Path $portableMsvcRoot 'VC'))",
    "/p:WindowsSdkDir=$windowsKitRoot",
    "/p:UniversalCRTSdkDir=$windowsKitRoot",
    "/p:WindowsTargetPlatformVersion=$sdkVersion",
    "/p:WindowsSDKVersion=$sdkVersion\",
    "/p:UCRTVersion=$sdkVersion",
    "/p:WindowsSDKInstalled=true",
    "/p:WindowsSDK_Desktop_Support=true",
    "/p:SolutionDir=$(Add-TrailingSeparator $RepoRoot)",
    "/p:OutDir=$(Add-TrailingSeparator $outDir)",
    "/p:IntDir=$(Add-TrailingSeparator $intDir)",
    "/p:OpenWydCompare=$($OpenWydCompare.IsPresent.ToString().ToLowerInvariant())",
    "/fileLogger",
    "/fileLoggerParameters:LogFile=$diagnosticLog;Verbosity=diagnostic;Encoding=UTF-8"
)
if ($OpenWydCompare) {
    $msbuildArguments += "/p:ForceImportAfterCppProps=$compareProps"
}

foreach ($optionVariable in @("CL", "_CL_", "LINK", "_LINK_")) {
    $optionValue = [Environment]::GetEnvironmentVariable($optionVariable, "Process")
    if (-not [string]::IsNullOrWhiteSpace($optionValue)) {
        throw (
            "Refusing non-reproducible tool option injection through " +
            "$optionVariable. Clear the variable before building."
        )
    }
}

. $contractScript

$resourceCompiler = Join-Path $sdkBin "rc.exe"
$manifestTool = Join-Path $sdkBin "mt.exe"
$resourceConverter = Join-Path $compilerBin "cvtres.exe"
Assert-File $resourceCompiler "Windows SDK resource compiler"
Assert-File $manifestTool "Windows SDK manifest tool"
Assert-File $resourceConverter "MSVC resource converter"

$toolchainFiles = @{
    compiler = $compiler
    linker = $linker
    resourceConverter = $resourceConverter
    resourceCompiler = $resourceCompiler
    manifestTool = $manifestTool
    msbuild = $msbuild
    tracker = $tracker
}

$semanticArguments = @(
    $msbuildArguments | ForEach-Object {
        $normalized = [string]$_
        $normalized = $normalized.Replace(
            $OutputRoot,
            "<OUTPUT_ROOT>"
        )
        $normalized = $normalized.Replace(
            $RepoRoot,
            "<REPO_ROOT>"
        )
        $normalized = $normalized.Replace(
            $ToolsRoot,
            "<TOOLS_ROOT>"
        )
        $normalized
    }
)
$sourceContractBefore = New-OpenWydWindowsSourceContract `
    -RepoRoot $RepoRoot `
    -ToolsRoot $ToolsRoot `
    -ProjectPath $projectPath `
    -ComparePropsPath $compareProps `
    -BuildScriptPath $PSCommandPath `
    -ContractScriptPath $contractScript `
    -Configuration $Configuration `
    -Platform "Win32" `
    -PlatformToolset "v142" `
    -OpenWydCompare $OpenWydCompare.IsPresent `
    -MsvcVersion $msvcVersion `
    -WindowsSdkVersion $sdkVersion `
    -SemanticArguments $semanticArguments `
    -ToolchainFiles $toolchainFiles `
    -ToolchainDependencyRoots @(
        $compilerBin,
        $msvcInclude,
        $vcLibraryPath,
        $sdkIncludeRoot,
        $sdkUcrtLibraryPath,
        $sdkUmLibraryPath,
        $sdkBin,
        $msbuildBin,
        $vCTargetsPath
    ) `
    -RepositoryDependencyRoots @(
        (Join-Path $RepoRoot "Dependencies\Directx\Include"),
        (Join-Path $RepoRoot "Dependencies\Directx\Lib")
    )
$sourceContractManifestSha256 = Write-OpenWydWindowsSourceContract `
    -Contract $sourceContractBefore `
    -Path $sourceContractPath

$quotedArguments = $msbuildArguments | ForEach-Object {
    if ($_ -match '[\s;"]') {
        '"' + $_.Replace('"', '\"') + '"'
    }
    else {
        $_
    }
}
$invocation = '"' + $msbuild + '" ' + ($quotedArguments -join " ")
[IO.File]::WriteAllText($invocationLog, $invocation + [Environment]::NewLine)

$gitCommand = Get-Command git.exe -ErrorAction SilentlyContinue
$gitPath = if ($null -ne $gitCommand) {
    $gitCommand.Source
}
elseif (Test-Path -LiteralPath "C:\Program Files\Git\cmd\git.exe" -PathType Leaf) {
    "C:\Program Files\Git\cmd\git.exe"
}
else {
    $null
}
$sourceCommit = $null
$sourceStatus = @()
if ($null -ne $gitPath) {
    $sourceCommit = (& $gitPath -C $RepoRoot rev-parse HEAD 2>$null | Select-Object -First 1)
    $sourceStatus = @(& $gitPath -C $RepoRoot status --short 2>$null)
}

$metadata = [ordered]@{
    schemaVersion = 2
    startedUtc = [DateTime]::UtcNow.ToString("o")
    repository = $RepoRoot
    sourceCommit = $sourceCommit
    sourceStatus = $sourceStatus
    project = $projectPath
    configuration = $Configuration
    platform = "Win32"
    openWydCompare = $OpenWydCompare.IsPresent
    compareProps = if ($OpenWydCompare) { $compareProps } else { $null }
    target = $target
    cleanRequested = $Clean.IsPresent
    outputRoot = $OutputRoot
    outDir = $outDir
    intDir = $intDir
    forbiddenOutputRoot = $forbiddenRelease
    toolchain = [ordered]@{
        compiler = $compiler
        compilerVersion = (Get-Item -LiteralPath $compiler).VersionInfo.FileVersion
        compilerSha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $compiler).Hash
        linker = $linker
        linkerVersion = (Get-Item -LiteralPath $linker).VersionInfo.FileVersion
        linkerSha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $linker).Hash
        msvcVersion = $msvcVersion
        windowsSdkVersion = $sdkVersion
        msbuild = $msbuild
        msbuildVersion = (Get-Item -LiteralPath $msbuild).VersionInfo.FileVersion
        msbuildSha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $msbuild).Hash
        vcTargetsPath = $vCTargetsPath
    }
    invocationFile = $invocationLog
    consoleLog = $consoleLog
    diagnosticLog = $diagnosticLog
    sourceContract = [ordered]@{
        schema = [string]$sourceContractBefore.schema
        schemaVersion = [int]$sourceContractBefore.schemaVersion
        manifest = $sourceContractPath
        manifestSha256 = $sourceContractManifestSha256
        digest = [string]$sourceContractBefore.digest
        inputCount = @($sourceContractBefore.inputs).Count
        toolchainFileCount = @($sourceContractBefore.toolchain).Count
        toolsRoot = $ToolsRoot
        status = "captured-before-build"
    }
    exitCode = $null
    sourceBuildCertified = $false
    completedUtc = $null
    output = $null
}
$metadata | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $metadataPath -Encoding UTF8

$environmentKeys = @(
    "PATH",
    "INCLUDE",
    "LIB",
    "IncludePath",
    "LibraryPath",
    "VC_LibraryPath_x86",
    "WindowsSDK_LibraryPath_x86",
    "VSCMD_ARG_HOST_ARCH",
    "VSCMD_ARG_TGT_ARCH",
    "VCToolsVersion",
    "VCToolsInstallDir",
    "WindowsSDKVersion",
    "WindowsSdkBinPath"
)
$savedEnvironment = @{}
foreach ($key in $environmentKeys) {
    $savedEnvironment[$key] = [Environment]::GetEnvironmentVariable($key, "Process")
}

$exitCode = 1
try {
    $toolPath = @($compilerBin, $sdkBin, (Join-Path $sdkBin "ucrt"), $msbuildBin) -join ";"
    $includePath = @(
        $msvcInclude,
        (Join-Path $sdkIncludeRoot "ucrt"),
        (Join-Path $sdkIncludeRoot "shared"),
        (Join-Path $sdkIncludeRoot "um"),
        (Join-Path $sdkIncludeRoot "winrt"),
        (Join-Path $sdkIncludeRoot "cppwinrt")
    ) -join ";"
    $libraryPath = @($vcLibraryPath, $sdkUcrtLibraryPath, $sdkUmLibraryPath) -join ";"

    [Environment]::SetEnvironmentVariable("PATH", $toolPath + ";" + $savedEnvironment["PATH"], "Process")
    [Environment]::SetEnvironmentVariable("INCLUDE", $includePath, "Process")
    [Environment]::SetEnvironmentVariable("LIB", $libraryPath, "Process")
    [Environment]::SetEnvironmentVariable("IncludePath", $includePath, "Process")
    [Environment]::SetEnvironmentVariable("LibraryPath", $libraryPath, "Process")
    [Environment]::SetEnvironmentVariable("VC_LibraryPath_x86", $vcLibraryPath, "Process")
    [Environment]::SetEnvironmentVariable(
        "WindowsSDK_LibraryPath_x86",
        $sdkUcrtLibraryPath + ";" + $sdkUmLibraryPath,
        "Process"
    )
    [Environment]::SetEnvironmentVariable("VSCMD_ARG_HOST_ARCH", "x64", "Process")
    [Environment]::SetEnvironmentVariable("VSCMD_ARG_TGT_ARCH", "x86", "Process")
    [Environment]::SetEnvironmentVariable("VCToolsVersion", $msvcVersion, "Process")
    [Environment]::SetEnvironmentVariable("VCToolsInstallDir", (Add-TrailingSeparator $msvcToolsRoot), "Process")
    [Environment]::SetEnvironmentVariable("WindowsSDKVersion", $sdkVersion + "\", "Process")
    [Environment]::SetEnvironmentVariable(
        "WindowsSdkBinPath",
        (Add-TrailingSeparator (Join-Path $windowsKitRoot "bin")),
        "Process"
    )

    Write-Host "Building OpenWyd $Configuration|Win32 from source"
    Write-Host "MSVC $msvcVersion; Windows SDK $sdkVersion"
    Write-Host "Output: $outDir"
    if ($OpenWydCompare) {
        Write-Host "Compile mode: OPENWYD_COMPARE=1"
    }

    & $msbuild @msbuildArguments 2>&1 | Tee-Object -FilePath $consoleLog
    $exitCode = $LASTEXITCODE
}
finally {
    foreach ($key in $environmentKeys) {
        [Environment]::SetEnvironmentVariable($key, $savedEnvironment[$key], "Process")
    }
}

$metadata.exitCode = $exitCode
$metadata.completedUtc = [DateTime]::UtcNow.ToString("o")

if ($exitCode -ne 0) {
    $metadata.sourceContract.status = "build-failed"
    $metadata | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $metadataPath -Encoding UTF8
    throw "MSBuild failed with exit code $exitCode. See $diagnosticLog"
}

$sourceContractAfter = New-OpenWydWindowsSourceContractFromManifest `
    -Manifest $sourceContractBefore `
    -RepoRoot $RepoRoot `
    -ToolsRoot $ToolsRoot
if (
    -not ([string]$sourceContractBefore.digest).Equals(
        [string]$sourceContractAfter.digest,
        [StringComparison]::OrdinalIgnoreCase
    )
) {
    $metadata.sourceContract.status = "changed-during-build"
    $metadata | ConvertTo-Json -Depth 8 |
        Set-Content -LiteralPath $metadataPath -Encoding UTF8
    throw (
        "Source inputs or toolchain changed while MSBuild was running. " +
        "The output is not certified; rebuild from a stable checkout."
    )
}
$verifiedContractManifestSha256 = (
    Get-FileHash -LiteralPath $sourceContractPath -Algorithm SHA256
).Hash
if (
    -not $verifiedContractManifestSha256.Equals(
        $sourceContractManifestSha256,
        [StringComparison]::OrdinalIgnoreCase
    )
) {
    $metadata.sourceContract.status = "manifest-changed-during-build"
    $metadata | ConvertTo-Json -Depth 8 |
        Set-Content -LiteralPath $metadataPath -Encoding UTF8
    throw "Windows source-contract manifest changed while MSBuild was running."
}
$metadata.sourceContract.status = "verified-after-build"

$outputExe = Join-Path $outDir "WYD.exe"
Assert-File $outputExe "Built Win32 client"
if (Test-PathWithin $outputExe $forbiddenRelease) {
    throw "Post-build safety check failed: output is inside v769ClientRelease."
}

$machine = Get-PeMachine $outputExe
if ($machine -ne 0x014C) {
    throw ("Expected PE32 x86 machine 0x014C, got 0x{0:X4}: {1}" -f $machine, $outputExe)
}

$outputFile = Get-Item -LiteralPath $outputExe
$outputSha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $outputExe).Hash
$provenanceBindingSha256 = Get-OpenWydWindowsProvenanceBindingSha256 `
    -ExecutableSha256 $outputSha256 `
    -ExecutableSize $outputFile.Length `
    -ContractDigest ([string]$sourceContractBefore.digest) `
    -ContractManifestSha256 $sourceContractManifestSha256
$metadata.output = [ordered]@{
    executable = $outputExe
    size = $outputFile.Length
    sha256 = $outputSha256
    peMachine = ("0x{0:X4}" -f $machine)
    objectCount = @(
        Get-ChildItem -LiteralPath $intDir -Filter "*.obj" -File -Recurse -ErrorAction SilentlyContinue
    ).Count
    sourceContractDigest = [string]$sourceContractBefore.digest
    sourceContractManifestSha256 = $sourceContractManifestSha256
    provenanceBindingSha256 = $provenanceBindingSha256
}
$metadata.sourceBuildCertified = $true
$metadata | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $metadataPath -Encoding UTF8

Write-Host "Build succeeded: $outputExe"
Write-Host "SHA-256: $($metadata.output.sha256)"
Write-Host "PE machine: $($metadata.output.peMachine)"
Write-Host "Objects: $($metadata.output.objectCount)"
