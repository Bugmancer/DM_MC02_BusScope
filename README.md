# DM-MC02 BusScope

基于 **DM-MC02 / CtrBoard-H7（STM32H723VG）** 的嵌入式总线监测工具，集成三路 CAN/CAN-FD 状态监测、LCD 低频波形显示、PWM 参数估算和 USB CDC 状态输出。

适合在开发板上查看总线活动、核对最新报文，以及观察低频模拟信号。当前 USB 接口提供周期状态快照，不保存或输出全部历史 CAN 帧。

## 功能概览

| 模块 | 当前能力 |
| --- | --- |
| CAN / CAN-FD | 同时接收三路总线，支持标准 ID、扩展 ID 和 BRS 标识 |
| 总线统计 | 各通道累计接收数、帧率、最新 ID、长度及数据；报告 FIFO 丢失事件和 bus-off |
| LCD CAN 页面 | 显示最新帧前 8 字节，按通道变化刷新；短帧空位显示为 `--` |
| LCD 示波器 | PA0 输入，每帧 272 点，显示波形、帧末电压、最小值及最大值 |
| PWM 测量 | 通过迟滞阈值识别边沿，按完整周期估算频率和占空比 |
| 按键 | 切换页面、调节 Y 轴缩放与中心偏移，支持消抖和单次按下触发 |
| USB CDC | 每 500 ms 生成状态快照，最新 CAN-FD 帧最多输出完整 64 字节数据 |
| 自测输出 | PE13 输出约 10 Hz、50% 占空比 PWM，可连接 PA0 验证 |

### 使用边界

- **输入电压：** PA0 的测量范围为 `0 ~ VDDA`，当前按 3.3 V 换算。屏幕上的负电压坐标不代表引脚支持负电压，不能直接输入负压或超过 VDDA 的信号。
- **CAN 模式：** 三路默认仲裁段为 1 Mbit/s、数据段为 5 Mbit/s，使用 Normal 模式，会参与 ACK；当前拒绝远程帧。
- **波形带宽：** 显示采样节拍为 2 ms，名义采样率为 500 点/秒。高频信号会产生混叠，不能用该波形页面分析 CAN 位时序。
- **测量精度：** 采样由 RTOS 定时读取连续 ADC 转换结果，存在调度、采样量化及参考电压误差；当前没有精密电压校准或硬件输入捕获。

## 快速开始

### 1. 准备环境

| 用途 | 依赖 |
| --- | --- |
| 固件编译 | Windows、Keil MDK / uVision、ARM Compiler 5.06、STM32H7xx DFP |
| 修改外设配置 | STM32CubeMX，配置文件为 `DM_MC02_BusScope.ioc` |
| 主机测试 | PowerShell、可生成 Windows 可执行程序的 GCC，例如 MinGW-w64 |
| 板级运行 | DM-MC02 / CtrBoard-H7、配套 LCD 与按键、供电及烧录工具 |
| USB 查看 | 支持 USB CDC 虚拟串口的终端或上位机 |

主机测试使用本机 GCC，不使用 `arm-none-eabi-gcc`。项目自带 HAL、CMSIS、FreeRTOS 和 USB Device 中间件源码。

### 2. 连接硬件

下表为固件使用的 MCU 信号引脚，板上连接器位置以开发板原理图和丝印为准。

| 功能 | 引脚 / 外设 | 说明 |
| --- | --- | --- |
| 波形输入 | PA0 / ADC1_INP16 | 与信号源共地，输入限制在 `0 ~ VDDA` |
| LCD 按键 | PA5 / ADC1_INP19 | ADC 分压键盘输入 |
| 自测 PWM | PE13 / TIM1_CH3 | 连接到 PA0 后观察测试波形 |
| CAN1 | PD0 RX、PD1 TX / FDCAN1 | 通过对应 CAN 收发器连接总线 |
| CAN2 | PB5 RX、PB6 TX / FDCAN2 | 通过对应 CAN 收发器连接总线 |
| CAN3 | PD12 RX、PD13 TX / FDCAN3 | 通过对应 CAN 收发器连接总线 |
| LCD | SPI1，280 × 240 横屏 | 使用工程已有的屏幕接线配置 |
| USB CDC | 开发板 USB 设备接口 | 枚举后作为虚拟串口输出状态 |

