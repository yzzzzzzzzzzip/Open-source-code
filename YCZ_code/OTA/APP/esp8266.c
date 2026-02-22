#include "esp8266.h"

// ************************ 定义全局变量（存储解析后的服务器下发数据）************************ 
char uart1_tx_buffer[UART1_TX_BUF_MAX_LEN] = {0};  
char uart2_tx_buffer[UART2_TX_BUF_MAX_LEN] = {0};
char tx_buffer[UART1_TX_BUF_MAX_LEN]= {0};   // 串口1发送缓冲区（拼接AT指令）
char subscribe[256] = {0};// 容纳拼接后的完整订阅主题

// 全局固件信息变量
OTA_Firmware_InfoTypeDef g_ota_firmware_info = {
    .current_version = "1.0",  // 初始化当前版本为1.0
    .target_version = "",      // 目标版本默认空
    .tid = 0,                  // 固件ID默认0
    .size = 0,                 // 固件大小默认0
    .md5 = "",                 // MD5默认空
    .code = 0,                 // 响应码默认0
    .parse_ok = 0,             // 解析标志默认0
	.crc32  = 0
};

// 全局变量：当前OTA接收模式（默认无接收）
OTA_Recv_ModeTypeDef g_ota_recv_mode = OTA_RECV_MODE_NONE;

// 全局变量：存储 OneNET 下发的属性数据，初始化为默认值，避免脏数据
OneNET_Property_Set_t g_OneNET_Property_Data = {
    .light_b = false,
    .light_back = false,
    .light_f = false,
		.sun = false,
    .is_valid = false,
    .is_updated = false
};
// ******************************************************************************************

// 全局接收帧变量定义（esp8266.c 中）
struct STRUCT_USARTx_Fram strEsp8266_Fram_Record = {
    // 1. 接收缓冲区：初始化为全 0（字符串结束符，避免乱码）
    .Data_RX_BUF = {0},

    // 2. 匿名联合体：通过位段 InfBit 初始化（更直观，精准对应字段）
    .InfBit = {
        .FramLength = 0,          // 初始接收长度为 0
        .FramFinishFlag = 0       // 初始接收未完成
    }

};



// UART1 接收中断回调（极简版：仅接收+重启中断）
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART1)
    {
        // 1. 安全判断：FramLength未超过15位上限，且缓冲区未满
        if (strEsp8266_Fram_Record.InfBit.FramLength < ( (1<<15) - 1 ) &&  
            strEsp8266_Fram_Record.InfBit.FramLength < RX_BUF_MAX_LEN - 1)
        {
            // 先存数据，再计数+1（核心逻辑保留）
            strEsp8266_Fram_Record.InfBit.FramLength++;

            // 2. 帧结束判断（分模式适配）
            switch (g_ota_recv_mode)
            {
                // 2.1 版本检查模式（JSON响应，结束标志：最后一个字节是}）
                case OTA_RECV_MODE_CHECK_VER:
                    if (strEsp8266_Fram_Record.InfBit.FramLength >= 1)
                    {
                        uint16_t len = strEsp8266_Fram_Record.InfBit.FramLength;
                        // 检测JSON结束符}（适配你的响应末尾}）
                        if (strEsp8266_Fram_Record.Data_RX_BUF[len-1] == '}')
                        {
                            // 二次确认：确保是JSON的最后一个}（可选，增强鲁棒性）
                            char *json_start = strstr(strEsp8266_Fram_Record.Data_RX_BUF, "{");
                            if (json_start != NULL)
                            {
                                strEsp8266_Fram_Record.InfBit.FramFinishFlag = 1;
                            }
                        }
                    }
                    break;

                // 2.2 固件下载模式（二进制响应，保留原\r\n\r\n判断）
                case OTA_RECV_MODE_DOWNLOAD:
                    if (strEsp8266_Fram_Record.InfBit.FramLength >= 4)
                    {
                        uint16_t len = strEsp8266_Fram_Record.InfBit.FramLength;
                        if (strEsp8266_Fram_Record.Data_RX_BUF[len-4] == '\r' &&
                            strEsp8266_Fram_Record.Data_RX_BUF[len-3] == '\n' &&
                            strEsp8266_Fram_Record.Data_RX_BUF[len-2] == '\r' &&
                            strEsp8266_Fram_Record.Data_RX_BUF[len-1] == '\n')
                        {
                            strEsp8266_Fram_Record.InfBit.FramFinishFlag = 1;
                        }
                    }
                    break;

                // 2.3 无模式：不主动结束，靠超时/缓冲区满
                default:
                    break;
            }
        }
        // 3. 缓冲区满/长度溢出：强制结束接收
        else
        {
            strEsp8266_Fram_Record.InfBit.FramFinishFlag = 1;
            HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);
        }

        // 4. 重新开启中断接收下一个字节（关键）
        HAL_UART_Receive_IT(&huart1, 
                            (uint8_t*)&strEsp8266_Fram_Record.Data_RX_BUF[strEsp8266_Fram_Record.InfBit.FramLength], 
                            1);
    }
}

void UART2_Debug_Print(const char *fmt, ...)
{
    // 1. 合法性检查：格式化字符串不能为空，避免空指针操作
    if (fmt == NULL)
    {
        return;
    }

    // 2. 清空串口2发送缓冲区（避免上一次打印数据残留，导致乱码）
    memset(uart2_tx_buffer, 0, UART2_TX_BUF_MAX_LEN);

    // 3. 处理可变参数（实现格式化输出，替代 snprintf，支持多参数）
    va_list args;          // 定义可变参数列表变量
    va_start(args, fmt);   // 初始化可变参数列表，指向 fmt 后的第一个参数
    // 格式化填充缓冲区：vsnprintf 支持可变参数，更适合封装格式化函数
    vsnprintf(uart2_tx_buffer, UART2_TX_BUF_MAX_LEN - 1, fmt, args);  // 预留1字节防止溢出
    va_end(args);          // 结束可变参数列表处理，释放资源

    // 4. 自动添加换行符 \r\n（符合串口调试习惯，无需手动传入）
    strcat(uart2_tx_buffer, "\r\n");

    // 5. HAL 库标准阻塞发送，打印到串口2
    HAL_UART_Transmit(&huart2, (uint8_t*)uart2_tx_buffer, strlen(uart2_tx_buffer), UART_TIMEOUT_MS);
}

/**
  * @brief  向 ESP8266 发送 AT 指令并校验响应
  * @param  cmd：待发送的 AT 指令字符串（不可带 \r\n，函数内部自动添加）
  * @param  reply1：期待的响应字符串1（NULL 表示不校验该响应）
  * @param  reply2：期待的响应字符串2（NULL 表示不校验该响应，与 reply1 为或逻辑）
  * @param  waittime：等待 ESP8266 响应的最大时间（单位：ms）
  * @retval bool：true=指令发送成功且匹配到目标响应，false=发送失败或响应不匹配
  */
// 全局变量保持不变（strEsp8266_Fram_Record、uart1_tx_buffer 等）
bool ESP8266_Cmd(char *cmd, char *reply1, char *reply2, uint32_t waittime)
{
    // 1. 合法性检查：cmd 不能为空（避免空指针操作）
    if (cmd == NULL)
    {
        UART2_Debug_Print("ESP8266 Cmd Error: Cmd is NULL");
        return false;
    }

    // 2. 完全重置接收状态（关键：解决连续调用无效，清除所有残留状态）
    memset(strEsp8266_Fram_Record.Data_RX_BUF, 0, RX_BUF_MAX_LEN);
    memset(uart1_tx_buffer, 0, UART1_TX_BUF_MAX_LEN);
    strEsp8266_Fram_Record.InfBit.FramLength = 0;
    strEsp8266_Fram_Record.InfBit.FramFinishFlag = 0;
    strEsp8266_Fram_Record.InfAll = 0;

    // 3. 串口中断闭环处理（确保接收指针复位，无挂起状态，解决截断核心）
    HAL_UART_AbortReceive_IT(&huart1); // 关闭当前中断接收，清除挂起
    HAL_Delay(100); // 短延时，确保中断完全关闭

    // 重新开启中断接收（每次接收1字节，中断回调中需重新开启，确保持续接收）
    if (HAL_UART_Receive_IT(&huart1, 
                            (uint8_t*)&strEsp8266_Fram_Record.Data_RX_BUF[0], 
                            1) != HAL_OK)
    {
        UART2_Debug_Print("ESP8266 UART1 Interrupt Start Failed");
        return false;
    }

    // 4. 格式化拼接 AT 指令（添加\r\n，符合 ESP8266 指令格式）
    snprintf(uart1_tx_buffer, UART1_TX_BUF_MAX_LEN, "%s\r\n", cmd);
    uint16_t cmd_len = strlen(uart1_tx_buffer); // 记录指令长度，避免发送不完整
		
		UART2_Debug_Print("send order:%s",uart1_tx_buffer);
		
    // 5. HAL 库标准串口1阻塞发送 AT 指令（优化发送超时，确保指令完整发送）
    if (HAL_UART_Transmit(&huart1, 
                          (uint8_t*)uart1_tx_buffer, 
                          cmd_len, 
                          UART_TIMEOUT_MS * 2) != HAL_OK) // 延长发送超时，应对长指令
    {
        UART2_Debug_Print("ESP8266 UART1 Transmit Failed");
        // 发送失败后，重置中断接收，避免影响后续调用
        HAL_UART_AbortReceive_IT(&huart1);
        return false;
    }

    // 6. 指令发送后短缓冲（100ms 足够，避免占用有效等待时间，解决截断关键）
    // 原代码是 HAL_Delay(1000)，过长导致有效等待时间不足，模块响应未接收完就停止
    HAL_Delay(100);

    // 7. 无需接收响应，直接返回成功（复位等指令专用）
    if ((reply1 == NULL) && (reply2 == NULL))
    {
        UART2_Debug_Print("ESP8266 Cmd Success: No Response Need");
		UART2_Debug_Print("Cmd Replai Buffer Content: %s", strEsp8266_Fram_Record.Data_RX_BUF);
        // 复位指令无需校验，返回前重置中断即可，无需立即清零缓冲区
        strEsp8266_Fram_Record.InfBit.FramFinishFlag = 0;
        return true;
    }

    // 8. 等待 ESP8266 响应（优化：保留完整 waittime，确保响应接收完成，解决截断关键）
    // 采用循环等待，兼顾超时和接收完成状态，避免阻塞导致的截断
    uint32_t start_time = HAL_GetTick(); // 记录开始时间，用于超时判断
    bool response_received = false;
    while ((HAL_GetTick() - start_time) < waittime)
    {
        // 检测是否接收到有效响应，或接收完成
        if (strEsp8266_Fram_Record.InfBit.FramFinishFlag || 
            strEsp8266_Fram_Record.InfBit.FramLength >= (RX_BUF_MAX_LEN - 2))
        {
            response_received = true;
            break;
        }
        HAL_Delay(10); // 短延时循环，不占用过多 CPU 资源
    }
		if(response_received)
		UART2_Debug_Print("ESP8266 Cmd Success Response ");	
    // 9. 安全添加字符串结束符（优化：确保完整保留接收数据，解决截断关键）
    // 场景1：正常接收，在实际接收长度后添加 \0，保留所有有效数据
    // 场景2：缓冲区满，在末尾添加 \0，避免数组越界，同时保留完整缓冲区内容
    uint32_t valid_len = strEsp8266_Fram_Record.InfBit.FramLength;
    if (valid_len > RX_BUF_MAX_LEN - 1)
    {
        valid_len = RX_BUF_MAX_LEN - 1; // 防止数组越界
    }
    strEsp8266_Fram_Record.Data_RX_BUF[valid_len] = '\0'; // 在有效数据末尾添加结束符，不截断中间内容

    // 10. 调试钩子：打印缓冲区原始内容（完整保留，解决截断后可查看完整响应）
    UART2_Debug_Print("Cmd Replai Buffer Content: %s", strEsp8266_Fram_Record.Data_RX_BUF);

    // 11. 响应校验（优化：或逻辑，支持轻微截断匹配，解决截断关键）
    bool check_result = false;
    char *rx_buffer = strEsp8266_Fram_Record.Data_RX_BUF; // 简化缓冲区引用

    // 匹配 reply1，支持非空判断，应对轻微截断（比如 CONNECTED 截断为 CONNEC）
    if (reply1 != NULL && strlen(reply1) > 0)
    {
        if (strstr(rx_buffer, reply1) != NULL)
        {
            check_result = true;
        }
    }

    // 匹配 reply2，支持非空判断，应对中间状态（比如 WIFI C）
    if (!check_result && reply2 != NULL && strlen(reply2) > 0)
    {
        if (strstr(rx_buffer, reply2) != NULL)
        {
            check_result = true;
        }
    }

    // 12. 兜底清零接收状态（优化：延迟清零，避免刚接收完就清空，解决截断关键）
    // 原代码立即清零，导致后续查看缓冲区时数据已丢失，现在延迟 50ms 清零，不影响后续解析
    HAL_Delay(50);
    strEsp8266_Fram_Record.InfBit.FramLength = 0;
    strEsp8266_Fram_Record.InfBit.FramFinishFlag = 0;
    // 缓冲区无需立即 memset，下一次调用会自动重置，保留当前数据供后续排查

    // 13. 返回校验结果
    return check_result;
}

