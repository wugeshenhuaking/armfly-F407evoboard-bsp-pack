#ifndef _TASK_USB_H_
#define _TASK_USB_H_

#include <stdint.h>

void webusb_hid_keyboard_init(uint8_t busid, uintptr_t reg_base);
void hid_keyboard_test(uint8_t busid);


#endif
