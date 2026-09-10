// SPDX-License-Identifier: GPL-2.0+
/*
 * (C) Copyright 2010
 * Texas Instruments, <www.ti.com>
 *
 * Aneesh V <aneesh@ti.com>
 */
#include <dm.h>
#include <log.h>
#include <part.h>
#include <spl.h>
#include <spl_load.h>
#include <linux/compiler.h>
#include <errno.h>
#include <mmc.h>
#include <image.h>
#include <imx_container.h>
#include <cpu_func.h>
#include <linux/crc16.h>
#include <fdt_support.h>
#include <linux/libfdt.h>
#include <board_config.h>
#include <ems_pack.h>
#include <board_config_am62x.h>


static ulong h_spl_load_read(struct spl_load_info *load, ulong off,
			     ulong size, void *buf)
{
	struct blk_desc *bd = load->priv;
	lbaint_t sector = off >> bd->log2blksz;
	lbaint_t count = size >> bd->log2blksz;

	return blk_dread(bd, sector, count, buf) << bd->log2blksz;
}

static __maybe_unused unsigned long spl_mmc_raw_uboot_offset(int part)
{
#if IS_ENABLED(CONFIG_SYS_MMCSD_RAW_MODE_U_BOOT_USE_SECTOR)
	if (part == 0)
		return CONFIG_SYS_MMCSD_RAW_MODE_U_BOOT_DATA_PART_OFFSET;
#endif

	return 0;
}

static __maybe_unused
int mmc_load_image_raw_sector(struct spl_image_info *spl_image,
			      struct spl_boot_device *bootdev,
			      struct mmc *mmc, unsigned long sector)
{
	int ret;
	struct blk_desc *bd = mmc_get_blk_desc(mmc);
	struct spl_load_info load;

	spl_load_init(&load, h_spl_load_read, bd, bd->blksz);
	ret = spl_load(spl_image, bootdev, &load, 0, sector << bd->log2blksz);
	if (ret) {
		puts("mmc_load_image_raw_sector: mmc block read error\n");
		log_debug("(error=%d)\n", ret);
		return ret;
	}

	return 0;
}

static int spl_mmc_get_device_index(uint boot_device)
{
	switch (boot_device) {
	case BOOT_DEVICE_MMC1:
		return 0;
	case BOOT_DEVICE_MMC2:
	case BOOT_DEVICE_MMC2_2:
		return 1;
	}

	printf("spl: unsupported mmc boot device.\n");

	return -ENODEV;
}

static int spl_mmc_find_device(struct mmc **mmcp, int mmc_dev)
{
	int ret;

#if CONFIG_IS_ENABLED(DM_MMC)
	struct udevice *dev;
	struct uclass *uc;

	log_debug("Selecting MMC dev %d; seqs:\n", mmc_dev);
	if (_LOG_DEBUG) {
		uclass_id_foreach_dev(UCLASS_MMC, dev, uc)
			log_debug("%d: %s\n", dev_seq(dev), dev->name);
	}
	ret = mmc_init_device(mmc_dev);
#else
	ret = mmc_initialize(NULL);
#endif /* DM_MMC */
	if (ret) {
		printf("spl: could not initialize mmc. error: %d\n", ret);
		return ret;
	}
	*mmcp = find_mmc_device(mmc_dev);
	ret = *mmcp ? 0 : -ENODEV;
	if (ret) {
		printf("spl: could not find mmc device %d. error: %d\n",
		       mmc_dev, ret);
		return ret;
	}
#if CONFIG_IS_ENABLED(DM_MMC)
	log_debug("mmc %d: %s\n", mmc_dev, (*mmcp)->dev->name);
#endif

	return 0;
}

#if defined(CONFIG_SYS_MMCSD_RAW_MODE_U_BOOT_USE_PARTITION) || \
    defined(CONFIG_SYS_MMCSD_RAW_MODE_U_BOOT_USE_PARTITION_TYPE)
static int mmc_load_image_raw_partition(struct spl_image_info *spl_image,
					struct spl_boot_device *bootdev,
					struct mmc *mmc, int partition,
					unsigned long sector)
{
	struct disk_partition info;
	int ret;

#ifdef CONFIG_SYS_MMCSD_RAW_MODE_U_BOOT_USE_PARTITION_TYPE
	int type_part;
	/* Only support MBR so DOS_ENTRY_NUMBERS */
	for (type_part = 1; type_part <= DOS_ENTRY_NUMBERS; type_part++) {
		ret = part_get_info(mmc_get_blk_desc(mmc), type_part, &info);
		if (ret)
			continue;
		if (info.sys_ind ==
			CONFIG_SYS_MMCSD_RAW_MODE_U_BOOT_PARTITION_TYPE) {
			partition = type_part;
			break;
		}
	}
#endif

	ret = part_get_info(mmc_get_blk_desc(mmc), partition, &info);
	if (ret) {
		puts("spl: partition error\n");
		return ret;
	}

	return mmc_load_image_raw_sector(spl_image, bootdev, mmc, info.start + sector);
}
#endif

