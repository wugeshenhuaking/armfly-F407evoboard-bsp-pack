---
name: bsp-driver-generator
description: 生成安富莱 STM32F407 V5 BSP 驱动包风格的外设驱动文件（bsp_xxx.c / bsp_xxx.h）。当用户要求新增、创建或生成某个 BSP 外设驱动（例如 bsp_led.c、bsp_adc.c、bsp_dac.c、bsp_pwm.c、bsp_uart.c 等）时使用。本技能规定标准文件头注释框、函数命名约定（bsp_Xxx）、在 bsp_Init() 中调用的初始化约定（bsp_InitXxx 入口，内部依次调用 bsp_InitHardXxx 硬件初始化与 bsp_InitVarXxx 变量初始化）、空格缩进（每级 4 个空格，禁止制表符）、UTF-8 无 BOM 编码等规范。所有生成文件中的注释一律使用英文；文件末尾的 END OF FILE 行之后保留一个空行。
---

# BSP 驱动生成器（安富莱 V5 风格）

## 用途
生成符合安富莱 STM32F407 V5 BSP 包既有风格的外设驱动源文件，使新增驱动与 bsp_key.c、bsp.c 等现有文件在结构、命名、注释、编码上保持一致。

## 何时使用
- 用户要求"新增 / 创建 / 生成某个 BSP 驱动"，如 `bsp_led.c`、`bsp_adc.c`。
- 需要为某外设新建配套的 `.c` 与 `.h`。

## 使用步骤
1. 由外设英文名确定 `xxx`（全小写）与 `Xxx`（首字母大写，用于函数名）。例：按键 key → `bsp_key.c` / `bsp_InitKey`。
2. 依据 `references/template.md` 产出 `bsp_xxx.c` 与 `bsp_xxx.h` 两个文件。
3. 文件头统一使用"星框"注释，字段全部为**英文**（Module Name、File Name、Version、Description、Revision History、Copyright）。
4. 仅生成以下三个初始化函数（`Xxx` 为用户指定的外设名，首字母大写）：
   - `void bsp_InitXxx(void)`：初始化入口，在 `bsp.c` 的 `bsp_Init()` 中调用；函数体内**依次调用** `bsp_InitHardXxx()` 与 `bsp_InitVarXxx()`。
   - `void bsp_InitHardXxx(void)`：硬件初始化（使能时钟、配置 GPIO 与外设寄存器）。
   - `void bsp_InitVarXxx(void)`：软件变量初始化（变量默认值、运行状态）。
5. 若该外设后续需要周期处理，可另行补充 `bsp_XxxScan10ms(void)`（由 Systick 中断每 10ms 调用），但默认不生成。
6. 缩进使用 **4 个空格，禁止使用制表符（Tab）**；所有注释一律使用**英文**且用 `/* */`；文件末尾追加 `END OF FILE` 行，并在其后保留**一个空行**。
7. 编码遵循项目 `rules/encoding.md`：一律 **UTF-8（无 BOM）**；若文件原为 GB2312 先转 UTF-8 再改。
8. 在 `bsp.h` 中 `#include` 新增的头文件（如尚未包含），确保 `bsp_Init()` 能编译到新的 `bsp_InitXxx()`。

## 注意
- 函数命名强制 `bsp_` 前缀 + 首字母大写的动作/外设名，保持与现有 BSP 包一致。
- 注释一律英文（含文件头星框、函数注释块、行内注释）；代码中的字符串常量（如 LCD 显示用的中文）不受此限。
- 跳过第三方文件（`Libraries/`、`User/segger/` 等），详见 `rules/encoding.md`。
- 缩进只能使用空格，禁止使用制表符（Tab）；编辑器应配置为"插入空格"而非"保留 Tab"，确保生成的 `.c/.h` 不含任何 `\t` 字符。
