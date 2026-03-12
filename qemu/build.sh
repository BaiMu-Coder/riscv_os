# 获取当前脚本文件所在的目录
SHELL_FOLDER=$(cd "$(dirname "$0")";pwd)

rm -rf output
cd qemu-8.2.2
rm -rf build
if [ ! -d "$SHELL_FOLDER/output/qemu" ]; then  
./configure --prefix=$SHELL_FOLDER/output/qemu  --target-list=riscv64-softmmu --enable-gtk  --enable-virtfs --disable-gio
fi  
make -j$(nproc)
sudo make install
cd ..
