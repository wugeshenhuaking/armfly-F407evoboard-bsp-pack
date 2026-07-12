/*==========================================================================
 *  utility.h — 通用工具函数库
 *  适配说明:
 *    - 全部使用 <stdint.h> 标准类型
 *    - 变量定义在 utility.c, 此处仅 extern 声明
 *    - 适用于 STM32 Cortex-M3/M4 (含位带操作)
 *==========================================================================*/
#ifndef __UTILITY_H__
#define __UTILITY_H__

#include <stdint.h>

/*==========================================================================
 *  常量定义
 *==========================================================================*/
#ifndef PI
#define PI              3.1415926535
#endif

#define DATA_POOL_SIZE  8192

/*==========================================================================
 *  数据池
 *==========================================================================*/
typedef struct {
    uint32_t size;
    uint32_t used_size;
    uint8_t  buf[DATA_POOL_SIZE];
} DATA_POOL_T;

/*==========================================================================
 *  全局变量 (定义于 utility.c)
 *  cnt_1ms / cnt_1ms_H / delay_k / g_p_data_pool / reserved
 *  放置在固定地址, 用于 Bootloader-APP 通信或汇编直接访问.
 *==========================================================================*/
extern DATA_POOL_T       g_data_pool;
extern volatile uint32_t cnt_1ms;
extern volatile uint32_t cnt_1ms_H;
extern uint32_t          delay_k;
extern DATA_POOL_T      *g_p_data_pool;
extern uint32_t          g_rnd0;
extern uint8_t           reserved[12];

/*==========================================================================
 *  硬件寄存器访问宏 (STM32 位带别名)
 *==========================================================================*/
#define HWREG(x)        (*((volatile uint32_t *)(x)))
#define HWREGH(x)       (*((volatile uint16_t *)(x)))
#define HWREGB(x)       (*((volatile uint8_t  *)(x)))

#define HWREGBITW(x, b) \
    HWREG(((uint32_t)(x) & 0xF0000000) | 0x02000000 | \
          (((uint32_t)(x) & 0x000FFFFF) << 5) | ((b) << 2))
#define HWREGBITH(x, b) \
    HWREGH(((uint32_t)(x) & 0xF0000000) | 0x02000000 | \
           (((uint32_t)(x) & 0x000FFFFF) << 5) | ((b) << 2))
#define HWREGBITB(x, b) \
    HWREGB(((uint32_t)(x) & 0xF0000000) | 0x02000000 | \
           (((uint32_t)(x) & 0x000FFFFF) << 5) | ((b) << 2))

#define REG_BIT(x, b) \
    (((uint32_t)(x) & 0xF0000000) | 0x02000000 | \
     (((uint32_t)(x) & 0x000FFFFF) << 5) | ((b) << 2))

typedef volatile uint32_t BITBAND_T;

/*==========================================================================
 *  GPIO 辅助宏 (需自行包含对应芯片的 GPIO 驱动头文件)
 *==========================================================================*/
#define TstB(A, B)  ((A & (1 << (B))) != 0)
#define SetGB(A)    GPIO_SetBits(A)
#define ClrGB(A)    GPIO_ResetBits(A)
#define TogGB(A)    GPIO_WriteBit(A, (BitAction)(1 - GPIO_ReadInputDataBit(A)))
#define RdGB(A)     GPIO_ReadInputDataBit(A)
#define WrGB(A, B)  GPIO_WriteBit(A, B)

typedef void (*GpioOut_T)(uint8_t pinsta);

/*==========================================================================
 *  字节序 / 缓冲区操作
 *==========================================================================*/
uint16_t  char_hl_short(uint8_t hi, uint8_t lo);
void      short_copy_xch(void *t, const void *s, int n, uint8_t b_xch);
uint8_t  *short_wr_buf(uint8_t *buf, uint16_t s);
uint16_t  short_rd_buf(const uint8_t *buf);
uint8_t  *short_wr_buf_xch(uint8_t *buf, uint16_t s);
uint16_t  short_rd_buf_xch(const uint8_t *buf);

/*==========================================================================
 *  IAP 跳转
 *==========================================================================*/
typedef void (*Function_T)(void);

void jump_to_app(uint32_t ApplicationAddress);

