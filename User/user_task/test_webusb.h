#ifndef _TEST_WEBUSB_H
#define _TEST_WEBUSB_H

#include "usbd_core.h"

#if USB_TEST_WEBUSB
/* WebUSB 镜像 test：与 CDC 共用 EP2(0x82 / 0x02)，二者不能同时启用
   （见 task_usb.h 中的 #error 检查）。启用本 test 时按 USB_TEST_WEBUSB=1 重编译。 */
#define WEBUSB_IN_EP    0x82
#define WEBUSB_OUT_EP   0x02
#ifdef CONFIG_USB_HS
#define WEBUSB_EP_MPS   512
#else
#define WEBUSB_EP_MPS   64
#endif

/* 这些描述符实体定义在本模块的 .c 中，框架层 task_usb.c 按需在 usb_descriptor 中引用 */
extern struct usb_webusb_descriptor webusb_url_desc;
extern struct usb_msosv2_descriptor msosv2_desc;
extern struct usb_bos_descriptor bos_desc;

/* 通过 WebUSB bulk IN 端点向浏览器发送任意数据（异步，完成清 webusb_tx_busy） */
int  webusb_send_data(uint8_t busid, const uint8_t *data, uint32_t len);
/* 接口初始化：注册 WebUSB 接口 + IN/OUT 端点 */
void test_webusb_init(uint8_t busid);
/* USB 事件处理：配置后挂载 OUT 读、置"强制整帧"，断开时复位发送状态 */
void test_webusb_event(uint8_t busid, uint8_t event);
/* 主循环轮询：连接后先发整帧，之后只发脏 tile（非阻塞） */
void test_webusb_poll(uint8_t busid);
#endif

#endif
