/*==========================================================================
 *  utility.c — 通用工具函数库
 *==========================================================================*/
#include "utility.h"
#include <string.h>

/*==========================================================================
 *  全局变量定义
 *
 *  cnt_1ms / cnt_1ms_H / delay_k / g_p_data_pool / reserved
 *  放置在固定地址 (STM32 SRAM 起始), 用于 Bootloader-APP 通信或汇编访问.
 *  g_rnd0 为普通全局变量, 无固定地址.
 *==========================================================================*/
DATA_POOL_T g_data_pool = { DATA_POOL_SIZE, 0, {0} };

volatile uint32_t cnt_1ms       __attribute__((section(".ARM.__at_0x20000000")));
volatile uint32_t cnt_1ms_H     __attribute__((section(".ARM.__at_0x20000004")));
uint32_t          delay_k       __attribute__((section(".ARM.__at_0x20000008")));
DATA_POOL_T      *g_p_data_pool __attribute__((section(".ARM.__at_0x2000000C"))) = &g_data_pool;
uint32_t          g_rnd0;
uint8_t           reserved[12]  __attribute__((section(".ARM.__at_0x20000014")));

/*==========================================================================
 *  字节序 / 缓冲区操作
 *==========================================================================*/

/* 两个字节合成一个 uint16_t (小端: lo 在低地址) */
uint16_t char_hl_short(uint8_t hi, uint8_t lo)
{
    return (uint16_t)((uint16_t)hi << 8) | lo;
}

/* 从源缓冲区拷贝 n 个 uint16_t 到目的缓冲区, b_xch=1 时逐字节交换 */
void short_copy_xch(void *t, const void *s, int n, uint8_t b_xch)
{
    uint8_t       *pt = (uint8_t *)t;
    const uint8_t *ps = (const uint8_t *)s;
    int i;

    if (b_xch) {
        for (i = 0; i < n; i++) {
            uint8_t ch = *ps++;
            *pt++ = *ps++;
            *pt++ = ch;
        }
    } else {
        n *= 2;
        for (i = 0; i < n; i++) {
            *pt++ = *ps++;
        }
    }
}

/* uint16_t 写入缓冲区 (小端) */
uint8_t *short_wr_buf(uint8_t *buf, uint16_t s)
{
    *buf++ = (uint8_t)(s);
    *buf++ = (uint8_t)(s >> 8);
    return buf;
}

/* 从缓冲区读取 uint16_t (小端) */
uint16_t short_rd_buf(const uint8_t *buf)
{
    return (uint16_t)buf[0] | ((uint16_t)buf[1] << 8);
}

/* uint16_t 交换高低字节后写入缓冲区 (大端) */
uint8_t *short_wr_buf_xch(uint8_t *buf, uint16_t s)
{
    *buf++ = (uint8_t)(s >> 8);
    *buf++ = (uint8_t)(s);
    return buf;
}

/* 从缓冲区按大端读取 uint16_t */
uint16_t short_rd_buf_xch(const uint8_t *buf)
{
    return ((uint16_t)buf[0] << 8) | (uint16_t)buf[1];
}

/*==========================================================================
 *  IAP 跳转
 *==========================================================================*/
void jump_to_app(uint32_t ApplicationAddress)
{
    Function_T Jump_To_Application;

    /* 从向量表偏移 +4 取复位中断入口 */
    Jump_To_Application = (Function_T)(*(volatile uint32_t *)(ApplicationAddress + 4));

#if defined(__ARMCC_VERSION) && (__ARMCC_VERSION < 6000000)
    /* AC5 (armcc) 原生内联汇编 */
    __asm("MSR MSP, *(volatile unsigned int*) ApplicationAddress");
#elif defined(__clang__) || defined(__ARM_COMPUTE_CLANG)
    /* AC6 (armclang) GNU 扩展汇编 */
    {
        volatile uint32_t app_msp = *(volatile uint32_t *)ApplicationAddress;
        __asm volatile (
            "MSR MSP, %0"
            :
            : "r"(app_msp)
            : "memory"
        );
    }
#endif

    Jump_To_Application();
}

/*==========================================================================
 *  延时函数
 *==========================================================================*/

/*
 * 根据系统时钟频率计算延时系数并初始化.
 * sys_freq_hz: CPU 主频 (Hz), 例: 72000000 表示 72MHz.
 *
 * 原理: delay_us() 每次空循环约 3 个时钟周期,
 *       delay_k = sys_freq_hz / 3000000
 *       使 delay_us(1) ≈ 1 微秒.
 */
