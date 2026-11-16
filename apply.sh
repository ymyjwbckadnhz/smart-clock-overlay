#!/usr/bin/env bash
# Apply the smart-clock overlay to a Luban-Lite checkout.
set -e

SDK="${1:-../luban-lite}"
HERE="$(cd "$(dirname "$0")" && pwd)"

if [ ! -d "$SDK/application/rt-thread/helloworld" ]; then
    echo "error: $SDK does not look like a Luban-Lite checkout" >&2
    echo "usage: $0 /path/to/luban-lite" >&2
    exit 1
fi

git -C "$SDK" apply "$HERE/patches/0001-smart-clock.patch"
cp -r "$HERE/application/rt-thread/helloworld/smart_clock" \
      "$SDK/application/rt-thread/helloworld/"

echo "overlay applied to $SDK"
echo "next: cd $SDK && scons --apply-def=d13x_JLC_rt-thread_helloworld_defconfig && scons -j8"