#if CONFIG_IS_ENABLED(FALCON_BOOT_MMCSD)
static int mmc_load_image_raw_os(struct spl_image_info *spl_image,
				 struct spl_boot_device *bootdev,
				 struct mmc *mmc)
{
	int ret;

	ret = mmc_load_image_raw_sector(spl_image, bootdev, mmc,
		CONFIG_SYS_MMCSD_RAW_MODE_KERNEL_SECTOR);
	if (ret)
		return ret;

	if (spl_image->os != IH_OS_LINUX && spl_image->os != IH_OS_TEE) {
		puts("Expected image is not found. Trying to start U-Boot\n");
		return -ENOENT;
	}

#if defined(CONFIG_SYS_MMCSD_RAW_MODE_ARGS_SECTOR)
	unsigned long count;

	count = blk_dread(mmc_get_blk_desc(mmc),
		CONFIG_SYS_MMCSD_RAW_MODE_ARGS_SECTOR,
		CONFIG_SYS_MMCSD_RAW_MODE_ARGS_SECTORS,
		(void *)CONFIG_SPL_PAYLOAD_ARGS_ADDR);
	if (count != CONFIG_SYS_MMCSD_RAW_MODE_ARGS_SECTORS) {
		puts("mmc_load_image_raw_os: mmc block read error\n");
		return -EIO;
	}
#endif	/* CONFIG_SYS_MMCSD_RAW_MODE_ARGS_SECTOR */

	return 0;
}
#else
static int mmc_load_image_raw_os(struct spl_image_info *spl_image,
				 struct spl_boot_device *bootdev,
				 struct mmc *mmc)
{
	return -ENOSYS;
}
#endif

#ifndef CONFIG_SPL_OS_BOOT
int spl_start_uboot(void)
{
	return 1;
}
#endif

#ifdef CONFIG_SYS_MMCSD_FS_BOOT
static int spl_mmc_fs_load_os(struct spl_image_info *spl_image,
			      struct spl_boot_device *bootdev,
			      struct blk_desc *blk_dev, int part)
{
	int err = -ENOSYS;

	if (CONFIG_IS_ENABLED(FS_FAT)) {
		err = spl_load_image_fat_os(spl_image, bootdev, blk_dev, part);
		if (!err)
			return 0;
	}
	if (CONFIG_IS_ENABLED(FS_EXT4)) {
		err = spl_load_image_ext_os(spl_image, bootdev, blk_dev, part);
		if (!err)
			return 0;
	}

	return err;
}

static int __maybe_unused spl_mmc_fs_load(struct spl_image_info *spl_image,
			   struct spl_boot_device *bootdev,
			   struct blk_desc *blk_dev, int part, const char *file)
{
	int err = -ENOENT;

	if (CONFIG_IS_ENABLED(FS_FAT)) {
		err = spl_load_image_fat(spl_image, bootdev, blk_dev, part, file);
		if (!err)
			return 0;
	}
	if (CONFIG_IS_ENABLED(FS_EXT4)) {
		err = spl_load_image_ext(spl_image, bootdev, blk_dev, part, file);
		if (!err)
			return 0;
	}

	return err;
}

static int spl_mmc_do_fs_boot(struct spl_image_info *spl_image,
			      struct spl_boot_device *bootdev,
			      struct mmc *mmc,
			      const char *filename)
{
	int ret = -ENOSYS;

	__maybe_unused int partition = CONFIG_SYS_MMCSD_FS_BOOT_PARTITION;

#if CONFIG_SYS_MMCSD_FS_BOOT_PARTITION == -1
	{
		struct disk_partition info;
		debug("Checking for the first MBR bootable partition\n");
		for (int type_part = 1; type_part <= DOS_ENTRY_NUMBERS; type_part++) {
			ret = part_get_info(mmc_get_blk_desc(mmc), type_part, &info);
			if (ret)
				continue;
			debug("Partition %d is of type %d and bootable=%d\n", type_part, info.sys_ind, info.bootable);
			if (info.bootable != 0) {
				debug("Partition %d is bootable, using it\n", type_part);
				partition = type_part;
				break;
			}
		}
		printf("Using first bootable partition: %d\n", partition);
		if (partition == CONFIG_SYS_MMCSD_FS_BOOT_PARTITION) {
			return -ENOSYS;
		}
	}
#endif

	if (!spl_start_uboot()) {
		ret = spl_mmc_fs_load_os(spl_image, bootdev,
					 mmc_get_blk_desc(mmc), partition);
		if (!ret)
			return 0;
		printf("%s, Failed to load falcon payload: %d\n", __func__,
		       ret);
		if (IS_ENABLED(CONFIG_SPL_OS_BOOT_SECURE))
			return ret;
		printf("Fallback to U-Boot\n");
	}

#ifdef CONFIG_SPL_FS_LOAD_PAYLOAD_NAME
	return spl_mmc_fs_load(spl_image, bootdev, mmc_get_blk_desc(mmc),
			       partition, filename);
#else
	return ret;
#endif
}
#endif

