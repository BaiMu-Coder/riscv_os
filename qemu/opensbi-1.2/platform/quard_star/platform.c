/* OpenSBI 真正的主体是通用的 libsbi.a； 这里的platform.c 只是把 这块板子/机器怎么初始化 告诉 OpenSBI。*/
// 官方文档也是这么定义的：平台支持是通过一个 struct sbi_platform 和一组 platform hooks 提供给通用库使用的；平台代码会先做成 libplatsbi.a，
// 再和通用 libsbi.a、firmware 代码一起链接成最终可启动的固件镜像。

#include <libfdt.h>  //设备树DTB/FDT解析库

//OpenSBI核心层的头文件
#include <sbi/riscv_asm.h>
#include <sbi/sbi_hartmask.h>
#include <sbi/sbi_platform.h>
#include <sbi/sbi_string.h>

//基于设备树的通用平台辅助
#include <sbi_utils/fdt/fdt_domain.h>
#include <sbi_utils/fdt/fdt_fixup.h>
#include <sbi_utils/fdt/fdt_helper.h>
#include <sbi_utils/fdt/fdt_pmu.h>

//具体设备驱动
#include <sbi_utils/irqchip/fdt_irqchip.h>
#include <sbi_utils/serial/fdt_serial.h>
#include <sbi_utils/timer/fdt_timer.h>
#include <sbi_utils/ipi/fdt_ipi.h>
#include <sbi_utils/reset/fdt_reset.h>

//打印使用
#include <sbi/sbi_console.h>


extern struct sbi_platform platform;
static u32 quard_star_hart_index2id[SBI_HARTMASK_MAX_BITS] = { 0 };

/*
第一段：函数的作用与调用时机
fw_platform_init() 函数在 OpenSBI 参考固件的引导核心（boot HART）上会被非常早地调用。
这样设计的目的是让特定平台（platform-specific）的代码，有机会在 platform 实例被系统正式使用之前，对其进行更新或初始化。

第二段：传入的参数
传递给fw_platform_init()函数的参数，是系统启动时硬件寄存器 A0 到 A4 的初始状态。
arg0 代表的是引导核心的 ID（boot HART id）；
arg1 代表的是由上一级引导程序（例如 BootROM）传递过来的 DTB 所在的内存地址。

第三段：返回值
fw_platform_init() 函数的返回值是 （DTB设备树）的最新的内存位置。
如果 DTB 的存放位置没有发生改变（或者你只是在原内存地址上对 FDT 进行了就地修改），那么 fw_platform_init() 总是可以直接把原始的 FDT 地址（也就是参数 arg1）原样返回。
*/


// OpenSBI 一启动，就会立刻调用这个 fw_platform_init()。   
// firmware/fw_base.S这个文件里面硬编码了   call fw_platform_init   所以才会调用这个函数
unsigned long fw_platform_init(unsigned long arg0, unsigned long arg1,unsigned long arg2, unsigned long arg3,unsigned long arg4)
{
	const char *model;
	void *fdt = (void *)arg1;
	u32 hartid, hart_count = 0;
	int rc, root_offset, cpus_offset, cpu_offset, len;

	//根据路径名称，在设备树中找到对应节点的 偏移量  返回给root_offset
	root_offset = fdt_path_offset(fdt, "/");  // 去找根节点 /
	if (root_offset < 0)
		goto fail;

    //精准提取属性，读取节点里面某个具体属性的值 ， 返回读到的数据有多少字节数传给len
	model = fdt_getprop(fdt, root_offset, "model", &len);   //读取model属性
	if (model)
		sbi_strncpy(platform.name, model, sizeof(platform.name));


	cpus_offset = fdt_path_offset(fdt, "/cpus");  
	if (cpus_offset < 0)
		goto fail;

/*
#define fdt_for_each_subnode(node, fdt, parent)		\
	for(node = fdt_first_subnode(fdt, parent); node >= 0; node = fdt_next_subnode(fdt, node))
*/

//遍历每个CPU子节点，解析hartid
//fdt_first_subnode获取设备树parent的第一个子节点       fdt_next_subnode根据当前节点去找下一个紧挨的节点，找不到返回负值
	fdt_for_each_subnode(cpu_offset, fdt, cpus_offset) {
		rc = fdt_parse_hart_id(fdt, cpu_offset, &hartid);  // 读取编号，也就是节点里的reg属性
		if (rc)
			continue;

		if (SBI_HARTMASK_MAX_BITS <= hartid)
			continue;

		quard_star_hart_index2id[hart_count++] = hartid;
	}

	platform.hart_count = hart_count;

	/* Return original FDT pointer */
	return arg1;

fail:
//riscv汇编语法：  wfi 让PU 执行到这里就会停下来，挂起时钟，进入低功耗的休眠状态。
//直到有一个硬件中断到来，CPU 才会被瞬间“惊醒”，然后去处理中断，处理完再继续往下执行。
	while (1)
		wfi();
}





static int quard_star_early_init(bool cold_boot)
{

		return 0;

}


