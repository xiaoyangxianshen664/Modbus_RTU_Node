#include "project_app.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
#include "modbus_rtu.h"
#include "modbus_registers.h"
#include "modbus_transport.h"
#include "config_storage.h"
#include "iwdg.h"
#include "rs485.h"
#include "adc_multi.h"
#include "rtc_test.h"
#include "ff.h"
#include "printf.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define MODBUS_TASK_PRIORITY    3U
#define MONITOR_TASK_PRIORITY   4U
#define ACQUIRE_TASK_PRIORITY   2U
#define LOG_TASK_PRIORITY       1U
#define MODBUS_TASK_STACK       384U
#define MONITOR_TASK_STACK      128U
#define ACQUIRE_TASK_STACK      384U
#define LOG_TASK_STACK          384U
#define LOG_QUEUE_LENGTH        8U

#define NTC_R25_OHM             10000.0f
#define NTC_FIXED_OHM           10000.0f
#define NTC_BETA                3950.0f
#define NTC_T25_K               298.15f

typedef struct
{
    RTC_TimeTypeDef time;
    uint16_t pot_raw;
    uint16_t pot_x10;
    uint16_t ntc_raw;
    uint16_t temp_x10;
    uint16_t alarm;
} log_record_t;

static TaskHandle_t g_modbus_task_handle;
static SemaphoreHandle_t g_register_mutex;
static QueueHandle_t g_log_queue;
static modbus_registers_t g_registers;
static FATFS g_fatfs;
static volatile uint8_t g_log_ready;
static volatile uint32_t g_reset_cause;
static volatile uint32_t g_acquire_heartbeat;
static volatile uint32_t g_log_heartbeat;
static volatile uint32_t g_log_errors;
static volatile uint32_t g_health_errors;

static void modbus_task(void *argument);
static void monitor_task(void *argument);
static void acquire_task(void *argument);
static void log_task(void *argument);

/** 启动早期保存上一次复位原因，随后清除RCC标志。 */
void project_diagnostics_init(void)
{
    g_reset_cause = 0U;
    if (__HAL_RCC_GET_FLAG(RCC_FLAG_IWDGRST) != RESET) g_reset_cause |= 1U << 0;
    if (__HAL_RCC_GET_FLAG(RCC_FLAG_WWDGRST) != RESET) g_reset_cause |= 1U << 1;
    if (__HAL_RCC_GET_FLAG(RCC_FLAG_SFTRST) != RESET)  g_reset_cause |= 1U << 2;
    if (__HAL_RCC_GET_FLAG(RCC_FLAG_PINRST) != RESET)  g_reset_cause |= 1U << 3;
    if (__HAL_RCC_GET_FLAG(RCC_FLAG_PORRST) != RESET)  g_reset_cause |= 1U << 4;
    if (__HAL_RCC_GET_FLAG(RCC_FLAG_LPWRRST) != RESET) g_reset_cause |= 1U << 6;
    __HAL_RCC_CLEAR_RESET_FLAGS();
}

static uint16_t ntc_to_temp_x10(uint16_t raw)
{
    float resistance;
    float kelvin;
    float value;

    if ((raw == 0U) || (raw >= 4095U)) return 0U;
    resistance = NTC_FIXED_OHM * (float)raw / (float)(4095U - raw);
    kelvin = 1.0f / ((1.0f / NTC_T25_K) +
                     (logf(resistance / NTC_R25_OHM) / NTC_BETA));
    value = (kelvin - 273.15f) * 10.0f;
    if (value < 0.0f) value = 0.0f;
    if (value > 800.0f) value = 800.0f;
    return (uint16_t)value;
}

/** 创建四个业务任务并启动调度器；正常情况下本函数不会返回。 */
void project_app_start(void)
{
    modbus_registers_init(&g_registers);
    (void)config_storage_load_holding(g_registers.holding);

    g_register_mutex = xSemaphoreCreateMutex();
    g_log_queue = xQueueCreate(LOG_QUEUE_LENGTH, sizeof(log_record_t));
    configASSERT(g_register_mutex != NULL);
    configASSERT(g_log_queue != NULL);
    configASSERT(xTaskCreate(modbus_task, "Modbus", MODBUS_TASK_STACK, NULL,
                             MODBUS_TASK_PRIORITY, &g_modbus_task_handle) == pdPASS);
    configASSERT(xTaskCreate(monitor_task, "Monitor", MONITOR_TASK_STACK, NULL,
                             MONITOR_TASK_PRIORITY, NULL) == pdPASS);
    configASSERT(xTaskCreate(acquire_task, "Acquire", ACQUIRE_TASK_STACK, NULL,
                             ACQUIRE_TASK_PRIORITY, NULL) == pdPASS);
    configASSERT(xTaskCreate(log_task, "Log", LOG_TASK_STACK, NULL,
                             LOG_TASK_PRIORITY, NULL) == pdPASS);
    vTaskStartScheduler();
    for (;;) {}
}

