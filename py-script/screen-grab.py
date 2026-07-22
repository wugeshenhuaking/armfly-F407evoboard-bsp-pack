import mss
import numpy as np
import serial
import time
import struct

# ===== 配置区 =====
COM_PORT = "COM4"          # 改成你 STM32 CDC 枚举出来的串口号
BAUD = 2000000              # CDC 虚拟串口，波特率对 USB CDC 无实际意义，随便填
VIRTUAL_MONITOR_INDEX = 2   # 改成你上一步查到的虚拟屏索引
FRAME_MAGIC = 0xAA55

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

    with mss.mss() as sct:
        monitor = sct.monitors[VIRTUAL_MONITOR_INDEX]
        print(f"抓取区域: {monitor}")

        frame_count = 0
        t0 = time.time()

        while True:
            img = np.array(sct.grab(monitor))  # BGRA, shape (H, W, 4)
            rgb565 = bgra_to_rgb565(img)
            packet = build_packet(monitor['width'], monitor['height'], rgb565)

            ser.write(packet)

            frame_count += 1
            if frame_count % 10 == 0:
                elapsed = time.time() - t0
                print(f"已发送 {frame_count} 帧, 平均 {frame_count/elapsed:.2f} fps")

if __name__ == "__main__":
    main()