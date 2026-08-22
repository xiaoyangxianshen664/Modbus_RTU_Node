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

阶段 1 的 PC 端测试使用 CMake、gcc 和 CTest，当前 4 组测试全部通过。
阶段 2 修正 T1.5 状态机后，8 项 RS485 串口测试已重新回归并全部通过。

## 项目结构

```text
protocol/include/   协议层头文件
protocol/src/       协议层实现
tests/              PC 端单元测试与集成测试
firmware/           STM32F429 固件工程（阶段 2 开始使用）
docs/               项目文档、架构图和测试资料
```

## 编译与测试

```bash
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

## 后续计划

阶段 3：在当前裸机传输链路上接入 FreeRTOS，拆分接收、协议处理和数据采集任务。

完整项目说明（架构图、任务表、寄存器表、接线图和测试截图）在阶段 8 统一补齐。
