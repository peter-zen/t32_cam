# 设置项目根路径
set(PROJECT_ROOT_DIR ${CMAKE_CURRENT_SOURCE_DIR})
# 设置目标系统名称
set(CMAKE_SYSTEM_NAME Linux)

set(USE_UCLIBC 1)

# 设置目标系统处理器架构
set(CMAKE_SYSTEM_PROCESSOR mips)
set(TOOLCHAIN_PATH ${PROJECT_ROOT_DIR}/../bsp/toolchain/mips-gcc540-glibc222-cmake3.16.3-r3.3.7.mxu2.cve)

if (USE_UCLIBC)
set(UCLIBC_TAG "-uclibc")
add_definitions(-DUSE_UCLIBC)
else()
set(UCLIBC_TAG "")
endif()

# 设置交叉编译器路径
set(CMAKE_C_COMPILER ${TOOLCHAIN_PATH}/bin/mips-linux${UCLIBC_TAG}-gnu-gcc)
set(CMAKE_CXX_COMPILER ${TOOLCHAIN_PATH}/bin/mips-linux${UCLIBC_TAG}-gnu-g++)

# 设置交叉编译工具链的根路径
set(CMAKE_FIND_ROOT_PATH ${TOOLCHAIN_PATH})

list(APPEND CMAKE_FIND_ROOT_PATH
        ${TOOLCHAIN_PATH}/lib/gcc/mips-linux${UCLIBC_TAG}-gnu/5.4.0/
        ${TOOLCHAIN_PATH}/mips-linux${UCLIBC_TAG}-gnu/lib/
)

list(APPEND CMAKE_FIND_ROOT_PATH ${TOOLCHAIN_PATH}/include)

# 设置程序、库和头文件搜索路径为主机和目标目录
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM BOTH)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY BOTH)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE BOTH)

# 设置链接器路径（如果需要）
set(CMAKE_LINKER ${TOOLCHAIN_PATH}/bin/mips-linux${UCLIBC_TAG}-gnu-ld)

# 设置其他工具（如 archiver 和 ranlib）
#set(CMAKE_AR ${TOOLCHAIN_PATH}/bin/mips-linux${UCLIBC_TAG}-gnu-ar)
#set(CMAKE_AS ${TOOLCHAIN_PATH}/bin/mips-linux${UCLIBC_TAG}-gnu-as)
#set(CMAKE_NM ${TOOLCHAIN_PATH}/bin/mips-linux${UCLIBC_TAG}-gnu-nm)
#set(CMAKE_RANLIB ${TOOLCHAIN_PATH}/bin/mips-linux${UCLIBC_TAG}-gnu-ranlib)
set(CMAKE_OBJDUMP ${TOOLCHAIN_PATH}/bin/mips-linux${UCLIBC_TAG}-gnu-objdump)
set(CMAKE_OBJCOPY ${TOOLCHAIN_PATH}/bin/mips-linux${UCLIBC_TAG}-gnu-objcopy)
set(CMAKE_STRIP ${TOOLCHAIN_PATH}/bin/mips-linux${UCLIBC_TAG}-gnu-strip)
#set(CMAKE_READELF ${TOOLCHAIN_PATH}/bin/mips-linux${UCLIBC_TAG}-gnu-readelf)
#set(CMAKE_SIZE ${TOOLCHAIN_PATH}/bin/mips-linux${UCLIBC_TAG}-gnu-size)
#set(CMAKE_OBJSIZE ${TOOLCHAIN_PATH}/bin/mips-linux${UCLIBC_TAG}-gnu-objsize)

# Set optimization flags to reduce executable size
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Os -ffunction-sections -fdata-sections -fno-unwind-tables -fno-asynchronous-unwind-tables -fno-ident")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Os -ffunction-sections -fdata-sections -fno-unwind-tables -fno-asynchronous-unwind-tables -fno-ident")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,--gc-sections -Wl,-s -Wl,--strip-all -latomic")
set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -Wl,--gc-sections -Wl,-s -Wl,--strip-all")
set(CMAKE_MODULE_LINKER_FLAGS "${CMAKE_MODULE_LINKER_FLAGS} -Wl,--gc-sections -Wl,-s -Wl,--strip-all")
