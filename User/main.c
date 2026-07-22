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
#include "bsp.h"     /* Hardware abstraction layer */
#include "utility.h" /* General utility functions */
#include "task_usb.h"
#include "LCD/framebuffer.h"   /* 软件显存（外部SRAM镜像），提供 LCD_FRAMEBUFFER 等 */
/* Define example name and release date */
#define EXAMPLE_NAME "V5-Running LED"
#define EXAMPLE_DATE "2019-04-23"
#define DEMO_VER "1.0"

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

typedef struct
{
    time_ms_t timer;
} task_led_t;

task_led_t led_task = {0};

void app_task_led_init(task_led_t *task)
{
    left_ms_set(&task->timer, 500);
}

typedef struct
{
    time_ms_t timer;
} task_fb_t;

task_fb_t fb_task = {0};

void app_task_fb_init(task_fb_t *task)
{
    left_ms_set(&task->timer, 100);
}

void app_task_fb(task_fb_t *task)
{
    if (left_ms(&task->timer))
    {
        return;
    }

    /* CDC 接收 PC 屏数据并绘制到 MCU 屏（当前主用功能）。
       收到完整一帧后在 cdc_fb_service 内整屏刷到 LCD GRAM。 */
    cdc_fb_service();

    /* 原 LCD 动态演示（移动红方块 / 计数器）会持续改写显存，与 CDC 整屏
       覆盖互相干扰，故默认关闭；如需验证 WebUSB 脏区域镜像可重新启用。 */
#if 0
    static uint16_t box_x = 0;
    lcd_fill(box_x, 750, box_x + 30, 780, WHITE);   /* 擦除上一帧方块（首帧擦白底，无副作用） */
    box_x += 6;
    if (box_x > FB_WIDTH - 40) {
        box_x = 0;
    }
    lcd_fill(box_x, 750, box_x + 30, 780, RED);     /* 画新方块（标记脏 tile） */

    static uint32_t cnt = 0;
    static uint8_t  cnt_tick = 0;
    if (++cnt_tick >= 10) {                          /* 100ms * 10 = 1s */
        cnt_tick = 0;
        lcd_fill(70, 135, 200, 151, WHITE);         /* 清除数字区（保留 "CNT:" 前缀） */
        lcd_show_num(70, 135, cnt, 5, 16, RED);     /* 显示 5 位自增数字 */
        cnt++;
    }
#endif

    /* WebUSB 镜像轮询（当前已在 task_usb.c 中通过 WEBUSB_TX_ENABLED 关闭发送，
       保留调用不影响；如需恢复镜像发送，把该宏置 1 重编译即可）。 */
    webusb_fb_poll(0);

    left_ms_set(&task->timer, 20);   /* CDC 全量约 1fps，20ms 轮询足够且省 CPU */
}

void app_task_led(task_led_t *task)
{
    if (left_ms(&task->timer))
    {
        return;
    }
    // LED task logic here
    bsp_LedToggle(1);
    printf("LED1 toggled\r\n");
    left_ms_set(&task->timer, 500);
}

static void JumpToBootloader(void);

int main(void)
{
    bsp_Init();   /* Hardware initialization */
    PrintfLogo(); /* Print example name and version info */
    PrintfHelp(); /* Print operation tips */
    app_task_led_init(&led_task);
    app_task_fb_init(&fb_task);
    static volatile uint8_t jump_flag = 0;

    webusb_hid_keyboard_init(0, USB_OTG_FS_PERIPH_BASE);

    /* Enter main loop */
    while (1)
    {
        bsp_Idle(); /* This function is in bsp.c. Users can modify it for CPU sleep and watchdog feeding */
        app_task_led(&led_task); 
        app_task_fb(&fb_task);
    }
}

static void JumpToBootloader(void)
{
    uint32_t i = 0;
    void (*SysMemBootJump)(void);        /* Declare a function pointer */
    __IO uint32_t BootAddr = 0x1FFF0000; /* System BootLoader address of STM32F4 */
    /* Disable global interrupts */
    DISABLE_INT();
    /* Disable SysTick timer and reset it to default values */
    SysTick->CTRL = 0;
    SysTick->LOAD = 0;
    SysTick->VAL = 0;
    /* Reset all clocks to default state, switch to HSI clock */
    HAL_RCC_DeInit();
    /* Disable all interrupts and clear all pending interrupt flags */
    for (i = 0; i < 8; i++)
    {
        NVIC->ICER[i] = 0xFFFFFFFF;
        NVIC->ICPR[i] = 0xFFFFFFFF;
    }
    /* Enable global interrupts */
    ENABLE_INT();
    /* Jump to system BootLoader: first word is MSP, offset +4 is reset handler address */
    SysMemBootJump = (void (*)(void))(*((uint32_t *)(BootAddr + 4)));
#if defined(__ARMCC_VERSION) && (__ARMCC_VERSION < 6000000)
    /*
     * AC5 (ARMCC):
     *   AC5 recognizes SysMemBootJump() as a tail call and generates a plain
     *   BX instruction without touching the stack, so the CMSIS helpers are fine.
     */
    __set_MSP(*(uint32_t *)BootAddr);
    __DSB();
    __ISB();

    __set_CONTROL(0);
    __DSB();
    __ISB();
    SysMemBootJump();
#elif defined(__clang__) || defined(__GNUC__)
    /*
     * AC6 (ARMCLANG / Clang):
     *   AC6 does NOT treat SysMemBootJump() as a tail call; it inserts PUSH/POP
     *   after __set_MSP. By then MSP already points into system memory, so the
     *   stack write triggers a BusFault. Use inline asm to guarantee that no
     *   stack operation is inserted between MSR MSP -> MSR CONTROL -> BX.
     */
    __ASM volatile(
        "msr msp, %0\n"     /* Set main stack pointer (MSP) */
        "msr control, %1\n" /* CONTROL=0: privileged mode + use MSP */
        "isb\n"             /* Instruction synchronization barrier, flush pipeline */
        "bx %2\n"           /* Jump to system BootLoader */
        : : "r"(*(uint32_t *)BootAddr),
        "r"((uint32_t)0), "r"(SysMemBootJump) : "memory");
#else
#error "Unsupported compiler — jump_to_app requires inline assembly to set MSP"
#endif
    /* If the jump succeeds, code never reaches here; user may add code below */
    while (1)
    {
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
        CPU_Sn0 = *(__IO uint32_t *)(0x1FFF7A10);
        CPU_Sn1 = *(__IO uint32_t *)(0x1FFF7A10 + 4);
        CPU_Sn2 = *(__IO uint32_t *)(0x1FFF7A10 + 8);
        printf("\r\nCPU : STM32F407IGT6, LQFP176, Clock: %dMHz\r\n", SystemCoreClock / 1000000);
        printf("UID = 0x%08X 0x%08X 0x%08X\n\r", CPU_Sn2, CPU_Sn1, CPU_Sn0);
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
