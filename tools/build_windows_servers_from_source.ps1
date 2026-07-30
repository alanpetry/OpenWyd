<#
.SYNOPSIS
Builds the recovered DBSrv and TMSrv sources without trusting recovered binaries.

.DESCRIPTION
Copies a text-only subset of an external server source bundle into an ignored
artifact directory, applies the debug-only OPENWYD_COMPARE loopback adaptation
to that copy, compiles the .cpp entries from the two vcxproj files, and prepares
a loopback runtime from data files only.

Recovered EXEs, DLLs, LIBs, objects, PDBs, and other compiled files are never
copied into the working source or runtime and are never passed to the linker.
The linker receives only the MSVC/Windows SDK libraries listed in this script.

The accepted input layouts are:

  <ServerSourceRoot>\Source\Code\DBSrv\DBSrv.vcxproj
  <ServerSourceRoot>\Source\Code\TMSrv\TMSrv.vcxproj
  <ServerSourceRoot>\Server\DBSrv\run
  <ServerSourceRoot>\Server\TMSrv\run

or a direct Source root, with -RuntimeDataRoot pointing at the Server data root.

.PARAMETER ServerSourceRoot
External bundle root or direct Source root. The default is the ignored
artifacts/server-stack/input directory.

.PARAMETER RuntimeDataRoot
Server data root containing Common, DBSrv/run, and TMSrv/run. It is inferred
from a bundle layout when omitted.

.PARAMETER OutputRoot
Ignored output directory. It must be a strict descendant of this repository's
artifacts directory. The default is artifacts/server-stack/source-build.

.PARAMETER Project
Build both servers (default) or one server while still preparing the data-only
runtime.

.PARAMETER Clean
Reuses an output directory only when it contains this tool's safety sentinel.
No source input directory is ever modified or deleted.

.PARAMETER ValidateOnly
Validates paths and the expected source/data layout without copying or building.

.PARAMETER RestoreBaseline
Restores account, char, and capsule from the generated baseline snapshot. The
servers must be stopped. Current state is moved to a timestamped backup first.

.PARAMETER TestAccountOne
First local comparison account. It is created through CFileDB::AddAccount.

.PARAMETER TestAccountTwo
Second local comparison account. It is created through CFileDB::AddAccount.

.PARAMETER TestAccountPassword
Password used only for the two local comparison accounts. It is written only
to an ignored local manifest. Do not pass a real credential.

.PARAMETER Help
Prints command-line help without probing the source bundle or toolchain.
#>
[CmdletBinding()]
param(
    [string]$ServerSourceRoot = "",

    [string]$RuntimeDataRoot = "",

    [string]$OutputRoot = "",

    [string]$RepoRoot = "",

    [string]$ToolsRoot = "",

    [ValidateSet("All", "DBSrv", "TMSrv")]
    [string]$Project = "All",

    [switch]$Clean,

    [switch]$ValidateOnly,

    [switch]$RestoreBaseline,

    [string]$TestAccountOne = "CMPNATIVE",

    [string]$TestAccountTwo = "CMPWASM",

    [string]$TestAccountPassword = "compare123",

    [Alias("h")]
    [switch]$Help
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

if ($Help) {
    @"
Usage:
  powershell -ExecutionPolicy Bypass -File tools/build_windows_servers_from_source.ps1 [options]

Options:
  -ServerSourceRoot <path>  Bundle root or direct Source root.
  -RuntimeDataRoot <path>   Server data root when it cannot be inferred.
  -OutputRoot <path>        Strict descendant of artifacts/.
  -RepoRoot <path>          Override the detected OpenWyd repository root.
  -ToolsRoot <path>         Override the workspace-local .tools directory.
  -Project All|DBSrv|TMSrv  Select projects (default: All).
  -Clean                    Reuse a sentinel-owned output directory.
  -ValidateOnly             Validate paths/layout only.
  -RestoreBaseline          Restore flat-file state; servers must be stopped.
  -TestAccountOne <id>      First local test account (default: CMPNATIVE).
  -TestAccountTwo <id>      Second local test account (default: CMPWASM).
  -TestAccountPassword <p>  Local-only test password (default: compare123).
  -Help                     Print this message.

Default input layout:
  artifacts/server-stack/input/Source
  artifacts/server-stack/input/Server

Default output:
  artifacts/server-stack/source-build
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

    $prefix = $parentPath + [IO.Path]::DirectorySeparatorChar
    return $candidatePath.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)
}

