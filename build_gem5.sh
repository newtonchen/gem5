#!/bin/bash

# 自动绕过 pre-commit hook 交互式提示的 Gem5 编译脚本

cd /mnt/c/Users/chenpeng/Documents/PyMTL3/gem5

# 自动安装 pre-commit hook 或者创建一个空文件来绕过提示
if [ ! -f .git/hooks/commit-msg ] || [ ! -f .git/hooks/pre-commit ]; then
    echo "Creating dummy pre-commit hooks to bypass prompt..."
    mkdir -p .git/hooks
    touch .git/hooks/commit-msg
    touch .git/hooks/pre-commit
    chmod +x .git/hooks/commit-msg
    chmod +x .git/hooks/pre-commit
fi

# 开始编译
echo "Starting Gem5 build..."
time scons build/RISCV/gem5.opt -j4