u32 __weak spl_mmc_boot_mode(struct mmc *mmc, const u32 boot_device)
{
#if defined(CONFIG_SPL_FS_FAT) || defined(CONFIG_SPL_FS_EXT4)
	return MMCSD_MODE_FS;
#elif defined(CONFIG_SUPPORT_EMMC_BOOT)
	return MMCSD_MODE_EMMCBOOT;
#else
	return MMCSD_MODE_RAW;
#endif
}

#ifdef CONFIG_SYS_MMCSD_RAW_MODE_U_BOOT_USE_PARTITION
int __weak spl_mmc_boot_partition(const u32 boot_device)
{
	return CONFIG_SYS_MMCSD_RAW_MODE_U_BOOT_PARTITION;
}
#endif

unsigned long __weak arch_spl_mmc_get_uboot_raw_sector(struct mmc *mmc,
						       unsigned long raw_sect)
{
	return raw_sect;
}

unsigned long __weak board_spl_mmc_get_uboot_raw_sector(struct mmc *mmc,
						  unsigned long raw_sect)
{
	return arch_spl_mmc_get_uboot_raw_sector(mmc, raw_sect);
}

unsigned long __weak spl_mmc_get_uboot_raw_sector(struct mmc *mmc,
						  unsigned long raw_sect)
{
	return board_spl_mmc_get_uboot_raw_sector(mmc, raw_sect);
}

int default_spl_mmc_emmc_boot_partition(struct mmc *mmc)
{
	int part;
#ifdef CONFIG_SYS_MMCSD_RAW_MODE_EMMC_BOOT_PARTITION
	part = CONFIG_SYS_MMCSD_RAW_MODE_EMMC_BOOT_PARTITION;
#else
	/*
	 * We need to check what the partition is configured to.
	 * 1 and 2 match up to boot0 / boot1 and 7 is user data
	 * which is the first physical partition (0).
	 */
	part = EXT_CSD_EXTRACT_BOOT_PART(mmc->part_config);
	if (part == EMMC_BOOT_PART_USER)
		part = EMMC_HWPART_DEFAULT;
#endif
	return part;
}

int __weak spl_mmc_emmc_boot_partition(struct mmc *mmc)
{
	return default_spl_mmc_emmc_boot_partition(mmc);
}

static int spl_mmc_get_mmc_devnum(struct mmc *mmc)
{
	struct blk_desc *block_dev;
#if !CONFIG_IS_ENABLED(BLK)
	block_dev = &mmc->block_dev;
#else
	block_dev = mmc_get_blk_desc(mmc);
#endif
	return block_dev->devnum;
}

static struct mmc *mmc;

void spl_mmc_clear_cache(void)
{
	mmc = NULL;
}

int spl_mmc_load(struct spl_image_info *spl_image,
		 struct spl_boot_device *bootdev,
		 const char *filename,
		 int raw_part,
		 unsigned long raw_sect)
{
	u32 boot_mode;
	int ret = 0;
	__maybe_unused int part = 0;
	int mmc_dev;

	/* Perform peripheral init only once for an mmc device */
	mmc_dev = spl_mmc_get_device_index(bootdev->boot_device);
	log_debug("boot_device=%d, mmc_dev=%d\n", bootdev->boot_device,
		  mmc_dev);
	if (!mmc || spl_mmc_get_mmc_devnum(mmc) != mmc_dev) {
		ret = spl_mmc_find_device(&mmc, mmc_dev);
		if (ret)
			return ret;

		ret = mmc_init(mmc);
		if (ret) {
			mmc = NULL;
			printf("spl: mmc init failed with error: %d\n", ret);
			return ret;
		}
	}

	boot_mode = spl_mmc_boot_mode(mmc, bootdev->boot_device);
	ret = -EINVAL;
	switch (boot_mode) {
	case MMCSD_MODE_EMMCBOOT:
		part = spl_mmc_emmc_boot_partition(mmc);

		if (CONFIG_IS_ENABLED(MMC_TINY))
			ret = mmc_switch_part(mmc, part);
		else
			ret = blk_dselect_hwpart(mmc_get_blk_desc(mmc), part);

		if (ret) {
			puts("spl: mmc partition switch failed\n");
			return ret;
		}
		/* Fall through */
	case MMCSD_MODE_RAW:
		debug("spl: mmc boot mode: raw\n");

		if (!spl_start_uboot()) {
			ret = mmc_load_image_raw_os(spl_image, bootdev, mmc);
			if (!ret)
				return 0;
			if (IS_ENABLED(CONFIG_SPL_OS_BOOT_SECURE))
				return ret;
		}

		raw_sect = spl_mmc_get_uboot_raw_sector(mmc, raw_sect);

#ifdef CONFIG_SYS_MMCSD_RAW_MODE_U_BOOT_USE_SECTOR
		ret = mmc_load_image_raw_sector(spl_image, bootdev, mmc,
						raw_sect +
						spl_mmc_raw_uboot_offset(part));
		if (!ret)
			return 0;
#elif defined(CONFIG_SYS_MMCSD_RAW_MODE_U_BOOT_USE_PARTITION) || \
      defined(CONFIG_SYS_MMCSD_RAW_MODE_U_BOOT_USE_PARTITION_TYPE)
		ret = mmc_load_image_raw_partition(spl_image, bootdev,
						   mmc, raw_part,
						   raw_sect);
		if (!ret)
			return 0;
#endif
		/* If RAW mode fails, try FS mode. */
		fallthrough;
#ifdef CONFIG_SYS_MMCSD_FS_BOOT
	case MMCSD_MODE_FS:
		debug("spl: mmc boot mode: fs\n");

		ret = spl_mmc_do_fs_boot(spl_image, bootdev, mmc, filename);
		if (!ret)
			return 0;

		break;
#endif
	default:
		puts("spl: mmc: wrong boot mode\n");
	}

	return ret;
}

