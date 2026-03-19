#include <stdarg.h>
#include "string.h"
#include "asm/sbi.h"

//----------------------------功能介绍----------------------------------------------
// printk 函数：这是最终用户用来打印日志的接口，格式化并输出日志。
// myprintf 函数：格式化字符串并处理类型转换。
// init_printk_done 函数：初始化打印机制，并设置具体的字符输出函数。
// putchar_fn：实际的字符输出函数（通过串口打印字符）。

#define CONSOLE_PRINT_BUFFER_SIZE 1024
static char print_buf[CONSOLE_PRINT_BUFFER_SIZE];

/* record buffer */
#define CONFIG_LOG_BUF_SHIFT 17
#define LOG_BUF_LEN (1U << CONFIG_LOG_BUF_SHIFT)
static char log_buf[LOG_BUF_LEN];

enum printk_status {
    PRINTK_STATUS_DOWN,
    PRINTK_STATUS_READY,
};

static enum printk_status g_printk_status = PRINTK_STATUS_DOWN;
static char *g_record = log_buf;
static unsigned long g_record_len = 0;

#define ZEROPAD  1   /* pad with zero */
#define SIGN     2   /* unsigned/signed long */
#define PLUS     4   /* show plus */
#define SPACE    8   /* space if plus */
#define LEFT     16  /* left justified */
#define SPECIAL  32  /* 0x */
#define SMALL    64  /* use 'abcdef' instead of 'ABCDEF' */

#define is_digit(c) ((c) >= '0' && (c) <= '9')

#define do_div(n, base) ({                    \
    unsigned int __base = (base);             \
    unsigned int __rem;                       \
    __rem = ((unsigned long)(n)) % __base;    \
    (n) = ((unsigned long)(n)) / __base;      \
    __rem;                                    \
})

static const char *scan_number(const char *string, int *number)
{
    int tmp = 0;

    while (is_digit(*string)) {
        tmp *= 10;
        tmp += *(string++) - '0';
    }

    *number = tmp;
    return string;
}

static inline void buf_putc(char **pos, char *end, char c)
{
    if (*pos < end)
        *(*pos)++ = c;
}

static inline void buf_puts_n(char **pos, char *end, const char *s, int n)
{
    int i;
    for (i = 0; i < n; i++)
        buf_putc(pos, end, s[i]);
}

static char *number(char *str, char *end, unsigned long num, int base,
                    int size, int precision, int type)
{
    char c, sign = 0, tmp[128];
    long snum;
    const char *digits = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    int i;

    if (type & SMALL)
        digits = "0123456789abcdefghijklmnopqrstuvwxyz";
    if (type & LEFT)
        type &= ~ZEROPAD;
    if (base < 2 || base > 36)
        return str;

    c = (type & ZEROPAD) ? '0' : ' ';

    if (type & SIGN) {
        snum = (long)num;
        if (snum < 0) {
            sign = '-';
            num = (unsigned long)(-snum);
        } else if (type & PLUS) {
            sign = '+';
            size--;
        } else if (type & SPACE) {
            sign = ' ';
            size--;
        }
    }

    if (type & SPECIAL) {
        if (base == 16)
            size -= 2;
        else if (base == 8)
            size--;
    }

    i = 0;
    if (num == 0)
        tmp[i++] = '0';
    else
        while (num != 0)
            tmp[i++] = digits[do_div(num, base)];

    if (i > precision)
        precision = i;
    size -= precision;

    if (!(type & (ZEROPAD | LEFT))) {
        while (size-- > 0)
            buf_putc(&str, end, ' ');
    }

    if (sign)
        buf_putc(&str, end, sign);

    if (type & SPECIAL) {
        if (base == 8) {
            buf_putc(&str, end, '0');
        } else if (base == 16) {
            buf_putc(&str, end, '0');
            buf_putc(&str, end, digits[33]); /* 'x' or 'X' */
        }
    }

    if (!(type & LEFT)) {
        while (size-- > 0)
            buf_putc(&str, end, c);
    }

    while (i < precision--)
        buf_putc(&str, end, '0');

    while (i-- > 0)
        buf_putc(&str, end, tmp[i]);

    while (size-- > 0)
        buf_putc(&str, end, ' ');

    return str;
}

/*
 * myprintf 函数根据 fmt 中的格式化字符（例如 %d, %s 等）来决定如何处理后续的参数。它通过 scan_number 和 number 辅助函数将数据转换为字符串，并将其写入 string 缓冲区。
 *
 * 1. flags:
 *  - : 左对齐
 *  + : 加号或减号
 *  # : specifier 是 o、x、X 时，增加前缀 0 / 0x
 *  0 : 使用 0 填充字段宽度
 *
 * 2. 最小宽度 width
 * 3. 类型长度
 *  h : short, short int
 *  l : long, unsigned long, long int
 *  ll: long long, unsigned long long
 */
