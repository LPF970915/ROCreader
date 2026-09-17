param(
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path,
    [string]$OutputDir = (Join-Path $PSScriptRoot "dist_official"),
    [string]$DownloadsDir = (Join-Path $PSScriptRoot "Downloads"),
    [string]$SdAppsDir = "E:\Roms\APPS",
    [string]$ReleaseVersion = ""
)

$ErrorActionPreference = "Stop"

function Get-NextRgdsReleaseVersion {
    param(
        [string]$DownloadsDir
    )

    # Start at 2.64 even when the directory is empty or contains only old releases.
    $maxVersionValue = 263
    if (Test-Path $DownloadsDir) {
        Get-ChildItem -LiteralPath $DownloadsDir -File -Filter "*for RGDS plus.zip" | ForEach-Object {
            if ($_.Name -match 'ver(\d+)\.(\d+)\s+for\s+RGDS plus\.zip$') {
                $major = [int]$Matches[1]
                $minor = [int]$Matches[2]
                $value = ($major * 100) + $minor
                if ($value -gt $maxVersionValue) {
                    $maxVersionValue = $value
                }
            }
        }
    }

    $nextValue = $maxVersionValue + 1
    $nextMajor = [int][Math]::Floor($nextValue / 100)
    $nextMinor = $nextValue % 100
    return ("ver{0}.{1:D2}" -f $nextMajor, $nextMinor)
}

$RepoRootAbs = [System.IO.Path]::GetFullPath($RepoRoot)
$OutputDirAbs = [System.IO.Path]::GetFullPath($OutputDir)
$DownloadsDirAbs = [System.IO.Path]::GetFullPath($DownloadsDir)
$Sysroot = Join-Path $RepoRootAbs "H700\sysroot_device"
$Dockerfile = Join-Path $PSScriptRoot "toolchain\Dockerfile.official"

if (-not (Test-Path $Sysroot)) {
    throw "RGDS plus official build requires sysroot: $Sysroot"
}

New-Item -ItemType Directory -Force -Path (Split-Path $Dockerfile) | Out-Null
Set-Content -LiteralPath $Dockerfile -Encoding ASCII -Value @"
FROM ubuntu:22.04
RUN apt-get -o Acquire::Retries=5 update && apt-get -o Acquire::Retries=5 install -y --no-install-recommends \
      ca-certificates \
      file \
      g++-aarch64-linux-gnu \
      make \
      pkg-config \
      python3 \
      tar \
      zip \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /work
"@

New-Item -ItemType Directory -Force -Path $OutputDirAbs | Out-Null
New-Item -ItemType Directory -Force -Path $DownloadsDirAbs | Out-Null

if ([string]::IsNullOrWhiteSpace($ReleaseVersion)) {
    $ReleaseVersion = Get-NextRgdsReleaseVersion -DownloadsDir $DownloadsDirAbs
    Write-Host "Auto RGDS release version: $ReleaseVersion"
}

$image = "rocreader-rgds-plus-official:latest"
docker build -t $image -f $Dockerfile (Split-Path $Dockerfile)
if ($LASTEXITCODE -ne 0) {
    throw "RGDS plus toolchain build failed with exit code $LASTEXITCODE"
}

$repoDocker = ($RepoRootAbs -replace "\\", "/")
$outDocker = ($OutputDirAbs -replace "\\", "/")
$downloadsDocker = ($DownloadsDirAbs -replace "\\", "/")
$releaseNamePrefix = "ROC$([char]0x5168)$([char]0x80FD)$([char]0x6F2B)$([char]0x753B)$([char]0x9605)$([char]0x8BFB)$([char]0x5668)"
$releaseZipName = "$releaseNamePrefix$ReleaseVersion for RGDS plus.zip"