#define m_jump_to_app_disallint(A) do { \
    __disable_irq();                    \
    NVIC->ICER[0] = 0xFFFFFFFF;        \
    NVIC->ICER[1] = 0xFFFFFFFF;        \
    NVIC->ICER[2] = 0xFFFFFFFF;        \
    jump_to_app(A);                     \
} while(0)

/*==========================================================================
 *  延时
 *==========================================================================*/
void delay_us_init(uint32_t sys_freq_hz);
void delay_us(uint32_t us);

/*==========================================================================
 *  位带操作 (Cortex-M3/M4)
 *==========================================================================*/
uint32_t *calc_bitadr(void *x, uint8_t b);
void      bitband_set(void *p, uint8_t b);
void      bitband_clr(void *p, uint8_t b);
void      bitband_toggle(void *p, uint8_t b);
uint8_t   bitband_tsc(void *p, uint8_t b);

/*==========================================================================
 *  便捷宏
 *==========================================================================*/
#define HRL(x)          ((x) & 0xFF)
#define HRH(x)          ((x) >> 8)
#define countof(A)      (sizeof(A) / sizeof(*(A)))
#define LastMember(A)   (A)[countof(A) - 1]
#define BufEnd(A)       (A)[sizeof(A) - 1]
#define Bit2int(A)      (1 << (A))
#define TST_BIT(A, bit) (((A) & (1 << (bit))) != 0)
#define U8_TstB(A, bit) TST_BIT(A, bit)

/*==========================================================================
 *  汇编位操作 (STM32F103_HW_ASM.S, Cortex-M0 无位带时使用)
 *==========================================================================*/
void     _U32_SetB(uint32_t *x, uint8_t b);
void     _U16_SetB(uint16_t *x, uint8_t b);
void     _U8_SetB(uint8_t *x, uint8_t b);
void     _U32_ClrB(uint32_t *x, uint8_t b);
void     _U16_ClrB(uint16_t *x, uint8_t b);
void     _U8_ClrB(uint8_t *x, uint8_t b);
void     _U32_TogB(uint32_t *x, uint8_t b);
void     _U16_TogB(uint16_t *x, uint8_t b);
void     _U8_TogB(uint8_t *x, uint8_t b);
uint8_t  _U32_TscB(uint32_t *x, uint8_t b);
uint8_t  _U16_TscB(uint16_t *x, uint8_t b);
uint8_t  _U8_TscB(uint8_t *x, uint8_t b);
uint16_t _short_xch_hl(uint16_t i);

#ifdef MCU_CORE_M0
/* Cortex-M0: 无位带, 使用汇编函数 */
#define u32_setb    _U32_SetB
#define u16_setb    _U16_SetB
#define u8_setb     _U8_SetB
#define u32_clrb    _U32_ClrB
#define u16_clrb    _U16_ClrB
#define u8_clrb     _U8_ClrB
#define u32_togb    _U32_TogB
#define u16_togb    _U16_TogB
#define u8_togb     _U8_TogB
#define u32_tscb    _U32_TscB
#define u16_tscb    _U16_TscB
#define u8_tscb     _U8_TscB
#else
/* Cortex-M3/M4: 有位带, 使用位带别名操作 */
#define u32_setb    bitband_set
#define u16_setb    bitband_set
#define u8_setb     bitband_set
#define u32_clrb    bitband_clr
#define u16_clrb    bitband_clr
#define u8_clrb     bitband_clr
#define u32_togb    bitband_toggle
#define u16_togb    bitband_toggle
#define u8_togb     bitband_toggle
#define u32_tscb    bitband_tsc
#define u16_tscb    bitband_tsc
#define u8_tscb     bitband_tsc
#endif

/*==========================================================================
 *  毫秒定时器
 *
 *  定时器值含义:
 *    0    → 未运行
 *    非零 → 到期时刻的 cnt_1ms 绝对值
 *
 *  到期判断 (无符号回绕):
 *    diff = deadline - cnt_1ms
 *    diff 的 bit31 = 1 → 已过期
 *    diff 的 bit31 = 0 → 未过期, diff 即剩余毫秒
 *
 *  最大可定时时长: 2^31 ms ≈ 24.8 天
 *==========================================================================*/
typedef uint32_t time_ms_t;

