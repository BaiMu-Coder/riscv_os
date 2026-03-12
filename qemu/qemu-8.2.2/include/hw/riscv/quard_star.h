#ifndef HW_RISCV_QUARD_STAR__H
#define HW_RISCV_QUARD_STAR__H

#include "hw/riscv/riscv_hart.h"
#include "hw/sysbus.h"
#include "qom/object.h"
#include "hw/block/flash.h"

#define QUARD_STAR_CPUS_MAX 8
#define QUARD_STAR_SOCKETS_MAX 8

#define TYPE_RISCV_QUARD_STAR_MACHINE MACHINE_TYPE_NAME("quard-star")
typedef struct QuardStarState QuardStarState;

// 把一个父类/通用对象指针，检查后转成你自己的实例类型指针。
DECLARE_INSTANCE_CHECKER(QuardStarState, RISCV_QUARD_STAR_MACHINE,
                         TYPE_RISCV_QUARD_STAR_MACHINE)


//板子的硬件信息
struct QuardStarState {
    /*< private >*/
    MachineState parent;

    /*< public >*/
    RISCVHartArrayState soc[QUARD_STAR_SOCKETS_MAX];
    PFlashCFI01* flash;
    DeviceState* plic[QUARD_STAR_SOCKETS_MAX];
};

enum {
    QUARD_STAR_MROM,
    QUARD_STAR_SRAM,
    QUARD_STAR_CLINT,
    QUARD_STAR_PLIC,
    QUARD_STAR_UART0,
    QUARD_STAR_FLASH,
    QUARD_STAR_DRAM,
};

enum {
    QUARD_STAR_UART0_IRQ = 10,  //定义了串口中断号为10
};
#endif