$cmd = @'
set -eux
cd /work
mkdir -p /work/RGDSPlus/dist_official /work/RGDSPlus/Downloads
H700_ROOT=/work/H700 \
SYSROOT=/work/H700/sysroot_device \
DIST_ROOT=/work/RGDSPlus/dist_official/base \
DOWNLOADS_ROOT=/work/RGDSPlus/Downloads \
DOWNLOAD_ZIP_FILE=/work/RGDSPlus/dist_official/ROCreader_RGDSPlus_base.zip \
LEGACY_DOWNLOADS_MIRROR=0 \
REQUIRE_MUPDF=0 \
MAKE_JOBS=$(nproc 2>/dev/null || echo 2) \
./cross_compile_low_glibc.sh

rm -rf /work/RGDSPlus/dist_official/Roms
mkdir -p /work/RGDSPlus/dist_official/Roms/APPS
mkdir -p /work/RGDSPlus/dist_official/Roms/APPS/Imgs
cp /work/RGDSPlus/Imgs/ROCreader_RGDSPlus.png /work/RGDSPlus/dist_official/Roms/APPS/Imgs/ROCreader_RGDSPlus.png
cp -a /work/RGDSPlus/dist_official/base/APPS/ROCreader /work/RGDSPlus/dist_official/Roms/APPS/ROCreader_RGDSPlus
cp /work/RGDSPlus/rgds_plus_official_launcher.sh /work/RGDSPlus/dist_official/Roms/APPS/ROCreader_RGDSPlus.sh
cp /work/RGDSPlus/rgds_plus_power_control.sh /work/RGDSPlus/dist_official/Roms/APPS/ROCreader_RGDSPlus/rgds_power_control.sh
# Release defaults must not inherit a developer's current runtime settings.
cp /work/RGDSPlus/native_config.release.ini /work/RGDSPlus/dist_official/Roms/APPS/ROCreader_RGDSPlus/native_config.ini
sed -i 's/\r$//' /work/RGDSPlus/dist_official/Roms/APPS/ROCreader_RGDSPlus/native_config.ini
rm -rf /work/RGDSPlus/dist_official/Roms/APPS/ROCreader_RGDSPlus/URL
find /work/RGDSPlus/dist_official/Roms/APPS -type f -name '*.sh' -exec sed -i 's/\r$//' {} +
chmod +x /work/RGDSPlus/dist_official/Roms/APPS/ROCreader_RGDSPlus.sh
chmod +x /work/RGDSPlus/dist_official/Roms/APPS/ROCreader_RGDSPlus/rgds_power_control.sh
printf '%s\n' "$RGDS_PLUS_RELEASE_VERSION" > /work/RGDSPlus/dist_official/Roms/APPS/ROCreader_RGDSPlus/version.txt
cd /work/RGDSPlus/dist_official
python3 - <<'PY'
import os, zipfile
src='Roms'
dst='/work/RGDSPlus/Downloads/' + os.environ['RGDS_PLUS_RELEASE_ZIP_NAME']
if os.path.exists(dst):
    os.remove(dst)
with zipfile.ZipFile(dst, 'w', compression=zipfile.ZIP_DEFLATED, compresslevel=9) as zf:
    for root, dirs, files in os.walk(src):
        dirs.sort()
        files.sort()
        rel_root=os.path.relpath(root, '.').replace('\\\\', '/')
        if rel_root != '.':
            zf.write(root, rel_root + '/')
        for name in files:
            full=os.path.join(root, name)
            rel=os.path.relpath(full, '.').replace('\\\\', '/')
            zf.write(full, rel)
PY
rm -f /work/RGDSPlus/dist_official/ROCreader_RGDSPlus_base.zip
file /work/RGDSPlus/dist_official/Roms/APPS/ROCreader_RGDSPlus/rocreader_sdl
'@

$cmd = $cmd -replace "`r`n", "`n"

