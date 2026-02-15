#include "esp8266.h"

// ************************ 定义全局变量（存储解析后的服务器下发数据）************************
volatile bool usart1_tx_done = true;  
char uart1_tx_buffer[UART1_TX_BUF_MAX_LEN] = {0};  
char uart2_tx_buffer[UART2_TX_BUF_MAX_LEN] = {0};
char tx_buffer[UART1_TX_BUF_MAX_LEN]= {0};   // 串口1发送缓冲区（拼接AT指令）
char subscribe[256] = {0};// 容纳拼接后的完整订阅主题

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
        if (strEsp8266_Fram_Record.InfBit.FramLength < RX_BUF_MAX_LEN - 1)
        {
            // ===================== 直接替换这部分判断 =====================
            if (strEsp8266_Fram_Record.InfBit.FramLength > 110 &&
                strEsp8266_Fram_Record.Data_RX_BUF[strEsp8266_Fram_Record.InfBit.FramLength] == '}' &&
                strEsp8266_Fram_Record.Data_RX_BUF[strEsp8266_Fram_Record.InfBit.FramLength - 1] == '}')
            {
                strEsp8266_Fram_Record.InfBit.FramFinishFlag = 1;
            }
            // ==============================================================
            strEsp8266_Fram_Record.InfBit.FramLength++;
        }
        else
        {
            strEsp8266_Fram_Record.InfBit.FramFinishFlag = 1;
        }

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
    bool mode_result = false;
    bool wifi_link_result = false;
    bool tcp_connect_result = false;
    bool transparent_result = false;
    char at_cmd[32] = {0};

    UART2_Debug_Print("===== Start ESP8266 STA TCP Client Init =====");

    // 步骤1：配置 ESP8266 为纯 STA 模式（UART1 发送 AT 指令，通过 ESP8266_Cmd 封装）
    mode_result = ESP8266_Net_Mode_Choose(STA);
    if (!mode_result)
    {
        UART2_Debug_Print("TCP Client Init Failed: STA Mode Configure Error");
        UART2_Debug_Print("===== ESP8266 STA TCP Client Init Abort =====");
        return;
    }
    HAL_Delay(800);

    // 步骤2：连接 WiFi（UART1 发送 AT 指令，复用已有函数）
    wifi_link_result = ESP8266_LINK_AP((char*)ESP8266_STA_SSID, (char*)ESP8266_STA_PWD);
    if (!wifi_link_result)
    {
        UART2_Debug_Print("TCP Client Init Failed: WiFi Link Error (Check SSID/PWD)");
        UART2_Debug_Print("===== ESP8266 STA TCP Client Init Abort =====");
        return;
    }
    HAL_Delay(1000);

    // 步骤3：关闭残留透传（UART1 发送 AT 指令）
    UART2_Debug_Print("TCP Client Info: Close Residual Transparent Mode");
    transparent_result = ESP8266_Cmd("AT+CIPMODE=0", "OK", NULL, 1000);
    if (!transparent_result)
    {
        UART2_Debug_Print("TCP Client Warn: Close Residual Transparent Mode Failed");
    }
    HAL_Delay(500);

    // 步骤4：连接 TCP 服务器（UART1 发送 AT 指令）
    snprintf(at_cmd, sizeof(at_cmd), "AT+CIPSTART=\"TCP\",\"%s\",%d", 
             TCP_SERVER_IP, TCP_SERVER_PORT);
    UART2_Debug_Print("TCP Client Info: Try to Connect TCP Server (%s:%d)", 
                      TCP_SERVER_IP, TCP_SERVER_PORT);

    tcp_connect_result = ESP8266_Cmd(at_cmd, "CONNECT", "OK", ESP8266_TCP_TIMEOUT);
    if (!tcp_connect_result)
    {
        UART2_Debug_Print("TCP Client Init Failed: TCP Server Connect Error");
        UART2_Debug_Print("Buffer Content: %s", strEsp8266_Fram_Record.Data_RX_BUF);
        UART2_Debug_Print("===== ESP8266 STA TCP Client Init Abort =====");
        return;
    }
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

        // ******** 关键：通过 UART1 发送数据给 ESP8266，由 ESP8266 透传给 TCP 服务器 ********
        const char tcp_send_data[32] = "Hello TCP!";
        UART2_Debug_Print("TCP Client Info: Send Data to ESP8266 (UART1) : %s", tcp_send_data);
        
        // 严格使用 &huart1（与 ESP8266 通讯串口）发送透传数据，无 AT 格式、无 \r\n
        if (HAL_UART_Transmit(&huart1,  // 明确使用 UART1，与 ESP8266 交互
                              (uint8_t*)tcp_send_data, 
                              strlen(tcp_send_data), 
                              UART_TIMEOUT_MS) == HAL_OK)
        {
            UART2_Debug_Print("TCP Client Info: Data Send to ESP8266 Success (Will Transparent to Server)");
        }
        else
        {
            UART2_Debug_Print("TCP Client Error: Data Send to ESP8266 Failed (UART1 Error)");
        }
        // **********************************************************************
    }
    else
    {
        UART2_Debug_Print("TCP Client Warn: Transparent Mode Enable Failed (Use Non-Transparent Mode)");
    }

    UART2_Debug_Print("===== ESP8266 STA TCP Client Init Completed =====");
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
					g_uart1_busy = 1; // 立即占标：解析开始，上报全程禁止（核心！）
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
						
						g_uart1_busy = 0; // 解析完成：释放标志，上报才允许执行（核心！）
        }
}


