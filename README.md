# Apollo STM32N657 to Power Deck ESP32-C6 SPI Link

本仓库提供一套已经完成基础验证的 SPI 单向通信工程：Apollo 上的
STM32N657 使用 SPI5 Master 向 Power Deck 上的 ESP32-C6 SPI Slave
连续发送固定长度数据帧。

当前版本使用阻塞式 `HAL_SPI_Transmit()`，每帧 14900 字节，发送周期
30 ms（约 33.3 FPS），SCK 约 6.25 MHz。ESP32-C6 使用 DMA 接收，并每秒
输出一次接收帧数、正确帧数和错误帧数。

## 仓库内容

```text
firmware/apollo_spi_master/   STM32CubeMX/CubeIDE FSBL 工程
firmware/powerdeck_spi_slave/ ESP-IDF SPI Slave 工程
docs/                         接线、协议、构建和故障排查
captures/                     已验证波形的逻辑分析仪数据
```

## 硬件连接

| Apollo STM32N657 | Power Deck ESP32-C6 | 作用 |
| --- | --- | --- |
| PF15 / SPI5_SCK | GPIO6 / SCK | SPI 时钟 |
| PF11 / SPI5_MOSI | GPIO7 / MOSI | Master 到 Slave 数据 |
| PA3 / SPI5_CS | GPIO4 / CS | 低电平有效片选 |
| GND | GND | 共地 |

MISO 当前不使用。双方 IO 必须工作在兼容的 3.3 V 逻辑电平下。

## 只需修改的参数

Apollo 配置文件：

```text
firmware/apollo_spi_master/FSBL/Core/Inc/spi_link_config.h
```

Power Deck 配置文件：

```text
firmware/powerdeck_spi_slave/main/spi_link_config.h
```

两端的帧长度必须保持一致：

```c
/* Apollo */
#define SPI_TEST_FRAME_SIZE 14900U

/* Power Deck */
#define SPI_FRAME_SIZE_BYTES 14900U
```

帧率和 SCK 由 Apollo 控制：

```c
#define SPI_TEST_PERIOD_MS          30U
#define SPI_TEST_BAUDRATE_PRESCALER SPI_BAUDRATEPRESCALER_16
```

## 快速开始

1. 按上表连接 SCK、MOSI、CS 和 GND。
2. 先烧录 Power Deck，让 SPI Slave 和 DMA 接收队列准备完成。
3. 再烧录并启动 Apollo FSBL。
4. 打开 ESP-IDF monitor 查看统计结果。

正常输出示例：

```text
I (...) spi_slave_rx: 1s stats: frames=33 ok=33 errors=0 fps=33.3 | total: frames=330 ok=330 errors=0
```

完整操作见 [构建与烧录](docs/build-and-flash.md)。通信原理见
[协议说明](docs/protocol.md)。

## 当前限制

- 只有 Master 到 Slave 的单向数据传输。
- 帧长度固定，没有帧头、长度字段、序号或 CRC。
- Apollo 使用阻塞式发送，尚未升级到 DMA。
- 没有 READY 握手，依靠启动延时保证 Slave 先就绪。

建议先保持当前配置完成复现，再逐步修改帧长度、周期和分频。