function Get-RelativePath {
    param(
        [Parameter(Mandatory = $true)]
        [string]$BasePath,

        [Parameter(Mandatory = $true)]
        [string]$Path
    )

    $baseUri = New-Object Uri((Add-TrailingSeparator (Get-FullPath $BasePath)))
    $pathUri = New-Object Uri((Get-FullPath $Path))
    $relative = [Uri]::UnescapeDataString($baseUri.MakeRelativeUri($pathUri).ToString())
    return $relative.Replace('/', '\')
}

function Assert-Directory {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path,

        [Parameter(Mandatory = $true)]
        [string]$Description
    )

    if (-not (Test-Path -LiteralPath $Path -PathType Container)) {
        throw "$Description not found: $Path"
    }
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

function Assert-NotFileSystemRoot {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path,

        [Parameter(Mandatory = $true)]
        [string]$Description
    )

    $fullPath = (Get-FullPath $Path).TrimEnd([char[]]@('\', '/'))
    $root = [IO.Path]::GetPathRoot($fullPath).TrimEnd([char[]]@('\', '/'))
    if ($fullPath.Equals($root, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing dangerous $Description at a filesystem root: $Path"
    }
}

function Assert-NoReparsePoints {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Root,

        [Parameter(Mandatory = $true)]
        [string]$Description
    )

    $reparsePoints = @(
        Get-ChildItem -LiteralPath $Root -Force -Recurse -ErrorAction Stop |
            Where-Object { ($_.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0 }
    )
    if ($reparsePoints.Count -ne 0) {
        throw "$Description contains a reparse point, which is not accepted: $($reparsePoints[0].FullName)"
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

function Test-IsPortableExecutable {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path
    )

    $file = Get-Item -LiteralPath $Path
    if ($file.Length -lt 2) {
        return $false
    }

    $stream = [IO.File]::Open(
        $Path,
        [IO.FileMode]::Open,
        [IO.FileAccess]::Read,
        [IO.FileShare]::ReadWrite
    )
    try {
        return ($stream.ReadByte() -eq 0x4D -and $stream.ReadByte() -eq 0x5A)
    }
    finally {
        $stream.Dispose()
    }
}

function Write-JsonFile {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path,

        [Parameter(Mandatory = $true)]
        [object]$Value,

        [int]$Depth = 12
    )

    $json = $Value | ConvertTo-Json -Depth $Depth
    [IO.File]::WriteAllText(
        $Path,
        $json + [Environment]::NewLine,
        (New-Object Text.UTF8Encoding($false))
    )
}

function Copy-TreeFiles {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Source,

        [Parameter(Mandatory = $true)]
        [string]$Destination,

        [Parameter(Mandatory = $true)]
        [scriptblock]$IncludeFile
    )

    New-Item -ItemType Directory -Force -Path $Destination | Out-Null
    foreach ($directory in @(Get-ChildItem -LiteralPath $Source -Directory -Recurse -Force)) {
        $relativeDirectory = Get-RelativePath $Source $directory.FullName
        New-Item -ItemType Directory -Force -Path (Join-Path $Destination $relativeDirectory) | Out-Null
    }

    $copied = New-Object Collections.Generic.List[object]
    $excluded = New-Object Collections.Generic.List[object]
    foreach ($file in @(Get-ChildItem -LiteralPath $Source -File -Recurse -Force | Sort-Object FullName)) {
        $relative = Get-RelativePath $Source $file.FullName
        $decision = & $IncludeFile $file $relative
        if ($decision.Include) {
            $destinationFile = Join-Path $Destination $relative
            New-Item -ItemType Directory -Force -Path (Split-Path -Parent $destinationFile) | Out-Null
            Copy-Item -LiteralPath $file.FullName -Destination $destinationFile
            $copied.Add([ordered]@{
                path = $relative
                size = $file.Length
                sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $file.FullName).Hash
            })
        }
        else {
            $entry = [ordered]@{
                path = $relative
                size = $file.Length
                reason = $decision.Reason
            }
            if ($decision.Hash) {
                $entry.sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $file.FullName).Hash
            }
            $excluded.Add($entry)
        }
    }

    return [ordered]@{
        copied = $copied.ToArray()
        excluded = $excluded.ToArray()
    }
}

function Apply-CompareSourcePatches {
    param(
        [Parameter(Mandatory = $true)]
        [string]$WorkCodeRoot
    )

    $windows1252 = [Text.Encoding]::GetEncoding(1252)
    $patchResults = New-Object Collections.Generic.List[object]

    $serverPath = Join-Path $WorkCodeRoot "DBSrv\Server.cpp"
    Assert-File $serverPath "DBSrv Server.cpp"
    $serverText = [IO.File]::ReadAllText($serverPath, $windows1252)
    $marker = "#if defined(OPENWYD_COMPARE) && defined(_DEBUG)"
    if ($serverText.IndexOf($marker, [StringComparison]::Ordinal) -lt 0) {
        $anchor = "`t/*freeaddrinfo(res);"
        $anchorIndex = $serverText.IndexOf($anchor, [StringComparison]::Ordinal)
        if ($anchorIndex -lt 0 -or
            $serverText.IndexOf($anchor, $anchorIndex + $anchor.Length, [StringComparison]::Ordinal) -ge 0) {
            throw "DBSrv loopback patch anchor is missing or ambiguous: $serverPath"
        }

        $newLine = if ($serverText.Contains("`r`n")) { "`r`n" } else { "`n" }
        $blockLines = @(
            "#if defined(OPENWYD_COMPARE) && defined(_DEBUG)",
            "`tif (ServerIndex == -1)",
            "`t{",
            "`t`tfor (i = 0; i < MAX_SERVERGROUP; i++)",
            "`t`t{",
            "`t`t`tif (!strcmp(g_pServerList[i][0], `"127.0.0.1`"))",
            "`t`t`t{",
            "`t`t`t`tServerIndex = i;",
            "`t`t`t`tLocalIP[0] = 127;",
            "`t`t`t`tLocalIP[1] = 0;",
            "`t`t`t`tLocalIP[2] = 0;",
            "`t`t`t`tLocalIP[3] = 1;",
            "`t`t`t`tbreak;",
            "`t`t`t}",
            "`t`t}",
            "`t}",
            "#endif",
            ""
        )
        $block = ($blockLines -join $newLine) + $newLine
        $serverText = $serverText.Insert($anchorIndex, $block)
        [IO.File]::WriteAllText($serverPath, $serverText, $windows1252)
        $patchResults.Add([ordered]@{
            file = "DBSrv\Server.cpp"
            patch = "debug-only loopback server identity"
            result = "applied"
            guard = "defined(OPENWYD_COMPARE) && defined(_DEBUG)"
        })
    }
    else {
        $patchResults.Add([ordered]@{
            file = "DBSrv\Server.cpp"
            patch = "debug-only loopback server identity"
            result = "already-present"
            guard = "defined(OPENWYD_COMPARE) && defined(_DEBUG)"
        })
    }

    $sqliteHeader = Join-Path $WorkCodeRoot "DBSrv\Sqlite_Connect.h"
    Assert-File $sqliteHeader "DBSrv Sqlite_Connect.h"
    $sqliteText = [IO.File]::ReadAllText($sqliteHeader, $windows1252)
    $sqlitePattern = "(?m)^[ `t]*#include[ `t]+<sqlite3\.h>[ `t]*\r?\n"
    $sqliteMatches = [Text.RegularExpressions.Regex]::Matches($sqliteText, $sqlitePattern)
    if ($sqliteMatches.Count -gt 1) {
        throw "Unexpected duplicate sqlite3 includes: $sqliteHeader"
    }
    if ($sqliteMatches.Count -eq 1) {
        $sqliteText = [Text.RegularExpressions.Regex]::Replace($sqliteText, $sqlitePattern, "")
        [IO.File]::WriteAllText($sqliteHeader, $sqliteText, $windows1252)
        $sqliteResult = "applied"
    }
    else {
        $sqliteResult = "already-absent"
    }
    $patchResults.Add([ordered]@{
        file = "DBSrv\Sqlite_Connect.h"
        patch = "remove unused missing sqlite3 header"
        result = $sqliteResult
        evidence = "Sqlite_Connect.cpp contains no active sqlite3 call"
    })

    return $patchResults.ToArray()
}

function Get-ProjectCppSources {
    param(
        [Parameter(Mandatory = $true)]
        [string]$ProjectName,

        [Parameter(Mandatory = $true)]
        [string]$WorkCodeRoot
    )

    $projectDirectory = Join-Path $WorkCodeRoot $ProjectName
    $projectPath = Join-Path $projectDirectory "$ProjectName.vcxproj"
    Assert-File $projectPath "$ProjectName vcxproj"

    [xml]$projectXml = [IO.File]::ReadAllText($projectPath)
    $namespace = New-Object Xml.XmlNamespaceManager($projectXml.NameTable)
    $namespace.AddNamespace("msb", "http://schemas.microsoft.com/developer/msbuild/2003")
    $compileNodes = @($projectXml.SelectNodes("//msb:ClCompile[@Include]", $namespace))
    if ($compileNodes.Count -eq 0) {
        throw "$ProjectName vcxproj has no ClCompile entries: $projectPath"
    }

    $sources = New-Object Collections.Generic.List[object]
    $excluded = New-Object Collections.Generic.List[object]
    foreach ($node in $compileNodes) {
        $include = [string]$node.Include
        if ($include.IndexOfAny([char[]]@("`0", "`r", "`n", '"', '*', '?')) -ge 0) {
            throw "$ProjectName contains an unsafe ClCompile path: $include"
        }
        if (-not [IO.Path]::GetExtension($include).Equals(".cpp", [StringComparison]::OrdinalIgnoreCase)) {
            throw "$ProjectName contains a non-.cpp ClCompile entry: $include"
        }

        $candidate = Get-FullPath (Join-Path $projectDirectory $include)
        if (-not (Test-PathWithin $candidate $WorkCodeRoot)) {
            throw "$ProjectName source escapes the copied Code root: $include"
        }
        $relative = Get-RelativePath $WorkCodeRoot $candidate

        if ($ProjectName -eq "TMSrv" -and
            $relative.Equals("TMSrv\DialogConfigExtra.cpp", [StringComparison]::OrdinalIgnoreCase)) {
            $excluded.Add([ordered]@{
                path = $relative
                reason = "dead MFC dialog; its include and all uses in TMSrv Server.cpp are commented"
            })
            continue
        }

        if (-not (Test-Path -LiteralPath $candidate -PathType Leaf)) {
            if ($ProjectName -eq "TMSrv" -and
                $relative.Equals("TMSrv\Nyerds.cpp", [StringComparison]::OrdinalIgnoreCase)) {
                $excluded.Add([ordered]@{
                    path = $relative
                    reason = "stale vcxproj entry; file is absent from the supplied source"
                })
                continue
            }
            throw "$ProjectName source listed by vcxproj is missing: $relative"
        }

        $sources.Add([ordered]@{
            path = $relative
            fullPath = $candidate
            sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $candidate).Hash
        })
    }

    $collisions = @(
        $sources |
            Group-Object { [IO.Path]::GetFileNameWithoutExtension($_.path).ToLowerInvariant() } |
            Where-Object Count -gt 1
    )
    if ($collisions.Count -ne 0) {
        throw "$ProjectName has object basename collisions: $($collisions[0].Group.path -join ', ')"
    }

    $expectedSourceCount = if ($ProjectName -eq "DBSrv") { 9 } else { 127 }
    if ($sources.Count -ne $expectedSourceCount) {
        throw "$ProjectName source manifest changed: expected $expectedSourceCount .cpp files, got $($sources.Count)."
    }

    $resourceNodes = @($projectXml.SelectNodes("//msb:ResourceCompile[@Include]", $namespace))
    $excludedResources = New-Object Collections.Generic.List[object]
    foreach ($resourceNode in $resourceNodes) {
        $resourceInclude = [string]$resourceNode.Include
        if ($resourceInclude.IndexOfAny([char[]]@("`0", "`r", "`n", '"', '*', '?')) -ge 0) {
            throw "$ProjectName contains an unsafe ResourceCompile path: $resourceInclude"
        }
        $resourcePath = Get-FullPath (Join-Path $projectDirectory $resourceInclude)
        if (-not (Test-PathWithin $resourcePath $WorkCodeRoot)) {
            throw "$ProjectName resource escapes the copied Code root: $resourceInclude"
        }
        $excludedResources.Add([ordered]@{
            path = Get-RelativePath $WorkCodeRoot $resourcePath
            reason = "GUI icon/menu/resource data is not required by the headless comparison server runtime"
        })
    }

    return [ordered]@{
        project = $projectPath
        sources = $sources.ToArray()
        excluded = $excluded.ToArray()
        excludedResources = $excludedResources.ToArray()
    }
}

function Set-RuntimeLoopbackConfiguration {
    param(
        [Parameter(Mandatory = $true)]
        [string]$RuntimeServerRoot
    )

    $relativeFiles = @(
        "Common\Settings\config.json",
        "Common\serverlist.txt",
        "DBSrv\run\Config\config.json",
        "DBSrv\run\admin.txt",
        "DBSrv\run\localip.txt",
        "DBSrv\run\redirect.sample.txt",
        "DBSrv\run\serverlist.txt",
        "TMSrv\run\Config\config.json",
        "TMSrv\run\admin.txt",
        "TMSrv\run\localip.txt",
        "TMSrv\run\serverlist.txt"
    )
    $windows1252 = [Text.Encoding]::GetEncoding(1252)
    $results = New-Object Collections.Generic.List[object]
    foreach ($relative in $relativeFiles) {
        $path = Join-Path $RuntimeServerRoot $relative
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
            $results.Add([ordered]@{
                file = $relative
                result = "not-present"
                replacements = 0
            })
            continue
        }

        $text = [IO.File]::ReadAllText($path, $windows1252)
        $matches = [Text.RegularExpressions.Regex]::Matches(
            $text,
            "(?<![0-9])(?:[0-9]{1,3}\.){3}[0-9]{1,3}(?![0-9])"
        )
        $rewritten = [Text.RegularExpressions.Regex]::Replace(
            $text,
            "(?<![0-9])(?:[0-9]{1,3}\.){3}[0-9]{1,3}(?![0-9])",
            "127.0.0.1"
        )
        [IO.File]::WriteAllText($path, $rewritten, $windows1252)
        $results.Add([ordered]@{
            file = $relative
            result = "loopback"
            replacements = $matches.Count
        })
    }

    foreach ($required in @(
        "DBSrv\run\serverlist.txt",
        "DBSrv\run\localip.txt",
        "TMSrv\run\serverlist.txt",
        "TMSrv\run\localip.txt"
    )) {
        $path = Join-Path $RuntimeServerRoot $required
        Assert-File $path "required loopback runtime configuration"
        $text = [IO.File]::ReadAllText($path, $windows1252)
        if ($text.IndexOf("127.0.0.1", [StringComparison]::Ordinal) -lt 0) {
            throw "Runtime configuration was not rewritten to loopback: $path"
        }
    }

    return $results.ToArray()
}

function Get-FileInventory {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Root
    )

    return @(
        Get-ChildItem -LiteralPath $Root -File -Recurse -Force |
            Sort-Object FullName |
            ForEach-Object {
                [ordered]@{
                    path = Get-RelativePath $Root $_.FullName
                    size = $_.Length
                    sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $_.FullName).Hash
                }
            }
    )
}

