#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qemu/error-report.h"
#include "qemu/guest-random.h"
#include "qapi/error.h"
#include "hw/boards.h"
#include "hw/loader.h"
#include "hw/sysbus.h"
#include "hw/qdev-properties.h"
#include "hw/char/serial.h"
#include "target/riscv/cpu.h"

#include "hw/riscv/riscv_hart.h"
#include "hw/riscv/quard_star.h"
#include "hw/riscv/boot.h"
#include "hw/riscv/numa.h"
#include "hw/intc/riscv_aclint.h"
#include "hw/intc/riscv_aplic.h"
#include "hw/intc/riscv_imsic.h"
#include "hw/platform-bus.h"
#include "hw/intc/sifive_plic.h"
#include "hw/misc/sifive_test.h"

#include "chardev/char.h"
#include "sysemu/device_tree.h"
#include "sysemu/sysemu.h"
#include "sysemu/kvm.h"
#include "sysemu/tpm.h"
#include "hw/pci/pci.h"
#include "hw/pci-host/gpex.h"
#include "hw/display/ramfb.h"
#include "hw/acpi/aml-build.h"
#include "qapi/qapi-visit-common.h"


//指定初始化器  [索引]=值
static const MemMapEntry quard_star_memmap[] = {
    [QUARD_STAR_MROM]  = {        0x0,        0x8000 },
    [QUARD_STAR_SRAM]  = {     0x8000,        0x8000 },
    [QUARD_STAR_CLINT] = { 0x02000000,        0x10000 },
    [QUARD_STAR_PLIC]  = { 0x0c000000,        QUARD_STAR_PLIC_SIZE(QUARD_STAR_CPUS_MAX * 2) },
    [QUARD_STAR_UART0] = { 0x10000000,        0x100 },
    [QUARD_STAR_UART1] = { 0x10001000,        0x100 },
    [QUARD_STAR_UART2] = { 0x10002000,        0x100 },
    [QUARD_STAR_RTC]   = { 0x10003000,        0x1000 },
    [QUARD_STAR_FLASH] = { 0x20000000,        0x2000000 },
    [QUARD_STAR_DRAM]  = { 0x80000000,        0x40000000 },
};


/* 创建CPU */
// 按machine当前的 CPU/socket 拓扑配置，为每个 socket 创建一个 RISCVHartArray 子对象，并在 realize 时让它生成对应数量的 RISC-V CPU hart。
static void quard_star_cpu_create(MachineState *machine)
{
    int i, base_hartid, hart_count;
    char *soc_name;

    // 而是因为 它是由 DECLARE_INSTANCE_CHECKER 宏生成出来的，不是手写函数或普通宏定义。
    // 作用就类似QuardStarState *s = (QuardStarState *)object_dynamic_cast(machine);
    // 当然真实展开更复杂，还带类型检查，但本质就是：把一个父类/通用对象指针，检查后转成你自己的实例类型指针。
    QuardStarState *s = RISCV_QUARD_STAR_MACHINE(machine);  //因为我们创建machine时，是按QuardStarState的内存大小创建的，可以这个表示过去，而不会越界

    //检查socket 数量是否超过了板子支持的最大值
    if (QUARD_STAR_SOCKETS_MAX < riscv_socket_count(machine)) {
        error_report("number of sockets/nodes should be less than %d",
            QUARD_STAR_SOCKETS_MAX);
        exit(1);
    }

    //按socket创建cpu集群
    //一个 socket 对应一个 RISCVHartArrayState 对象。一次循环一个cpu组
    for (i = 0; i < riscv_socket_count(machine); i++) {
        if (!riscv_socket_check_hartids(machine, i))  //这是为了让 RISCVHartArray 能用“起始号 + 数量”这种简化参数来批量生成 CPU，而不是给每个 hart 单独传一堆乱七八糟的 ID。
        {
            error_report("discontinuous hartids in socket%d", i);
            exit(1);
        }

        base_hartid = riscv_socket_first_hartid(machine, i); //这一步得到第 i 个 socket 的第一个 hart ID。
        if (base_hartid < 0) {
            error_report("can't find hartid base for socket%d", i);
            exit(1);
        }

        hart_count = riscv_socket_hart_count(machine, i);  // 得到socket 里有多少个 hart。
        if (hart_count < 0) {
            error_report("can't find hart count for socket%d", i);
            exit(1);
        }
        
        //socket个数   hart_count  命令行传参
        // base_hartid  从0开始自己增加

        soc_name = g_strdup_printf("soc%d", i);  //分配了堆
        object_initialize_child(OBJECT(machine), soc_name, &s->soc[i],TYPE_RISCV_HART_ARRAY); 
        // 在父对象 machine 下面，初始化一个名字叫 soc0/soc1 的子对象，这个子对象的类型是 TYPE_RISCV_HART_ARRAY，对象内存放在 &s->soc[i] 这块位置上。 这一步只是“把容器对象先搭出来”。

        g_free(soc_name); //释放

        //设置cpu类型
        object_property_set_str(OBJECT(&s->soc[i]), "cpu-type",machine->cpu_type, &error_abort);
       //设置起始hart_id
        object_property_set_int(OBJECT(&s->soc[i]), "hartid-base",base_hartid, &error_abort);
       //设置hart数量
        object_property_set_int(OBJECT(&s->soc[i]), "num-harts",hart_count, &error_abort);
       //完成cpu集群初始化
        sysbus_realize(SYS_BUS_DEVICE(&s->soc[i]), &error_abort);
    }
}