/**
  * @brief  ESP8266 AT 指令通信测试（模块启动自检）
  * @note   1. 最多尝试 10 次 AT 指令交互，提高测试成功率
  *         2. 单次测试超时 500ms，符合 ESP8266 指令响应特性
  *         3. 成功后立即退出循环，失败后打印重试提示，最终失败打印错误信息
  * @param  无
  * @retval 无
  */
void ESP8266_AT_Test(void)
{
    uint8_t count = 0;

    HAL_Delay(1000);

    while (count < 10)
    {
        if (ESP8266_Cmd("AT", "OK", NULL, 500))
        {
            UART2_Debug_Print("ESP8266 AT Command Test Success");
            return;
        }

        UART2_Debug_Print("ESP8266 AT Command Test Failed, Retrying... (%d/10)", count + 1);

        count++;
        // 延长重试间隔（从300ms改为500ms，给模块足够的响应恢复时间）
        HAL_Delay(500);
    }

    UART2_Debug_Print("ESP8266 AT Command Test Final Failed (10 Retries Exhausted)");
}
/**
  * @brief  重启 ESP8266 模块
  * @note   1. 采用软件复位方式，发送 AT+RST 指令触发模块重启
  *         2. 复位指令无需校验响应（模块重启过程中会断开串口通信）
  *         3. 复位后添加足够延时，给模块完成上电初始化（1000ms）
  *         4. 复用已封装的 ESP8266_Cmd 函数和 UART2_Debug_Print 调试函数
  * @param  无
  * @retval 无
  */

void ESP8266_Rst(void)
{
    // 1. 打印调试信息
    UART2_Debug_Print("ESP8266 Sending Soft Reset Command (AT+RST)");

    // 2. 发送 AT+RST 软件复位指令，无需校验响应
    ESP8266_Cmd("AT+RST", NULL, NULL, UART_TIMEOUT_MS);
		HAL_UART_Receive_IT(&huart1, (uint8_t*)&strEsp8266_Fram_Record.Data_RX_BUF[0], 1);
    // 3. 延长复位后就绪延时（关键：从1000ms改为2000ms，确保模块完全初始化）
    //    老旧模块可适当延长至3000ms，避免后续指令被忽略
    HAL_Delay(2000);

    // 4. 强制彻底清零接收状态（核心：消除复位后的状态残留，为下一次Cmd调用铺路）
    memset(strEsp8266_Fram_Record.Data_RX_BUF, 0, RX_BUF_MAX_LEN);
    strEsp8266_Fram_Record.InfBit.FramLength = 0;
    strEsp8266_Fram_Record.InfBit.FramFinishFlag = 0;
    strEsp8266_Fram_Record.InfAll = 0;

    // 5. 打印调试信息
    UART2_Debug_Print("ESP8266 Soft Reset Process Completed");
}


															// 模块初始化流程
void ESP8266_Init(void)
{
		
    // 1. 先软件复位 ESP8266，恢复默认状态
		ESP8266_Rst();
		HAL_Delay(4000);
    // 2. 再进行 AT 指令通信测试
    ESP8266_AT_Test();
}

/**
  * @brief  选择 ESP8266 的网络工作模式
  * @note   1. 枚举值直接对应 AT 指令参数（0=STA，1=AP，2=STA_AP），无需额外转换
  *         2. 响应校验支持 `OK`（配置成功）和 `no change`（模式未改变，也算成功）
  *         3. 先做参数合法性检查，避免非法模式配置
  *         4. 复用 ESP8266_Cmd 发送指令，复用 UART2_Debug_Print 打印调试信息
  * @param  enumMode：网络模式枚举（STA=客户端模式，AP=热点模式，STA_AP=双模）
  * @retval bool：true=模式配置成功/已生效，false=配置失败/参数非法
  */
bool ESP8266_Net_Mode_Choose(ENUM_Net_ModeTypeDef enumMode)
{
    char at_cmd[32] = {0};  // 临时存储拼接后的 AT 指令（足够容纳 AT+CWMODE=2\r\n）
    bool cfg_result = false; // 配置结果标记

    // 1. 合法性检查：判断传入的网络模式是否在有效范围（0~2）
    if (enumMode > STA_AP)
    {
        UART2_Debug_Print("ESP8266 Invalid Network Mode (Only 0/1/2 Allowed)");
        return false;
    }

    // 2. 格式化拼接 AT+CWMODE 指令（枚举值直接作为指令参数，与 AT 指令规范对应）
    //    指令格式：AT+CWMODE=<mode> （mode=0/1/2 对应 STA/AP/STA_AP）
    snprintf(at_cmd, sizeof(at_cmd), "AT+CWMODE=%d", enumMode);
		UART2_Debug_Print("ESP8266  Network Mode.....");
    // 3. 发送配置指令，校验响应（OK=配置成功，no change=模式已生效，均算成功）
    cfg_result = ESP8266_Cmd(at_cmd, "OK", "no change", 1000);

    // 4. 打印调试信息，区分配置结果
    if (cfg_result)
    {
        // 打印成功信息，同时标注配置的模式
        const char* mode_name[] = {"STA Mode", "AP Mode", "STA_AP Dual Mode"};
        UART2_Debug_Print("ESP8266 %s Configure Success", mode_name[enumMode]);
    }
    else
    {
        UART2_Debug_Print("ESP8266 Network Mode Configure Failed");
    }

    // 5. 返回配置结果
    return cfg_result;
}
/*
  * @brief  ESP8266 创建 WiFi 热点（AP 模式下）
  * @note   1. 指令格式：AT+CWSAP="<ssid>","<pwd>",<channel>,<encryption>
  *         2. 信道固定为 1（兼容大部分设备，无需自定义）
  *         3. 加密模式要求：OPEN 模式无需密码，其他加密模式密码长度≥8位
  *         4. 需先成功配置 ESP8266 为 AP 或 STA_AP 模式，否则指令无效
  * @param  pSSID：创建的热点名称字符串（避免特殊字符，建议英文/数字）
  * @param  pPassWord：热点密码字符串（加密模式下需≥8位，OPEN 模式可传 NULL）
  * @param  enunPsdMode：热点加密方式枚举（OPEN=开放，WEP/WPA_PSK 等加密模式）
  * @retval bool：true=热点创建成功，false=创建失败/参数非法
  */
