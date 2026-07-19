/*
 * Copyright (c) 2024, sakumisu
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "task_usb.h"
#include "usbd_core.h"
#include "usbd_hid.h"
#include "LCD/framebuffer.h"   /* 软件显存 + 脏区域tile定义 */

#define WEBUSB_VENDOR_CODE (0x22)
#define WINUSB_VENDOR_CODE (0x21)

#define URL_DESCRIPTOR_LENGTH    (3 + 41)

#define WEBUSB_INTF_NUM 0x01

#define WEBUSB_IN_EP    0x82
#define WEBUSB_OUT_EP   0x02
#ifdef CONFIG_USB_HS
#define WEBUSB_EP_MPS   512
#else
#define WEBUSB_EP_MPS   64
#endif

/* 插上 USB 后浏览器弹出的"前往"链接：指向本地 WebUSB 镜像页面。
   服务器从项目根目录启动，文件在 Doc/ 子目录下，故路径带 /Doc/。
   localhost 被浏览器视为安全上下文，故用 HTTP scheme 即可。
   字符串(不含 scheme) = "localhost:8000/Doc/webusb_lcd_mirror.html"，共 41 字符。 */
#define WEBUSB_URL_STRINGS                                 \
    'l', 'o', 'c', 'a', 'l', 'h', 'o', 's', 't', ':', '8', \
        '0', '0', '0', '/', 'D', 'o', 'c', '/', 'w', 'e', \
        'b', 'u', 's', 'b', '_', 'l', 'c', 'd', '_', 'm', \
        'i', 'r', 'r', 'o', 'r', '.', 'h', 't', 'm', 'l',

const uint8_t USBD_WebUSBURLDescriptor[URL_DESCRIPTOR_LENGTH] = {
    URL_DESCRIPTOR_LENGTH,
    WEBUSB_URL_TYPE,
    WEBUSB_URL_SCHEME_HTTP,
    WEBUSB_URL_STRINGS
};

const uint8_t WINUSB_WCIDDescriptor[] = {
    USB_MSOSV2_COMP_ID_SET_HEADER_DESCRIPTOR_INIT(10 + USB_MSOSV2_COMP_ID_FUNCTION_WINUSB_MULTI_DESCRIPTOR_LEN),
    USB_MSOSV2_COMP_ID_FUNCTION_WINUSB_MULTI_DESCRIPTOR_INIT(WEBUSB_INTF_NUM),
};

__ALIGN_BEGIN const uint8_t USBD_BinaryObjectStoreDescriptor[] = {
    USB_BOS_HEADER_DESCRIPTOR_INIT(5 + USB_BOS_CAP_PLATFORM_WEBUSB_DESCRIPTOR_LEN + USB_BOS_CAP_PLATFORM_WINUSB_DESCRIPTOR_LEN, 2),
    USB_BOS_CAP_PLATFORM_WEBUSB_DESCRIPTOR_INIT(WEBUSB_VENDOR_CODE, 0x01),
    USB_BOS_CAP_PLATFORM_WINUSB_DESCRIPTOR_INIT(WINUSB_VENDOR_CODE, sizeof(WINUSB_WCIDDescriptor)),
};

struct usb_webusb_descriptor webusb_url_desc = {
    .vendor_code = WEBUSB_VENDOR_CODE,
    .string = USBD_WebUSBURLDescriptor,
    .string_len = URL_DESCRIPTOR_LENGTH
};

struct usb_msosv2_descriptor msosv2_desc = {
    .vendor_code = WINUSB_VENDOR_CODE,
    .compat_id = WINUSB_WCIDDescriptor,
    .compat_id_len = sizeof(WINUSB_WCIDDescriptor),
};

struct usb_bos_descriptor bos_desc = {
    .string = USBD_BinaryObjectStoreDescriptor,
    .string_len = sizeof(USBD_BinaryObjectStoreDescriptor)
};

#define USBD_VID           0xffff
#define USBD_PID           0xffff
#define USBD_MAX_POWER     100
#define USBD_LANGID_STRING 1033

#define HID_INT_EP          0x81
#define HID_INT_EP_SIZE     8
#define HID_INT_EP_INTERVAL 10

