#ifndef MODBUS_CRC16_H
#define MODBUS_CRC16_H

#include <stddef.h>
#include <stdint.h>

//调用示例
/*   static const uint8_t request[] = {
        // 测试数据：Modbus 读保持寄存器请求帧
        UINT8_C(0x01), // 从机地址 0x01
        UINT8_C(0x03), // 功能码 0x03（读保持寄存器）
        UINT8_C(0x00), // 起始地址高字节 0x00
        UINT8_C(0x00), // 起始地址低字节 0x00
        UINT8_C(0x00), // 寄存器数量高字节 0x00
        UINT8_C(0x0A), // 寄存器数量低字节 0x0A（即 10 个）
    };
		
		modbus_crc16(request, sizeof(request));
*/
		
uint16_t modbus_crc16(const uint8_t *data, size_t length);

#endif