bool ESP8266_BuildAP(char *pSSID, char *pPassWord, ENUM_AP_PsdMode_TypeDef enunPsdMode)
{
    char at_cmd[128] = {0};  // 足够容纳完整 AT+CWSAP 指令（含 SSID、密码、参数）
    bool build_result = false;
    const char* encrypt_name[] = {"OPEN", "WEP", "WPA_PSK", "WPA2_PSK", "WPA_WPA2_PSK"};

    // ******** 步骤1：参数合法性全面检查（避免无效配置和程序崩溃）********
    // 1.1 检查 SSID 合法性（不能为空指针、不能为空字符串）
    if (pSSID == NULL || strlen(pSSID) == 0)
    {
        UART2_Debug_Print("ESP8266 AP SSID Invalid (Cannot be NULL or Empty)");
        return false;
    }

    // 1.2 检查加密方式合法性（必须在 OPEN ~ WPA_WPA2_PSK 范围内）
    if (enunPsdMode > WPA_WPA2_PSK)
    {
        UART2_Debug_Print("ESP8266 AP Encryption Mode Invalid (Only 0~4 Allowed)");
        return false;
    }

    // 1.3 检查密码合法性（非 OPEN 加密模式，密码需≥8位）
    if (enunPsdMode != OPEN)
    {
        // 加密模式：密码不能为空指针、不能为空字符串、长度≥8位
        if (pPassWord == NULL || strlen(pPassWord) < 8)
        {
						UART2_Debug_Print("ESP8266 AP Password Invalid (Encrypt Mode Need at least 8 Bytes)");
            return false;
        }
    }

    // ******** 步骤2：格式化拼接 AT+CWSAP 指令（适配加密模式）********
    // 指令说明：AT+CWSAP="<ssid>","<pwd>",<channel=1>,<encryption>
    // OPEN 模式：密码传空字符串，其他模式传传入的密码
    if (enunPsdMode == OPEN)
    {
        // 开放模式：无需密码，密码字段填空字符串
        snprintf(at_cmd, sizeof(at_cmd), "AT+CWSAP=\"%s\",\"\",1,%d", pSSID, enunPsdMode);
    }
    else
    {
        // 加密模式：拼接传入的密码
        snprintf(at_cmd, sizeof(at_cmd), "AT+CWSAP=\"%s\",\"%s\",1,%d", pSSID, pPassWord, enunPsdMode);
    }

    // ******** 步骤3：发送指令，校验响应（仅校验 "OK"，配置成功标志）********
    // 超时时间 1000ms，满足热点配置的响应耗时需求
    build_result = ESP8266_Cmd(at_cmd, "OK", NULL, 1000);

    // ******** 步骤4：打印详细调试信息，便于问题定位 ********
    if (build_result)
    {
        UART2_Debug_Print("ESP8266 AP Build Success (SSID: %s, Encrypt: %s)", pSSID, encrypt_name[enunPsdMode]);
    }
    else
    {
        UART2_Debug_Print("ESP8266 AP Build Failed (SSID: %s)", pSSID);
    }

    // ******** 步骤5：返回创建结果 ********
    return build_result;
}
void ESP8266_Init_AP_Example(void)
{
    // 步骤1：先配置 ESP8266 为 AP 模式
    bool net_mode_result = ESP8266_Net_Mode_Choose(STA_AP);
    if (!net_mode_result)
    {
        UART2_Debug_Print("ESP8266 AP Mode Configure Failed, Cannot Build AP");
        return;
    }
		else
		UART2_Debug_Print("ESP8266 AP Mode Configure Can Build AP");
		
    // 步骤2：创建 WiFi 热点（使用自定义 SSID、密码，WPA2_PSK 加密）
    bool ap_build_result = ESP8266_BuildAP(ESP8266_WIFI_NAME, ESP8266_ApPwd, WPA2_PSK);
    if (ap_build_result)
    {
        UART2_Debug_Print("ESP8266 AP Init Completed, Ready to Connect");
    }
	
}

/**
  * @brief  ESP8266 连接外部 WiFi 热点（STA 模式下）
  * @note   1. 指令格式：AT+CWJAP="<ssid>","<password>"
  *         2. 先自动配置 ESP8266 为 STA 模式（AT+CWMODE=0），避免模式错误导致连接失败
  *         3. 连接成功响应："CONNECTED"（核心标志）+ "OK"，连接失败响应："FAIL"
  *         4. 超时时间设为 3000ms（WiFi 扫描+认证+连接需要足够时间，不可过短）
  * @param  pSSID：WiFi 热点名称字符串（不可为空，避免中文/特殊字符）
  * @param  pPassWord：WiFi 热点密码字符串（不可为空，长度≥8位，符合 ESP8266 要求）
  * @retval bool：true=WiFi 连接成功，false=连接失败（参数错误/模式配置失败/密码错误/热点不可达等）
  */
bool ESP8266_LINK_AP(char *pSSID, char *pPassWord)
{
    char at_cmd[64] = {0};
    bool link_result = false;
    bool mode_result = false;

    // 步骤1：参数合法性检查
    if (pSSID == NULL || strlen(pSSID) == 0)
    {
        UART2_Debug_Print("ESP8266 LINK AP Error: SSID is NULL or Empty");
        return false;
    }
    if (pPassWord == NULL || strlen(pPassWord) < 8)
    {
        UART2_Debug_Print("ESP8266 LINK AP Error: Password is NULL or Length < 8");
        return false;
    }

    // 步骤2：配置为 STA 模式
    mode_result = ESP8266_Cmd("AT+CWMODE=1", "OK", "no change", 1000);
    if (!mode_result)
    {
        UART2_Debug_Print("ESP8266 LINK AP Error: STA Mode Configure Failed");
        return false;
    }
    HAL_Delay(500);

    // 步骤3：拼接连接指令
    snprintf(at_cmd, sizeof(at_cmd), "AT+CWJAP=\"%s\",\"%s\"", pSSID, pPassWord);

		// 步骤4：发送连接指令（修正：匹配完整的 WIFI CONNECTED，延长超时到 5000ms）
		link_result = ESP8266_Cmd(at_cmd, "OK","WIFI CONNECTED",  5000);

    // 步骤5：打印结果
    if (link_result)
    {
        UART2_Debug_Print("ESP8266 LINK AP Success (SSID: %s)", pSSID);
    }
    else
    {
        UART2_Debug_Print("ESP8266 LINK AP Failed (SSID: %s, Check Password/Hotspot Signal)", pSSID);
    }

    return link_result;
}


/**
  * @brief  独立扫描：获取附近所有可用 WiFi 热点列表（仅扫描，不连接）
  * @note   1. 扫描结果完整保留在全局缓冲区 strEsp8266_Fram_Record.Data_RX_BUF 中
  *         2. 扫描完成后不清理缓冲区，供后续解析选择热点
  *         3. 超时时间 12000ms，确保热点列表完整返回，无截断
  * @retval bool：true=扫描成功（缓冲区有有效热点列表），false=扫描失败（无热点/模块异常）
  */

bool ESP8266_Scan_WiFi(void)
{
    bool mode_result = false;
    bool scan_result = false;

    // 步骤1：配置 ESP8266 为 STA 模式（扫描依赖 STA 模式，调用优化后的 Cmd 函数）
    // 校验 OK/no change，兼容模式已配置的场景，等待时间 1000ms 足够
    mode_result = ESP8266_Cmd("AT+CWMODE=1", "OK", "no change", 1000);
    if (!mode_result)
    {
        UART2_Debug_Print("Scan Error: STA Mode Configure Failed");
        return false;
    }

    // 步骤2：模式切换后短缓冲（500ms，给模块足够时间完成模式切换，避免扫描指令被忽略）
    HAL_Delay(500);

    // 步骤3：发送第一个扫描指令（AT+CWLAP），调用优化后的 Cmd 函数防截断
    // 校验标志：+CWLAP:（扫描结果核心标志），备用标志：OK，等待时间 15000ms 确保多热点完整接收
    scan_result = ESP8266_Cmd("AT+CWLAP", "+CWLAP:", "OK", 6000);
    if (scan_result)
		UART2_Debug_Print("===== 扫描成功..... =====");
    else
		UART2_Debug_Print("Scan Failed");

    // 步骤5：判断扫描是否成功，打印完整热点列表（利用 Cmd 函数的防截断缓冲区）
    char *rx_buffer = strEsp8266_Fram_Record.Data_RX_BUF;
    bool has_valid_hotspot = (strstr(rx_buffer, "+CWLAP:") != NULL) || (strstr(rx_buffer, "+CWLIST:") != NULL);

    if (has_valid_hotspot)
    {
			UART2_Debug_Print("===== list up =====");
        return true;
    }
    else
    {
        UART2_Debug_Print("Scan Failed: No Valid Hotspots Found or Module Exception");
        //UART2_Debug_Print("Scan Buffer: %s", rx_buffer);
        return false;
    }
}
/**
  * @brief  ESP8266 配置为 STA 模式 TCP 客户端（支持透传）
  * @note   1. UART1：与 ESP8266 通讯（AT 指令、透传数据）
  *         2. UART2：仅调试打印，不与 ESP8266 交互
  * @param  无
  * @retval 无
  */
void ESP8266_StaTcpClient(void)
{
    bool tcp_connect_result = false;
    bool transparent_result = false;
    memset(tx_buffer,0,0);

    UART2_Debug_Print("===== Start ESP8266 STA TCP Client Init =====");

    // 步骤4：连接 TCP 服务器（UART1 发送 AT 指令）
    snprintf(tx_buffer, sizeof(tx_buffer), "AT+CIPSTART=\"TCP\",\"%s\",%d", 
             TCP_SERVER_IP, TCP_SERVER_PORT);
	
    UART2_Debug_Print("TCP Client Info: Try to Connect TCP Server (%s:%d)", 
                      TCP_SERVER_IP, TCP_SERVER_PORT);

    tcp_connect_result = ESP8266_Cmd(tx_buffer, "CONNECT", "OK", ESP8266_TCP_TIMEOUT);
    if (!tcp_connect_result)
    {
        UART2_Debug_Print("TCP Client Init Failed: TCP Server Connect Error");
        UART2_Debug_Print("Buffer Content: %s", strEsp8266_Fram_Record.Data_RX_BUF);
        
        return;
    }
	else
		UART2_Debug_Print("===== ESP8266 STA TCP Client Init Abort =====");
	
    HAL_Delay(800);

    // 步骤5：开启透传模式（UART1 发送 AT 指令）
    UART2_Debug_Print("TCP Client Info: Enable Transparent Mode");
    transparent_result = ESP8266_Cmd("AT+CIPMODE=1", "OK", NULL, 1000);
    if (transparent_result)
    {
        // 步骤6：进入透传发送状态（UART1 发送 AT 指令）
        ESP8266_Cmd("AT+CIPSEND", "OK", NULL, 1000);
        HAL_Delay(500);
		
        UART2_Debug_Print("TCP Client Info: Transparent Mode Enable Success (Send '+++' to Exit)");
	}
}

