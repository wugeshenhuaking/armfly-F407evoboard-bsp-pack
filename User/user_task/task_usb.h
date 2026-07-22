#ifndef _TASK_USB_H_
#define _TASK_USB_H_

#include <stdint.h>
#include "usbd_core.h"

/* ===================== USB Test 编译开关 =====================
 * 通过置 1 / 0 选择启用哪些 test。注意 F407 USB FS 仅有 4 个端点(EP0~EP3)：
 *   - HID    占 1 个端点 (EP1)
 *   - CDC    占 3 个端点 (EP2 notify + EP3 bulk 双向)
 *   - WebUSB 占 2 个端点 (EP2 双向)
 * 因此 CDC 与 WebUSB 共用 EP2，二者不能同时开；三者也不能同时开。
 * 默认：HID + CDC 启用，WebUSB 关闭（PC屏 -> MCU屏 投屏主用功能）。 */
#define USB_TEST_HID      0
#define USB_TEST_CDC      1
#define USB_TEST_WEBUSB   0

#if USB_TEST_CDC && USB_TEST_WEBUSB
#error "CDC and WebUSB share endpoint EP2 on F407 USB FS, cannot enable both"
#endif
#if (USB_TEST_HID + USB_TEST_CDC + USB_TEST_WEBUSB) > 2
#error "F407 USB FS has only 4 endpoints (EP0~EP3); at most two tests can coexist"
#endif

/* ===================== 接口号分配（按 HID -> CDC -> WebUSB 顺序累加） ===================== */
#define HID_INTF_NUM   0
#if USB_TEST_HID
  #define CDC_INTF_BASE  1
#else
  #define CDC_INTF_BASE  0
#endif
#if USB_TEST_CDC
  #define CDC_FIRST_INTF   CDC_INTF_BASE
  #if USB_TEST_HID
    #define WEBUSB_INTF_BASE  3
  #else
    #define WEBUSB_INTF_BASE  2
  #endif
#else
  #define WEBUSB_INTF_BASE  CDC_INTF_BASE
#endif
#if USB_TEST_WEBUSB
  #define WEBUSB_INTF_NUM   WEBUSB_INTF_BASE
#endif
#define FB_NUM_INTF  (WEBUSB_INTF_BASE + (USB_TEST_WEBUSB ? 1 : 0))

/* ===================== 设备描述符参数 ===================== */
#if USB_TEST_WEBUSB
  #define BCD_USB  USB_2_1   /* WebUSB 需要 BOS（WebUSB/WinUSB 平台能力） */
#else
  #define BCD_USB  USB_2_0
#endif
#if USB_TEST_CDC
  #define DEV_CLASS      0xEF   /* CDC 复合需要 IAD */
  #define DEV_SUBCLASS   0x02
  #define DEV_PROTOCOL   0x01
#else
  #define DEV_CLASS      0x00
  #define DEV_SUBCLASS   0x00
  #define DEV_PROTOCOL   0x00
#endif

/* ===================== 各 test 模块接口与端点宏 ===================== */
#include "test_hid.h"
#include "test_cdc.h"
#include "test_webusb.h"

/* ===================== 框架对外接口（供 main.c 调用） ===================== */
void usb_app_init(uint8_t busid, uintptr_t reg_base);
void usb_app_poll(uint8_t busid);

/* ===================== 共享 framebuffer 脏区域辅助 =====================
 * 实体实现在 task_usb.c，供 test_webusb.c（脏 tile 镜像）调用。
 * fb_mark_dirty_rect / fb_mark_dirty_all / fb_clear_dirty 见 framebuffer.h。 */
void fb_set_tile_bit(uint16_t idx);
void fb_clr_tile_bit(uint16_t idx);
int  fb_tile_is_dirty(uint16_t idx);
int  fb_find_next_dirty(uint16_t from);

#endif
