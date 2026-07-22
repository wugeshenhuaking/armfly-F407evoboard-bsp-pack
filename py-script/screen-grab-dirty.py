"""
PC 屏 -> MCU 屏（脏矩形/局部块更新版）
=====================================
相比 screen-grab-with-cursor.py，本脚本把 800x480 画面切成 32x32 的块(tile)，
每一帧只把"相对上一帧发生变化"的块通过 USB CDC 发给 F407，大幅降低 USB FS 带宽占用。

协议（小端字节序，与 MCU 端 test_cdc.c 对应）：
    帧头 16 字节:
        magic : u16 = 0xAA55
        seq   : u8
        type  : u8   0=整帧  1=局部块  2=刷屏标记
        x     : u16   (块左上角, 像素)
        y     : u16
        w     : u16   (块宽)
        h     : u16   (块高)
        len   : u32   (payload 字节数 = w*h*2)
    payload: RGB565 字节流(行优先)

发送节奏:
    - 仅首帧: 发整帧(type0) 做初始化
    - 有变化(鼠标移动等): 计算脏块，只发脏块(type1)，发完所有脏块后发一个刷屏标记(type2)
    - 空闲(无变化)帧: 不发整帧，改为每帧发 1 个 32x32 小块(type1)平摊 resync(与脏块同尺寸，
      不超 MCU 的 cdc_tile_stage 缓冲<8192B>)，避免移动途中整帧大传输卡顿；
      空闲累计 FULL_EVERY_IDLE 帧再发一次整帧(type0，直接写显存)做保险
    - 注意：空闲 resync 严禁发"整宽带块"(如 800xN)，否则单块超 8192B 冲爆 MCU 缓冲 -> HardFault
"""

import mss
import numpy as np
import serial
import time
import struct
import ctypes

# ===== 配置区 =====
COM_PORT = "COM5"          # 改成你 STM32 CDC 枚举出来的串口号
BAUD = 2000000              # CDC 虚拟串口，波特率对 USB CDC 无实际意义
VIRTUAL_MONITOR_INDEX = 2   # 改成你上一步查到的虚拟屏索引
FRAME_MAGIC = 0xAA55
TILE = 32                   # 脏矩形块大小(像素)，必须整除分辨率
# ===== 整帧 resync 策略（关键：避免移动鼠标时周期性卡顿）=====
# 旧逻辑：每 60 帧无脑发一整帧(768KB)，USB FS 要 ~0.8s 才能发完，
#         这段时间抓取循环被阻塞 -> 表现为"移动一会儿卡一下"。
# 新逻辑：
#   1) 仅首帧发整帧做初始化；
#   2) 有变化(鼠标移动)时不发整帧，只发脏块；
#   3) 空闲(无变化)帧才做后台 resync：每帧只发 1 个 32x32 小块(type1，与脏块同尺寸，
#      MCU 的 cdc_tile_stage 缓冲=8192B=最大64x64，绝不会溢出)，平摊整屏；
#      空闲久了再偶尔发一次整帧(type0，直接写显存，不超缓冲)做保险(静止时冻结不可见)。
# 注意：空闲 resync 严禁发"整宽带块"，否则单块超 8192B 会冲爆 MCU 缓冲 -> HardFault。
FULL_EVERY_IDLE = 600      # 空闲帧累计到该值，发一次整帧(type0)做终极保险

# ===== 光标绘制（解决"扩展屏不显示鼠标"问题）=====
# Windows 鼠标是硬件光标，不在帧缓存里，必须自己合成。
ARROW = [(0,0),(0,15),(4,11),(6,17),(9,16),(6,10),(11,10)]

def _min_max():
    xs = [p[0] for p in ARROW]; ys = [p[1] for p in ARROW]
    return min(xs), max(xs), min(ys), max(ys)
_AX0, _AX1, _AY0, _AY1 = _min_max()
_BW = _AX1 - _AX0 + 1
_BH = _AY1 - _AY0 + 1

