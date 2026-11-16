# 衡山派 D133EBS 智能面板（LVGL V9）

1024×600 LVDS 屏上的五页智能面板，基于 Luban-Lite + LVGL 9.1.0。

## 页面

| 页面 | 内容 | 数据源 |
|---|---|---|
| 时钟 | 时:分:秒与日期/星期横排、WiFi/NTP 状态 | ntp.aliyun.com（SNTP） |
| 天气 | IP 定位城市、温度/体感/湿度/风速、4 天预报、天气图标 | ip-api.com + api.open-meteo.com |
| 科技热点 | 8 条 IT 之家科技新闻标题 | api.ithome.com/json/newslist/news |
| 股指行情 | 上证/深证/创业板/沪深300 + 道琼斯/纳斯达克/标普500/纳指100（4×2 卡片，红涨绿跌） | qt.gtimg.cn |
| 设置 | WiFi 名称/密码输入（点输入框弹出键盘）、连接并保存；屏幕亮度滑块与快捷值 | 背光 PWM 通道 2 |

## 操作

- 左侧导航栏点按切页，内容区左右滑动切页（LVGL tileview）。
- 设置页点输入框弹出底部键盘面板（不遮罩上方页面），键盘顶部有独立编辑框，关闭/连接时同步回名称或密码框；修改 WiFi 后点“连接”，配置写入 Flash `data` 分区，重启后仍生效。只有键盘顶部编辑框显示光标，设置页原输入框不显示光标。
- 亮度拖动即时生效。

## 网络

- WiFi STA 默认使用占位符 `YOUR_WIFI_SSID` / `YOUR_WIFI_PASSWORD`，首次使用请在设置页填写实际账号密码并点“连接”，配置会写入 Flash。
- 连接后手动设置 DNS（223.5.5.5 / 119.29.29.29），因为热点 DHCP 未下发 DNS。
- NTP 同步后同时设置 RT-Thread RTC。
- 刷新周期：股指 60 s，天气/新闻 10 min，NTP 仅开机（失败重试；新闻失败 5 min 退避）。
- HTTP 接收缓冲 32 KB：IT之家 JSON 约 24 KB，加上响应头曾超过原 24 KB 缓冲导致截断/解析失败。

## 串口命令

```
sc_status               打印 WiFi/时间/天气/新闻/8 个指数/亮度
sc_wifi_cmd <ssid> [pwd] 修改 WiFi 并重连（会写入 Flash）
sc_bright_cmd <5..100>   设置背光亮度
```

## 文件

```
smart_clock/
├── net.c          WiFi、DNS、SNTP、HTTP、cJSON、行情解析、后台线程、WiFi 配置读写
├── ui.c           五个页面、导航、状态栏、键盘弹层、PWM 亮度、定时刷新
├── smart_clock.h  共享数据结构与互斥锁
├── font_cjk_16.c  20px 1bpp 宋体（GBK 21886 字符）
├── font_cjk_20.c  24px 1bpp 宋体（UI 固定字符子集）
├── font_icon.c    72px 4bpp Segoe UI Symbol 天气图标
└── SConscript
```

## 构建

```bash
scons --apply-def=d13x_JLC_rt-thread_helloworld_defconfig
scons -j8
```

镜像：`output/d13x_JLC_rt-thread_helloworld/images/d13x_JLC_v1.0.0.img`（约 2.6 MB）

配置要点：

- `CONFIG_AIC_LVGL_DEMO` 关闭，`CONFIG_LPKG_USING_LVGL=y`、`CONFIG_LVGL_V_9=y`
- `CONFIG_LPKG_USING_CJSON=y`
- `AIC_USING_FS_IMAGE_0/1` 关闭（不需要 demo 资源）
- **LVDS 位序为 VESA-24**（`CONFIG_AIC_LVDS_NS=y`、`CONFIG_AIC_LVDS_MODE=0`），1024×600 时序沿用屏幕适配配置
- `CONFIG_AIC_GE_DITHER` 关闭
- LVGL 线程栈 32 KB，色深 16

> 屏幕适配教程写的是 JEIDA-24，但这块 7 寸屏实际是 **VESA-24**。用 JEIDA-24 时纯红/绿/蓝看着正常，但中性灰会被映射成绿色、深灰会被映射成紫色；改成 VESA-24 后灰色/配色才正确。

## 字体与配色

- 字体为 1bpp 点阵宋体，完全不抗锯齿，因此没有灰色过渡像素；20px 覆盖 GBK 21886 个字符（含“叒/叕”等生僻字），24px 为 UI 字符子集。
- 配色采用 VSCode Dark+：背景 `#1E1E1E`、卡片/侧栏 `#252526`、次要文字 `#858585`、正文 `#D4D4D4`、强调蓝 `#569CD6`；仅股指沿用红涨绿跌。
- 天气图标为 Segoe UI Symbol 的 ☀ / ⛅ / ☁ / ☔ / ❄ / ⚡ / 🌫 单色字形。

## 板载 LED

PE17 用户 LED 由 `user_led.c` 改为 100 Hz 软件 PWM 呼吸灯（20 ms 周期，占空比 0→100→0 循环）。

## 已知限制

- 天气按公网 IP 定位（当前为上海）。
- 新闻只取标题。
- 股指非交易时段显示上一交易日收盘数据。
- 亮度设置不持久化，重启恢复 80%。