CAN 总线应连接收发器侧的 CANH / CANL，不能直接接到 MCU 的 RX / TX 引脚。接入前核对总线速率、共地和两端终端电阻。

### 3. 编译与烧录

进入 `DM_MC02_BusScope` 项目根目录，执行完整构建：

```powershell
powershell -ExecutionPolicy Bypass -File Tools/build.ps1 -Rebuild
```

也可使用 Keil 打开 [DM_MC02_BusScope.uvprojx](MDK-ARM/DM_MC02_BusScope.uvprojx)，选择同名目标并构建。

脚本依次使用显式指定的路径或 `KEIL_UV4_PATH`、PATH、Windows 注册表定位 uVision。无法自动找到时，可指定安装路径：

```powershell
powershell -ExecutionPolicy Bypass -File Tools/build.ps1 -Rebuild -Uv4Path 'C:\Keil_v5\UV4\UV4.exe'
```

将示例路径替换为实际安装路径。省略 `-Rebuild` 时进行增量构建。

| 输出 | 路径 |
| --- | --- |
| 构建日志 | `MDK-ARM/build.log` |
| HEX 固件 | `MDK-ARM/DM_MC02_BusScope/DM_MC02_BusScope.hex` |
| AXF 调试文件 | `MDK-ARM/DM_MC02_BusScope/DM_MC02_BusScope.axf` |

构建脚本只编译和链接。烧录时使用板卡支持的调试器或下载工具，下载生成的固件。

### 4. 首次运行

1. 上电后进入 CAN 页面，检查三路通道显示。
2. 按 OK / SELECT 切换到示波器页面。
3. 将 PE13 短接到 PA0，观察约 10 Hz、50% 占空比的自测 PWM。
4. 通过 USB 数据线连接设备接口，打开枚举出的虚拟串口，检查周期输出的 `CANn`、`STATn`、`SYS` 行。

USB 串口参数默认报告为 115200 / 8N1。修改串口软件中的波特率不会改变 CAN 速率或示波器采样率。工程使用 OTG_HS 控制器的内部 Full-Speed PHY，源码中的 `_HS` 后缀不表示当前工作在 USB High-Speed 速率。

## 页面与按键

### CAN 页面

每路显示通道号、帧率 `F`、累计帧数 `N`、最新帧 ID、长度、帧格式和前 8 字节数据。完整载荷可从 USB 状态行读取。

| 显示 | 含义 |
| --- | --- |
| 绿色通道标题 | 通道近期接收到数据 |
| 灰蓝色通道标题 | 通道近期无接收数据 |
| 红色通道标题 | 通道初始化失败或处于 bus-off |
| `STD` / `EXT` | 标准 ID / 扩展 ID |
| `CAN` / `FD`、`BRS` | 帧格式及位速率切换标识 |
| `--` | 当前帧中不存在该字节 |
| `!` | 曾发生 FIFO 丢失事件或读取错误，详情见 USB 诊断字段 |

上电尚未收到报文时，ID 和长度显示初始值；应结合累计帧数判断是否已有有效报文。

### 示波器页面

`V` 为当前显示帧的最后一个采样值；`MIN`、`MAX` 为该帧最小和最大电压。PWM 频率和占空比来自同一帧；有效边沿不足或幅度太小时显示 `--`。

波形左侧为电压轴（V），显示上限、下限及中间三个刻度，随 Y 轴缩放和中心偏移更新。初始刻度从上到下为 `+3.30`、`+1.65`、`0.00`、`-1.65`、`-3.30` V；0 V 在可见范围内时以较亮的水平线标出。

