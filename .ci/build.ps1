param(
    [string]$SdkDir = $env:SDK_DIR,
    [string]$BaseCommit = "f5be1e4c",
    [string]$ToolchainBin = $env:D13X_TOOLCHAIN_BIN,
    [string]$OutDir = ""
)

$ErrorActionPreference = "Continue"
$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
if (-not $OutDir) { $OutDir = Join-Path $repoRoot "dist" }

if (-not $SdkDir) { $SdkDir = $env:D13X_SDK_DIR }
if (-not $ToolchainBin) { $ToolchainBin = $env:D13X_TOOLCHAIN_BIN }
if (-not $SdkDir) {
    throw "SDK_DIR is not set. Point it at a Luban-Lite checkout or let the script clone one."
}

# Make the job independent of the interactive shell's PATH.
$envRoot = Join-Path $SdkDir "tools\env"
$extraPaths = @(
    (Join-Path $envRoot "tools\bin"),
    (Join-Path $envRoot "tools\Python27\Scripts"),
    (Join-Path $envRoot "tools\Python27"),
    (Join-Path $envRoot "tools\Python38"),
    (Join-Path $SdkDir "toolchain\bin")
)
if ($ToolchainBin) { $extraPaths = @($ToolchainBin) + $extraPaths }
$pathParts = @($extraPaths | Where-Object { Test-Path $_ })
if ($pathParts.Count -gt 0) {
    $env:PATH = ($pathParts -join ";") + ";" + $env:PATH
}
$env:PYTHONIOENCODING = "UTF-8"
$env:SDK_PRJ_TOP_DIR = $SdkDir
$env:ENV_ROOT = $envRoot
$env:PKGS_ROOT = Join-Path $envRoot "packages"
$env:RTT_ROOT = Join-Path $SdkDir "kernel\rt-thread"

if (-not (Get-Command scons -ErrorAction SilentlyContinue)) {
    throw "scons is not in PATH. Checked SDK tools\env\tools\Python27\Scripts."
}

if (-not (Test-Path (Join-Path $SdkDir ".git"))) {
    git clone https://gitee.com/lcsc/luban-lite.git $SdkDir
    if ($LASTEXITCODE -ne 0) { throw "git clone failed" }
}

git -C $SdkDir fetch --all --tags --prune
if ($LASTEXITCODE -ne 0) { throw "git fetch failed" }

git -C $SdkDir reset --hard
if ($LASTEXITCODE -ne 0) { throw "git reset failed" }
git -C $SdkDir clean -fd
if ($LASTEXITCODE -ne 0) { throw "git clean failed" }
git -C $SdkDir checkout $BaseCommit
if ($LASTEXITCODE -ne 0) { throw "git checkout $BaseCommit failed" }

# Clean the previous build tree so the artifact is reproducible.
Remove-Item (Join-Path $SdkDir "output\d13x_JLC_rt-thread_helloworld") `
    -Recurse -Force -ErrorAction SilentlyContinue

git -C $SdkDir apply (Join-Path $repoRoot "patches\0001-smart-clock.patch")
if ($LASTEXITCODE -ne 0) { throw "git apply failed" }

$appDst = Join-Path $SdkDir "application\rt-thread\helloworld\smart_clock"
Remove-Item $appDst -Recurse -Force -ErrorAction SilentlyContinue
Copy-Item (Join-Path $repoRoot "application\rt-thread\helloworld\smart_clock") `
    (Join-Path $SdkDir "application\rt-thread\helloworld") -Recurse -Force
if ($LASTEXITCODE -ne 0) { throw "copy application failed" }

# The SDK normally generates this ignored header as a side effect of a full
# build. A clean checkout needs it before compiling net.c/fal_cfg.h.
$py3 = Join-Path $envRoot "tools\Python38\python3.exe"
$gen = Join-Path $SdkDir "tools\scripts\gen_partition_table.py"
$imgCfg = Join-Path $SdkDir "target\d13x\JLC\pack\image_cfg.json"
$partJson = Join-Path $SdkDir "output\d13x_JLC_rt-thread_helloworld\images\partition.json"
New-Item -ItemType Directory -Force (Split-Path $partJson) | Out-Null
if (Test-Path $py3) {
    & $py3 $gen -c $imgCfg -o (Join-Path $SdkDir "partition_table.h") -j $partJson
}
else {
    python3 $gen -c $imgCfg -o (Join-Path $SdkDir "partition_table.h") -j $partJson
}
if ($LASTEXITCODE -ne 0) { throw "generate partition_table.h failed" }

Push-Location $SdkDir
try {
    scons --apply-def=d13x_JLC_rt-thread_helloworld_defconfig
    if ($LASTEXITCODE -ne 0) { throw "apply-def failed" }
    scons -j8
    if ($LASTEXITCODE -ne 0) { throw "scons build failed" }
}
finally {
    Pop-Location
}

$image = Join-Path $SdkDir "output\d13x_JLC_rt-thread_helloworld\images\d13x_JLC_v1.0.0.img"
if (-not (Test-Path $image)) { throw "image not found: $image" }

Remove-Item $OutDir -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force $OutDir | Out-Null
Copy-Item $image $OutDir
Copy-Item (Join-Path $repoRoot "README.md") $OutDir -ErrorAction SilentlyContinue
Copy-Item (Join-Path $repoRoot "THIRD_PARTY_NOTICES.md") $OutDir -ErrorAction SilentlyContinue
Copy-Item (Join-Path $repoRoot "LICENSE") $OutDir -ErrorAction SilentlyContinue
if (Test-Path (Join-Path $repoRoot "LICENSES")) {
    Copy-Item (Join-Path $repoRoot "LICENSES") $OutDir -Recurse -Force
}

$sdDir = Join-Path $repoRoot "sd"
if (Test-Path $sdDir) {
    Copy-Item (Join-Path $sdDir "*.bin") $OutDir -ErrorAction SilentlyContinue
}

$lines = Get-ChildItem $OutDir -File | Sort-Object Name | ForEach-Object {
    $h = (Get-FileHash $_.FullName -Algorithm SHA256).Hash.ToLower()
    "$h  $($_.Name)"
}
$lines | Set-Content (Join-Path $OutDir "SHA256SUMS.txt") -Encoding ascii

Write-Host "build artifacts:"
Get-ChildItem $OutDir -File | Select-Object Name, Length
