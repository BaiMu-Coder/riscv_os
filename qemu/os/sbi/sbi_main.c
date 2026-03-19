#include "asm/sbi.h"
#include "printk.h"

int sbi_main()
{
  
sbi_put_string("wellcome baimu os!!!!!\n");

init_printk_done(sbi_putchar);

printk("xxxxxxxxxx%d %s\n",5,"yyyyyyyyyyyyy");

while(1);

    return 0;
}