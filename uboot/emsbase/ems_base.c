

#include <stdarg.h>
#include <command.h>

#include <console.h>
#include <fat.h>
#include <env.h>
#include <mmc.h>
#include <malloc.h>
#include <net.h>
#include <image.h>
#include <init.h>


DECLARE_GLOBAL_DATA_PTR;


int do_emsboot(void)
{
    int iRet = 0;

    printf("ems board init\n");

    return iRet;
}

