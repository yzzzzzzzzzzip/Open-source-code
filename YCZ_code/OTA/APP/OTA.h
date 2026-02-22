#ifndef __OTA_H__
#define __OTA_H__

#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>  // 包含atoi()函数声明，解决未定义错误
#include "stdio.h"   // 若工程中有sprintf/printf，建议同时保留
#include "main.h"
#include "esp8266.h"

/************************ 宏定义 ************************/

#define OTA_FLAG_NEED_UPDATE     0xAA    // 需要更新的标志
#define OTA_FLAG_NO_UPDATE        0x00    // 不需要更新的标志
#define AT24C64_OTA_FLAG_ADDR    0x0000  // OTA标志位存储在AT24C64的起始地址

// Flash相关配置（根据你的硬件调整）
#define MD5_STRING_LEN           33           // MD5字符串长度（32位+结束符）
#define	OTA_MAX_RETRY_COUNT       3         //  下载重启次数
#define CRC32_STRING_LEN         9            // CRC32字符串长度（8位十六进制+结束符）



/************************ 结构体定义 ************************/


extern uint32_t g_ota_crc_download;


/************************ 函数声明 ************************/

/**
 * @brief 切换到新固件并重启（你已封装，保留声明）
 */
void OTA_Reboot_To_New_Firmware(void);


/************************ MD5相关函数声明 ************************/
/**
 * @brief 计算Flash中指定地址/长度数据的MD5值
 * @param flash_addr  数据起始地址（如OTA_FIRMWARE_FLASH_ADDR）
 * @param data_len    数据总长度（固件总大小）
 * @param out_md5     输出MD5字符串（长度需≥33）
 * @return true:计算成功 false:失败
 */
bool OTA_MD5_Calc_From_Flash(uint32_t flash_addr, uint32_t data_len, char *out_md5);

/**
 * @brief 校验固件MD5（对比本地计算值和g_ota_firmware_info.md5）
 * @param flash_addr  固件起始地址
 * @param firmware_len 固件总长度
 * @return true:MD5一致（校验通过） false:不一致/失败
 */
bool OTA_MD5_Verify_Firmware(uint32_t flash_addr, uint32_t firmware_len);

/**
 * @brief OTA完整流程：下载→MD5校验→升级（整合MD5后的主流程）
 * @param firmware_id 固件ID
 * @param total_size  固件总大小
 * @return true:升级流程成功 false:失败
 */
bool OTA_Full_Process(uint32_t firmware_id, uint32_t total_size);
void MD5_Simple_Test(void);


/************************ CRC32本地回环校验函数（新增） ************************/
/**
 * @brief 初始化下载时的CRC32计算（在开始下载前调用）
 */
void OTA_CRC32_Download_Init(void);

/**
 * @brief 更新下载时的CRC32（每收到一个分片数据就调用）
 * @param data  分片数据指针
 * @param len   分片数据长度
 */
void OTA_CRC32_Download_Update(const uint8_t *data, uint32_t len);

/**
 * @brief 完成下载时的CRC32计算（在所有分片下载完成后调用）
 * @return 下载时计算的CRC32值
 */
uint32_t OTA_CRC32_Download_Final(void);

/**
 * @brief 从Flash读取数据并计算CRC32（用于回环校验）
 * @param flash_addr  固件起始地址
 * @param data_len    固件总长度
 * @return 从Flash读取并计算的CRC32值
 */
uint32_t OTA_CRC32_Readback_From_Flash(uint32_t flash_addr, uint32_t data_len);

/**
 * @brief 本地回环校验：对比下载CRC32和回读CRC32
 * @param flash_addr  固件起始地址
 * @param firmware_len 固件总长度
 * @return true:一致 false:不一致
 */
bool OTA_CRC32_Loopback_Verify(uint32_t flash_addr, uint32_t firmware_len);

/**
 * @brief CRC32值转8位十六进制字符串
 */
void OTA_CRC32_To_String(uint32_t crc32, char str[9]);

// OTA完整流程
bool OTA_Full_Process(uint32_t firmware_id, uint32_t total_size);

void Test_CRC32_Algorithm(void);
#endif /* __OTA_H__ */
