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

> 官方教程和淘宝套件屏默认写的是 JEIDA-24，但本项目实际使用的 **辰源彩晶 7 英寸 1024×600 屏是 VESA-24**。用 JEIDA-24 时纯红/绿/蓝正常，中性灰会偏绿、深灰会偏紫；patch 里已改为 `CONFIG_AIC_LVDS_NS=y` / `CONFIG_AIC_LVDS_MODE=0`。换用官方套件屏时，先确认屏厂规格再决定是否改回 JEIDA，不要直接套用本仓库的 VESA 配置。

## 屏幕与触摸排线（重要）

- **屏幕 FPC 和触摸 FPC 的金手指都必须朝上插入（上接）**，再扣紧连接器锁扣。
- 插反或下接的典型现象：背光亮但无图像、花屏、触摸无反应。遇到这些先断电检查排线方向，不要先怀疑固件。
- 插拔前必须断电，避免带电插拔损坏 LVDS 收发器或触摸 IC。
- 本项目使用 **辰源彩晶 7 英寸 1024×600、40PIN LVDS** 屏；LVDS 映射和时序按这块屏调过。
- 官方文档/淘宝套件使用的是另一块 7 英寸 1024×600、40PIN、500nit 屏：  
  <https://e.tb.cn/h.jOqdBqqCDbDXRgD?tk=DvXk6VQDHfi>  
  商品页关键词：`7寸1024*600液晶触摸屏立创衡山派D133开发板屏40PIN LVDS500亮度`。
- 两块屏分辨率相同，但 **LVDS 位序/时序可能不同**。如果换官方屏后出现中性灰偏绿/偏紫、颜色不对，按屏厂规格调整 `CONFIG_AIC_LVDS_NS` 和 `CONFIG_AIC_LVDS_MODE`，并核对 `hfp/hbp/hsw/vfp/vbp/vsw`；不要同时套用辰源彩晶的配置。
- 触摸 IC 当前按 GT911 适配；换屏后如果触摸无反应，先确认触摸 FPC 方向和 I2C 地址，再改驱动配置。

## 功能

- 时钟：NTP 授时，时间/日期/星期横排
- 天气：IP 定位、当前天气、体感/湿度/风速、4 天预报
- 科技热点：IT 之家 8 条新闻
- 股指：上证/深证/创业板/沪深300 + 道琼斯/纳斯达克/标普500/纳指100
- 设置：WiFi 名称/密码编辑（底部键盘 + 顶部编辑条）、亮度滑块
- PE17 软件 PWM 呼吸灯
- 20px 2bpp Noto Serif SC（GB2312 Level1 + 符号，4586 字符）+ 24px 2bpp 标题子集 + 72px 天气图标

## 直接刷机（普通用户）

不需要编译，直接下载 Release：

<https://github.com/ymyjwbckadnhz/smart-clock-overlay/releases>

下载 `d13x_JLC_v1.0.0.img` 后：

```powershell
upgcmd.exe -p image d13x_JLC_v1.0.0.img
upgcmd.exe shcmd reset
```

需要先让板子进入升级模式，见下面“烧录”一节。

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

## 测试方法

当前 CI 只做**编译 + 产物检查**，实机验证按下面的手工清单执行；自动 HIL 暂未接入。

### 1. 构建检查

```bash
scons --apply-def=d13x_JLC_rt-thread_helloworld_defconfig
scons -j8
```

通过标准：

- `Luban-Lite is built successfully`
- 生成 `output/d13x_JLC_rt-thread_helloworld/images/d13x_JLC_v1.0.0.img`
- 镜像小于 3 MB（当前约 2.76 MB），不能超过 os 分区
- 无 `undefined reference`、无链接溢出

### 2. 烧录

正常 USB 升级：

```bash
upgcmd.exe -p image output/d13x_JLC_rt-thread_helloworld/images/d13x_JLC_v1.0.0.img
upgcmd.exe shcmd reset
```

进入 BROM 的两种方式：