/**
  * @brief  ESP8266 退出透传模式
  * @note   通过发送 "+++" 指令退出，需提前延时确保数据发送完成，避免指令被透传
  * @param  无
  * @retval 无
  */
void ESP8266_ExitUnvarnishSend(void)
{
	memset(tx_buffer,0,0);
	UART2_Debug_Print("===== Start ESP8266_ExitUnvarnishSend  =====");
	
	snprintf(tx_buffer, sizeof(tx_buffer), "+++");
	
	HAL_UART_Transmit(&huart1, (uint8_t*)tx_buffer, strlen(tx_buffer), UART_TIMEOUT_MS);
	
	HAL_Delay(500);
	//ESP8266_AT_Test();
	
}


/**
  * @brief  获取 ESP8266 整体网络连接状态（适用于单连接场景）
  * @note   1. 核心指令：AT+CIPSTATUS（查询 ESP8266 网络连接状态）
  *         2. 状态码映射：2=已获取 IP，3=已建立连接，4=连接断开，0=获取失败
  *         3. 适用于单连接场景（ESP8266 默认单连接模式），解析单连接响应数据
  *         4. 复用优化后的 ESP8266_Cmd 函数，确保响应无截断、解析可靠
  * @param  无
  * @retval uint8_t：网络连接状态码（2/3/4/0，对应上述说明）
  */
uint8_t ESP8266_Get_LinkStatus(void)
{
    bool cmd_result = false;
    char *buf = NULL;
    char *status_start = NULL;
    char *conn_status_start = NULL;
    uint8_t link_status = 0;  // 默认返回 0（获取失败）
    uint32_t esp_status = 0;

    // 步骤1：初始化缓冲区指针，指向全局响应缓冲区
    buf = strEsp8266_Fram_Record.Data_RX_BUF;
    if (buf == NULL)
    {
        UART2_Debug_Print("LinkStatus Error: Response Buffer is NULL");
        return 0;
    }

    cmd_result = ESP8266_Cmd("AT+CIPSTATUS", "OK", NULL, 500);
    if (!cmd_result)
    {
        UART2_Debug_Print("LinkStatus Error: Send AT+CIPSTATUS Command Failed");
        return 0;
    }

    // 步骤3：解析整体 STATUS（提取 ESP8266 全局网络状态）
    // 典型响应格式：STATUS:2\r\n+CIPSTATUS:0,"TCP","192.168.1.102",8080,CONNECTED\r\nOK
    status_start = strstr(buf, "STATUS:");
    if (status_start == NULL)
    {
        UART2_Debug_Print("LinkStatus Error: Parse STATUS Failed");
        return 0;
    }

    // 跳过 "STATUS:" 前缀，提取数字状态值
    status_start += strlen("STATUS:");
    esp_status = atoi(status_start);  // 转换为数字（1=未获取IP，2=已获取IP，3=已建立TCP/UDP连接）

    // 步骤4：解析连接状态（CONNECTED/CLOSED），映射到用户指定状态码
    conn_status_start = strstr(buf, "CONNECTED");
    if (esp_status >= 2)  // 已获取 IP（STATUS=2）或已建立连接（STATUS=3）
    {
        if (conn_status_start != NULL)
        {
            // 解析到 CONNECTED：已建立 TCP/UDP 连接，返回 3
            link_status = 3;
        }
        else
        {
            // 未解析到 CONNECTED，且已获取 IP：连接断开/无有效连接，区分状态
            if (esp_status == 2)
            {
                // STATUS=2（已获取 IP），但连接断开，返回 2（已获取IP）或 4（连接断开）
                // 映射：已获取 IP 且无连接 → 优先返回 2，若之前有连接现在断开返回 4（单连接场景简化为：2=已获取IP，4=连接断开）
                // 此处简化解析：STATUS=2 且 CLOSED → 返回 2（已获取IP），也可根据需求调整为 4
                link_status = 2;
            }
            else if (strstr(buf, "CLOSED") != NULL)
            {
                // 解析到 CLOSED：连接已断开，返回 4
                link_status = 4;
            }
        }
    }
    else
    {
        // STATUS<2（如 1）：未获取 IP，指令执行成功但网络未就绪，返回 0（获取失败）
        UART2_Debug_Print("LinkStatus Error: ESP8266 Has Not Obtained IP (Code: 0)");
        link_status = 0;
    }

    // 步骤5：返回映射后的网络状态码
    return link_status;
}

/**
  * @brief  ESP8266 连接外部 MQTT 服务器（适配安可信 MQTT 专用固件）
  * @note   1. 基于安可信 ESP8266 MQTT 固件，使用专属 MQTT AT 指令，无需手动建立 TCP 连接
  *         2. 核心流程：配置 MQTT 用户参数 → 连接 MQTT 服务器，固件封装 MQTT 协议层逻辑
  *         3. 支持单连接（ID=5 映射为固件 ID 0）、多连接（ID=0-4），兼容原有参数风格
  *         4. 安可信固件专属响应：连接成功返回 "MQTT CONNECTED"
  * @param  enumE：网络协议枚举（仅支持 NET_PROTOCOL_MQTT）
  * @param  ip：MQTT 服务器 IP/域名（如 "192.168.1.103" 或 "mqtt.iot.eclipse.org"）
  * @param  ComNum：MQTT 服务器端口号（如 "1883" 未加密，"8883" SSL 加密）
  * @param  id：连接 ID（0-4 多连接，5 单连接（映射为固件 ID 0））
  * @retval bool：true=连接成功，false=连接失败（参数非法/指令错误/网络不可达等）
  */
bool ESP8266_Link_Server(ENUM_NetPro_TypeDef enumE, char *ip, char *ComNum, ENUM_ID_NO_TypeDef id)
{
    bool cfg_result = false;
    bool conn_result = false;
		uint8_t	mqtt_conn_id = 0; 

    // ******** 步骤1：参数合法性全面检查（适配安可信固件要求）********
    // 1.1 仅支持 MQTT 协议（安可信固件专属指令仅适配 MQTT）
    if (enumE != NET_PROTOCOL_MQTT || enumE >= NET_PROTOCOL_MAX)
    {
        UART2_Debug_Print("MQTT Error: Only MQTT Protocol Supported (Aithinker Firmware)");
        return false;
    }

    // 1.2 检查服务器 IP/域名、端口合法性
    if (ip == NULL || strlen(ip) == 0 || ComNum == NULL || strlen(ComNum) == 0)
    {
        UART2_Debug_Print("MQTT Error: Server IP/Port Cannot be NULL or Empty");
        return false;
    }

    // 1.3 映射连接 ID（原有 ID 5 映射为安可信固件单连接 ID 0）
    if (id == ID_NO_SINGLE)
    {
        mqtt_conn_id = 0;  // 单连接：映射为固件默认 ID 0
        UART2_Debug_Print("MQTT Info: Single Connection Mode (Map ID 5 to Aithinker ID 0)");
    }
    else if (id >= ID_NO_0 && id <= ID_NO_4)
    {
        mqtt_conn_id = (uint8_t)id;  // 多连接：直接使用 ID 0-4
    }
    else
    {
        UART2_Debug_Print("MQTT Error: Invalid Connection ID (Only 0-4 or 5 Allowed)");
        return false;
    }

    UART2_Debug_Print("===== Start Aithinker ESP8266 MQTT Server Link =====");
    UART2_Debug_Print("MQTT Info: Server: %s:%s, Client ID: %s", ip, ComNum, MQTT_CLIENT_ID);

    // ******** 步骤2：配置 MQTT 用户参数（安可信固件专属指令：AT+MQTTUSERCFG）********
    // 指令格式：AT+MQTTUSERCFG=<conn_id>,<clean_session>,<client_id>,<username>,<password>,<0><0>
    snprintf(tx_buffer, sizeof(tx_buffer), 
             "AT+MQTTUSERCFG=%d,%d,\"%s\",\"%s\",\"%s\",%d,%d,\"",
             mqtt_conn_id, MQTT_CLEAN_SESSION, MQTT_CLIENT_ID, 
             MQTT_USER_NAME, MQTT_PASSWORD, MQTT_cert_key_ID,MQTT_CA_ID);

    cfg_result = ESP8266_Cmd(tx_buffer, "OK", "no change", 1000);
    if (!cfg_result)
    {
        UART2_Debug_Print("MQTT Error: Configure MQTT User Param Failed");
        UART2_Debug_Print("===== Aithinker MQTT Link Abort =====");
        return false;
    }
    HAL_Delay(500);  // 配置后延时稳定，避免后续指令冲突

    // ******** 步骤3：连接 MQTT 服务器（安可信固件专属指令：AT+MQTTCONN）********
    // 指令格式：AT+MQTTCONN=<conn_id>,\"<server_ip>\",<port>,<ssl_en>
    // ssl_en：0=不启用 SSL（1883 端口），1=启用 SSL（8883 端口）
		memset(tx_buffer, 0, 0);
    uint8_t ssl_en = (strcmp(ComNum, "8883") == 0) ? 0 : 1;  // 自动判断是否启用 SSL
    snprintf(tx_buffer, sizeof(tx_buffer), 
             "AT+MQTTCONN=%d,\"%s\",%s,%d",
             ID_NO_0, ip, ComNum, ssl_en);

    // 校验安可信固件专属响应 "MQTT CONNECTED"，超时延长到 8000ms（含 DNS 解析+MQTT 握手）
    conn_result = ESP8266_Cmd(tx_buffer, "MQTT CONNECTED", "OK", MQTT_CONNECT_TIMEOUT);
  
    // ******** 步骤4：打印连接成功信息，返回结果 ********
		memset(tx_buffer, 0, 0);
    UART2_Debug_Print("MQTT Success: Connected to MQTT Server (Aithinker Firmware)");
    UART2_Debug_Print("===== Aithinker ESP8266 MQTT Link Completed =====");
    return true;
}



