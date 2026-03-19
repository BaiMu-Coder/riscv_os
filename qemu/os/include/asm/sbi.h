#pragma once

enum sbi_ext_id {
        SBI_SET_TIMER = 0x0,                //允许设置一个定时器
        SBI_CONSOLE_PUTCHAR = 0x1,          //允许通过SBI向控制台输出字符
        SBI_CONSOLE_GETCHAR = 0x2,          //允许通过SBI从控制台获取字符
        SBI_CLEAR_IPI = 0x3,
        SBI_SEND_IPI = 0x4,
        SBI_REMOTE_FENCE_I = 0x5,
        SBI_REMOTE_SFENCE_VMA = 0x6,
        SBI_REMOTE_SFENCE_VMA_ASID = 0x7,
        SBI_SHUTDOWN = 0x8,
        SBI_BASE = 0x10,
        SBI_TIME = 0x54494D45,
        SBI_IPI = 0x735049,
        SBI_RFENCE = 0x52464E43,
        SBI_HSM = 0x48534D,
        SBI_SRST = 0x53525354,
        SBI_PMU = 0x504D55,
};

/* sbi 返回结构体*/
struct sbiret {
        long error;
        long value;
};





// 这个宏 SBI_CALL 是用来执行一个 ecall（环境调用），即让当前的程序陷入更高特权级别（如从用户模式进入内核模式，或者从内核模式进入机器模式）并传递一些参数。
// 使用汇编语法将函数参数传递给寄存器 a0、a1、a2 和 a7，这四个寄存器分别用于传递参数和系统调用的编号。   
// 语法： asm ("a0")的作用是 表示告诉编译器该变量 a0 应该与 RISC-V 汇编指令中的寄存器 a0 进行关联。

// a7 用来保存系统调用的编号（which），它在这里代表不同的 SBI 服务。
// asm volatile ("ecall") 执行一个 ecall 指令，触发从当前模式到机器模式的切换。
// 通过 ecall 后，返回的值（通常存放在 a0 中）会作为宏的结果返回。
#define SBI_CALL(which, arg0, arg1, arg2) ({			\
	register unsigned long a0 asm ("a0") = (unsigned long)(arg0);	\
	register unsigned long a1 asm ("a1") = (unsigned long)(arg1);	\
	register unsigned long a2 asm ("a2") = (unsigned long)(arg2);	\
	register unsigned long a7 asm ("a7") = (unsigned long)(which);	\
	asm volatile ("ecall"					\
		      : "+r" (a0)				\
		      : "r" (a1), "r" (a2), "r" (a7)		\
		      : "memory");				\
	a0;							\
})

// ecall 指令的参数：
// a7 寄存器：a7 用来存储系统调用的编号，决定了要执行的系统调用的种类（例如设置定时器、输出字符等）。
// a0、a1、a2：这些寄存器用于传递实际的参数给系统调用。例如，a0 存储第一个参数，a1 存储第二个参数，依此类推。
// 返回值：ecall 执行后，返回的值（如系统调用的执行结果）会存储在 a0 寄存器中。



/* 
 * 陷入到M模式，调用M模式提供的服务。
 * SBI运行到M模式下
 */
#define SBI_CALL_0(which) SBI_CALL(which, 0, 0, 0)
#define SBI_CALL_1(which, arg0) SBI_CALL(which, arg0, 0, 0)
#define SBI_CALL_2(which, arg0, arg1) SBI_CALL(which, arg0, arg1, 0)

static inline void sbi_set_timer(unsigned long stime_value)
{
	SBI_CALL_1(SBI_SET_TIMER, stime_value);
}

static inline void sbi_putchar(const char c)
{
	SBI_CALL_1(SBI_CONSOLE_PUTCHAR, c);
}

static inline void sbi_put_string(const char *str)
{
	int i; 
	for (i = 0; str[i] != '\0'; i++)
		sbi_putchar((char) str[i]);
}


