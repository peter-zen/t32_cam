/**
 * @file system_call_stub.c
 * @brief system_call库的PC模拟实现
 * 
 * 在PC环境下提供system_call库的替代实现，
 * 直接使用标准库函数实现相同功能。
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief system_call的PC模拟实现
 * 直接调用标准库的system()函数
 */
int system_call(char *cmd, int timeout_ms)
{
    (void)timeout_ms;  // PC环境下忽略超时参数
    
    if (cmd == NULL) {
        return -1;
    }
    
    return system(cmd);
}

/**
 * @brief popen_call的PC模拟实现
 * 直接调用标准库的popen()函数
 */
int popen_call(char *cmd, char *out, int max_size, int timeout_ms)
{
    (void)timeout_ms;  // PC环境下忽略超时参数
    
    if (cmd == NULL || out == NULL || max_size <= 0) {
        return -1;
    }
    
    FILE *fp = popen(cmd, "r");
    if (fp == NULL) {
        return -1;
    }
    
    memset(out, 0, max_size);
    
    int total_read = 0;
    char buffer[256];
    while (fgets(buffer, sizeof(buffer), fp) != NULL) {
        int len = strlen(buffer);
        if (total_read + len < max_size) {
            strcat(out, buffer);
            total_read += len;
        } else {
            break;
        }
    }
    
    int ret = pclose(fp);
    return ret;
}

/**
 * @brief system_call_init的PC模拟实现
 * PC环境下无需初始化，直接返回成功
 */
int system_call_init(void)
{
    printf("[SDK_STUB] system_call_init: PC simulation mode\n");
    return 0;
}

/**
 * @brief system_call_exit的PC模拟实现
 * PC环境下无需清理，直接返回成功
 */
int system_call_exit(void)
{
    printf("[SDK_STUB] system_call_exit: PC simulation mode\n");
    return 0;
}

#ifdef __cplusplus
}
#endif
