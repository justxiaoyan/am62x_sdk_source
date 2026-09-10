// SPDX-License-Identifier: GPL-2.0+
/*
 * emsflash command - display partition information
 */

#include <common.h>
#include <command.h>
#include <ems_blk.h>
#include <board_config.h>

/* 显示分区表 */
static void display_partition_table(BOARD_ABILITY_BLK_PARTS_T *pblkinfo, int part_count, const char *title)
{
    int i;

    printf("\n%s:\n", title);
    printf("====================================================================================\n");
    printf("%-10s\t%-20s\t%-20s\t%-20s\t%-10s\n", "number:", "partname:", "start:", "size:", "type");

    for (i = 0; i < part_count; i++)
    {
        printf("%-10d\t%-20s\t0x%-18x\t0x%-18x\t%-10s\n",
               i + 1,
               pblkinfo[i].lable,
               pblkinfo[i].addr,
               pblkinfo[i].size,
               pblkinfo[i].flag ? "bootable" : "--");
    }

    printf("====================================================================================\n\n");
}

static int do_emsflash(struct cmd_tbl *cmdtp, int flag, int argc, char *const argv[])
{
    BOARD_ABILITY_TABLE_T *pstAbi = NULL;

    pstAbi = obc_ability_get();

    if (argc < 2)
    {
        printf("Usage: emsflash <-l> [mmc|flash] | emsflash <mmc|flash> erase <partname>\n");
        return CMD_RET_USAGE;
    }

    if (!strcmp(argv[1], "-l"))
    {
        if (argc == 2)
        {
            /* emsflash -l: 显示所有分区信息 */
            if (pstAbi->stBlk.iPartCount_mmc > 0)
            {
                display_partition_table(pstAbi->stBlk.stParts_mmc, pstAbi->stBlk.iPartCount_mmc,
                                       "MMC partition table (ems-mmc)");
            }
            else
            {
                printf("No MMC partition information available\n");
            }

            if (pstAbi->stBlk.iPartCount_flash > 0)
            {
                display_partition_table(pstAbi->stBlk.stParts_flash, pstAbi->stBlk.iPartCount_flash,
                                       "Flash partition table (ems-flash)");
            }
            else
            {
                printf("No Flash partition information available\n");
            }
        }
        else if (argc == 3)
        {
            if (!strcmp(argv[2], "mmc"))
            {
                /* emsflash -l mmc: 显示 ems-mmc 分区信息 */
                if (pstAbi->stBlk.iPartCount_mmc > 0)
                {
                    display_partition_table(pstAbi->stBlk.stParts_mmc, pstAbi->stBlk.iPartCount_mmc,
                                           "MMC partition table (ems-mmc)");
                }
                else
                {
                    printf("No MMC partition information available\n");
                }
            }
            else if (!strcmp(argv[2], "flash"))
            {
                /* emsflash -l flash: 显示 ems-flash 分区信息 */
                if (pstAbi->stBlk.iPartCount_flash > 0)
                {
                    display_partition_table(pstAbi->stBlk.stParts_flash, pstAbi->stBlk.iPartCount_flash,
                                           "Flash partition table (ems-flash)");
                }
                else
                {
                    printf("No Flash partition information available\n");
                }
            }
            else
            {
                printf("Unknown partition type: %s\n", argv[2]);
                return CMD_RET_USAGE;
            }
        }
        else
        {
            printf("Usage: emsflash <-l> [mmc|flash]\n");
            return CMD_RET_USAGE;
        }
    }
    else if (!strcmp(argv[1], "mmc") || !strcmp(argv[1], "flash"))
    {
        /* emsflash <mmc|flash> erase <partname> */
        if (argc == 4 && !strcmp(argv[2], "erase"))
        {
            const char *partname = argv[3];
            int ret;

            /* 根据类型调用对应的擦除函数 */
            if (!strcmp(argv[1], "mmc"))
            {
                ret = ems_blk_erase_mmc_partition(pstAbi, (char *)partname);
            }
            else
            {
                ret = ems_blk_erase_flash_partition(pstAbi, (char *)partname);
            }

            if (0 != ret)
            {
                return CMD_RET_FAILURE;
            }
        }
        else
        {
            printf("Usage: emsflash <mmc|flash> erase <partname>\n");
            return CMD_RET_USAGE;
        }
    }
    else
    {
        printf("Unknown command: %s\n", argv[1]);
        return CMD_RET_USAGE;
    }

    return 0;
}

U_BOOT_CMD(
	emsflash, 4, 0, do_emsflash,
	"display and manage partition information",
	"-l            - display all partition information (mmc + flash)\n"
	"emsflash -l mmc      - display ems-mmc partition information\n"
	"emsflash -l flash    - display ems-flash partition information\n"
	"emsflash mmc erase <partname>   - erase mmc partition\n"
	"emsflash flash erase <partname> - erase flash partition\n"
);