docker run --rm `
    -v "${repoDocker}:/work" `
    -v "${outDocker}:/out" `
    -v "${downloadsDocker}:/downloads" `
    -e "RGDS_PLUS_RELEASE_ZIP_NAME=$releaseZipName" `
    -e "RGDS_PLUS_RELEASE_VERSION=$ReleaseVersion" `
    $image `
    bash -lc $cmd
if ($LASTEXITCODE -ne 0) {
    throw "RGDS plus package build failed with exit code $LASTEXITCODE"
}

$ZipPath = Join-Path $DownloadsDirAbs $releaseZipName
if (-not (Test-Path $ZipPath)) {
    throw "RGDS package missing: $ZipPath"
}

if (Test-Path $SdAppsDir) {
    $RuntimeSrc = Join-Path $OutputDirAbs "Roms\APPS\ROCreader_RGDSPlus"
    $LauncherSrc = Join-Path $OutputDirAbs "Roms\APPS\ROCreader_RGDSPlus.sh"
    $RuntimeDst = Join-Path $SdAppsDir "ROCreader_RGDSPlus"
    $PreserveDirs = @("books", "book_covers", "cache", "Downloads")
    $PreserveFiles = @(
        "native_progress.tsv",
        "native_favorites.txt",
        "native_history.txt",
        "native_config.ini",
        "native_keymap.ini",
        "reader.cfg",
        "reader.gptk",
        "online_sources.ini",
        "config.json"
    )
    $PreserveRoot = Join-Path $SdAppsDir "_ROCreader_RGDSPlus_preserve"
    $PreserveStamp = Get-Date -Format "yyyyMMdd_HHmmss"
    $PreserveStage = Join-Path $PreserveRoot $PreserveStamp
    New-Item -ItemType Directory -Force -Path $PreserveStage | Out-Null
    foreach ($dir in $PreserveDirs) {
        $srcDir = Join-Path $RuntimeDst $dir
        if (Test-Path $srcDir) {
            Copy-Item -LiteralPath $srcDir -Destination (Join-Path $PreserveStage $dir) -Recurse -Force
        }
    }
    foreach ($file in $PreserveFiles) {
        $srcFile = Join-Path $RuntimeDst $file
        if (Test-Path $srcFile) {
            Copy-Item -LiteralPath $srcFile -Destination (Join-Path $PreserveStage $file) -Force
        }
    }
    if (Test-Path $RuntimeDst) {
        $stamp = Get-Date -Format "yyyyMMdd_HHmmss"
        Move-Item -LiteralPath $RuntimeDst -Destination (Join-Path $SdAppsDir "_ROCreader_RGDSPlus_backup_$stamp") -Force
    }
    Copy-Item -LiteralPath $RuntimeSrc -Destination $RuntimeDst -Recurse -Force
    foreach ($dir in $PreserveDirs) {
        $preserved = Join-Path $PreserveStage $dir
        $target = Join-Path $RuntimeDst $dir
        if (Test-Path $target) {
            Remove-Item -LiteralPath $target -Recurse -Force
        }
        New-Item -ItemType Directory -Force -Path $target | Out-Null
        if (Test-Path $preserved) {
            Get-ChildItem -LiteralPath $preserved -Force | ForEach-Object {
                Copy-Item -LiteralPath $_.FullName -Destination $target -Recurse -Force
            }
        }
    }
    foreach ($file in $PreserveFiles) {
        $preserved = Join-Path $PreserveStage $file
        if (Test-Path $preserved) {
            Copy-Item -LiteralPath $preserved -Destination (Join-Path $RuntimeDst $file) -Force
        }
    }
    Copy-Item -LiteralPath $LauncherSrc -Destination (Join-Path $SdAppsDir "ROCreader_RGDSPlus.sh") -Force
    $ImagesDst = Join-Path $SdAppsDir "Imgs"
    New-Item -ItemType Directory -Force -Path $ImagesDst | Out-Null
    Copy-Item -LiteralPath (Join-Path $OutputDirAbs "Roms\APPS\Imgs\ROCreader_RGDSPlus.png") `
        -Destination (Join-Path $ImagesDst "ROCreader_RGDSPlus.png") -Force
    Write-Host "Copied RGDS plus official package to $SdAppsDir"
} else {
    Write-Host "SD apps dir not found; package left at $ZipPath"
}

Write-Host "RGDS plus official package: $ZipPath"