#define USB_CONFIG_SIZE       (48 + 9)
#define HID_KEYBOARD_REPORT_DESC_SIZE 63

static const uint8_t device_descriptor[] = {
    USB_DEVICE_DESCRIPTOR_INIT(USB_2_1, 0x00, 0x00, 0x00, USBD_VID, USBD_PID, 0x0002, 0x01)
};

static const uint8_t config_descriptor[] = {
    USB_CONFIG_DESCRIPTOR_INIT(USB_CONFIG_SIZE, 0x02, 0x01, USB_CONFIG_BUS_POWERED, USBD_MAX_POWER),
    HID_KEYBOARD_DESCRIPTOR_INIT(0x00, 0x01, HID_KEYBOARD_REPORT_DESC_SIZE, HID_INT_EP, HID_INT_EP_SIZE, HID_INT_EP_INTERVAL),
    USB_INTERFACE_DESCRIPTOR_INIT(WEBUSB_INTF_NUM, 0x00, 0x02, 0xff, 0x00, 0x00, 0x00),
    USB_ENDPOINT_DESCRIPTOR_INIT(WEBUSB_IN_EP, 0x02, WEBUSB_EP_MPS, 0x00),
    USB_ENDPOINT_DESCRIPTOR_INIT(WEBUSB_OUT_EP, 0x02, WEBUSB_EP_MPS, 0x00)
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
    .msosv2_descriptor = &msosv2_desc,
    .webusb_url_descriptor = &webusb_url_desc,
    .bos_descriptor = &bos_desc
};

/* USB HID device Configuration Descriptor */
static uint8_t hid_desc[9] __ALIGN_END = {
    /* 18 */
    0x09,                    /* bLength: HID Descriptor size */
    HID_DESCRIPTOR_TYPE_HID, /* bDescriptorType: HID */
    0x11,                    /* bcdHID: HID Class Spec release number */
    0x01,
    0x00,                          /* bCountryCode: Hardware target country */
    0x01,                          /* bNumDescriptors: Number of HID class descriptors to follow */
    0x22,                          /* bDescriptorType */
    HID_KEYBOARD_REPORT_DESC_SIZE, /* wItemLength: Total length of Report descriptor */
    0x00,
};

static const uint8_t hid_keyboard_report_desc[HID_KEYBOARD_REPORT_DESC_SIZE] = {
    0x05, 0x01, // USAGE_PAGE (Generic Desktop)
    0x09, 0x06, // USAGE (Keyboard)
    0xa1, 0x01, // COLLECTION (Application)
    0x05, 0x07, // USAGE_PAGE (Keyboard)
    0x19, 0xe0, // USAGE_MINIMUM (Keyboard LeftControl)
    0x29, 0xe7, // USAGE_MAXIMUM (Keyboard Right GUI)
    0x15, 0x00, // LOGICAL_MINIMUM (0)
    0x25, 0x01, // LOGICAL_MAXIMUM (1)
    0x75, 0x01, // REPORT_SIZE (1)
    0x95, 0x08, // REPORT_COUNT (8)
    0x81, 0x02, // INPUT (Data,Var,Abs)
    0x95, 0x01, // REPORT_COUNT (1)
    0x75, 0x08, // REPORT_SIZE (8)
    0x81, 0x03, // INPUT (Cnst,Var,Abs)
    0x95, 0x05, // REPORT_COUNT (5)
    0x75, 0x01, // REPORT_SIZE (1)
    0x05, 0x08, // USAGE_PAGE (LEDs)
    0x19, 0x01, // USAGE_MINIMUM (Num Lock)
    0x29, 0x05, // USAGE_MAXIMUM (Kana)
    0x91, 0x02, // OUTPUT (Data,Var,Abs)
    0x95, 0x01, // REPORT_COUNT (1)
    0x75, 0x03, // REPORT_SIZE (3)
    0x91, 0x03, // OUTPUT (Cnst,Var,Abs)
    0x95, 0x06, // REPORT_COUNT (6)
    0x75, 0x08, // REPORT_SIZE (8)
    0x15, 0x00, // LOGICAL_MINIMUM (0)
    0x25, 0xFF, // LOGICAL_MAXIMUM (255)
    0x05, 0x07, // USAGE_PAGE (Keyboard)
    0x19, 0x00, // USAGE_MINIMUM (Reserved (no event indicated))
    0x29, 0x65, // USAGE_MAXIMUM (Keyboard Application)
    0x81, 0x00, // INPUT (Data,Ary,Abs)
    0xc0        // END_COLLECTION
};

