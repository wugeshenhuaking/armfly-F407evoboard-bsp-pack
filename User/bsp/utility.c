/*==========================================================================
 *  utility.c — General-purpose utility function library
 *==========================================================================*/
#include "utility.h"
#include <string.h>

/*==========================================================================
 *  Global Variable Definitions
 *==========================================================================*/
data_pool_t g_data_pool = { DATA_POOL_SIZE, 0, {0} };

volatile uint32_t cnt_1ms       __attribute__((section(".ARM.__at_0x20000000")));
volatile uint32_t cnt_1ms_H     __attribute__((section(".ARM.__at_0x20000004")));
uint32_t          delay_k       __attribute__((section(".ARM.__at_0x20000008")));
data_pool_t      *g_p_data_pool __attribute__((section(".ARM.__at_0x2000000C"))) = &g_data_pool;
uint32_t          g_rnd0;
uint8_t           reserved[12]  __attribute__((section(".ARM.__at_0x20000014")));

/*==========================================================================
 *  Byte-Order / Buffer Operations
 *==========================================================================*/
uint16_t char_hl_short(uint8_t hi, uint8_t lo)
{
    return (uint16_t)((uint16_t)hi << 8) | lo;
}

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

uint8_t *short_wr_buf(uint8_t *buf, uint16_t s)
{
    *buf++ = (uint8_t)(s);
    *buf++ = (uint8_t)(s >> 8);
    return buf;
}

uint16_t short_rd_buf(const uint8_t *buf)
{
    return (uint16_t)buf[0] | ((uint16_t)buf[1] << 8);
}

uint8_t *short_wr_buf_xch(uint8_t *buf, uint16_t s)
{
    *buf++ = (uint8_t)(s >> 8);
    *buf++ = (uint8_t)(s);
    return buf;
}

uint16_t short_rd_buf_xch(const uint8_t *buf)
{
    return ((uint16_t)buf[0] << 8) | (uint16_t)buf[1];
}

/*==========================================================================
 *  IAP Jump
 *==========================================================================*/
void jump_to_app(uint32_t ApplicationAddress)
{
    function_t Jump_To_Application;
    Jump_To_Application = (function_t)(*(volatile uint32_t *)(ApplicationAddress + 4));

#if defined(__ARMCC_VERSION) && (__ARMCC_VERSION < 6000000)
    __asm("MSR MSP, *(volatile unsigned int*) ApplicationAddress");
#elif defined(__clang__) || defined(__GNUC__)
    {
        volatile uint32_t app_msp = *(volatile uint32_t *)ApplicationAddress;
        __asm volatile (
            "MSR MSP, %0"
            :
            : "r"(app_msp)
            : "memory"
        );
    }
#else
    #error "Unsupported compiler — jump_to_app requires inline assembly to set MSP"
#endif

    Jump_To_Application();
}

/*==========================================================================
 *  Delay
 *==========================================================================*/
void delay_us_init(uint32_t sys_freq_hz)
{
    delay_k = sys_freq_hz / 3000000;
    if (delay_k == 0) {
        delay_k = 1;
    }
}

void delay_us(uint32_t us)
{
    uint32_t i;
    us *= delay_k;
    if (us > 3) {
        us -= 3;
    }
    for (i = 0; i < us; i++) {
        __asm volatile("nop");
    }
}

/*==========================================================================
 *  Bit-Band Operations
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
 *  Millisecond Timer
 *==========================================================================*/
void left_ms_set(time_ms_t *p, uint32_t val)
{
    *p = cnt_1ms + val;
    if (*p == 0) {
        *p = 1;
    }
}

int32_t left_ms_sta(const time_ms_t *p)
{
    uint32_t diff;

    if (*p == 0) {
        return 0;
    }

    diff = *p - cnt_1ms;

    if (diff & 0x80000000U) {
        return 0;
    }

    return (int32_t)diff;
}

