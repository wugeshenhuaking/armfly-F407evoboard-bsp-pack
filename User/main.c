/*
*********************************************************************************************************
*
*   Module Name : Main Program Module
*   File Name   : main.c
*   Version     : V1.0
*   Description : Running LED (Marquee).
*                 Experiment Purpose:
*                   1. Learn how to implement running LEDs on the F407 platform.
*                 Experiment Content:
*                   1. Start an auto-reload software timer to toggle LED1 and LED2 every 100ms.
*                   2. Start another auto-reload software timer to toggle LED3 and LED4 every 500ms.
*                 Notes:
*                   1. It is recommended to use the serial tool SecureCRT to view print info,
*                      baud rate 115200, 8 data bits, no parity, 1 stop bit.
*                   2. Please set the editor indent and TAB to 4 for proper code alignment.
*
*   Revision History:
*       Version  Date         Author      Description
*       V1.0    2019-04-23   Eric2013    1. CMSIS pack version V5.5.0
*                                        2. HAL library version V2.4.0
*
*   Copyright (C), 2018-2030, www.armfly.com
*
*********************************************************************************************************
*/
#include "bsp.h"            /* Hardware abstraction layer */

/* Define example name and release date */
#define EXAMPLE_NAME    "V5-Running LED"
#define EXAMPLE_DATE    "2019-04-23"
#define DEMO_VER        "1.0"

static void PrintfLogo(void);
static void PrintfHelp(void);

/*
*********************************************************************************************************
*   Function: main
*   Description: C program entry
*   Parameters: None
*   Return: Error code (no action required)
*********************************************************************************************************
*/
int main(void)
{
    bsp_Init();     /* Hardware initialization */
    PrintfLogo();   /* Print example name and version info */
    PrintfHelp();   /* Print operation tips */
    /* Brief LED1 blink to indicate startup */
    bsp_LedOn(1);
    bsp_DelayMS(100);
    bsp_LedOff(1);
    bsp_DelayMS(100);
    bsp_StartAutoTimer(0, 100); /* Start a 100ms auto-reload timer */
    bsp_StartAutoTimer(1, 500); /* Start a 500ms auto-reload timer */
    /* Enter main loop */
    while (1)
    {
        bsp_Idle();     /* This function is in bsp.c. Users can modify it for CPU sleep and watchdog feeding */
        /* Check timer timeout */
        if (bsp_CheckTimer(0))
        {
            /* Enter here every 100ms */
            bsp_LedToggle(1);
        }
        /* Check timer timeout */
        if (bsp_CheckTimer(1))
        {
            /* Enter here every 500ms */
            bsp_LedToggle(2);
            bsp_LedToggle(3);
            bsp_LedToggle(4);
            printf("test\n");
        }
    }
}

/*
*********************************************************************************************************
*   Function: PrintfHelp
*   Description: Print operation tips
*   Parameters: None
*   Return: None
*********************************************************************************************************
*/
static void PrintfHelp(void)
{
    printf("Operation Tips:\r\n");
    printf("1. Start an auto-reload software timer to toggle LED1 and LED2 every 100ms\r\n");
    printf("2. Start another auto-reload software timer to toggle LED3 and LED4 every 500ms\r\n");
}

/*
*********************************************************************************************************
*   Function: PrintfLogo
*   Description: Print example name and release date. Connect serial cable and open
*                a terminal program on PC to view the results.
*   Parameters: None
*   Return: None
*********************************************************************************************************
*/
static void PrintfLogo(void)
{
    printf("*************************************************************\n\r");
    /* Read CPU ID */
    {
        uint32_t CPU_Sn0, CPU_Sn1, CPU_Sn2;
        CPU_Sn0 = *(__IO uint32_t*)(0x1FFF7A10);
        CPU_Sn1 = *(__IO uint32_t*)(0x1FFF7A10 + 4);
        CPU_Sn2 = *(__IO uint32_t*)(0x1FFF7A10 + 8);
        printf("\r\nCPU : STM32F407IGT6, LQFP176, Clock: %dMHz\r\n", SystemCoreClock / 1000000);
        printf("UID = %08X %08X %08X\n\r", CPU_Sn2, CPU_Sn1, CPU_Sn0);
    }
    printf("*************************************************************\n");
    printf("* Project    : %s\r\n", EXAMPLE_NAME);
    printf("* Version    : %s\r\n", DEMO_VER);
    printf("* Date       : %s\r\n", EXAMPLE_DATE);
    printf("* HAL Lib    : V2.4.0 (STM32F407 HAL Driver)\r\n");
    printf("* \r\n");
    printf("* QQ         : 1295744630\r\n");
    printf("* AliWangWang: armfly\r\n");
    printf("* Email      : armfly@qq.com\r\n");
    printf("* WeChat     : armfly_com\r\n");
    printf("* Taobao     : armfly.taobao.com\r\n");
    printf("* Copyright www.armfly.com AnFuLee Electronics\r\n");
    printf("*************************************************************\n");
}

/***************************** www.armfly.com (END OF FILE) *********************************/