#define HID_STATE_IDLE 0
#define HID_STATE_BUSY 1

/*!< hid state ! Data can be sent only when state is idle  */
static volatile uint8_t hid_state = HID_STATE_IDLE;

/* WebUSB vendor interface data buffers and transmit state */
USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX uint8_t webusb_tx_buffer[64];
USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX uint8_t webusb_rx_buffer[64];

volatile bool webusb_tx_busy = false;

/* ===================== 非阻塞帧发送状态机（脏区域分块） =====================
 * - 连接后先发一整帧（全屏同步），之后只发"脏 tile"，未变化区域不占 USB 带宽。
 * - 每个 tile / 每个数据块通过一次 usbd_ep_start_write 提交（驱动自动按 64 字节分包，
 *   整段完成才回调一次），因此调用次数从 1.2 万降到几百，main 循环永不冻结。
 */
static volatile uint8_t  fb_tx_active = 0;   /* 是否正在发送 */
static uint8_t           fb_tx_mode;          /* 1 = 整帧同步, 2 = 脏tile */
static uint8_t           fb_tx_busid;
static uint32_t          fb_full_offset;      /* 整帧模式：已发送字节偏移 */
static uint8_t           fb_tx_phase;          /* 整帧模式：0=待发帧头 1=数据 */
static uint16_t          fb_tile_idx;          /* 脏tile模式：当前tile索引 */
static uint8_t           fb_force_full = 0;   /* 连接后强制先发整帧 */

/* 发送用内部RAM缓冲（USB 不可Cache区域，避免 DMA/一致性问题），
   容纳 一个 tile 的像素(32x32x2=2048) + 协议头，足够一次提交多包。 */
#define FB_CHUNK        2048u
USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX static uint8_t webusb_fb_buf[FB_CHUNK + 16];

/* 脏区域位图（实体在此，framebuffer.h 仅 extern 声明） */
uint8_t fb_dirty[FB_DIRTY_BYTES];
uint8_t fb_dirty_all = 0;

static void fb_set_tile_bit(uint16_t idx) { fb_dirty[idx >> 3] |= (uint8_t)(1u << (idx & 7)); }
static void fb_clr_tile_bit(uint16_t idx) { fb_dirty[idx >> 3] &= (uint8_t)~(1u << (idx & 7)); }
static int  fb_tile_is_dirty(uint16_t idx) { return (fb_dirty[idx >> 3] >> (idx & 7)) & 1; }

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
static int fb_find_next_dirty(uint16_t from)
{
    if (fb_dirty_all) return 0;
    for (uint16_t i = from; i < FB_TILE_TOTAL; i++) {
        if (fb_tile_is_dirty(i)) return (int)i;
    }
    return -1;
}

static void webusb_fb_continue(void);        /* 前向声明，定义见下方 */

static void usbd_event_handler(uint8_t busid, uint8_t event)
{
    switch (event) {
        case USBD_EVENT_RESET:
            break;
        case USBD_EVENT_CONNECTED:
            break;
        case USBD_EVENT_DISCONNECTED:
            fb_tx_active = 0;   /* 中止任何正在发送的帧 */
            webusb_tx_busy = false;
            break;
        case USBD_EVENT_RESUME:
            break;
        case USBD_EVENT_SUSPEND:
            break;
        case USBD_EVENT_CONFIGURED:
            hid_state = HID_STATE_IDLE;
            webusb_tx_busy = false;
            fb_force_full = 1;   /* 连接后先发整帧，让浏览器获得完整画面 */
            /* Arm the first OUT transfer so the browser can send data in */
            usbd_ep_start_read(busid, WEBUSB_OUT_EP, webusb_rx_buffer, WEBUSB_EP_MPS);
            break;
        case USBD_EVENT_SET_REMOTE_WAKEUP:
            break;
        case USBD_EVENT_CLR_REMOTE_WAKEUP:
            break;

        default:
            break;
    }
}

