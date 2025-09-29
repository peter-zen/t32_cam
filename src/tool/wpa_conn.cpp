#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <mutex>
#include "system_call.h"
#include <netinet/in.h>

// Function prototypes
int check_arguments(int argc, char *argv[]);
int create_directory(const char *dir_path);
int generate_wifi_config(const char *ssid, const char *password, const char *config_file);
int load_wifi_driver(const char *driver_path);
int connect_wifi(const char *ifname, const char *config_file, int timeout);
int configure_dhcp(const char *ifname, int retries);
void print_ip_address(const char *ifname);
int execute_command(const char *command);
char *get_command_output(const char *command);

// Global mutex for system call synchronization
static std::mutex syscall_mutex;
static bool syscall_inited = false;

int main(int argc, char *argv[]) {
    // Check for required arguments
    if (check_arguments(argc, argv) != 0) {
        return 1;
    }
    
    // Assign arguments to variables
    const char *WIFI_IFNAME = argv[1];
    const char *WIFI_SSID = argv[2];
    const char *WIFI_PASSWORD = argv[3];
    int TIMEOUT = atoi(argv[4]);
    
    // Define the output file path
    const char *CONFIG_FILE = "/config/profiles/wpa_supplicant.conf";
    
    // Ensure the directory exists
    if (create_directory("/config/profiles") != 0) {
        printf("Error: Failed to create directory for configuration\n");
        return 1;
    }
    
    // Generate wpa_supplicant configuration
    if (generate_wifi_config(WIFI_SSID, WIFI_PASSWORD, CONFIG_FILE) != 0) {
        printf("Error: Failed to generate WiFi configuration\n");
        return 1;
    }
    
    // Load WiFi driver
    if (load_wifi_driver("/system/bin/wifi/8189fs.ko") != 0) {
        printf("Error: Failed to load WiFi driver\n");
        return 1;
    }
    
    // Connect WiFi
    if (connect_wifi(WIFI_IFNAME, CONFIG_FILE, TIMEOUT) != 0) {
        printf("Error: WiFi connection failed\n");
        return 1;
    }
 #if 0   
    // Configure DHCP
    if (configure_dhcp(WIFI_IFNAME, 20) != 0) {
        printf("Error: Failed to configure DHCP\n");
        return 1;
    }
    
    // Print IP address
    print_ip_address(WIFI_IFNAME);
#endif
    return 0;
}

int check_arguments(int argc, char *argv[]) {
    if (argc != 5) {
        printf("Error: Four arguments are required\n");
        printf("Usage: %s <wifi_ifname> <wifi_ssid> <wifi_password> <timeout>\n", argv[0]);
        return 1;
    }
    return 0;
}

int create_directory(const char *dir_path) {
    char cmd[256];
    sprintf(cmd, "mkdir -p %s", dir_path);
    return execute_command(cmd);
}

int generate_wifi_config(const char *ssid, const char *password, const char *config_file) {
    // Create command to generate configuration
    std::string cmd = "/system/bin/wifi/wpa_passphrase " + std::string(ssid) + " " + std::string(password) + " > " + std::string(config_file);
    int result = execute_command(cmd.c_str());
    if (result != 0) {
        printf("Error: Failed to generate WiFi configuration\n");
        return 1;
    }

    printf("WiFi configuration generated successfully\n");

    return 0;
}

int load_wifi_driver(const char *driver_path) {
    char cmd[256];
    sprintf(cmd, "insmod %s", driver_path);
    int result = execute_command(cmd);
    
    if (result == 0) {
        printf("WiFi driver successfully loaded\n");
    }
    
    return result;
}

int connect_wifi(const char *ifname, const char *config_file, int timeout) {
    // Start wpa_supplicant
    char cmd[256];
    sprintf(cmd, "/system/bin/wifi/wpa_supplicant -i %s -c %s -C /tmp/wpa_supplicant &", ifname, config_file);
    
    int result = execute_command(cmd);
    if (result != 0) {
        printf("Error: Failed to initiate WiFi connection\n");
        return 1;
    }
    
    printf("WiFi connection successfully initiated\n");
    printf("Waiting for WiFi connection to be established...\n");
    
    int wait_count = 0;
    char *conn_status = NULL;
    
    // Wait for connection to be established
    while (wait_count < timeout) {
        // Check connection status using wpa_cli with the same control interface path
        sprintf(cmd, "/system/bin/wifi/wpa_cli -i \"%s\" -p /tmp/wpa_supplicant status 2>/dev/null", ifname);
        conn_status = get_command_output(cmd);
        
        // Parse wpa_state from the output manually
        if (conn_status != NULL) {
            char *state_pos = strstr(conn_status, "wpa_state=");
            if (state_pos != NULL) {
                // Move past "wpa_state="
                state_pos += 10;
                // Find the end of the line or next line
                char *state_end = strpbrk(state_pos, "\n\r");
                if (state_end != NULL) {
                    *state_end = '\0';  // Terminate the string at the end of the state line
                }
                
                if (strcmp(state_pos, "COMPLETED") == 0) {
                    printf("WiFi successfully connected\n");
                    free(conn_status);
                    break;
                }
            }
            free(conn_status);
        }
        
        sleep(1);
        wait_count++;
    }
    
    // Get final status for check - without pipe/awk
    sprintf(cmd, "/system/bin/wifi/wpa_cli -i \"%s\" -p /tmp/wpa_supplicant status 2>/dev/null", ifname);
    conn_status = get_command_output(cmd);
    
    // Parse wpa_state from final output manually
    char *state_value = NULL;
    if (conn_status != NULL) {
        char *state_pos = strstr(conn_status, "wpa_state=");
        if (state_pos != NULL) {
            state_pos += 10;  // Move past "wpa_state="
            char *state_end = strpbrk(state_pos, "\n\r");
            if (state_end != NULL) {
                *state_end = '\0';  // Terminate the string at the end of the state line
            }
            state_value = state_pos;
        }
    }
    
    if (state_value == NULL || strcmp(state_value, "COMPLETED") != 0) {
        printf("Error: WiFi connection timed out after %d seconds\n", timeout);
        if (conn_status != NULL) {
            printf("Current status: %s\n", (state_value != NULL) ? state_value : "unknown");
            free(conn_status);
        }
        return 1;
    }
    
    free(conn_status);
    return 0;
}

