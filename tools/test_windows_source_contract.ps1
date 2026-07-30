[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
. (Join-Path $PSScriptRoot "windows_source_contract.ps1")

$testRoot = Join-Path $repoRoot (
    "artifacts\source-contract-unit-" + [Guid]::NewGuid().ToString("N")
)
$fixtureRepo = Join-Path $testRoot "repo"
$fixtureTools = Join-Path $testRoot "tools"

function Write-FixtureFile {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path,

        [Parameter(Mandatory = $true)]
        [string]$Contents
    )

    $parent = Split-Path -Parent $Path
    if (-not (Test-Path -LiteralPath $parent -PathType Container)) {
        New-Item -ItemType Directory -Force -Path $parent | Out-Null
    }
    [IO.File]::WriteAllText($Path, $Contents, [Text.UTF8Encoding]::new($false))
}

try {
    New-Item -ItemType Directory -Force -Path $fixtureRepo, $fixtureTools | Out-Null
    $projectDirectory = Join-Path $fixtureRepo "Projects\Client"
    $projectPath = Join-Path $projectDirectory "Client.vcxproj"
    $headerPath = Join-Path $projectDirectory "client.h"
    $compareProps = Join-Path $fixtureRepo "tools\compare.props"
    $buildScript = Join-Path $fixtureRepo "tools\build.ps1"
    $contractImplementation = Join-Path $fixtureRepo "tools\contract.ps1"
    $dependencyRoot = Join-Path $fixtureRepo "Dependencies\Directx\Include"
    $toolPath = Join-Path $fixtureTools "compiler\cl.exe"
    $manifestPath = Join-Path $testRoot "source-contract.json"

    Write-FixtureFile $projectPath @'
<?xml version="1.0" encoding="utf-8"?>
<Project xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
  <ItemGroup>
    <ClCompile Include="client.cpp" />
    <ClInclude Include="client.h" />
    <ResourceCompile Include="client.rc" />
    <Image Include="client.ico" />
  </ItemGroup>
</Project>
'@
    Write-FixtureFile (Join-Path $projectDirectory "client.cpp") (
        "#include `"client.h`"`nint main() { return CLIENT_VALUE; }`n"
    )
    Write-FixtureFile $headerPath "#define CLIENT_VALUE 7`n"
    Write-FixtureFile (Join-Path $projectDirectory "client.rc") (
        "IDI_CLIENT ICON `"client.ico`"`n"
    )
    Write-FixtureFile (Join-Path $projectDirectory "client.ico") "fixture-icon"
    Write-FixtureFile $compareProps "<Project />`n"
    Write-FixtureFile $buildScript "Write-Output build`n"
    Write-FixtureFile $contractImplementation "function ContractFixture {}`n"
    Write-FixtureFile (Join-Path $dependencyRoot "d3d9.h") "#define D3D9_FIXTURE 1`n"
    Write-FixtureFile $toolPath "fixture-compiler"

    $contractArguments = @{
        RepoRoot = $fixtureRepo
        ToolsRoot = $fixtureTools
        ProjectPath = $projectPath
        ComparePropsPath = $compareProps
        BuildScriptPath = $buildScript
        ContractScriptPath = $contractImplementation
        Configuration = "Debug"
        Platform = "Win32"
        PlatformToolset = "v142"
        OpenWydCompare = $true
        MsvcVersion = "14.29.fixture"
        WindowsSdkVersion = "10.0.fixture"
        SemanticArguments = @(
            "<REPO_ROOT>/Projects/Client/Client.vcxproj",
            "/p:Configuration=Debug",
            "/p:OpenWydCompare=true"
        )
        ToolchainFiles = @{ compiler = $toolPath }
        ToolchainDependencyRoots = @((Split-Path -Parent $toolPath))
        RepositoryDependencyRoots = @($dependencyRoot)
    }

    $first = New-OpenWydWindowsSourceContract @contractArguments
    $second = New-OpenWydWindowsSourceContract @contractArguments
    if ($first.digest -ne $second.digest) {
        throw "The source contract is not deterministic."
    }
    for ($index = 1; $index -lt @($first.toolchain).Count; $index++) {
        $previous = $first.toolchain[$index - 1]
        $current = $first.toolchain[$index]
        $order = [StringComparer]::Ordinal.Compare(
            [string]$previous.role,
            [string]$current.role
        )
        if ($order -eq 0) {
            $order = [StringComparer]::Ordinal.Compare(
                [string]$previous.path,
                [string]$current.path
            )
        }
        if ($order -gt 0) {
            throw "Toolchain records are not in deterministic ordinal order."
        }
    }
    if (@($first.inputs).Count -lt 8) {
        throw "The fixture source contract omitted declared/build dependency inputs."
    }
    $manifestHash = Write-OpenWydWindowsSourceContract $first $manifestPath
    $validated = Assert-OpenWydWindowsSourceContract `
        -ManifestPath $manifestPath `
        -RepoRoot $fixtureRepo `
        -ToolsRoot $fixtureTools `
        -ExpectedManifestSha256 $manifestHash `
        -ExpectedDigest $first.digest
    if ($validated.digest -ne $first.digest) {
        throw "A stable source contract did not validate."
    }

    Write-FixtureFile $headerPath "#define CLIENT_VALUE 8`n"
    $contentMutationRejected = $false
    try {
        Assert-OpenWydWindowsSourceContract `
            -ManifestPath $manifestPath `
            -RepoRoot $fixtureRepo `
            -ToolsRoot $fixtureTools | Out-Null
    }
    catch {
        $contentMutationRejected = $_.Exception.Message -like (
            "*changed after the client build*"
        )
    }
    if (-not $contentMutationRejected) {
        throw "A dirty content mutation was not rejected."
    }

    Write-FixtureFile $headerPath "#define CLIENT_VALUE 7`n"
    Write-FixtureFile $toolPath "mutated-fixture-compiler"
    $toolchainMutationRejected = $false
    try {
        Assert-OpenWydWindowsSourceContract `
            -ManifestPath $manifestPath `
            -RepoRoot $fixtureRepo `
            -ToolsRoot $fixtureTools | Out-Null
    }
    catch {
        $toolchainMutationRejected = $_.Exception.Message -like (
            "*changed after the client build*"
        )
    }
    if (-not $toolchainMutationRejected) {
        throw "A toolchain content mutation was not rejected."
    }

    Write-FixtureFile $toolPath "fixture-compiler"
    $newToolPath = Join-Path (Split-Path -Parent $toolPath) "new-backend.dll"
    Write-FixtureFile $newToolPath "new-toolchain-input"
    $newToolchainInputRejected = $false
    try {
        Assert-OpenWydWindowsSourceContract `
            -ManifestPath $manifestPath `
            -RepoRoot $fixtureRepo `
            -ToolsRoot $fixtureTools | Out-Null
    }
    catch {
        $newToolchainInputRejected = $_.Exception.Message -like (
            "*changed after the client build*"
        )
    }
    if (-not $newToolchainInputRejected) {
        throw "A newly added toolchain input was not rejected."
    }
    [IO.File]::Delete($newToolPath)

    Write-FixtureFile (Join-Path $projectDirectory "new_dirty_header.h") (
        "#define NEW_DIRTY_INPUT 1`n"
    )
    $newInputRejected = $false
    try {
        Assert-OpenWydWindowsSourceContract `
            -ManifestPath $manifestPath `
            -RepoRoot $fixtureRepo `
            -ToolsRoot $fixtureTools | Out-Null
    }
    catch {
        $newInputRejected = $_.Exception.Message -like (
            "*changed after the client build*"
        )
    }
    if (-not $newInputRejected) {
        throw "A newly added source input was not rejected."
    }

    $binding = Get-OpenWydWindowsProvenanceBindingSha256 `
        -ExecutableSha256 ("A" * 64) `
        -ExecutableSize 123 `
        -ContractDigest $first.digest `
        -ContractManifestSha256 $manifestHash
    $differentBinding = Get-OpenWydWindowsProvenanceBindingSha256 `
        -ExecutableSha256 ("B" * 64) `
        -ExecutableSize 123 `
        -ContractDigest $first.digest `
        -ContractManifestSha256 $manifestHash
    if ($binding -eq $differentBinding) {
        throw "The executable-to-contract binding ignored the executable hash."
    }

    Write-Output (
        "PASS windows source contract: deterministic, dirty-content/new-input/" +
        "toolchain/new-tool rejection, manifest verification, and executable binding"
    )
}
finally {
    $resolvedTestRoot = [IO.Path]::GetFullPath($testRoot)
    $resolvedArtifacts = [IO.Path]::GetFullPath((Join-Path $repoRoot "artifacts"))
    if (
        (Test-Path -LiteralPath $resolvedTestRoot) -and
        (Test-OpenWydContractPathWithin $resolvedTestRoot $resolvedArtifacts) -and
        -not $resolvedTestRoot.Equals(
            $resolvedArtifacts,
            [StringComparison]::OrdinalIgnoreCase
        )
    ) {
        Remove-Item -LiteralPath $resolvedTestRoot -Recurse -Force
    }
}