int spl_mmc_load_image(struct spl_image_info *spl_image,
		       struct spl_boot_device *bootdev)
{
	return spl_mmc_load(spl_image, bootdev,
#ifdef CONFIG_SPL_FS_LOAD_PAYLOAD_NAME
			    CONFIG_SPL_FS_LOAD_PAYLOAD_NAME,
#else
			    NULL,
#endif
#ifdef CONFIG_SYS_MMCSD_RAW_MODE_U_BOOT_PARTITION
			    spl_mmc_boot_partition(bootdev->boot_device),
#else
			    0,
#endif
#ifdef CONFIG_SYS_MMCSD_RAW_MODE_U_BOOT_SECTOR
			    CONFIG_SYS_MMCSD_RAW_MODE_U_BOOT_SECTOR);
#else
			    0);
#endif
}

SPL_LOAD_IMAGE_METHOD("MMC1", 0, BOOT_DEVICE_MMC1, spl_mmc_load_image);
SPL_LOAD_IMAGE_METHOD("MMC2", 0, BOOT_DEVICE_MMC2, spl_mmc_load_image);
SPL_LOAD_IMAGE_METHOD("MMC2_2", 0, BOOT_DEVICE_MMC2_2, spl_mmc_load_image);

#if defined(CONFIG_EMS_BASE)

/* ============================================================================
 * EMS Custom MMC Load with Signature Verification
 * ============================================================================ */

/**
 * ems_spl_mmc_load_with_verify - 从 MMC 读取带 EMS 签名验证的镜像
 *
 * 数据布局（存储设备上）：
 *   [EMS_PACK_HEAD_T (512B)] + [legacy_img_hdr (64B)] + [image数据]
 *
 * 内存布局（加载后）：
 *   CONFIG_TEXT_BASE - 576:  [EMS_PACK_HEAD_T (512B)]
 *   CONFIG_TEXT_BASE - 64:   [legacy_img_hdr (64B)]
 *   CONFIG_TEXT_BASE:        [image数据] ← 关键！镜像入口点
 *
 * @spl_image: SPL 镜像信息结构体
 * @bootdev:   启动设备信息
 * @filename:  文件名（RAW模式传NULL）
 * @raw_part:  分区号
 * @raw_sect:  扇区号（块计数，512字节/块）
 *
 * 返回值：
 *   0       - 成功
 *   -EINVAL - 魔数验证失败
 *   其他    - MMC 读取错误
 */
