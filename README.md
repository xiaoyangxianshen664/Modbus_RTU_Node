# Modbus_RTU_Node

基于 STM32F429 / STM32F103RCT6 与 FreeRTOS 的 Modbus RTU 数据采集节点。

项目从 PC 端协议测试、开发板固件逐步推进到专用 PCB 设计、焊接和整机联调，已在自制 STM32F103RCT6 板上跑通 Modbus、双路 ADC、W25Q256 配置持久化、RTC、SDIO/FatFs 日志、任务健康监控和 IWDG。

![自制板完整测试接线](images/rct6-project/05-full-test-setup.jpg)

[查看 RCT6 硬件移植与调试报告](docs/RCT6硬件移植与调试报告.md)

## 实机结果

- Keil MDK-ARM：`0 Error(s), 0 Warning(s)`
- 固件占用：`Code=60552`、`RO-data=1228`、`RW-data=200`、`ZI-data=21440`
- RCT6 资源配置：Flash 256 KB、SRAM 48 KB
- FreeRTOS：Modbus、Monitor、Acquire、Log 四任务稳定运行
- 5 分钟压力测试：1521 帧，帧头错误 0，CRC 错误 0，健康故障 0，日志错误 0
- SD 卡：FatFs 挂载、CSV 追加写入和跨复位读取正常
- W25Q256：双槽配置保存、回读校验和断电恢复正常

## 当前进度

- 阶段 0：项目骨架已建立 ✅
- 阶段 1：Modbus RTU 协议层已完成 ✅
  - CRC16 校验
  - Modbus 大端字节序读写助手
  - ADU 长度、地址和 CRC 检查
  - 输入寄存器与保持寄存器模型
  - 0x03 / 0x04 读寄存器
  - 0x06 / 0x10 写保持寄存器
  - 异常响应与广播处理
- 阶段 2：裸机 RS485 + Modbus RTU 传输层已完成 ✅
  - USART2（PD5/PD6）与 PB8 DE/RE 方向控制
  - 9600 8E1、单字节中断接收与 256 字节帧缓冲区
  - TIM4 近似实现 T1.5 帧内间隔检查和 T3.5 帧结束判断
  - 主循环调用阶段 1 协议层解析请求并发送响应
  - 0x03 / 0x04 / 0x06 / 0x10、异常、广播和 CRC 错误实机验证
- 阶段 3：FreeRTOS 多任务接入已完成 ✅
  - 建立 Monitor、Modbus、Acquire、Log 四个业务任务及优先级
  - TIM4 检测到 T3.5 后，通过任务通知唤醒 ModbusTask
  - 使用互斥锁保护共享寄存器，锁内只复制或提交快照
  - MonitorTask 和 AcquireTask 保持 100 ms 周期，LogTask 模拟低优先级耗时负载
  - 低优先级负载运行期间连续轮询约 20 秒，共收到 189 帧完整响应，CRC 错误为 0
- 阶段 4：ADC DMA 数据采集已完成 ✅
  - ADC1 扫描 PC3 电位器和 PA4 NTC 两路模拟量，DMA 循环批量采样
  - DMA 半传输/全传输中断通过任务通知唤醒 AcquireTask，任务中求平均并换算工程量
  - 输入寄存器映射电位器原始值、百分比、NTC 原始值、温度和报警状态
  - 保持寄存器配置采集处理周期及 20.0~80.0℃ 报警上下限，默认报警范围为 20.0~40.0℃
  - USART1 输出十进制采样结果，已实机验证 ADC、Modbus 读取、保持寄存器写入和高温报警
- 阶段 5：RTC + SD 卡 CSV 日志已完成 ✅
  - 初始化 RTC，并通过备份寄存器避免每次复位覆盖当前时间
  - 复用已验证的 SDIO + DMA + FatFs 驱动，启动时注册 SD 驱动并挂载 FAT32 文件系统
  - AcquireTask 获取 RTC 时间并组装 `log_record_t`，通过 `LogQueue` 发送给 LogTask
  - LogTask 异步追加写入 `0:/measure.csv`，记录日期、时间、ADC 原始值、工程量、温度和报警状态
  - 确认开发板 PB13 需要拉低以关闭 WiFi 模块，释放 SDIO 资源
  - NTC 温度换算采用 R25=10KΩ、固定分压电阻=10KΩ、B=3950 的 Beta 公式
