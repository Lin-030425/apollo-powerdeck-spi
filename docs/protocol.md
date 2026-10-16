# SPI 协议说明

## 参数

| 项目 | 配置 |
| --- | --- |
| Master | STM32N657 SPI5 |
| Slave | ESP32-C6 SPI2_HOST |
| SPI mode | Mode 0（CPOL=0、CPHA=0） |
| 数据宽度 | 8 bit |
| 位序 | MSB first |
| CS | 软件控制，低电平有效 |
| 当前帧长 | 14900 字节 |
| 当前 SCK | 约 6.25 MHz |
| 当前周期 | 30 ms，约 33.3 FPS |

## 一帧的时序

```text
Apollo 将 CS 拉低
  -> HAL_SPI_Transmit() 发送固定长度数据
  -> 等待 SPI 发送完成
  -> Apollo 将 CS 拉高
  -> Power Deck 得到一个完成的 SPI transaction
```

CS 的一个低电平区间就是一帧。Power Deck 提前将三个 DMA transaction 放入
队列，接收完成中断只通知任务，数据校验在普通任务上下文执行。

## 测试数据

当前测试帧由以下 4 字节循环组成：

```text
00 01 10 11 00 01 10 11 ...
```

Power Deck 同时检查：

1. 实际位数是否等于 `帧长 × 8`。
2. 每个字节是否符合测试序列。

只有两项都正确才计入 `ok`。

## 带宽

当前有效载荷速率约为：

```text
14900 × 8 × 33.3 ≈ 3.97 Mbit/s
```

6.25 MHz SCK 发送一帧约需 19.1 ms，可以放入 30 ms 周期。继续提高帧率或
增加帧长时，需要重新核算 SCK 带宽，并考虑把 Apollo 改为 DMA 发送。

## 后续正式协议建议

图像正式传输时，建议加入帧头、帧序号、有效载荷长度和 CRC，并增加 READY
握手，避免 Slave 尚未提交 DMA 缓冲区时 Master 已经开始发送。