static void riscv_install_rom_reset_stub(MachineState *machine, RISCVHartArrayState *harts,
                               hwaddr start_addr,
                               hwaddr rom_base, hwaddr rom_size,
                               uint64_t kernel_entry,
                               uint64_t fdt_load_addr)
{
    int i;
    uint32_t start_addr_hi32 = 0x00000000;
    uint32_t fdt_load_addr_hi32 = 0x00000000;

    if (!riscv_is_32bit(harts)) {
        start_addr_hi32 = start_addr >> 32;
        fdt_load_addr_hi32 = fdt_load_addr >> 32;
    }
    /* reset vector */
    uint32_t reset_vec[10] = {
        0x00000297,                  /* 1:  auipc  t0, %pcrel_hi(fw_dyn) */
        0x02828613,                  /*     addi   a2, t0, %pcrel_lo(1b) */
        0xf1402573,                  /*     csrr   a0, mhartid  */
        0,
        0,
        0x00028067,                  /*     jr     t0 */
        start_addr,                  /* start: .dword */
        start_addr_hi32,
        fdt_load_addr,               /* fdt_laddr: .dword */
        fdt_load_addr_hi32,
                                     /* fw_dyn: */
    };
    if (riscv_is_32bit(harts)) {
        reset_vec[3] = 0x0202a583;   /*     lw     a1, 32(t0) */
        reset_vec[4] = 0x0182a283;   /*     lw     t0, 24(t0) */
    } else {
        reset_vec[3] = 0x0202b583;   /*     ld     a1, 32(t0) */
        reset_vec[4] = 0x0182b283;   /*     ld     t0, 24(t0) */
    }

    if (!harts->harts[0].cfg.ext_zicsr) {
        /*
         * The Zicsr extension has been disabled, so let's ensure we don't
         * run the CSR instruction. Let's fill the address with a non
         * compressed nop.
         */
        reset_vec[2] = 0x00000013;   /*     addi   x0, x0, 0 */
    }

    /* copy in the reset vector in little_endian byte order */
    for (i = 0; i < ARRAY_SIZE(reset_vec); i++) {
        reset_vec[i] = cpu_to_le32(reset_vec[i]);
    }
    rom_add_blob_fixed_as("mrom.reset", reset_vec, sizeof(reset_vec),
                          rom_base, &address_space_memory);
    riscv_rom_copy_firmware_info(machine, rom_base, rom_size, sizeof(reset_vec),
                                 kernel_entry);
}




                          /*  创建内存 */
