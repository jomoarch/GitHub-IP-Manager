#!/bin/bash
echo "正在编译 GitHub IP 优化工具..."
if [ -d "build" ]; then
  rm -rf build
fi
mkdir -p build
cd build
cmake ..
make -j$(nproc)

if [ $? -eq 0 ]; then
  echo "编译成功！可执行文件在 build/ 目录中"
else
  echo "编译失败！"
  exit 1
fi