def _point_in_poly(px, py, poly):
    inside = False
    n = len(poly); j = n - 1
    for i in range(n):
        xi, yi = poly[i]; xj, yj = poly[j]
        if ((yi > py) != (yj > py)) and (px < (xj - xi) * (py - yi) / (yj - yi) + xi):
            inside = not inside
        j = i
    return inside

ARROW_MASK = np.array(
    [[_point_in_poly(xx + _AX0, yy + _AY0, ARROW) for xx in range(_BW)] for yy in range(_BH)],
    dtype=bool)
EDGE_MASK = np.zeros((_BH, _BW), dtype=bool)
for yy in range(_BH):
    for xx in range(_BW):
        if ARROW_MASK[yy, xx]:
            edge = (yy == 0 or not ARROW_MASK[yy-1, xx]) or \
                   (yy == _BH-1 or not ARROW_MASK[yy+1, xx]) or \
                   (xx == 0 or not ARROW_MASK[yy, xx-1]) or \
                   (xx == _BW-1 or not ARROW_MASK[yy, xx+1])
            if edge:
                EDGE_MASK[yy, xx] = True

class _POINT(ctypes.Structure):
    _fields_ = [("x", ctypes.c_long), ("y", ctypes.c_long)]

def get_cursor_pos():
    pt = _POINT()
    ctypes.windll.user32.GetCursorPos(ctypes.byref(pt))
    return pt.x, pt.y

def draw_cursor(bgra, gx, gy, mon_left, mon_top):
    """在 bgra 帧上，按全局光标坐标 (gx,gy) 合成箭头光标。"""
    h, w = bgra.shape[:2]
    cx = gx - mon_left
    cy = gy - mon_top
    if not (0 <= cx < w and 0 <= cy < h):
        return
    y0, x0 = cy + _AY0, cx + _AX0
    y1, x1 = cy + _AY1 + 1, cx + _AX1 + 1
    if x0 < 0 or y0 < 0 or x1 > w or y1 > h:
        return
    region = bgra[y0:y1, x0:x1]
    # BGRA 四通道：只对前 3 个通道(BGR)赋值，alpha 通道保持不动
    region[ARROW_MASK, :3] = (255, 255, 255)   # 白填充
    region[EDGE_MASK, :3] = (0, 0, 0)          # 黑描边

# ===== 颜色/协议工具 =====
def bgra_to_rgb565(bgra: np.ndarray) -> np.ndarray:
    """mss 抓出来的是 BGRA，转成 RGB565"""
    b = bgra[:, :, 0].astype(np.uint16)
    g = bgra[:, :, 1].astype(np.uint16)
    r = bgra[:, :, 2].astype(np.uint16)
    rgb565 = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
    return rgb565

def build_header(seq, ptype, x, y, w, h, length):
    """16 字节帧头，小端"""
    return struct.pack('<HBBHHHHI', FRAME_MAGIC, seq & 0xFF, ptype, x, y, w, h, length)

