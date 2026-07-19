# WebUSB 与网络（HTTP/WebSocket）联动方案

> 目标：把 STM32 设备通过 USB 传出的数据，最终送到远端（公司/云端）进行分析。
> 本文档整理自开发过程中的讨论，包含原理、架构、浏览器端改造、服务器端示例，以及"安全上下文"相关的坑。

---

## 1. 核心结论先说

**WebUSB 只能连接"本地物理插着的 USB 设备"，它不能跨网络。**

所以"设备放客户现场、公司电脑直接用 WebUSB 读"这件事，单靠 WebUSB 做不到——它建立的 USB 连接只存在于**插着设备那台电脑的浏览器里**。

要实现"远程采集分析"，必须把链路拆成**两层**：

```
设备(STM32)
   │  USB 线（WebUSB 直连，只能本地）
   ▼
客户现场电脑的浏览器（WebUSB 把数据读出来）
   │  WebSocket / HTTP（这一层才是走网络的）
   ▼
公司服务器（收数据、存库）
   │  网络
   ▼
公司分析电脑 / 网页（从服务器取数据看）
```

- **WebUSB** 负责"最后一公里"（浏览器 ↔ 设备）；
- **网络传输**（WebSocket 最合适，全双工、能实时推流）负责"跨地传输"。

---

## 2. 当前工程已做的改动

文件：`User/user_task/task_usb.c` / `task_usb.h`

原始代码里 WebUSB 厂商接口（interface 1）**端点数为 0**，根本无法传数据。已按 CherryUSB 官方 `winusb2.0_template.c` 套路补上一对批量端点：

- `WEBUSB_IN_EP = 0x82`（设备 → 浏览器，批量 IN）
- `WEBUSB_OUT_EP = 0x02`（浏览器 → 设备，批量 OUT）
- 接口 `bNumEndpoints` 由 0 改为 2，`USB_CONFIG_SIZE` 同步更新为 `(48+9)`
- 新增 `webusb_tx_buffer[64]` / `webusb_rx_buffer[64]` + `webusb_tx_busy` 发送忙标志
- `webusb_in_ep_callback`：IN 完成清忙标志，长度恰为 MPS 整数倍时发 ZLP 收尾
- `webusb_out_ep_callback`：收到浏览器数据后回显，并重新 arm OUT 接收
- **`webusb_send_data(busid, data, len)`**：对外发送函数（异步，返回 0 成功 / -1 未配置）
- `USBD_EVENT_CONFIGURED` 里复位忙标志并启动首次 OUT 接收
- `webusb_hid_keyboard_init()` 注册两个新端点
- `task_usb.h` 导出 `webusb_send_data()`

HID 键盘功能原样保留，两者互不干扰。端点分配无冲突：HID 用 `0x81`，WebUSB 用 `0x82`/`0x02`。

### 设备端如何主动发数据

在任意任务里周期调用即可（例如 `bsp_RunPer10ms()` 或某个 user_task）：

```c
uint8_t buf[8] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
webusb_send_data(0, buf, 8);   /* busid = 0 */
```

浏览器点"连接"→"开始接收"就能看到这 8 字节。

---

## 3. 浏览器端：收到 USB 数据后，通过 WebSocket 上报

文件：`Doc/webusb_demo.html`（本地，接设备）

在原来 `transferIn` 收到数据的地方，除了显示，再用 WebSocket 把数据转发给公司服务器。

关键改造（在原 `read.onclick` 循环里加几行）：

```js
// 连接公司服务器（公网部署时必须用 wss://）
const ws = new WebSocket('wss://你的公司服务器:8080');

// 收到 USB 数据时：
const result = await device.transferIn(IN_EP, 64);
const data = new Uint8Array(result.data.buffer);
const hex = Array.from(data).map(b => b.toString(16).padStart(2, '0')).join(' ');
println('收到 ' + data.length + ' 字节: ' + hex);

// 同时上报到服务器
if (ws.readyState === WebSocket.OPEN) {
    ws.send(JSON.stringify({ ts: Date.now(), data: Array.from(data) }));
}
```

---

## 4. 服务器端：接收上报并存储/转发

新增一个 WebSocket 服务（几十行）。以下给出两种可选实现。

### 4.1 Node.js 最小示例（`server.js`）

```js
// npm install ws
const { WebSocketServer } = require('ws');
const fs = require('fs');

const wss = new WebSocketServer({ port: 8080 });
wss.on('connection', (ws) => {
    ws.on('message', (m) => {
        const line = `[${new Date().toISOString()}] ${m.toString()}\n`;
        console.log('收到上报:', line.trim());
        fs.appendFileSync('upload.log', line);   // 落盘，供后续分析
    });
});
console.log('WebSocket 服务器已启动: ws://0.0.0.0:8080');
```

