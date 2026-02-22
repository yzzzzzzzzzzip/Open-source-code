/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : esp8266.h
  * @brief          : ESP8266 WiFi模块驱动头文件
  * @details        : 包含安可信固件MQTT通信、OneNET平台接入等功能定义
  ******************************************************************************
  * @attention
  * 头文件保护宏：防止多个源文件包含时出现多重定义编译错误（必备规范）
  ******************************************************************************
  */

/* 防止重复包含保护 */
#ifndef __ESP8266_H__
#define __ESP8266_H__

#ifdef __cplusplus
extern "C" {
#endif

/******************************* 编译器兼容性配置 ***************************/
/* 第一步：先启用匿名联合体支持（针对Keil ARMCC编译器） */
/* 放在头文件最开头、结构体定义之前，确保编译器解析结构体时已启用该功能 */
#if defined (__CC_ARM)
#pragma anon_unions
#endif

/******************************* 包含必要头文件 ***************************/
#include "main.h"
#include "usart.h"
#include "gpio.h"
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>      /* 包含atoi()函数声明，解决未定义错误 */
#include "stdio.h"       /* 若工程中有sprintf/printf，建议同时保留 */

/******************************* 全局配置宏定义 ***************************/
/* 串口通信相关配置 */
#define UART_TIMEOUT_MS         1000        /* 串口阻塞超时时间（单位：ms） */
#define RX_BUF_MAX_LEN          1024        /* 串口1最大接收缓存字节数（存储ESP8266响应数据） */
#define UART1_TX_BUF_MAX_LEN    512         /* 串口1最大发送缓存字节数（存储待发送的AT指令） */
#define UART2_TX_BUF_MAX_LEN    1024        /* 串口2最大发送缓存字节数（存储调试打印信息） */

/******************************* OneNET MQTT配置宏 ***************************/
/* 安可信MQTT固件核心配置宏（用户可自定义） */
#define MQTT_CLIENT_ID          "home"      /* MQTT客户端设备名称 */
#define MQTT_USER_NAME          "VRph6cA2aV"                /* MQTT服务器产品ID */
#define MQTT_PASSWORD           ""  /* MQTT服务器Token */
#define MQTT_cert_key_ID        0           /* MQTT cert证书, 参数为0 */
#define MQTT_CA_ID              0           /* MQTT目前支持一套CA证书, 参数为0 */
#define MQTT_CONNECT_TIMEOUT    2000        /* MQTT连接超时（ms，安可信固件耗时略长） */
#define MQTT_CLEAN_SESSION      1           /* 清理会话（1=清理，0=保留会话） */
#define MQTT_IP                 "mqtts.heclouds.com"    /* MQTT连接IP */
#define MQTT_DUANKOU            "1883"      /* MQTT连接端口 */
#define MQTT_SUBSCRIBE_QOS      0           /* QoS等级（0=最多一次，1=至少一次，OneNET推荐0） */

/* OneNET主题定义 */
#define ONENET_SUB_TOPIC_SUFFIX     "$sys/%s/%s/thing/property/set"       /* 订阅主题固定后缀 */
#define ONENET_SUB_TOPIC_Repaly     "$sys/%s/%s/thing/property/post/set_reply"  /* 订阅回复主题 */
#define ONENET_POST_TOPIC           "$sys/%s/%s/thing/property/post"     /* 上传主题固定格式（OneNET属性上报主题） */

/* JSON数据上报配置 */
#define UPLOAD_JSON_ID          "1"         /* JSON报文id（固定值，可随意填） */
#define UPLOAD_UART_TIMEOUT     5000        /* UART发送超时时间（ms） */
#define ONENET_JSON_VERSION     "1.0"       /* 物模型版本号 */
#define ONENET_MQTT_QOS         0           /* MQTT QOS等级（固定0，匹配指令格式） */
#define ONENET_MQTT_RETAIN      0           /* MQTT Retain标志（固定0，匹配指令格式） */

/******************************* 用户配置参数 ***************************/
/* ESP8266网络连接配置 */
#define ESP8266_WIFI_NAME       "TP-LINK_B4F2"      /* WiFi名称 */
#define ESP8266_ApPwd           "Qq3184986381"      /* WiFi密码 */
#define ESP8266_STA_SSID        "TP-LINK_B4F2"      /* 要连接的WiFi热点名称 */
#define ESP8266_STA_PWD         "Qq3184986381"      /* WiFi热点密码（≥8位） */

/* TCP服务器配置 */
#define TCP_SERVER_IP           "192.168.0.100"     /* 远端TCP服务器IP地址（需与ESP8266在同一网段） */
#define TCP_SERVER_PORT         8080                /* 远端TCP服务器端口号（自定义，需与服务器一致） */
#define ESP8266_TCP_TIMEOUT     3000                /* TCP连接超时时间（ms，建议≥3000） */

/******************************* 枚举类型定义 ***************************/
/**
  * @brief  ESP8266网络工作模式枚举
  * @note   对应AT指令：AT+CWMODE=<mode>
  *         该枚举值直接作为AT指令参数，配置模块的网络工作形态
  */
typedef enum
{
    STA     = 1,    /* 客户端模式（Station Mode）：连接外部WiFi热点 */
    AP      = 2,    /* 热点模式（Access Point Mode）：创建自身WiFi热点 */
    STA_AP  = 3     /* 双模模式：同时支持STA模式和AP模式 */
} ENUM_Net_ModeTypeDef;

/**
  * @brief  ESP8266网络通信协议枚举
  * @note   对应AT指令：AT+CIPSTART=<id>,"<protocol>","<ip>",<port>
  *         用于指定与服务器建立连接的网络协议类型
  */
typedef enum
{
    NET_PROTOCOL_TCP = 0,
    NET_PROTOCOL_UDP = 1,
    NET_PROTOCOL_MQTT = 2,     /* 安可信固件MQTT专属 */
    NET_PROTOCOL_MAX  =3
} ENUM_NetPro_TypeDef;

/**
  * @brief  ESP8266网络连接ID枚举
  * @note   对应AT指令：AT+CIPSTART=<id>,... / AT+CIPSEND=<id>,...
  *         多连接模式下（AT+CIPMUX=1）支持ID 0-4，单连接模式下（AT+CIPMUX=0）仅支持ID 5
  */
typedef enum
{
    ID_NO_0 = 0,            /* 多连接ID 0（安可信固件默认单连接为ID 0） */
    ID_NO_1 = 1,
    ID_NO_2 = 2,
    ID_NO_3 = 3,
    ID_NO_4 = 4,
    ID_NO_SINGLE = 5        /* 映射到安可信固件ID 0（单连接） */
} ENUM_ID_NO_TypeDef;

/**
  * @brief  ESP8266 AP模式下热点加密方式枚举
  * @note   对应AT指令：AT+CWSAP="<ssid>","<pwd>",<channel>,<encryption>
  *         用于配置自身热点的加密方式，密码长度需匹配对应加密方式要求
  */
typedef enum
{
    OPEN           = 0,   /* 开放模式：无密码，任何人可直接连接（安全性低） */
    WEP            = 1,   /* WEP加密：早期加密方式，安全性较弱（已逐步淘汰） */
    WPA_PSK        = 2,   /* WPA-PSK加密：主流加密方式，安全性较高（推荐） */
    WPA2_PSK       = 3,   /* WPA2-PSK加密：更安全的加密方式，兼容性好（推荐） */
    WPA_WPA2_PSK   = 4    /* WPA/WPA2混合加密：兼容WPA和WPA2客户端（最高兼容性） */
} ENUM_AP_PsdMode_TypeDef;

/******************************* 结构体定义 ***************************/
/**
  * @brief  串口接收数据帧处理结构体（用于ESP8266数据接收缓存与状态标记）
  * @note   1. 采用匿名联合体+位段设计，高效利用16位存储空间
  *         2. 该结构体实例为外部全局变量，仅在此声明，定义在esp8266.c中
  *         3. 兼容STM32 HAL库__IO宏（等价于volatile，防止编译器优化内存数据）
  */
struct STRUCT_USARTx_Fram
{
    char  Data_RX_BUF[RX_BUF_MAX_LEN];  /* 串口接收数据缓冲区：存储ESP8266返回的完整响应数据 */

    /**
      * @brief  匿名联合体（整合整体状态与位段状态，节省存储空间）
      * @note   InfAll（16位整体）与InfBit（位段拆分）共用同一块16位内存空间
      */
    union
    {
        __IO uint16_t InfAll;  /* 接收帧状态整体（16位）：可直接赋值/读取整体状态 */

        /**
          * @brief  位段结构体（拆分16位状态为具体功能位，精准控制）
          * @note   低15位（bit0~bit14）：接收数据长度；最高位（bit15）：接收完成标志
          */
        struct
        {
            __IO uint16_t FramLength     :15;  /* 接收帧有效数据长度（占15位，最大支持32767字节，匹配RX_BUF_MAX_LEN） */
            __IO uint16_t FramFinishFlag :1;   /* 接收帧完成标志（占1位：0=未完成，1=已完成） */
        } InfBit;
    };
};

/**
  * @brief  OneNET下发属性数据存储结构体
  * @note   包含所有需要的字段和状态标志
  */
typedef struct
{
    bool Alarm;        /* 报警状态（false=无报警，true=有报警） */
    bool led;          /* LED控制状态（false=关闭，true=开启） */
    bool light_b;      /* 前灯控制状态 */
    bool light_back;   /* 后灯控制状态 */
    bool light_f;      /* 左前灯控制状态 */
    bool sun;          /* 日光灯控制状态 */
    bool is_valid;     /* 数据是否有效（false=无效，true=有效） */
    bool is_updated;   /* 数据是否更新（false=未更新，true=已更新，用于主循环判断） */
} OneNET_Property_Set_t;

/******************************* 全局变量声明 ***************************/
/* OneNET属性数据全局实例 */
extern OneNET_Property_Set_t g_OneNET_Property_Data;

/* ESP8266串口接收帧全局实例 */
extern struct STRUCT_USARTx_Fram strEsp8266_Fram_Record;

/* TCP连接状态标志 */
extern volatile uint8_t ucTcpClosedFlag;                    /* TCP连接关闭标志位 */

/* 串口通信状态标志 */
extern volatile bool usart1_tx_done;                       /* 串口1发送完成标志（中断用） */

/* 串口缓冲区声明 */
extern char uart1_tx_buffer[UART1_TX_BUF_MAX_LEN];         /* 串口1发送缓冲区（存储AT指令） */
extern char uart2_tx_buffer[UART2_TX_BUF_MAX_LEN];         /* 串口2发送缓冲区（存储调试信息） */

/******************************* 函数声明 ***************************/
/* 调试打印函数 */
void UART2_Debug_Print(const char *fmt, ...);

/* 模块初始化相关函数 */
void ESP8266_Init(void);
void ESP8266_Rst(void);
bool ESP8266_Cmd(char *cmd, char *reply1, char *reply2, uint32_t waittime);
void ESP8266_AT_Test(void);
bool ESP8266_Net_Mode_Choose(ENUM_Net_ModeTypeDef enumMode);

/* AP模式相关函数 */
void ESP8266_Init_AP_Example(void);
bool ESP8266_BuildAP(char *pSSID, char *pPassWord, ENUM_AP_PsdMode_TypeDef enunPsdMode);

/* STA模式相关函数 */
bool ESP8266_LINK_AP(char *pSSID, char *pPassWord);
bool ESP8266_Scan_WiFi(void);
uint8_t ESP8266_Get_LinkStatus(void);
void ESP8266_StaTcpClient(void);
bool ESP8266_Link_Server(ENUM_NetPro_TypeDef enumE, char *ip, char *ComNum, ENUM_ID_NO_TypeDef id);

/* 透传模式相关函数 */
void ESP8266_ExitUnvarnishSend(void);
bool ESP8266_Enter_Transparent_Mode(void);

/* MQTT相关函数 */
void ESP8266_Aithinker_MQTT_Example(void);
bool ESP8266_MQTT_Subscribe_OneNET(void);
bool ESP8266_MQTT_Subscribe_OneNET_Repaly(void);
bool ESP8266_Bind_OneNET_Report_Topic_With_Cmd(void);
bool ESP8266_AT_MQTT_Publish_Exact_Format(bool Alarm, bool led, uint8_t light);
bool ESP8266_AT_MQTT_Publish_Raw(bool light_b, bool light_back, bool light_f, bool sun);
bool ESP8266_Transparent_Send_Pure_JSON_With_Topic(void);
bool ESP8266_AT_MQTT_Publish_OneJSON(bool Alarm, bool led, uint8_t light, int64_t timestamp);

/* 数据解析相关函数 */
void main_loop_task(void);
void ESP8266_Parse_OneNET_Property(char *recv_buf);
bool ESP8266_Parse_OneNET_Success_Response(void);

/* 多连接与服务器相关函数 */
bool ESP8266_Enable_MultipleId(FunctionalState enumEnUnvarnishTx);
bool ESP8266_StartOrShutServer(FunctionalState enumMode, char *pPortNum, char *pTimeOver);
uint8_t ESP8266_Get_IdLinkStatus(void);
uint8_t ESP8266_Inquire_ApIp(char *pApIp, uint8_t ucArrayLength);

/* 数据收发函数 */
bool ESP8266_SendString(FunctionalState enumEnUnvarnishTx, char *pStr, uint32_t ulStrLength, ENUM_ID_NO_TypeDef ucId);
char *ESP8266_ReceiveString(FunctionalState enumEnUnvarnishTx);

#ifdef __cplusplus
}
#endif

#endif /* __ESP8266_H__ */