int configure_dhcp(const char *ifname, int retries) {
    char cmd[256];
    sprintf(cmd, "udhcpc -i \"%s\" -t %d", ifname, retries);
    int result = execute_command(cmd);
    
    if (result == 0) {
        printf("DHCP successfully configured\n");
    }
    
    return result;
}

void print_ip_address(const char *ifname) {
    char cmd[256];
    // Use ifconfig instead of ip command as environment doesn't support ip
    sprintf(cmd, "ifconfig \"%s\"", ifname);
    char *output = get_command_output(cmd);
    
    if (output != NULL) {
        // Parse inet address from ifconfig output
        // Looking for pattern like "inet addr:192.168.1.100" or "inet 192.168.1.100"
        char *inet_pos = strstr(output, "inet addr:");
        if (inet_pos == NULL) {
            // Try alternative format "inet "
            inet_pos = strstr(output, "inet ");
            if (inet_pos != NULL) {
                inet_pos += 5;  // Move past "inet "
            }
        } else {
            inet_pos += 10;  // Move past "inet addr:" to get to the IP
        }
        
        if (inet_pos != NULL) {
            // Skip any leading whitespace
            while (*inet_pos == ' ' || *inet_pos == '\t') {
                inet_pos++;
            }
            
            // Find the end of the IP address (before space, netmask, or other characters)
            char *addr_end = inet_pos;
            while (*addr_end != ' ' && *addr_end != '\t' && *addr_end != '\n' && *addr_end != '\0') {
                addr_end++;
            }
            
            if (addr_end != inet_pos) {
                // Create a temporary copy of the IP address
                char ip_address[INET_ADDRSTRLEN];
                int ip_len = addr_end - inet_pos;
                if (ip_len < INET_ADDRSTRLEN) {
                    strncpy(ip_address, inet_pos, ip_len);
                    ip_address[ip_len] = '\0';
                    printf("IP Address: %s\n", ip_address);
                } else {
                    printf("IP Address too long for %s\n", ifname);
                }
            } else {
                printf("No IP address found for %s\n", ifname);
            }
        } else {
            printf("No IP address found for %s\n", ifname);
        }
        free(output);
    }
}

int execute_command(const char *command) {
    int ret = 0;
	{
		std::unique_lock<std::mutex> lock(syscall_mutex);
		if (!syscall_inited) {
			ret = system_call_init();
			if(ret < 0) {
				return ret;
			}
			syscall_inited = true;
		}
	}
	
	ret = system_call((char*)command, 10000);
	if(ret < 0) {
		return ret;
	}
	
	return ret;
}

char *get_command_output(const char *command) {
    char buffer[1024] = {0};
    char *result = NULL;
    
    int ret = 0;
    {
        std::unique_lock<std::mutex> lock(syscall_mutex);
        if (!syscall_inited) {
            ret = system_call_init();
            if(ret < 0) {
                printf("Failed to initialize system call\n");
                return NULL;
            }
            syscall_inited = true;
        }
    }
    
    ret = popen_call((char*)command, buffer, sizeof(buffer) - 1, 5000);
    if (ret < 0) {
        printf("Command execution failed: %s\n", command);
        return NULL;
    }
    
    // Check if we got any output
    if (strlen(buffer) > 0) {
        // Remove trailing newline if present
        size_t len = strlen(buffer);
        if (len > 0 && buffer[len-1] == '\n') {
            buffer[len-1] = '\0';
        }
        
        // Allocate memory for the result and copy the output
        result = strdup(buffer);
        if (result == NULL) {
            printf("Memory allocation failed for command output\n");
        }
    }
    
    return result;
}