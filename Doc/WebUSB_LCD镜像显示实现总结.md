# WebUSB LCD 镜像显示 Demo 实现总结

> 把 STM32F407 开发板 LCD 软件显存实时镜像到 PC 浏览器显示，无需安装任何驱动，
> 插上 USB 即可在网页上看到开发板画面（含移动小方块等动态内容）。

---

## 1. 背景与目标

- **硬件**：安富莱 V5 开发板（STM32F407IGT6），板载 LCD（480×800，RGB565 软件显存）。
- **软件栈**：CherryUSB 协议栈，自定义一个 WebUSB 厂商接口（bulk IN/OUT）。
- **目标**：PC 端 Chrome/Edge 通过 WebUSB 直接接收显存数据并绘制到 `<canvas>`，
  做到“插上即用、实时镜像、动态不撕裂”。

---

## 2. 整体思路

### 2.1 数据通路

```
STM32 软件显存 (LCD_FRAMEBUFFER, RGB565)
        │  webusb_fb_poll() 周期性扫描脏区域
        ▼
   CherryUSB 厂商接口 (Interface 1)
        │  IN 端点 0x82：MCU → PC（显存数据）
        │  OUT 端点 0x02：PC → MCU（控制命令）
        ▼
   PC 浏览器 (WebUSB API)
        │  transferIn 收包 → 解析协议 → RGB565→RGBA
        ▼
   <canvas> 双缓冲渲染 → 屏幕
```

### 2.2 传输协议（每个 USB 包 ≤ 64 字节）

包格式：`魔数(0xAA 0x55) + type(1) + offset(4, LE) + payload`

| type | 名称 | 说明 |
|------|------|------|
| 1 | `FRAME_HEADER` | 整帧头：`fmt(1=RGB565) + w(2) + h(2)`，PC 据此分配整帧缓冲 |
| 2 | `FRAME_DATA`   | 整帧像素数据（首包带 header，后续为裸数据续接） |
| 3 | `TILE`         | 脏区域增量：`tile_x(2)+tile_y(2)+tile_w(2)+tile_h(2)+tile_off(4)+RGB565` |

> 一次 `usbd_ep_start_write` 提交会被驱动自动拆成多个 64 字节包，只有**第一个包带协议头**，
> 后续包是纯裸像素数据（无魔数），PC 端用“是否带魔数”区分新包/续接包。

### 2.3 发送策略（节省 USB 带宽）

- **连接后先发一整帧**（全屏同步），让 PC 获得完整底图。
- 之后只发**脏 tile**：用 `fb_dirty[FB_DIRTY_BYTES]` 位图标记变化的 32×32 tile，
  未变化区域完全不占 USB 带宽。
- 每个 tile / 数据块通过一次 `usbd_ep_start_write` 提交，由 IN 端点回调驱动后续，
  非阻塞，main 循环不冻结。

### 2.4 PC 端渲染

- `transferIn` 持续收包，按协议重组整帧 / 增量贴 tile。
- RGB565 → RGBA：`r=(r5<<3)|(r5>>2)` 等，写入 `ImageData`。
- 双缓冲：先画到离屏 `backCanvas`，再 `requestAnimationFrame` 每帧整体 blit。

---

## 3. 遇到的困难与解决方案

### 困难 1：浏览器缩放导致“一列像素丢失”

- **现象**：PC 界面有一列像素丢失，细字体竖笔画被吞。
- **根因**：`canvas` 按 `max-height: 85vh` 做**非整数缩放**，在 `image-rendering: pixelated`
  模式下浏览器把某些源像素列合并/丢弃。
- **解决**：
  - 默认 **1:1 显示**（`canvas` 尺寸 = 480×800 CSS 像素），每个源像素对应一个 CSS 像素。
  - 新增缩放按钮：`适配窗口` / `0.5x`（整数缩放，仍不丢列） / `1:1`。
  - 修复 `drawTile` 在 tile 被右/下边缘裁剪时，按**原始 tile 宽度**做行 stride 读取
    （旧代码按裁剪后宽度连续读，导致像素错位）。

### 困难 2：移动方块出现横向撕裂

- **现象**：小方块（高 30px，跨 tile 行 23/24）上半快、下半慢，中间一条撕裂缝；
  **LCD 上不撕裂**（屏幕刷新率高、整屏同步写 GRAM）。
- **根因**：PC 端**逐 tile 接收、逐 tile `putImageData`**，两次绘制之间浏览器发生一次重绘，
  于是“上半已到新位置、下半还是旧的”，形成横向撕裂线。
- **解决**：**双缓冲**
  - 所有 tile / 整帧先画到离屏 `backCanvas`（与显存同尺寸）。
  - 新增 `blitLoop`（`requestAnimationFrame` 循环），每帧把 `backCanvas` 整体
    `drawImage` 到可见 `canvas`，且仅在有更新（`needBlit`）时提交。
  - 效果：一个逻辑帧内所有 tile 原子地一起出现，撕裂消失。

### 困难 3：刷新浏览器后画面不出现、日志卡住

- **现象**：只刷新 HTML（MCU 继续运行），重连后界面不出现，日志停在“已连接设备”。
- **根因**（两层）：
  1. MCU 的 `fb_force_full = 1`（强制发整帧）**只在 `USBD_EVENT_CONFIGURED`
     （首次 USB 枚举）时置位**。刷新网页只是 WebUSB 层面断开重连，**MCU 不会重新枚举**，
     所以只发脏 tile，不再发整帧头。
  2. 浏览器刷新后 JS 状态全清，`backCanvas` 为 `null`；收到 `type=3` tile 时
     `drawTile` 调 `backCtx.createImageData` **直接抛异常**，`readLoop` 被 `catch` 终止，
     接收循环被打断，日志/画面全停。
