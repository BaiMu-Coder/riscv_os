.section .text             
.globl _start              

_start:                       
    csrr    a0, mhartid        //读取mhartid的值   mhartid寄存是定义了内核的hart id，这里读取到a0寄存器里   
    beqz    a0, _core0
_loop:                       
             j  _loop            
_core0:                       
        li                t0,        0x100          //t0 = 0x100
        slli              t0,        t0, 20         //t0逻辑左移20位 t0 = 0x10000000
        li                t1,        'H'            //t1 = 'H' 字符的ASCII码值写入t1
        sb                t1,        0(t0)          //s是store写入的意思，b是byte，这里指的是写入t1
                                                    //的值到t0指向的地址，即为写入0x10000000这个寄存器
                                                    //这个寄存器正是uart0的发送data寄存器，此时串口会输出"H"
        li                t1,        'e'            //接下来都是重复内容
        sb                t1, 0(t0)
        li                t1,        'l'
        sb                t1, 0(t0)
        li                t1,        'l'
        sb                t1, 0(t0)
        li                t1,        'o'
        sb                t1, 0(t0)
        li                t1,        ' '
        sb                t1, 0(t0)
        li                t1,        'Q'
        sb                t1, 0(t0)
        li                t1,        'u'
        sb                t1, 0(t0)
        li                t1,        'a'
        sb                t1, 0(t0)
        li                t1,        'r'
        sb                t1, 0(t0)
        li                t1,        'd'
        sb                t1, 0(t0)
        li                t1,        ' '
        sb                t1, 0(t0)
        li                t1,        'S'
        sb                t1, 0(t0)
        li                t1,        't'
        sb                t1, 0(t0)
        li                t1,        'a'
        sb                t1, 0(t0)
        li                t1,        'r'
        sb                t1, 0(t0)
        li                t1,        ' '
        sb                t1, 0(t0)
        li                t1,        'b'
        sb                t1, 0(t0)
        li                t1,        'o'
        sb                t1, 0(t0)
        li                t1,        'a'
        sb                t1, 0(t0)
        li                t1,        'r'
        sb                t1, 0(t0)
        li                t1,        'd'
        sb                t1, 0(t0)
        li                t1,        '!'
        sb                t1, 0(t0)
        li                t1,        '\n'
        sb                t1, 0(t0)          //到这里就会输出"Hello Quard Star board!"  
        j                _loop               //完成后进入loop

    .end                       //汇编文件结束符号