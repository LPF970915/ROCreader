$ErrorActionPreference = "Stop"

$buildScript = Join-Path $PSScriptRoot "..\RGDSPlus\build_rgds_plus_official.ps1"
$tokens = $null
$parseErrors = $null
$ast = [System.Management.Automation.Language.Parser]::ParseFile(
    $buildScript, [ref]$tokens, [ref]$parseErrors)
if ($parseErrors.Count -gt 0) {
    throw "Build script syntax errors: $parseErrors"
}
$versionFunction = $ast.Find({
    param($node)
    $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and
        $node.Name -eq "Get-NextRgdsReleaseVersion"
}, $true)
if ($null -eq $versionFunction) {
    throw "Release version function missing"
}
# Load only the version function, without running Docker or SD deployment.
. ([scriptblock]::Create($versionFunction.Extent.Text))

$tempRoot = [System.IO.Path]::GetFullPath([System.IO.Path]::GetTempPath())
$fixture = Join-Path $tempRoot ("rocreader-version-" + [guid]::NewGuid().ToString("N"))
try {
    $cases = @(
        @{ Name = "missing directory"; Files = $null; Expected = "ver2.64" },
        @{ Name = "empty directory"; Files = @(); Expected = "ver2.64" },
        @{ Name = "legacy releases"; Files = @("ver2.00", "ver2.03"); Expected = "ver2.64" },
        @{ Name = "first release"; Files = @("ver2.64"); Expected = "ver2.65" },
        @{ Name = "next release"; Files = @("ver2.65"); Expected = "ver2.66" },
        @{ Name = "major rollover"; Files = @("ver2.99"); Expected = "ver3.00" }
    )
    foreach ($case in $cases) {
        if ($null -ne $case.Files) {
            New-Item -ItemType Directory -Force -Path $fixture | Out-Null
            foreach ($version in $case.Files) {
                New-Item -ItemType File -Path (Join-Path $fixture "ROCreader$version for RGDS plus.zip") | Out-Null
            }
        }
        $actual = Get-NextRgdsReleaseVersion -DownloadsDir $fixture
        if ($actual -ne $case.Expected) {
            throw "$($case.Name): expected $($case.Expected), got $actual"
        }
        Write-Host "[pass] $($case.Name): $actual"
    }
} finally {
    $resolvedFixture = [System.IO.Path]::GetFullPath($fixture)
    if ((Split-Path -Parent $resolvedFixture).TrimEnd('\', '/') -ne $tempRoot.TrimEnd('\', '/') -or
        (Split-Path -Leaf $resolvedFixture) -notlike "rocreader-version-*") {
        throw "Refusing to remove unexpected fixture path: $resolvedFixture"
    }
    if (Test-Path -LiteralPath $resolvedFixture) {
        Remove-Item -LiteralPath $resolvedFixture -Recurse -Force
    }
}
