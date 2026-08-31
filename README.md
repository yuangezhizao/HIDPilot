# HIDPilot

HIDPilot 是面向 Seeed Studio XIAO RP2350 的可配置 USB HID 活动固件。设备枚举为键盘/相对鼠标复合 HID，并通过隔离的厂商 HID 接口向桌面 Chromium 提供 WebHID 配置通道。

默认配置在 USB 枚举完成后立即执行相对 X `+100`、等待 `200 ms`、相对 X `-100`，之后以相邻两轮开始时间至少间隔 `55 s` 的节奏重复。这里的移动量是 HID 相对报告单位，实际屏幕像素受操作系统指针加速影响。

## 安全提示

- 配置可发送鼠标点击和键盘组合键。试运行或应用含此类动作的配置前，网页会显示警告并倒计时 3 秒；仍应先关闭可能产生不可逆操作的窗口。
- 固件会在禁用、拔出、挂起、配置替换和动作异常时重置执行器，并在恢复发送后先发键盘与鼠标中性报告，降低卡键或按钮残留风险。
- USB 标识 `0xCAFE:0x4008` 仅用于原型开发。产品化前必须取得并替换为合法的 VID/PID，不应冒用 Seeed、Raspberry Pi 或其他厂商标识。

## 功能

- 无 RTOS、持续调用 `tud_task()` 的非阻塞状态机，不使用动作延时阻塞 USB。
- 键盘与相对鼠标共用一个 HID 接口及独立 Report ID；64 字节配置 Input/Output Report 位于单独的 `Usage Page 0xFF00 / Usage 0x01` 顶层集合。
- 动作支持 `1–60000 ms` 延时、四轴 `-127–127` 相对鼠标移动、`10–1000 ms` 鼠标点击和键盘组合键点击，最多 32 项。
- 运行时支持单次试运行、暂停、临时应用、保存、恢复默认、应用重启和进入 BOOTSEL。
- 两个 4 KiB Flash 槽位提供 CRC、代际选择、写后回读和掉电回退；最后一个 Flash 扇区保持未使用。
- USB 描述符声明 Remote Wake；周期到期且主机允许远程唤醒时请求恢复，恢复后先发送中性报告。

## 构建

要求 Pico SDK 2.2.0、CMake、Ninja 和 Arm GNU Toolchain 14.2.1。目标固定为 RP2350 Arm Secure 和 `seeed_xiao_rp2350`。

本机 Raspberry Pi Pico VS Code 扩展的默认安装命令如下：

```sh
PICO_SDK_PATH="$HOME/.pico-sdk/sdk/2.2.0" \
PICO_TOOLCHAIN_PATH="$HOME/.pico-sdk/toolchain/14_2_Rel1" \
"$HOME/.pico-sdk/cmake/v3.31.5/bin/cmake" \
  -S . -B build -G Ninja \
  -DCMAKE_MAKE_PROGRAM="$HOME/.pico-sdk/ninja/v1.12.1/ninja" \
  -DCMAKE_BUILD_TYPE=Release

"$HOME/.pico-sdk/cmake/v3.31.5/bin/cmake" --build build -j 8
```

产物位于 `build/hidpilot.elf`、`build/hidpilot.bin`、`build/hidpilot.uf2` 和 `build/hidpilot.elf.map`。构建后的边界检查会读取 ELF 的 `__flash_binary_end`，若固件进入 `0x101fd000` 起的配置区则直接失败。

本项目在 `pico_sdk_init()` 前把 `PICO_FLASH_SIZE_BYTES` 强制设为 `2097152`。SDK 2.2.0 的 XIAO RP2350 板级头声明 4 MiB，但当前板载 Flash 实测为 2 MiB；错误的尺寸会让持久化槽位落到不存在的地址。

Flash 布局：

| 区域 | Flash 偏移 | 用途 |
|---|---:|---|
| 固件 | `0x000000–0x1FCFFF` | ELF/UF2 镜像允许范围 |
| 配置槽 0 | `0x1FD000–0x1FDFFF` | 双槽记录 |
| 配置槽 1 | `0x1FE000–0x1FEFFF` | 双槽记录 |
| 保留 | `0x1FF000–0x1FFFFF` | 不使用，遵循 RP2350 A2 E10 的 SDK 预留方式 |

## 主机测试

```sh
sh tests/run_host_tests.sh
node --test web/protocol.test.js
```

C 测试覆盖配置边界、调度、挂起恢复、协议事务与双槽掉电回退；Node 内置测试运行器覆盖网页协议帧、CRC、字段校验和配置往返，不需要安装 npm 依赖。

## 烧录

按住 XIAO RP2350 的 BOOT 键连接 USB，或在已连接的配置页选择“重启到 BOOTSEL”，然后执行：

```sh
"$HOME/.pico-sdk/picotool/2.2.0-a4/picotool/picotool" load -v -x build/hidpilot.uf2
```

也可以把 `build/hidpilot.uf2` 拖入 BOOTSEL 磁盘。`-v` 会回读校验，`-x` 会在成功后启动应用。

## WebHID 配置页

WebHID 需要安全上下文。不要双击打开 HTML，应从仓库根目录启动本地服务器：

```sh
python3 -m http.server 8000 --directory web
```

然后用桌面 Chrome 或 Edge 访问 `http://localhost:8000`，点击“连接设备”，在系统选择器中选择 `HIDPilot XIAO RP2350`。页面按 VID/PID 和厂商 Usage 过滤，只访问独立配置集合；浏览器不会向页面暴露受保护的键盘和鼠标集合。

推荐流程：

1. 连接后从设备读取当前配置。
2. 编辑周期和动作顺序，先执行“单次试运行”。
3. 用“临时应用”观察周期行为，确认后再“保存到 Flash”。
4. “持久化暂停”会把启用标志关闭并保存；“恢复默认”会直接恢复并保存出厂默认配置。
5. 重启后重新授权连接并回读，确认 Flash 代际和配置内容。

当前支持桌面版 Chrome 与 Edge 的标准 WebHID。Safari 和 Firefox 不支持 WebHID，因此不能使用此配置页。

## USB 与协议

- VID/PID：`0xCAFE:0x4008`
- 制造商：`HIDPilot`
- 产品：`HIDPilot XIAO RP2350`
- 序列号：板载 Flash 唯一 ID 的十六进制字符串
- 配置集合：`Usage Page 0xFF00 / Usage 0x01`
- 固件版本：`1.0.0`
- 协议版本：`1`

完整 64 字节帧、schema v1、命令、状态码和 Flash 记录定义见 [docs/protocol.md](docs/protocol.md)。

## 恢复 BOOTSEL

若配置页不可用或固件无法枚举，断开 USB，按住板上的 BOOT 键再连接即可进入 ROM BOOTSEL。此路径不依赖当前 Flash 中的应用固件，之后可重新烧录 UF2。
