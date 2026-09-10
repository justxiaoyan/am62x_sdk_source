#ifndef __EMS_PACK_H__
#define __EMS_PACK_H__

#include <linux/types.h>

// 魔数定义
#define EMS_MAGIC "EMSFS"
#define EMS_MAGIC_LEN 6
#define EMS_HEADER_SIZE 512

#pragma pack(push, 1) 
// EMS打包头结构体（使用联合体确保512字节大小）
typedef union {
    struct {
        char magic[EMS_MAGIC_LEN];  // 魔数 "EMSFS"
        u32 file_size;              // 文件大小
        u16 crc16;                  // CRC16校验值
        u16 head_write_flag;        // 头部写入标志：1=需要写入升级分区，0=不需要
        char pack_file[64];         // 输出文件名
        char file_name[64];         // 原始文件名
    } __attribute__((packed));
    u8 raw[EMS_HEADER_SIZE];        // 确保整个结构体为512字节
} EMS_PACK_HEAD_T;
#pragma pack(pop)

/**
 * ems_verify_pack_header - 验证EMS打包头
 * @header: 打包头指针
 * @data: 实际数据指针（512字节头部之后的数据）
 *
 * 返回值: 0=成功, -1=魔数错误, -2=CRC校验失败
 */
int ems_verify_pack_header(EMS_PACK_HEAD_T *header, const u8 *data);

/**
 * ems_calculate_crc16 - 计算CRC16校验值
 * @data: 数据指针
 * @length: 数据长度
 *
 * 返回值: CRC16校验值
 */
u16 ems_calculate_crc16(const u8 *data, u32 length);

#endif /* __EMS_PACK_H__ */
