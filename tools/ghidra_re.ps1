<#
.SYNOPSIS
Headless Ghidra RE workflow for Giants Recompiled. Entry point for setup,
importing default.xex, reading a function's decompiled code, and renaming +
exporting names into config/default.toml. No GUI involved anywhere.

.USAGE
    tools/ghidra_re.ps1 setup
    tools/ghidra_re.ps1 import
    tools/ghidra_re.ps1 dump <address>
    tools/ghidra_re.ps1 rename <address> <name> [<address> <name> ...]
#>

param(
    [Parameter(Mandatory = $true, Position = 0)]
    [ValidateSet("setup", "import", "dump", "rename")]
    [string]$Command,

    [Parameter(Position = 1, ValueFromRemainingArguments = $true)]
    [string[]]$Rest
)

$ErrorActionPreference = "Stop"
$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$ToolchainDir = Join-Path $RepoRoot "logs\toolchain"
$JdkDir = Join-Path $ToolchainDir "jdk"
$GhidraDir = Join-Path $ToolchainDir "ghidra"
$ProjectDir = Join-Path $RepoRoot "logs\ghidra_project"
$ProjectName = "giantsrecomp"
$XexPath = Join-Path $RepoRoot "rom\default.xex"

function Get-AnalyzeHeadlessPath {
    $found = Get-ChildItem -Path $GhidraDir -Filter "analyzeHeadless.bat" -Recurse -ErrorAction SilentlyContinue |
        Select-Object -First 1
    if (-not $found) {
        throw "analyzeHeadless.bat not found under $GhidraDir -- run 'tools/ghidra_re.ps1 setup' first."
    }
    return $found.FullName
}

function Get-JavaHome {
    $found = Get-ChildItem -Path $JdkDir -Filter "java.exe" -Recurse -ErrorAction SilentlyContinue |
        Select-Object -First 1
    if (-not $found) {
        throw "java.exe not found under $JdkDir -- run 'tools/ghidra_re.ps1 setup' first."
    }
    # java.exe is at <home>\bin\java.exe
    return (Get-Item $found.FullName).Directory.Parent.FullName
}

function Get-GhidraInstallDir {
    $found = Get-ChildItem -Path $GhidraDir -Directory -ErrorAction SilentlyContinue | Select-Object -First 1
    if (-not $found) {
        throw "Ghidra install not found under $GhidraDir -- run 'tools/ghidra_re.ps1 setup' first."
    }
    return $found.FullName
}

