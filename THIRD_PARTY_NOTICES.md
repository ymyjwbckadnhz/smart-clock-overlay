# Third-party notices

This repository is an **application overlay** for the Luban-Lite SDK. It does not
contain the SDK, toolchain, vendor drivers or firmware blobs. The patch only
modifies text files from the upstream SDK.

## Upstream SDK

- Luban-Lite / ArtInChip D13x SDK
- Upstream: https://gitee.com/lcsc/luban-lite.git
- Baseline commit: `f5be1e4c`
- License: see the `LICENSE` / `COPYING` files in each upstream directory.
  The upstream repository does not have a single root LICENSE; different
  components use different terms.

## Components referenced by the application

| Component | License | Notes |
|---|---|---|
| LVGL 9.1.0 | MIT | GUI toolkit, see `lvgl/LICENSE` upstream |
| RT-Thread | Apache-2.0 | RTOS kernel, see `kernel/rt-thread/LICENSE` upstream |
| lwIP 2.1.3 | BSD-style | TCP/IP stack, see upstream `COPYING` |
| cJSON | MIT | JSON parser, see `packages/third-party/cjson/LICENSE` |
| Montserrat font | SIL OFL 1.1 | LVGL built-in numeric font |

## Vendor binaries (not included here)

- Realtek RTL8189FTV WiFi driver and firmware
- ArtInChip display/GE/DE libraries and prebuilt `.a` files
- Other SoC/vendor blobs in the upstream SDK

These are **not** part of this overlay. If you redistribute the full SDK, check
the corresponding vendor licenses and do not assume they are open source.

## Fonts and icon glyphs — important

The generated C arrays in this repository are bitmap glyph data derived from
fonts installed on the author's Windows machine:

- `font_cjk_16.c`, `font_cjk_20.c`: **SimSun / 宋体** (Microsoft / Zhongyi)
- `font_icon.c`: **Segoe UI Symbol** (Microsoft)

Windows system fonts generally do **not** permit redistribution of glyph data.
Before publishing this repository publicly, do one of the following:

1. Replace them with glyphs generated from an open font, e.g.
   **Noto Serif CJK SC / 思源宋体** (SIL OFL 1.1), and an open icon set; or
2. Confirm that your font license allows embedding/redistribution; or
3. Do not commit the generated font C files and let users generate them locally.

The rest of the application code can be distributed under Apache-2.0, but that
does not relicense the SDK, vendor blobs, or font glyph data.
