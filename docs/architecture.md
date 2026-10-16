# 代码运行逻辑

## Apollo 发送端

```text
系统与 SPI5 初始化
  -> 配置软件 CS 和 SPI 分频
  -> 填充测试缓冲区
  -> 等待 Power Deck 启动
  -> CS 拉低
  -> HAL_SPI_Transmit 发送一帧
  -> CS 拉高
  -> 等待下一个发送周期
```

主要文件：

- `FSBL/Core/Inc/spi_link_config.h`：用户参数
- `FSBL/Core/Src/main.c`：测试数据、调度和发送逻辑
- `FSBL/Core/Src/spi.c`：CubeMX 生成的 SPI5 基础配置
- `FSBL/Core/Inc/main.h`：软件 CS 引脚定义

`main.c` 中的 `SPI5_Test_Config()` 会在 `MX_SPI5_Init()` 之后覆盖测试所需的
分频和 IO 保持设置，因此判断实际运行配置时需要同时检查这两个函数。

## Power Deck 接收端

```text
分配三个 DMA 缓冲区
  -> 初始化 SPI2 Slave
  -> 提交三个接收 transaction
  -> 等待接收完成中断
  -> 获取完成的 transaction
  -> 检查长度和测试数据
  -> 更新每秒统计
  -> 清空并重新提交 DMA 缓冲区
```

主要文件：

- `main/spi_link_config.h`：帧长、日志周期和 GPIO
- `main/spi_slave_receiver.c`：DMA 接收、校验和统计
- `examples/spi_slave_receiver_verbose.c`：保留的逐帧调试版本，不参与编译
