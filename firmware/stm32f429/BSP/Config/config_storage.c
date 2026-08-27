#include "./Config/config_storage.h"
#include "./SPI/SPI_Flash.h"
#include "modbus_registers.h"
#include "modbus_crc16.h"
#include <stddef.h>
#include <string.h>


/*配置记录固定标识*/
#define CONFIG_RECORD_MAGIC   0x43464731UL 	//用于判断当前 Flash 内容是不是本配置模块写入的记录。

/*配置记录格式版本*/
#define CONFIG_RECORD_VERSION 1U					  //将来如果修改记录结构，可以增加版本号，启动时拒绝读取旧格式或不兼容格式。


/*
 * Flash 中保存的一条完整配置记录。
 *
 * 一条记录不仅保存 holding 数组，
 * 还保存识别信息、版本、序号和 CRC。
 */
typedef struct
{
    uint32_t magic;																		/* 固定标识，用于判断记录来源 */
    uint16_t version;																	/* 记录格式版本 */
    uint16_t reserved;																/* 预留字段，便于以后扩展 */
		uint32_t sequence;																/* 配置序号，数值越大表示记录越新*/
																											/*比如A区和B区都有一个对应的sequence，如果A区对应为2，B区对应为1，说明A区正在用，此时覆盖写入B区 */
    uint16_t holding[MODBUS_HOLDING_REGISTER_COUNT];	/* 4 个 Modbus 保持寄存器 */
    uint16_t crc;																			/* 对 crc 字段之前的数据计算 CRC16 */
} config_record_t;

static uint8_t  g_config_storage_ready;								/*配置存储是否可用：1：W25Q256 JEDEC ID 匹配；0：Flash 不可用。*/
static uint8_t  g_active_slot;												/*当前已经确认有效的槽位：槽 A 有效时值为0， 槽 B 有效时值为1*/
																											/*保存新配置时，会写入另一个槽，这样当前有效槽可以作为掉电恢复备份。*/
static uint32_t g_sequence;														/*当前已经加载的最新配置序号，每保存一次新配置，对应槽序号加 1，启动时通过比较两个槽的 sequence 判断谁是最新记录*/


/**
 * @brief  初始化配置存储模块
 *
 * 初始化 W25Q256，并读取 JEDEC ID 确认外部 Flash 芯片是否存在且型号正确。
 * 同时清零当前配置槽和配置序号，后续由 config_storage_load_holding()
 * 从 Flash 中恢复实际配置状态。
 *
 * @param  无
 * @return 无
 *
 * @note   配置模块只使用低地址区域，采用 24 位地址接口即可覆盖两个配置扇区。
 */
void config_storage_init(void)																							//此时只是完成 Flash 初始化，还没有读取配置内容。
{
    SPI_Flash_Init();																												// 初始化 SPI5；
    g_config_storage_ready = (uint8_t)(SPI_Flash_ReadID() == FLASH_CHIP_ID); // 读取 W25Q256 的 JEDEC ID；确认芯片是否为 W25Q256；
    g_active_slot = 0U;																											 // 先把当前槽状态清零
    g_sequence = 0U;																											   // 初始序号为 0，之后更会新该值
}




/**
 * @brief  查询外部 Flash 配置存储是否可用
 *
 * @param  无
 *
 * @return 1=W25Q256 已识别，可以进行配置读写
 *         0=Flash 不可用
 */
uint8_t config_storage_is_ready(void)
{
    return g_config_storage_ready;
}



/*
	config_record_crc() 用来判断一条配置记录在 Flash 中保存后有没有损坏。
	为什么需要它：例如原来保存的是 holding[2] = 500
	如果 存在Flash 中某一位被破坏，读取出来变成：holding[2] = 501
	那么重新计算出的 CRC 就不会再等于原来保存的 CRC：保存的 CRC：0x1234，重新计算的 CRC：0x8A72
	系统就知道这条记录不可信，不会加载它，而是尝试使用另一个槽。
	
	调用链：
	config_record_crc() 只负责： 计算 CRC
	config_record_is_valid() 负责综合判断：magic 是否正确，version 是否正确，CRC 是否正确，holding 参数是否在合法范围
	config_record_crc()
        ↓
	生成或重新计算 CRC
        ↓
	config_record_is_valid()
        ↓
	判断整条配置记录是否可用
 */
static uint16_t config_record_crc(const config_record_t *record)
{
		/*第一个参数： 待计算的数据数组首地址
			第二个参数： 待计算的length - 数组的字节数
	*/
    return modbus_crc16((const uint8_t *)record,offsetof(config_record_t, crc));
		
	  /*offsetof，意思就是：计算 crc 这个成员，在 config_record_t 结构体中的字节偏移量。
		typedef struct
		{
				uint32_t magic;       // 4 字节
				uint16_t version;     // 2 字节
				uint16_t reserved;    // 2 字节
				uint32_t sequence;    // 4 字节
				uint16_t holding[4];  // 8 字节
				uint16_t crc;         // 2 字节
		} config_record_t;
		offsetof(config_record_t, crc) 结果就是：20 字节
	  实际上就是：modbus_crc16((const uint8_t *)record,20）;
		因此函数只计算：magic，version，reserved，sequence，holding[4]等数据的CRC值
	 */
                        
}


