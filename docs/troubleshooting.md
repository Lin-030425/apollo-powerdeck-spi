# 故障排查

## Power Deck 只显示 SPI ready

表示接收端已经进入等待状态，但还没有完成一个 SPI transaction。检查：

- Apollo 是否已经运行而不是停在 Debug 断点。
- CS 是否真的连接 PA3 到 GPIO4。
- SCK 是否连接 PF15 到 GPIO6。
- MOSI 是否连接 PF11 到 GPIO7。
- 两块板是否共地。

## 收到 516 bit，预期 512 bit

CS 有效期间出现了额外 SCK 边沿。确保 Mode 0 下 SCK 空闲为低电平，启用
`SPI_MASTER_KEEP_IO_STATE_ENABLE`，并避免在 CS 拉低后长时间等待再启动 SPI。

## 多帧合并成一个超长 transaction

通常表示 Power Deck 没有看到正确的 CS 上升沿。检查 CS 引脚映射、接线和
GPIO 电平，而不是先修改 MOSI 数据。

## 数据为全零

依次确认：

1. Apollo 缓冲区已填充。
2. `HAL_SPI_Transmit()` 返回 `HAL_OK`。
3. 逻辑分析仪能在 PF11/MOSI 上解码出测试序列。
4. Power Deck 的 DMA 缓冲区具备 `MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL`。

## Power Deck 一直 waiting for download

ESP32-C6 仍处于下载模式。烧录完成后释放 BOOT，再按 RESET，使其从 Flash
启动。

## CubeIDE 报 Target no device found

ST-LINK 已连接电脑，但没有发现目标 STM32。检查 Apollo 供电、SWD 接线、
复位状态和 ST-LINK 固件。

## 两端 expected frame size 不一致

同时修改以下两个宏并重新烧录两块板：

```text
Apollo:     SPI_TEST_FRAME_SIZE
Power Deck: SPI_FRAME_SIZE_BYTES
```
