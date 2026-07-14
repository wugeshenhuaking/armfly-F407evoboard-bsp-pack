/*
*********************************************************************************************************
*
*	Module Name : USB driver module (STM32F407 OTG_FS)
*	File Name   : bsp_usb.h
*	Version     : V1.0
*	Description : Header file of the USB OTG FS board support driver.
*
*	Copyright (C), 2026, AnFuLi Electronics www.armfly.com
*
*********************************************************************************************************
*/

#ifndef __BSP_USB_H
#define __BSP_USB_H

/* Function declarations exposed to other modules */
void bsp_InitUsb(void);
void bsp_InitHardUsb(void);
void bsp_InitVarUsb(void);

uint8_t bsp_IsUsbCableConnected(void);

#endif

/***************************** AnFuLi Electronics www.armfly.com (END OF FILE) *********************************/