// ROM是只读仓库,出厂时数据就被物理掩膜固定死了。
// RAM是运行空间，cpu的工作台
// 在整机物理地址空间里挂三块内存：一块 DRAM、一块 SRAM、一块只读的 MROM；
// 然后在 MROM 中写入一段 RISC-V 启动代码，让 CPU 复位后先从 MROM 执行，再跳转到 Flash 的基地址继续启动。
static void quard_star_memory_create(MachineState *machine)
{
    QuardStarState *s = RISCV_QUARD_STAR_MACHINE(machine);
    MemoryRegion *system_memory = get_system_memory();   //拿到这台虚拟机的全局物理地址空间根节点。你必须显式地把一个 MemoryRegion 挂到 system_memory 上，那个地址范围才真的存在。
   
    //分配三片存储空间 dram sram mrom,只是分配了三个壳子
    MemoryRegion *dram_mem = g_new(MemoryRegion, 1);  //DRAM
    MemoryRegion *sram_mem = g_new(MemoryRegion, 1);  //SRAM
    MemoryRegion *mask_rom = g_new(MemoryRegion, 1);  //MROM  


    //初始化一块RAM region
    memory_region_init_ram(dram_mem, NULL, "riscv_quard_star_board.dram",
                           quard_star_memmap[QUARD_STAR_DRAM].size, &error_fatal);
    //映射到系统物理空间:把这块 RAM 挂到整机物理地址空间的某个 base 地址上。
    memory_region_add_subregion(system_memory, 
                                quard_star_memmap[QUARD_STAR_DRAM].base, dram_mem);

    memory_region_init_ram(sram_mem, NULL, "riscv_quard_star_board.sram",
                           quard_star_memmap[QUARD_STAR_SRAM].size, &error_fatal);
    memory_region_add_subregion(system_memory, 
                                quard_star_memmap[QUARD_STAR_SRAM].base, sram_mem);

    memory_region_init_rom(mask_rom, NULL, "riscv_quard_star_board.mrom",
                           quard_star_memmap[QUARD_STAR_MROM].size, &error_fatal);
    memory_region_add_subregion(system_memory, 
                                quard_star_memmap[QUARD_STAR_MROM].base, mask_rom);

    //在 MROM 里写入一段 reset 启动代码，并把 CPU 的 reset 行为和这段代码对应起来。
    // MROM -> FLASH 是很典型的板级启动设计。
    riscv_install_rom_reset_stub(machine, &s->soc[0], 
                              quard_star_memmap[QUARD_STAR_FLASH].base,
                              quard_star_memmap[QUARD_STAR_MROM].base,
                              quard_star_memmap[QUARD_STAR_MROM].size,
                              0x0, 0x0);
}



/*创建flash并映射，把 Flash 当成一个带协议、带 ID、带擦写行为、还能挂镜像文件的设备来建模。*/
//Flash 的核心特性：既能“持久保存”，又能“翻新修改” , “主力实体大仓库”。
//角色是硬盘（现在的 SSD 固态硬盘，底层用的就是 Flash 闪存芯片）。
static void quard_star_flash_create(MachineState *machine)
{
    #define QUARD_STAR_FLASH_SECTOR_SIZE (256 * KiB)  //0x40000  每个块/扇区的大小
    QuardStarState *s = RISCV_QUARD_STAR_MACHINE(machine);
    MemoryRegion *system_memory = get_system_memory();
    DeviceState *dev = qdev_new(TYPE_PFLASH_CFI01);    //创建一个 CFI 并行 Flash 设备对象。

    //设置Flash设备属性
    qdev_prop_set_uint64(dev, "sector-length", QUARD_STAR_FLASH_SECTOR_SIZE);
    qdev_prop_set_uint8(dev, "width", 4);   //Flash 对外呈现的总线访问宽度是 4 字节，也就是 32 位。
    qdev_prop_set_uint8(dev, "device-width", 2);  //这个表示单颗 flash 芯片的宽度是 2 字节 = 16 位。
    qdev_prop_set_bit(dev, "big-endian", false);  //小端访问
    qdev_prop_set_uint16(dev, "id0", 0x89);
    qdev_prop_set_uint16(dev, "id1", 0x18);
    qdev_prop_set_uint16(dev, "id2", 0x00);
    qdev_prop_set_uint16(dev, "id3", 0x00);
    qdev_prop_set_string(dev, "name","quard-star.flash0");

    //把设备挂进对象树
    object_property_add_child(OBJECT(s), "quard-star.flash0", OBJECT(dev));
    object_property_add_alias(OBJECT(s), "pflash0",OBJECT(dev), "drive");  //你可以从 machine 这层去配置 flash 的后端镜像，而不用每次都直接摸到底层设备属性。

    //把 block backend 绑定到 Flash 设备
    s->flash = PFLASH_CFI01(dev); //把通用 DeviceState * 转成更具体的 PFlashCFI01 *，方便后面操作。
    pflash_cfi01_legacy_drive(s->flash,drive_get(IF_PFLASH, 0, 0));

    //计算总块数
    hwaddr flashsize = quard_star_memmap[QUARD_STAR_FLASH].size;
    hwaddr flashbase = quard_star_memmap[QUARD_STAR_FLASH].base;
    assert(QEMU_IS_ALIGNED(flashsize, QUARD_STAR_FLASH_SECTOR_SIZE));
    assert(flashsize / QUARD_STAR_FLASH_SECTOR_SIZE <= UINT32_MAX);
    qdev_prop_set_uint32(dev, "num-blocks", flashsize / QUARD_STAR_FLASH_SECTOR_SIZE);

     //前面设置属性，都还只是准备阶段。realize 才是设备正式生效。
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);

    //把 Flash 的 MMIO 区域映射到物理地址空间
    memory_region_add_subregion(system_memory, flashbase,
                                sysbus_mmio_get_region(SYS_BUS_DEVICE(dev), 0));
}







