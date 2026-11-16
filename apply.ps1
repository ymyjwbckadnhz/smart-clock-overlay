param(
    [string]$Sdk = "..\luban-lite"
)

$ErrorActionPreference = "Stop"
$here = Split-Path -Parent $MyInvocation.MyCommand.Path

if (-not (Test-Path (Join-Path $Sdk "application\rt-thread\helloworld"))) {
    Write-Error "Not a Luban-Lite checkout: $Sdk"
}

git -C $Sdk apply (Join-Path $here "patches\0001-smart-clock.patch")
Copy-Item (Join-Path $here "application\rt-thread\helloworld\smart_clock") `
          (Join-Path $Sdk "application\rt-thread\helloworld") -Recurse -Force

Write-Host "overlay applied to $Sdk"
Write-Host "next: cd $Sdk; scons --apply-def=d13x_JLC_rt-thread_helloworld_defconfig; scons -j8"