void delay_us_init(uint32_t sys_freq_hz)
{
    delay_k = sys_freq_hz / 3000000;
    if (delay_k == 0) {
        delay_k = 1;    /* 防止系数为 0 导致延时失效 */
    }
}

/* 微秒级忙等待延时 (近似值, 适用于非精确场景) */
void delay_us(uint32_t us)
{
    uint32_t i;
    us *= delay_k;
    if (us > 3) {
        us -= 3;        /* 补偿函数调用/循环初始化开销 */
    }
    for (i = 0; i < us; i++) {
        ;               /* 每次循环约 3 个时钟周期 */
    }
}

/*==========================================================================
 *  位带操作 (Cortex-M3/M4 SRAM 和外设区域)
 *==========================================================================*/

uint32_t *calc_bitadr(void *x, uint8_t b)
{
    uint32_t y;
    y  = ((uint32_t)x) & 0xF0000000;
    y |= 0x02000000;
    y |= ((uint32_t)x & 0x000FFFFF) << 5;
    y |= (uint32_t)b << 2;
    return (uint32_t *)y;
}

void bitband_set(void *p, uint8_t b)
{
    *calc_bitadr(p, b) = 1;
}

void bitband_clr(void *p, uint8_t b)
{
    *calc_bitadr(p, b) = 0;
}

void bitband_toggle(void *p, uint8_t b)
{
    uint32_t *bitband = calc_bitadr(p, b);
    *bitband = (*bitband) ? 0 : 1;
}

uint8_t bitband_tsc(void *p, uint8_t b)
{
    uint32_t *bitband = calc_bitadr(p, b);
    if (*bitband) {
        *bitband = 0;
        return 1;
    }
    return 0;
}

/*==========================================================================
 *  毫秒定时器
 *==========================================================================*/

void left_ms_set(time_ms_t *p, uint32_t val)
{
    *p = cnt_1ms + val;
    if (*p == 0) {          /* 0 保留表示"未运行" */
        *p = 1;
    }
}

int32_t left_ms_sta(time_ms_t *p)
{
    uint32_t diff;

    if (*p == 0) {
        return -1;          /* 未启动 */
    }

    diff = *p - cnt_1ms;

    if (diff & 0x80000000U) {
        *p = 0;             /* 到期, 自动停止 */
        return 0;
    }

    return (int32_t)diff;   /* 剩余毫秒 */
}

int32_t left_ms(time_ms_t *p)
{
    uint32_t diff;

    if (*p == 0) {
        return 0;           /* 未启动, 视为已到期 */
    }

    diff = *p - cnt_1ms;

    if (diff & 0x80000000U) {
        *p = 0;
        return 0;
    }

    return (int32_t)diff;
}

void left_ms_stop(time_ms_t *p)
{
    *p = 0;
}

int32_t left_ms_running(const time_ms_t *p)
{
    return (*p != 0U) ? 1 : 0;
}

/*==========================================================================
 *  JSON 简易解析
 *==========================================================================*/

int get_json_1fs(char *buf, int buf_size, const char *s, const char *name)
{
    const char *p0, *p1, *pe;
    int n, name_len;

    /* 检查基本 JSON 格式: 至少 {"x":0} 共 7 字符, 首尾 {} */
    n = (int)strlen(s);
    if (n < 7 || s[0] != '{' || s[n - 1] != '}') {
        return GET_JSON_1FS_FAIL;
    }
    pe = s + n - 1;

    /* 在 JSON 中查找 "name" 键 (全字匹配: 前后须有双引号) */
    name_len = (int)strlen(name);
    p0 = s;
    while ((p0 = strstr(p0, name)) != NULL) {
        if (p0 > s && *(p0 - 1) == '"' && *(p0 + name_len) == '"') {
            break;
        }
        p0++;   /* 子串误匹配, 继续向后搜索 */
    }
    if (p0 == NULL) {
        return GET_JSON_1FS_FAIL;
    }

    /* 跳过 "name" : */
    p0 += name_len;
    if (*p0 != '"') return GET_JSON_1FS_FAIL;
    p0++;
    if (*p0 != ':') return GET_JSON_1FS_FAIL;
    p0++;

    while (*p0 == ' ') {
        p0++;
    }

    if (*p0 == '"') {
        /* 值是字符串: "value" */
        p0++;   /* 跳过开头的 " */
        p1 = strchr(p0, '"');
        if (p1 == NULL) return GET_JSON_1FS_FAIL;
        n = (int)(p1 - p0);
        p1++;
        if (*p1 != ',' && *p1 != '}') return GET_JSON_1FS_FAIL;
    } else {
        /* 值是数字: 找到逗号或结束大括号 */
        p1 = strchr(p0, ',');
        if (p1 == NULL) {
            p1 = pe;    /* 最后一个字段, 值到 '}' 为止 */
        }
        n = (int)(p1 - p0);
    }

    if (n >= buf_size) {
        return GET_JSON_1FS_FAIL;   /* 值过长 */
    }

    memcpy(buf, p0, (uint32_t)n);
    buf[n] = '\0';
    return n;
}

