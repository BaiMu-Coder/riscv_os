OUTPUT_ARCH( "riscv" )  /*输出可执行文件平台*/

ENTRY( _start )         /*程序入口函数*/

MEMORY                  /*定义内存域,供后面的SECTIONS去放内容*/
{ 
    /*定义名为flash的内存域属性以及起始地址，大小等*/
    /*flash是一个名字，不是关键字   (rxai!w)描述属性：这是一块r可读、x可执行、a可分配、i已初始化、!w但不可写的区域*/
    flash (rxai!w) : ORIGIN = 0x20000000, LENGTH = 512k 
}

SECTIONS                /*定义段域*/
{
  .text :               /*.text段域*/
  {
    KEEP(*(.text))      /*将所有.text段链接在此域内，keep是保持防止优化，即无论如何都保留此段*/
  } >flash              /*这里的意思就是 把这个段放到flash这块内存里面*/
}