function Get-StringSha256 {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Value
    )

    $sha = [Security.Cryptography.SHA256]::Create()
    try {
        $bytes = [Text.Encoding]::UTF8.GetBytes($Value)
        return ([BitConverter]::ToString($sha.ComputeHash($bytes))).Replace("-", "")
    }
    finally {
        $sha.Dispose()
    }
}

function Get-CppFunctionDefinition {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Text,

        [Parameter(Mandatory = $true)]
        [string]$Signature,

        [Parameter(Mandatory = $true)]
        [string]$SourceDescription
    )

    $start = $Text.IndexOf($Signature, [StringComparison]::Ordinal)
    if ($start -lt 0 -or
        $Text.IndexOf($Signature, $start + $Signature.Length, [StringComparison]::Ordinal) -ge 0) {
        throw "Function signature is missing or ambiguous in ${SourceDescription}: $Signature"
    }
    $brace = $Text.IndexOf("{", $start, [StringComparison]::Ordinal)
    if ($brace -lt 0) {
        throw "Function body is missing in ${SourceDescription}: $Signature"
    }

    $depth = 0
    $state = "code"
    $escaped = $false
    for ($index = $brace; $index -lt $Text.Length; $index++) {
        $character = $Text[$index]
        $next = if ($index + 1 -lt $Text.Length) { $Text[$index + 1] } else { [char]0 }

        if ($state -eq "line-comment") {
            if ($character -eq "`n") {
                $state = "code"
            }
            continue
        }
        if ($state -eq "block-comment") {
            if ($character -eq "*" -and $next -eq "/") {
                $state = "code"
                $index++
            }
            continue
        }
        if ($state -eq "string") {
            if ($escaped) {
                $escaped = $false
            }
            elseif ($character -eq "\") {
                $escaped = $true
            }
            elseif ($character -eq '"') {
                $state = "code"
            }
            continue
        }
        if ($state -eq "character") {
            if ($escaped) {
                $escaped = $false
            }
            elseif ($character -eq "\") {
                $escaped = $true
            }
            elseif ($character -eq "'") {
                $state = "code"
            }
            continue
        }

        if ($character -eq "/" -and $next -eq "/") {
            $state = "line-comment"
            $index++
            continue
        }
        if ($character -eq "/" -and $next -eq "*") {
            $state = "block-comment"
            $index++
            continue
        }
        if ($character -eq '"') {
            $state = "string"
            continue
        }
        if ($character -eq "'") {
            $state = "character"
            continue
        }
        if ($character -eq "{") {
            $depth++
            continue
        }
        if ($character -eq "}") {
            $depth--
            if ($depth -eq 0) {
                return $Text.Substring($start, $index - $start + 1)
            }
        }
    }

    throw "Unterminated function body in ${SourceDescription}: $Signature"
}

function Get-CppDataDefinition {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Text,

        [Parameter(Mandatory = $true)]
        [string]$Marker,

        [Parameter(Mandatory = $true)]
        [string]$Terminator,

        [Parameter(Mandatory = $true)]
        [string]$SourceDescription
    )

    $start = $Text.IndexOf($Marker, [StringComparison]::Ordinal)
    if ($start -lt 0 -or
        $Text.IndexOf($Marker, $start + $Marker.Length, [StringComparison]::Ordinal) -ge 0) {
        throw "Data definition is missing or ambiguous in ${SourceDescription}: $Marker"
    }
    $end = $Text.IndexOf($Terminator, $start, [StringComparison]::Ordinal)
    if ($end -lt 0) {
        throw "Data definition terminator is missing in ${SourceDescription}: $Marker"
    }
    return $Text.Substring($start, $end - $start + $Terminator.Length)
}

function Write-OfficialAccountPrimitivesSource {
    param(
        [Parameter(Mandatory = $true)]
        [string]$WorkCodeRoot,

        [Parameter(Mandatory = $true)]
        [string]$Path
    )

    $windows1252 = [Text.Encoding]::GetEncoding(1252)
    $basedefPath = Join-Path $WorkCodeRoot "Basedef.cpp"
    $cFileDbPath = Join-Path $WorkCodeRoot "DBSrv\CFileDB.cpp"
    $basedefText = [IO.File]::ReadAllText($basedefPath, $windows1252)
    $cFileDbText = [IO.File]::ReadAllText($cFileDbPath, $windows1252)
    $definitions = @(
        (Get-CppDataDefinition $basedefText "unsigned char KorFirst[36]" "};" "Basedef.cpp"),
        (Get-CppDataDefinition $basedefText "int KorIndex[19]" ";" "Basedef.cpp"),
        (Get-CppFunctionDefinition $basedefText "void BASE_GetFirstKey(" "Basedef.cpp"),
        (Get-CppFunctionDefinition $cFileDbText "CFileDB::CFileDB()" "DBSrv\CFileDB.cpp"),
        (Get-CppFunctionDefinition $cFileDbText "CFileDB::~CFileDB()" "DBSrv\CFileDB.cpp"),
        (Get-CppFunctionDefinition $cFileDbText "int CFileDB::AddAccount(" "DBSrv\CFileDB.cpp"),
        (Get-CppFunctionDefinition $cFileDbText "int CFileDB::DBWriteAccount(" "DBSrv\CFileDB.cpp"),
        (Get-CppFunctionDefinition $cFileDbText "int CFileDB::DBReadAccount(" "DBSrv\CFileDB.cpp")
    )
    $header = @'
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <errno.h>
#include <fcntl.h>
#include <io.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "DBSrv/CFileDB.h"

void Log(char* first, char* second, unsigned int ip);

'@
    $generated = $header + ($definitions -join "`r`n`r`n") + "`r`n"
    [IO.File]::WriteAllText($Path, $generated, $windows1252)
    return [ordered]@{
        generatedSource = $Path
        generatedSha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $Path).Hash
        originalSources = @(
            [ordered]@{
                path = $basedefPath
                sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $basedefPath).Hash
                extracted = @("KorFirst", "KorIndex", "BASE_GetFirstKey")
            },
            [ordered]@{
                path = $cFileDbPath
                sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $cFileDbPath).Hash
                extracted = @(
                    "CFileDB::CFileDB",
                    "CFileDB::~CFileDB",
                    "CFileDB::AddAccount",
                    "CFileDB::DBWriteAccount",
                    "CFileDB::DBReadAccount"
                )
            }
        )
    }
}

function Write-AccountProvisionerSource {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path
    )

    $source = @'
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <direct.h>
#include <errno.h>
#include <io.h>
#include <new>
#include <stdio.h>
#include <string.h>

#include "DBSrv/CFileDB.h"

void Log(char* first, char* second, unsigned int ip)
{
    fprintf(stderr, "CFileDB: %s | %s | %u\n",
        first ? first : "", second ? second : "", ip);
}

static bool BuildAccountPath(const char* account, char* output, size_t outputSize)
{
    char upper[ACCOUNTNAME_LENGTH] = {0};
    char first[128] = {0};
    strncpy(upper, account, ACCOUNTNAME_LENGTH - 1);
    _strupr(upper);
    BASE_GetFirstKey(upper, first);
    return _snprintf(output, outputSize, "./account/%s/%s", first, upper) > 0;
}

static bool EnsureAccountDirectory(const char* account)
{
    char upper[ACCOUNTNAME_LENGTH] = {0};
    char first[128] = {0};
    char directory[256] = {0};
    strncpy(upper, account, ACCOUNTNAME_LENGTH - 1);
    _strupr(upper);
    BASE_GetFirstKey(upper, first);
    if (_mkdir("./account") != 0 && errno != EEXIST)
        return false;
    _snprintf(directory, sizeof(directory), "./account/%s", first);
    return _mkdir(directory) == 0 || errno == EEXIST;
}

