# DM-MC02 BusScope

## 项目介绍

基于 DM-MC02（STM32H723VG）的总线监测和低频示波器工具。可同时查看三路 CAN / CAN-FD 的帧率、接收数量和最新报文，也可用板载 LCD 显示模拟波形、估算 PWM 频率和占空比。

USB 虚拟串口每 500 ms 输出一次状态快照，最新 CAN-FD 报文最多输出 64 字节。它不记录完整的 CAN 历史数据。

## 使用说明

### 编译与烧录

用 Keil MDK（ARM Compiler 5）打开 [DM_MC02_BusScope.uvprojx](MDK-ARM/DM_MC02_BusScope.uvprojx)，选择同名目标并编译。也可在项目根目录运行：

```powershell
powershell -ExecutionPolicy Bypass -File Tools/build.ps1 -Rebuild
```

脚本可用 `-Uv4Path` 指定 Keil 路径。通过 SWD 烧录生成的 `MDK-ARM/DM_MC02_BusScope/DM_MC02_BusScope.hex`。

### 接线

| 功能 | 连接 |
| --- | --- |
| 波形输入 | PA0，与信号源共地，输入范围 0～VDDA（按 3.3 V 换算） |
| 自测 PWM | PE13 接 PA0，输出约 10 Hz、50% 占空比 |
| CAN | 连接对应通道收发器侧的 CANH / CANL，并核对共地和终端电阻 |
| USB | 用数据线连接开发板 USB 设备接口 |

PA0 不能输入负电压或超过 VDDA 的信号。屏幕负电压刻度仅用于显示偏移，不代表引脚支持负压。

三路 CAN 默认仲裁速率为 1 Mbit/s、数据速率为 5 Mbit/s；需与所接总线一致。固件使用 Normal 模式，会参与 ACK。

### 页面操作

上电进入 CAN 页面，显示各通道最新报文的 ID、长度、前 8 字节数据，以及帧率和累计接收数。按 OK / SELECT 切换到示波器页面。

| 按键 | 示波器操作 |
| --- | --- |
| OK / SELECT | 切换 CAN / 示波器页面 |
| 上 / 下 | 放大 / 缩小 Y 轴 |
| 左 / 右 | 增加 / 减少中心电压，移动波形 |

示波器每帧显示 272 点，采样间隔 2 ms，适合低频信号；不能用于分析 CAN 位时序。PWM 边沿不足或幅度过小时显示 `--`。首次使用可将 PE13 接到 PA0，检查自测波形。

### USB 查看

连接 USB 后，用串口工具打开枚举出的虚拟串口，选择 115200、8N1。输出为文本 CSV：`CAN1～3` 是最新报文，`STAT1～3` 是通道统计，`SYS` 是 ADC、PWM 和采样状态。LCD 通道标题变红时，检查通道初始化和 bus-off 状态；排除总线问题后重启设备。