/*==========================================================================
 *  HEX <-> 字符串转换
 *==========================================================================*/

int hexchar_byte(uint8_t ch)
{
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}

/* 字符串 (hex) → 字节数组, 返回转换后的字节数, 失败返回 0 */
int str_to_hex(uint8_t *buf, uint32_t buf_size, const char *s)
{
    uint32_t i, n;
    int hi, lo;

    n = (uint32_t)strlen(s);
    if (n & 1U) {
        return STR_TO_HEX_FAIL;        /* 长度必须为偶数 */
    }
    n /= 2;
    if (n > buf_size) {
        return STR_TO_HEX_FAIL;
    }

    for (i = 0; i < n; i++) {
        hi = hexchar_byte((uint8_t)*s++);
        lo = hexchar_byte((uint8_t)*s++);
        if (hi < 0 || lo < 0) {
            return STR_TO_HEX_FAIL;
        }
        *buf++ = (uint8_t)((hi << 4) | lo);
    }
    return (int)n;
}

/* 固定长度字符串 → 字节数组 */
int nstr_to_hex(uint8_t *buf, const char *s, uint32_t n)
{
    uint32_t i;
    int hi, lo;

    if (n & 1U) {
        return STR_TO_HEX_FAIL;
    }
    n /= 2;

    for (i = 0; i < n; i++) {
        hi = hexchar_byte((uint8_t)*s++);
        lo = hexchar_byte((uint8_t)*s++);
        if (hi < 0 || lo < 0) {
            return STR_TO_HEX_FAIL;
        }
        *buf++ = (uint8_t)((hi << 4) | lo);
    }
    return (int)n;
}

/* 字节数组 → HEX 字符串 (自动追加 '\0') */
void hex_to_str(char *str_buf, const uint8_t *hex_buf, uint32_t hex_n)
{
    static const char hex_table[] = "0123456789ABCDEF";
    uint32_t i;

    for (i = 0; i < hex_n; i++) {
        *str_buf++ = hex_table[hex_buf[i] >> 4];
        *str_buf++ = hex_table[hex_buf[i] & 0x0F];
    }
    *str_buf = '\0';
}

/*==========================================================================
 *  字符串工具
 *==========================================================================*/

void str2lwr(char *p)
{
    while (*p) {
        if (*p >= 'A' && *p <= 'Z') {
            *p += ('a' - 'A');
        }
        p++;
    }
}

void str2upr(char *p)
{
    while (*p) {
        if (*p >= 'a' && *p <= 'z') {
            *p -= ('a' - 'A');
        }
        p++;
    }
}

/*==========================================================================
 *  日历 / 时间转换
 *==========================================================================*/

static const uint8_t tb_month_days[] = {
    0,
    31, 28, 31, 30, 31, 30,     /* 1-6 月 */
    31, 31, 30, 31, 30, 31      /* 7-12 月 */
};

#define TIME_DATA_YEAR_ORG  1970
#define SECONDS_DAY         (3600UL * 24)
#define SECONDS_YEAR        (3600UL * 24 * 365)
#define DAYS_YEAR           365

/* 日历 → Unix 时间戳 */
int calendar_int(CALENDAR_T *p)
{
    uint32_t i, j, k, days;
    uint8_t  b_leap;

    i = p->year;
    if (i < TIME_DATA_YEAR_ORG) return CALENDAR_FAIL;

    b_leap = (((i % 400) == 0) || ((i % 4 == 0) && (i % 100 != 0))) ? 1 : 0;

    j = i - TIME_DATA_YEAR_ORG;
    days = DAYS_YEAR * j + j / 4;

    i = p->month;
    if (i < 1 || i > 12) return CALENDAR_FAIL;

    k = 0;
    for (j = 1; j < i; j++) {
        k += tb_month_days[j];
    }
    if (i >= 3) {
        k += b_leap;
    }
    days += k;

    i = p->day;
    if (i < 1 || i > ((uint32_t)tb_month_days[p->month] + b_leap)) {
        return CALENDAR_FAIL;
    }
    days += (i - 1);

    k = days * 24;
    i = p->hour;
    if (i > 23) return CALENDAR_FAIL;
    k += i;
    k *= 60;

    i = p->min;
    if (i > 59) return CALENDAR_FAIL;
    k += i;
    k *= 60;

    i = p->sec;
    if (i > 59) return CALENDAR_FAIL;
    k += i;

    p->sec1970 = k;
    return CALENDAR_SUCCESS;
}

