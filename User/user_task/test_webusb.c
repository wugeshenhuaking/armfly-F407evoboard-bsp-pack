/*
 * WebUSB 镜像 test —— MCU屏 -> 浏览器 镜像发送（脏 tile 增量）。
 * 仅在 USB_TEST_WEBUSB=1 时参与编译（与 CDC 共用 EP2，二者不可同时开）。
 * 独立模块，由 task_usb.c 框架调度。
 */
#include "task_usb.h"
#include "usbd_core.h"
#include "LCD/framebuffer.h"   /* 软件显存 + 脏区域tile定义 */

#if USB_TEST_WEBUSB

#define WEBUSB_VENDOR_CODE (0x22)
#define WINUSB_VENDOR_CODE (0x21)

#define URL_DESCRIPTOR_LENGTH    (3 + 41)

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

/* ===================== LCD framebuffer over WebUSB =====================
 * 协议（每个 USB 包 <= 64 字节，魔数 0xAA 0x55 开头）：
 *   type=1 FRAME_HEADER: payload = fmt(1=RGB565) + w(2 LE) + h(2 LE)
 *   type=2 FRAME_DATA:   payload = 从 offset 开始的 RGB565 像素字节
 *   type=3 TILE:         payload = tile_x(2) + tile_y(2) + tile_w(2) + tile_h(2)
 *                               + tile_off(4) + RGB565 像素字节
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

/* 由 app_task_fb 周期性调用：连接后先发整帧，之后只发脏 tile。
 * 非阻塞：一次只提交一个 tile / 一个数据块，由 IN 端点回调驱动后续。 */
void test_webusb_poll(uint8_t busid)
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

    /* 逐行拷贝 tile 像素（行之间在显存中不连续）。
       LCD_FRAMEBUFFER 为 volatile uint16_t*，读显存用 const 转换避免限定符告警 */
    const uint16_t *src = (const uint16_t *)LCD_FRAMEBUFFER + (uint32_t)ty * FB_WIDTH + tx;
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

void test_webusb_init(uint8_t busid)
{
    /* 镜像发送开时才注册 WebUSB 端点，省 FIFO/带宽（CDC 关闭时本 test 才启用） */
    usbd_add_endpoint(busid, &webusb_in_ep);
    usbd_add_endpoint(busid, &webusb_out_ep);
}

void test_webusb_event(uint8_t busid, uint8_t event)
{
    switch (event) {
        case USBD_EVENT_DISCONNECTED:
            fb_tx_active = 0;   /* 中止任何正在发送的帧 */
            webusb_tx_busy = false;
            break;
        case USBD_EVENT_CONFIGURED:
            fb_force_full = 1;   /* 连接后先发整帧，让浏览器获得完整画面 */
            usbd_ep_start_read(busid, WEBUSB_OUT_EP, webusb_rx_buffer, WEBUSB_EP_MPS);
            break;
        default:
            break;
    }
}

#endif /* USB_TEST_WEBUSB */