/**
 * @brief  透传模式一次性发送「指定主题+纯 JSON」（符合 OneNET 规范）
 * @note   1. 格式：主题\nJSON（换行符 \n 分隔，不是 \t）
 *         2. 主题：$sys/IeHhID6vH1/1/thing/property/post
 *         3. JSON：你验证成功的格式，包含所有空格
 * @param  Alarm: 报警状态（bool）
 * @param  led: LED 状态（bool）
 * @param  light: 亮度值（0-100 整数）
 * @retval bool：发送成功/失败
 */
bool ESP8266_Transparent_Send_Topic_Plus_JSON(bool Alarm, bool led, uint8_t light)
{
    // 1. 定义缓冲区（容纳主题 + 换行 + JSON，足够冗余）
    char upload_data[384] = {0};
    char pure_json[256] = {0};
    // 指定专属上报主题（一字不差）
    char onenet_topic[] = "$sys/IeHhID6vH1/1/thing/property/post";

    // 2. 拼接纯 JSON（和你的正确格式 1:1 对齐，包含所有空格）
    snprintf(pure_json, sizeof(pure_json),
             "{\"id\": \"1\",\"params\": { \"Alarm\": { \"value\": %s },\"led\": { \"value\": %s },\"light\": { \"value\": %d }}}",
             Alarm ? "true" : "false",
             led ? "true" : "false",
             light);

    // 3. 拼接「主题\nJSON」（关键：换行符 \n 分隔
    snprintf(upload_data, sizeof(upload_data),
              "%s\n%s",  // 主题 + 换行符 + 纯 JSON（无多余字符）
             onenet_topic,
             pure_json);

    // 4. 透传发送完整数据
    HAL_StatusTypeDef transmit_status = HAL_UART_Transmit(&huart1,
                                                           (uint8_t*)upload_data,
                                                           strlen(upload_data),
                                                           5000);

    // 5. 结果返回与日志
    if (transmit_status == HAL_OK)
    {
        UART2_Debug_Print("Send Success: Topic + JSON -> %s", upload_data);
        return true;
    }
    else
    {
        UART2_Debug_Print("Send Failed: Topic + JSON Transmit Error");
        return false;
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
 * @brief  向 OneNET 上报4个布尔类型属性（light_b/light_back/light_f/sun）
 * @note   1. 沿用 AT+MQTTPUBRAW 两步上报流程，指令格式严格匹配
 *         2. JSON格式：{"id":"1","params":{"xxx":{"value":true/false}}}
 *         3. 所有参数为bool类型，自动转换为JSON的true/false（无引号）
 * @param  light_b: 布尔属性1
 * @param  light_back: 布尔属性2
 * @param  light_f: 布尔属性3
 * @param  sun: 布尔属性4
 * @retval bool：true=上报成功，false=上报失败
 */
bool ESP8266_AT_MQTT_Publish_Raw(bool light_b, bool light_back, bool light_f, bool sun)
{
    // 1. 定义缓冲区（256足够容纳4个布尔字段的JSON，留足余量）
    memset(tx_buffer,0,0);
    memset(subscribe,0,0);	
    char onenet_topic [64] = {0};
    uint32_t json_size = 0;        // JSON实际字节长度（关键：必须准确）

    // 2. 拼接OneNET要求的JSON数据（4个布尔字段，格式严格匹配）
    snprintf(subscribe, sizeof(subscribe),
             "{\"id\": \"1\",\"params\": { \"light_b\": { \"value\": %s },\"light_back\": { \"value\": %s },\"light_f\": { \"value\": %s },\"sun\": { \"value\": %s }}}",
             light_b ? "true" : "false",
             light_back ? "true" : "false",
             light_f ? "true" : "false",
             sun ? "true" : "false");

    // 3. 校验JSON长度（避免空数据/缓冲区溢出）
    json_size = strlen(subscribe);
    if (json_size == 0 || json_size >= sizeof(subscribe))
    {
        UART2_Debug_Print("MQTT Publish Failed: JSON Size Error (%d)", json_size);
        return false;
    }

    // 4. 拼接第一步AT+MQTTPUBRAW指令（带topic、size，格式不变）
		snprintf(onenet_topic, sizeof(onenet_topic), 
             ONENET_POST_TOPIC, 
             MQTT_USER_NAME, MQTT_CLIENT_ID);
			 
    snprintf(tx_buffer, sizeof(tx_buffer),
             "AT+MQTTPUBRAW=%d,\"%s\",%d,0,0\r\n",
             ID_NO_0, onenet_topic, json_size);
		
 
    // 5. 发送AT指令，等待ESP8266返回OK（进入数据接收模式）
    bool cmd_result = ESP8266_Cmd(tx_buffer, "OK", NULL, 1000);
    if (cmd_result == false)
    {
        UART2_Debug_Print("MQTT Publish Failed: AT Cmd Send Error");
        return false;
    }

    // 6. 发送第二步纯JSON数据（无AT指令、无\r\n，仅原始数据）
    HAL_StatusTypeDef data_result = HAL_UART_Transmit_IT(&huart1,
                                                      (uint8_t*)subscribe,
                                                      json_size);
    if (data_result != HAL_OK)
    {
        UART2_Debug_Print("MQTT Publish Failed: JSON Data Send Error (%d)", data_result);
        return false;
    }

    // 7. 上报成功，打印调试信息（含实际上报的JSON，方便排查）
    UART2_Debug_Print("MQTT Publish Success: %s", subscribe);
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