void usbd_hid_int_callback(uint8_t busid, uint8_t ep, uint32_t nbytes)
{
    hid_state = HID_STATE_IDLE;
}

static struct usbd_endpoint hid_in_ep = {
    .ep_cb = usbd_hid_int_callback,
    .ep_addr = HID_INT_EP
};

static struct usbd_interface intf0;

/* WebUSB vendor interface data channel (bulk IN / OUT) */
static void webusb_in_ep_callback(uint8_t busid, uint8_t ep, uint32_t nbytes)
{
    if ((nbytes % WEBUSB_EP_MPS) == 0 && nbytes) {
        /* Send zero-length packet to terminate a transfer whose length is a multiple of MPS */
        usbd_ep_start_write(busid, WEBUSB_IN_EP, NULL, 0);
        return;
    }
    webusb_tx_busy = false;
    /* 若帧正在流式发送，由本回调驱动下一个包（非阻塞，main 循环不冻结） */
    if (fb_tx_active) {
        webusb_fb_continue();
    }
}

/* 浏览器通过 OUT 端点下发的控制命令 */
#define CMD_REQ_FULL_FRAME  0x01   /* 请求 MCU 发送一整帧显存（刷新/重连后绘制底图用） */

static void webusb_out_ep_callback(uint8_t busid, uint8_t ep, uint32_t nbytes)
{
    /* 浏览器在连接/刷新后会下发 CMD_REQ_FULL_FRAME，要求 MCU 立即发送一整帧
       显存作为底图。收到后取消可能残留的发送状态，并强制下一轮 webusb_fb_poll
       发送整帧，使浏览器（尤其刷新重连后）能立即获得完整画面，避免空白。 */
    if (nbytes > 0 && webusb_rx_buffer[0] == CMD_REQ_FULL_FRAME) {
        fb_tx_active  = 0;     /* 取消可能残留的发送（如重连前的脏 tile） */
        fb_force_full = 1;     /* 下一轮 poll 立即发整帧 */
    }
    /* 重新挂载 OUT 传输，等待下一条命令 */
    usbd_ep_start_read(busid, WEBUSB_OUT_EP, webusb_rx_buffer, WEBUSB_EP_MPS);
}

/*
 * Send arbitrary data to the browser through the WebUSB bulk IN endpoint.
 * Returns 0 on success, -1 if the device is not configured yet.
 * The transfer is asynchronous; webusb_tx_busy is cleared when it completes.
 */
int webusb_send_data(uint8_t busid, const uint8_t *data, uint32_t len)
{
    if (usb_device_is_configured(busid) == false) {
        return -1;
    }
    if (len > sizeof(webusb_tx_buffer)) {
        len = sizeof(webusb_tx_buffer);
    }
    memcpy(webusb_tx_buffer, data, len);
    webusb_tx_busy = true;
    usbd_ep_start_write(busid, WEBUSB_IN_EP, webusb_tx_buffer, len);
    return 0;
}

/* ===================== LCD framebuffer over WebUSB =====================
 * 协议（每个 USB 包 <= 64 字节，魔数 0xAA 0x55 开头）：
 *   type=1 FRAME_HEADER: payload = fmt(1=RGB565) + w(2 LE) + h(2 LE)
 *                          浏览器据此分配整帧缓冲并渲染（仅连接时整帧同步用）
 *   type=2 FRAME_DATA:   payload = 从 offset 开始的 RGB565 像素字节
 *                          浏览器累计到整帧后渲染（整帧同步）
 *   type=3 TILE:         payload = tile_x(2) + tile_y(2) + tile_w(2) + tile_h(2)
 *                               + tile_off(4) + RGB565 像素字节
 *                          浏览器按 tile 增量贴图，不清屏（脏区域更新用）
 *
 * 发送策略：连接后先发一整帧做全屏同步；之后 app_task_fb 周期性调用
 * webusb_fb_poll()，只把"脏 tile"发给浏览器，未变化区域完全不占 USB 带宽。
 * 每个 tile / 每个数据块通过一次 usbd_ep_start_write 提交（驱动自动按 64 字节
 * 分包、整段完成才回调一次），所以即使整帧也只需几百次调用，main 循环不冻结。
 */
