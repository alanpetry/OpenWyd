Set-StrictMode -Version Latest

function Get-OpenWydContractSha256 {
    param(
        [Parameter(Mandatory = $true)]
        [byte[]]$Bytes
    )

    $algorithm = [Security.Cryptography.SHA256]::Create()
    try {
        return ([BitConverter]::ToString($algorithm.ComputeHash($Bytes))).Replace("-", "")
    }
    finally {
        $algorithm.Dispose()
    }
}

function Get-OpenWydContractTextSha256 {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Text
    )

    return Get-OpenWydContractSha256 ([Text.Encoding]::UTF8.GetBytes($Text))
}

function Test-OpenWydContractPathWithin {
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

function Get-OpenWydContractPathIdentifier {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path,

        [Parameter(Mandatory = $true)]
        [string]$RepoRoot,

        [Parameter(Mandatory = $true)]
        [string]$ToolsRoot
    )

    $fullPath = [IO.Path]::GetFullPath($Path)
    $fullRepoRoot = [IO.Path]::GetFullPath($RepoRoot).TrimEnd([char[]]@('\', '/'))
    $fullToolsRoot = [IO.Path]::GetFullPath($ToolsRoot).TrimEnd([char[]]@('\', '/'))
    if (Test-OpenWydContractPathWithin $fullPath $fullRepoRoot) {
        $relative = $fullPath.Substring($fullRepoRoot.Length).TrimStart('\', '/')
        return "repo:" + $relative.Replace('\', '/')
    }
    if (Test-OpenWydContractPathWithin $fullPath $fullToolsRoot) {
        $relative = $fullPath.Substring($fullToolsRoot.Length).TrimStart('\', '/')
        return "tools:" + $relative.Replace('\', '/')
    }

    throw "Source-contract path is outside RepoRoot and ToolsRoot: $fullPath"
}

function Resolve-OpenWydContractPathIdentifier {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Identifier,

        [Parameter(Mandatory = $true)]
        [string]$RepoRoot,

        [Parameter(Mandatory = $true)]
        [string]$ToolsRoot
    )

    if ($Identifier.StartsWith("repo:", [StringComparison]::Ordinal)) {
        $root = $RepoRoot
        $relative = $Identifier.Substring(5)
    }
    elseif ($Identifier.StartsWith("tools:", [StringComparison]::Ordinal)) {
        $root = $ToolsRoot
        $relative = $Identifier.Substring(6)
    }
    else {
        throw "Unsupported source-contract path identifier: $Identifier"
    }

    if ([IO.Path]::IsPathRooted($relative) -or $relative -match '(^|/)\.\.(/|$)') {
        throw "Unsafe source-contract path identifier: $Identifier"
    }
    $resolved = [IO.Path]::GetFullPath(
        (Join-Path ([IO.Path]::GetFullPath($root)) $relative.Replace('/', '\'))
    )
    if (-not (Test-OpenWydContractPathWithin $resolved $root)) {
        throw "Source-contract path escaped its declared root: $Identifier"
    }
    return $resolved
}

function Add-OpenWydContractFile {
    param(
        [Parameter(Mandatory = $true)]
        [hashtable]$Files,

        [Parameter(Mandatory = $true)]
        [string]$Path,

        [Parameter(Mandatory = $true)]
        [string]$Role,

        [Parameter(Mandatory = $true)]
        [string]$RepoRoot,

        [Parameter(Mandatory = $true)]
        [string]$ToolsRoot
    )

    $fullPath = [IO.Path]::GetFullPath($Path)
    if (-not (Test-Path -LiteralPath $fullPath -PathType Leaf)) {
        throw "Source-contract input is missing ($Role): $fullPath"
    }
    $identifier = Get-OpenWydContractPathIdentifier $fullPath $RepoRoot $ToolsRoot
    $key = $identifier.ToLowerInvariant()
    if (-not $Files.ContainsKey($key)) {
        $Files[$key] = [ordered]@{
            fullPath = $fullPath
            path = $identifier
            roles = [Collections.Generic.HashSet[string]]::new(
                [StringComparer]::Ordinal
            )
        }
    }
    [void]$Files[$key].roles.Add($Role)
}

function ConvertTo-OpenWydContractToken {
    param(
        [AllowEmptyString()]
        [string]$Value
    )

    if ($null -eq $Value) {
        $Value = ""
    }
    return [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($Value))
}

function New-OpenWydWindowsSourceContract {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)]
        [string]$RepoRoot,

        [Parameter(Mandatory = $true)]
        [string]$ToolsRoot,

        [Parameter(Mandatory = $true)]
        [string]$ProjectPath,

        [string]$ComparePropsPath = "",

        [Parameter(Mandatory = $true)]
        [string]$BuildScriptPath,

        [Parameter(Mandatory = $true)]
        [string]$ContractScriptPath,

        [Parameter(Mandatory = $true)]
        [string]$Configuration,

        [Parameter(Mandatory = $true)]
        [string]$Platform,

        [Parameter(Mandatory = $true)]
        [string]$PlatformToolset,

        [Parameter(Mandatory = $true)]
        [bool]$OpenWydCompare,

        [Parameter(Mandatory = $true)]
        [string]$MsvcVersion,

        [Parameter(Mandatory = $true)]
        [string]$WindowsSdkVersion,

        [Parameter(Mandatory = $true)]
        [string[]]$SemanticArguments,

        [Parameter(Mandatory = $true)]
        [hashtable]$ToolchainFiles,

        [Parameter(Mandatory = $true)]
        [string[]]$ToolchainDependencyRoots,

        [Parameter(Mandatory = $true)]
        [string[]]$RepositoryDependencyRoots
    )

    $RepoRoot = [IO.Path]::GetFullPath($RepoRoot)
    $ToolsRoot = [IO.Path]::GetFullPath($ToolsRoot)
    $ProjectPath = [IO.Path]::GetFullPath($ProjectPath)
    $BuildScriptPath = [IO.Path]::GetFullPath($BuildScriptPath)
    $ContractScriptPath = [IO.Path]::GetFullPath($ContractScriptPath)
    if (-not (Test-Path -LiteralPath $ProjectPath -PathType Leaf)) {
        throw "Source-contract project is missing: $ProjectPath"
    }

    $projectDirectory = Split-Path -Parent $ProjectPath
    $projectTreeExtensions = @(
        ".asm", ".bmp", ".c", ".cc", ".cpp", ".cur", ".cxx", ".def",
        ".h", ".hh", ".hpp", ".hxx", ".ico", ".idl", ".inl", ".manifest",
        ".png", ".props", ".rc", ".rc2", ".targets", ".vcxproj"
    )
    $declaredItemTypes = @(
        "ClCompile", "ClInclude", "ResourceCompile", "Image", "None",
        "Manifest", "Midl", "CustomBuild"
    )

    $files = @{}
    Add-OpenWydContractFile $files $ProjectPath "project" $RepoRoot $ToolsRoot
    Add-OpenWydContractFile $files $BuildScriptPath "build-script" $RepoRoot $ToolsRoot
    Add-OpenWydContractFile $files $ContractScriptPath "contract-script" $RepoRoot $ToolsRoot
    if ($OpenWydCompare) {
        if ([string]::IsNullOrWhiteSpace($ComparePropsPath)) {
            throw "ComparePropsPath is required when OpenWydCompare is enabled."
        }
        Add-OpenWydContractFile (
            $files
        ) $ComparePropsPath "compare-property-sheet" $RepoRoot $ToolsRoot
    }

    try {
        [xml]$projectXml = Get-Content -LiteralPath $ProjectPath -Raw
    }
    catch {
        throw "Unable to parse source-contract project XML: $ProjectPath ($($_.Exception.Message))"
    }
    $itemTypePredicate = ($declaredItemTypes | ForEach-Object {
        "local-name()='$_'"
    }) -join " or "
    $declaredNodes = @(
        $projectXml.SelectNodes("//*[@" + "Include and (" + $itemTypePredicate + ")]")
    )
    foreach ($node in $declaredNodes) {
        $include = [string]$node.Include
        if ([string]::IsNullOrWhiteSpace($include)) {
            continue
        }
        if ($include.IndexOfAny([char[]]@('*', '?')) -ge 0 -or $include.Contains('$(')) {
            throw "Source-contract cannot resolve project item '$include' ($($node.LocalName))."
        }
        $declaredPath = [IO.Path]::GetFullPath((Join-Path $projectDirectory $include))
        Add-OpenWydContractFile (
            $files
        ) $declaredPath ("project-item:" + $node.LocalName) $RepoRoot $ToolsRoot
    }

    foreach ($candidate in Get-ChildItem -LiteralPath $projectDirectory -File -Recurse) {
        if ($projectTreeExtensions -contains $candidate.Extension.ToLowerInvariant()) {
            Add-OpenWydContractFile (
                $files
            ) $candidate.FullName "project-tree-input" $RepoRoot $ToolsRoot
        }
    }

    $dependencyRootIdentifiers = @()
    foreach ($dependencyRoot in $RepositoryDependencyRoots) {
        $fullDependencyRoot = [IO.Path]::GetFullPath($dependencyRoot)
        if (-not (Test-Path -LiteralPath $fullDependencyRoot -PathType Container)) {
            throw "Source-contract dependency root is missing: $fullDependencyRoot"
        }
        if (-not (Test-OpenWydContractPathWithin $fullDependencyRoot $RepoRoot)) {
            throw "Repository dependency root is outside RepoRoot: $fullDependencyRoot"
        }
        $dependencyRootIdentifiers += Get-OpenWydContractPathIdentifier (
            $fullDependencyRoot
        ) $RepoRoot $ToolsRoot
        foreach ($candidate in Get-ChildItem -LiteralPath $fullDependencyRoot -File -Recurse) {
            Add-OpenWydContractFile (
                $files
            ) $candidate.FullName "repository-build-dependency" $RepoRoot $ToolsRoot
        }
    }
    $dependencyRootIdentifiers = @($dependencyRootIdentifiers | Sort-Object -Unique)

    $inputRecords = @(
        $files.Values |
            Sort-Object { $_.path } |
            ForEach-Object {
                $file = Get-Item -LiteralPath $_.fullPath
                [ordered]@{
                    path = $_.path
                    roles = @($_.roles | Sort-Object)
                    size = [long]$file.Length
                    sha256 = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash
                }
            }
    )

    $toolchainRecords = @()
    $explicitToolchainSelection = @()
    foreach ($role in @($ToolchainFiles.Keys | Sort-Object)) {
        $toolPath = [IO.Path]::GetFullPath([string]$ToolchainFiles[$role])
        if (-not (Test-Path -LiteralPath $toolPath -PathType Leaf)) {
            throw "Source-contract toolchain file is missing ($role): $toolPath"
        }
        $toolPathIdentifier = Get-OpenWydContractPathIdentifier (
            $toolPath
        ) $RepoRoot $ToolsRoot
        $explicitToolchainSelection += [ordered]@{
            role = [string]$role
            path = $toolPathIdentifier
        }
        $toolFile = Get-Item -LiteralPath $toolPath
        $toolchainRecords += [ordered]@{
            role = [string]$role
            path = $toolPathIdentifier
            size = [long]$toolFile.Length
            sha256 = (Get-FileHash -LiteralPath $toolPath -Algorithm SHA256).Hash
            fileVersion = [string]$toolFile.VersionInfo.FileVersion
        }
    }
    $toolchainDependencyRootIdentifiers = @()
    foreach ($toolchainRoot in $ToolchainDependencyRoots) {
        $fullToolchainRoot = [IO.Path]::GetFullPath($toolchainRoot)
        if (-not (Test-Path -LiteralPath $fullToolchainRoot -PathType Container)) {
            throw "Source-contract toolchain root is missing: $fullToolchainRoot"
        }
        if (-not (Test-OpenWydContractPathWithin $fullToolchainRoot $ToolsRoot)) {
            throw "Toolchain dependency root is outside ToolsRoot: $fullToolchainRoot"
        }
        $toolchainRootIdentifier = Get-OpenWydContractPathIdentifier (
            $fullToolchainRoot
        ) $RepoRoot $ToolsRoot
        $toolchainDependencyRootIdentifiers += $toolchainRootIdentifier
        foreach (
            $toolFile in Get-ChildItem -LiteralPath $fullToolchainRoot -File -Recurse
        ) {
            $relativeToolPath = $toolFile.FullName.Substring(
                $fullToolchainRoot.TrimEnd([char[]]@('\', '/')).Length
            ).TrimStart('\', '/').Replace('\', '/')
            $toolchainRecords += [ordered]@{
                role = "dependency-root:$toolchainRootIdentifier/$relativeToolPath"
                path = Get-OpenWydContractPathIdentifier (
                    $toolFile.FullName
                ) $RepoRoot $ToolsRoot
                size = [long]$toolFile.Length
                sha256 = (
                    Get-FileHash -LiteralPath $toolFile.FullName -Algorithm SHA256
                ).Hash
                fileVersion = [string]$toolFile.VersionInfo.FileVersion
            }
        }
    }
    $toolchainDependencyRootIdentifiers = @(
        $toolchainDependencyRootIdentifiers | Sort-Object -Unique
    )
    # OrderedDictionary keys are available through PowerShell's adapter for
    # serialization, but `Sort-Object role, path` does not reliably resolve
    # them as properties on Windows PowerShell 5.1. Use an explicit ordinal
    # comparer so filesystem enumeration order and host culture cannot change
    # the contract digest.
    $orderedToolchainRecords = [Collections.Generic.List[object]]::new()
    foreach ($record in $toolchainRecords) {
        $orderedToolchainRecords.Add($record)
    }
    $toolchainRecordComparison = [Comparison[object]] {
        param($left, $right)
        $result = [StringComparer]::Ordinal.Compare(
            [string]$left.role,
            [string]$right.role
        )
        if ($result -ne 0) {
            return $result
        }
        return [StringComparer]::Ordinal.Compare(
            [string]$left.path,
            [string]$right.path
        )
    }
    $orderedToolchainRecords.Sort($toolchainRecordComparison)
    $toolchainRecords = @($orderedToolchainRecords)

    $selection = [ordered]@{
        project = Get-OpenWydContractPathIdentifier $ProjectPath $RepoRoot $ToolsRoot
        compareProps = if ($OpenWydCompare) {
            Get-OpenWydContractPathIdentifier $ComparePropsPath $RepoRoot $ToolsRoot
        }
        else {
            $null
        }
        buildScript = Get-OpenWydContractPathIdentifier $BuildScriptPath $RepoRoot $ToolsRoot
        contractScript = Get-OpenWydContractPathIdentifier (
            $ContractScriptPath
        ) $RepoRoot $ToolsRoot
        projectTreeExtensions = @($projectTreeExtensions | Sort-Object)
        projectDeclaredItemTypes = @($declaredItemTypes | Sort-Object)
        repositoryDependencyRoots = $dependencyRootIdentifiers
        toolchainDependencyRoots = $toolchainDependencyRootIdentifiers
        toolchainFiles = $explicitToolchainSelection
    }
    $build = [ordered]@{
        configuration = $Configuration
        platform = $Platform
        platformToolset = $PlatformToolset
        openWydCompare = $OpenWydCompare
        msvcVersion = $MsvcVersion
        windowsSdkVersion = $WindowsSdkVersion
        semanticArguments = @($SemanticArguments)
        optionInjectionEnvironment = [ordered]@{
            CL = ""
            _CL_ = ""
            LINK = ""
            _LINK_ = ""
        }
    }

    $digestLines = [Collections.Generic.List[string]]::new()
    $digestLines.Add("schema|openwyd.windows-source-contract|2")
    foreach ($propertyName in @(
        "configuration", "platform", "platformToolset", "openWydCompare",
        "msvcVersion", "windowsSdkVersion"
    )) {
        $value = [string]$build[$propertyName]
        $digestLines.Add(
            "build|$propertyName|" + (ConvertTo-OpenWydContractToken $value)
        )
    }
    for ($index = 0; $index -lt $SemanticArguments.Count; $index++) {
        $digestLines.Add(
            "argument|$index|" +
            (ConvertTo-OpenWydContractToken ([string]$SemanticArguments[$index]))
        )
    }
    foreach ($propertyName in @("CL", "_CL_", "LINK", "_LINK_")) {
        $digestLines.Add(
            "option-environment|$propertyName|" +
            (ConvertTo-OpenWydContractToken (
                [string]$build.optionInjectionEnvironment[$propertyName]
            ))
        )
    }
    foreach ($propertyName in @(
        "project", "compareProps", "buildScript", "contractScript"
    )) {
        $digestLines.Add(
            "selection|$propertyName|" +
            (ConvertTo-OpenWydContractToken ([string]$selection[$propertyName]))
        )
    }
    foreach ($extension in $selection.projectTreeExtensions) {
        $digestLines.Add(
            "selection-extension|" + (ConvertTo-OpenWydContractToken $extension)
        )
    }
    foreach ($itemType in $selection.projectDeclaredItemTypes) {
        $digestLines.Add(
            "selection-item-type|" + (ConvertTo-OpenWydContractToken $itemType)
        )
    }
    foreach ($dependencyRoot in $selection.repositoryDependencyRoots) {
        $digestLines.Add(
            "selection-dependency-root|" +
            (ConvertTo-OpenWydContractToken $dependencyRoot)
        )
    }
    foreach ($toolchainRoot in $selection.toolchainDependencyRoots) {
        $digestLines.Add(
            "selection-toolchain-root|" +
            (ConvertTo-OpenWydContractToken $toolchainRoot)
        )
    }
    foreach ($toolchainFile in $selection.toolchainFiles) {
        $digestLines.Add(
            "selection-toolchain-file|" +
            (ConvertTo-OpenWydContractToken ([string]$toolchainFile.role)) + "|" +
            (ConvertTo-OpenWydContractToken ([string]$toolchainFile.path))
        )
    }
    foreach ($inputRecord in $inputRecords) {
        $digestLines.Add(
            "input|" +
            (ConvertTo-OpenWydContractToken $inputRecord.path) + "|" +
            $inputRecord.size + "|" +
            $inputRecord.sha256 + "|" +
            (ConvertTo-OpenWydContractToken ($inputRecord.roles -join ","))
        )
    }
    foreach ($toolchainRecord in $toolchainRecords) {
        $digestLines.Add(
            "toolchain|" +
            (ConvertTo-OpenWydContractToken $toolchainRecord.role) + "|" +
            (ConvertTo-OpenWydContractToken $toolchainRecord.path) + "|" +
            $toolchainRecord.size + "|" +
            $toolchainRecord.sha256 + "|" +
            (ConvertTo-OpenWydContractToken $toolchainRecord.fileVersion)
        )
    }
    $digestPayload = ($digestLines -join "`n") + "`n"

    return [ordered]@{
        schema = "openwyd.windows-source-contract"
        schemaVersion = 2
        digestAlgorithm = "sha256"
        digest = Get-OpenWydContractTextSha256 $digestPayload
        selection = $selection
        build = $build
        inputs = $inputRecords
        toolchain = $toolchainRecords
    }
}

function New-OpenWydWindowsSourceContractFromManifest {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)]
        [object]$Manifest,

        [Parameter(Mandatory = $true)]
        [string]$RepoRoot,

        [Parameter(Mandatory = $true)]
        [string]$ToolsRoot
    )

    if (
        [string]$Manifest.schema -ne "openwyd.windows-source-contract" -or
        [int]$Manifest.schemaVersion -ne 2 -or
        [string]$Manifest.digestAlgorithm -ne "sha256"
    ) {
        throw "Unsupported OpenWyd Windows source-contract schema."
    }
    $toolchainFiles = @{}
    foreach ($tool in @($Manifest.selection.toolchainFiles)) {
        $role = [string]$tool.role
        if ([string]::IsNullOrWhiteSpace($role) -or $toolchainFiles.ContainsKey($role)) {
            throw "Source-contract manifest has an invalid duplicate toolchain role."
        }
        $toolchainFiles[$role] = Resolve-OpenWydContractPathIdentifier (
            [string]$tool.path
        ) $RepoRoot $ToolsRoot
    }
    $dependencyRoots = @(
        $Manifest.selection.repositoryDependencyRoots |
            ForEach-Object {
                Resolve-OpenWydContractPathIdentifier (
                    [string]$_
                ) $RepoRoot $ToolsRoot
            }
    )
    $toolchainRoots = @(
        $Manifest.selection.toolchainDependencyRoots |
            ForEach-Object {
                Resolve-OpenWydContractPathIdentifier (
                    [string]$_
                ) $RepoRoot $ToolsRoot
            }
    )

    $resolvedProject = Resolve-OpenWydContractPathIdentifier (
        [string]$Manifest.selection.project
    ) $RepoRoot $ToolsRoot
    $resolvedCompareProps = ""
    if ($null -ne $Manifest.selection.compareProps) {
        $resolvedCompareProps = Resolve-OpenWydContractPathIdentifier (
            [string]$Manifest.selection.compareProps
        ) $RepoRoot $ToolsRoot
    }
    $resolvedBuildScript = Resolve-OpenWydContractPathIdentifier (
        [string]$Manifest.selection.buildScript
    ) $RepoRoot $ToolsRoot
    $resolvedContractScript = Resolve-OpenWydContractPathIdentifier (
        [string]$Manifest.selection.contractScript
    ) $RepoRoot $ToolsRoot

    return New-OpenWydWindowsSourceContract `
        -RepoRoot $RepoRoot `
        -ToolsRoot $ToolsRoot `
        -ProjectPath $resolvedProject `
        -ComparePropsPath $resolvedCompareProps `
        -BuildScriptPath $resolvedBuildScript `
        -ContractScriptPath $resolvedContractScript `
        -Configuration ([string]$Manifest.build.configuration) `
        -Platform ([string]$Manifest.build.platform) `
        -PlatformToolset ([string]$Manifest.build.platformToolset) `
        -OpenWydCompare ([bool]$Manifest.build.openWydCompare) `
        -MsvcVersion ([string]$Manifest.build.msvcVersion) `
        -WindowsSdkVersion ([string]$Manifest.build.windowsSdkVersion) `
        -SemanticArguments @($Manifest.build.semanticArguments) `
        -ToolchainFiles $toolchainFiles `
        -ToolchainDependencyRoots $toolchainRoots `
        -RepositoryDependencyRoots $dependencyRoots
}

function Write-OpenWydWindowsSourceContract {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)]
        [object]$Contract,

        [Parameter(Mandatory = $true)]
        [string]$Path
    )

    $json = ($Contract | ConvertTo-Json -Depth 12) + [Environment]::NewLine
    [IO.File]::WriteAllText(
        [IO.Path]::GetFullPath($Path),
        $json,
        [Text.UTF8Encoding]::new($false)
    )
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash
}

function Assert-OpenWydWindowsSourceContract {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)]
        [string]$ManifestPath,

        [Parameter(Mandatory = $true)]
        [string]$RepoRoot,

        [Parameter(Mandatory = $true)]
        [string]$ToolsRoot,

        [string]$ExpectedManifestSha256 = "",

        [string]$ExpectedDigest = ""
    )

    if (-not (Test-Path -LiteralPath $ManifestPath -PathType Leaf)) {
        throw "Windows source-contract manifest is missing: $ManifestPath"
    }
    $actualManifestHash = (
        Get-FileHash -LiteralPath $ManifestPath -Algorithm SHA256
    ).Hash
    if (
        -not [string]::IsNullOrWhiteSpace($ExpectedManifestSha256) -and
        -not $actualManifestHash.Equals(
            $ExpectedManifestSha256,
            [StringComparison]::OrdinalIgnoreCase
        )
    ) {
        throw "Windows source-contract manifest hash does not match build metadata."
    }
    try {
        $stored = Get-Content -LiteralPath $ManifestPath -Raw | ConvertFrom-Json
    }
    catch {
        throw "Windows source-contract manifest is invalid JSON: $ManifestPath"
    }
    if (
        -not [string]::IsNullOrWhiteSpace($ExpectedDigest) -and
        -not ([string]$stored.digest).Equals(
            $ExpectedDigest,
            [StringComparison]::OrdinalIgnoreCase
        )
    ) {
        throw "Windows source-contract digest does not match build metadata."
    }
    $recomputed = New-OpenWydWindowsSourceContractFromManifest `
        -Manifest $stored `
        -RepoRoot $RepoRoot `
        -ToolsRoot $ToolsRoot
    if (
        -not ([string]$stored.digest).Equals(
            [string]$recomputed.digest,
            [StringComparison]::OrdinalIgnoreCase
        )
    ) {
        throw (
            "Windows source inputs or toolchain changed after the client build. " +
            "Rebuild WYD.exe from the current checkout."
        )
    }
    return [ordered]@{
        manifest = $stored
        recomputed = $recomputed
        manifestSha256 = $actualManifestHash
        digest = [string]$recomputed.digest
    }
}

function Get-OpenWydWindowsProvenanceBindingSha256 {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)]
        [string]$ExecutableSha256,

        [Parameter(Mandatory = $true)]
        [long]$ExecutableSize,

        [Parameter(Mandatory = $true)]
        [string]$ContractDigest,

        [Parameter(Mandatory = $true)]
        [string]$ContractManifestSha256
    )

    $payload = (
        "schema|openwyd.windows-build-provenance-binding|1`n" +
        "executable-sha256|$($ExecutableSha256.ToUpperInvariant())`n" +
        "executable-size|$ExecutableSize`n" +
        "contract-digest|$($ContractDigest.ToUpperInvariant())`n" +
        "contract-manifest-sha256|$($ContractManifestSha256.ToUpperInvariant())`n"
    )
    return Get-OpenWydContractTextSha256 $payload
}
