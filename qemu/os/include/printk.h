#ifndef F2075C70_1F1C_4A44_B800_E003919BEA6B
#define F2075C70_1F1C_4A44_B800_E003919BEA6B

void init_printk_done(void (*fn)(char c));  //初始化打印函数
int printk(const char *fmt, ...);


#endif /* F2075C70_1F1C_4A44_B800_E003919BEA6B */
