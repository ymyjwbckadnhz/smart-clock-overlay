# Smart Clock Overlay for Luban-Lite / D133EBS

基于 **Luban-Lite SDK + LVGL 9.1.0** 的 7 英寸 1024×600 LVDS 智能面板应用层。

本仓库**不包含完整的 Luban-Lite SDK**，只包含：

- `application/rt-thread/helloworld/smart_clock/`：五页 UI、网络、字库、图标
- `patches/0001-smart-clock.patch`：对 SDK 的必要修改（LVDS 配置、LVGL 配置、SConscript、PE17 呼吸灯）
- 构建说明与第三方声明

这样仓库只有约 8 MB，避免把 1 GB 的 SDK、工具链、厂商二进制一起发布。

## 上游基线

- 仓库：<https://gitee.com/lcsc/luban-lite.git>
- 基线提交：`f5be1e4c`（衡山派发布版 V1.0）
- 屏幕：7 英寸 1024×600，LVDS **VESA-24**
- 开发板：衡山派 HSPI-D133EBS（ArtInChip D13x）

> 屏幕适配教程写的是 JEIDA-24，但这块屏实际是 **VESA-24**。用 JEIDA-24 时纯红/绿/蓝正常，中性灰会偏绿、深灰会偏紫；patch 里已改为 `CONFIG_AIC_LVDS_NS=y` / `CONFIG_AIC_LVDS_MODE=0`。

## 功能

- 时钟：NTP 授时，时间/日期/星期横排
- 天气：IP 定位、当前天气、体感/湿度/风速、4 天预报
- 科技热点：IT 之家 8 条新闻
- 股指：上证/深证/创业板/沪深300 + 道琼斯/纳斯达克/标普500/纳指100
- 设置：WiFi 名称/密码编辑（底部键盘 + 顶部编辑条）、亮度滑块
- PE17 软件 PWM 呼吸灯
- 20px GBK 宋体（21886 字符）+ 24px 标题子集 + 72px 天气图标

## 快速开始

```bash
# 1. 克隆上游 SDK
git clone https://gitee.com/lcsc/luban-lite.git
cd luban-lite
git checkout f5be1e4c

# 2. 应用补丁
git apply /path/to/smart-clock-overlay/patches/0001-smart-clock.patch

# 3. 复制应用
cp -r /path/to/smart-clock-overlay/application/rt-thread/helloworld/smart_clock \
      application/rt-thread/helloworld/

# 4. 编译
scons --apply-def=d13x_JLC_rt-thread_helloworld_defconfig
scons -j8
```

Windows PowerShell：

```powershell
git clone https://gitee.com/lcsc/luban-lite.git
cd luban-lite
git checkout f5be1e4c

git apply X:\path\to\smart-clock-overlay\patches\0001-smart-clock.patch
Copy-Item X:\path\to\smart-clock-overlay\application\rt-thread\helloworld\smart_clock `
          application\rt-thread\helloworld\ -Recurse -Force

scons --apply-def=d13x_JLC_rt-thread_helloworld_defconfig
scons -j8
```

也可以直接运行仓库里的 `apply.sh` / `apply.ps1`。

镜像生成在：

```
output/d13x_JLC_rt-thread_helloworld/images/d13x_JLC_v1.0.0.img
```

## WiFi 配置

源码默认值是占位符：

```c
#define SC_WIFI_SSID_DEFAULT "YOUR_WIFI_SSID"
#define SC_WIFI_PASS_DEFAULT "YOUR_WIFI_PASSWORD"
```

首次使用请在设置页填写 WiFi 名称/密码，点“连接”，配置会写入 Flash `data` 分区，重启后保持。

串口也可以修改：

```
sc_wifi_cmd <ssid> <password>
```

## 串口命令

```
sc_status               打印 WiFi/时间/天气/新闻/8 个指数/亮度
sc_wifi_cmd <ssid> [pwd] 修改 WiFi 并重连（写 Flash）
sc_bright_cmd <5..100>   设置背光亮度
```

## 烧录

D13x 进入升级模式：

```
m 0x190300FC 0xac 1
m 0x19030100 0x54 1
m 0x190300FC 0 1
reboot
```

PC 端：

```
upgcmd.exe -p image output/d13x_JLC_rt-thread_helloworld/images/d13x_JLC_v1.0.0.img
upgcmd.exe shcmd reset
```

## 关键配置

- LVDS：`CONFIG_AIC_LVDS_NS=y`、`CONFIG_AIC_LVDS_MODE=0`（VESA-24）
- RGB565 双缓冲
- `CONFIG_AIC_LVGL_DEMO` 关闭，`CONFIG_LPKG_USING_LVGL=y`、`CONFIG_LVGL_V_9=y`
- `CONFIG_LPKG_USING_CJSON=y`
- `CONFIG_AIC_USING_FS_IMAGE_0/1` 关闭
- `CONFIG_AIC_GE_DITHER` 关闭
- HTTP 缓冲 32 KB（IT 之家 JSON 约 24 KB，原 24 KB 缓冲会截断）
- 20px 宋体覆盖 GBK 21886 字符

## 目录

```
application/rt-thread/helloworld/smart_clock/
├── net.c          WiFi/DNS/SNTP/HTTP/cJSON/行情/配置持久化
├── ui.c           五页 UI/键盘/亮度/定时刷新
├── smart_clock.h  共享数据与锁
├── font_cjk_16.c  20px 1bpp 宋体（GBK）
├── font_cjk_20.c  24px 1bpp 宋体（UI 子集）
├── font_icon.c    72px 4bpp 天气图标
├── README.md      应用层详细说明
└── SConscript
```

## 字体与许可证提醒

- 本仓库的 `font_cjk_16.c` / `font_cjk_20.c` 由 **Windows 宋体（SimSun）** 生成，`font_icon.c` 由 **Segoe UI Symbol** 生成。Windows 字体通常不允许再分发字形数据；如果要公开仓库，建议用 **Noto Serif CJK / 思源宋体** 和开源图标集重新生成，或先确认字体授权。
- 上游 SDK 的第三方组件（RT-Thread、LVGL、lwIP、cJSON、Realtek 固件、ArtInChip 二进制等）保留各自许可证；本仓库只提供应用层和文本 patch，不包含这些二进制。
- 你自己的应用代码可按 Apache-2.0 发布；不要重新许可上游组件。

详见 `THIRD_PARTY_NOTICES.md`。

## 已知限制

- 天气按公网 IP 定位。
- 新闻只取标题。
- 股指非交易时段显示上一交易日收盘数据。
- 亮度设置不持久化，重启恢复 80%。