/* 创建plic */
static void quard_star_plic_create(MachineState *machine)
{
    int socket_count = riscv_socket_count(machine);
    QuardStarState *s = RISCV_QUARD_STAR_MACHINE(machine);
    int i,hart_count,base_hartid;
    for ( i = 0; i < socket_count; i++) {
    
        hart_count = riscv_socket_hart_count(machine, i);
        base_hartid = riscv_socket_first_hartid(machine, i);
        char *plic_hart_config;
        /* Per-socket PLIC hart topology configuration string */
        plic_hart_config = riscv_plic_hart_config_string(hart_count);
        
        s->plic[i] = sifive_plic_create(
            quard_star_memmap[QUARD_STAR_PLIC].base + i *quard_star_memmap[QUARD_STAR_PLIC].size ,
            plic_hart_config, hart_count , base_hartid,
            QUARD_STAR_PLIC_NUM_SOURCES,
            QUARD_STAR_PLIC_NUM_PRIORITIES,
            QUARD_STAR_PLIC_PRIORITY_BASE,
            QUARD_STAR_PLIC_PENDING_BASE,
            QUARD_STAR_PLIC_ENABLE_BASE,
            QUARD_STAR_PLIC_ENABLE_STRIDE,
            QUARD_STAR_PLIC_CONTEXT_BASE,
            QUARD_STAR_PLIC_CONTEXT_STRIDE,
            quard_star_memmap[QUARD_STAR_PLIC].size);
        g_free(plic_hart_config);
    }
}



/*  创建 clint */
static void quard_star_clint_create(MachineState *machine)
{
    int i , hart_count,base_hartid;
    int socket_count = riscv_socket_count(machine);
    //每个CPU都需要创建 clint
    for ( i = 0; i < socket_count; i++) {

        base_hartid = riscv_socket_first_hartid(machine, i);
        hart_count = riscv_socket_hart_count(machine, i);

        riscv_aclint_swi_create(quard_star_memmap[QUARD_STAR_CLINT].base + i *quard_star_memmap[QUARD_STAR_CLINT].size,base_hartid, hart_count, false);
        riscv_aclint_mtimer_create(quard_star_memmap[QUARD_STAR_CLINT].base + i*quard_star_memmap[QUARD_STAR_CLINT].size+ RISCV_ACLINT_SWI_SIZE,
            RISCV_ACLINT_DEFAULT_MTIMER_SIZE, base_hartid, hart_count,
            RISCV_ACLINT_DEFAULT_MTIMECMP, RISCV_ACLINT_DEFAULT_MTIME,
            RISCV_ACLINT_DEFAULT_TIMEBASE_FREQ, true);
    }
}


 /*  创建RTC  */
static void quard_star_rtc_create(MachineState *machine)
{    
    QuardStarState *s = RISCV_QUARD_STAR_MACHINE(machine);
    sysbus_create_simple("goldfish_rtc", quard_star_memmap[QUARD_STAR_RTC].base,
        qdev_get_gpio_in(DEVICE(s->plic[0]), QUARD_STAR_RTC_IRQ));
}


/* 创建3个 uart */
static void quard_star_serial_create(MachineState *machine)
{
    MemoryRegion *system_memory = get_system_memory();
    QuardStarState *s = RISCV_QUARD_STAR_MACHINE(machine);
    
    serial_mm_init(system_memory, quard_star_memmap[QUARD_STAR_UART0].base,
        0, qdev_get_gpio_in(DEVICE(s->plic[0]), QUARD_STAR_UART0_IRQ), 399193,
        serial_hd(0), DEVICE_LITTLE_ENDIAN);
    serial_mm_init(system_memory, quard_star_memmap[QUARD_STAR_UART1].base,
        0, qdev_get_gpio_in(DEVICE(s->plic[0]), QUARD_STAR_UART1_IRQ), 399193,
        serial_hd(1), DEVICE_LITTLE_ENDIAN);
    serial_mm_init(system_memory, quard_star_memmap[QUARD_STAR_UART2].base,
        0, qdev_get_gpio_in(DEVICE(s->plic[0]), QUARD_STAR_UART2_IRQ), 399193,
        serial_hd(2), DEVICE_LITTLE_ENDIAN);
}