function Invoke-Setup {
    New-Item -ItemType Directory -Force -Path $ToolchainDir | Out-Null

    if (-not (Get-ChildItem -Path $JdkDir -Filter "java.exe" -Recurse -ErrorAction SilentlyContinue)) {
        Write-Host "Downloading JDK 21 (Temurin)..."
        $jdkRelease = Invoke-RestMethod -Uri "https://api.adoptium.net/v3/assets/latest/21/hotspot?os=windows&architecture=x64&image_type=jdk"
        $jdkUrl = $jdkRelease[0].binary.package.link
        $jdkZip = Join-Path $ToolchainDir "jdk.zip"
        Invoke-WebRequest -Uri $jdkUrl -OutFile $jdkZip
        New-Item -ItemType Directory -Force -Path $JdkDir | Out-Null
        Expand-Archive -Path $jdkZip -DestinationPath $JdkDir -Force
        Remove-Item $jdkZip
    } else {
        Write-Host "JDK already present, skipping."
    }

    if (-not (Get-ChildItem -Path $GhidraDir -Filter "ghidraRun.bat" -Recurse -ErrorAction SilentlyContinue)) {
        Write-Host "Downloading latest Ghidra release..."
        $ghidraRelease = Invoke-RestMethod -Uri "https://api.github.com/repos/NationalSecurityAgency/ghidra/releases/latest"
        $ghidraAsset = $ghidraRelease.assets | Where-Object { $_.name -match "^ghidra_.*_PUBLIC_.*\.zip$" } | Select-Object -First 1
        if (-not $ghidraAsset) {
            throw "Could not find a Ghidra release zip asset on the latest GitHub release."
        }
        $ghidraZip = Join-Path $ToolchainDir "ghidra.zip"
        Invoke-WebRequest -Uri $ghidraAsset.browser_download_url -OutFile $ghidraZip
        New-Item -ItemType Directory -Force -Path $GhidraDir | Out-Null
        Expand-Archive -Path $ghidraZip -DestinationPath $GhidraDir -Force
        Remove-Item $ghidraZip
    } else {
        Write-Host "Ghidra already present, skipping."
    }

    $ghidraInstallDir = Get-ChildItem -Path $GhidraDir -Directory | Select-Object -First 1
    # Ghidra's active module scan path is <install>\Ghidra\Extensions\<name>\ -- NOT the
    # top-level <install>\Extensions\Ghidra\, which only holds installable-but-inactive zips
    # (confirmed live: a module placed under Extensions\Ghidra\ never reached the loader;
    # ClassSearcher only picks up Ghidra\Extensions\<name>\ with its Module.manifest).
    $extensionsDir = Join-Path $ghidraInstallDir.FullName "Ghidra\Extensions"
    $alreadyInstalled = Get-ChildItem -Path $extensionsDir -Filter "*XEXLoader*" -Directory -ErrorAction SilentlyContinue
    if (-not $alreadyInstalled) {
        Write-Host "Downloading XEXLoaderWV extension..."
        $xexRelease = Invoke-RestMethod -Uri "https://api.github.com/repos/zeroKilo/XEXLoaderWV/releases/latest"
        $xexAsset = $xexRelease.assets | Where-Object { $_.name -match "\.zip$" } | Select-Object -First 1
        if (-not $xexAsset) {
            throw "Could not find an XEXLoaderWV release zip asset on the latest GitHub release."
        }
        $xexZip = Join-Path $ToolchainDir "xexloaderwv.zip"
        Invoke-WebRequest -Uri $xexAsset.browser_download_url -OutFile $xexZip
        New-Item -ItemType Directory -Force -Path $extensionsDir | Out-Null
        Expand-Archive -Path $xexZip -DestinationPath $extensionsDir -Force
        Remove-Item $xexZip

        # The extension's declared version (extension.properties) must match the installed
        # Ghidra version or the module is silently excluded. Patch it to match rather than
        # pin to whatever version the extension author last built against.
        $ghidraVersion = if ($ghidraInstallDir.Name -match 'ghidra_([\d.]+)_PUBLIC') { $matches[1] } else { $null }
        if ($ghidraVersion) {
            $propsFile = Get-ChildItem -Path $extensionsDir -Filter "extension.properties" -Recurse -ErrorAction SilentlyContinue | Select-Object -First 1
            if ($propsFile) {
                (Get-Content $propsFile.FullName) -replace '^version=.*$', "version=$ghidraVersion" |
                    Set-Content $propsFile.FullName
            }
        }
    } else {
        Write-Host "XEXLoaderWV already present, skipping."
    }

    # Ghidra 12's .py GhidraScript provider requires PyGhidra (native CPython via JPype) --
    # there's no more Jython fallback for headless .py scripts. PyGhidra is also usable as a
    # standalone library, which is what tools/ghidra_dump_function.py and
    # tools/ghidra_rename_and_export.py use directly, bypassing analyzeHeadless -postScript
    # entirely for those two operations. Installed from PyPI (not the offline wheel bundled
    # under Ghidra's pypkg/dist, which is pinned to jpype 1.5.2 with no wheel for newer
    # Python versions) so it resolves a jpype build compatible with whatever python3 is on
    # this machine.
    Write-Host "Installing PyGhidra..."
    python3 -m pip install --quiet pyghidra
    if ($LASTEXITCODE -ne 0) {
        throw "pip install pyghidra failed with exit code $LASTEXITCODE"
    }

    Write-Host "Setup complete."
}

function Invoke-Import {
    if (-not (Test-Path $XexPath)) {
        throw "rom/default.xex not found at $XexPath -- place your own dump there first."
    }
    New-Item -ItemType Directory -Force -Path $ProjectDir | Out-Null
    $env:JAVA_HOME = Get-JavaHome
    $analyzeHeadless = Get-AnalyzeHeadlessPath
    & $analyzeHeadless $ProjectDir $ProjectName -import $XexPath
    if ($LASTEXITCODE -ne 0) {
        throw "analyzeHeadless import failed with exit code $LASTEXITCODE"
    }
    Write-Host "Import complete: $ProjectDir\$ProjectName"
}

switch ($Command) {
    "setup" { Invoke-Setup }
    "import" { Invoke-Import }
    "dump" {
        if ($Rest.Count -ne 1) {
            throw "Usage: tools/ghidra_re.ps1 dump <address>"
        }
        $env:JAVA_HOME = Get-JavaHome
        $env:GHIDRA_INSTALL_DIR = Get-GhidraInstallDir
        python3 (Join-Path $RepoRoot "tools\ghidra_dump_function.py") $ProjectDir $ProjectName $Rest[0]
    }
    "rename" { throw "'rename' not implemented yet (Task 4)." }
}
