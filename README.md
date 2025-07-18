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