运行：`node server.js`

### 4.2 Python 最小示例（`server.py`）

```python
# pip install websockets
import asyncio, json, datetime

async def handler(ws):
    async for msg in ws:
        line = f"[{datetime.datetime.now().isoformat()}] {msg}"
        print("收到上报:", line.strip())
        with open("upload.log", "a", encoding="utf-8") as f:
            f.write(line + "\n")

async def main():
    import websockets
    async with websockets.serve(handler, "0.0.0.0", 8080):
        print("WebSocket 服务器已启动: ws://0.0.0.0:8080")
        await asyncio.Future()

asyncio.run(main())
```

运行：`python server.py`

### 4.3 公司分析端

可以是同一个网页的"管理员视图"，或另一台电脑连服务器订阅数据（再起一个 WebSocket 客户端订阅 `upload.log` 或服务器主动推送）。

---

## 5. 安全上下文（Secure Context）详解

WebUSB API 只能在**安全上下文**里用。Chrome 认定的安全上下文：

| 来源 | 是否安全上下文 | WebUSB 支持度 |
|---|---|---|
| `https://` | ✅ | 完整支持 |
| `http://localhost`（含 127.0.0.1） | ✅ | 完整支持 |
| `file://` | ⚠️ 理论算，但实际**各版本/各浏览器行为不一致** | 经常受限/失败 |

WebSocket 同理：浏览器只认 `wss://`（加密），不认明文 `ws://` 跨公网。

**结论：统一用 `http://localhost` 调试，公网部署必须用 `https://` + `wss://`。不要依赖 `file://`。**

---

## 6. 为什么 VSCode 里（显示 file://）能用，独立 Chrome 双击 file:// 却不行

### 现象

- VSCode 内置预览面板打开 `file://.../webusb_demo.html` → 能连 WebUSB；
- 独立 Chrome 双击同一文件（`file://`）→ 报"找不到文件"（实际多为 `requestDevice` 抛 `NotFoundError`，找不到匹配 VID/PID 的设备，而非 HTML 文件本身找不到）。

### 原因

1. **VSCode 用的是内置 Webview 渲染环境（基于 Electron/Chromium）**，不是独立 Chrome 进程。它对 `file://` 和 WebUSB 的权限控制比独立 Chrome **宽松得多**（Electron 默认给 file:// 同源、Webview 有特权），所以能直接连。
2. **独立 Chrome 在 `file://` 下对 WebUSB 有额外安全限制**。`file://` 没有可靠的 origin（来源标识），Chrome 无法给它管理设备权限，于是对 `requestDevice` 的枚举/弹窗做了限制，表现就是"列不出设备 / 找不到设备"。
3. 一旦起 http 服务器、用 `http://localhost` 打开，localhost 是**明确受支持、限制最少**的安全上下文，一切正常。

> 注意：如果 Chrome 真的是"HTML 文件本身打不开"，通常是双击时路径含中文/空格被截断；但本例页面能进到 `requestDevice` 那步，说明页面已正常加载，故更可能是上面的设备枚举限制。

---

## 7. 真实部署的注意事项

1. **服务器必须公网可达 + HTTPS/WSS**（WebUSB 和 WebSocket 都要求安全上下文）；
2. **必须加认证**（否则谁都能往服务器塞数据）；
3. **数据量大时考虑缓冲/重传**（客户电脑网络断了要能续传）；
4. **如果客户现场没有能开浏览器的电脑**，WebUSB 这条路不合适，需换成"设备 → 串口/4G 模块 → 服务器"的纯嵌入式方案；
5. HTML 里的 `VID/PID`（当前为 `0xffff`）必须和 `task_usb.c` 的 `USBD_VID / USBD_PID` 一致，改一端要同步另一端。

---

## 8. 待办 / 下一步

- [ ] 将 `webusb_demo.html` 升级为"收到 USB 数据自动 WebSocket 上报"版本；
- [ ] 在 `Doc/` 下新增最小服务器（`server.js` 或 `server.py`）与"公司分析页"，本地跑通整条链路；
- [ ] 在 `webusb_demo.html` 顶部加提示："不要用 file:// 直接打开，请用本地 http 服务器或 Live Server"；
- [ ] 设备端把 `webusb_send_data()` 接入周期任务，方便上电即看到流数据。

---

*END OF FILE*
