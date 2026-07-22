#ifndef _TEST_CDC_H
#define _TEST_CDC_H

#include "usbd_cdc_acm.h"

/* CDC ACM test：端点分配必须落在 F407 USB FS 的 4 个端点(0~3)内。
   notify(INT) 用 EP2(0x82, IN)，BULK IN/OUT 双向共用 EP3(0x83 / 0x03)。
   注意：WebUSB 也用 EP2，二者不能同时启用（见 task_usb.h 的 #error 检查）。 */
#define CDC_INT_EP   0x82
#define CDC_OUT_EP   0x03
#define CDC_IN_EP    0x83
#ifdef CONFIG_USB_HS
#define CDC_EP_MPS   512
#else
#define CDC_EP_MPS   64
#endif

/* 接口初始化：注册 CDC 两个接口 + BULK IN/OUT + INT 通知端点 */
void test_cdc_init(uint8_t busid);
/* USB 事件处理：配置完成后挂载 OUT 读，开始接收 PC 屏数据 */
void test_cdc_event(uint8_t busid, uint8_t event);
/* 主循环轮询：检测整帧收齐后刷到 LCD GRAM */
void test_cdc_poll(uint8_t busid);

#endif