刻度文字、刻度线、边框和网格只在进入页面或调整缩放、偏移时完整重画。普通采样帧比较新旧波形的像素掩码，仅写入发生变化的像素，不清空绘图区；旧波形离开的位置直接恢复网格、0 V 参考线或背景颜色。相同波形不会产生绘图区写入，顶部测量值仍随帧更新。

底部为相对当前帧起点的时间轴 `t(ms)`，每 100 ms 标注一次。`T:542ms` 表示首末采样点之间的跨度，即 `(272 - 1) × 2 ms`；它与约 544 ms 的整帧采集周期不同。为刻度预留边缘后，全部 272 点仍映射到完整绘图区，不截掉帧尾数据。

| 按键 | CAN 页面 | 示波器页面 |
| --- | --- | --- |
| OK / SELECT | 切换到示波器 | 切换到 CAN |
| 上 | 无操作 | Y 轴放大，每次增加 0.1x |
| 下 | 无操作 | Y 轴缩小，每次减少 0.1x |
| 左 | 无操作 | 中心电压增加 1 V，波形向下移动 |
| 右 | 无操作 | 中心电压减少 1 V，波形向上移动 |

缩放范围为 `0.1x ~ 8.0x`，初始值为 `1.0x`；中心电压偏移范围为 `-10 V ~ +10 V`。初始中心为 0 V，显示量程约为 `-3.3 V ~ +3.3 V`，不随波形幅度自动调整。超量程部分裁剪显示。

按键只在消抖后状态变化时触发，按住不连发。量程和偏移在下一次波形绘制时生效；每帧采集约需 544 ms，实际刷新还受绘图耗时影响。

## USB 状态协议

数据采用文本 CSV，行尾为 `\r\n`。每次快照依次输出 `CAN1`、`STAT1`、`CAN2`、`STAT2`、`CAN3`、`STAT3`、`SYS`。

上位机应按换行组装报文，并忽略不认识的前缀。USB 数据包、串口读取边界与文本行边界没有一一对应关系。

### 最新 CAN 帧

```text
CANn,tick_ms,fps,id_hex,length,format,brs[,byte_hex...]
```

| 字段 | 含义 |
| --- | --- |
| `n` | 通道号，1、2、3 |
| `tick_ms` | 本轮快照生成时的系统毫秒计时值，不是报文到达时间戳 |
| `fps` | 约每秒更新一次，按实际统计时长折算的帧/秒 |
| `id_hex` | 最新帧 ID，格式如 `0x00000123` |
| `length` | 载荷字节数，Classic CAN 最多 8，CAN-FD 最多 64 |
| `format` | `CAN` 或 `FD` |
| `brs` | `BRS` 或 `NOBRS` |
| `byte_hex` | 两位十六进制字节，数量与 `length` 一致；长度为 0 时无数据字段 |

### 通道诊断

```text
STATn,count,lost_events,rx_errors,initialized,bus_off,id_type
```

| 字段 | 含义 |
| --- | --- |
| `count` | 累计成功读出的帧数；启动后尚未收到报文时为 0 |
| `lost_events` | FIFO 丢失通知次数，一次通知可能对应多个丢帧，不能视为精确丢帧数 |
| `rx_errors` | 调用 HAL 读取 FIFO 失败的次数 |
| `initialized` | 1 表示通道配置和启动成功，0 表示失败 |
| `bus_off` | 1 表示检测到总线关闭，0 表示未检测到；当前只报告，不自动恢复 |
| `id_type` | 最新帧 ID 类型，`STD` 或 `EXT` |

### 系统诊断

```text
SYS,adc_ready,pwm_ready,scope_replaced_frames,scope_late_events
```