void ESP8266_Aithinker_MQTT_Example(void)
{
    bool mqtt_link_result = false;
    // 步骤2：连接 MQTT 服务器（安可信固件专属，单连接 ID=5）
    mqtt_link_result = ESP8266_Link_Server(NET_PROTOCOL_MQTT,
                                           MQTT_IP,
                                           MQTT_DUANKOU,
                                           ID_NO_0);
    if (mqtt_link_result)
    {
        UART2_Debug_Print("MQTT Example: Aithinker MQTT Connect Success, Ready to Publish/Subscribe");
    }
    else
    {
        UART2_Debug_Print("MQTT Example: Aithinker MQTT Connect Failed");
    }
}
/**
  * @brief  ESP8266 订阅 OneNET 系统主题（适配 AT+MQTTSUB 指令，宏定义可灵活修改）
  * @note   1. 核心指令：AT+MQTTSUB=0,"$sys/产品id/设备名称/thing/property/post/reply",0
  *         2. 主题通过宏定义拼接，修改 ONENET_PRODUCT_ID/ONENET_DEVICE_NAME 即可适配不同设备
  *         3. 基于安可信 MQTT 固件，订阅成功返回 "OK"，无需额外校验其他响应
  *         4. 订阅的主题用于接收 OneNET 属性上报的回复报文，确认数据是否上报成功
  * @param  无（核心参数已通过宏定义配置，方便修改）
  * @retval bool：true=订阅成功，false=订阅失败（指令错误/宏定义为空/网络异常等）
  */
bool ESP8266_MQTT_Subscribe_OneNET(void)
{
    memset(tx_buffer,0,0);
	memset(subscribe,0,0);										
    bool sub_result = false;

    // ******** 步骤1：宏定义合法性校验（避免空产品ID/设备名称导致订阅失败）********
    if (MQTT_USER_NAME == NULL || strlen(MQTT_USER_NAME) == 0 ||
        MQTT_CLIENT_ID == NULL || strlen(MQTT_CLIENT_ID) == 0)
    {
        UART2_Debug_Print("MQTT Subscribe Error: Product ID/Device Name Cannot be Empty (Check Macro)");
        return false;
    }

    UART2_Debug_Print("===== Start OneNET MQTT Topic Subscribe =====");

    // ******** 步骤2：拼接完整订阅主题（宏定义替换，无需手动修改字符串）********
    snprintf(subscribe, sizeof(subscribe), 
             ONENET_SUB_TOPIC_SUFFIX, 
             MQTT_USER_NAME, MQTT_CLIENT_ID);

    // ******** 步骤3：拼接完整 AT+MQTTSUB 订阅指令 ********
    // 指令格式：AT+MQTTSUB=0,"完整主题",QoS
    snprintf(tx_buffer, sizeof(tx_buffer), 
             "AT+MQTTSUB=%d,\"%s\",%d", 
             ID_NO_0,subscribe, MQTT_SUBSCRIBE_QOS);

    // ******** 步骤4：发送订阅指令，校验 OK 响应（安可信固件订阅成功核心标志）********
    sub_result = ESP8266_Cmd(tx_buffer, "OK", NULL, MQTT_CONNECT_TIMEOUT);
    if (!sub_result)
    {

        UART2_Debug_Print("===== OneNET MQTT Subscribe Abort =====");
        return false;
    }

    // ******** 步骤5：打印订阅成功信息，返回结果 ********
    UART2_Debug_Print("===== OneNET MQTT Topic Subscribe Finished =====");
    return true;
}
bool ESP8266_MQTT_Subscribe_OneNET_Repaly(void)
{
    memset(tx_buffer,0,0);
    memset(subscribe,0,0);	
    bool sub_result = false;

    // ******** 步骤1：宏定义合法性校验（避免空产品ID/设备名称导致订阅失败）********
    if (MQTT_USER_NAME == NULL || strlen(MQTT_USER_NAME) == 0 ||
        MQTT_CLIENT_ID == NULL || strlen(MQTT_CLIENT_ID) == 0)
    {
        UART2_Debug_Print("MQTT Subscribe Error: Product ID/Device Name Cannot be Empty (Check Macro)");
        return false;
    }

    UART2_Debug_Print("===== Start OneNET MQTT Topic Subscribe news =====");

    // ******** 步骤2：拼接完整订阅主题（宏定义替换，无需手动修改字符串）********
    snprintf(subscribe, sizeof(subscribe), 
             ONENET_SUB_TOPIC_Repaly, 
             MQTT_USER_NAME, MQTT_CLIENT_ID);

    // ******** 步骤3：拼接完整 AT+MQTTSUB 订阅指令 ********
    // 指令格式：AT+MQTTSUB=0,"完整主题",QoS
    snprintf(tx_buffer, sizeof(tx_buffer), 
             "AT+MQTTSUB=%d,\"%s\",%d", 
             ID_NO_0,subscribe, MQTT_SUBSCRIBE_QOS);

    // ******** 步骤4：发送订阅指令，校验 OK 响应（安可信固件订阅成功核心标志）********
    sub_result = ESP8266_Cmd(tx_buffer, "OK", NULL, MQTT_CONNECT_TIMEOUT);
    if (!sub_result)
    {

        UART2_Debug_Print("===== OneNET MQTT Subscribe Repaly =====");
        return false;
    }

    // ******** 步骤5：打印订阅成功信息，返回结果 ********
    UART2_Debug_Print("===== OneNET MQTT Topic Subscribe Repaly Finished =====");
    return true;
}


/**
 * @brief 解析 OneNET MQTT 下发的属性设置指令（+MQTTSUBRECV 报文），结果存入全局变量
 * @note 1. 输入示例："+MQTTSUBRECV:0,\"$sys/IeHhID6vH1/1/thing/property/set\",74,{\"id\":\"20\",\"version\":\"1.0\",\"params\":{\"Alarm\":false,\"led\":true,\"light\":88}}"
 *       2. 解析结果存入全局变量 g_OneNET_Property_Data，不做实时硬件控制，供后续项目使用
 *       3. 解析成功后设置 is_updated 为 true，标记有新数据下发
 * @param recv_buf: 收到的完整 MQTT 订阅报文（UART1 中断接收的原始数据）
 * @retval void
 */
void ESP8266_Parse_OneNET_Property(char *recv_buf)
{
    if (recv_buf == NULL || strlen(recv_buf) == 0)
    {
        UART2_Debug_Print("Parse Info: Empty Buffer (Skip Parse)");
        return;
    }

    OneNET_Property_Set_t temp_data = {0};
    temp_data.is_valid = false;
    temp_data.is_updated = false;

    // 直接解析各字段（无需 params）
    char *light_b = strstr(recv_buf, "\"light_b\":");
    if (light_b != NULL)
    {
        light_b += strlen("\"light_b\":");
        while (*light_b == ' ' || *light_b == '\t') light_b++;
        // 精准判断：只在当前字段值范围内（到逗号/大括号）找 true/false
        char *val_end = light_b;
        while (*val_end != ',' && *val_end != '}' && *val_end != '\0') val_end++;
        char val[10] = {0};
        strncpy(val, light_b, val_end - light_b);
        temp_data.light_b = (strstr(val, "true") != NULL);
    }
    else
        temp_data.light_b = g_OneNET_Property_Data.light_b;

    char *light_back = strstr(recv_buf, "\"light_back\":");
    if (light_back != NULL)
    {
        light_back += strlen("\"light_back\":");
        while (*light_back == ' ' || *light_back == '\t') light_back++;
        char *val_end = light_back;
        while (*val_end != ',' && *val_end != '}' && *val_end != '\0') val_end++;
        char val[10] = {0};
        strncpy(val, light_back, val_end - light_back);
        temp_data.light_back = (strstr(val, "true") != NULL);
    }
    else
        temp_data.light_back = g_OneNET_Property_Data.light_back;

    char *light_f = strstr(recv_buf, "\"light_f\":");
    if (light_f != NULL)
    {
        light_f += strlen("\"light_f\":");
        while (*light_f == ' ' || *light_f == '\t') light_f++;
        char *val_end = light_f;
        while (*val_end != ',' && *val_end != '}' && *val_end != '\0') val_end++;
        char val[10] = {0};
        strncpy(val, light_f, val_end - light_f);
        temp_data.light_f = (strstr(val, "true") != NULL);
    }
    else
        temp_data.light_f = g_OneNET_Property_Data.light_f;

    char *sun = strstr(recv_buf, "\"sun\":");
    if (sun != NULL)
    {
        sun += strlen("\"sun\":");
        while (*sun == ' ' || *sun == '\t') sun++;
        char *val_end = sun;
        while (*val_end != ',' && *val_end != '}' && *val_end != '\0') val_end++;
        char val[10] = {0};
        strncpy(val, sun, val_end - sun);
        temp_data.sun = (strstr(val, "true") != NULL);
    }
    else
        temp_data.sun = g_OneNET_Property_Data.sun;

    // 更新全局变量
    temp_data.is_valid = (sun != NULL || light_f != NULL || light_back != NULL || light_b != NULL);
    if (temp_data.is_valid)
    {
        temp_data.is_updated = true;
        g_OneNET_Property_Data = temp_data;
        UART2_Debug_Print("Parse Success: Global Var Updated -> sun=%d, light_back=%d, light_f=%d, light_b=%d",
                          temp_data.sun, temp_data.light_back, temp_data.light_f, temp_data.light_b);
   
    }
    else
    {
        UART2_Debug_Print("Parse Error: No Valid Fields Found");
    }
}

