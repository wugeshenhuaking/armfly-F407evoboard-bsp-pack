#ifndef _TASK_USB_H_
#define _TASK_USB_H_

#include <stdint.h>

void webusb_hid_keyboard_init(uint8_t busid, uintptr_t reg_base);
void hid_keyboard_test(uint8_t busid);
int webusb_send_data(uint8_t busid, const uint8_t *data, uint32_t len);
void webusb_fb_poll(uint8_t busid);
void cdc_fb_service(void);   /* CDC 接收 PC 屏数据：收齐整帧后刷到 LCD GRAM */


#endif
