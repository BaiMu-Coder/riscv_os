# 获取当前脚本文件所在的目录
SHELL_FOLDER=$(cd "$(dirname "$0")";pwd)

cd qemu-8.2.2
if [ ! -d "$SHELL_FOLDER/output/qemu" ]; then  
./configure --prefix=$SHELL_FOLDER/output/qemu  --target-list=riscv64-softmmu --enable-gtk  --enable-virtfs --disable-gio
fi  
make -j32
sudo make install
cd ..
