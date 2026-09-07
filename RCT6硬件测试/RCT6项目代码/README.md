# STM32F103RCT6 Modbus RTU Node

这是完整项目从 STM32F429 移植到自制 STM32F103RCT6 板后的 Keil HAL 工程。

## 主要功能

- FreeRTOS：Modbus、Monitor、Acquire、Log 四个任务
- USART2 + RS485：`9600、8E1`，从站地址 `1`
- Modbus RTU：`0x03 / 0x04 / 0x06 / 0x10`、异常响应和广播写入
- 双 ADC DMA：电位器工程量与 NTC 温度采集
- W25Q256：配置 A/B 双槽保存、CRC16 校验和断电恢复
- RTC + VBAT：日志时间戳和断电走时
- SDIO 4-bit DMA + FatFs：追加写入 `0:/measure.csv`
- MonitorTask + IWDG：任务心跳和健康监控

CAN 和 AT24C02C 已在同级硬件测试工程中完成单项验证，但没有加入当前 Modbus 四任务业务流程。

## 工程结构

```text
App/          项目应用、Modbus 传输、配置存储和 IWDG
Dirvers/BSP/  RCT6 板级外设驱动（原工程目录名保留）
Middlewares/  FreeRTOS 和 FatFs
Protocol/     Modbus RTU 协议层
Users/        main、HAL 配置和中断入口
Projects/     Keil MDK 工程文件
```

## 编译与烧录

1. 使用 Keil MDK-ARM 打开 `Projects/project.uvprojx`。
2. 选择 STM32F103RCT6 目标和实际使用的 SWD 下载器。
3. 编译并烧录。
4. USART1 调试串口使用 `115200、8N1`；USART2 Modbus 使用 `9600、8E1`。

已验证的构建结果：

```text
Code=60552  RO-data=1228  RW-data=200  ZI-data=21440
0 Error(s), 0 Warning(s)
```

目标内存配置为 Flash 256 KB、SRAM 48 KB。

## 上电输出

正常启动时，USART1 将依次显示 FatFs 挂载、W25Q256 配置存储、自检信息和 ADC 周期数据：

```text
FatFs mount PASS, result=0
W25Q256 config storage PASS
STM32F103RCT6 Modbus RTU Node start
USART2: 9600, 8E1, slave address=1
FreeRTOS: Modbus + Monitor + Acquire + Log
```

硬件、测试结果和问题复盘见仓库根目录的 `docs/RCT6硬件移植与调试报告.md`。
