# 获取当前脚本文件所在的目录
SHELL_FOLDER=$(cd "$(dirname "$0")";pwd)

rm -rf output
cd qemu-8.2.2
rm -rf build

if [ ! -d "$SHELL_FOLDER/output/qemu" ]; then 
# []这里面的条件为真（-d是判断 后面这个目录是否存在），则执行then后面的命令

# --prefix设置安装目录
./configure --prefix="$SHELL_FOLDER/output/qemu"  --target-list=riscv64-softmmu --enable-gtk  --enable-virtfs --disable-gio

#fi表示 if语句的结束
fi  

make -j$(nproc)
sudo make install
cd ..


CROSS_PREFIX=riscv64-linux-gnu



#-----------------------------------------------------------------------------------------------------------------------------------------------#
#-----------------------------------------------------------------编译 lowlevelboot-------------------------------------------------------------#
#-----------------------------------------------------------------------------------------------------------------------------------------------#
mkdir -p "$SHELL_FOLDER/output/lowlevelboot"
cd boot || exit 1
# -x assembler-with-cpp 表示按“带 C 预处理器的汇编”来处理这个文件
${CROSS_PREFIX}-gcc -x assembler-with-cpp -c start.s -o $SHELL_FOLDER/output/lowlevelboot/start.o

${CROSS_PREFIX}-gcc -nostartfiles -T./boot.ld -Wl,-Map=$SHELL_FOLDER/output/lowlevelboot/lowlevel_fw.map -Wl,--gc-sections \
$SHELL_FOLDER/output/lowlevelboot/start.o -o $SHELL_FOLDER/output/lowlevelboot/lowlevel_fw.elf
#-nostartfiles：不要自动连接系统默认的启动文件
#-T./boot.ld  ：指定链接脚本
#-Wl,-Map=... : 让链接器生成map文件
#-Wl,--gc-sections :开启section垃圾回收，没被引用的section会被删除


# 使用gnu工具生成原始的程序bin文件
# .elf 适合调试、查看符号、反汇编    .bin 才是裸固件内容
${CROSS_PREFIX}-objcopy -O binary -S $SHELL_FOLDER/output/lowlevelboot/lowlevel_fw.elf $SHELL_FOLDER/output/lowlevelboot/lowlevel_fw.bin
#-O binary : 输出格式是纯二进制
#-S        : 去掉不必要的符号/调试信息


# 使用gnu工具生成反汇编文件，方便调试分析（当然我们这个代码太简单，不是很需要）
${CROSS_PREFIX}-objdump --source --demangle --disassemble --reloc --wide $SHELL_FOLDER/output/lowlevelboot/lowlevel_fw.elf > $SHELL_FOLDER/output/lowlevelboot/lowlevel_fw.lst

cd $SHELL_FOLDER/output/lowlevelboot
rm -rf fw.bin

#创建一个 大小位32MB，内容全是0的fw.bin
dd of=fw.bin bs=1k count=32k if=/dev/zero
#dd :按块从输入读数据，再按块写到输出，还能指定偏移、块大小、是否截断。
#if :指定输入  （/dev/zero是一个特殊设备，读出来永远是0）
#of :指定输出
#bs :每次按1KB位一个块
#count ：一共拷贝32K个块（1K=1024）

#把lowlevel固件写进镜像开头
dd of=fw.bin bs=1k conv=notrunc seek=0 if=lowlevel_fw.bin
#seek :表示第0个块开始写
#conv=notrunc :表示不要把fw.bin截短 （保持整个镜像大小不变，只覆盖前面那一段内容）



#-----------------------------------------------------------------------------------------------------------------------------------------------#
#---------------------------------------------------------------------编译 opensbi--------------------------------------------------------------#
#-----------------------------------------------------------------------------------------------------------------------------------------------#
mkdir -p "$SHELL_FOLDER/output/opensbi"

cd $SHELL_FOLDER/opensbi-1.2
rm -rf build
make CROSS_COMPILE=${CROSS_PREFIX}- PLATFORM=quard_star
cp -r $SHELL_FOLDER/opensbi-1.2/build/platform/quard_star/firmware/*.bin $SHELL_FOLDER/output/opensbi/

# 生成sbi.dtb
cd $SHELL_FOLDER/dts
dtc -I dts -O dtb -o $SHELL_FOLDER/output/opensbi/quard_star_sbi.dtb quard_star_sbi.dts

# 合成firmware固件
if [ ! -d "$SHELL_FOLDER/output/fw" ]; then  
mkdir $SHELL_FOLDER/output/fw
fi  
cd $SHELL_FOLDER/output/fw
rm -rf fw.bin
# 填充 32K的0
dd of=fw.bin bs=1k count=32k if=/dev/zero   
# # 写入 lowlevel_fw.bin 偏移量地址为 0
dd of=fw.bin bs=1k conv=notrunc seek=0 if=$SHELL_FOLDER/output/lowlevelboot/lowlevel_fw.bin
# 写入 quard_star_sbi.dtb 地址偏移量为 512K，因此 fdt的地址偏移量为 0x80000
dd of=fw.bin bs=1k conv=notrunc seek=512 if=$SHELL_FOLDER/output/opensbi/quard_star_sbi.dtb
# 写入 fw_jump.bin 地址偏移量为 2K*1K= 0x2000000，因此 fw_jump.bin的地址偏移量为  0x2000000
dd of=fw.bin bs=1k conv=notrunc seek=2k if=$SHELL_FOLDER/output/opensbi/fw_jump.bin




