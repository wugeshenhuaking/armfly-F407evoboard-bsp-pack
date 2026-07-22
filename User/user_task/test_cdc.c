/*
 * CDC ACM test —— PC屏 -> MCU屏 虚拟串口接收，整帧刷 LCD。
 * 独立模块，由 task_usb.c 框架调度。端点：EP2(notify) + EP3(bulk 双向)。
 */
#include "task_usb.h"
#include "usbd_core.h"
#include "LCD/framebuffer.h"   /* LCD_FRAMEBUFFER / fb_flush_to_lcd / fb_mark_dirty_all */

/* CDC 接收缓冲（USB 中断回调中写入，用于接收 PC 屏 RGB565 数据）。 */
#define CDC_RX_BUF_SIZE   2048u
USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX static uint8_t cdc_rx_buf[CDC_RX_BUF_SIZE];

static struct usbd_interface cdc_intf0;
static struct usbd_interface cdc_intf1;

static void usbd_cdc_bulk_out(uint8_t busid, uint8_t ep, uint32_t nbytes);
static void usbd_cdc_bulk_in(uint8_t busid, uint8_t ep, uint32_t nbytes);

static struct usbd_endpoint cdc_out_ep = {
    .ep_addr = CDC_OUT_EP,
    .ep_cb = usbd_cdc_bulk_out
};
static struct usbd_endpoint cdc_in_ep = {
    .ep_addr = CDC_IN_EP,
    .ep_cb = usbd_cdc_bulk_in
};

void test_cdc_init(uint8_t busid)
{
    /* CDC ACM：注册两个接口 + BULK IN/OUT + INT 通知端点 */
    usbd_add_interface(busid, usbd_cdc_acm_init_intf(busid, &cdc_intf0));
    usbd_add_interface(busid, usbd_cdc_acm_init_intf(busid, &cdc_intf1));
    usbd_add_endpoint(busid, &cdc_out_ep);
    usbd_add_endpoint(busid, &cdc_in_ep);
}

void test_cdc_event(uint8_t busid, uint8_t event)
{
    /* 配置完成后挂载 CDC OUT 读，开始接收 PC 屏数据 */
    if (event == USBD_EVENT_CONFIGURED) {
        usbd_ep_start_read(busid, CDC_OUT_EP, cdc_rx_buf, CDC_RX_BUF_SIZE);
    }
}

/* ===================== CDC 接收 PC 屏数据并绘制到 MCU 屏（脏矩形/局部块更新） =====================
 * 协议（PC 端 Python 发送，小端字节序）：
 *   帧头(16B): magic(0xAA55,u16) + seq(u8) + type(u8) + x(u16) + y(u16) + w(u16) + h(u16) + len(u32)
 *   type 0 = 整帧刷新 : payload = w*h*2 字节 RGB565，MCU 直接平铺覆盖整块显存后整屏刷 LCD
 *   type 1 = 局部块   : payload = w*h*2 字节 RGB565（块内行优先），MCU 把块贴到显存(x,y)处（不刷屏）
 *   type 2 = 刷屏标记 : len=0，MCU 把当前显存整屏刷到 LCD（一次更新发完所有 tile 后发一个）
 * MCU 把 payload 写入外部SRAM显存 LCD_FRAMEBUFFER，收到 type2（或 type0 收齐）后由主循环
 * 调 test_cdc_poll() -> fb_flush_to_lcd() 刷到 LCD GRAM。
 * 这样 PC 只发变化区域，USB FS 带宽利用率大幅提升（静止画面 0 字节）。 */
#define CDC_FRAME_MAGIC     0xAA55
#define CDC_HDR_SIZE        16u
/* 局部块临时缓冲（按 64x64 预留足够空间，实际发送用 32x32） */
#define CDC_TILE_STAGE_SIZE 8192u

enum { CDC_RX_HDR = 0, CDC_RX_PAYLOAD } cdc_rx_state = CDC_RX_HDR;
static uint8_t  cdc_hdr[CDC_HDR_SIZE];
static uint8_t  cdc_hdr_i;
static uint8_t  cdc_pkt_type;     /* 0=整帧 1=块 2=刷屏标记 */
static uint16_t cdc_x, cdc_y, cdc_w, cdc_h;
static uint32_t cdc_payload_rem;
static uint32_t cdc_tile_off;     /* 当前包内已收字节数（整帧=显存偏移，块=临时缓冲偏移） */
static uint8_t  cdc_tile_stage[CDC_TILE_STAGE_SIZE];
volatile uint8_t cdc_frame_ready;

/* 把临时缓冲中的 w*h RGB565(行优先) 贴到显存 (x,y) 处（按行 stride 拷贝，处理换行） */
static void cdc_blit_tile_to_fb(void)
{
    const uint16_t *src = (const uint16_t *)cdc_tile_stage;
    for (uint16_t row = 0; row < cdc_h; row++) {
        uint16_t *dst = (uint16_t *)LCD_FRAMEBUFFER + (uint32_t)(cdc_y + row) * FB_WIDTH + cdc_x;
        memcpy(dst, src + (uint32_t)row * cdc_w, (uint32_t)cdc_w * 2u);
    }
}

