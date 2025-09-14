# How to build project
## Modify toolchain.cmake
- Set the project root path to your toolchain.cmake file
    e.g: set(PROJECT_ROOT_DIR /home/zengping/t32)
## make build directory
- mkdir build
- cd build
## run cmake
- cmake -DCMAKE_TOOLCHAIN_FILE=../toolchain.cmake ..
- make

# How to build sdk/samples/libimp-samples/
- cd sdk/samples/libimp-samples/
- make CROSS_COMPILE=/home/zengping/t32/bsp/toolchain/mips-gcc540-glibc222-cmake3.16.3-r3.3.7.mxu2.cve/bin/mips-linux-uclibc-gnu-