// 主循环（或独立解析任务）：解析缓冲区数据
void main_loop_task(void)
{
				
        // 1. 检测是否有完整帧需要解析（帧完成标志位为 1）
        if (strEsp8266_Fram_Record.InfBit.FramFinishFlag == 1)
        {	
					
					HAL_UART_AbortReceive_IT(&huart1);
					UART2_Debug_Print("Main Loop: Receive Complete Data: %s", strEsp8266_Fram_Record.Data_RX_BUF);
            // 2. 读取全局接收缓冲区（核心：获取 strEsp8266_Fram_Record.Data_RX_BUF 数据）
            char *recv_buf = strEsp8266_Fram_Record.Data_RX_BUF;
            uint32_t recv_len = strEsp8266_Fram_Record.InfBit.FramLength;

            // 3. 打印缓冲区数据（调试用，确认收到完整数据）
            UART2_Debug_Print("Main Loop: Receive Complete Data (Len: %d)", recv_len);

            // 4. 执行解析逻辑（和之前的解析逻辑一致，仅触发时机改变）
            char *mqtt_start = strstr(recv_buf, "MQTTSUBRECV:");
            if (mqtt_start != NULL)
            {
                UART2_Debug_Print("Main Loop: Start Parse MQTT Packet");
                char *json_start = strchr(mqtt_start, '{');
                if (json_start != NULL)
                {
                    json_start++; // 跳过 '{'，指向目标字段
                    ESP8266_Parse_OneNET_Property(json_start); // 调用原有解析函数
                }
            }
            else
            {
                UART2_Debug_Print("Main Loop: Non-MQTT Packet (Ignore)");
            }
						
            // 5. 解析完成后：清空缓冲区 + 重置帧状态（关键：为下一次接收做准备）
            memset(strEsp8266_Fram_Record.Data_RX_BUF, 0, RX_BUF_MAX_LEN);
            strEsp8266_Fram_Record.InfBit.FramLength = 0;
            strEsp8266_Fram_Record.InfBit.FramFinishFlag = 0;
        HAL_UART_Receive_IT(&huart1, (uint8_t*)&strEsp8266_Fram_Record.Data_RX_BUF[strEsp8266_Fram_Record.InfBit.FramLength], 
                            1);
            UART2_Debug_Print("Main Loop: Buffer Cleared, Ready for Next Receive\n");
						
						
        }
}



 /* @note   1. 指令格式严格符合 ESP8266 MQTT AT 指令规范，带 \r\n 结尾
 *         2. 校验 ESP8266 响应 "OK"，确保绑定操作成功
 *         3. 依赖你提供的 ESP8266_Cmd 函数，自带超时和响应校验，可靠性高
 * @retval bool：true=主题绑定成功，false=主题绑定失败（发送失败/响应不匹配/超时）
 */
bool ESP8266_Bind_OneNET_Report_Topic_With_Cmd(void)
{
    memset(tx_buffer,0,0);
    memset(subscribe,0,0);	
    bool sub_result = false;

    // ******** 步骤1：宏定义合法性校验（避免空产品ID/设备名称导致订阅失败）********
    if (MQTT_USER_NAME == NULL || strlen(MQTT_USER_NAME) == 0 ||
        MQTT_CLIENT_ID == NULL || strlen(MQTT_CLIENT_ID) == 0)
    {
        UART2_Debug_Print("MQTT Subscribe Error: Product ID/Device Name Cannot be Empty (Check Macro)");
        return false;
    }

    UART2_Debug_Print("===== Start OneNET MQTT Topic Subscribe =====");

    // ******** 步骤2：拼接完整订阅主题（宏定义替换，无需手动修改字符串）********
    snprintf(subscribe, sizeof(subscribe), 
             ONENET_POST_TOPIC, 
             MQTT_USER_NAME, MQTT_CLIENT_ID);


    // ******** 步骤3：拼接完整 AT+MQTTSUB 订阅指令 ********
    // 指令格式：AT+MQTTSUB=0,"完整主题",QoS
    snprintf(tx_buffer, sizeof(tx_buffer), 
             "AT+MQTTSUB=%d,\"%s\",%d", 
             ID_NO_0,subscribe, MQTT_SUBSCRIBE_QOS);

    // ******** 步骤4：发送订阅指令，校验 OK 响应（安可信固件订阅成功核心标志）********
    sub_result = ESP8266_Cmd(tx_buffer, "OK", NULL, MQTT_CONNECT_TIMEOUT);
    if (!sub_result)
    {

        UART2_Debug_Print("===== OneNET MQTT Subscribe Repaly =====");
        return false;
    }

    // ******** 步骤5：打印订阅成功信息，返回结果 ********
    UART2_Debug_Print("===== OneNET MQTT Topic Subscribe  Finished =====");
    return true;
}



/**
 * @brief  按照 AT+MQTTPUBRAW 两步流程，向 OneNET 上报属性数据
 * @note   1. 第一步：发送 AT+MQTTPUBRAW=0,"topic",size,0,0\r\n
 *         2. 第二步：发送纯 JSON 数据（字节数 = size，格式严格匹配要求）
 *         3. Topic：$sys/IeHhID6vH1/1/thing/property/post（带双引号）
 *         4. JSON：{"id": "1","params": { "Alarm": { "value": true },"led": { "value": true },"light": { "value": 28 }}}
 * @param  Alarm: 报警状态（bool，直接生成 true/false，无额外引号）
 * @param  led: LED 状态（bool，直接生成 true/false，无额外引号）
 * @param  light: 亮度值（0-100 整数，匹配格式要求）
 * @retval bool：true=两步操作均成功，false=某一步失败
 */
bool ESP8266_Parse_OneNET_Success_Response(void)
{
    // ---------------------- 步骤 1：定义变量，拼接纯 JSON 并计算 size ----------------------
    // 1. 缓冲区定义（JSON 保留所有空格，指令帧足够容纳）
    char mqtt_raw_cmd[256] = {0};    // 第一步的 AT+MQTTPUBRAW 指令
    char json_data[128] = {0};       // 第二步要发送的纯 JSON 数据
    char* onenet_topic = ONENET_SUB_TOPIC_Repaly; 
    uint32_t json_size = 0;          // JSON 数据的字节长度（size）

    // 2. 拼接纯 JSON 数据（严格匹配你的格式，包含所有空格，无任何转义）
		snprintf(json_data, sizeof(json_data),
         "{\"id\":\"1\",\"code\":200,\"msg\":\"success\"}");

    // 3. 计算 JSON 数据的字节长度（size，关键：必须准确，等于 strlen(json_data)）
    json_size = strlen(json_data);
    if (json_size == 0 || json_size >= sizeof(json_data))
    {
        UART2_Debug_Print("Failed: JSON size = %d", json_size);
        return false;
    }

    // ---------------------- 步骤 2：发送 AT+MQTTPUBRAW 指令帧（第一步） ----------------------
    // 拼接指令帧：AT+MQTTPUBRAW=0,"topic",size,0,0\r\n（topic 带双引号，size 为 JSON 字节长度）
    snprintf(mqtt_raw_cmd, sizeof(mqtt_raw_cmd),
             "AT+MQTTPUBRAW=%d,\"%s\",%d,0,0\r\n",
             ID_NO_0,onenet_topic,
             json_size);

    // 调用 ESP8266_Cmd 发送指令帧，校验 ESP8266 响应 "OK"（表示进入数据接收模式）
    bool cmd_result = ESP8266_Cmd(mqtt_raw_cmd, "OK", NULL, 5000);
    if (cmd_result == false)
    {
        UART2_Debug_Print("Failed: AT+MQTTPUBRAW ");
        return false;
    }

    // ---------------------- 步骤 3：发送 JSON 数据帧（第二步，纯数据，无额外字符） ----------------------
    // 直接发送纯 JSON 数据（字节数 = json_size，无需 AT 指令，无需 \r\n，ESP8266 会接收指定 size 字节后停止）
    HAL_StatusTypeDef data_result = HAL_UART_Transmit(&huart1,
                                                      (uint8_t*)json_data,
                                                      json_size,
                                                      5000);

    // 校验 JSON 数据发送结果
    if (data_result != HAL_OK)
    {
        UART2_Debug_Print("Failed: %d", data_result);
        return false;
    }

    // ---------------------- 步骤 4：打印成功日志，返回结果 ----------------------
    UART2_Debug_Print("Success: AT+MQTTPUBRAW ");
    UART2_Debug_Print("Topic: %s", onenet_topic);
    return true;
}
/**
 * @brief 格式化打印缓冲区内容（整行显示，适配HTTP响应格式）
 * @param title 打印标题
 * @param buf 数据缓冲区
 * @param len 数据长度
 */
void UART2_Debug_Print_Format(const char *title, const uint8_t *buf, uint16_t len)
{
    if (buf == NULL || len == 0 || title == NULL) return;

    // 1. 打印标题
    UART2_Debug_Print("%s", title);
    
    // 2. 临时缓冲区：拼接一行内容（避免逐字符换行）
	memset(uart2_tx_buffer,0,0);
    uint16_t line_idx = 0;

    for (uint16_t i=0; i<len; i++)
    {
        // 跳过开头的0x00（避免干扰）
        if (i == 0 && buf[i] == 0x00) continue;

        // 处理换行符：拼接当前行并打印，然后清空
        if (buf[i] == '\n')
        {
            // 把当前行的\r替换为可视字符（可选）
            for (uint16_t j=0; j<line_idx; j++)
            {
                if (uart2_tx_buffer[j] == '\r') uart2_tx_buffer[j] = ' ';
            }
            // 打印整行
            UART2_Debug_Print("%s", uart2_tx_buffer);
            // 清空临时缓冲区
            memset(uart2_tx_buffer, 0, sizeof(uart2_tx_buffer));
            line_idx = 0;
            continue;
        }

        // 可打印字符：直接拼接
        if (buf[i] >= 0x20 && buf[i] <= 0x7E)
        {
            if (line_idx < sizeof(uart2_tx_buffer)-1)
            {
                uart2_tx_buffer[line_idx++] = buf[i];
            }
        }
        // 不可打印字符（除了\r\n）：打印为.
        else if (buf[i] != '\r')
        {
            if (line_idx < sizeof(uart2_tx_buffer)-1)
            {
                uart2_tx_buffer[line_idx++] = '.';
            }
        }
    }

    // 打印最后一行（无\n结尾的内容，如JSON）
    if (line_idx > 0)
    {
        UART2_Debug_Print("%s", uart2_tx_buffer);
    }
}

/**
 * @brief  重置ESP8266接收帧结构体（发起新请求前调用）
 * @note   清空缓冲区+重置状态位，避免旧数据干扰
 */
