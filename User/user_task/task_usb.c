/*
 * Copyright (c) 2024, sakumisu
 *
 * SPDX-License-Identifier: Apache-2.0
 */
/*
 * task_usb.c —— USB 应用框架层
 * - 构造设备/配置/字符串描述符（按编译开关拼接启用的 test 接口）
 * - 提供统一入口 usb_app_init() / usb_app_poll()，在内部按开关分发到
 *   test_hid / test_cdc / test_webusb 三个独立模块
 * - 维护共享的 framebuffer 脏区域机制（fb_dirty 等），供 WebUSB 镜像使用
 *
 * 编译开关见 task_usb.h：USB_TEST_HID / USB_TEST_CDC / USB_TEST_WEBUSB
 */
#include "task_usb.h"
#include "usbd_core.h"
#include "usbd_hid.h"
#include "usbd_cdc_acm.h"
#include "LCD/framebuffer.h"   /* 软件显存 + 脏区域tile定义 */

#define USBD_VID           0xffff
#define USBD_PID           0xffff
#define USBD_MAX_POWER     100
#define USBD_LANGID_STRING 1033

/* 配置描述符总长（按启用的 test 累加）。
   HID=25, CDC=CDC_ACM_DESCRIPTOR_LEN, WebUSB=23(接口9+2*端点7)。 */
#define USB_CONFIG_SIZE (9 \
    + (USB_TEST_HID ? 25 : 0) \
    + (USB_TEST_CDC ? CDC_ACM_DESCRIPTOR_LEN : 0) \
    + (USB_TEST_WEBUSB ? 23 : 0))

static const uint8_t device_descriptor[] = {
    /* 设备描述符：bcdUSB 与 bDeviceClass 按组合确定（CDC 需要 IAD / WebUSB 需要 BOS） */
    USB_DEVICE_DESCRIPTOR_INIT(BCD_USB, DEV_CLASS, DEV_SUBCLASS, DEV_PROTOCOL, USBD_VID, USBD_PID, 0x0002, 0x01)
};

static const uint8_t config_descriptor[] = {
    USB_CONFIG_DESCRIPTOR_INIT(USB_CONFIG_SIZE, FB_NUM_INTF, 0x01, USB_CONFIG_BUS_POWERED, USBD_MAX_POWER),
#if USB_TEST_HID
    HID_KEYBOARD_DESCRIPTOR_INIT(HID_INTF_NUM, 0x01, HID_KEYBOARD_REPORT_DESC_SIZE, HID_INT_EP, HID_INT_EP_SIZE, HID_INT_EP_INTERVAL),
#endif
#if USB_TEST_CDC
    CDC_ACM_DESCRIPTOR_INIT(CDC_FIRST_INTF, CDC_INT_EP, CDC_OUT_EP, CDC_IN_EP, CDC_EP_MPS, 0x00),
#endif
#if USB_TEST_WEBUSB
    USB_INTERFACE_DESCRIPTOR_INIT(WEBUSB_INTF_NUM, 0x00, 0x02, 0xff, 0x00, 0x00, 0x00),
    USB_ENDPOINT_DESCRIPTOR_INIT(WEBUSB_IN_EP, 0x02, WEBUSB_EP_MPS, 0x00),
    USB_ENDPOINT_DESCRIPTOR_INIT(WEBUSB_OUT_EP, 0x02, WEBUSB_EP_MPS, 0x00),
#endif
};

static const uint8_t device_quality_descriptor[] = {
    ///////////////////////////////////////
    /// device qualifier descriptor
    ///////////////////////////////////////
    0x0a,
    USB_DESCRIPTOR_TYPE_DEVICE_QUALIFIER,
    0x00,
    0x02,
    0x00,
    0x00,
    0x00,
    0x40,
    0x00,
    0x00,
};

static const char *string_descriptors[] = {
    (const char[]){ 0x09, 0x04 }, /* Langid */
    "CherryUSB",                  /* Manufacturer */
    "CherryUSB WEBUSB HID DEMO",  /* Product */
    "2022123456",                 /* Serial Number */
};

static const uint8_t *device_descriptor_callback(uint8_t speed)
{
    return device_descriptor;
}

static const uint8_t *config_descriptor_callback(uint8_t speed)
{
    return config_descriptor;
}

static const uint8_t *device_quality_descriptor_callback(uint8_t speed)
{
    return device_quality_descriptor;
}