/* Unix 时间戳 → 日历 */
void int_calendar(CALENDAR_T *p)
{
    uint32_t i, j, k, days;
    uint8_t  b_leap;

    i = p->sec1970;
    days = i / SECONDS_DAY;
    p->week = (uint8_t)(((days + 3) % 7) + 1);

    j = days / 365;
    if ((365 * j + j / 4) > days) {
        j--;
    }

    k = 1970 + j;
    p->year = (uint16_t)k;

    b_leap = (((k % 400) == 0) || ((k % 4 == 0) && (k % 100 != 0))) ? 1 : 0;
    days -= (j * 365 + j / 4);

    k = 0;
    for (i = 1; i <= 12; i++) {
        j = k + tb_month_days[i];
        if (b_leap && i == 2) j++;
        if (j > days) break;
        k = j;
    }

    p->month = (uint8_t)i;
    p->day   = (uint8_t)(days - k + 1);

    i = p->sec1970 % SECONDS_DAY;
    p->hour = (uint8_t)(i / 3600);
    i %= 3600;
    p->min  = (uint8_t)(i / 60);
    p->sec  = (uint8_t)(i % 60);
}

/*==========================================================================
 *  数值格式化
 *==========================================================================*/

/*
 * 将 uint32_t 转为格式化字符串
 * s:       输出缓冲区
 * data:    数据值
 * i_nb:    整数位数
 * dec_nb:  小数位数 (0=无小数)
 * b_sign:  1=有符号 (最高位为符号位)
 * b_inv0:  1=显示无效前导零, 0=前导零替换为空格
 * 返回值:  写入的字符数 (不含 '\0')
 */
int Dword2Str(char *s, uint32_t data, uint8_t i_nb, uint8_t dec_nb,
              uint8_t b_sign, uint8_t b_inv0)
{
    int   i, j, char_nb;
    char *p;
    uint8_t b_s = 0;

    if (!i_nb) i_nb++;
    char_nb = i_nb;
    if (dec_nb) {
        char_nb += 1 + dec_nb;  /* 小数位 + 小数点 */
    }

    if (b_sign) {
        if ((int32_t)data < 0) {
            data = 0U - data;   /* 取绝对值 (无符号运算, 无溢出风险) */
            b_s = 1;
        }
    }

    p = s + char_nb + b_sign;
    *p-- = '\0';

    for (i = 0; i < char_nb; i++) {
        if (i != 0 && i == dec_nb) {
            *p-- = '.';
        } else {
            j = data % 10;
            data /= 10;
            *p-- = (char)(j + '0');
        }
    }

    if (b_sign) {
        *p = ' ';       /* 符号位先填空格 */
    }

    p++;
    if (!b_inv0 && i_nb) {
        /* 前导零替换为空格 */
        for (i = 0; i < i_nb - 1; i++) {
            if (*p == '0') {
                *p = ' ';
            } else {
                break;
            }
            p++;
        }
    }

    if (b_sign) {
        p--;
        *p = b_s ? '-' : ' ';
    }

    return char_nb + b_sign;
}

/* uint16_t 按二进制位展开为 16 字符的字符串 ('0'/'1') */
void Short_BinStr(char *buf, uint16_t us)
{
    int i;
    uint16_t mask = 0x8000;
    for (i = 0; i < 16; i++) {
        *buf++ = (us & mask) ? '1' : '0';
        mask >>= 1;
    }
    *buf = '\0';
}

/*==========================================================================
 *  数据池
 *==========================================================================*/

uint8_t *DataPool_Get(uint32_t size)
{
    uint8_t *r = NULL;

    if (size > (g_p_data_pool->size - g_p_data_pool->used_size)) {
        /* 内存池耗尽 — 死循环报警 (嵌入式常见处理, 可改为断言或返回NULL) */
        while (1);
    }

    r = g_p_data_pool->buf + g_p_data_pool->used_size;
    g_p_data_pool->used_size += size;
    return r;
}