- 阶段 6：看门狗与复位诊断已完成 ✅
  - MonitorTask 通过 AcquireTask/LogTask 心跳判断关键任务是否持续推进
  - 仅在健康状态下喂独立看门狗 IWDG，异常时停止喂狗并等待自动复位
  - 通过 `input[8]~input[11]` 提供复位原因、健康故障次数、日志/SD 错误次数和当前健康状态
  - 已实机验证外部 NRST、断电上电和 IWDG 自动复位诊断
- 阶段 7：配置持久化与掉电恢复已完成 ✅
  - 使用 SPI5 外接 W25Q256 保存 4 个保持寄存器配置
  - 使用槽 A `0x001000` 和槽 B `0x002000` 两个独立 4KB 扇区交替保存
  - 配置记录包含 magic、版本号、递增 sequence、holding 数据和 CRC16
  - 启动时读取两个槽，校验合法性并恢复 sequence 最大的有效记录
  - 保持寄存器发生变化后写入备用槽，完成擦除、分页写入、回读和 CRC 校验
  - 已实机验证配置写入后立即读取、按键复位恢复、断电上电恢复和双槽交替保存
- 阶段 8：完整系统验收已完成 ✅
  - PC 端 CMake/CTest 测试 4/4 通过
  - 完成 0x03 / 0x04 / 0x06 / 0x10 正常功能、异常响应、广播、边界和 0x10 原子性测试
  - 错误 CRC、非法功能码、非法地址、非法数量和非法参数测试通过
  - 5 分钟混合轮询压力测试共 1131 帧，CRC 错误为 0，无通信中断
  - SD 写入期间持续轮询正常；读卡器已确认 `measure.csv` 存在并包含 11049 条有效记录
- 阶段 9：专用 PCB 设计、焊接与硬件联调已完成 ✅
  - 完成 STM32F103RCT6 专用板原理图、PCB、打板、焊接和迭代记录
  - 单项验证 LED/按键、USART1、RS485、CAN、双 ADC DMA、W25Q256、AT24C02C、SDIO/FatFs 和 RTC/VBAT
  - 将阶段 1~8 完整项目固件移植到 RCT6，修正 Flash/SRAM 配置并保持四任务架构
  - 完成 Modbus 功能、异常、广播、运行时报警、掉电保存、CSV 日志和 5 分钟压力测试

阶段 1 的 PC 端测试使用 CMake、gcc 和 CTest，当前 4 组测试全部通过。
阶段 2 修正 T1.5 状态机后，8 项 RS485 串口测试已重新回归并全部通过。
阶段 3 Keil 编译为 0 Error、0 Warning，读写、异常、广播和连续轮询实机验证全部通过。
阶段 4 Keil 编译为 0 Error、0 Warning，PC 端 4/4 测试通过，双通道 ADC DMA 和报警逻辑已上板验证。
阶段 5 已上板确认 SD 卡识别、FatFs 挂载成功，串口持续输出 ADC 采样；电脑端已读出 `measure.csv`，确认存在 11049 条有效记录。复位后重复表头是当前 `header_written` 仅保存在 RAM 中的已知现象。
阶段 6 Keil 编译为 0 Error、0 Warning；已完成 input/holding 寄存器、复位诊断、健康状态和 IWDG 自动复位实机测试。
阶段 7 Keil 编译为 0 Error、0 Warning；W25Q256 配置持久化、复位恢复、断电恢复和双槽交替保存已完成实机验证。
阶段 8 已完成系统级验收：协议、异常、广播、边界、原子性、CRC 错误、并发轮询和 5 分钟压力测试全部通过；SD 日志文件已完成电脑端内容验证。
阶段 9 已完成专用 PCB 的设计、焊接、单项外设验证和 RCT6 整机固件验收；详细证据见 [RCT6 硬件移植与调试报告](docs/RCT6硬件移植与调试报告.md)。

## 项目结构

```text
protocol/include/   协议层头文件
protocol/src/       协议层实现
tests/              PC 端单元测试与集成测试
firmware/           STM32F429 + FreeRTOS 固件工程
RCT6硬件测试/RCT6项目代码/ STM32F103RCT6 完整项目工程
docs/               项目文档、架构图和测试资料
images/rct6-project/ 阶段 9 实物、原理图、PCB 和实测截图
```

## 编译与测试

```bash
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

## 当前结论

阶段 1~9 已完成。项目已经覆盖协议实现、RTOS 任务协作、外设驱动、数据采集、掉电存储、文件日志、硬件设计、焊接调试和整机验收的完整流程。

当前已知边界：RTC 日期尚未通过上位机同步；IWDG 已正常运行，但 RCT6 整机版本尚未单独执行“停止喂狗”的故障注入测试。这两项不影响现有功能验收，可作为后续增强。