/** TIM4确认T3.5后从中断上下文唤醒Modbus任务。 */
void project_modbus_notify_from_isr(void)
{
    BaseType_t higher_task_woken = pdFALSE;
    if (g_modbus_task_handle != NULL)
    {
        vTaskNotifyGiveFromISR(g_modbus_task_handle, &higher_task_woken);
        portYIELD_FROM_ISR(higher_task_woken);
    }
}

static void modbus_task(void *argument)
{
    uint8_t request[MODBUS_MAX_ADU_SIZE];
    uint8_t response[MODBUS_MAX_ADU_SIZE];
    uint16_t holding_before[MODBUS_HOLDING_REGISTER_COUNT];
    modbus_registers_t snapshot;
    (void)argument;

    for (;;)
    {
        uint16_t request_length;
        size_t response_length = 0U;
        uint8_t valid;
        modbus_rtu_status_t status;

        if (modbus_transport_frame_ready() == 0U)
        {
            (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        }
        taskENTER_CRITICAL();
        request_length = RS485_ReadFrame(request, sizeof(request));
        valid = modbus_transport_frame_valid();
        modbus_transport_frame_clear();
        taskEXIT_CRITICAL();
        if ((request_length == 0U) || (valid == 0U)) continue;

        xSemaphoreTake(g_register_mutex, portMAX_DELAY);
        snapshot = g_registers;
        memcpy(holding_before, snapshot.holding, sizeof(holding_before));
        xSemaphoreGive(g_register_mutex);

        status = modbus_rtu_process_request(request, request_length, &snapshot,
                                            response, sizeof(response),
                                            &response_length);
        if (memcmp(holding_before, snapshot.holding, sizeof(holding_before)) != 0)
        {
            xSemaphoreTake(g_register_mutex, portMAX_DELAY);
            memcpy(g_registers.holding, snapshot.holding,
                   sizeof(g_registers.holding));
            xSemaphoreGive(g_register_mutex);
            (void)config_storage_save_holding(snapshot.holding);
        }
        if ((status == MODBUS_RTU_RESPONSE_READY) && (response_length > 0U))
        {
            (void)RS485_Send(response, (uint16_t)response_length);
        }
    }
}

static void monitor_task(void *argument)
{
    TickType_t last_wake = xTaskGetTickCount();
    uint32_t last_acquire = 0U;
    uint32_t last_log = 0U;
    uint8_t acquire_stall = 0U;
    uint8_t log_stall = 0U;
    (void)argument;

    for (;;)
    {
        uint8_t healthy = 1U;
        acquire_stall = (g_acquire_heartbeat == last_acquire) ?
                        (uint8_t)(acquire_stall + 1U) : 0U;
        log_stall = ((g_log_heartbeat == last_log) &&
                     (uxQueueMessagesWaiting(g_log_queue) > 0U)) ?
                    (uint8_t)(log_stall + 1U) : 0U;
        last_acquire = g_acquire_heartbeat;
        last_log = g_log_heartbeat;
        if (acquire_stall >= 20U) healthy = 0U;
        if ((g_log_ready != 0U) && (log_stall >= 20U)) healthy = 0U;
        if (healthy != 0U) Project_IWDG_Feed(); else g_health_errors++;

        xSemaphoreTake(g_register_mutex, portMAX_DELAY);
        (void)modbus_registers_update_input(&g_registers, 8U, (uint16_t)g_reset_cause);
        (void)modbus_registers_update_input(&g_registers, 9U, (uint16_t)g_health_errors);
        (void)modbus_registers_update_input(&g_registers, 10U, (uint16_t)g_log_errors);
        (void)modbus_registers_update_input(&g_registers, 11U, healthy);
        xSemaphoreGive(g_register_mutex);
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(100U));
    }
}

