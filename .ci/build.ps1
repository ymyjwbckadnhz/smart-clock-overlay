param(
    [string]$SdkDir = $env:SDK_DIR,
    [string]$BaseCommit = "f5be1e4c",
    [string]$ToolchainBin = $env:D13X_TOOLCHAIN_BIN,
    [string]$OutDir = ""
)

$ErrorActionPreference = "Stop"
$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
if (-not $OutDir) { $OutDir = Join-Path $repoRoot "dist" }

if (-not $SdkDir) {
    throw "SDK_DIR is not set. Point it at a Luban-Lite checkout or let the script clone one."
}
if ($ToolchainBin) {
    $env:PATH = "$ToolchainBin;$env:PATH"
}
if (-not (Get-Command scons -ErrorAction SilentlyContinue)) {
    throw "scons is not in PATH. Install the SDK's Python 2.7 + SCons and the RISC-V toolchain first."
}

if (-not (Test-Path (Join-Path $SdkDir ".git"))) {
    git clone https://gitee.com/lcsc/luban-lite.git $SdkDir
    if ($LASTEXITCODE -ne 0) { throw "git clone failed" }
}

git -C $SdkDir fetch --all --tags --prune
if ($LASTEXITCODE -ne 0) { throw "git fetch failed" }

git -C $SdkDir reset --hard
git -C $SdkDir clean -fd
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