int32_t left_ms(time_ms_t *p)
{
    uint32_t diff;

    if (*p == 0) {
        return 0;
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
 *  Simple JSON Parser
 *==========================================================================*/
int get_json_1fs(char *buf, int buf_size, const char *s, const char *name)
{
    const char *p0, *p1, *pe;
    int n, name_len;

    n = (int)strlen(s);
    if (n < 7 || s[0] != '{' || s[n - 1] != '}') {
        return GET_JSON_1FS_FAIL;
    }
    pe = s + n - 1;

    name_len = (int)strlen(name);
    p0 = s;
    while ((p0 = strstr(p0, name)) != NULL) {
        if (p0 > s && *(p0 - 1) == '"' && *(p0 + name_len) == '"') {
            const char *q = p0 - 2;
            while (q > s && *q == ' ') q--;
            if (*q == '{' || *q == ',') {
                break;
            }
        }
        p0++;
    }
    if (p0 == NULL) {
        return GET_JSON_1FS_FAIL;
    }

    p0 += name_len;
    if (*p0 != '"') return GET_JSON_1FS_FAIL;
    p0++;
    if (*p0 != ':') return GET_JSON_1FS_FAIL;
    p0++;

    while (*p0 == ' ') {
        p0++;
    }

    if (*p0 == '"') {
        p0++;
        p1 = strchr(p0, '"');
        if (p1 == NULL) return GET_JSON_1FS_FAIL;
        n = (int)(p1 - p0);
        p1++;
        if (*p1 != ',' && *p1 != '}') return GET_JSON_1FS_FAIL;
    } else {
        p1 = strchr(p0, ',');
        if (p1 == NULL) {
            p1 = pe;
        }
        n = (int)(p1 - p0);
    }

    if (n >= buf_size) {
        return GET_JSON_1FS_FAIL;
    }

    memcpy(buf, p0, (uint32_t)n);
    buf[n] = '\0';
    return n;
}

/*==========================================================================
 *  HEX <-> String Conversion
 *==========================================================================*/
int hexchar_byte(uint8_t ch)
{
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}

int str_to_hex(uint8_t *buf, uint32_t buf_size, const char *s)
{
    uint32_t i, n;
    int hi, lo;

    n = (uint32_t)strlen(s);
    if (n & 1U) return STR_TO_HEX_FAIL;
    n /= 2;
    if (n > buf_size) return STR_TO_HEX_FAIL;

    for (i = 0; i < n; i++) {
        hi = hexchar_byte((uint8_t)*s++);
        lo = hexchar_byte((uint8_t)*s++);
        if (hi < 0 || lo < 0) return STR_TO_HEX_FAIL;
        *buf++ = (uint8_t)((hi << 4) | lo);
    }
    return (int)n;
}

int nstr_to_hex(uint8_t *buf, const char *s, uint32_t n)
{
    uint32_t i;
    int hi, lo;

    if (n & 1U) return STR_TO_HEX_FAIL;
    n /= 2;

    for (i = 0; i < n; i++) {
        hi = hexchar_byte((uint8_t)*s++);
        lo = hexchar_byte((uint8_t)*s++);
        if (hi < 0 || lo < 0) return STR_TO_HEX_FAIL;
        *buf++ = (uint8_t)((hi << 4) | lo);
    }
    return (int)n;
}

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
 *  String Utilities
 *==========================================================================*/
void str2lwr(char *p)
{
    while (*p) {
        if (*p >= 'A' && *p <= 'Z') *p += ('a' - 'A');
        p++;
    }
}

void str2upr(char *p)
{
    while (*p) {
        if (*p >= 'a' && *p <= 'z') *p -= ('a' - 'A');
        p++;
    }
}

/*==========================================================================
 *  Calendar / Time
 *==========================================================================*/
static const uint8_t tb_month_days[] = {
    0,
    31, 28, 31, 30, 31, 30,
    31, 31, 30, 31, 30, 31
};

#define TIME_DATA_YEAR_ORG  1970
#define SECONDS_DAY         (3600UL * 24)

int calendar_int(calendar_t *p)
{
    uint32_t i, j, k, days;
    uint8_t  b_leap;

    i = p->year;
    if (i < TIME_DATA_YEAR_ORG) return CALENDAR_FAIL;

    b_leap = (((i % 400) == 0) || ((i % 4 == 0) && (i % 100 != 0))) ? 1 : 0;
    j = i - TIME_DATA_YEAR_ORG;

    days = 365 * j
         + (1969 + j) / 4
         - (1969 + j) / 100
         + (1969 + j) / 400
         - 477;

    i = p->month;
    if (i < 1 || i > 12) return CALENDAR_FAIL;

    k = 0;
    for (j = 1; j < i; j++) k += tb_month_days[j];
    if (i >= 3) k += b_leap;
    days += k;

    i = p->day;
    if (i < 1 || i > ((uint32_t)tb_month_days[p->month] + b_leap))
        return CALENDAR_FAIL;
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

void int_calendar(calendar_t *p)
{
    uint32_t i, j, k, days;
    uint8_t  b_leap;

    i = p->sec1970;
    days = i / SECONDS_DAY;
    p->week = (uint8_t)(((days + 3) % 7) + 1);

    {
        uint32_t lo = 0, hi = 800;
        while (lo < hi) {
            uint32_t mid = (lo + hi + 1) / 2;
            uint32_t d = 365 * mid
                       + (1969 + mid) / 4
                       - (1969 + mid) / 100
                       + (1969 + mid) / 400
                       - 477;
            if (d <= days) lo = mid;
            else           hi = mid - 1;
        }
        j = lo;
    }

    k = 1970 + j;
    p->year = (uint16_t)k;

    days -= (365 * j
           + (1969 + j) / 4
           - (1969 + j) / 100
           + (1969 + j) / 400
           - 477);

    b_leap = (((k % 400) == 0) || ((k % 4 == 0) && (k % 100 != 0))) ? 1 : 0;

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
    p->min = (uint8_t)(i / 60);
    p->sec = (uint8_t)(i % 60);
}

/*==========================================================================
 *  Numeric Formatting
 *==========================================================================*/
int dword_to_str(char *s, uint32_t data, uint8_t i_nb, uint8_t dec_nb,
                 uint8_t b_sign, uint8_t b_inv0)
{
    int   i, j, char_nb;
    char *p;
    uint8_t b_s = 0;

    if (!i_nb) i_nb++;
    char_nb = i_nb;
    if (dec_nb) char_nb += 1 + dec_nb;

    if (b_sign) {
        if ((int32_t)data < 0) {
            data = 0U - data;
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

    if (b_sign) *p = ' ';

    p++;
    if (!b_inv0 && i_nb) {
        for (i = 0; i < i_nb - 1; i++) {
            if (*p == '0') *p = ' ';
            else break;
            p++;
        }
    }

    if (b_sign) {
        p--;
        *p = b_s ? '-' : ' ';
    }

    return char_nb + b_sign;
}

void short_bin_str(char *buf, uint16_t us)
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
 *  Data Pool
 *==========================================================================*/
uint8_t *data_pool_get(uint32_t size)
{
    uint8_t  *r = NULL;
    uint32_t  aligned_used;

    aligned_used = (g_p_data_pool->used_size + 3U) & ~3U;

    if (size > (g_p_data_pool->size - aligned_used)) {
        while (1);  /* Pool exhausted — halt */
    }

    r = g_p_data_pool->buf + aligned_used;
    g_p_data_pool->used_size = aligned_used + size;
    return r;
}

void data_pool_reset(void)
{
    g_p_data_pool->used_size = 0;
}

/*==========================================================================
 *  ASC <-> Integer Conversion
 *==========================================================================*/
int digchar_byte(uint8_t ch)
{
    if (ch >= '0' && ch <= '9') return ch - '0';
    return -1;
}

int asc2int_dft(const char *p, int dft)
{
    uint8_t  b_neg = 0;
    int      i, j;
    uint32_t r = 0;
    uint8_t  b_has_digit = 0;

    if (*p == '-') { p++; b_neg = 1; }

    for (i = 0; i < 10; i++) {
        uint8_t ch = (uint8_t)*p++;
        if (ch == 0) break;
        j = digchar_byte(ch);
        if (j < 0) return dft;
        r = r * 10 + (uint32_t)j;
        b_has_digit = 1;
    }

    if (!b_has_digit) return dft;
    if (r >= 0x80000000U) return 0;
    if (b_neg) r = 0U - r;
    return (int)r;
}

int64_t asc2s64_dft(const char *p, int64_t dft)
{
    uint8_t  b_neg = 0;
    int      i, j;
    uint64_t r = 0;
    uint8_t  b_has_digit = 0;

    if (*p == '-') { p++; b_neg = 1; }

    for (i = 0; i < 19; i++) {
        uint8_t ch = (uint8_t)*p++;
        if (ch == 0) break;
        j = digchar_byte(ch);
        if (j < 0) return dft;
        r = r * 10 + (uint64_t)j;
        b_has_digit = 1;
    }

    if (!b_has_digit) return dft;
    if (r >= 0x8000000000000000ULL) return 0;
    if (b_neg) r = 0U - r;
    return (int64_t)r;
}

int asc2int(const char *p, int *result)
{
    uint8_t  b_neg = 0;
    int      i, j;
    uint32_t r = 0;

    if (*p == '-') { p++; b_neg = 1; }

    for (i = 0; i < 10; i++) {
        uint8_t ch = (uint8_t)*p++;
        if (ch == 0) break;
        j = digchar_byte(ch);
        if (j < 0) { *result = ASC2INT_FAIL; return (int)r; }
        r = r * 10 + (uint32_t)j;
    }

    if (r >= 0x80000000U) { *result = ASC2INT_FAIL; return 0; }

    if (b_neg) r = 0U - r;
    *result = ASC2INT_SUCCESS;
    return (int)r;
}

int asc2s64(int64_t *result, const char *p)
{
    uint8_t  b_neg = 0;
    int      i, j;
    uint64_t r = 0;

    if (*p == '-') { p++; b_neg = 1; }

    for (i = 0; i < 19; i++) {
        uint8_t ch = (uint8_t)*p++;
        if (ch == 0) break;
        j = digchar_byte(ch);
        if (j < 0) { *result = 0; return ASC2INT_FAIL; }
        r = r * 10 + (uint64_t)j;
    }

    if (r >= 0x8000000000000000ULL) { *result = 0; return ASC2INT_FAIL; }

    if (b_neg) r = 0U - r;
    *result = (int64_t)r;
    return ASC2INT_SUCCESS;
}

/*==========================================================================
 *  Function Dispatch Table
 *==========================================================================*/
uint32_t get_func(func_tb_t *func_tb, const char *func_name)
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
 *  Communication Buffer
 *==========================================================================*/
int comm_buf_del_n(comm_buf_t *p, uint16_t n)
{
    if (n >= p->n) {
        p->n = 0;
    } else {
        uint16_t remain = p->n - n;
        memmove(p->buf, p->buf + n, remain);
        p->n = remain;
    }
    return (int)p->n;
}

void comm_buf_del_all(comm_buf_t *p)
{
    p->n = 0;
}

/*==========================================================================
 *  Random Number
 *==========================================================================*/
uint32_t my_rnd(void)
{
    g_rnd0 = (g_rnd0 + cnt_1ms) * 1103515245U + 12345U;
    return g_rnd0;
}