| 字段 | 含义 |
| --- | --- |
| `adc_ready` | 1 表示 ADC 已启动且未报告运行错误；为 0 时停止采样和按键处理 |
| `pwm_ready` | 1 表示自测 PWM 启动成功，0 表示启动失败 |
| `scope_replaced_frames` | 尚未被 LCD 消费就被更新帧替换的完整波形帧数；停留在 CAN 页时增长正常 |
| `scope_late_events` | 采样任务检测到节拍异常的次数；异常时丢弃正在采集的部分帧 |

单路 CAN 数据与系统诊断的节选示例：

```text
CAN1,1500,100,0x00000123,2,CAN,NOBRS,12,34
STAT1,150,0,0,1,0,STD
SYS,1,1,0,0
```

计数器和毫秒计时值均为 32 位无符号整数，会自然回绕。无新帧时继续输出最后一帧，帧率会在后续统计周期回落为 0。

快照生成周期为 500 ms，不保证主机每 500 ms 必定收到数据。USB 忙时每隔 10 ms 尝试提交，未提交的旧快照会被新快照替换。已经提交的缓冲区保留到传输完成；`USBD_OK` 仅表示交给 USB 栈，不代表主机确认接收。

## 代码结构

```text
DM_MC02_BusScope/
  App/
    Inc/                      应用接口
    bus_scope.c               任务、状态、采样与页面逻辑
    bus_scope_signal.c        不依赖 HAL 的 PWM 测量算法
  Core/                       CubeMX 外设初始化和 FreeRTOS 配置
  Drivers/                    CMSIS 与 STM32 HAL
  Middlewares/                FreeRTOS 与 USB Device 中间件
  USB_DEVICE/                 USB 配置和 CDC 收发接口
  User/                       LCD 驱动与字库
  MDK-ARM/
    DM_MC02_BusScope.uvprojx   Keil 工程
    BusScope.sct              自定义内存布局
  Tests/                      主机回归测试及 HAL/RTOS 桩
  Tools/                      PowerShell 构建和测试脚本
  DM_MC02_BusScope.ioc        CubeMX 工程配置
```

### 任务分工

保留 CubeMX 生成的任务名称，实际职责如下。栈容量单位为 32 位字。

| 任务名 | 职责 | 优先级 | 栈容量 |
| --- | --- | --- | --- |
| `ImuTask` | 每 2 ms 采集一个波形点 | Realtime | 256 |
| `FunTest` | 三路 CAN 接收、帧率与协议状态 | High | 512 |
| `KeyTask` | ADC 按键消抖和页面设置 | AboveNormal | 128 |
| `LcdTask` | LCD 初始化和绘图 | Normal | 512 |
| `defaultTask` | USB 初始化、快照生成与发送 | BelowNormal | 512 |

CAN 中断通过任务通知唤醒接收任务，并保留 1 ms 超时轮询。每轮每路最多读取 32 帧；LCD 和 USB 使用临界区内取得的通道快照，避免读取到更新一半的状态。

示波器使用双缓冲发布完整帧，LCD 复制帧后绘制，采样不等待绘图完成。采样节拍通过 `vTaskDelayUntil` 维持，检测到节拍异常时重新开始采集当前帧。PWM 算法使用双阈值迟滞，并排除帧首尾不完整周期；一帧内至少需要两个可靠上升沿。

### DMA 与再生成约束

ADC DMA 缓冲区固定在 SRAM1 `0x30000000`，独占一个 32 字节对齐的缓存行。该区在 [BusScope.sct](MDK-ARM/BusScope.sct) 中设置为 `UNINIT`，ARMCC 声明配合 `zero_init` 属性，应用开启 SRAM1 时钟后再初始化，避免启动阶段提前访问。

读取 DMA 数据时按需使 D-cache 缓存行失效。DMA 半传输和传输完成中断未用于采样，启动后关闭这两类中断，保留错误处理。DMA1 不能访问 DTCM，不应将此缓冲区移入 DTCM。