- 按住 **UBOOT** 再按 **RESET**
- 如果接了 ESP8266 自动下载电路：`DTR=1, RTS=0` 进 BROM，`DTR=0, RTS=1` 正常复位

> 烧录前确认 `upgcmd.exe -l` 能看到设备；不要在 download 模式拔电。

### 3. 串口基线

串口 **115200 8N1**，上电后应能看到：

```text
Welcome to ArtInChip Luban-Lite 1.1.0 [D13x Inside]
Image version: 1.0.0
```

进入 shell 后执行：

```text
sc_status
free
```

通过标准：

- `wifi=1`、`ntp=1`
- `weather=1`
- `news=1 count=8`
- `stock=1`，8 行指数都有数值
- `free` 的 `heap_sys available size` 大于 100 KB（当前约 760 KB）

### 4. 五页功能清单

| 页面 | 检查项 | 通过标准 |
|---|---|---|
| 时钟 | 时间、日期、星期、NTP 状态 | 时间正确，10 分钟内不跳变；断网后仍走时 |
| 天气 | 城市、当前温度、天气现象、4 天预报、图标 | 上海/城市名正确；温度合理；图标不是方块 |
| 新闻 | 8 条标题、时间、滚动、触摸 | 8 条都有内容；中文/标点无方块；可滚动 |
| 股指 | 上证/深证/创业板/沪深300 + 道指/纳指/标普/纳指100 | 8 行都有数值；涨绿跌红；无 `--` |
| 设置 | WiFi 名称/密码、键盘、亮度滑条、PE17 | 能连接新 WiFi；密码有遮挡；亮度 5–100 可调；LED 呼吸 |

补充检查：

- 底部键盘不遮挡输入区，光标只在键盘上方编辑栏
- 切换页面无花屏、无残影、无卡死
- 触摸五个导航按钮和键盘按键都准确
- 重启后 WiFi、亮度设置仍保留

### 5. 稳定性

- 连续运行 **30 分钟**：新闻/天气/股指至少各刷新一次，无重启
- 连续运行 **2 小时**：`free` 的 available size 不明显持续下降
- 反复切换五页 50 次：无卡死、无内存耗尽
- 断网 5 分钟后恢复：WiFi 重连、NTP/数据恢复

### 6. 失败时抓什么

- 完整串口日志（从 `Pre-Boot Program` 到出错点）
- `sc_status`、`free` 输出
- 镜像文件名/大小、烧录时间
- 屏幕照片（花屏/方块/布局问题时）
- 是否插了 TF 卡、WiFi SSID、大致网络环境

### 7. 外部服务说明

天气/新闻/股票依赖公网，以下情况先排除网络，不要直接判固件故障：

- `wifi=0`：WiFi 配置/路由器/信号
- `weather=0`：Open-Meteo 不可达或 DNS 失败
- `news=0`：IT 之家接口超时/限流（可 `sc_news_test` 单独重试）
- `stock=0`：腾讯行情接口超时

启动、WiFi、NTP、内存、触摸、显示属于**硬性通过项**；外部数据失败要记录并重试，不能掩盖启动类问题。

## 关键配置

- LVDS：`CONFIG_AIC_LVDS_NS=y`、`CONFIG_AIC_LVDS_MODE=0`（VESA-24）
- RGB565 双缓冲
- `CONFIG_AIC_LVGL_DEMO` 关闭，`CONFIG_LPKG_USING_LVGL=y`、`CONFIG_LVGL_V_9=y`
- `CONFIG_LPKG_USING_CJSON=y`
- `CONFIG_AIC_USING_FS_IMAGE_0/1` 关闭
- `CONFIG_AIC_GE_DITHER` 关闭
- HTTP 缓冲 32 KB（IT 之家 JSON 约 24 KB，原 24 KB 缓冲会截断）
- 20px Noto Serif SC 2bpp 覆盖 GB2312 Level1、符号区与“叒/叕”等扩展，共 4586 字；24px 为 UI 子集。固件约 2.76 MB，仍在 3 MB os 分区内。

## 目录