/* 把收到的字节流喂入帧解析状态机（在 USB 中断回调中调用） */
static void cdc_rx_feed(const uint8_t *data, uint32_t len)
{
    uint32_t i = 0;
    while (i < len) {
        if (cdc_rx_state == CDC_RX_HDR) {
            /* 逐字节收集帧头，兼容跨包截断 */
            cdc_hdr[cdc_hdr_i++] = data[i++];
            if (cdc_hdr_i >= CDC_HDR_SIZE) {
                uint16_t magic = (uint16_t)(cdc_hdr[0] | ((uint16_t)cdc_hdr[1] << 8));
                if (magic != CDC_FRAME_MAGIC) {
                    /* 同步丢失：滑动丢弃首字节后重试 */
                    memmove(cdc_hdr, cdc_hdr + 1, CDC_HDR_SIZE - 1);
                    cdc_hdr_i = CDC_HDR_SIZE - 1;
                    continue;
                }
                cdc_pkt_type = cdc_hdr[3];
                cdc_x = (uint16_t)(cdc_hdr[4] | ((uint16_t)cdc_hdr[5] << 8));
                cdc_y = (uint16_t)(cdc_hdr[6] | ((uint16_t)cdc_hdr[7] << 8));
                cdc_w = (uint16_t)(cdc_hdr[8] | ((uint16_t)cdc_hdr[9] << 8));
                cdc_h = (uint16_t)(cdc_hdr[10] | ((uint16_t)cdc_hdr[11] << 8));
                cdc_payload_rem = (uint32_t)cdc_hdr[12] | ((uint32_t)cdc_hdr[13] << 8)
                                | ((uint32_t)cdc_hdr[14] << 16) | ((uint32_t)cdc_hdr[15] << 24);
                cdc_tile_off = 0;
                cdc_rx_state = CDC_RX_PAYLOAD;
                cdc_hdr_i = 0;
                if (cdc_payload_rem == 0) {
                    /* type2 刷屏标记或无负载包：立即处理，不进入 PAYLOAD */
                    if (cdc_pkt_type == 2) {
                        cdc_frame_ready = 1;   /* 主循环据此刷屏 */
                    }
                    cdc_rx_state = CDC_RX_HDR;
                    continue;
                }
            }
        } else { /* CDC_RX_PAYLOAD */
            uint32_t chunk = len - i;
            if (chunk > cdc_payload_rem) chunk = cdc_payload_rem;
            if (cdc_pkt_type == 0) {
                /* 整帧：直接平铺进显存（偏移=已收字节数） */
                memcpy((uint8_t *)LCD_FRAMEBUFFER + cdc_tile_off, data + i, chunk);
            } else {
                /* 局部块：先攒到临时缓冲 */
                memcpy(cdc_tile_stage + cdc_tile_off, data + i, chunk);
            }
            cdc_tile_off   += chunk;
            cdc_payload_rem -= chunk;
            i += chunk;
            if (cdc_payload_rem == 0) {
                if (cdc_pkt_type == 1) {
                    cdc_blit_tile_to_fb();   /* 把块贴到显存指定位置（暂不刷屏） */
                } else { /* type 0 整帧收齐 */
                    cdc_frame_ready = 1;     /* 主循环据此刷屏 */
                }
                cdc_rx_state = CDC_RX_HDR;
                cdc_hdr_i = 0;
            }
        }
    }
}

static void usbd_cdc_bulk_out(uint8_t busid, uint8_t ep, uint32_t nbytes)
{
    (void)ep;
    if (nbytes > 0) {
        cdc_rx_feed(cdc_rx_buf, nbytes);
    }
    /* 重新挂载 OUT 传输，等待下一包 */
    usbd_ep_start_read(busid, CDC_OUT_EP, cdc_rx_buf, CDC_RX_BUF_SIZE);
}

static void usbd_cdc_bulk_in(uint8_t busid, uint8_t ep, uint32_t nbytes)
{
    (void)busid; (void)ep;
    /* CDC 回传（如需从 MCU 发数据给 PC）。当前仅处理 ZLP。 */
    if ((nbytes % CDC_EP_MPS) == 0 && nbytes) {
        usbd_ep_start_write(busid, CDC_IN_EP, NULL, 0);
    }
}

/* 主循环调用：检测 CDC 整帧收齐后刷到 LCD GRAM */
void test_cdc_poll(uint8_t busid)
{
    (void)busid;
    if (cdc_frame_ready) {
        cdc_frame_ready = 0;
        fb_flush_to_lcd();      /* LCD_FRAMEBUFFER -> LCD GRAM（整屏覆盖） */
        fb_mark_dirty_all();    /* 若将来开启 WebUSB 镜像可同步；当前发送已禁用，无害 */
    }
}