int main(int argc, char** argv)
{
    if (argc != 4)
    {
        fprintf(stderr, "usage: provision_compare_accounts ACCOUNT1 ACCOUNT2 PASSWORD\n");
        return 2;
    }

    char firstPath[256] = {0};
    char secondPath[256] = {0};
    if (!BuildAccountPath(argv[1], firstPath, sizeof(firstPath)) ||
        !BuildAccountPath(argv[2], secondPath, sizeof(secondPath)) ||
        !EnsureAccountDirectory(argv[1]) ||
        !EnsureAccountDirectory(argv[2]))
    {
        fprintf(stderr, "could not prepare official account directories\n");
        return 3;
    }
    if (_access(firstPath, 0) == 0 || _access(secondPath, 0) == 0)
    {
        fprintf(stderr, "refusing to overwrite an existing comparison account\n");
        return 4;
    }

    CFileDB* database = new (std::nothrow) CFileDB();
    if (!database)
        return 5;

    char realName[] = "OpenWyd Compare";
    char email[] = "compare@invalid";
    char telephone[] = "local";
    char address[] = "local";
    int firstCreated = database->AddAccount(
        argv[1], argv[3], realName, 0, 0, email, telephone, address, 0);
    int secondCreated = database->AddAccount(
        argv[2], argv[3], realName, 0, 0, email, telephone, address, 0);
    if (!firstCreated || !secondCreated)
    {
        fprintf(stderr, "CFileDB::AddAccount failed (%d, %d)\n",
            firstCreated, secondCreated);
        delete database;
        return 6;
    }

    STRUCT_ACCOUNTFILE first = {};
    STRUCT_ACCOUNTFILE second = {};
    strncpy(first.Info.AccountName, argv[1], ACCOUNTNAME_LENGTH - 1);
    strncpy(second.Info.AccountName, argv[2], ACCOUNTNAME_LENGTH - 1);
    if (!database->DBReadAccount(&first) || !database->DBReadAccount(&second))
    {
        fprintf(stderr, "CFileDB::DBReadAccount validation failed\n");
        delete database;
        return 7;
    }
    if (_stricmp(first.Info.AccountName, argv[1]) != 0 ||
        _stricmp(second.Info.AccountName, argv[2]) != 0)
    {
        fprintf(stderr, "account identity validation failed\n");
        delete database;
        return 8;
    }

    memset(first.Info.AccountName, 0, sizeof(first.Info.AccountName));
    memset(second.Info.AccountName, 0, sizeof(second.Info.AccountName));
    bool equivalent = memcmp(&first, &second, sizeof(STRUCT_ACCOUNTFILE)) == 0;
    printf("{\"equivalent\":%s,\"accountFileSize\":%u}\n",
        equivalent ? "true" : "false",
        (unsigned int)sizeof(STRUCT_ACCOUNTFILE));
    delete database;
    return equivalent ? 0 : 9;
}
'@
    [IO.File]::WriteAllText($Path, $source, (New-Object Text.UTF8Encoding($false)))
}

function Assert-ServerStopped {
    $processes = @(Get-Process -Name "DBSrv", "TMSrv" -ErrorAction SilentlyContinue)
    if ($processes.Count -ne 0) {
        throw "Stop DBSrv and TMSrv before restoring the baseline snapshot."
    }

    $ports = @(
        [Net.NetworkInformation.IPGlobalProperties]::GetIPGlobalProperties().GetActiveTcpListeners() |
            Where-Object { $_.Port -in @(7514, 8281, 8895) }
    )
    if ($ports.Count -ne 0) {
        throw "Server port is still listening; refusing snapshot restore: $($ports.Port -join ', ')"
    }
}

if ([string]::IsNullOrWhiteSpace($RepoRoot)) {
    $RepoRoot = Split-Path -Parent $PSScriptRoot
}
$RepoRoot = Get-FullPath $RepoRoot
Assert-NotFileSystemRoot $RepoRoot "repository root"
Assert-Directory $RepoRoot "repository root"
Assert-Directory (Join-Path $RepoRoot ".git") "repository .git directory"

$artifactsRoot = Get-FullPath (Join-Path $RepoRoot "artifacts")
if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
    $OutputRoot = Join-Path $artifactsRoot "server-stack\source-build"
}
$OutputRoot = Get-FullPath $OutputRoot $RepoRoot
Assert-NotFileSystemRoot $OutputRoot "output root"
if (-not (Test-PathWithin $OutputRoot $artifactsRoot) -or
    $OutputRoot.Equals($artifactsRoot, [StringComparison]::OrdinalIgnoreCase)) {
    throw "OutputRoot must be a strict descendant of the repository artifacts directory: $artifactsRoot"
}
foreach ($reservedArtifactRoot in @(
    (Join-Path $artifactsRoot "server-stack\input"),
    (Join-Path $artifactsRoot "server-stack\reference"),
    (Join-Path $artifactsRoot "server-stack\snapshots"),
    (Join-Path $artifactsRoot "server-stack\work\Source")
)) {
    if (Test-PathWithin $OutputRoot $reservedArtifactRoot) {
        throw "OutputRoot is inside a reserved source/reference/data area: $reservedArtifactRoot"
    }
}

