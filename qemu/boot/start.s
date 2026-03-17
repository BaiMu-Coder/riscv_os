# .macro伪指令申明汇编宏， .endm表示汇编宏的结束
#语法  .macro 宏名，参数1，参数2，……
#内部使用参数的：  \参数1     \是宏参数替换语法
.macro loop,cunt
        li  t1, 0xffff
        li  t2, \cunt
1:
        nop
        addi    t1, t1, -1
        #b=backward，1b就是往前找最近的1
        bnez    t1, 1b
        li      t1,  0xffff
        addi    t2, t2, -1
        bnez    t2, 1b
        .endm


#把源地址开始的数据，（4字节）地拷贝到目标地址，直到目标地址到达 dst_end
.macro load_data,_src_start,_dst_start,_dst_end
                                        #f=forward,往下找第一个2的标签
        bgeu        \_dst_start, \_dst_end, 2f
1:
        lw      t0, (\_src_start)
        sw      t0, (\_dst_start)
        addi    \_src_start, \_src_start, 4
        addi    \_dst_start, \_dst_start, 4
        bltu    \_dst_start, \_dst_end, 1b
2:
        .endm




.section .text
.globl _start

_start:
        //load opensbi_fw.bin 
        //[0x20200000:0x20400000] --> [0x80000000:0x80200000]
    li  a0,0x20200000
    li  a1,0x80000000
    li  a2,0x80200000
    load_data a0,a1,a2

        //load qemu_sbi.dtb
        //[0x20080000:0x20200000] --> [0x82200000:0x82380000]
    li  a0,0x20080000
    li  a1,0x82200000
    li  a2,0x82380000
    load_data a0,a1,a2


       //load trusted_fw.bin
       //[0x20400000:0x20800000] --> [0xb0000000:0xb0400000]
    li a0,0x20400000
    li a1,0xb0000000
    li a2,0xb0400000
    load_data a0,a1,a2


    csrr a0, mhartid    
    beqz  a0,_no_wait
    loop  0x1000
_no_wait:
    li   a1, 0x82200000
    li   t0, 0x80000000
    jr   t0

    .end