void    left_ms_set(time_ms_t *p, uint32_t val);
int32_t left_ms_sta(time_ms_t *p);         /* 到期首次返回0, 之后返回-1 */
int32_t left_ms(time_ms_t *p);             /* 到期返回0 */
void    left_ms_stop(time_ms_t *p);
int32_t left_ms_running(const time_ms_t *p);

/*==========================================================================
 *  JSON 简易解析
 *  支持格式: {"key":"str_val"} 或 {"key":123}
 *  注意: 做全字匹配 (检查键名前后双引号), 可正确跳过子串,
 *        如 {"username":"a","name":"b"} 中搜索 "name" 不会误匹配 "username".
 *==========================================================================*/
#define GET_JSON_1FS_FAIL   0
int get_json_1fs(char *buf, int buf_size, const char *s, const char *name);

/*==========================================================================
 *  HEX <-> 字符串转换
 *==========================================================================*/
#define STR_TO_HEX_FAIL     0
#define STR_TO_HEX_SUCCESS  1

int      hexchar_byte(uint8_t ch);
int      str_to_hex(uint8_t *buf, uint32_t buf_size, const char *s);
int      nstr_to_hex(uint8_t *buf, const char *s, uint32_t n);
void     hex_to_str(char *str_buf, const uint8_t *hex_buf, uint32_t hex_n);

/*==========================================================================
 *  字符串工具
 *==========================================================================*/
void str2lwr(char *p);
void str2upr(char *p);

/*==========================================================================
 *  日历 / 时间转换 (Unix 时间戳, 1970-01-01 起)
 *  注意: 闰年仅用 4 年一闰, 未完全实现百年/四百年规则,
 *        2100 年之后可能有 ±1 天偏差.
 *==========================================================================*/
typedef struct {
    uint16_t year;      /* 1970-9999 */
    uint8_t  month;     /* 1-12 */
    uint8_t  day;       /* 1-31 */
    uint8_t  hour;      /* 0-23 */
    uint8_t  min;       /* 0-59 */
    uint8_t  sec;       /* 0-59 */
    uint8_t  week;      /* 1-7 (1=周一) */
    uint32_t sec1970;   /* Unix 时间戳 */
} CALENDAR_T;

#define CALENDAR_SUCCESS    0
#define CALENDAR_FAIL      (-1)

int  calendar_int(CALENDAR_T *p);   /* 日历 → 时间戳 */
void int_calendar(CALENDAR_T *p);   /* 时间戳 → 日历 */

/*==========================================================================
 *  数值格式化
 *==========================================================================*/
int  Dword2Str(char *s, uint32_t data, uint8_t i_nb, uint8_t dec_nb,
               uint8_t b_sign, uint8_t b_inv0);
void Short_BinStr(char *buf, uint16_t us);

/*==========================================================================
 *  数据池分配 (简单线性分配器, 不支持释放)
 *==========================================================================*/
uint8_t *DataPool_Get(uint32_t size);

/*==========================================================================
 *  ASC <-> 整型转换
 *==========================================================================*/
#define ASC2INT_SUCCESS 1
#define ASC2INT_FAIL    0

int      digchar_byte(uint8_t ch);
int      asc2int_dft(const char *p, int dft);
int64_t  asc2s64_dft(const char *p, int64_t dft);
int      asc2int(const char *p, int *result);       /* 返回转换值, *result 表示成败 */
int      asc2s64(int64_t *result, const char *p);   /* 成功写入 *result 并返回1 */

/*==========================================================================
 *  函数传递表
 *==========================================================================*/
typedef struct {
    const char *func_name;
    uint32_t    func_adr;
} FUNC_TB_T;

uint32_t Get_Func(FUNC_TB_T *func_tb, const char *func_name);

/*==========================================================================
 *  通讯缓冲区
 *==========================================================================*/
typedef struct {
    uint8_t  *buf;
    uint16_t  n;
    uint16_t  size;
} COMM_BUF_T;

int  comm_buf_del_n(COMM_BUF_T *p, uint16_t n);
void comm_buf_del_all(COMM_BUF_T *p);

/*==========================================================================
 *  随机数 (伪随机, 基于 LCG)
 *==========================================================================*/
uint32_t MyRnd(void);

#endif /* __UTILITY_H__ */