$sentinelPath = Join-Path $OutputRoot ".openwyd-server-build-root.json"
if ($RestoreBaseline) {
    Assert-Directory $OutputRoot "server build output"
    Assert-File $sentinelPath "server build safety sentinel"
    $sentinel = Get-Content -LiteralPath $sentinelPath -Raw | ConvertFrom-Json
    if ($sentinel.schema -ne "openwyd-server-source-build-root-v1" -or
        -not ([string]$sentinel.outputRoot).Equals($OutputRoot, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Output safety sentinel does not own this exact directory: $sentinelPath"
    }

    Assert-ServerStopped
    $runtimeStateRoot = Join-Path $OutputRoot "runtime\Server\DBSrv\run"
    $snapshotStateRoot = Join-Path $OutputRoot "snapshots\baseline\DBSrv\run"
    Assert-Directory $runtimeStateRoot "DBSrv runtime"
    Assert-Directory $snapshotStateRoot "baseline snapshot"

    $backupRoot = Join-Path $OutputRoot (
        "restore-backups\" +
        [DateTime]::UtcNow.ToString("yyyyMMddTHHmmssfffZ") +
        "-" +
        [Guid]::NewGuid().ToString("N")
    )
    New-Item -ItemType Directory -Force -Path $backupRoot | Out-Null
    foreach ($stateName in @("account", "char", "capsule")) {
        $current = Join-Path $runtimeStateRoot $stateName
        $baseline = Join-Path $snapshotStateRoot $stateName
        Assert-Directory $baseline "baseline $stateName directory"
        if (Test-Path -LiteralPath $current) {
            Move-Item -LiteralPath $current -Destination (Join-Path $backupRoot $stateName)
        }
        Copy-Item -LiteralPath $baseline -Destination $current -Recurse
    }

    $restoreReport = [ordered]@{
        schemaVersion = 1
        restoredUtc = [DateTime]::UtcNow.ToString("o")
        outputRoot = $OutputRoot
        snapshot = $snapshotStateRoot
        previousStateBackup = $backupRoot
        restoredDirectories = @("account", "char", "capsule")
        restoredFiles = Get-FileInventory $runtimeStateRoot |
            Where-Object { $_.path -match "^(account|char|capsule)\\" }
    }
    $restoreReportPath = Join-Path $OutputRoot "manifests\last-restore.json"
    Write-JsonFile $restoreReportPath $restoreReport
    Write-Host "Baseline restored. Previous state backup: $backupRoot"
    return
}

if ([string]::IsNullOrWhiteSpace($ServerSourceRoot)) {
    $ServerSourceRoot = Join-Path $artifactsRoot "server-stack\input"
}
$ServerSourceRoot = Get-FullPath $ServerSourceRoot $RepoRoot
Assert-NotFileSystemRoot $ServerSourceRoot "server source root"
if ($ServerSourceRoot.Equals($RepoRoot, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing the repository root as ServerSourceRoot."
}
if ((Test-PathWithin $ServerSourceRoot $RepoRoot) -and
    -not (Test-PathWithin $ServerSourceRoot $artifactsRoot)) {
    throw "ServerSourceRoot inside the repository must be below ignored artifacts/: $ServerSourceRoot"
}

$bundleSourceCandidate = Join-Path $ServerSourceRoot "Source"
$directSourceCandidate = $ServerSourceRoot
if (Test-Path -LiteralPath (Join-Path $bundleSourceCandidate "Code\DBSrv\DBSrv.vcxproj") -PathType Leaf) {
    $resolvedSourceRoot = Get-FullPath $bundleSourceCandidate
    if ([string]::IsNullOrWhiteSpace($RuntimeDataRoot)) {
        $RuntimeDataRoot = Join-Path $ServerSourceRoot "Server"
    }
}
elseif (Test-Path -LiteralPath (Join-Path $directSourceCandidate "Code\DBSrv\DBSrv.vcxproj") -PathType Leaf) {
    $resolvedSourceRoot = Get-FullPath $directSourceCandidate
    if ([string]::IsNullOrWhiteSpace($RuntimeDataRoot)) {
        $RuntimeDataRoot = Join-Path (Split-Path -Parent $resolvedSourceRoot) "Server"
    }
}
else {
    throw "ServerSourceRoot does not contain Source\Code or Code with DBSrv.vcxproj: $ServerSourceRoot"
}

$RuntimeDataRoot = Get-FullPath $RuntimeDataRoot $RepoRoot
Assert-NotFileSystemRoot $resolvedSourceRoot "resolved source root"
Assert-NotFileSystemRoot $RuntimeDataRoot "runtime data root"
if ($resolvedSourceRoot.Equals($RepoRoot, [StringComparison]::OrdinalIgnoreCase) -or
    $RuntimeDataRoot.Equals($RepoRoot, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing the repository root as a source or runtime data root."
}
if ((Test-PathWithin $RuntimeDataRoot $RepoRoot) -and
    -not (Test-PathWithin $RuntimeDataRoot $artifactsRoot) -and
    -not $RuntimeDataRoot.Equals((Join-Path $RepoRoot "Servidor\Server"), [StringComparison]::OrdinalIgnoreCase)) {
    throw "RuntimeDataRoot inside the repository must be tracked Servidor\Server or below ignored artifacts/."
}
if ((Test-PathWithin $OutputRoot $ServerSourceRoot) -or
    (Test-PathWithin $ServerSourceRoot $OutputRoot) -or
    (Test-PathWithin $OutputRoot $resolvedSourceRoot) -or
    (Test-PathWithin $resolvedSourceRoot $OutputRoot) -or
    (Test-PathWithin $OutputRoot $RuntimeDataRoot) -or
    (Test-PathWithin $RuntimeDataRoot $OutputRoot)) {
    throw "OutputRoot must not overlap ServerSourceRoot or RuntimeDataRoot."
}

$sourceCodeRoot = Join-Path $resolvedSourceRoot "Code"
Assert-File (Join-Path $sourceCodeRoot "DBSrv\DBSrv.vcxproj") "DBSrv vcxproj"
Assert-File (Join-Path $sourceCodeRoot "TMSrv\TMSrv.vcxproj") "TMSrv vcxproj"
foreach ($requiredRuntimeDirectory in @("Common", "DBSrv\run", "TMSrv\run")) {
    Assert-Directory (Join-Path $RuntimeDataRoot $requiredRuntimeDirectory) "runtime data directory"
}

Assert-NoReparsePoints $sourceCodeRoot "server source"
Assert-NoReparsePoints $RuntimeDataRoot "runtime data"

foreach ($accountName in @($TestAccountOne, $TestAccountTwo)) {
    if ($accountName.Length -lt 2 -or $accountName.Length -ge 16 -or
        $accountName -notmatch "^[A-Za-z][A-Za-z0-9]+$") {
        throw "Test account names must be 2-15 ASCII alphanumeric characters beginning with a letter."
    }
}
if ($TestAccountOne.Equals($TestAccountTwo, [StringComparison]::OrdinalIgnoreCase)) {
    throw "The two test account names must be distinct."
}
if ($TestAccountPassword.Length -lt 1 -or $TestAccountPassword.Length -ge 12 -or
    $TestAccountPassword -notmatch "^[!-~]+$") {
    throw "TestAccountPassword must be 1-11 printable ASCII characters and must not be a real credential."
}
$TestAccountOne = $TestAccountOne.ToUpperInvariant()
$TestAccountTwo = $TestAccountTwo.ToUpperInvariant()

if ($ValidateOnly) {
    [ordered]@{
        validation = "succeeded"
        serverSourceRoot = $ServerSourceRoot
        resolvedSourceRoot = $resolvedSourceRoot
        runtimeDataRoot = $RuntimeDataRoot
        outputRoot = $OutputRoot
    } | ConvertTo-Json
    return
}

if (Test-Path -LiteralPath $OutputRoot) {
    $existingChildren = @(Get-ChildItem -LiteralPath $OutputRoot -Force)
    if ($existingChildren.Count -ne 0) {
        if (-not $Clean) {
            throw "OutputRoot is not empty. Use -Clean only for an output owned by this tool: $OutputRoot"
        }
        Assert-File $sentinelPath "server build safety sentinel"
        $existingSentinel = Get-Content -LiteralPath $sentinelPath -Raw | ConvertFrom-Json
        if ($existingSentinel.schema -ne "openwyd-server-source-build-root-v1" -or
            -not ([string]$existingSentinel.outputRoot).Equals($OutputRoot, [StringComparison]::OrdinalIgnoreCase)) {
            throw "Refusing to clean an output without a matching safety sentinel: $OutputRoot"
        }

        $rebuildBackupRoot = Join-Path $OutputRoot (
            "rebuild-backups\" +
            [DateTime]::UtcNow.ToString("yyyyMMddTHHmmssfffZ") +
            "-" +
            [Guid]::NewGuid().ToString("N")
        )
        New-Item -ItemType Directory -Force -Path $rebuildBackupRoot | Out-Null
        foreach ($childName in @("work", "build", "runtime", "snapshots", "logs", "manifests")) {
            $childPath = Join-Path $OutputRoot $childName
            if (-not (Test-PathWithin $childPath $OutputRoot)) {
                throw "Internal output path escaped OutputRoot: $childPath"
            }
            if (Test-Path -LiteralPath $childPath) {
                Move-Item -LiteralPath $childPath -Destination (Join-Path $rebuildBackupRoot $childName)
            }
        }
    }
}
else {
    New-Item -ItemType Directory -Path $OutputRoot | Out-Null
}

$workRoot = Join-Path $OutputRoot "work"
$workSourceRoot = Join-Path $workRoot "Source"
$workCodeRoot = Join-Path $workSourceRoot "Code"
$buildRoot = Join-Path $OutputRoot "build"
$runtimeServerRoot = Join-Path $OutputRoot "runtime\Server"
$snapshotRoot = Join-Path $OutputRoot "snapshots\baseline\DBSrv\run"
$logsRoot = Join-Path $OutputRoot "logs"
$manifestsRoot = Join-Path $OutputRoot "manifests"
New-Item -ItemType Directory -Force -Path @(
    $workCodeRoot,
    $buildRoot,
    $runtimeServerRoot,
    $snapshotRoot,
    $logsRoot,
    $manifestsRoot
) | Out-Null

$sentinelValue = [ordered]@{
    schema = "openwyd-server-source-build-root-v1"
    outputRoot = $OutputRoot
    createdUtc = [DateTime]::UtcNow.ToString("o")
}
Write-JsonFile $sentinelPath $sentinelValue

$metadataPath = Join-Path $manifestsRoot "build-metadata.json"
$metadata = [ordered]@{
    schemaVersion = 1
    status = "running"
    startedUtc = [DateTime]::UtcNow.ToString("o")
    completedUtc = $null
    error = $null
    inputs = [ordered]@{
        serverSourceRoot = $ServerSourceRoot
        resolvedSourceRoot = $resolvedSourceRoot
        runtimeDataRoot = $RuntimeDataRoot
    }
    outputRoot = $OutputRoot
    configuration = "Debug"
    platform = "Win32"
    openWydCompare = $true
    projectSelection = $Project
    sourceCopy = $null
    sourcePatches = @()
    projects = @()
    runtime = $null
    snapshot = $null
    toolchain = $null
    testAccounts = $null
}
Write-JsonFile $metadataPath $metadata

try {
    $sourceAllowedExtensions = @(
        ".cpp",
        ".h",
        ".hpp",
        ".inl",
        ".vcxproj",
        ".filters",
        ".props",
        ".targets"
    )
    $compiledExtensions = @(
        ".exe",
        ".dll",
        ".lib",
        ".obj",
        ".pdb",
        ".ilk",
        ".exp",
        ".map",
        ".sys",
        ".com",
        ".scr"
    )

    $sourceCopy = Copy-TreeFiles $sourceCodeRoot $workCodeRoot {
        param($file, $relative)
        $extension = $file.Extension.ToLowerInvariant()
        if ($sourceAllowedExtensions -contains $extension) {
            return [ordered]@{ Include = $true; Reason = ""; Hash = $false }
        }
        $hashExcluded = $compiledExtensions -contains $extension
        return [ordered]@{
            Include = $false
            Reason = if ($hashExcluded) { "compiled artifact excluded" } else { "not in text source allowlist" }
            Hash = $hashExcluded
        }
    }
    $metadata.sourceCopy = [ordered]@{
        copiedCount = @($sourceCopy.copied).Count
        excludedCount = @($sourceCopy.excluded).Count
        copied = $sourceCopy.copied
        excluded = $sourceCopy.excluded
    }
    Write-JsonFile (Join-Path $manifestsRoot "source-inventory.json") $metadata.sourceCopy

    $workCompiledFiles = @(
        Get-ChildItem -LiteralPath $workCodeRoot -File -Recurse |
            Where-Object { $compiledExtensions -contains $_.Extension.ToLowerInvariant() }
    )
    if ($workCompiledFiles.Count -ne 0) {
        throw "Compiled file reached the working source copy: $($workCompiledFiles[0].FullName)"
    }

    $metadata.sourcePatches = @(Apply-CompareSourcePatches $workCodeRoot)
    $projectNames = if ($Project -eq "All") { @("DBSrv", "TMSrv") } else { @($Project) }
    $projectPlans = @{}
    foreach ($projectName in $projectNames) {
        $projectPlans[$projectName] = Get-ProjectCppSources $projectName $workCodeRoot
    }

    if ([string]::IsNullOrWhiteSpace($ToolsRoot)) {
        $ToolsRoot = Join-Path (Split-Path -Parent $RepoRoot) ".tools"
    }
    $ToolsRoot = Get-FullPath $ToolsRoot
    $portableMsvcRoot = Join-Path $ToolsRoot "portable-msvc-v142-x86\msvc"
    $msvcVersionsRoot = Join-Path $portableMsvcRoot "VC\Tools\MSVC"
    $windowsKitRoot = Join-Path $portableMsvcRoot "Windows Kits\10"
    Assert-Directory $msvcVersionsRoot "portable MSVC versions directory"
    Assert-Directory $windowsKitRoot "portable Windows SDK"

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
    $dumpbin = Join-Path $msvcToolsRoot "bin\Hostx64\x86\dumpbin.exe"
    Assert-File $compiler "MSVC v142 x86 compiler"
    Assert-File $linker "MSVC v142 x86 linker"
    Assert-File $dumpbin "MSVC dumpbin"

    $msvcInclude = Join-Path $msvcToolsRoot "include"
    $sdkIncludeRoot = Join-Path $windowsKitRoot "Include\$sdkVersion"
    $vcLibraryPath = Join-Path $msvcToolsRoot "lib\x86"
    $sdkUcrtLibraryPath = Join-Path $windowsKitRoot "Lib\$sdkVersion\ucrt\x86"
    $sdkUmLibraryPath = Join-Path $windowsKitRoot "Lib\$sdkVersion\um\x86"
    $compilerBin = Split-Path -Parent $compiler
    $sdkBin = Join-Path $windowsKitRoot "bin\$sdkVersion\x64"
    foreach ($requiredDirectory in @(
        $msvcInclude,
        $sdkIncludeRoot,
        $vcLibraryPath,
        $sdkUcrtLibraryPath,
        $sdkUmLibraryPath,
        $compilerBin,
        $sdkBin
    )) {
        Assert-Directory $requiredDirectory "required toolchain directory"
    }

    $metadata.toolchain = [ordered]@{
        compiler = $compiler
        compilerVersion = (Get-Item -LiteralPath $compiler).VersionInfo.FileVersion
        compilerSha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $compiler).Hash
        linker = $linker
        linkerVersion = (Get-Item -LiteralPath $linker).VersionInfo.FileVersion
        linkerSha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $linker).Hash
        dumpbin = $dumpbin
        msvcVersion = $msvcVersion
        windowsSdkVersion = $sdkVersion
        recoveredLibrariesUsed = @()
    }

    $environmentKeys = @("PATH", "INCLUDE", "LIB")
    $savedEnvironment = @{}
    foreach ($key in $environmentKeys) {
        $savedEnvironment[$key] = [Environment]::GetEnvironmentVariable($key, "Process")
    }

    $builtProjects = New-Object Collections.Generic.List[object]
    $accountProvisioner = $null
    try {
        $includePath = @(
            $msvcInclude,
            (Join-Path $sdkIncludeRoot "ucrt"),
            (Join-Path $sdkIncludeRoot "shared"),
            (Join-Path $sdkIncludeRoot "um"),
            (Join-Path $sdkIncludeRoot "winrt"),
            (Join-Path $sdkIncludeRoot "cppwinrt")
        ) -join ";"
        $libraryPath = @($vcLibraryPath, $sdkUcrtLibraryPath, $sdkUmLibraryPath) -join ";"
        [Environment]::SetEnvironmentVariable(
            "PATH",
            $compilerBin + ";" + $sdkBin + ";" + $savedEnvironment["PATH"],
            "Process"
        )
        [Environment]::SetEnvironmentVariable("INCLUDE", $includePath, "Process")
        [Environment]::SetEnvironmentVariable("LIB", $libraryPath, "Process")

        $systemLibraries = @(
            "winmm.lib",
            "ws2_32.lib",
            "kernel32.lib",
            "user32.lib",
            "gdi32.lib"
        )
        $allowedDependencies = @(
            "winmm.dll",
            "ws2_32.dll",
            "kernel32.dll",
            "user32.dll",
            "gdi32.dll"
        )

        foreach ($projectName in $projectNames) {
            $plan = $projectPlans[$projectName]
            $projectBuildRoot = Join-Path $buildRoot $projectName
            $objectRoot = Join-Path $projectBuildRoot "obj"
            New-Item -ItemType Directory -Force -Path $projectBuildRoot, $objectRoot | Out-Null

            $compileArguments = @(
                "/nologo",
                "/EHsc",
                "/W0",
                "/Od",
                "/Zi",
                "/MTd",
                "/Gy",
                "/FS",
                "/MP",
                "/c",
                "/D_CRT_SECURE_NO_WARNINGS",
                "/D_PACKET_DEBUG",
                "/DOPENWYD_COMPARE=1",
                "/DWIN32",
                "/D_WINDOWS",
                "/D_DEBUG",
                "/I`"$workCodeRoot`"",
                "/I`"$(Join-Path $workCodeRoot 'jsonlib')`"",
                "/Fo$objectRoot\",
                "/Fd`"$(Join-Path $projectBuildRoot "$projectName-compile.pdb")`""
            )
            $compileArguments += @($plan.sources | ForEach-Object { "`"$($_.fullPath)`"" })
            $compileResponse = Join-Path $projectBuildRoot "$projectName-cl.rsp"
            [IO.File]::WriteAllLines($compileResponse, $compileArguments, [Text.Encoding]::Unicode)
            $compileLog = Join-Path $logsRoot "$projectName-compile.log"

            Write-Host "Compiling $projectName from $(@($plan.sources).Count) .cpp files"
            $savedPreference = $ErrorActionPreference
            $ErrorActionPreference = "Continue"
            & $compiler "@$compileResponse" 2>&1 | Tee-Object -FilePath $compileLog
            $compileExitCode = $LASTEXITCODE
            $ErrorActionPreference = $savedPreference
            if ($compileExitCode -ne 0) {
                throw "$projectName compilation failed with exit code $compileExitCode. See $compileLog"
            }

            $objectFiles = @(
                $plan.sources | ForEach-Object {
                    Join-Path $objectRoot "$([IO.Path]::GetFileNameWithoutExtension($_.path)).obj"
                }
            )
            $missingObjects = @($objectFiles | Where-Object { -not (Test-Path -LiteralPath $_ -PathType Leaf) })
            if ($missingObjects.Count -ne 0) {
                throw "$projectName did not produce all expected objects: $($missingObjects -join ', ')"
            }

            $outputExe = Join-Path $projectBuildRoot "$projectName.exe"
            $linkArguments = @(
                "/NOLOGO",
                "/DEBUG",
                "/INCREMENTAL:NO",
                "/VERBOSE:LIB",
                "/SUBSYSTEM:WINDOWS",
                "/MACHINE:X86",
                "/OUT:`"$outputExe`"",
                "/PDB:`"$(Join-Path $projectBuildRoot "$projectName.pdb")`""
            )
            $linkArguments += @($objectFiles | ForEach-Object { "`"$_`"" })
            $linkArguments += $systemLibraries
            $linkResponse = Join-Path $projectBuildRoot "$projectName-link.rsp"
            [IO.File]::WriteAllLines($linkResponse, $linkArguments, [Text.Encoding]::Unicode)
            $linkLog = Join-Path $logsRoot "$projectName-link.log"

            Write-Host "Linking $projectName without recovered libraries"
            $ErrorActionPreference = "Continue"
            $linkOutput = @(& $linker "@$linkResponse" 2>&1)
            $linkExitCode = $LASTEXITCODE
            $ErrorActionPreference = $savedPreference
            $linkOutput | Set-Content -LiteralPath $linkLog -Encoding UTF8
            if ($linkExitCode -ne 0) {
                throw "$projectName link failed with exit code $linkExitCode. See $linkLog"
            }
            $recoveredLibraryEvidence = @(
                Get-Content -LiteralPath $linkLog |
                    Where-Object {
                        $_ -match [Text.RegularExpressions.Regex]::Escape($resolvedSourceRoot) -and
                        $_ -match "\.(lib|dll)(\s|$)"
                    }
            )
            if ($recoveredLibraryEvidence.Count -ne 0) {
                throw "$projectName linker log references a recovered library: $($recoveredLibraryEvidence[0])"
            }

            $machine = Get-PeMachine $outputExe
            if ($machine -ne 0x014C) {
                throw ("Expected PE32 x86 machine 0x014C, got 0x{0:X4}: {1}" -f $machine, $outputExe)
            }

            $dependencyLog = Join-Path $logsRoot "$projectName-dependents.log"
            $dependencyLines = @(& $dumpbin /nologo /dependents $outputExe 2>&1)
            $dependencyLines | Set-Content -LiteralPath $dependencyLog -Encoding UTF8
            if ($LASTEXITCODE -ne 0) {
                throw "dumpbin failed for $outputExe. See $dependencyLog"
            }
            $dependencies = @(
                $dependencyLines |
                    ForEach-Object {
                        if ([string]$_ -match "^\s+([A-Za-z0-9_.-]+\.dll)\s*$") {
                            $Matches[1].ToLowerInvariant()
                        }
                    } |
                    Sort-Object -Unique
            )
            $unexpectedDependencies = @(
                $dependencies | Where-Object { $allowedDependencies -notcontains $_ }
            )
            if ($unexpectedDependencies.Count -ne 0) {
                throw "$projectName imports an unexpected runtime DLL: $($unexpectedDependencies -join ', ')"
            }

            $outputFile = Get-Item -LiteralPath $outputExe
            $builtProjects.Add([ordered]@{
                name = $projectName
                projectFile = Get-RelativePath $workCodeRoot $plan.project
                compiledSourceCount = @($plan.sources).Count
                compiledSources = @(
                    $plan.sources | ForEach-Object {
                        [ordered]@{ path = $_.path; sha256 = $_.sha256 }
                    }
                )
                excludedProjectEntries = $plan.excluded
                excludedResources = $plan.excludedResources
                executable = $outputExe
                size = $outputFile.Length
                sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $outputExe).Hash
                peMachine = ("0x{0:X4}" -f $machine)
                objectCount = $objectFiles.Count
                linkedLibraries = $systemLibraries
                dependencies = $dependencies
                compileResponse = $compileResponse
                linkResponse = $linkResponse
                compileLog = $compileLog
                linkLog = $linkLog
                dependencyLog = $dependencyLog
            })
        }

        $provisionerBuildRoot = Join-Path $buildRoot "account-provisioner"
        $provisionerObjectRoot = Join-Path $provisionerBuildRoot "obj"
        $provisionerSource = Join-Path $workRoot "provision_compare_accounts.cpp"
        $officialPrimitivesSource = Join-Path $workRoot "official_account_primitives.cpp"
        New-Item -ItemType Directory -Force -Path $provisionerBuildRoot, $provisionerObjectRoot | Out-Null
        Write-AccountProvisionerSource $provisionerSource
        $provisionerExtraction = Write-OfficialAccountPrimitivesSource `
            $workCodeRoot `
            $officialPrimitivesSource
        $provisionerSources = @(
            $officialPrimitivesSource,
            $provisionerSource
        )
        foreach ($provisionerInput in $provisionerSources) {
            Assert-File $provisionerInput "account provisioner source"
        }

        $provisionerCompileArguments = @(
            "/nologo",
            "/EHsc",
            "/W0",
            "/Od",
            "/Zi",
            "/MTd",
            "/Gy",
            "/FS",
            "/c",
            "/D_CRT_SECURE_NO_WARNINGS",
            "/DOPENWYD_COMPARE=1",
            "/DWIN32",
            "/D_WINDOWS",
            "/D_DEBUG",
            "/I`"$workCodeRoot`"",
            "/I`"$(Join-Path $workCodeRoot 'jsonlib')`"",
            "/Fo$provisionerObjectRoot\",
            "/Fd`"$(Join-Path $provisionerBuildRoot 'account-provisioner-compile.pdb')`""
        )
        $provisionerCompileArguments += @($provisionerSources | ForEach-Object { "`"$_`"" })
        $provisionerCompileResponse = Join-Path $provisionerBuildRoot "account-provisioner-cl.rsp"
        [IO.File]::WriteAllLines(
            $provisionerCompileResponse,
            $provisionerCompileArguments,
            [Text.Encoding]::Unicode
        )
        $provisionerCompileLog = Join-Path $logsRoot "account-provisioner-compile.log"
        Write-Host "Compiling local account provisioner through official CFileDB sources"
        $savedPreference = $ErrorActionPreference
        $ErrorActionPreference = "Continue"
        & $compiler "@$provisionerCompileResponse" 2>&1 |
            Tee-Object -FilePath $provisionerCompileLog
        $provisionerCompileExitCode = $LASTEXITCODE
        $ErrorActionPreference = $savedPreference
        if ($provisionerCompileExitCode -ne 0) {
            throw "Account provisioner compilation failed with exit code $provisionerCompileExitCode. See $provisionerCompileLog"
        }

        $provisionerObjects = @(
            (Join-Path $provisionerObjectRoot "official_account_primitives.obj"),
            (Join-Path $provisionerObjectRoot "provision_compare_accounts.obj")
        )
        foreach ($provisionerObject in $provisionerObjects) {
            Assert-File $provisionerObject "account provisioner object"
        }
        $provisionerExe = Join-Path $provisionerBuildRoot "provision_compare_accounts.exe"
        $provisionerLinkArguments = @(
            "/NOLOGO",
            "/DEBUG",
            "/INCREMENTAL:NO",
            "/OPT:REF",
            "/VERBOSE:LIB",
            "/SUBSYSTEM:CONSOLE",
            "/MACHINE:X86",
            "/OUT:`"$provisionerExe`"",
            "/PDB:`"$(Join-Path $provisionerBuildRoot 'account-provisioner.pdb')`""
        )
        $provisionerLinkArguments += @($provisionerObjects | ForEach-Object { "`"$_`"" })
        $provisionerLinkArguments += $systemLibraries
        $provisionerLinkResponse = Join-Path $provisionerBuildRoot "account-provisioner-link.rsp"
        [IO.File]::WriteAllLines(
            $provisionerLinkResponse,
            $provisionerLinkArguments,
            [Text.Encoding]::Unicode
        )
        $provisionerLinkLog = Join-Path $logsRoot "account-provisioner-link.log"
        $ErrorActionPreference = "Continue"
        $provisionerLinkOutput = @(& $linker "@$provisionerLinkResponse" 2>&1)
        $provisionerLinkExitCode = $LASTEXITCODE
        $ErrorActionPreference = $savedPreference
        $provisionerLinkOutput | Set-Content -LiteralPath $provisionerLinkLog -Encoding UTF8
        if ($provisionerLinkExitCode -ne 0) {
            throw "Account provisioner link failed with exit code $provisionerLinkExitCode. See $provisionerLinkLog"
        }
        $provisionerRecoveredLibraries = @(
            Get-Content -LiteralPath $provisionerLinkLog |
                Where-Object {
                    $_ -match [Text.RegularExpressions.Regex]::Escape($resolvedSourceRoot) -and
                    $_ -match "\.(lib|dll)(\s|$)"
                }
        )
        if ($provisionerRecoveredLibraries.Count -ne 0) {
            throw "Account provisioner references a recovered library: $($provisionerRecoveredLibraries[0])"
        }
        $provisionerMachine = Get-PeMachine $provisionerExe
        if ($provisionerMachine -ne 0x014C) {
            throw "Account provisioner is not PE32 x86: $provisionerExe"
        }
        $provisionerDependencyLog = Join-Path $logsRoot "account-provisioner-dependents.log"
        $provisionerDependencyLines = @(& $dumpbin /nologo /dependents $provisionerExe 2>&1)
        $provisionerDependencyLines | Set-Content -LiteralPath $provisionerDependencyLog -Encoding UTF8
        if ($LASTEXITCODE -ne 0) {
            throw "dumpbin failed for the account provisioner. See $provisionerDependencyLog"
        }
        $provisionerDependencies = @(
            $provisionerDependencyLines |
                ForEach-Object {
                    if ([string]$_ -match "^\s+([A-Za-z0-9_.-]+\.dll)\s*$") {
                        $Matches[1].ToLowerInvariant()
                    }
                } |
                Sort-Object -Unique
        )
        $provisionerUnexpectedDependencies = @(
            $provisionerDependencies | Where-Object { $allowedDependencies -notcontains $_ }
        )
        if ($provisionerUnexpectedDependencies.Count -ne 0) {
            throw "Account provisioner imports an unexpected runtime DLL: $($provisionerUnexpectedDependencies -join ', ')"
        }
        $accountProvisioner = [ordered]@{
            executable = $provisionerExe
            size = (Get-Item -LiteralPath $provisionerExe).Length
            sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $provisionerExe).Hash
            peMachine = ("0x{0:X4}" -f $provisionerMachine)
            officialSources = $provisionerExtraction.originalSources
            generatedSources = @(
                @($officialPrimitivesSource, $provisionerSource) | ForEach-Object {
                    [ordered]@{
                        path = Get-RelativePath $workRoot $_
                        sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $_).Hash
                    }
                }
            )
            linkedLibraries = $systemLibraries
            dependencies = $provisionerDependencies
            compileResponse = $provisionerCompileResponse
            linkResponse = $provisionerLinkResponse
            compileLog = $provisionerCompileLog
            linkLog = $provisionerLinkLog
            dependencyLog = $provisionerDependencyLog
        }
    }
    finally {
        foreach ($key in $environmentKeys) {
            [Environment]::SetEnvironmentVariable($key, $savedEnvironment[$key], "Process")
        }
    }
    $metadata.projects = $builtProjects.ToArray()

    $runtimeCompiledExtensions = @(
        ".exe",
        ".dll",
        ".lib",
        ".obj",
        ".pdb",
        ".ilk",
        ".exp",
        ".map",
        ".sys",
        ".com",
        ".scr"
    )
    $runtimeCopy = [ordered]@{
        copied = New-Object Collections.Generic.List[object]
        excluded = New-Object Collections.Generic.List[object]
    }
    foreach ($runtimePart in @("Common", "DBSrv\run", "TMSrv\run")) {
        $partSource = Join-Path $RuntimeDataRoot $runtimePart
        $partDestination = Join-Path $runtimeServerRoot $runtimePart
        $partResult = Copy-TreeFiles $partSource $partDestination {
            param($file, $relative)
            $extension = $file.Extension.ToLowerInvariant()
            if ($runtimeCompiledExtensions -contains $extension) {
                return [ordered]@{
                    Include = $false
                    Reason = "precompiled runtime artifact excluded"
                    Hash = $true
                }
            }
            if (Test-IsPortableExecutable $file.FullName) {
                return [ordered]@{
                    Include = $false
                    Reason = "MZ/PE payload excluded regardless of extension"
                    Hash = $true
                }
            }
            return [ordered]@{ Include = $true; Reason = ""; Hash = $false }
        }
        foreach ($entry in $partResult.copied) {
            $entry.path = Join-Path $runtimePart $entry.path
            $runtimeCopy.copied.Add($entry)
        }
        foreach ($entry in $partResult.excluded) {
            $entry.path = Join-Path $runtimePart $entry.path
            $runtimeCopy.excluded.Add($entry)
        }
    }

    $loopbackResults = @(Set-RuntimeLoopbackConfiguration $runtimeServerRoot)
    foreach ($builtProject in $builtProjects) {
        $runDirectory = Join-Path $runtimeServerRoot "$($builtProject.name)\run"
        Assert-Directory $runDirectory "$($builtProject.name) runtime directory"
        $runtimeExe = Join-Path $runDirectory "$($builtProject.name).exe"
        Copy-Item -LiteralPath $builtProject.executable -Destination $runtimeExe
        $runtimeHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $runtimeExe).Hash
        if (-not $runtimeHash.Equals($builtProject.sha256, [StringComparison]::OrdinalIgnoreCase)) {
            throw "Runtime copy hash mismatch: $runtimeExe"
        }
        $builtProject.runtimeExecutable = $runtimeExe
    }

    $runtimeExecutables = @(Get-ChildItem -LiteralPath $runtimeServerRoot -File -Filter "*.exe" -Recurse)
    if ($runtimeExecutables.Count -ne $builtProjects.Count) {
        throw "Runtime contains an unexpected executable count: $($runtimeExecutables.Count)"
    }
    foreach ($runtimeExecutable in $runtimeExecutables) {
        $matchingBuild = @(
            $builtProjects |
                Where-Object {
                    [IO.Path]::GetFileName($_.executable).Equals(
                        $runtimeExecutable.Name,
                        [StringComparison]::OrdinalIgnoreCase
                    )
                }
        )
        if ($matchingBuild.Count -ne 1) {
            throw "Runtime executable did not come from this build: $($runtimeExecutable.FullName)"
        }
        $runtimeHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $runtimeExecutable.FullName).Hash
        if (-not $runtimeHash.Equals($matchingBuild[0].sha256, [StringComparison]::OrdinalIgnoreCase)) {
            throw "Runtime executable differs from this build: $($runtimeExecutable.FullName)"
        }
    }

    $runtimeCompiledResidue = @(
        Get-ChildItem -LiteralPath $runtimeServerRoot -File -Recurse |
            Where-Object {
                $_.Extension -ne ".exe" -and
                $runtimeCompiledExtensions -contains $_.Extension.ToLowerInvariant()
            }
    )
    if ($runtimeCompiledResidue.Count -ne 0) {
        throw "Precompiled residue reached runtime: $($runtimeCompiledResidue[0].FullName)"
    }

    if ($null -eq $accountProvisioner) {
        throw "The source-built account provisioner was not produced."
    }
    $dbRunDirectory = Join-Path $runtimeServerRoot "DBSrv\run"
    $accountProvisionLog = Join-Path $logsRoot "account-provisioner-run.log"
    $savedLocation = (Get-Location).Path
    $savedPreference = $ErrorActionPreference
    try {
        Set-Location -LiteralPath $dbRunDirectory
        $ErrorActionPreference = "Continue"
        $provisionOutput = @(
            & $accountProvisioner.executable `
                $TestAccountOne `
                $TestAccountTwo `
                $TestAccountPassword 2>&1 |
                ForEach-Object { [string]$_ }
        )
        $provisionExitCode = $LASTEXITCODE
    }
    finally {
        $ErrorActionPreference = $savedPreference
        Set-Location -LiteralPath $savedLocation
    }
    $provisionOutput | Set-Content -LiteralPath $accountProvisionLog -Encoding UTF8
    if ($provisionExitCode -ne 0) {
        throw "Official CFileDB test-account provisioning failed with exit code $provisionExitCode. See $accountProvisionLog"
    }
    $provisionResult = $provisionOutput[-1] | ConvertFrom-Json
    if (-not [bool]$provisionResult.equivalent) {
        throw "The two official CFileDB account records are not equivalent after normalizing AccountName."
    }

    $testAccountEntries = New-Object Collections.Generic.List[object]
    foreach ($accountName in @($TestAccountOne, $TestAccountTwo)) {
        $accountPath = Join-Path $dbRunDirectory "account\$($accountName.Substring(0, 1))\$accountName"
        Assert-File $accountPath "provisioned CFileDB account"
        if ((Get-Item -LiteralPath $accountPath).Length -ne [int]$provisionResult.accountFileSize) {
            throw "Provisioned account size differs from sizeof(STRUCT_ACCOUNTFILE): $accountPath"
        }
        $testAccountEntries.Add([ordered]@{
            account = $accountName
            relativeFile = Get-RelativePath $dbRunDirectory $accountPath
            size = (Get-Item -LiteralPath $accountPath).Length
            sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $accountPath).Hash
        })
    }
    $localAccountConfig = [ordered]@{
        schemaVersion = 1
        warning = "Local deterministic test credentials only; never reuse this password."
        accounts = @(
            [ordered]@{ role = "native"; username = $TestAccountOne },
            [ordered]@{ role = "wasm"; username = $TestAccountTwo }
        )
        password = $TestAccountPassword
    }
    $localAccountConfigPath = Join-Path $manifestsRoot "test-accounts.local.json"
    Write-JsonFile $localAccountConfigPath $localAccountConfig
    $metadata.testAccounts = [ordered]@{
        creationPath = "CFileDB::AddAccount -> CFileDB::DBWriteAccount"
        provisioner = $accountProvisioner
        provisionerRunLog = $accountProvisionLog
        localCredentialFile = $localAccountConfigPath
        passwordSha256 = Get-StringSha256 $TestAccountPassword
        equivalentAfterNormalizingAccountName = [bool]$provisionResult.equivalent
        accountFileSize = [int]$provisionResult.accountFileSize
        accounts = $testAccountEntries.ToArray()
    }

    $metadata.runtime = [ordered]@{
        root = $runtimeServerRoot
        dataCopiedCount = $runtimeCopy.copied.Count
        precompiledExcludedCount = $runtimeCopy.excluded.Count
        precompiledExcluded = $runtimeCopy.excluded.ToArray()
        loopbackConfiguration = $loopbackResults
        files = Get-FileInventory $runtimeServerRoot
    }
    Write-JsonFile (Join-Path $manifestsRoot "runtime-inventory.json") $metadata.runtime

    foreach ($stateName in @("account", "char", "capsule")) {
        $stateSource = Join-Path $runtimeServerRoot "DBSrv\run\$stateName"
        Assert-Directory $stateSource "DBSrv $stateName state directory"
        Copy-Item -LiteralPath $stateSource -Destination (Join-Path $snapshotRoot $stateName) -Recurse
    }
    $snapshotFiles = Get-FileInventory $snapshotRoot
    foreach ($snapshotFile in $snapshotFiles) {
        $snapshotPath = Join-Path $snapshotRoot $snapshotFile.path
        if (Test-IsPortableExecutable $snapshotPath) {
            throw "Portable executable found in flat-file snapshot: $snapshotPath"
        }
    }
    $metadata.snapshot = [ordered]@{
        root = $snapshotRoot
        stateDirectories = @("account", "char", "capsule")
        fileCount = $snapshotFiles.Count
        files = $snapshotFiles
    }
    Write-JsonFile (Join-Path $manifestsRoot "baseline-state-inventory.json") $metadata.snapshot

    $metadata.status = "succeeded"
    $metadata.completedUtc = [DateTime]::UtcNow.ToString("o")
    Write-JsonFile $metadataPath $metadata

    Write-Host "Server source build succeeded."
    foreach ($builtProject in $builtProjects) {
        Write-Host "$($builtProject.name): $($builtProject.executable)"
        Write-Host "SHA-256: $($builtProject.sha256)"
    }
    Write-Host "Runtime: $runtimeServerRoot"
    Write-Host "Baseline snapshot: $snapshotRoot"
    Write-Host "Manifest: $metadataPath"
}
catch {
    $metadata.status = "failed"
    $metadata.completedUtc = [DateTime]::UtcNow.ToString("o")
    $metadata.error = $_.Exception.Message
    Write-JsonFile $metadataPath $metadata
    throw
}
