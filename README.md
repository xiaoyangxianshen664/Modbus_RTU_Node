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

阶段 1 的 PC 端测试使用 CMake、gcc 和 CTest，当前 4 组测试全部通过。

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

阶段 2：将协议层接入 STM32F429，学习并实现 UART、RS485 收发和 FreeRTOS 任务协作。

完整项目说明（架构图、任务表、寄存器表、接线图和测试截图）在阶段 8 统一补齐。
