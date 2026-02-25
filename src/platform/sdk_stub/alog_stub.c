/**
 * @file alog_stub.c
 * @brief alog库的PC模拟实现
 * 
 * 在PC环境下提供alog库的替代实现，
 * 将日志输出到标准输出。
 */

#include <stdio.h>
#include <stdarg.h>
#include <time.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Log level definitions (matching imp_log.h) */
enum {
    IMP_LOG_LEVEL_UNKNOWN = 0,
    IMP_LOG_LEVEL_STDOUT,
    IMP_LOG_LEVEL_VERBOSE,
    IMP_LOG_LEVEL_DEBUG,
    IMP_LOG_LEVEL_INFO,
    IMP_LOG_LEVEL_WARN,
    IMP_LOG_LEVEL_ERROR,
    IMP_LOG_LEVEL_FATAL,
    IMP_LOG_LEVEL_SILENT
};

/* Log output definitions */
#define IMP_LOG_OUT_STDOUT      0
#define IMP_LOG_OUT_LOCAL_FILE  1
#define IMP_LOG_OUT_SERVER      2

/* Global log option */
static int g_log_option = 0x3F;  /* Default: all options enabled */

/**
 * @brief 获取日志级别字符串
 */
static const char* get_level_str(int level)
{
    switch (level) {
        case IMP_LOG_LEVEL_VERBOSE: return "V";
        case IMP_LOG_LEVEL_DEBUG:   return "D";
        case IMP_LOG_LEVEL_INFO:    return "I";
        case IMP_LOG_LEVEL_WARN:    return "W";
        case IMP_LOG_LEVEL_ERROR:   return "E";
        case IMP_LOG_LEVEL_FATAL:   return "F";
        default:                    return "?";
    }
}

/**
 * @brief imp_log_fun的PC模拟实现
 * 将日志输出到标准输出
 */
void imp_log_fun(int le, int op, int out, const char* tag, 
                 const char* file, int line, const char* func, 
                 const char* fmt, ...)
{
    (void)op;
    (void)out;
    
    /* Skip silent level */
    if (le == IMP_LOG_LEVEL_SILENT) {
        return;
    }
    
    /* Get current time */
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char time_buf[32];
    strftime(time_buf, sizeof(time_buf), "%H:%M:%S", tm_info);
    
    /* Extract filename from path */
    const char *filename = file;
    const char *p = strrchr(file, '/');
    if (p) {
        filename = p + 1;
    }
    
    /* Print log header */
    printf("[%s][%s][%s] %s:%d %s(): ", 
           time_buf, get_level_str(le), tag ? tag : "???",
           filename, line, func);
    
    /* Print log message */
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
    
    /* Ensure newline */
    if (fmt && strlen(fmt) > 0 && fmt[strlen(fmt)-1] != '\n') {
        printf("\n");
    }
    
    fflush(stdout);
}

/**
 * @brief 设置日志选项
 */
void IMP_Log_Set_Option(int op)
{
    g_log_option = op;
}

/**
 * @brief 获取日志选项
 */
int IMP_Log_Get_Option(void)
{
    return g_log_option;
}

#ifdef __cplusplus
}
#endif