```
application/rt-thread/helloworld/smart_clock/
├── net.c          WiFi/DNS/SNTP/HTTP/cJSON/行情/配置持久化
├── ui.c           五页 UI/键盘/亮度/定时刷新
├── smart_clock.h  共享数据与锁
├── font_cjk_16.c  20px 2bpp Noto Serif SC（GB2312 Level1 + 符号，4586 字）
├── font_cjk_20.c  24px 2bpp Noto Serif SC（UI 子集）
├── font_icon.c    72px 4bpp 天气图标
├── README.md      应用层详细说明
└── SConscript
```

## CI/CD

固件构建需要 **1 GB 的 Luban-Lite SDK、Python 2.7 + SCons、riscv64-unknown-elf 工具链**，
不适合在 GitHub 托管 runner 上每次重新安装。推荐用你自己的 Windows 机器做
**self-hosted runner**，SDK 只 clone 一次，之后每次 fetch + reset + apply patch。

### GitHub Actions

1. 在 GitHub 仓库 **Settings → Actions → Runners → New self-hosted runner** 添加 Windows runner，
   按页面提示下载、`config.cmd`、`run.cmd`（建议注册为 Windows 服务）。
2. 在仓库 **Settings → Secrets and variables → Actions → Variables** 添加：
   - `SDK_DIR`：Luban-Lite checkout 路径，例如 `C:\d13x-sdk`
   - `D13X_TOOLCHAIN_BIN`：工具链 `bin` 目录（可选，如果已在 PATH 中）
3. 确保 runner 的 PATH 中有：
   - `scons`
   - 对应 Python 2.7 环境
   - `riscv64-unknown-elf-gcc`
4. push 到 `main` 会构建并上传 artifact；push `v*` tag 会创建 GitHub Release：
   ```bash
   git tag -a v1.0.0 -m "Smart Clock v1.0.0"
   git push origin v1.0.0
   ```
5. 工作流：`.github/workflows/build.yml`；构建脚本：`.ci/build.ps1`。

首次运行会自动 clone SDK；之后每次重置到 `f5be1e4c`、打 patch、复制应用并编译。
产物在 `dist/`：`.img`、`SHA256SUMS.txt`。

已在干净的 SDK worktree 上验证：构建成功，镜像 **2,755,584 bytes**。

> self-hosted runner 会执行仓库里的脚本，只把它挂在可信仓库/分支上。

### Gitee

- 可以把同一仓库镜像到 Gitee；Gitee Go 选择 **Windows 主机组/自定义 runner**，
  构建步骤直接执行：
  ```powershell
  .\.ci\build.ps1 -SdkDir C:\d13x-sdk
  ```
- Gitee 发行版可以用 Gitee Go 的“发行版/附件”步骤，或调用 Gitee API 上传 `dist/*`；
  Token 放在流水线变量里，不要写进仓库。
- 如果只想用一套 CI，建议 **GitHub Actions 负责构建/Release，Gitee 作为镜像仓库**，
  避免维护两套流水线。

### 本地模拟

Windows 上也可以直接跑同一套脚本：

```powershell
$env:SDK_DIR = "C:\d13x-sdk"
.\.ci\build.ps1 -SdkDir $env:SDK_DIR
```

## 字体与许可证提醒

- 本仓库的 `font_cjk_16.c` / `font_cjk_20.c` 由 **Noto Serif SC（SIL OFL 1.1）** 生成，可随仓库分发。`font_icon.c` 仍由 **Segoe UI Symbol** 生成，公开前建议换开源图标集，或确认授权。
- 上游 SDK 的第三方组件（RT-Thread、LVGL、lwIP、cJSON、Realtek 固件、ArtInChip 二进制等）保留各自许可证；本仓库只提供应用层和文本 patch，不包含这些二进制。
- 你自己的应用代码可按 Apache-2.0 发布；不要重新许可上游组件。

详见 `THIRD_PARTY_NOTICES.md`。

## 已知限制

- 天气按公网 IP 定位。
- 新闻只取标题。
- 股指非交易时段显示上一交易日收盘数据。
- 亮度设置不持久化，重启恢复 80%。
