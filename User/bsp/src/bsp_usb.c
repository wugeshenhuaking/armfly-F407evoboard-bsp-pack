/*
*********************************************************************************************************
*
*   Module Name : USB driver module (STM32F407 OTG_FS)
*   File Name   : bsp_usb.c
*   Version     : V1.0
*   Description : Board support driver for the STM32F407 USB OTG FS peripheral.
*               This module enables the USB OTG FS clock, configures the DM/DP
*               pins (PA11/PA12) and the VBUS sense pin (PA9). It is a hardware
*               level BSP; the USB protocol stack (device/host) is built on top.
*
*   Revision History:
*       Version   Date        Author   Description
*       V1.0    2026-07-14  User     Initial release
*
*   Copyright (C), 2026, AnFuLi Electronics www.armfly.com
*
*********************************************************************************************************
*/

#include "bsp.h"
/*
    STM32-V5 USB OTG FS pin assignment:
        PA11 : OTG_FS_DM   (alternate function 10)
        PA12 : OTG_FS_DP   (alternate function 10)
        PA9  : OTG_FS_VBUS (VBUS sense input, 5V tolerant)
        PA10 : OTG_FS_ID   (OTG ID, used in host mode, left as default here)
*/

/* Software running state of the USB driver */
static uint8_t s_UsbConnectState = 0;   /* 0 = cable disconnected, 1 = connected */

/*
*********************************************************************************************************
*   Function   : bsp_InitUsb
*   Description : Initialize the USB OTG FS peripheral. Entry point called by bsp_Init(),
*                 it calls bsp_InitHardUsb() and bsp_InitVarUsb().
*   Parameters : None
*   Return      : None
*********************************************************************************************************
*/
void bsp_InitUsb(void)
{
    /* Step 1: Hardware initialization (clock, GPIO, registers) */
    bsp_InitHardUsb();
    /* Step 2: Software variable / default state initialization */
    bsp_InitVarUsb();
    /* Step 3: Software variable / default state initialization */
}

/*
*********************************************************************************************************
*   Function   : bsp_InitHardUsb
*   Description : Hardware initialization of USB OTG FS (enable clock, configure GPIO).
*   Parameters : None
*   Return      : None
*********************************************************************************************************
*/
void bsp_InitHardUsb(void)
{
    // GPIO_InitTypeDef gpio_init;
    // /* Step 1: Enable GPIOA and USB OTG FS peripheral clocks */
    // __HAL_RCC_GPIOA_CLK_ENABLE();
    // __HAL_RCC_USB_OTG_FS_CLK_ENABLE();
    // /* Step 2: Configure PA11 (DM) and PA12 (DP) as USB OTG FS alternate function */
    // gpio_init.Mode = GPIO_MODE_AF_PP;
    // gpio_init.Pull = GPIO_NOPULL;
    // gpio_init.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    // gpio_init.Alternate = GPIO_AF10_OTG_FS;
    // gpio_init.Pin = GPIO_PIN_11;   /* OTG_FS_DM */
    // HAL_GPIO_Init(GPIOA, &gpio_init);
    // gpio_init.Pin = GPIO_PIN_12;   /* OTG_FS_DP */
    // HAL_GPIO_Init(GPIOA, &gpio_init);
    // /* Step 3: Configure PA9 as VBUS sense input (used for cable detection) */
    // gpio_init.Mode = GPIO_MODE_INPUT;
    // gpio_init.Pull = GPIO_NOPULL;
    // gpio_init.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    // gpio_init.Pin = GPIO_PIN_9;    /* OTG_FS_VBUS */
    // HAL_GPIO_Init(GPIOA, &gpio_init);
}

/*
*********************************************************************************************************
*   Function   : bsp_InitVarUsb
*   Description : Software initialization of USB (initialize variables, set default state).
*   Parameters : None
*   Return      : None
*********************************************************************************************************
*/
void bsp_InitVarUsb(void)
{
    /* Step 1: Initialize software variables and default parameters */
    s_UsbConnectState = 0;
}

/*
*********************************************************************************************************
*   Function   : bsp_IsUsbCableConnected
*   Description : Detect whether the USB cable is plugged in by reading the VBUS pin.
*   Parameters : None
*   Return      : 1 = cable connected (VBUS present), 0 = not connected
*********************************************************************************************************
*/
uint8_t bsp_IsUsbCableConnected(void)
{
    if ((GPIOA->IDR & GPIO_PIN_9) != 0)
    {
        s_UsbConnectState = 1;
        return 1;
    }
    s_UsbConnectState = 0;
    return 0;
}


void HAL_PCD_MspInit(PCD_HandleTypeDef* pcdHandle)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    if(pcdHandle->Instance == USB_OTG_FS)
    {
        /* GPIO settings：PA11 PA12 */
        __HAL_RCC_GPIOA_CLK_ENABLE();
        GPIO_InitStruct.Pin = GPIO_PIN_11 | GPIO_PIN_12;
        GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
        GPIO_InitStruct.Pull = GPIO_NOPULL;
        GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
        GPIO_InitStruct.Alternate = GPIO_AF10_OTG_FS;
        HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
        /* USB_OTG_FS clk enable */
        __HAL_RCC_USB_OTG_FS_CLK_ENABLE();
        /* nvic settings*/
        HAL_NVIC_SetPriority(OTG_FS_IRQn, 4, 0);
        HAL_NVIC_EnableIRQ(OTG_FS_IRQn);
    }
}

void HAL_PCD_MspDeInit(PCD_HandleTypeDef* pcdHandle)
{
    if(pcdHandle->Instance == USB_OTG_FS)
    {
        __HAL_RCC_USB_OTG_FS_CLK_DISABLE();
        HAL_GPIO_DeInit(GPIOA, GPIO_PIN_11 | GPIO_PIN_12);
    }
}
/***************************** AnFuLi Electronics www.armfly.com (END OF FILE) *********************************/
