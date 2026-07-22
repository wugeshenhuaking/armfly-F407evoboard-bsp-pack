import mss
import numpy as np
import serial
import time
import struct
import ctypes

# ===== 配置区 =====
COM_PORT = "COM5"          # 改成你 STM32 CDC 枚举出来的串口号
BAUD = 2000000              # CDC 虚拟串口，波特率对 USB CDC 无实际意义，随便填
VIRTUAL_MONITOR_INDEX = 2   # 改成你上一步查到的虚拟屏索引
FRAME_MAGIC = 0xAA55

# ===== 光标绘制（解决"扩展屏不显示鼠标"问题）=====
# Windows 鼠标是硬件光标，不在帧缓存里，必须自己合成。
# 经典箭头光标多边形（局部坐标，尖端在 0,0），白色填充 + 黑色描边。
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

# 预计算箭头填充掩码与描边掩码（箭头形状固定，只算一次）
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
    """在 bgra 帧上，按全局光标坐标 (gx,gy) 合成箭头光标。坐标已映射到虚拟屏局部空间。"""
    h, w = bgra.shape[:2]
    cx = gx - mon_left
    cy = gy - mon_top
    # 光标不在虚拟屏范围内则不画
    if not (0 <= cx < w and 0 <= cy < h):
        return
    y0, x0 = cy + _AY0, cx + _AX0
    y1, x1 = cy + _AY1 + 1, cx + _AX1 + 1
    if x0 < 0 or y0 < 0 or x1 > w or y1 > h:
        return  # 箭头越界，简单跳过
    region = bgra[y0:y1, x0:x1]
    # BGRA 四通道：只对前 3 个通道(BGR)赋值，alpha 通道保持不动
    region[ARROW_MASK, :3] = (255, 255, 255)   # 白填充
    region[EDGE_MASK, :3] = (0, 0, 0)          # 黑描边

def bgra_to_rgb565(bgra: np.ndarray) -> np.ndarray:
    """mss 抓出来的是 BGRA，转成 RGB565"""
    b = bgra[:, :, 0].astype(np.uint16)
    g = bgra[:, :, 1].astype(np.uint16)
    r = bgra[:, :, 2].astype(np.uint16)
    rgb565 = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
    return rgb565

def build_packet(width, height, pixel_data: np.ndarray) -> bytes:
    payload = pixel_data.astype('<u2').tobytes()  # 小端序
    header = struct.pack('<HHHI', FRAME_MAGIC, width, height, len(payload))
    return header + payload

def main():
    ser = serial.Serial(COM_PORT, BAUD, timeout=1)
    print(f"已连接 {COM_PORT}")

    with mss.MSS() as sct:
        monitor = sct.monitors[VIRTUAL_MONITOR_INDEX]
        print(f"抓取区域: {monitor}")

        frame_count = 0
        t0 = time.time()

        while True:
            img = np.array(sct.grab(monitor))  # BGRA, shape (H, W, 4)

            # —— 新增：合成鼠标光标 ——
            gx, gy = get_cursor_pos()
            draw_cursor(img, gx, gy, monitor['left'], monitor['top'])

            rgb565 = bgra_to_rgb565(img)
            packet = build_packet(monitor['width'], monitor['height'], rgb565)

            ser.write(packet)

            frame_count += 1
            if frame_count % 10 == 0:
                elapsed = time.time() - t0
                print(f"已发送 {frame_count} 帧, 平均 {frame_count/elapsed:.2f} fps")

if __name__ == "__main__":
    main()