/*
 * 从 Flash 读出一条配置后，先检查它是不是“可信的有效配置”，它不负责读取 Flash，也不负责修改配置，它只是做“验货”。
 *
 * 检查顺序：
 *   1. magic 是否正确；
 *   2. version 是否兼容；
 *   3. CRC 是否正确；
 *   4. holding 参数是否符合业务范围。
 *
 * 返回值：
 *   1：记录有效；
 *   0：记录损坏、格式不匹配或参数非法。
 */
static uint8_t config_record_is_valid(const config_record_t *record)
{
    if (record->magic != CONFIG_RECORD_MAGIC ||										//magic存标识码0x43464731UL
        record->version != CONFIG_RECORD_VERSION ||								//version存版本号
				record->crc != config_record_crc(record))									//CRC 是否正确；
        return 0U;

    if (record->holding[0] == 0U ||																//holding 参数是否符合业务范围。
        record->holding[1] < 200U || record->holding[1] > 800U ||
        record->holding[2] < 200U || record->holding[2] > 800U ||
        record->holding[3] > 1U)
        return 0U;

    return 1U;
}



/**
 * @brief  从 Flash 的 A/B 两个配置槽中加载最新的有效配置
 *
 * 分别读取 Slot A 和 Slot B，并通过 config_record_is_valid()
 * 检查配置记录是否有效。
 *
 * 如果两个槽都有效，则比较 sequence，选择 sequence 较大的最新配置。
 * 如果只有一个槽有效，则直接使用该槽。
 * 如果两个槽都无效，则加载失败，调用者继续使用默认配置。
 *
 * @param  参数 holding 是一个指针，指向调用者提供的数组。
 *         实际调用：config_storage_load_holding(g_registers.holding);也就是：holding 指向 g_registers.holding[0]
 *
 * @return 1：成功加载有效配置
 *         0：没有有效配置、参数为空或 Flash 不可用
 *
 * @note   加载成功后，同时更新 g_sequence 和 g_active_slot，
 *         为后续保存配置时选择备用槽提供依据。
 */
uint8_t config_storage_load_holding(uint16_t *holding)
{		
		/* 函数局部变量 */
    config_record_t slot_a;																		//用于保存从flash的槽 A 读出来的记录					
    config_record_t slot_b;																		//用于保存从flash的槽 B 读出来的记录
	
		/*创建一个结构体指针 selected，它不是保存记录本身，而是指向最终选中槽位*/
    const config_record_t *selected = NULL;										 //开始时还没有选择任何槽，所以设置为：
		
		
		/*返回 0 不代表系统停止运行；调用者之前已经设置了默认 holding 值。*/
    if (holding == NULL || g_config_storage_ready == 0U)			// 条件1：表示调用者没有提供输出数组，就会向空地址写数据，可能导致程序异常，因此return0
        return 0U;																						// 条件2：g_config_storage_ready == 0U  初始化时没有识别到 W25Q256，说明外部 Flash 不可用
		
		
    SPI_Flash_BufferRead((uint8_t *)&slot_a,CONFIG_FLASH_SLOT_A_ADDR,sizeof(slot_a));//从 Flash 的 A 槽读取一条完整配置记录到 slot_a
    SPI_Flash_BufferRead((uint8_t *)&slot_b,CONFIG_FLASH_SLOT_B_ADDR,sizeof(slot_b));//从 Flash 的 B 槽读取一条完整配置记录到 slot_b
	
		
    if (config_record_is_valid(&slot_a) != 0U)								//有效时，config_record_is_valid会返回1
        selected = &slot_a;																		//如果槽 A 有效，就暂时把它作为候选配置，注意这里是“暂时选择”，后面还要继续判断槽 B。
		
		/*
		slot_b.sequence 为访问结构体slot_b的成员sequence
		selected->sequence为访问上一次selected指向哪一个结构体对应的结构体成员sequence，此处我们默认上电是A，所以是slot_a.sequence
		
		*/
		
    if (config_record_is_valid(&slot_b) != 0U &&									 //条件1：config_record_is_valid(&slot_b) != 0U 槽 B有效
        (selected == NULL || slot_b.sequence > selected->sequence))//条件2：并且：当前还没有选中任何记录 或者槽 B 的序号比当前记录更大
        selected = &slot_b;																				 //那么就把槽 B 选为当前有效记录。

		
    if (selected == NULL)																					// A/B 两个槽都无效，没有可恢复的配置 
        return 0U;
		
		
    memcpy(holding, selected->holding, sizeof(selected->holding));// 将选中的flash对应槽从配置复制到运行时保持寄存器数组holding[] 
																																	//第一个参数：holding（为传入的形参）实际是：g_registers.holding
																																	//第二个参数：表示被选中记录中的 holding 数组。
    g_sequence = selected->sequence;															//恢复当前配置序号
		
		
		/*
		这句利用比较结果赋值。
		如果选中槽 B：selected == &slot_b，结果为真：selected == &slot_b返回1 ，此时g_active_slot = 1;
		如果选中槽 B：selected == &slot_b，结果为假：selected == &slot_b返回0， 此时g_active_slot = 0;
		这个变量会影响下一次保存：当前 A → 下一次写 B ，当前 B → 下一次写 A。
		*/
    g_active_slot = (uint8_t)(selected == &slot_b);
    return 1U;																										// 记录当前有效槽
}

