	 /*
	  * 设置sbi的加载入口地址为0x80200000
	  */
OUTPUT_ARCH(riscv)
ENTRY(_start)

MEMORY
{
    /*(rxai!w)描述属性：这是一块r可读、x可执行、a可分配、i已初始化、!w但不可写的区域*/
    /*这里严格带空格，不会语法会报错*/
      ram (rwx) : ORIGIN = 0x80200000, LENGTH = 128M
}

SECTIONS
{
    . = ORIGIN(ram);

    .text  : {
        KEEP(*(.text.boot))
        *(.text .text.*)
    } >ram

    .rodata  :  {
        *(.rodata .rodata.*)
    } >ram

    .data ALIGN(4K) :  {
        __data_start = .;
        *(.sdata .sdata.*)
        *(.data .data.*)
        __data_end = .;
    } >ram

    .bss  : {
        __bss_start = .;
        *(.sbss .sbss.*)
        *(.bss .bss.*)
        *(COMMON)
        __bss_end = .;
    } >ram

    _end = .;
}

	

	