//这是 OpenSBI 在把控制权正式移交给你的操作系统（Kernel）之前的“最后一道工序”。
//核心任务：修补/更新设备树
//cold_boot：在多核 CPU 系统中，开机时通常只有一个被选中的核心（Primary HART）最先醒来干活，这叫冷启动。
// 等它把系统基础环境搭好后，再去唤醒其他核心（Secondary HARTs），其他核心醒来的过程叫热启动。
static int quard_star_final_init(bool cold_boot)
{

	void *fdt;

	if (cold_boot)  
		fdt_reset_init();  //初始化一下系统的复位控制器
	if (!cold_boot)
		return 0;
    //修改设备树这种全局操作，主核做一次就行了


// 上一级引导程序把设备树（FDT）的地址放在了 a1 寄存器里（也就是 arg1）。
// Scratch 机制：OpenSBI 运行期间，为了不丢失这些重要参数，它会为每一个 CPU 核心分配一个叫 scratch（暂存区/小背包）的数据结构。
// 这行代码的意思就是：从当前 CPU 的暂存区里，把那个叫 arg1 的指针拿出来，赋值给 fdt 变量。这样，C 代码就再次拿到了设备树在内存里的首地址！
	fdt = sbi_scratch_thishart_arg1_ptr();


	// 因为上一阶段写的静态 .dts 文件，很多信息在运行时是需要动态更新的。OpenSBI 作为底层固件，比你写的静态文件更了解当前真实的硬件和安全状态。所以要进行修补
	fdt_cpu_fixup(fdt);  //根据 OpenSBI 刚才探测到的真实 CPU 状态，去设备树里修改 CPU 节点的信息（比如更新正确的时钟频率，或者把坏掉的核标记为 disabled）。
	fdt_fixups(fdt);   //通用修补。比如 OpenSBI 自己在内存里占了一块地方运行，它必须在设备树的 reserved-memory 节点里把这块内存划出来，警告你的操作系统：“这块地盘我 OpenSBI 占了，你 Kernel 别乱动！”
	fdt_domain_fixup(fdt); //域修补。OpenSBI 支持安全域（Domain）划分，它会在这里把安全隔离规则写进设备树，告诉 OS 哪些外设可以访问，哪些不能碰。

	return 0;
}

static void quard_star_early_exit(void)
{

}

static void quard_star_final_exit(void)
{

}

static int quard_star_domains_init(void)
{
	return fdt_domains_populate(fdt_get_address());
}

static int quard_star_pmu_init(void)
{

	return fdt_pmu_setup(fdt_get_address());
}



static uint64_t quard_star_pmu_xlate_to_mhpmevent(uint32_t event_idx,
					       uint64_t data)
{
	uint64_t evt_val = 0;

	/* data is valid only for raw events and is equal to event selector */
	if (event_idx == SBI_PMU_EVENT_RAW_IDX)
		evt_val = data;
	else {
		/**
		 * Generic platform follows the SBI specification recommendation
		 * i.e. zero extended event_idx is used as mhpmevent value for
		 * hardware general/cache events if platform does't define one.
		 */
		evt_val = fdt_pmu_get_select_value(event_idx);
		if (!evt_val)
			evt_val = (uint64_t)event_idx;
	}

	return evt_val;
}

static u64 quard_star_tlbr_flush_limit(void)
{
	return SBI_PLATFORM_TLB_RANGE_FLUSH_LIMIT_DEFAULT;
}

//把这些函数挂进OpenSBI
const struct sbi_platform_operations platform_ops = {
	.early_init		= quard_star_early_init,             //早期初始化，不需要
	.final_init		= quard_star_final_init,            //最终初始化，需要
	.early_exit		= quard_star_early_exit,            //早期退出，不需要
	.final_exit		= quard_star_final_exit,            //最终退出，不需要
	.domains_init		= quard_star_domains_init,      //从设备树填充域，需要
	.console_init		= fdt_serial_init,              //初始化控制台
	.irqchip_init		= fdt_irqchip_init,             //初始化中断
	.irqchip_exit		= fdt_irqchip_exit,             //中断退出
	.ipi_init		= fdt_ipi_init,
	.ipi_exit		= fdt_ipi_exit,
	.pmu_init		= quard_star_pmu_init,              //需要
	.pmu_xlate_to_mhpmevent = quard_star_pmu_xlate_to_mhpmevent,
	.get_tlbr_flush_limit	= quard_star_tlbr_flush_limit, //需要
	.timer_init		= fdt_timer_init,
	.timer_exit		= fdt_timer_exit,
};


struct sbi_platform platform = {
	.opensbi_version	= OPENSBI_VERSION,
	.platform_version	= SBI_PLATFORM_VERSION(0x0, 0x01),
	.name			= "Quard-Star",
	.features		= SBI_PLATFORM_DEFAULT_FEATURES,
	.hart_count		= SBI_HARTMASK_MAX_BITS,
	.hart_index2id		= quard_star_hart_index2id,
	.hart_stack_size	= SBI_PLATFORM_DEFAULT_HART_STACK_SIZE,
	.platform_ops_addr	= (unsigned long)&platform_ops
};