void ESP8266_Reset_Fram_Record(void)
{
    // 1. 清空接收缓冲区
    memset(strEsp8266_Fram_Record.Data_RX_BUF, 0, RX_BUF_MAX_LEN);
    // 2. 重置联合体状态（整体置0，同时清空FramLength和FramFinishFlag）
    strEsp8266_Fram_Record.InfAll = 0;

}

/**
 * @brief 发送HTTP GET请求下载固件分片，并写入W25Q32
 * @param firmware_id 固件ID（如1310012）
 * @param start_byte 分片起始字节（如0,256,512...）
 * @param end_byte 分片结束字节（如255,511,767...）
 * @return true:成功 false:失败
 */
// 全局变量：记录已下载的固件总长度（用于校验）
static uint32_t g_ota_downloaded_len = 0;

bool ESP8266_OTADownload(uint32_t firmware_id, uint32_t start_byte, uint32_t end_byte) {
 // 1. 安全校验：仅对非最后一个分片要求256字节（新增total_size参数）
    uint32_t chunk_size = end_byte - start_byte + 1;
    // 新增：判断是否是最后一个分片（end_byte >= 总大小-1）
    bool is_last_chunk = (end_byte >= g_ota_firmware_info.size - 1);
    
    // 非最后一个分片必须是256字节，最后一个分片可以小于256
    if (!is_last_chunk && (chunk_size != W25Q32_PAGE_SIZE)) {
        UART2_Debug_Print("【OTA下载】错误：非最后分片大小必须为256字节！当前：%d", chunk_size);
        return false;
    }


    // 2. 清空缓冲区+重置接收状态
    memset(tx_buffer, 0, sizeof(tx_buffer));
    ESP8266_Reset_Fram_Record();
	HAL_Delay(50);
    strEsp8266_Fram_Record.InfBit.FramLength = 0;
    strEsp8266_Fram_Record.InfBit.FramFinishFlag = 0;
    g_ota_recv_mode = OTA_RECV_MODE_NONE;

    // 3. 构造HTTP GET分片下载请求（带Range头）
    uint16_t request_length = sprintf(tx_buffer,
                                       OTA_HTTP_GET_REQUEST_FMT,
                                       MQTT_USER_NAME, MQTT_CLIENT_ID, firmware_id,
                                       HTTP_PASSWORD, TCP_SERVER_IP,
                                       start_byte, end_byte);

    // 4. 打印调试信息
   // UART2_Debug_Print("【OTA下载】构造请求：%s", tx_buffer);

    // 5. 发送请求到ESP8266（串口1）
    HAL_StatusTypeDef tx_status = HAL_UART_Transmit(&huart1,
                                                    (uint8_t*)tx_buffer,
                                                    request_length,
                                                    UART_TIMEOUT_MS);

    if (tx_status != HAL_OK) {
        UART2_Debug_Print("【OTA下载】串口发送失败，错误码：%d", tx_status);
        return false;
    }

    // 6. 等待响应（这里用延时+打印，确保数据接收完整）
    HAL_Delay(1500);

    // 7. 获取总接收长度
    uint16_t total_recv_len = strEsp8266_Fram_Record.InfBit.FramLength;
    UART2_Debug_Print("【OTA下载】总接收长度：%d 字节", total_recv_len);

    // 8. 查找HTTP头结束标志 \r\n\r\n（和打印函数逻辑完全一致）
    uint16_t header_end_pos = 0;
    bool find_flag = false;
    for (uint16_t i = 0; i <= total_recv_len - 4; i++) {
        if (strEsp8266_Fram_Record.Data_RX_BUF[i] == '\r' &&
            strEsp8266_Fram_Record.Data_RX_BUF[i+1] == '\n' &&
            strEsp8266_Fram_Record.Data_RX_BUF[i+2] == '\r' &&
            strEsp8266_Fram_Record.Data_RX_BUF[i+3] == '\n') {
            header_end_pos = i;
            find_flag = true;
            break;
        }
    }

    if (!find_flag) {
        UART2_Debug_Print("【OTA下载】未找到HTTP头结束标志\\r\\n\\r\\n！");
        return false;
    }

    // 9. 核心：取对应长度的固件数据（最后一个分片取实际长度）
    uint16_t firmware_start_idx = header_end_pos + 4;
    // 最后一个分片取实际需要的长度，其他分片取256字节
    uint16_t firmware_data_len = is_last_chunk ? chunk_size : W25Q32_PAGE_SIZE;

    // 安全校验：确保不越界
    if (firmware_start_idx + firmware_data_len > total_recv_len) {
        firmware_data_len = total_recv_len - firmware_start_idx;
        UART2_Debug_Print("【OTA下载】警告：缓冲区不足，实际取%d字节", firmware_data_len);
    }

    uint8_t *firmware_data = (uint8_t *)&strEsp8266_Fram_Record.Data_RX_BUF[firmware_start_idx];

    // 10. 写入W25Q32（核心步骤）
    uint32_t flash_write_addr = OTA_FLASH_START_ADDR + start_byte;
    UART2_Debug_Print("【OTA下载】写入W25Q32：地址0x%06X，长度%d字节", flash_write_addr, firmware_data_len);

    // 10.1 擦除扇区（仅当跨扇区时擦除，避免重复擦除）
    static uint32_t last_erase_sector = 0xFFFFFFFF;
    uint32_t current_sector = flash_write_addr & 0xFFFFF000; // 取扇区起始地址
    if (current_sector != last_erase_sector) {
        SPI_Flash_Erase_Sector(current_sector);
        last_erase_sector = current_sector;
        UART2_Debug_Print("【OTA下载】擦除扇区：0x%06X", current_sector);
    }
	
	//OTA_CRC32_Download_Update(firmware_data, firmware_data_len);
	
    // 10.2 写入一页数据（256字节）
    SPI_Flash_Write_Page(flash_write_addr, firmware_data_len, firmware_data);

    // 10.3 校验写入（可选，提高可靠性）
    uint8_t verify_buf[W25Q32_PAGE_SIZE] = {0};
    SPI_Flash_Read(flash_write_addr, firmware_data_len, verify_buf);
    if (memcmp(firmware_data, verify_buf, firmware_data_len) != 0) {
        UART2_Debug_Print("【OTA下载】写入校验失败！");
        return false;
    }
	
	// ★★★ 新增：打印下载分片的信息，和回读一一对应 ★★★
printf("[DOWNLOAD-CHUNK] Addr: 0x%06X | len: %d | offset: %d\r\n", flash_write_addr, firmware_data_len, start_byte);
printf("  First 16 bytes: ");
for (int i = 0; i < 16; i++) printf("%02X ", verify_buf[i]);
printf("\r\n");
if (firmware_data_len > 16) {
    printf("  Last 16 bytes:  ");
    for (int i = firmware_data_len - 16; i < firmware_data_len; i++) printf("%02X ", verify_buf[i]);
    printf("\r\n");
}

    OTA_CRC32_Download_Update(verify_buf, firmware_data_len);
    // 11. 更新已下载长度
    g_ota_downloaded_len += firmware_data_len;
    UART2_Debug_Print("【OTA下载】分片写入成功！已下载：%d/%d字节",
                      g_ota_downloaded_len, g_ota_firmware_info.size);

    return true;
}

/**
 * @brief 发送OTA版本检查请求，获取固件信息（版本、大小、MD5等）
 * @param user_name MQTT_USER_NAME（如VAvg8087hx）
 * @param client_id MQTT_CLIENT_ID（如RB）
 * @param type 请求类型（如1）
 * @param current_version 当前版本（如"1.2"）
 * @return true:请求发送成功且解析成功，false:失败
 */
