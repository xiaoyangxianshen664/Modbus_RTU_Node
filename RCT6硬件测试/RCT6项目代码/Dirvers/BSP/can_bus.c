#include "can_bus.h"
#include <string.h>

CAN_HandleTypeDef g_can1;

static uint8_t g_can_rx_data[CAN_BUS_DATA_LENGTH];
static volatile uint8_t g_can_rx_ready;

/**
 * @brief  配置仅接收扩展数据帧0x1314的CAN过滤器
 * @param  无
 * @return HAL_OK表示过滤器配置成功; HAL_ERROR表示配置失败
 * @note   bxCAN的29位扩展ID左移3位, IDE和RTR也参与32位严格匹配.
 * @example result = CAN_Bus_ConfigFilter();
 */
static HAL_StatusTypeDef CAN_Bus_ConfigFilter(void)
{
    CAN_FilterTypeDef filter = {0};
    uint32_t filter_id;

    filter_id = ((uint32_t)CAN_BUS_EXTENDED_ID << 3) |
                CAN_ID_EXT | CAN_RTR_DATA;
    filter.FilterBank = 0U;
    filter.FilterMode = CAN_FILTERMODE_IDMASK;
    filter.FilterScale = CAN_FILTERSCALE_32BIT;
    filter.FilterIdHigh = (filter_id >> 16) & 0xFFFFU;
    filter.FilterIdLow = filter_id & 0xFFFFU;
    filter.FilterMaskIdHigh = 0xFFFFU;
    filter.FilterMaskIdLow = 0xFFFFU;
    filter.FilterFIFOAssignment = CAN_FILTER_FIFO0;
    filter.FilterActivation = ENABLE;
    filter.SlaveStartFilterBank = 14U;

    return HAL_CAN_ConfigFilter(&g_can1, &filter);
}

/**
 * @brief  初始化RCT6板CAN1正常通信模式
 * @param  无
 * @return HAL_OK表示CAN已初始化、启动并打开FIFO0接收中断; 其他值表示失败
 * @note   PA11=RX、PA12=TX; APB1为36MHz, 4分频和9TQ得到1Mbps波特率.
 * @example if (CAN_Bus_Init() != HAL_OK) { Error_Handler(); }
 */
HAL_StatusTypeDef CAN_Bus_Init(void)
{
    GPIO_InitTypeDef gpio = {0};
    HAL_StatusTypeDef result;

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_AFIO_CLK_ENABLE();
    __HAL_RCC_CAN1_CLK_ENABLE();

    gpio.Pin = RCT6_CAN_TX_PIN;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(RCT6_CAN_TX_PORT, &gpio);

    gpio.Pin = RCT6_CAN_RX_PIN;
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(RCT6_CAN_RX_PORT, &gpio);

    g_can1.Instance = CAN1;
    g_can1.Init.Prescaler = 4U;
    g_can1.Init.Mode = CAN_MODE_NORMAL;
    g_can1.Init.SyncJumpWidth = CAN_SJW_1TQ;
    g_can1.Init.TimeSeg1 = CAN_BS1_5TQ;
    g_can1.Init.TimeSeg2 = CAN_BS2_3TQ;
    g_can1.Init.TimeTriggeredMode = DISABLE;
    g_can1.Init.AutoBusOff = ENABLE;
    g_can1.Init.AutoWakeUp = ENABLE;
    g_can1.Init.AutoRetransmission = ENABLE;
    g_can1.Init.ReceiveFifoLocked = DISABLE;
    g_can1.Init.TransmitFifoPriority = DISABLE;
    result = HAL_CAN_Init(&g_can1);
    if (result != HAL_OK)
    {
        return result;
    }

    result = CAN_Bus_ConfigFilter();
    if (result != HAL_OK)
    {
        return result;
    }

    g_can_rx_ready = 0U;
    HAL_NVIC_SetPriority(USB_LP_CAN1_RX0_IRQn, 3U, 0U);
    HAL_NVIC_EnableIRQ(USB_LP_CAN1_RX0_IRQn);

    result = HAL_CAN_Start(&g_can1);
    if (result != HAL_OK)
    {
        return result;
    }

    return HAL_CAN_ActivateNotification(&g_can1,
                                        CAN_IT_RX_FIFO0_MSG_PENDING);
}