#define FB_HDR_SIZE   7
#define FB_MAGIC_0    0xAA
#define FB_MAGIC_1    0x55
#define FB_TYPE_HEAD  0x01
#define FB_TYPE_DATA  0x02
#define FB_TYPE_TILE  0x03

static inline void fb_set_fhdr(uint8_t *p, uint8_t type, uint32_t offset)
{
    p[0] = FB_MAGIC_0;
    p[1] = FB_MAGIC_1;
    p[2] = type;
    p[3] = (uint8_t)(offset & 0xFF);
    p[4] = (uint8_t)((offset >> 8) & 0xFF);
    p[5] = (uint8_t)((offset >> 16) & 0xFF);
    p[6] = (uint8_t)((offset >> 24) & 0xFF);
}

/* 由 app_task_fb 周期性调用：连接后先发整帧，之后只发脏 tile。
 * 非阻塞：一次只提交一个 tile / 一个数据块，由 IN 端点回调驱动后续。 */
void webusb_fb_poll(uint8_t busid)
{
    if (usb_device_is_configured(busid) == false) {
        return;
    }
    if (fb_tx_active) {
        return;  /* 正在发送，等回调驱动 */
    }
    if (fb_force_full || fb_dirty_all) {
        /* 整帧同步（连接后首帧 / 整屏被清屏） */
        fb_tx_mode     = 1;
        fb_tx_busid    = busid;
        fb_full_offset = 0;
        fb_tx_phase    = 0;
        fb_tx_active   = 1;
        fb_force_full  = 0;
        fb_clear_dirty();          /* 整帧已覆盖全部内容 */
        webusb_fb_continue();
    } else {
        int idx = fb_find_next_dirty(0);
        if (idx < 0) {
            return;  /* 无变化，不占用 USB 带宽 */
        }
        fb_tx_mode   = 2;
        fb_tx_busid  = busid;
        fb_tile_idx  = (uint16_t)idx;
        fb_tx_active = 1;
        webusb_fb_continue();
    }
}

/* 发送单个 tile（脏区域模式）。构建完包后启动传输，并预取下一个脏 tile。 */
static void webusb_fb_send_tile(uint8_t busid, uint16_t idx)
{
    uint16_t col = (uint16_t)(idx % FB_TILE_COLS);
    uint16_t row = (uint16_t)(idx / FB_TILE_COLS);
    uint16_t tx  = (uint16_t)(col * FB_TILE_W);
    uint16_t ty  = (uint16_t)(row * FB_TILE_H);
    uint16_t tw  = (uint16_t)((tx + FB_TILE_W > FB_WIDTH)  ? (FB_WIDTH  - tx) : FB_TILE_W);
    uint16_t th  = (uint16_t)((ty + FB_TILE_H > FB_HEIGHT) ? (FB_HEIGHT - ty) : FB_TILE_H);

    fb_clr_tile_bit(idx);   /* 该 tile 即将发出，清除脏标记 */

    uint8_t *p = webusb_fb_buf;
    p[0] = FB_MAGIC_0; p[1] = FB_MAGIC_1; p[2] = FB_TYPE_TILE;
    p[3] = (uint8_t)(tx & 0xFF); p[4] = (uint8_t)(tx >> 8);
    p[5] = (uint8_t)(ty & 0xFF); p[6] = (uint8_t)(ty >> 8);
    p[7] = (uint8_t)(tw & 0xFF); p[8] = (uint8_t)(tw >> 8);
    p[9] = (uint8_t)(th & 0xFF); p[10] = (uint8_t)(th >> 8);
    p[11] = 0; p[12] = 0; p[13] = 0; p[14] = 0;  /* tile_off = 0（一个tile一次发完） */

    /* 逐行拷贝 tile 像素（行之间在显存中不连续） */
    const uint16_t *src = LCD_FRAMEBUFFER + (uint32_t)ty * FB_WIDTH + tx;
    uint8_t *dst = p + 15;
    for (uint16_t r = 0; r < th; r++) {
        memcpy(dst + (uint32_t)r * tw * 2, src + (uint32_t)r * FB_WIDTH, (uint32_t)tw * 2);
    }
    uint32_t tiledata = (uint32_t)tw * th * 2;

    webusb_tx_busy = true;
    usbd_ep_start_write(busid, WEBUSB_IN_EP, webusb_fb_buf, tiledata + 15);
}

