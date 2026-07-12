/*==========================================================================
 *  utility.h — General-purpose utility function library
 *==========================================================================*/
#ifndef __UTILITY_H__
#define __UTILITY_H__

#include <stdint.h>

/*==========================================================================
 *  Constants
 *==========================================================================*/
#ifndef PI
#define PI              3.1415926535
#endif
#define DATA_POOL_SIZE  8192

/*==========================================================================
 *  Data Pool
 *==========================================================================*/
typedef struct {
    uint32_t size;
    uint32_t used_size;
    uint8_t  buf[DATA_POOL_SIZE];
} data_pool_t;

/*==========================================================================
 *  Global Variables (defined in utility.c)
 *==========================================================================*/
extern data_pool_t       g_data_pool;
extern volatile uint32_t cnt_1ms;
extern volatile uint32_t cnt_1ms_H;
extern uint32_t          delay_k;
extern data_pool_t      *g_p_data_pool;
extern uint32_t          g_rnd0;
extern uint8_t           reserved[12];

/*==========================================================================
 *  Hardware Register Access Macros (STM32 Bit-Band Aliases)
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

typedef volatile uint32_t bitband_t;

/*==========================================================================
 *  GPIO Helper Macros (requires chip-specific GPIO header)
 *==========================================================================*/
#define TstB(A, B)  ((A & (1 << (B))) != 0)
#define SetGB(A)    GPIO_SetBits(A)
#define ClrGB(A)    GPIO_ResetBits(A)
#define TogGB(A)    GPIO_WriteBit(A, (BitAction)(1 - GPIO_ReadInputDataBit(A)))
#define RdGB(A)     GPIO_ReadInputDataBit(A)
#define WrGB(A, B)  GPIO_WriteBit(A, B)

typedef void (*gpio_out_t)(uint8_t pinsta);

/*==========================================================================
 *  Byte-Order / Buffer Operations
 *==========================================================================*/
uint16_t  char_hl_short(uint8_t hi, uint8_t lo);
void      short_copy_xch(void *t, const void *s, int n, uint8_t b_xch);
uint8_t  *short_wr_buf(uint8_t *buf, uint16_t s);
uint16_t  short_rd_buf(const uint8_t *buf);
uint8_t  *short_wr_buf_xch(uint8_t *buf, uint16_t s);
uint16_t  short_rd_buf_xch(const uint8_t *buf);

/*==========================================================================
 *  IAP Jump
 *==========================================================================*/
typedef void (*function_t)(void);
void jump_to_app(uint32_t ApplicationAddress);

#define m_jump_to_app_disallint(A) do { \
    __disable_irq();                    \
    NVIC->ICER[0] = 0xFFFFFFFF;        \
    NVIC->ICER[1] = 0xFFFFFFFF;        \
    NVIC->ICER[2] = 0xFFFFFFFF;        \
    jump_to_app(A);                     \
} while(0)

/*==========================================================================
 *  Delay
 *==========================================================================*/
void delay_us_init(uint32_t sys_freq_hz);
void delay_us(uint32_t us);

/*==========================================================================
 *  Bit-Band Operations (Cortex-M3/M4)
 *==========================================================================*/
uint32_t *calc_bitadr(void *x, uint8_t b);
void      bitband_set(void *p, uint8_t b);
void      bitband_clr(void *p, uint8_t b);
void      bitband_toggle(void *p, uint8_t b);
uint8_t   bitband_tsc(void *p, uint8_t b);

/*==========================================================================
 *  Convenience Macros
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
 *  Assembly Bit Operations (STM32F103_HW_ASM.S, for Cortex-M0 without bit-band)
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
 *  Millisecond Timer
 *  Value 0  => not running; nonzero => absolute cnt_1ms expiry time
 *  Maximum duration: 2^31 ms ≈ 24.8 days
 *==========================================================================*/
typedef uint32_t time_ms_t;

void    left_ms_set(time_ms_t *p, uint32_t val);
int32_t left_ms_sta(const time_ms_t *p);
int32_t left_ms(time_ms_t *p);
void    left_ms_stop(time_ms_t *p);
int32_t left_ms_running(const time_ms_t *p);

/*==========================================================================
 *  Simple JSON Parser
 *==========================================================================*/
#define GET_JSON_1FS_FAIL   0
int get_json_1fs(char *buf, int buf_size, const char *s, const char *name);

/*==========================================================================
 *  HEX <-> String Conversion
 *==========================================================================*/
#define STR_TO_HEX_FAIL     (-1)
#define STR_TO_HEX_SUCCESS  1

int      hexchar_byte(uint8_t ch);
int      str_to_hex(uint8_t *buf, uint32_t buf_size, const char *s);
int      nstr_to_hex(uint8_t *buf, const char *s, uint32_t n);
void     hex_to_str(char *str_buf, const uint8_t *hex_buf, uint32_t hex_n);

/*==========================================================================
 *  String Utilities
 *==========================================================================*/
void str2lwr(char *p);
void str2upr(char *p);

/*==========================================================================
 *  Calendar / Time
 *==========================================================================*/
typedef struct {
    uint16_t year;
    uint8_t  month;
    uint8_t  day;
    uint8_t  hour;
    uint8_t  min;
    uint8_t  sec;
    uint8_t  week;
    uint32_t sec1970;
} calendar_t;

#define CALENDAR_SUCCESS    0
#define CALENDAR_FAIL      (-1)

int  calendar_int(calendar_t *p);
void int_calendar(calendar_t *p);

/*==========================================================================
 *  Numeric Formatting
 *==========================================================================*/
int  dword_to_str(char *s, uint32_t data, uint8_t i_nb, uint8_t dec_nb,
                  uint8_t b_sign, uint8_t b_inv0);
void short_bin_str(char *buf, uint16_t us);

/*==========================================================================
 *  Data Pool Allocation
 *==========================================================================*/
uint8_t *data_pool_get(uint32_t size);
void     data_pool_reset(void);

/*==========================================================================
 *  ASC <-> Integer Conversion
 *==========================================================================*/
#define ASC2INT_SUCCESS 1
#define ASC2INT_FAIL    0

int      digchar_byte(uint8_t ch);
int      asc2int_dft(const char *p, int dft);
int64_t  asc2s64_dft(const char *p, int64_t dft);
int      asc2int(const char *p, int *result);
int      asc2s64(int64_t *result, const char *p);

/*==========================================================================
 *  Function Dispatch Table
 *==========================================================================*/
typedef struct {
    const char *func_name;
    uint32_t    func_adr;
} func_tb_t;

uint32_t get_func(func_tb_t *func_tb, const char *func_name);

/*==========================================================================
 *  Communication Buffer
 *==========================================================================*/
typedef struct {
    uint8_t  *buf;
    uint16_t  n;
    uint16_t  size;
} comm_buf_t;

int  comm_buf_del_n(comm_buf_t *p, uint16_t n);
void comm_buf_del_all(comm_buf_t *p);

/*==========================================================================
 *  Random Number
 *==========================================================================*/
uint32_t my_rnd(void);

#endif /* __UTILITY_H__ */
