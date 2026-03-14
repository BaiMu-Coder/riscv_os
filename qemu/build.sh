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
if [ ! -d "$SHELL_FOLDER/output/lowlevelboot" ]; then  
mkdir $SHELL_FOLDER/output/lowlevelboot
fi  
cd boot
$CROSS_PREFIX-gcc -x assembler-with-cpp -c start.s -o $SHELL_FOLDER/output/lowlevelboot/start.o
$CROSS_PREFIX-gcc -nostartfiles -T./boot.ld -Wl,-Map=$SHELL_FOLDER/output/lowlevelboot/lowlevel_fw.map -Wl,--gc-sections $SHELL_FOLDER/output/lowlevelboot/start.o -o $SHELL_FOLDER/output/lowlevelboot/lowlevel_fw.elf
# 使用gnu工具生成原始的程序bin文件
$CROSS_PREFIX-objcopy -O binary -S $SHELL_FOLDER/output/lowlevelboot/lowlevel_fw.elf $SHELL_FOLDER/output/lowlevelboot/lowlevel_fw.bin
# 使用gnu工具生成反汇编文件，方便调试分析（当然我们这个代码太简单，不是很需要）
$CROSS_PREFIX-objdump --source --demangle --disassemble --reloc --wide $SHELL_FOLDER/output/lowlevelboot/lowlevel_fw.elf > $SHELL_FOLDER/output/lowlevelboot/lowlevel_fw.lst

cd $SHELL_FOLDER/output/lowlevelboot
rm -rf fw.bin
dd of=fw.bin bs=1k count=32k if=/dev/zero
dd of=fw.bin bs=1k conv=notrunc seek=0 if=lowlevel_fw.bin