int myprintf(char *string, unsigned int size, const char *fmt, va_list arg)
{
    char *pos;
    char *end;
    int flags;
    int field_width;
    int precision;
    int qualifier;
    char *ip;
    char *s;
    int len;
    unsigned long num;
    int base;

    if (!string || size == 0)
        return 0;

    pos = string;
    end = string + size - 1; /* 留一个字节给 '\0' */

    for (; *fmt; fmt++) {
        if (*fmt != '%') {
            buf_putc(&pos, end, *fmt);
            continue;
        }

        /* process flags */
        flags = 0;
repeat:
        ++fmt; /* skip first % */
        switch (*fmt) {
        case '-':
            flags |= LEFT;
            goto repeat;
        case '+':
            flags |= PLUS;
            goto repeat;
        case ' ':
            flags |= SPACE;
            goto repeat;
        case '#':
            flags |= SPECIAL;
            goto repeat;
        case '0':
            flags |= ZEROPAD;
            goto repeat;
        default:
            break;
        }

        /* width */
        field_width = -1;
        if (is_digit(*fmt)) {
            fmt = scan_number(fmt, &field_width);
        } else if (*fmt == '*') {
            field_width = va_arg(arg, int);
            ++fmt;
            if (field_width < 0) {
                field_width = -field_width;
                flags |= LEFT;
            }
        }

        /* precision */
        precision = -1;
        if (*fmt == '.') {
            ++fmt;
            if (is_digit(*fmt))
                fmt = scan_number(fmt, &precision);
            else if (*fmt == '*') {
                precision = va_arg(arg, int);
                ++fmt;
            }
            if (precision < 0)
                precision = 0;
        }

        /* qualifier */
        qualifier = -1;
        if (*fmt == 'h' || *fmt == 'l' || *fmt == 'L') {
            qualifier = *fmt;
            ++fmt;

            if (qualifier == 'l' && *fmt == 'l') {
                qualifier = 'L';
                ++fmt;
            }
        }

        switch (*fmt) {
        case 'c':
            if (!(flags & LEFT))
                while (--field_width > 0)
                    buf_putc(&pos, end, ' ');
            buf_putc(&pos, end, (unsigned char)va_arg(arg, int));
            while (--field_width > 0)
                buf_putc(&pos, end, ' ');
            continue;

        case 's':
            s = va_arg(arg, char *);
            if (!s)
                s = "<NULL>";
            len = strlen(s);
            if (precision < 0)
                precision = len;
            else if (len > precision)
                len = precision;

            if (!(flags & LEFT))
                while (len < field_width--)
                    buf_putc(&pos, end, ' ');
            buf_puts_n(&pos, end, s, len);
            while (len < field_width--)
                buf_putc(&pos, end, ' ');
            continue;

        case 'n':
            ip = (char *)va_arg(arg, int *);
            if (ip)
                *ip = (char)(pos - string);
            continue;

        case 'p':
            if (field_width == -1) {
                field_width = 2 * (int)sizeof(void *);
                flags |= ZEROPAD;
            }
            pos = number(pos, end,
                         (unsigned long)va_arg(arg, void *),
                         16, field_width, precision, flags);
            continue;

        case 'o':
            base = 8;
            break;

        case 'x':
            flags |= SMALL;
            /* fallthrough */
        case 'X':
            base = 16;
            break;

        case 'd':
        case 'i':
            flags |= SIGN;
            /* fallthrough */
        case 'u':
            base = 10;
            break;

        case '%':
            buf_putc(&pos, end, '%');
            continue;

        default:
            buf_putc(&pos, end, '%');
            if (*fmt)
                buf_putc(&pos, end, *fmt);
            else
                --fmt;
            continue;
        }

        if (qualifier == 'L') {
            /* 这里仍然按 unsigned long / long 处理，最小修补，不展开 long long */
            if (flags & SIGN)
                num = (unsigned long)va_arg(arg, long);
            else
                num = (unsigned long)va_arg(arg, unsigned long);
        } else if (qualifier == 'h') {
            if (flags & SIGN)
                num = (unsigned long)(short)va_arg(arg, int);
            else
                num = (unsigned long)(unsigned short)va_arg(arg, unsigned int);
        } else if (qualifier == 'l') {
            if (flags & SIGN)
                num = (unsigned long)va_arg(arg, long);
            else
                num = (unsigned long)va_arg(arg, unsigned long);
        } else {
            if (flags & SIGN)
                num = (unsigned long)(int)va_arg(arg, int);
            else
                num = (unsigned long)va_arg(arg, unsigned int);
        }

        pos = number(pos, end, num, base, field_width, precision, flags);
    }

    *pos = '\0';
    return (int)(pos - string);
}

static void (*putchar_fn)(char c) = 0;

/*
 * 设置打印函数（putchar_fn）
 * 将日志缓冲区中的内容逐个字符地通过该打印函数输出，
 * 最后重置日志缓冲区。
 */
void init_printk_done(void (*fn)(char c))
{
    unsigned long i;

    if (!fn)
        return;

    putchar_fn = fn;
    g_printk_status = PRINTK_STATUS_READY;

    for (i = 0; i < g_record_len; i++)
        putchar_fn(log_buf[i]);

    g_record = log_buf;
    g_record_len = 0;
}

//printk 是用户调用的主接口，它实际上调用了 myprintf 来进行格式化，然后将结果写入日志缓冲区或直接通过 putchar_fn 输出。
int printk(const char *fmt, ...)
{
    va_list arg;
    int len;
    unsigned long remain;
    int i;

    va_start(arg, fmt);
    len = myprintf(print_buf, sizeof(print_buf), fmt, arg);
    va_end(arg);

    if (len <= 0)
        return len;

    /* 记录到日志缓冲区 */
    if (g_printk_status == PRINTK_STATUS_DOWN || !putchar_fn) {
        if (g_record_len >= LOG_BUF_LEN)
            return 0;

        remain = LOG_BUF_LEN - g_record_len;
        if ((unsigned long)len > remain)
            len = (int)remain;

        memcpy(g_record, print_buf, (unsigned long)len);
        g_record += len;
        g_record_len += (unsigned long)len;
        return len;
    }

    for (i = 0; i < len; i++)
        putchar_fn(print_buf[i]);

    return len;
}