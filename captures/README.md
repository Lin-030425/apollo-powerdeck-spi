# Logic Analyzer Capture

`known-good-spi-64-byte.csv` 是基础链路调试阶段采集的已知正常波形：

- 64 字节一帧
- 数据为 `00 01 10 11` 循环
- 512 个 SCK 周期
- 当时的测试 SCK 约 390.625 kHz

该文件用于对照 CS、SCK 和 MOSI 的基本时序，不代表当前 14900 字节、
6.25 MHz 的最终配置。