/**
 * @brief  将保持寄存器配置保存到 W25Q256 的备用槽
 * @param  holding 输入数组，包含 4 个已经通过协议校验的保持寄存器值
 * @return 1=保存并回读校验成功，0=参数非法、Flash 不可用或校验失败
 * @note   每次保存只擦除目标槽所在扇区，另一槽保留为掉电恢复备份。
 * @example uint8_t config_storage_save_holding(const uint16_t *holding);
						modbus_registers_t g_registers;
 */
uint8_t config_storage_save_holding(const uint16_t *holding)
{
    config_record_t record;																						//准备写入 Flash 的新配置。
    config_record_t verify;																						//从 Flash 写完以后重新读出来的配置，用来检查到底写成功没有。
		const uint32_t target_addr = g_active_slot == 0U									//g_active_slot=0 ，第一次上电读的是默认配置，g_active_slot值一直是0，没变化
                               ? CONFIG_FLASH_SLOT_B_ADDR							//地址B0x002000U
                               : CONFIG_FLASH_SLOT_A_ADDR;						//地址A0x001000U

    if (holding == NULL || g_config_storage_ready == 0U)							//条件1：说明你传进来的配置数组holding[]不存在。
        return 0U;																										//条件2：说明 Flash 根本没初始化成功。
		
		/*
				holding[0]：不能为 0		 （采样周期）
				holding[1]：范围 200~800 （20°到80°）
				holding[2]：范围 200~800	（20°到80°）
				holding[3]：只能是 0 或 1 （SD卡日志）
		*/
    if (holding[0] == 0U ||																						//再次检查待保存到flash的默认配置是否合法
        holding[1] < 200U || holding[1] > 800U ||
        holding[2] < 200U || holding[2] > 800U ||
        holding[3] > 1U)
        return 0U;
		
		/*
		 作用1：防止结构体中存在未初始化数据；
		 作用2：让 reserved 和编译器可能加入的填充字节保持确定值，避免影响 CRC。
		*/
    memset(&record, 0, sizeof(record));														//这句会把整个结构体全部清零：
    record.magic = CONFIG_RECORD_MAGIC;														//写入记录标识：0x43464731UL
    record.version = CONFIG_RECORD_VERSION;												//写入记录版本：1
    record.sequence = g_sequence + 1U;														//第一次修改holding[]后进入config_storage_save_holding，record.sequence由0变为1
		
		
    memcpy(record.holding, holding, sizeof(record.holding));			//复制 4 个 holding
    record.crc = config_record_crc(&record);											//计算并填写 CRC，最终形成一条完整记录：记录头信息 + sequence + holding 配置 + CRC
	
    SPI_Flash_EraseSector(target_addr);														//擦除目标扇区，我们项目第一次是B槽0x002000 ~ 0x002FFF，第二片扇区
    SPI_Flash_BufferWrite((uint8_t *)&record, target_addr, sizeof(record)); //把默认或当前 holding 配置写入记录：
    SPI_Flash_BufferRead((uint8_t *)&verify, target_addr, sizeof(verify));	//回读刚才写入的记录

    if (memcmp(&record, &verify, sizeof(record)) != 0 ||				//比较写入前后内容如果不相等return 0U
        config_record_is_valid(&verify) == 0U)									//再次检查读回记录是否合法
        return 0U;

    g_sequence = record.sequence;																//保存成功后更新序号，record.sequence=1赋值给g_sequence，g_sequence由0变为1
		
		/*
		 这是一个取反切换：
		 g_active_slot = 0 → 1
		 g_active_slot = 1 → 0
		*/
    g_active_slot = (uint8_t)(g_active_slot == 0U);							//切换当前有效槽，原本默认配置g_active_slot = 0，等式成立，因此g_active_slot = 1
    return 1U;																									//返回保存成功
}