def main():
    ser = serial.Serial(COM_PORT, BAUD, timeout=1)
    print(f"已连接 {COM_PORT}  脏矩形模式 tile={TILE} 空闲resync按32x32小块平摊 整帧保险间隔={FULL_EVERY_IDLE}帧")

    with mss.MSS() as sct:
        monitor = sct.monitors[VIRTUAL_MONITOR_INDEX]
        W, H = monitor['width'], monitor['height']
        print(f"抓取区域: {monitor}")

        assert W % TILE == 0 and H % TILE == 0, f"分辨率 {W}x{H} 必须能被 TILE={TILE} 整除"
        cols = W // TILE
        rows = H // TILE

        prev = None          # 上一帧(已合成光标)的 RGB565
        seq = 0
        frame_count = 0
        update_count = 0     # 实际发过数据的帧数
        dirty_total = 0
        idle_streak = 0      # 连续空闲(无脏块)帧计数
        resync_r = 0         # 后台 resync 当前 tile 行索引
        resync_c = 0         # 后台 resync 当前 tile 列索引
        t0 = time.time()

        while True:
            img = np.array(sct.grab(monitor))           # BGRA (H, W, 4)
            gx, gy = get_cursor_pos()
            draw_cursor(img, gx, gy, monitor['left'], monitor['top'])
            cur = bgra_to_rgb565(img)                    # (H, W) uint16

            if prev is None:
                # —— 首帧：发整帧初始化（仅一次）——
                payload = cur.astype('<u2').tobytes()
                ser.write(build_header(seq, 0, 0, 0, W, H, len(payload)))
                ser.write(payload)
                prev = cur.copy()
                update_count += 1
                sent_tiles = 0
            else:
                # —— 脏矩形：逐块比较 ——
                cur_t = cur.reshape(rows, TILE, cols, TILE)
                prev_t = prev.reshape(rows, TILE, cols, TILE)
                dirty = (cur_t != prev_t).any(axis=(1, 3))     # (rows, cols) bool
                r_idx, c_idx = np.nonzero(dirty)
                n = r_idx.size
                if n > 0:
                    # 有变化：只发脏块 + 刷屏标记（鼠标移动走这里，绝不大块传输）
                    for r, c in zip(r_idx.tolist(), c_idx.tolist()):
                        tile = cur[r*TILE:(r+1)*TILE, c*TILE:(c+1)*TILE]
                        payload = tile.astype('<u2').tobytes()
                        ser.write(build_header(seq, 1, c*TILE, r*TILE, TILE, TILE, len(payload)))
                        ser.write(payload)
                    ser.write(build_header(seq, 2, 0, 0, 0, 0, 0))   # 刷屏标记
                    prev = cur.copy()
                    update_count += 1
                    dirty_total += n
                    sent_tiles = n
                    idle_streak = 0
                else:
                    # 空闲帧：平摊 resync，每帧只发 1 个 TILE×TILE 小块(type1)，
                    # 与脏块同尺寸(2048B)，远小于 MCU 的 cdc_tile_stage(8192B)，绝不溢出。
                    idle_streak += 1
                    r, c = resync_r, resync_c
                    tile = cur[r*TILE:(r+1)*TILE, c*TILE:(c+1)*TILE]
                    payload = tile.astype('<u2').tobytes()          # = TILE*TILE*2 = 2048 字节
                    ser.write(build_header(seq, 1, c*TILE, r*TILE, TILE, TILE, len(payload)))
                    ser.write(payload)
                    ser.write(build_header(seq, 2, 0, 0, 0, 0, 0))   # 刷屏标记
                    prev = cur.copy()
                    update_count += 1
                    sent_tiles = -2   # -2 表示空闲 resync 小块
                    # 推进到下一个 tile，循环遍历整屏
                    resync_c += 1
                    if resync_c >= cols:
                        resync_c = 0
                        resync_r += 1
                        if resync_r >= rows:
                            resync_r = 0
                    # 空闲很久后，发一次整帧(type0，直接写显存不超缓冲)做终极保险
                    # （屏幕静止，即使短暂冻结也不可见）
                    if idle_streak % FULL_EVERY_IDLE == 0:
                        payload = cur.astype('<u2').tobytes()
                        ser.write(build_header(seq, 0, 0, 0, W, H, len(payload)))
                        ser.write(payload)

            frame_count += 1
            seq = (seq + 1) & 0xFF

            if frame_count % 30 == 0:
                elapsed = time.time() - t0
                fps = update_count / elapsed if elapsed > 0 else 0
                print(f"帧{frame_count}  抓取循环 {frame_count/elapsed:.1f}/s  有效刷新 {fps:.1f}/s  累计脏块 {dirty_total}")

if __name__ == "__main__":
    main()
