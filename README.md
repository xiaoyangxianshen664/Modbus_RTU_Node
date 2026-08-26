# Modbus_RTU_Node

基于 STM32F429 与 FreeRTOS 的 Modbus RTU 数据采集节点。

面向工业现场通信场景的 MCU 从站设备：完整寄存器模型、异常处理、可靠 RS485 通信和任务协作。

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
- 阶段 7：配置持久化与掉电恢复 ⬜
  - 计划将保持寄存器配置保存到 STM32 内部 Flash
  - 计划加入配置 CRC、版本校验、默认值回退和掉电安全机制

阶段 1 的 PC 端测试使用 CMake、gcc 和 CTest，当前 4 组测试全部通过。
阶段 2 修正 T1.5 状态机后，8 项 RS485 串口测试已重新回归并全部通过。
阶段 3 Keil 编译为 0 Error、0 Warning，读写、异常、广播和连续轮询实机验证全部通过。
阶段 4 Keil 编译为 0 Error、0 Warning，PC 端 4/4 测试通过，双通道 ADC DMA 和报警逻辑已上板验证。
阶段 5 已上板确认 SD 卡识别、FatFs 挂载成功，串口持续输出 ADC 采样；待 USB 读卡器到货后确认电脑端 `measure.csv` 内容。
阶段 6 Keil 编译为 0 Error、0 Warning；已完成 input/holding 寄存器、复位诊断、健康状态和 IWDG 自动复位实机测试。

## 项目结构

```text
protocol/include/   协议层头文件
protocol/src/       协议层实现
tests/              PC 端单元测试与集成测试
firmware/           STM32F429 + FreeRTOS 固件工程
docs/               项目文档、架构图和测试资料
```

## 编译与测试

```bash
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

## 后续计划

阶段 7：增加保持寄存器配置的 Flash 持久化、CRC 校验、双备份和掉电恢复。

完整项目说明（架构图、任务表、寄存器表、接线图和测试截图）在阶段 8 统一补齐。
