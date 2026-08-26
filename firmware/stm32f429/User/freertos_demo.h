#ifndef __FREERTOS_DEMO_H
#define __FREERTOS_DEMO_H

void freertos_demo(void);                    // 创建阶段 3 四任务并启动调度器
void modbus_task_notify_from_isr(void);       // T3.5 中断通知 ModbusTask 处理完整帧
void adc_dma_notify_from_isr(void);            // ADC DMA 中断通知 AcquireTask 处理一批采样
void log_storage_init(void);                   // 初始化 RTC 并挂载 SD 文件系统

#endif