int ems_spl_mmc_load_verify(struct spl_image_info *spl_image,
				  struct spl_boot_device *bootdev,
				  const char *filename,
				  int raw_part,
				  unsigned long raw_sect)
{
	int ret;
	int mmc_dev;
	struct blk_desc *bd;
	ulong read_size;
	u32 boot_mode;
    u16 calculated_crc;
	EMS_PACK_HEAD_T *ems_header;
	void *load_buffer;
	/* 计算总头部大小：EMS头(512) + legacy头(64) = 576字节 */
	const ulong TOTAL_HEADER_SIZE = EMS_HEADER_SIZE + sizeof(struct legacy_img_hdr);
    u16 sect_count = 0; 

	/* 1#: 初始化 MMC 设备 */
	mmc_dev = spl_mmc_get_device_index(bootdev->boot_device);
	if (!mmc || spl_mmc_get_mmc_devnum(mmc) != mmc_dev) {
		ret = spl_mmc_find_device(&mmc, mmc_dev);
		if (ret) {
			printf("EMS SPL: Failed to find mmc device: %d\n", ret);
			return ret;
		}

		ret = mmc_init(mmc);
		if (ret) {
			mmc = NULL;
			printf("EMS SPL: mmc init failed: %d\n", ret);
			return ret;
		}
	}

	/* 2#: 检查启动模式并切换分区（EMMCBOOT 模式） */
	boot_mode = spl_mmc_boot_mode(mmc, bootdev->boot_device);
	if (boot_mode == MMCSD_MODE_EMMCBOOT) {
		int part = spl_mmc_emmc_boot_partition(mmc);
		if (CONFIG_IS_ENABLED(MMC_TINY))
			ret = mmc_switch_part(mmc, part);
		else
			ret = blk_dselect_hwpart(mmc_get_blk_desc(mmc), part);

		if (ret) {
			printf("EMS SPL: Partition switch failed: %d\n", ret);
			return ret;
		}
	}

	/* 3#: 获取块设备描述符 */
	bd = mmc_get_blk_desc(mmc);
	if (!bd) {
		printf("EMS SPL: Failed to get block descriptor\n");
		return -ENODEV;
	}

	/* 4#: 获取加载缓冲区
	 * 关键：offset = -(EMS_HEADER_SIZE + sizeof(legacy_img_hdr)) = -576
	 * 返回地址 = CONFIG_TEXT_BASE - 576
	 * 这样可以确保 image 数据正好从 CONFIG_TEXT_BASE 开始
	 */
	load_buffer = spl_get_load_buffer(-TOTAL_HEADER_SIZE, 1 * bd->blksz);

    /* 5#: 读取512字节头部（1个扇区） */
    read_size = blk_dread(bd, raw_sect, 1, load_buffer);
    if (read_size != 1) {
        printf("EMS SPL: Failed to read header, read_size=%ld\n", read_size);
        return -EIO;
    }

	/* 6#: 验证 EMS 头部（位于缓冲区开头） */
	ems_header = (EMS_PACK_HEAD_T *)load_buffer;

	if (memcmp(ems_header->magic, EMS_MAGIC, EMS_MAGIC_LEN) != 0) {
		printf("EMS SPL: error magic: \"EMSFS\"\n");
		return -EINVAL;
	}

    /* 7#: 读取剩余扇区 */
    sect_count = (ems_header->file_size / (1 << bd->log2blksz)) + 1;
    read_size = blk_dread(bd, raw_sect + 1, sect_count, load_buffer + EMS_HEADER_SIZE);
    if (read_size != sect_count) {
        printf("EMS SPL: Failed to read header, read_size=%ld\n", read_size);
        return -EIO;
    }

    /* 5. 验证CRC16 */
    calculated_crc = crc16(0xFFFF, load_buffer + EMS_HEADER_SIZE, ems_header->file_size);
    if (calculated_crc != ems_header->crc16) {
        printf("EMS SPL: CRC16 mismatch! stored=0x%04X, calc=0x%04X\n",
               ems_header->crc16, calculated_crc);
        return -EILSEQ;
    }

    printf("EMS SPL: CRC16 PASS! stored=0x%04X, calc=0x%04X\n",
               ems_header->crc16, calculated_crc);

	return 0;
}

/**
 * ems_parse_partitions - 解析设备树中的 emspart 分区信息
 *
 * @fdt_addr:      设备树地址
 * @emspart_name:  要查找的 emspart 属性值（如 "ems-mmc" 或 "ems-flash"）
 * @parts_array:   目标分区数组
 * @max_parts:     最大分区数量
 *
 * 返回值：找到的分区数量
 */
