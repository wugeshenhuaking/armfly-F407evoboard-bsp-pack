#ifndef _TEST_HID_H
#define _TEST_HID_H

#include "usbd_hid.h"

/* HID 键盘 test：固定使用端点 EP1(0x81, IN)。
   端点号由本文件独占，不与 CDC/WebUSB 冲突（见 task_usb.h 开关约束）。 */
#define HID_INT_EP          0x81
#define HID_INT_EP_SIZE     8
#define HID_INT_EP_INTERVAL 10
#define HID_KEYBOARD_REPORT_DESC_SIZE 63

/* 接口初始化：注册 HID 接口 + IN 端点 */
void test_hid_init(uint8_t busid);
/* USB 事件处理：配置完成后复位发送状态 */
void test_hid_event(uint8_t busid, uint8_t event);
/* 主循环轮询：发送一次键盘报文（HID 键盘 demo） */
void test_hid_poll(uint8_t busid);

#endif