/* quard-star 初始化各种硬件 */
static void quard_star_machine_init(MachineState *machine)
{
   //创建CPU
   quard_star_cpu_create(machine);
   // 创建主存
   quard_star_memory_create(machine);
      //创建flash
   quard_star_flash_create(machine);
      //创建PLIC
    quard_star_plic_create(machine);
    //创建RISCV_CLINT
    quard_star_clint_create(machine);

        // 创建串口设备
    quard_star_serial_create(machine);
    // 创建 RTC
    quard_star_rtc_create(machine);

}



static void quard_star_machine_instance_init(Object *obj)
{

}  

/* 创建machine */
static void quard_star_machine_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    //核心必须项
    mc->desc = "RISC-V Quard Star board";   //板子的描述字符串
    mc->init = quard_star_machine_init;   //真正创建设备的地方  ， 注册一个回调入口。  
    mc->max_cpus = QUARD_STAR_CPUS_MAX;    //板子支持的最大CPU数
    mc->default_cpu_type = TYPE_RISCV_CPU_BASE;   //默认的cpu类型

 
    //和具体高级功能相关的可选项
    mc->pci_allow_0_address = true;  //PCI相关，在某些 PCI 场景下，是否允许地址 0 这种情况
    mc->possible_cpu_arch_ids = riscv_numa_possible_cpu_arch_ids;  //给 QEMU 提供可能的 CPU 架构 ID 集合
    mc->cpu_index_to_instance_props = riscv_numa_cpu_index_to_props;  //把CPU index 映射成对应的实例属性,这是为了让 QEMU 能把“第几个 CPU”转换成板子内部的 CPU 拓扑属性。
    mc->get_default_cpu_node_id = riscv_numa_get_default_cpu_node_id;  //给某个 CPU 分配默认的 NUMA node ID。
    mc->numa_mem_supported = true;  //这块 machine 支持 NUMA 内存相关配置。
} 





/* 注册 quard-star */
// TypeInfo决定“怎么创建这种对象”。类型描述
// QuardStarState 是“这个对象创建出来后里面长什么样”。对象实例内容

// 这个类型叫什么？             -> .name
// 它继承谁？                   -> .parent
// 类建立时用什么函数填规则？   -> .class_init
// 实例创建时用什么函数初始化？ -> .instance_init
// 实例对象多大？               -> .instance_size
// 它实现了什么接口？           -> .interfaces
static const TypeInfo quard_star_machine = {
    .name       = MACHINE_TYPE_NAME("quard-star"),
    .parent     = TYPE_MACHINE,  //也就是说你定义的不是一个普通设备，而是一个 machine 类型。因为 QEMU 类型系统是继承体系，不是全平铺的。
                                // 继承 TYPE_MACHINE，就意味着：这个类型会拥有 MachineClass ,它可以有 mc->init ,用来描述整块板子的，不是单个外设
    .class_init = quard_star_machine_class_init,  //类的初始化函数
    .instance_init = quard_star_machine_instance_init, //实例初始化函数
    .instance_size = sizeof(QuardStarState),           //这个实例对象要分配多大的内存，这里有点继承的意思
    
     //表示实现了某些接口
    .interfaces = (InterfaceInfo[]) {
         { TYPE_HOTPLUG_HANDLER },//热插拔接口
         { }
    },
};



static void quard_star_machine_register(void)
{
    type_register_static(&quard_star_machine);
}


//执行程序时，qemu程序先启动，过程中自动执行各处的type_init函数，完成设备的注册,走到type_register_static这个函数，
type_init(quard_star_machine_register)  
//就和字符设备驱动里的module_init一样，是一个宏，                               
// type_init不是“直接调用你的注册函数”，而是“让编译器生成一个 constructor 包装器；
// 这个包装器在程序启动时把你的注册函数塞进 QEMU 的 QOM 初始化表；后面 QEMU 再统一调用这张表里的函数 (module_call_init()时遍历列表) 