/* 状态机驱动：每完成一个传输（webusb_in_ep_callback）调用一次 */
static void webusb_fb_continue(void)
{
    if (!fb_tx_active) {
        return;
    }

    if (fb_tx_mode == 1) {            /* 整帧同步 */
        if (fb_tx_phase == 0) {
            uint8_t *p = webusb_fb_buf;
            fb_set_fhdr(p, FB_TYPE_HEAD, 0);
            p[7] = 0x01;                                   /* fmt = RGB565 */
            p[8] = (uint8_t)(FB_WIDTH & 0xFF);
            p[9] = (uint8_t)(FB_WIDTH >> 8);
            p[10] = (uint8_t)(FB_HEIGHT & 0xFF);
            p[11] = (uint8_t)(FB_HEIGHT >> 8);
            webusb_tx_busy = true;
            usbd_ep_start_write(fb_tx_busid, WEBUSB_IN_EP, webusb_fb_buf, 12);
            fb_tx_phase = 1;
            return;
        }
        if (fb_full_offset >= FB_SIZE_BYTES) {
            fb_tx_active = 0;   /* 整帧发送完成 */
            return;
        }
        uint32_t remain = FB_SIZE_BYTES - fb_full_offset;
        uint16_t chunk = (remain > FB_CHUNK) ? (uint16_t)FB_CHUNK : (uint16_t)remain;
        memcpy(webusb_fb_buf + FB_HDR_SIZE,
               (const uint8_t *)LCD_FRAMEBUFFER + fb_full_offset, chunk);
        fb_set_fhdr(webusb_fb_buf, FB_TYPE_DATA, fb_full_offset);
        webusb_tx_busy = true;
        usbd_ep_start_write(fb_tx_busid, WEBUSB_IN_EP, webusb_fb_buf, (uint32_t)chunk + FB_HDR_SIZE);
        fb_full_offset += chunk;
        return;
    }

    /* fb_tx_mode == 2：脏 tile 模式 */
    webusb_fb_send_tile(fb_tx_busid, fb_tile_idx);
    int next = fb_find_next_dirty((uint16_t)(fb_tile_idx + 1));
    if (next < 0) {
        fb_tx_active = 0;   /* 没有更多脏 tile，本次为最后一片 */
    } else {
        fb_tile_idx = (uint16_t)next;
    }
}

static struct usbd_endpoint webusb_in_ep = {
    .ep_addr = WEBUSB_IN_EP,
    .ep_cb = webusb_in_ep_callback,
};

static struct usbd_endpoint webusb_out_ep = {
    .ep_addr = WEBUSB_OUT_EP,
    .ep_cb = webusb_out_ep_callback,
};

void webusb_hid_keyboard_init(uint8_t busid, uintptr_t reg_base)
{
    usbd_desc_register(busid, &webusb_hid_descriptor);

    usbd_add_interface(busid, usbd_hid_init_intf(busid, &intf0, hid_keyboard_report_desc, HID_KEYBOARD_REPORT_DESC_SIZE));
    usbd_add_endpoint(busid, &hid_in_ep);

    usbd_add_endpoint(busid, &webusb_in_ep);
    usbd_add_endpoint(busid, &webusb_out_ep);

    usbd_initialize(busid, reg_base, usbd_event_handler);
}

USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX uint8_t write_buffer[64];

void hid_keyboard_test(uint8_t busid)
{
    const uint8_t sendbuffer[8] = { 0x00, 0x00, HID_KBD_USAGE_A, 0x00, 0x00, 0x00, 0x00, 0x00 };

    if (usb_device_is_configured(busid) == false) {
        return;
    }

    memcpy(write_buffer, sendbuffer, 8);
    hid_state = HID_STATE_BUSY;
    usbd_ep_start_write(busid, HID_INT_EP, write_buffer, 8);
    while (hid_state == HID_STATE_BUSY) {
    }
}