bool ESP8266_OTACheckVersion(void) {
    // 1. 清空发送/接收缓冲区
    memset(tx_buffer, 0, sizeof(tx_buffer));
    ESP8266_Reset_Fram_Record();  // 重置接收结构体
    g_ota_recv_mode = OTA_RECV_MODE_CHECK_VER;
    // 2. 构造HTTP GET请求（使用宏定义）
    int req_len = sprintf(tx_buffer,
                          OTA_CHECK_VERSION_REQUEST_FMT,
                          MQTT_USER_NAME, MQTT_CLIENT_ID,
                          HTTP_PASSWORD, TCP_SERVER_IP);
    
    
    UART2_Debug_Print("【OTA检查】请求构造:%s", tx_buffer);
   
    // 4. 发送请求到ESP8266（串口1）
    HAL_StatusTypeDef tx_status = HAL_UART_Transmit(&huart1,
                                                    (uint8_t*)tx_buffer,
                                                    req_len,
                                                    UART_TIMEOUT_MS);

    // 5. 等待响应（阻塞等待，可根据实际调整超时）
        HAL_Delay(1000);
    UART2_Debug_Print("【OTA数据】总接收长度：%d 字节",  strEsp8266_Fram_Record.InfBit.FramLength);
	
	
	// 7. 解析JSON响应（修复版：分段查找+精准截取）
	uint16_t recv_len = strEsp8266_Fram_Record.InfBit.FramLength;	
	memset(&g_ota_firmware_info, 0, sizeof(OTA_Firmware_InfoTypeDef));
	char *json_start = NULL;
	// 遍历缓冲区，找到第一个{（绕过前面的HTTP头/乱码）
	for (uint16_t i=0; i<recv_len; i++) {
		if (strEsp8266_Fram_Record.Data_RX_BUF[i] == '{') {
			json_start = &strEsp8266_Fram_Record.Data_RX_BUF[i];
			break;
		}
	}
	
	if (json_start == NULL) {
		UART2_Debug_Print("【OTA检查】未找到JSON起始符 {");
		return false;
	}
	UART2_Debug_Print("【OTA检查】找到JSON起始位置：%d，JSON内容：%s", 
					(int)(json_start - strEsp8266_Fram_Record.Data_RX_BUF), json_start);
	
	// 7.1 解析响应码（"code":0）
	char *p_code = strstr(json_start, "\"code\":");
	if (p_code != NULL) {
		p_code += strlen("\"code\":");  // 跳过"code":
		g_ota_firmware_info.code = atoi(p_code);  // 转成数字
		UART2_Debug_Print("【OTA检查】解析code：%d", g_ota_firmware_info.code);
	} else {
		UART2_Debug_Print("【OTA检查】未找到code字段");
		return false;
	}
	
	if (g_ota_firmware_info.code != 0) {
		UART2_Debug_Print("【OTA检查】响应失败，code：%d", g_ota_firmware_info.code);
		return false;
	}
	
	// 7.2 解析data嵌套字段（核心修复）
	char *p_data = strstr(json_start, "\"data\":{");
	if (p_data == NULL) {
		UART2_Debug_Print("【OTA检查】未找到data字段");
		return false;
	}
	p_data += strlen("\"data\":{");  // 跳过"data":{"
	
	// 7.2.1 解析target（目标版本：1.4）
	char *p_target = strstr(p_data, "\"target\":\"");
	if (p_target != NULL) {
		p_target += strlen("\"target\":\"");
		// 截取到下一个"为止
		char *p_target_end = strstr(p_target, "\"");
		if (p_target_end != NULL) {
			strncpy(g_ota_firmware_info.target_version, p_target, p_target_end - p_target);
			UART2_Debug_Print("【OTA检查】解析target：%s", g_ota_firmware_info.target_version);
		}
	}
	
	// 7.2.2 解析tid（固件ID：1310012）
	char *p_tid = strstr(p_data, "\"tid\":");
	if (p_tid != NULL) {
		p_tid += strlen("\"tid\":");
		g_ota_firmware_info.tid = atoi(p_tid);
		UART2_Debug_Print("【OTA检查】解析tid：%d", g_ota_firmware_info.tid);
	}
	
	// 7.2.3 解析size（固件大小：7360）
	char *p_size = strstr(p_data, "\"size\":");
	if (p_size != NULL) {
		p_size += strlen("\"size\":");
		g_ota_firmware_info.size = atoi(p_size);
		UART2_Debug_Print("【OTA检查】解析size：%d", g_ota_firmware_info.size);
	}
	
	// 7.2.4 解析md5（MD5值）
	char *p_md5 = strstr(p_data, "\"md5\":\"");
	if (p_md5 != NULL) {
		p_md5 += strlen("\"md5\":\"");
		char *p_md5_end = strstr(p_md5, "\"");
		if (p_md5_end != NULL) {
			strncpy(g_ota_firmware_info.md5, p_md5, p_md5_end - p_md5);
			UART2_Debug_Print("【OTA检查】解析md5：%s", g_ota_firmware_info.md5);
		}
	}
	
	// 7.3 校验解析结果
	int parse_count = 0;
	if (g_ota_firmware_info.target_version[0] != '\0') parse_count++;
	if (g_ota_firmware_info.tid != 0) parse_count++;
	if (g_ota_firmware_info.size != 0) parse_count++;
	if (g_ota_firmware_info.md5[0] != '\0') parse_count++;
	
	if (parse_count == 4) {
		g_ota_firmware_info.parse_ok = 1;
		UART2_Debug_Print("【OTA检查】解析成功：");
		AT24C64_Write_Byte(AT24C64_OTA_FLAG_ADDR,OTA_FLAG_NEED_UPDATE);
		
		return true;
	} else {
		UART2_Debug_Print("【OTA检查】解析失败，仅解析到%d个字段", parse_count);
		UART2_Debug_Print("  已解析：target=[%s], tid=[%d], size=[%d], md5=[%s]",
						g_ota_firmware_info.target_version,
						g_ota_firmware_info.tid,
						g_ota_firmware_info.size,
						g_ota_firmware_info.md5);
		return false;
			}			
	}	
		
		

/**
 * @brief 循环下载所有固件分片（含失败重试）
 * @param firmware_id 固件ID
 * @param total_size  固件总大小（如7360）
 * @param max_retry   每个分片最大重试次数
 * @return true:全部下载完成 false:失败
 */
bool ESP8266_OTA_Download_All(uint32_t firmware_id, uint32_t total_size, uint8_t max_retry) {
    g_ota_downloaded_len = 0; // 重置已下载长度
    uint32_t start_byte = 0;
    uint32_t end_byte = W25Q32_PAGE_SIZE - 1; // 初始0-255
    bool all_success = true;
    
    UART2_Debug_Print("【OTA全量下载】开始下载总大小：%d字节的固件，分片大小：%d字节",
                      total_size, W25Q32_PAGE_SIZE);
   
    // 循环下载所有分片
    while (start_byte < total_size) {
        // 调整最后一个分片的结束字节
        if (end_byte >= total_size) {
            end_byte = total_size - 1;
        }

        uint8_t retry_count = 0;
        bool chunk_success = false;

        // 分片失败重试
        while (retry_count < max_retry) {
            UART2_Debug_Print("=====================================");
            UART2_Debug_Print("【OTA全量下载】下载分片：%d-%d字节（重试%d/%d）",
                              start_byte, end_byte, retry_count+1, max_retry);

            // 下载当前分片
            if (ESP8266_OTADownload(firmware_id, start_byte, end_byte)) {
                chunk_success = true;
                break;
            }

            retry_count++;
            UART2_Debug_Print("【OTA全量下载】分片%d-%d下载失败，%dms后重试...",
                              start_byte, end_byte, 1000 * retry_count);
            HAL_Delay(1000 * retry_count); // 重试间隔递增（避免频繁请求）
        }

        // 重试耗尽仍失败
        if (!chunk_success) {
            UART2_Debug_Print("【OTA全量下载】分片%d-%d下载失败，重试次数耗尽！", start_byte, end_byte);
            all_success = false;
            break;
        }

        // 切换到下一个分片
        start_byte += W25Q32_PAGE_SIZE;
        end_byte = start_byte + W25Q32_PAGE_SIZE - 1;
        if (end_byte >= total_size) {
            end_byte = total_size - 1; // 最后一个分片可能不足256字节
        }

        HAL_Delay(1500); // 分片间隔，避免服务器压力
    }

    UART2_Debug_Print("【OTA全量下载】所有分片下载完成！总大小：%d字节", total_size);
    
    // 新增：校验已下载长度是否匹配总大小
    if (g_ota_downloaded_len != total_size) {
        UART2_Debug_Print("【OTA警告】已下载长度(%d)与总长度(%d)不一致！", 
                          g_ota_downloaded_len, total_size);
        all_success = false;
    }

    // 修复：返回实际的成功状态，而非固定true
    return all_success;
}		
	
	
	
	
	
	
	
	
	
	
	
	

// 宏定义
#define HTTP_HEADER_END_FLAG_LEN  4       // \r\n\r\n的长度
#define PRINT_HEX_PER_LINE        16      // 每行16字节
#define OTA_FIRMWARE_LEN          256     // 固定取256字节

void ESP8266_Print_OTA_Data(void)
{
    uint16_t total_len = strEsp8266_Fram_Record.InfBit.FramLength;
    if (total_len == 0)
    {
        UART2_Debug_Print("【OTA数据】无接收数据");
        return;
    }

    // 1. 基础信息打印
    UART2_Debug_Print("【OTA数据】总接收长度：%d 字节，强制提取\\r\\n\\r\\n后256字节", total_len);

    // 2. 只找\r\n\r\n的位置（不纠结其他）
    uint16_t header_end_pos = 0;
    bool find_flag = false;
    for (uint16_t i=0; i <= total_len - HTTP_HEADER_END_FLAG_LEN; i++)
    {
        if (strEsp8266_Fram_Record.Data_RX_BUF[i] == '\r' &&
            strEsp8266_Fram_Record.Data_RX_BUF[i+1] == '\n' &&
            strEsp8266_Fram_Record.Data_RX_BUF[i+2] == '\r' &&
            strEsp8266_Fram_Record.Data_RX_BUF[i+3] == '\n')
        {
            header_end_pos = i;
            find_flag = true;
            break;
        }
    }

    if (!find_flag)
    {
        UART2_Debug_Print("【OTA数据】未找到\\r\\n\\r\\n，无法提取固件！");
        return;
    }

    // 3. 核心：计算固件起始位置，强制取256字节
    uint16_t firmware_start_idx = header_end_pos + HTTP_HEADER_END_FLAG_LEN;
    // 安全校验：确保取256字节不越界（不够则取到末尾）
    uint16_t actual_len = (total_len - firmware_start_idx) >= OTA_FIRMWARE_LEN ? 
                          OTA_FIRMWARE_LEN : (total_len - firmware_start_idx);

    UART2_Debug_Print("【固件分片】\\r\\n\\r\\n在第%d字节，固件起始位置：第%d字节，提取长度：%d字节",
                      header_end_pos, firmware_start_idx, actual_len);
    UART2_Debug_Print("【固件分片】256字节内容（十六进制）：");

    // 4. 打印这256字节（格式正确，地址递增）
    char temp_hex_buf[64] = {0};
    for (uint16_t i = 0; i < actual_len; i++)
    {
        // 每行开头打印正确偏移地址
        if (i % PRINT_HEX_PER_LINE == 0)
        {
            memset(temp_hex_buf, 0, sizeof(temp_hex_buf));
            snprintf(temp_hex_buf, sizeof(temp_hex_buf)-1, "0x%04X: ", firmware_start_idx + i);
        }

        // 拼接十六进制字节
        uint16_t remain = sizeof(temp_hex_buf) - strlen(temp_hex_buf) - 1;
        if (remain >= 3)
        {
            snprintf(&temp_hex_buf[strlen(temp_hex_buf)], remain, "%02X ", 
                     (uint8_t)strEsp8266_Fram_Record.Data_RX_BUF[firmware_start_idx + i]);
        }

        // 每行结束/最后一行打印
        if ((i % PRINT_HEX_PER_LINE == PRINT_HEX_PER_LINE - 1) || (i == actual_len - 1))
        {
            UART2_Debug_Print("%s", temp_hex_buf);
        }
    }

    // 5. 若实际长度不足256，提示（便于排查）
    if (actual_len < OTA_FIRMWARE_LEN)
    {
        UART2_Debug_Print("【警告】\\r\\n\\r\\n后仅%d字节，不足256字节！", actual_len);
    }
    else
    {
        UART2_Debug_Print("【固件分片】256字节提取完成！");
    }
}