#!/bin/bash
export DISPLAY=:0
SHELL_FOLDER=$(cd "$(dirname "$0")";pwd)

# 构建参数数组
QEMU_ARGS=(
    -M quard-star       # 指定模拟的机器型号为 quard-star
    -m 1G               # 分配 1GB 的运行内存
    -smp 8              # 模拟 8 核 CPU
    -bios none          # 不加载默认 BIOS
    -nographic          # 禁用图形界面
    -parallel none      # 禁用并口
    -drive if=pflash,bus=0,unit=0,format=raw,file=${SHELL_FOLDER}/output/fw/fw.bin    #指定固件驱动
    #-drive        : 表示给虚拟机挂一个“驱动器/存储后端”
    #if=pflash     ：表示这个 drive 不是普通硬盘，不是 IDE/SCSI/virtio-blk，而是 parallel flash
    #bus=0,unit=0  ：挂在0行flash总线，第0个单元
    #format=raw    ：表示这个文件就是原始裸镜像 ，也就是说 fw.bin 里的字节内容，直接就是 flash 里的字节内容。

    -d in_asm           # 开启汇编指令日志
    -D qemu.log         # 将日志输出到 qemu.log 文件
)

# 执行命令并传入数组参数 ("${数组名[@]}" 会完美展开所有参数)
${SHELL_FOLDER}/output/qemu/bin/qemu-system-riscv64 "${QEMU_ARGS[@]}"