static int ems_parse_partitions(void *fdt_addr, const char *emspart_name,
                                BOARD_ABILITY_BLK_PARTS_T *parts_array, int max_parts)
{
	int node_offset, partitions_node, subnode_offset;
	const char *label, *emspart, *flag;
	const fdt32_t *reg;
	int label_len, reg_len, flag_len, len;
	int part_count = 0;

	/* 从根节点开始查找指定的 emspart 节点 */
	for (node_offset = fdt_first_subnode(fdt_addr, 0);
	     node_offset >= 0;
	     node_offset = fdt_next_subnode(fdt_addr, node_offset)) {

		emspart = fdt_getprop(fdt_addr, node_offset, "emspart", &len);
		if (emspart && strstr(emspart, emspart_name)) {
			/* 找到目标节点，查找 partitions 子节点 */
			partitions_node = fdt_subnode_offset(fdt_addr, node_offset, "partitions");
			if (partitions_node >= 0) {
				/* 遍历所有分区 */
				for (subnode_offset = fdt_first_subnode(fdt_addr, partitions_node);
				     subnode_offset >= 0 && part_count < max_parts;
				     subnode_offset = fdt_next_subnode(fdt_addr, subnode_offset)) {

					/* 获取 label 属性 */
					label = fdt_getprop(fdt_addr, subnode_offset, "label", &label_len);
					if (label) {
						/* 清零整个 lable 字段 */
						memset(parts_array[part_count].lable, 0, sizeof(parts_array[part_count].lable));
						/* 复制字符串，确保不超过数组大小 */
						int copy_len = (label_len < sizeof(parts_array[part_count].lable)) ?
						               label_len : (sizeof(parts_array[part_count].lable) - 1);
						strncpy(parts_array[part_count].lable, label, copy_len);
						parts_array[part_count].lable[sizeof(parts_array[part_count].lable) - 1] = '\0';
					}

					/* 获取 reg 属性 */
					reg = fdt_getprop(fdt_addr, subnode_offset, "reg", &reg_len);
					if (reg) {
						parts_array[part_count].addr = fdt32_to_cpu(reg[0]);
						parts_array[part_count].size = fdt32_to_cpu(reg[1]);
					}

					/* 获取 bootable 标记 */
					flag = fdt_getprop(fdt_addr, subnode_offset, "bootable", &flag_len);
					parts_array[part_count].flag = flag ? 1 : 0;

					part_count++;
				}
				break;
			}
		}
	}

	return part_count;
}

/**
 * ems_spl_get_partition_info - 根据分区名获取分区信息
 *
 * @part_name:  分区名称（如 "teeos0", "teeos1", "uboot0" 等）
 * @start_sect: 输出参数，返回起始扇区号（512字节/扇区）
 * @size_bytes: 输出参数，返回分区大小（字节）
 *
 * 返回值：
 *   0       - 成功找到分区
 *   -ENOENT - 未找到分区
 *   -EINVAL - 参数错误或共享内存未初始化
 */
int ems_spl_get_partition_info(const char *part_name, unsigned long *start_sect,
                                unsigned int *size_bytes)
{
	BOARD_ABILITY_TABLE_T *shared_ability;
	int i;

	if (!part_name || !start_sect || !size_bytes)
    {
		printf("EMS SPL: Invalid parameters for partition lookup\n");
		return -EINVAL;
	}

	/* 从共享内存读取分区表 */
	shared_ability = (BOARD_ABILITY_TABLE_T *)CONFIG_EMS_SHMEM_ADDR;

	/* 验证共享内存中的魔数 */
	if (memcmp(shared_ability->magic, EMS_MAGIC, EMS_MAGIC_LEN) != 0) {
		printf("EMS SPL: Shared memory not initialized (invalid magic)\n");
		return -EINVAL;
	}

	/* 在 MMC 分区表中查找 */
	for (i = 0; i < shared_ability->stBlk.iPartCount_mmc; i++) {
		if (strcmp(shared_ability->stBlk.stParts_mmc[i].lable, part_name) == 0) {
			/* 找到匹配的分区 */
			*start_sect = shared_ability->stBlk.stParts_mmc[i].addr / 512;
			*size_bytes = shared_ability->stBlk.stParts_mmc[i].size;

			return 0;
		}
	}

	printf("EMS SPL: Partition '%s' not found in partition table\n", part_name);
	return -ENOENT;
}

/**
 * ems_spl_load_image_with_retry - 通用的镜像加载函数，支持失败重试
 *
 * @spl_image:  SPL 镜像信息结构体
 * @bootdev:    启动设备信息
 * @part_prefix: 分区名前缀（如 "teeos" 或 "uboot"）
 *
 * 功能：
 *   从设备树分区表动态获取分区信息，依次尝试加载 part0 -> part1 -> part2
 *   如果某个分区加载失败，自动尝试下一个备份分区
 *
 * 返回值：
 *   0       - 成功加载
 *   -ENOENT - 所有分区均加载失败
 */
int ems_spl_load_image_with_retry(struct spl_image_info *spl_image,
                                    struct spl_boot_device *bootdev,
                                    const char *part_prefix)
{
	char part_name[32];
	int i;
	int ret;

	/* 默认从分区0开始尝试，失败后依次尝试1和2 */
	for (i = 0; i < 3; i++) {
		unsigned long start_sect = 0;
		unsigned int size_bytes = 0;

		/* 构造分区名：teeos0/teeos1/teeos2 或 uboot0/uboot1/uboot2 */
		snprintf(part_name, sizeof(part_name), "%s%d", part_prefix, i);

		/* 从设备树分区表获取分区信息 */
		ret = ems_spl_get_partition_info(part_name, &start_sect, &size_bytes);
		if (ret) {
			printf("EMS SPL: Failed to get partition info for %s, ret=%d\n",
			       part_name, ret);
			continue;
		}

		/* 校验分区 */
		ret = ems_spl_mmc_load_verify(spl_image, bootdev, NULL, 0, start_sect);
		if (ret) {
			printf("EMS SPL: %s verify failed, ret=%d, trying next partition\n",
			       part_name, ret);
			continue;
		}

		/* 加载镜像 */
		ret = spl_mmc_load(spl_image, bootdev, NULL, 0, start_sect + 1);
		if (ret) {
			printf("EMS SPL: %s load failed, ret=%d, trying next partition\n",
			       part_name, ret);
			continue;
		}

		return 0;
	}

	printf("EMS SPL: Failed to load %s from all partitions (0, 1, 2)\n", part_prefix);
	return -ENOENT;
}

