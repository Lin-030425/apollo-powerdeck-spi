# 构建与烧录

## 软件版本

- STM32CubeMX：使用支持 STM32N657 的版本
- STM32CubeIDE：使用支持 STM32N657 FSBL 的版本
- ESP-IDF：当前工程验证版本为 v6.1
- 调试器：ST-LINK V3

## Apollo STM32N657

### 导入 CubeIDE 工程

1. 打开 STM32CubeIDE。
2. 选择 `File > Import > General > Existing Projects into Workspace`。
3. Root directory 选择：

   ```text
   firmware/apollo_spi_master/STM32CubeIDE/FSBL
   ```

4. 选择 `apollo_spi_test_FSBL` 并完成导入。
5. 选中工程，执行 `Project > Build Project`。

CubeIDE 工程通过 linked resources 引用上层的 `FSBL/Core` 和 `Drivers`，
因此不要只复制 `STM32CubeIDE/FSBL` 子目录。

### CubeMX 配置

CubeMX 文件位于：

```text
firmware/apollo_spi_master/apollo_spi_test.ioc
```

当前关键配置：SPI5 Master、Mode 0、8 bit、MSB first、软件 NSS；PF15 为
SCK、PF11 为 MOSI、PA3 为软件 CS。

### 烧录

1. ST-LINK 连接电脑和 Apollo SWD。
2. 给 Apollo 正确供电并共地。
3. 在 CubeIDE 中选中 `apollo_spi_test_FSBL`。
4. 使用 Debug 或 Run 下载最新构建的 FSBL。
5. Debug 模式下按 Resume 让程序继续运行。

## Power Deck ESP32-C6

进入工程目录：

```sh
cd firmware/powerdeck_spi_slave
```

加载 ESP-IDF 环境并构建：

```sh
. "$IDF_PATH/export.sh"
idf.py set-target esp32c6
idf.py build
```

烧录并打开串口监视器：

```sh
idf.py -p /dev/cu.usbmodem101 flash monitor
```

端口名称可能不同，可使用以下命令查看：

```sh
ls /dev/cu.*
```

烧录时 BOOT 保持低电平并复位；烧录结束后释放 BOOT，再次复位进入 Flash
启动模式。串口监视器退出快捷键为 `Ctrl+]`。

## 推荐验证顺序

1. 64 字节、500 ms 周期、低速 SCK。
2. 保持帧长和周期不变，提高 SCK。
3. 逐步增加到 256、1024、4096、14900 字节。
4. 最后把发送周期缩短到 30 ms。

每次只修改一个变量，确保 Power Deck 持续显示 `errors=0`。
