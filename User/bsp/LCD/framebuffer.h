/**
 ****************************************************************************************************
 * @file        framebuffer.h
 * @brief       软件显存（外部SRAM镜像）定义
 *              LCD为FSMC MCU屏，MCU端本身没有真实显存（像素存在LCD控制器内部GRAM），
 *              本文件在外部SRAM(FSMC NE3, 基地址0x68000000)中开辟一块
 *              uint16_t[FB_WIDTH * FB_HEIGHT] 数组作为软件镜像显存，用途：
 *                1) 脏区域(tile)比对，只发送变化的区域，降低WebUSB带宽；
 *                2) 通过 webusb_fb_poll() 整帧同步 + 脏区域(tile)分块发给PC端渲染。
 *              注意：使用前必须已调用 sram_init()（bsp_Init() 中已调用，FSMC已激活）。
 ****************************************************************************************************
 */

#ifndef __FRAMEBUFFER_H
#define __FRAMEBUFFER_H

#include "stdint.h"
#include "lcd.h"   /* 用于 fb_flush_to_lcd() 调用LCD刷新函数 */

/* 外部SRAM(NE3)基地址，sram_init() 激活FSMC后即可直接按内存访问 */
#define LCD_FB_ADDR   0x68000000UL

/* 显存分辨率（与LCD实际分辨率保持一致，竖屏：宽480 高800） */
#define FB_WIDTH      480
#define FB_HEIGHT     800

/* 直接把外部SRAM当 uint16_t 显存数组用（RGB565，16bit/像素） */
#define LCD_FRAMEBUFFER   ((volatile uint16_t *)LCD_FB_ADDR)

/* 显存总字节数：800 * 480 * 2 = 768KB，外部SRAM共1MB(0x68000000~0x680FFFFF)，放得下 */
#define FB_SIZE_BYTES   ((uint32_t)FB_WIDTH * FB_HEIGHT * 2u)

/* ===================== 脏区域(tile)跟踪 =====================
 * LCD 每次画点/填充/清屏时，把对应 tile 标记为"脏"。
 * WebUSB 发送任务只发送脏 tile，未变化的区域不占用 USB 带宽。
 * 脏位表与标记函数实体定义在 task_usb.c（仅一份），本头文件只做 extern 声明。
 */
#define FB_TILE_W      32
#define FB_TILE_H      32
#define FB_TILE_COLS   ((FB_WIDTH  + FB_TILE_W - 1) / FB_TILE_W)
#define FB_TILE_ROWS   ((FB_HEIGHT + FB_TILE_H - 1) / FB_TILE_H)
#define FB_TILE_TOTAL  (FB_TILE_COLS * FB_TILE_ROWS)
#define FB_DIRTY_BYTES ((FB_TILE_TOTAL + 7) / 8)

extern uint8_t fb_dirty[FB_DIRTY_BYTES];
extern uint8_t fb_dirty_all;

void fb_mark_dirty_rect(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);
void fb_mark_dirty_all(void);
void fb_clear_dirty(void);

/* 写像素到显存（同时请用 lcd_draw_point() 镜像到LCD，或事后 fb_flush_to_lcd() */
static inline void fb_put_pixel(uint16_t x, uint16_t y, uint16_t rgb565)
{
    if (x >= FB_WIDTH || y >= FB_HEIGHT) {
        return;   /* 超出显存范围则忽略，保护相邻SRAM不被误写 */
    }
    LCD_FRAMEBUFFER[(uint32_t)y * FB_WIDTH + x] = rgb565;
    fb_mark_dirty_rect(x, y, x, y);
}

/* 读像素（脏区域比对用） */
static inline uint16_t fb_get_pixel(uint16_t x, uint16_t y)
{
    return LCD_FRAMEBUFFER[(uint32_t)y * FB_WIDTH + x];
}

/* 整屏填充指定颜色（清屏用，首次发送前建议先清一次，避免显存里是随机值） */
static inline void fb_clear(uint16_t color)
{
    uint32_t i;
    uint32_t total = (uint32_t)FB_WIDTH * FB_HEIGHT;
    for (i = 0; i < total; i++) {
        LCD_FRAMEBUFFER[i] = color;
    }
    fb_mark_dirty_all();   /* 整屏已变，标记全部 tile 脏 */
}

/* 矩形区域填充（镜像 lcd_fill 用） */
static inline void fb_fill_rect(uint16_t sx, uint16_t sy, uint16_t ex, uint16_t ey, uint16_t color)
{
    uint16_t x, y;
    if (sx > ex) { uint16_t t = sx; sx = ex; ex = t; }
    if (sy > ey) { uint16_t t = sy; sy = ey; ey = t; }
    if (sx >= FB_WIDTH || sy >= FB_HEIGHT) {
        return;
    }
    if (ex >= FB_WIDTH)  ex = (uint16_t)(FB_WIDTH - 1);
    if (ey >= FB_HEIGHT) ey = (uint16_t)(FB_HEIGHT - 1);
    for (y = sy; y <= ey; y++) {
        for (x = sx; x <= ex; x++) {
            LCD_FRAMEBUFFER[(uint32_t)y * FB_WIDTH + x] = color;
        }
    }
    fb_mark_dirty_rect(sx, sy, ex, ey);   /* 标记受影响 tile 脏 */
}

/* 将显存整屏刷到LCD（镜像显示，副屏场景用） */
static inline void fb_flush_to_lcd(void)
{
    lcd_color_fill(0, 0, FB_WIDTH - 1, FB_HEIGHT - 1, (uint16_t *)LCD_FRAMEBUFFER);
}

#endif

/***************************** (END OF FILE) *********************************/