/**
 * ems_spl_fdt_init - SPL阶段设备树预加载和分区信息解析
 *
 * 功能：
 *   1. 从默认地址加载设备树到内存
 *   2. 解析设备树中的分区信息（ems-mmc 和 ems-flash）
 *   3. 将解析结果填充到共享内存 0x98000000
 *   4. 打印分区信息
 *
 * 返回值：
 *   0       - 成功
 *   非0     - 失败（不影响启动流程）
 */
int ems_spl_fdt_init(struct spl_boot_device *bootdev)
{
	int ret;
	struct blk_desc *bd;
	int mmc_dev;
	ulong read_size;
	ulong fdt_sector;
	void *fdt_addr = (void *)CONFIG_AM62X_HEX_FDT_ADDR;
	BOARD_ABILITY_TABLE_T *shared_ability = (BOARD_ABILITY_TABLE_T *)CONFIG_EMS_SHMEM_ADDR;
	BOARD_ABILITY_TABLE_T local_ability;
	int mmc_part_count = 0, flash_part_count = 0;
	int fdt_partition_idx;
	int load_success = 0;

	/* FDT分区地址数组 */
	const ulong fdt_base_addrs[] = {
		CONFIG_AM62X_FDT0_DEVADDR,  /* fdt0: 0x00900000 */
		CONFIG_AM62X_FDT1_DEVADDR,  /* fdt1: 0x00980000 */
		CONFIG_AM62X_FDT2_DEVADDR   /* fdt2: 0x00a00000 */
	};
	const int fdt_partition_count = sizeof(fdt_base_addrs) / sizeof(fdt_base_addrs[0]);

	/* 检查 MMC 设备是否已初始化 */
	/* 1#: 初始化 MMC 设备 */
	mmc_dev = spl_mmc_get_device_index(bootdev->boot_device);
	if (!mmc || spl_mmc_get_mmc_devnum(mmc) != mmc_dev) {
		ret = spl_mmc_find_device(&mmc, mmc_dev);
		if (ret) {
			printf("EMS SPL: Failed to find mmc device: %d\n", ret);
			return ret;
		}

		ret = mmc_init(mmc);
		if (ret) {
			mmc = NULL;
			printf("EMS SPL: mmc init failed: %d\n", ret);
			return ret;
		}
	}

	bd = mmc_get_blk_desc(mmc);
	if (!bd) {
		printf("SPL: Failed to get block descriptor\n");
		return -1;
	}

	/* 依次尝试从 fdt0, fdt1, fdt2 加载设备树 */
	for (fdt_partition_idx = 0; fdt_partition_idx < fdt_partition_count; fdt_partition_idx++)
    {
		/* 1# 从当前FDT分区地址读取 EMS 打包头 (512字节) */
		fdt_sector = fdt_base_addrs[fdt_partition_idx] / bd->blksz;

		printf("SPL: Loading EMS header from sector 0x%lx to 0x%p\n", fdt_sector, fdt_addr);

		/* 读取第一个块（包含 EMS_PACK_HEAD_T） */
		read_size = blk_dread(bd, fdt_sector, 1, fdt_addr);
		if (read_size != 1) {
			printf("SPL: Failed to read EMS header from fdt%d, read_size=%ld\n",
			       fdt_partition_idx, read_size);
			continue;  /* 尝试下一个分区 */
		}

		/* 2# 验证 EMS 打包头 */
		EMS_PACK_HEAD_T *ems_header = (EMS_PACK_HEAD_T *)fdt_addr;
		if (memcmp(ems_header->magic, EMS_MAGIC, EMS_MAGIC_LEN) != 0) {
			printf("SPL: Invalid EMS magic in fdt%d, expected \"EMSFS\"\n",
			       fdt_partition_idx);
			continue;  /* 尝试下一个分区 */
		}

		/* 3# 读取剩余的设备树数据 */
		ulong sect_count = (ems_header->file_size / bd->blksz) + 1;

		read_size = blk_dread(bd, fdt_sector + 1, sect_count, fdt_addr + EMS_HEADER_SIZE);
		if (read_size != sect_count) {
			printf("SPL: Failed to read FDT data from fdt%d, read_size=%ld, expected=%ld\n",
			       fdt_partition_idx, read_size, sect_count);
			continue;  /* 尝试下一个分区 */
		}

		/* 4# 验证 CRC16 */
		u16 calculated_crc = crc16(0xFFFF, (u8 *)(fdt_addr + EMS_HEADER_SIZE), ems_header->file_size);
		if (calculated_crc != ems_header->crc16) {
			printf("SPL: CRC16 mismatch in fdt%d! stored=0x%04X, calc=0x%04X\n",
			       fdt_partition_idx, ems_header->crc16, calculated_crc);
			continue;  /* 尝试下一个分区 */
		}

		/* 5# 设备树数据从 EMS_HEADER_SIZE 偏移开始 */
		void *fdt_data = fdt_addr + EMS_HEADER_SIZE;

		/* 6# 检查设备树有效性 */
		ret = fdt_check_header(fdt_data);
		if (ret < 0) {
			printf("SPL: Invalid FDT header in fdt%d: %d\n", fdt_partition_idx, ret);
			continue;  /* 尝试下一个分区 */
		}

		/* 7# 更新 fdt_addr 指向实际的设备树数据 */
		fdt_addr = fdt_data;
		load_success = 1;
		break;  /* 成功加载，退出循环 */
	}

	/* 检查是否成功加载了FDT */
	if (!load_success) {
		printf("SPL: Failed to load FDT from all partitions (fdt0, fdt1, fdt2)\n");
		return -1;
	}

	printf("SPL: Successfully loaded FDT from fdt%d partition\n", fdt_partition_idx);

	/* 3# 初始化本地 BOARD_ABILITY_TABLE_T 结构 */
	memset(&local_ability, 0, sizeof(BOARD_ABILITY_TABLE_T));
	memcpy(local_ability.magic, EMS_MAGIC, EMS_MAGIC_LEN);

	/* 4# 解析 ems-mmc 分区信息 */
	mmc_part_count = ems_parse_partitions(fdt_addr, "ems-mmc",
	                                      local_ability.stBlk.stParts_mmc,
	                                      MAX_PART_NUM);
	local_ability.stBlk.iPartCount_mmc = mmc_part_count;

	/* 5# 解析 ems-flash 分区信息 */
	flash_part_count = ems_parse_partitions(fdt_addr, "ems-flash",
	                                        local_ability.stBlk.stParts_flash,
	                                        MAX_PART_NUM);
	local_ability.stBlk.iPartCount_flash = flash_part_count;

#if 0
	/* 6# 打印 MMC 分区信息 */
	if (mmc_part_count > 0) {
		printf("\nSPL: MMC partition table (ems-mmc):\n");
		printf("====================================================================================\n");
		printf("%-10s\t%-20s\t%-20s\t%-20s\t%-10s\n",
		       "number:", "partname:", "start:", "size:", "type");

		for (int i = 0; i < mmc_part_count; i++) {
			printf("%-10d\t%-20s\t0x%-18x\t0x%-18x\t%-10s\n",
			       i + 1,
			       local_ability.stBlk.stParts_mmc[i].lable,
			       local_ability.stBlk.stParts_mmc[i].addr,
			       local_ability.stBlk.stParts_mmc[i].size,
			       local_ability.stBlk.stParts_mmc[i].flag ? "bootable" : "--");
		}
		printf("====================================================================================\n\n");
	}

	/* 7# 打印 Flash 分区信息 */
	if (flash_part_count > 0) {
		printf("\nSPL: Flash partition table (ems-flash):\n");
		printf("====================================================================================\n");
		printf("%-10s\t%-20s\t%-20s\t%-20s\t%-10s\n",
		       "number:", "partname:", "start:", "size:", "type");

		for (int i = 0; i < flash_part_count; i++) {
			printf("%-10d\t%-20s\t0x%-18x\t0x%-18x\t%-10s\n",
			       i + 1,
			       local_ability.stBlk.stParts_flash[i].lable,
			       local_ability.stBlk.stParts_flash[i].addr,
			       local_ability.stBlk.stParts_flash[i].size,
			       local_ability.stBlk.stParts_flash[i].flag ? "bootable" : "--");
		}
		printf("====================================================================================\n\n");
	}
#endif

	/* 8# 复制到共享内存 0x98000000 */
	memcpy(shared_ability, &local_ability, sizeof(BOARD_ABILITY_TABLE_T));

	/* 9# 刷新 cache，确保数据写入物理内存，以便 U-Boot 阶段能够正确读取 */
	flush_dcache_range((unsigned long)shared_ability,
	                   (unsigned long)shared_ability + sizeof(BOARD_ABILITY_TABLE_T));

	return 0;
}


#endif