- **解决**：
  - **MCU**：OUT 端点回调识别命令 `CMD_REQ_FULL_FRAME(0x01)`，收到后
    `fb_tx_active = 0; fb_force_full = 1;`，下一轮 `webusb_fb_poll` 立即发整帧。
  - **HTML**：
    - 点击“连接 USB 设备”后，主动 `transferOut(OUT_EP, [0x01])` 请求整帧底图。
    - 连接时重置所有接收状态（`frameBuf/backCanvas/gotHeader/recvTotal` 等）。
    - 新增 `gotHeader` 标志：**未收到整帧头前忽略脏 tile**，避免 `null` backCtx 异常。
    - `drawTile` 增加 `backCtx` 空指针防御。
    - 状态栏显示累计接收 KB，整帧渲染完打印日志。

### 困难 4：HTML 引用未定义常量 `WEBUSB_OUT_EP`

- **现象**：请求整帧失败 `ReferenceError: WEBUSB_OUT_EP is not defined`。
- **根因**：该常量只定义在 MCU 的 `task_usb.c`，HTML 没有。
- **解决**：HTML 顶部加 `const OUT_EP = 2;`（对应 MCU 的 `WEBUSB_OUT_EP = 0x02`），
  发送命令改用 `device.transferOut(OUT_EP, ...)`。

### 困难 5：WebUSB 落地页跳转 404

- **现象**：插上 USB 浏览器弹出提示，点击跳转到 github 并显示 404。
- **根因**：
  1. URL 描述符写死 `github.com/cherry-embedded/CherryUSB`。
  2. 本地 `python -m http.server` 从**项目根目录**启动，文件实际在 `Doc/` 子目录，
     路径不对。
- **解决**：
  - 把 WebUSB URL 描述符改为 `localhost:8000/Doc/webusb_lcd_mirror.html`。
  - scheme 用 `WEBUSB_URL_SCHEME_HTTP`（localhost 被浏览器视为安全上下文，可用 http）。
  - `URL_DESCRIPTOR_LENGTH` 改为 `3 + 41 = 44`。
  - 提醒：服务器须从项目根目录启动，否则路径需相应调整。

---

## 4. 关键代码位置

### MCU 端 —— `User/user_task/task_usb.c`
| 函数 / 宏 | 作用 |
|-----------|------|
| `webusb_out_ep_callback` | 解析 OUT 命令 `CMD_REQ_FULL_FRAME(0x01)`，触发整帧同步 |
| `webusb_fb_poll` | 周期性调用：连接后发整帧，之后只发脏 tile |
| `webusb_fb_continue` | 发送状态机，驱动整帧/脏 tile 的续传 |
| `webusb_fb_send_tile` | 构建并发送单个脏 tile（含 15 字节协议头） |
| `USBD_WebUSBURLDescriptor` | WebUSB 落地页 URL（插 USB 后浏览器提示跳转地址） |
| `fb_mark_dirty_rect` / `fb_dirty` | 脏区域位图标记 |

### PC 端 —— `Doc/webusb_lcd_mirror.html`
| 位置 | 作用 |
|------|------|
| `connect` 处理 | 连接后 `transferOut` 发请求整帧命令、重置状态 |
| `readLoop` | 收包、协议解析、`gotHeader` 守卫（未收帧头前丢弃 tile） |
| `drawTile` / `renderFrame` | RGB565→RGBA，画到离屏 `backCanvas` |
| `blitLoop` | `requestAnimationFrame` 每帧整体 blit，消除撕裂 |

---

## 5. 使用步骤

1. **编译烧录固件**（注意：本工程须用 **AC5 / ARMCC V5.06** 编译，AC6 在底层跳转等代码上会
   产生 HardFault，详见工程记忆）。
2. 在**项目根目录** `V5-000_程序模板/` 下启动本地服务：
   ```bash
   python -m http.server 8000
   ```
3. 用 Chrome/Edge 打开 `http://localhost:8000/Doc/webusb_lcd_mirror.html`。
4. 插上 USB，浏览器弹出“设备已插入”提示，**点击跳转**到上述页面。
5. 页面内点“连接 USB 设备” → 自动发送请求整帧命令 → 显示完整 LCD 画面，
   之后动态内容靠脏 tile 增量更新。

---

## 6. 注意事项

- **刷新浏览器 ≠ MCU 重新枚举**：MCU 不会因网页刷新而重新配置，因此必须靠 OUT 命令
  `CMD_REQ_FULL_FRAME` 主动请求整帧，否则画面不出现。
- **服务器目录决定 URL 路径**：若改在 `Doc/` 目录起服务，须把固件 URL 中的 `/Doc/` 去掉再烧录。
- **1:1 显示最清晰**：非整数缩放会丢像素列，建议用 `1:1` 或 `0.5x`。
- **撕裂靠双缓冲解决**：若后续新增其它动态元素，继续走“先画 backCanvas、rAF 整体提交”的模式。
- 若想降低动态内容延迟，可把 `app_task_fb` 的发送周期从 100ms 调小（如 30~50ms），
  但 USB 流量会相应增加。
