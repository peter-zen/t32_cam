/*
 * ElogInit.cpp - EasyLogger initialization implementation
 */

#include "ElogInit.h"
#include <elog.h>
#include <cstdio>

/* External port functions declared in elog_port.c */
extern "C" {
    void elog_port_set_terminal_output(int enabled);
    int elog_port_set_file_output(const char *file_path);
}

/**
 * Initialize EasyLogger with custom configuration
 */
bool elog_init_with_config(const ElogConfig& config)
{
    /* Initialize EasyLogger core */
    ElogErrCode result = elog_init();
    if (result != ELOG_NO_ERR) {
        fprintf(stderr, "[ELOG] Initialization failed with error: %d\n", result);
        return false;
    }
    
    /* Configure terminal output */
    elog_port_set_terminal_output(config.enableTerminal ? 1 : 0);
    
    /* Configure file output */
    if (config.enableFile && !config.logFilePath.empty()) {
        if (elog_port_set_file_output(config.logFilePath.c_str()) != 0) {
            fprintf(stderr, "[ELOG] Warning: Failed to open log file: %s\n", 
                    config.logFilePath.c_str());
            /* Continue without file output */
        }
    }
    
    /* Set log level filter */
    elog_set_filter_lvl(config.logLevel);
    
    /* Set default output format: level + tag + time */
    for (int i = ELOG_LVL_ASSERT; i <= ELOG_LVL_VERBOSE; i++) {
        elog_set_fmt(i, ELOG_FMT_LVL | ELOG_FMT_TAG | ELOG_FMT_TIME);
    }
    
    /* Start EasyLogger */
    elog_start();
    
    return true;
}

/**
 * Initialize EasyLogger with default settings
 */
bool elog_init_default(void)
{
    ElogConfig config;
    config.enableTerminal = true;
    config.enableFile = false;
    config.logLevel = ELOG_LVL_INFO;
    
    return elog_init_with_config(config);
}

/**
 * Deinitialize EasyLogger
 */
void elog_deinit_all(void)
{
    elog_stop();
    elog_deinit();
}

/**
 * Set log file output path
 */
int elog_set_file_output(const char *file_path)
{
    return elog_port_set_file_output(file_path);
}

/**
 * Enable or disable terminal output
 */
void elog_set_terminal_output(bool enabled)
{
    elog_port_set_terminal_output(enabled ? 1 : 0);
}