/*==========================================================================
 *  ASC <-> 整型转换
 *==========================================================================*/

int digchar_byte(uint8_t ch)
{
    if (ch >= '0' && ch <= '9') {
        return ch - '0';
    }
    return -1;
}

/* ASC → int, 转换失败返回 dft */
int asc2int_dft(const char *p, int dft)
{
    uint8_t  b_neg = 0;
    int      i, j;
    uint32_t r = 0;

    if (*p == '-') {
        p++;
        b_neg = 1;
    }

    for (i = 0; i < 10; i++) {
        uint8_t ch = (uint8_t)*p++;
        if (ch == 0) break;
        j = digchar_byte(ch);
        if (j < 0) return dft;
        r = r * 10 + (uint32_t)j;
    }

    if (r >= 0x80000000U) return 0;
    if (b_neg) r = 0U - r;
    return (int)r;
}

/* ASC → int64_t, 转换失败返回 dft */
int64_t asc2s64_dft(const char *p, int64_t dft)
{
    uint8_t  b_neg = 0;
    int      i, j;
    uint64_t r = 0;

    if (*p == '-') {
        p++;
        b_neg = 1;
    }

    for (i = 0; i < 19; i++) {
        uint8_t ch = (uint8_t)*p++;
        if (ch == 0) break;
        j = digchar_byte(ch);
        if (j < 0) return dft;
        r = r * 10 + (uint64_t)j;
    }

    if (r >= 0x8000000000000000ULL) return 0;
    if (b_neg) r = 0U - r;
    return (int64_t)r;
}

/* ASC → int, 返回转换值, *result 表示成功/失败 */
int asc2int(const char *p, int *result)
{
    uint8_t  b_neg = 0;
    int      i, j;
    uint32_t r = 0;

    if (*p == '-') {
        p++;
        b_neg = 1;
    }

    for (i = 0; i < 10; i++) {
        uint8_t ch = (uint8_t)*p++;
        if (ch == 0) break;
        j = digchar_byte(ch);
        if (j < 0) {
            *result = ASC2INT_FAIL;
            return (int)r;
        }
        r = r * 10 + (uint32_t)j;
    }

    if (r >= 0x80000000U) {
        *result = ASC2INT_FAIL;
        return 0;
    }

    if (b_neg) r = 0U - r;
    *result = ASC2INT_SUCCESS;
    return (int)r;
}

/* ASC → int64_t, 成功写入 *result 并返回 1, 失败返回 0 */
int asc2s64(int64_t *result, const char *p)
{
    uint8_t  b_neg = 0;
    int      i, j;
    uint64_t r = 0;

    if (*p == '-') {
        p++;
        b_neg = 1;
    }

    for (i = 0; i < 19; i++) {
        uint8_t ch = (uint8_t)*p++;
        if (ch == 0) break;
        j = digchar_byte(ch);
        if (j < 0) {
            *result = 0;
            return ASC2INT_FAIL;
        }
        r = r * 10 + (uint64_t)j;
    }

    if (r >= 0x8000000000000000ULL) {
        *result = 0;
        return ASC2INT_FAIL;
    }

    if (b_neg) r = 0U - r;
    *result = (int64_t)r;
    return ASC2INT_SUCCESS;
}

/*==========================================================================
 *  函数传递表
 *==========================================================================*/

uint32_t Get_Func(FUNC_TB_T *func_tb, const char *func_name)
{
    while (func_tb->func_adr != 0) {
        if (strcmp(func_tb->func_name, func_name) == 0) {
            return func_tb->func_adr;
        }
        func_tb++;
    }
    return 0;
}

/*==========================================================================
 *  通讯缓冲区
 *==========================================================================*/

/* 从缓冲区头部删除 n 个字节, 后续字节前移, 返回剩余字节数 */
int comm_buf_del_n(COMM_BUF_T *p, uint16_t n)
{
    if (n >= p->n) {
        p->n = 0;
    } else {
        uint16_t remain = p->n - n;
        memmove(p->buf, p->buf + n, remain);   /* memmove 可安全处理重叠区域 */
        p->n = remain;
    }
    return (int)p->n;
}

void comm_buf_del_all(COMM_BUF_T *p)
{
    p->n = 0;
}

/*==========================================================================
 *  伪随机数 (LCG)
 *==========================================================================*/

uint32_t MyRnd(void)
{
    g_rnd0 = (g_rnd0 + cnt_1ms) * 1103515245U + 12345U;
    return g_rnd0;
}