static const char *string_descriptor_callback(uint8_t speed, uint8_t index)
{
    if (index >= (sizeof(string_descriptors) / sizeof(char *))) {
        return NULL;
    }
    return string_descriptors[index];
}

const struct usb_descriptor webusb_hid_descriptor = {
    .device_descriptor_callback = device_descriptor_callback,
    .config_descriptor_callback = config_descriptor_callback,
    .device_quality_descriptor_callback = device_quality_descriptor_callback,
    .string_descriptor_callback = string_descriptor_callback,
#if USB_TEST_WEBUSB
    /* 仅当 WebUSB 镜像开启时注册 BOS/WinUSB/MSOSv2，避免 WinUSB 误绑到 CDC 接口 */
    .msosv2_descriptor = &msosv2_desc,
    .webusb_url_descriptor = &webusb_url_desc,
    .bos_descriptor = &bos_desc
#endif
};

/* ===================== 共享 framebuffer 脏区域机制 =====================
 * 实体在此（仅一份），framebuffer.h 声明了 fb_mark_dirty_rect / fb_mark_dirty_all /
 * fb_clear_dirty；其余辅助函数在 task_usb.h 声明，供 test_webusb.c 调用。 */
uint8_t fb_dirty[FB_DIRTY_BYTES];
uint8_t fb_dirty_all = 0;

void fb_set_tile_bit(uint16_t idx) { fb_dirty[idx >> 3] |= (uint8_t)(1u << (idx & 7)); }
void fb_clr_tile_bit(uint16_t idx) { fb_dirty[idx >> 3] &= (uint8_t)~(1u << (idx & 7)); }
int  fb_tile_is_dirty(uint16_t idx) { return (fb_dirty[idx >> 3] >> (idx & 7)) & 1; }

void fb_mark_dirty_rect(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    if (fb_dirty_all) return;   /* 整屏已待发，无需逐tile标记 */
    if (x0 > x1) { uint16_t t = x0; x0 = x1; x1 = t; }
    if (y0 > y1) { uint16_t t = y0; y0 = y1; y1 = t; }
    if (x0 >= FB_WIDTH || y0 >= FB_HEIGHT) return;
    if (x1 >= FB_WIDTH)  x1 = (uint16_t)(FB_WIDTH - 1);
    if (y1 >= FB_HEIGHT) y1 = (uint16_t)(FB_HEIGHT - 1);
    uint16_t c0 = x0 / FB_TILE_W, c1 = x1 / FB_TILE_W;
    uint16_t r0 = y0 / FB_TILE_H, r1 = y1 / FB_TILE_H;
    for (uint16_t r = r0; r <= r1; r++) {
        for (uint16_t c = c0; c <= c1; c++) {
            fb_set_tile_bit((uint16_t)(r * FB_TILE_COLS + c));
        }
    }
}
void fb_mark_dirty_all(void) { fb_dirty_all = 1; }
void fb_clear_dirty(void)
{
    fb_dirty_all = 0;
    for (uint16_t i = 0; i < FB_DIRTY_BYTES; i++) fb_dirty[i] = 0;
}
int fb_find_next_dirty(uint16_t from)
{
    if (fb_dirty_all) return 0;
    for (uint16_t i = from; i < FB_TILE_TOTAL; i++) {
        if (fb_tile_is_dirty(i)) return (int)i;
    }
    return -1;
}

/* ===================== USB 事件分发 ===================== */
static void usbd_event_handler(uint8_t busid, uint8_t event)
{
#if USB_TEST_HID
    test_hid_event(busid, event);
#endif
#if USB_TEST_CDC
    test_cdc_event(busid, event);
#endif
#if USB_TEST_WEBUSB
    test_webusb_event(busid, event);
#endif
}

/* ===================== 框架对外接口 ===================== */
void usb_app_init(uint8_t busid, uintptr_t reg_base)
{
    usbd_desc_register(busid, &webusb_hid_descriptor);

#if USB_TEST_HID
    test_hid_init(busid);
#endif
#if USB_TEST_CDC
    test_cdc_init(busid);
#endif
#if USB_TEST_WEBUSB
    test_webusb_init(busid);
#endif

    usbd_initialize(busid, reg_base, usbd_event_handler);
}

void usb_app_poll(uint8_t busid)
{
#if USB_TEST_HID
    test_hid_poll(busid);
#endif
#if USB_TEST_CDC
    test_cdc_poll(busid);
#endif
#if USB_TEST_WEBUSB
    test_webusb_poll(busid);
#endif
}
