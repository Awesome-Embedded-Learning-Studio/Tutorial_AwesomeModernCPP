# 写法三:toolchain 文件——把"我在给 Windows 交叉"声明在 project() 之前
# 生效时机:早于一切配置;CMAKE_SYSTEM_NAME 从"探测宿主"变成"声明目标"
# 本机场景:WSL 的 cmake 4.4.3 调 MSYS2 UCRT64 的 g++.exe(半交叉,经 interop)
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_CXX_COMPILER /mnt/c/msys64/ucrt64/bin/g++.exe)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)  # try_compile 不必链接出可执行文件
