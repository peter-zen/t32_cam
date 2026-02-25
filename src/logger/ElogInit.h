/*
 * ElogInit.h - EasyLogger initialization interface
 * 
 * Provides simple initialization functions for EasyLogger.
 * Call elog_init_default() or elog_init_with_config() at application startup.
 */

#ifndef ELOG_INIT_H
#define ELOG_INIT_H

#include <string>
#include <elog.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Configuration structure for EasyLogger initialization
 */
struct ElogConfig {
    bool enableTerminal;      /**< Enable terminal (stdout) output */
    bool enableFile;          /**< Enable file output */
    std::string logFilePath;  /**< Path to log file (if enableFile is true) */
    uint8_t logLevel;         /**< Log level filter (ELOG_LVL_xxx) */
    
    /** Default constructor with sensible defaults */
    ElogConfig() 
        : enableTerminal(true)
        , enableFile(false)
        , logFilePath("")
        , logLevel(ELOG_LVL_INFO)
    {}
};

/**
 * Initialize EasyLogger with custom configuration
 * 
 * @param config Configuration options
 * @return true on success, false on failure
 */
bool elog_init_with_config(const ElogConfig& config);

/**
 * Initialize EasyLogger with default settings
 * - Terminal output enabled
 * - File output disabled
 * - Log level: INFO
 * - Format: level, tag, time, message
 * 
 * @return true on success, false on failure
 */
bool elog_init_default(void);

/**
 * Deinitialize EasyLogger
 * Call this before application exit to clean up resources
 */
void elog_deinit_all(void);

/**
 * Set log file output path
 * Can be called after initialization to change log file
 * 
 * @param file_path Path to log file, NULL to disable file output
 * @return 0 on success, -1 on failure
 */
int elog_set_file_output(const char *file_path);

/**
 * Enable or disable terminal output
 * 
 * @param enabled true to enable, false to disable
 */
void elog_set_terminal_output(bool enabled);

#ifdef __cplusplus
}
#endif

#endif // ELOG_INIT_H
