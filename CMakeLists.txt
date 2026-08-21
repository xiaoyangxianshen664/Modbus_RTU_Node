# Modbus_RTU_Node 顶层 CMakeLists
# 阶段 1 补充：protocol/ 协议库 + tests/ 单元测试（gcc + CTest），与 Keil 固件解耦
cmake_minimum_required(VERSION 3.16)
project(Modbus_RTU_Node C)

add_library(modbus_protocol STATIC
    protocol/src/modbus_crc16.c
    protocol/src/modbus_rtu.c
    protocol/src/modbus_registers.c
)

target_include_directories(modbus_protocol
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/protocol/include
)

target_compile_features(modbus_protocol PUBLIC c_std_99)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(modbus_protocol PRIVATE -Wall -Wextra -Wpedantic)
endif()

include(CTest)

if(BUILD_TESTING)
    add_executable(test_crc16 tests/test_crc16.c)
    target_link_libraries(test_crc16 PRIVATE modbus_protocol)

    add_executable(test_modbus_rtu tests/test_modbus_rtu.c)
    target_link_libraries(test_modbus_rtu PRIVATE modbus_protocol)

    add_executable(test_modbus_registers tests/test_modbus_registers.c)
    target_link_libraries(test_modbus_registers PRIVATE modbus_protocol)

    add_executable(test_modbus_protocol tests/test_modbus_protocol.c)
    target_link_libraries(test_modbus_protocol PRIVATE modbus_protocol)

    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(test_crc16 PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(test_modbus_rtu PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(test_modbus_registers PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(test_modbus_protocol PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    add_test(NAME crc16 COMMAND test_crc16)
    add_test(NAME modbus_rtu COMMAND test_modbus_rtu)
    add_test(NAME modbus_registers COMMAND test_modbus_registers)
    add_test(NAME modbus_protocol COMMAND test_modbus_protocol)
endif()