/**
 * @brief  发送一帧扩展ID为0x1314的8字节CAN数据
 * @param  data [输入] 8字节发送数据
 * @return HAL_OK表示报文已被总线另一节点应答; HAL_TIMEOUT表示未收到ACK; 其他值表示HAL错误
 * @note   正常模式必须有另一CAN节点应答; 无应答时自动重发, 超时后主动取消邮箱.
 * @example CAN_Bus_Send(data);
 */
HAL_StatusTypeDef CAN_Bus_Send(const uint8_t data[CAN_BUS_DATA_LENGTH])
{
    CAN_TxHeaderTypeDef header = {0};
    HAL_StatusTypeDef result;
    uint32_t mailbox;
    uint32_t start_tick;

    if (data == NULL)
    {
        return HAL_ERROR;
    }

    header.StdId = 0U;
    header.ExtId = CAN_BUS_EXTENDED_ID;
    header.IDE = CAN_ID_EXT;
    header.RTR = CAN_RTR_DATA;
    header.DLC = CAN_BUS_DATA_LENGTH;
    header.TransmitGlobalTime = DISABLE;

    result = HAL_CAN_AddTxMessage(&g_can1, &header, (uint8_t *)data, &mailbox);
    if (result != HAL_OK)
    {
        return result;
    }

    start_tick = HAL_GetTick();
    while (HAL_CAN_IsTxMessagePending(&g_can1, mailbox) != 0U)
    {
        if ((HAL_GetTick() - start_tick) >= CAN_BUS_TX_TIMEOUT_MS)
        {
            (void)HAL_CAN_AbortTxRequest(&g_can1, mailbox);
            return HAL_TIMEOUT;
        }
    }

    return HAL_OK;
}

/**
 * @brief  取出CAN接收中断保存的一帧数据
 * @param  data [输出] 8字节接收缓冲区
 * @return 1表示取得新报文; 0表示当前无新报文或参数无效
 * @note   复制期间短暂屏蔽中断, 防止回调与主循环同时访问缓冲区.
 * @example if (CAN_Bus_Read(data) != 0U) { ... }
 */
uint8_t CAN_Bus_Read(uint8_t data[CAN_BUS_DATA_LENGTH])
{
    uint32_t primask;

    if ((data == NULL) || (g_can_rx_ready == 0U))
    {
        return 0U;
    }

    primask = __get_PRIMASK();
    __disable_irq();
    memcpy(data, g_can_rx_data, CAN_BUS_DATA_LENGTH);
    g_can_rx_ready = 0U;
    if (primask == 0U)
    {
        __enable_irq();
    }

    return 1U;
}

/**
 * @brief  转交CAN1 FIFO0共享中断给HAL
 * @param  无
 * @return 无
 * @note   STM32F103的中断向量名为USB_LP_CAN1_RX0_IRQHandler, 与USB低优先级共享.
 * @example 在USB_LP_CAN1_RX0_IRQHandler()中调用.
 */
void CAN_Bus_RX0_IRQHandler(void)
{
    HAL_CAN_IRQHandler(&g_can1);
}

/**
 * @brief  CAN1 FIFO0收到报文后的HAL回调
 * @param  hcan [输入] 触发回调的CAN句柄
 * @return 无
 * @note   过滤器已筛选0x1314扩展数据帧; 回调再次检查帧头后保存8字节数据.
 * @example 由HAL_CAN_IRQHandler()内部自动调用.
 */
void HAL_CAN_RxFifo0MsgPendingCallback(CAN_HandleTypeDef *hcan)
{
    CAN_RxHeaderTypeDef header = {0};
    uint8_t received[CAN_BUS_DATA_LENGTH];

    if ((hcan == &g_can1) &&
        (HAL_CAN_GetRxMessage(hcan, CAN_RX_FIFO0, &header, received) == HAL_OK) &&
        (header.IDE == CAN_ID_EXT) &&
        (header.RTR == CAN_RTR_DATA) &&
        (header.ExtId == CAN_BUS_EXTENDED_ID) &&
        (header.DLC == CAN_BUS_DATA_LENGTH))
    {
        memcpy(g_can_rx_data, received, CAN_BUS_DATA_LENGTH);
        g_can_rx_ready = 1U;
    }
}