static void acquire_task(void *argument)
{
    (void)argument;
    for (;;)
    {
        uint16_t pot_raw;
        uint16_t ntc_raw;
        uint16_t pot_x10;
        uint16_t temp_x10;
        uint16_t alarm;
        uint16_t period;
        uint16_t low;
        uint16_t high;
        uint16_t log_enabled;
        log_record_t record;

        if (ADC_Multi_GetAverage(&pot_raw, &ntc_raw) == 0U)
        {
            vTaskDelay(pdMS_TO_TICKS(10U));
            continue;
        }
        pot_x10 = (uint16_t)(((uint32_t)pot_raw * 1000U) / 4095U);
        temp_x10 = ntc_to_temp_x10(ntc_raw);

        xSemaphoreTake(g_register_mutex, portMAX_DELAY);
        period = g_registers.holding[0];
        low = g_registers.holding[1];
        high = g_registers.holding[2];
        log_enabled = g_registers.holding[3];
        alarm = (low > high) ? 3U : ((temp_x10 < low) ? 1U :
                                    ((temp_x10 > high) ? 2U : 0U));
        (void)modbus_registers_update_input(&g_registers, 0U, pot_raw);
        (void)modbus_registers_update_input(&g_registers, 1U, pot_x10);
        (void)modbus_registers_update_input(&g_registers, 2U, ntc_raw);
        (void)modbus_registers_update_input(&g_registers, 3U, temp_x10);
        (void)modbus_registers_update_input(&g_registers, 4U, alarm);
        xSemaphoreGive(g_register_mutex);

        myprintf("ADC pot_raw=%u, pot=%u.%u%%, ntc_raw=%u, temp=%u.%uC, alarm=%u\r\n",
                 pot_raw, pot_x10 / 10U, pot_x10 % 10U, ntc_raw,
                 temp_x10 / 10U, temp_x10 % 10U, alarm);
        memset(&record, 0, sizeof(record));
        (void)RTC_Test_GetTime(&record.time);
        record.pot_raw = pot_raw;
        record.pot_x10 = pot_x10;
        record.ntc_raw = ntc_raw;
        record.temp_x10 = temp_x10;
        record.alarm = alarm;
        if ((log_enabled != 0U) &&
            (xQueueSend(g_log_queue, &record, 0U) != pdPASS))
        {
            g_log_errors++;
        }
        g_acquire_heartbeat++;
        vTaskDelay(pdMS_TO_TICKS((period == 0U) ? 1000U : period));
    }
}

static void log_task(void *argument)
{
    log_record_t record;
    uint8_t header_checked = 0U;
    (void)argument;

    for (;;)
    {
        FIL file;
        if (xQueueReceive(g_log_queue, &record, portMAX_DELAY) != pdPASS) continue;
        if ((g_log_ready != 0U) &&
            (f_open(&file, "0:/measure.csv", FA_OPEN_APPEND | FA_WRITE) == FR_OK))
        {
            if ((header_checked == 0U) && (f_size(&file) == 0U))
            {
                (void)f_printf(&file,
                    "date,time,pot_raw,pot_x10,ntc_raw,temp_x10,alarm\r\n");
            }
            header_checked = 1U;
            if (f_printf(&file,
                "2026-09-07,%02u:%02u:%02u,%u,%u,%u,%u,%u\r\n",
                record.time.Hours, record.time.Minutes, record.time.Seconds,
                record.pot_raw, record.pot_x10, record.ntc_raw,
                record.temp_x10, record.alarm) < 0)
            {
                g_log_errors++;
            }
            if (f_close(&file) != FR_OK) g_log_errors++;
        }
        else if (g_log_ready != 0U)
        {
            g_log_errors++;
        }
        g_log_heartbeat++;
    }
}

/** 主函数完成RTC初始化后调用，用于挂载已经验证过的FatFs磁盘。 */
void project_log_storage_mount(void)
{
    FRESULT result = f_mount(&g_fatfs, "0:", 1U);
    g_log_ready = (result == FR_OK) ? 1U : 0U;
    myprintf("FatFs mount %s, result=%u\r\n",
             (g_log_ready != 0U) ? "PASS" : "FAIL", (unsigned int)result);
}