使用 CubeMX 再生成后，检查以下项目并重新构建：

- Keil 链接器仍使用 `MDK-ARM/BusScope.sct`。
- App 组包含 `bus_scope.c` 和 `bus_scope_signal.c`，包含路径中有 `App/Inc`。
- FreeRTOS 的任务优先级、栈容量和应用入口与上表一致。
- `USER CODE` 区中的 USB 任务入口、错误钩子及 CDC 实现得到保留。

## 测试与验收

### 主机测试

在项目根目录执行：

```powershell
powershell -ExecutionPolicy Bypass -File Tools/test.ps1
```

需要指定编译器时：

```powershell
powershell -ExecutionPolicy Bypass -File Tools/test.ps1 -Compiler 'C:\msys64\ucrt64\bin\gcc.exe'
```

测试可执行文件默认写入系统临时目录，可用 `-OutputDirectory` 指定输出目录。

| 测试组 | 主要覆盖范围 |
| --- | --- |
| `signal` | 不同相位和占空比、直流、低幅度、阈值噪声、无效参数与整数边界 |
| `usb` | 异步缓冲区所有权、忙状态、断连与恢复、长度检查、串口参数握手 |
| `app` | DLC 转换、短帧清尾、完整 64 字节输出、缓冲区边界、计数回绕、采样延迟、双缓冲交接及示波器刻度绘制 |

2026-10-04 验证记录：固件通过 Keil 构建（0 错误、0 警告）和上述三组主机测试，修改后的固件尚未完成烧录后的实板验收。测试中的 HAL / RTOS 桩用于验证应用逻辑，不能证明实机时序或总线吞吐量。

### 实板验收

1. 不接 USB 上电，确认 CAN 页面和按键工作；再反复插拔 USB，检查枚举和完整状态行。
2. 将 PE13 接 PA0，确认约 10 Hz、50% 占空比波形；切换页面、缩放和偏移后检查持续更新，确认电压刻度同步变化、时间刻度与波形对应。
3. 各通道依次注入标准帧、扩展帧和 64 字节 CAN-FD 帧，再发送 0 / 1 / 8 字节短帧，核对计数、长度及数据。
4. 三路同时持续接收并切换示波器页面，记录 `lost_events`、`rx_errors` 和 `scope_late_events`，按目标负载评估吞吐量。
5. 长时间运行并检查任务栈高水位；停止 CAN 发送后确认帧率回落为 0，将 PA0 接到稳定电平后确认 PWM 测量无效。

RTOS 已启用任务创建失败检查、栈溢出和内存分配失败钩子；发生这些错误时进入 `Error_Handler`。静态编译检查不能替代实际栈余量测量。

## 常见问题

| 现象 | 检查方向 |
| --- | --- |
| CAN 计数一直为 0 | 检查通道、CANH / CANL、速率、终端电阻及 `initialized`；仅有 ID 初始值不代表收到报文 |
| CAN 标题变红 | 查看 `initialized` 与 `bus_off`；修复总线条件后重新初始化设备，当前没有自动 bus-off 恢复 |
| `lost_events` 增长 | 表示 FIFO 曾溢出，降低输入负载并检查实际接收能力；USB 快照不会补回丢失帧 |
| USB 无输出 | 检查数据线、USB 设备接口、系统枚举及串口是否被其他程序占用 |
| 波形不在屏幕内 | 检查 PA0 接线、共地、缩放和中心偏移，确认输入在 ADC 允许范围内 |
| PWM 显示 `--` | 检查波形幅度及一帧内是否包含两个可靠上升沿，先用板载 10 Hz 信号验证 |
| 波形帧替换计数持续增长 | CAN 页面不消费波形帧，增长正常；在示波器页增长则说明绘制未跟上采集 |
| ADC 和按键同时停止 | 查看 `adc_ready`，检查 ADC / DMA 错误及 SRAM1 链接配置 |
