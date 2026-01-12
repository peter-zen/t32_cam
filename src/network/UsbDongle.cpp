#include "UsbDongle.h"
#include "Common.h"
#include "Logger.h"
#include "StringConvert.h"
#include <iostream>
#include <string>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <vector>
#include <cstdio>
#include <mutex>
#include "Misc.h"
using namespace network;



// 检查命令是否存在
bool UsbDongle::have(const std::string& cmd) {
    std::string command = "command -v " + cmd + " > /dev/null 2>&1";
    return Misc::syscall(command.c_str(), 1000) == 0;
}

// 检查模块是否已加载
bool UsbDongle::loaded(const std::string& module_name) {
    // 使用grep匹配行首的模块名，后面可以跟任意字符（包括空格、数字等）
    // 移除-x选项，因为我们不要求完全匹配整行
    std::string command = "grep -q '^" + module_name + "' /proc/modules";
    return Misc::syscall(command.c_str(), 1000) == 0;
}

// 加载单个模块
bool UsbDongle::load_one(const std::string& module_name) {
    std::string moddir = "/system/modules/usb_dongle";
    
    // 首先尝试使用modprobe
    if (have("modprobe")) {
        std::string command = "modprobe " + module_name + " > /dev/null 2>&1";
        int ret = Misc::syscall(command.c_str(), 1000);
        if (ret != 0) {
            Logger::log(LogLevel::ERROR, "failed %s", module_name.c_str());
			return false;
        }
        
        if (loaded(module_name)) {
            Logger::log(LogLevel::INFO, "loaded %s", module_name.c_str());
            return true;
        }
    }
    
    // 如果modprobe失败或不存在，尝试使用insmod直接加载
    std::string find_command = "find " + moddir + " -type f -name \"" + module_name + ".ko\" 2>/dev/null";
    char buffer[256] = {0};
    
    int ret = Misc::popencall((char*)find_command.c_str(), buffer, sizeof(buffer), 1000);
    if (ret < 0) {
        Logger::log(LogLevel::ERROR, "Error executing command: %s", find_command.c_str());
        return false;
    }
    
    std::string module_path(buffer);
    // 只取第一行（移除head -n 1命令的依赖）
    size_t newline_pos = module_path.find('\n');
    if (newline_pos != std::string::npos) {
        module_path = module_path.substr(0, newline_pos);
    }
    
    // 移除换行符
    if (!module_path.empty() && module_path.back() == '\n') {
        module_path.pop_back();
    }
    
    if (!module_path.empty()) {
        std::string command = "insmod " + module_path + " > /dev/null 2>&1";
        int ret = Misc::syscall(command.c_str(), 1000);
        if (ret != 0) {
            Logger::log(LogLevel::ERROR, "failed %s", module_name.c_str());
        }
        
        if (loaded(module_name)) {
            Logger::log(LogLevel::INFO, "loaded %s", module_name.c_str());
            return true;
        }
    } else {
        Logger::log(LogLevel::ERROR, "missing %s", module_name.c_str());
    }
    
    return false;
}

// 定义类的静态成员变量
network::UsbDongle::ErrorMessage network::UsbDongle::cme_error[] = {
	{0, "Phone failure"},
	{1, "No connection to phone"},
	{2, "Phone-adaptor link reserved"},
	{3, "Operation not allowed"},
	{4, "Operation not supported"},
	{5, "PH-SIM PIN required"},
	{6, "PH-FSIM PIN required"},
	{7, "PH-FSIM PUK required"},
	{10, "(U)SIM not inserted"},
	{11, "(U)SIM PIN required"},
	{12, "(U)SIM PUK required"},
	{13, "(U)SIM failure"},
	{14, "(U)SIM busy"},
	{15, "(U)SIM wrong"},
	{16, "Incorrect password"},
	{17, "(U)SIM PIN2 required"},
	{18, "(U)SIM PUK2 required"},
	{20, "Memory full"},
	{21, "Invalid index"},
	{22, "Not found"},
	{23, "Memory failure"},
	{24, "Text string too long"},
	{25, "Invalid characters in text string"},
	{26, "Dial string too long"},
	{27, "Invalid characters in dial string"},
	{30, "No network service"},
	{31, "Network timeout"},
	{32, "Network not allowed - emergency calls only"},
	{40, "Network personalization PIN required"},
	{41, "Network personalization PUK required"},
	{42, "Network subset personalization PIN required"},
	{43, "Network subset personalization PUK required"},
	{44, "Service provider personalization PIN required"},
	{45, "Service provider personalization PUK required"},
	{46, "Corporate personalization PIN required"},
	{47, "Corporate personalization PUK required"},
	{901, "Audio unknown error"},
	{902, "Audio invalid parameters"},
	{903, "Audio operation not supported"},
	{904, "Audio device busy"}
};

network::UsbDongle::ErrorMessage network::UsbDongle::cms_error[] = {
	{300, "ME failure"},
	{301, "SMS ME reserved"},
	{302, "Operation not allowed"},
	{303, "Operation not supported"},
	{304, "Invalid PDU mode"},
	{305, "Invalid text mode"},
	{310, "(U)SIM not inserted"},
	{311, "(U)SIM pin necessary"},
	{312, "PH SIM pin necessary"},
	{313, "(U)SIM failure"},
	{314, "(U)SIM busy"},
	{315, "(U)SIM wrong"},
	{316, "(U)SIM PUK required"},
	{317, "(U)SIM PIN2 required"},
	{318, "(U)SIM PUK2 required"},
	{320, "Memory failure"},
	{321, "Invalid memory index"},
	{322, "Memory full"},
	{330, "SMSC address unknown"},
	{331, "No network"},
	{332, "Network timeout"},
	{500, "Unknown"},
	{512, "(U)SIM not ready"},
	{513, "Message length exceeds"},
	{514, "Invalid request parameters"},
	{515, "ME storage failure"},
	{517, "Invalid service mode"},
	{528, "More message to send state error"},
	{529, "MO SMS is not allowed"},
	{530, "GPRS is suspended"},
	{531, "ME storage full"}
};

const char *UsbDongle::usb4g_model_name(UsbDongleModel model)
{
	switch (model) {
		case UsbDongleModel::EC20: return "EC20";
		case UsbDongleModel::EC200A: return "EC200A";
		case UsbDongleModel::EG800K: return "EG800K";
		case UsbDongleModel::RG255AA: return "RG255AA";
		default: return "UNKNOWN";
	}
}

UsbDongle::UsbDongle()
{
	simPinCode.clear();
}

UsbDongle::~UsbDongle()
{
	close();
}

std::shared_ptr<UsbDongle> UsbDongle::getInstance()
{
	static std::shared_ptr<UsbDongle> instance = nullptr;
	static std::once_flag flag;
	std::call_once(flag, []() { instance.reset(new UsbDongle()); });
	return instance;
}

void UsbDongle::setModel(const UsbDongleModel &model)
{
	this->model = model;
}
bool UsbDongle::probe()
{
	const char *candidates[] = {
		"/dev/ttyUSB0",
		"/dev/ttyUSB1",
		"/dev/ttyUSB2",
		"/dev/ttyUSB3",
		"/dev/ttyUSB4",
		"/dev/ttyUSB5",
		"/dev/ttyUSB6"
	};

	for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); ++i) {
		const char *dev = candidates[i];
		SerialPort probe;
		if (!probe.open(dev, 115200)) {
			continue;
		}
		if (!probe.write("AT\r\n")) {
			probe.close();
			continue;
		}
		std::string resp;
		if (!probe.read(resp, 500)) {
			probe.close();
			continue;
		}
		probe.close();
		if (resp.find("OK") != std::string::npos) {
			Logger::log(LogLevel::INFO, "Opening %s dongle on %s", usb4g_model_name(this->model), dev);
			return this->io.open(dev, 115200);
		}
	}

	Logger::log(LogLevel::ERROR, "Failed to detect AT port for %s dongle", usb4g_model_name(this->model));
	return false;
}

bool UsbDongle::open(const std::string &device_name)
{
	if (!device_name.empty()) {
		return this->io.open(device_name, 115200);
	}

	return probe();
}

bool UsbDongle::close()
{
	return this->io.close();
}

bool UsbDongle::getSimNumber(std::string &sim_number)
{
	static const char *cmd = "AT+CNUM\r\n";
	static const int max_retries = 10;
	static const int timeout_ms = 5000;

	if (!this->io.write(cmd)) {
		Logger::log(LogLevel::ERROR, "Failed to write AT+CNUM command");
		return false;
	}

	for (int retry = max_retries; retry > 0; retry--) {
		std::string buff;
		if (!this->io.read(buff, timeout_ms)) {
			continue;
		}
		if (echoError(buff)) {
			continue;
		}
		if (buff.find("+CNUM") == std::string::npos) {
			Logger::log(LogLevel::ERROR, "CNUM response missing +CNUM marker");
			return false;
		}

		size_t start_pos = buff.find(",\"");
		if (start_pos == std::string::npos) {
			Logger::log(LogLevel::ERROR, "CNUM response format error: missing phone number start");
			return false;
		}
		start_pos += 2; /* Skip the ," characters */

		size_t end_pos = buff.find("\",", start_pos);
		if (end_pos == std::string::npos) {
			Logger::log(LogLevel::ERROR, "CNUM response format error: missing phone number end");
			return false;
		}

		sim_number = buff.substr(start_pos, end_pos - start_pos);
		return true; /* Success */
	}

	Logger::log(LogLevel::ERROR, "Failed to get SIM number after %d retries", max_retries);
	return false;
}

void UsbDongle::clear()
{
	Logger::log(LogLevel::DEBUG,  "Clearing input buffer");
	std::string buff;
	int count = 0;
	do {
		if (!this->io.read(buff, 512)) {
			break;
		}
	} while (count++ < 10);

	if (count > 0) {
		Logger::log(LogLevel::VERBOSE, "Cleared %d buffer(s)", count);
	}
}

bool UsbDongle::setEcho(bool enable)
{
	std::string command = "ATE" + to_string_custom(enable) + "\r\n";
	Logger::log(LogLevel::DEBUG,  "Setting echo: %s", enable ? "ON" : "OFF");

	if (!this->io.write(command)) {
		Logger::log(LogLevel::ERROR, "Failed to write ATE command");
		return false;
	}

	std::string buff;
	static const int timeout_ms = 5000;
	if (this->io.read(buff, timeout_ms)) {
		if (echoError(buff)) {
			Logger::log(LogLevel::ERROR, "Failed to set echo: %s", buff.c_str());
			return false;
		}
		if (buff.find("OK") != std::string::npos) {
			Logger::log(LogLevel::DEBUG,  "Echo set successfully");
			return true;
		}
	}

	Logger::log(LogLevel::ERROR, "Failed to set echo");
	return false;
}

bool UsbDongle::setErrMsgFmt(int format)
{
	std::string command = "AT+CMEE=" + to_string_custom(format) + "\r\n";
	Logger::log(LogLevel::DEBUG,  "Setting error message format: %d", format);

	if (!this->io.write(command)) {
		Logger::log(LogLevel::ERROR, "Failed to write CMEE command");
		return false;
	}

	std::string buff;
	static const int timeout_ms = 5000;
	if (this->io.read(buff, timeout_ms)) {
		if (echoError(buff)) {
			Logger::log(LogLevel::ERROR, "Failed to set error message format: %s", buff.c_str());
			return false;
		}
		if (buff.find("OK") != std::string::npos) {
			Logger::log(LogLevel::DEBUG,  "Error message format set successfully");
			return true;
		}
	}

	Logger::log(LogLevel::ERROR, "Failed to set error message format");
	return false;
}

bool UsbDongle::setNetType(int type)
{
	std::string desire = "+QCFG: \"usbnet\"," + to_string_custom(type);
	Logger::log(LogLevel::DEBUG,  "Setting network type: %d", type);

	// read current setting
	std::string command = "AT+QCFG=\"usbnet\"\r\n";
	if (!this->io.write(command)) {
		Logger::log(LogLevel::ERROR, "Failed to query current network type");
		return false;
	}

	std::string buff;
	static const int timeout_ms = 5000;
	if (this->io.read(buff, timeout_ms)) {
		if (echoError(buff)) {
			Logger::log(LogLevel::ERROR, "Failed to query current network type: %s", buff.c_str());
			return false;
		}
		if (buff.find(desire) != std::string::npos) {
			Logger::log(LogLevel::DEBUG,  "Network type already set to %d", type);
			return true;
		}
	}

	// set new network type
	command = "AT+QCFG=\"usbnet\"," + to_string_custom(type) + "\r\n";
	if (!this->io.write(command)) {
		Logger::log(LogLevel::ERROR, "Failed to write network type command");
		return false;
	}

	if (this->io.read(buff, timeout_ms)) {
		if (echoError(buff)) {
			Logger::log(LogLevel::ERROR, "Failed to set network type: %s", buff.c_str());
			return false;
		}
		if (buff.find("OK") != std::string::npos) {
			Logger::log(LogLevel::INFO, "Network type set to %d successfully", type);
			return true;
		}
	}

	Logger::log(LogLevel::ERROR, "Failed to set network type");
	return false;
}

bool UsbDongle::setDevCtrl(int type, int cid)
{
	std::string command = "AT+QNETDEVCTL=" + to_string_custom(type) + "," + to_string_custom(cid) + "\r\n";
	if (!this->io.write(command)) {
		return false;
	}

	std::string buff;
	if (this->io.read(buff, 5000)) {
		if (echoError(buff)) {
			Logger::log(LogLevel::ERROR, "Failed to set device control: %s", buff.c_str());
			return false;
		}
		if (buff.find("OK") != std::string::npos) {
			return true;
		}
	}

	return false;
}

bool UsbDongle::setNetScanMode(int scanmode, int effect)
{
	std::string desire = "+QCFG: \"nwscanmode\"," + to_string_custom(scanmode);
	Logger::log(LogLevel::DEBUG,  "Setting network scan mode: %d (effect: %d)", scanmode, effect);

	// read current setting
	std::string command = "AT+QCFG=\"nwscanmode\"\r\n";
	if (!this->io.write(command)) {
		Logger::log(LogLevel::ERROR, "Failed to query current scan mode");
		return false;
	}

	std::string buff;
	static const int timeout_ms = 5000;
	if (this->io.read(buff, timeout_ms)) {
		if (echoError(buff)) {
			Logger::log(LogLevel::ERROR, "Failed to query current scan mode: %s", buff.c_str());
			return false;
		}
		if (buff.find(desire) != std::string::npos) {
			Logger::log(LogLevel::DEBUG,  "Scan mode already set to %d", scanmode);
			return true;
		}
	}

	// set new scan mode
	command = "AT+QCFG=\"nwscanmode\"," + to_string_custom(scanmode) + "," + to_string_custom(effect) + "\r\n";
	if (!this->io.write(command)) {
		Logger::log(LogLevel::ERROR, "Failed to write scan mode command");
		return false;
	}

	if (this->io.read(buff, timeout_ms)) {
		if (echoError(buff)) {
			Logger::log(LogLevel::ERROR, "Failed to set scan mode: %s", buff.c_str());
			return false;
		}
		if (buff.find("OK") != std::string::npos) {
			Logger::log(LogLevel::INFO, "Scan mode set to %d successfully", scanmode);
			return true;
		}
	}

	Logger::log(LogLevel::ERROR, "Failed to set scan mode");
	return false;
}

bool UsbDongle::setWakeupConfig()
{
	std::string command;
	std::string desire;

	if (this->model == UsbDongleModel::EC20) {
		/* AT+QINDCFG="ring" */
		desire = "+QINDCFG: \"ring\",1";
		command = "AT+QINDCFG=\"ring\"\r\n";
		if (!this->io.write(command)) {
			return false;
		}

		std::string buff;
		if (this->io.read(buff, 5000)) {
			if (echoError(buff)) {
				Logger::log(LogLevel::ERROR, "Failed to query ring config: %s", buff.c_str());
				return false;
			}
			if (buff.find(desire) == std::string::npos) {
				command = "AT+QINDCFG=\"ring\",1\r\n";
				if (!this->io.write(command)) {
					return false;
				}

				if (this->io.read(buff, 5000)) {
					if (echoError(buff)) {
						Logger::log(LogLevel::ERROR, "Failed to set ring config: %s", buff.c_str());
						return false;
					}
					if (buff.find("OK") != std::string::npos) {
						return true;
					}
				}
			}
		}

	} else if (this->model == UsbDongleModel::EC200A || this->model == UsbDongleModel::EG800K) {
		/* AT+QINDCFG="call" */
		desire = "+QINDCFG: \"call\",1";
		command = "AT+QINDCFG=\"call\"\r\n";
		if (!this->io.write(command)) {
			return false;
		}

		std::string buff;
		if (this->io.read(buff, 5000)) {
			if (echoError(buff)) {
				Logger::log(LogLevel::ERROR, "Failed to query call config: %s", buff.c_str());
				return false;
			}
			if (buff.find(desire) == std::string::npos) {
				command = "AT+QINDCFG=\"call\",1\r\n";
				if (!this->io.write(command)) {
					return false;
				}

				if (this->io.read(buff, 5000)) {
					if (echoError(buff)) {
						Logger::log(LogLevel::ERROR, "Failed to set call config: %s", buff.c_str());
						return false;
					}
					if (buff.find("OK") != std::string::npos) {
						return true;
					}
				}
			}
		}
	}

	/* AT+QINDCFG="smsincoming" */
	desire = "+QINDCFG: \"smsincoming\",1";
	command = "AT+QINDCFG=\"smsincoming\"\r\n";
	if (!this->io.write(command)) {
		return false;
	}
	
	std::string buff;
	if (this->io.read(buff, 5000)) {
		if (echoError(buff)) {
			Logger::log(LogLevel::ERROR, "Failed to query smsincoming config: %s", buff.c_str());
			return false;
		}
		if (buff.find(desire) == std::string::npos) {
			command = "AT+QINDCFG=\"smsincoming\",1\r\n";
			if (!this->io.write(command)) {
				return false;
			}

			if (this->io.read(buff, 5000)) {
				if (echoError(buff)) {
					Logger::log(LogLevel::ERROR, "Failed to set smsincoming config: %s", buff.c_str());
					return false;
				}
				if (buff.find("OK") != std::string::npos) {
					return true;
				}
			}
		}
	}

	/* AT+QCFG="urc/ri/ring" */
	desire = "+QCFG: \"urc/ri/ring\",\"pulse\",120,1,";
	command = "AT+QCFG=\"urc/ri/ring\"\r\n";
	if (!this->io.write(command)) {
		return false;
	}

	// read back
	command = "AT+QCFG=\"urc/ri/ring\"\r\n";
	if (!this->io.write(command)) {
		return false;
	}

	if (this->io.read(buff, 5000)) {
		if (echoError(buff)) {
			Logger::log(LogLevel::ERROR, "Failed to query urc/ri/ring config: %s", buff.c_str());
			return false;
		}
		if (buff.find(desire) == std::string::npos) {
			command = "AT+QCFG=\"urc/ri/ring\",\"pulse\",120,1\r\n";
			if (!this->io.write(command)) {
				return false;
			}

			if (this->io.read(buff, 5000)) {
				if (echoError(buff)) {
					Logger::log(LogLevel::ERROR, "Failed to set urc/ri/ring config: %s", buff.c_str());
					return false;
				}
				if (buff.find("OK") != std::string::npos) {
					return true;
				}
			}
		}
	}

	/* AT+QCFG="risignaltype" */
	desire = "+QCFG: \"risignaltype\",\"physical\"";
	command = "AT+QCFG=\"risignaltype\"\r\n";
	if (!this->io.write(command)) {
		return false;
	}

	// read back
	command = "AT+QCFG=\"risignaltype\"\r\n";
	if (!this->io.write(command)) {
		return false;
	}

	if (this->io.read(buff, 5000)) {
		if (echoError(buff)) {
			Logger::log(LogLevel::ERROR, "Failed to query risignaltype config: %s", buff.c_str());
			return false;
		}
		if (buff.find(desire) == std::string::npos) {
			command = "AT+QCFG=\"risignaltype\",\"physical\"\r\n";
			if (!this->io.write(command)) {
				return false;
			}

			if (this->io.read(buff, 5000)) {
				if (echoError(buff)) {
					Logger::log(LogLevel::ERROR, "Failed to set risignaltype config: %s", buff.c_str());
					return false;
				}
				if (buff.find("OK") != std::string::npos) {
					return true;
				}
			}
		}
	}

	return false;
}

bool UsbDongle::getIP(std::string &ip)
{
	std::string command = "AT+CGPADDR=1\r\n";
	if (!this->io.write(command)) {
		return false;
	}
	for (int retry = 10; retry > 0; retry--) {
		std::string buff;
		if (this->io.read(buff, 5000)) {
			if (echoError(buff)) {
				Logger::log(LogLevel::ERROR, "Failed to get IP: %s", buff.c_str());
				continue;
			}
			size_t pos = buff.find("+CGPADDR:");
			if (pos == std::string::npos) {
				continue;
			}
			pos = buff.find(",\"", pos);
			if (pos == std::string::npos) {
				continue;
			}
			pos += 2;
			size_t end = buff.find("\"", pos);
			if (end == std::string::npos || end <= pos) {
				continue;
			}
			ip = buff.substr(pos, end - pos);
			return true;
		}
	}
	return false;
}

bool UsbDongle::ping(const std::string &ip)
{
	std::string command = "AT+QPING=1," + ip + "\r\n";
	if (!this->io.write(command)) {
		return false;
	}
	for (int retry = 1; retry > 0; retry--) {
		std::string buff;
		if (this->io.read(buff, 5000)) {
			if (echoError(buff)) {
				Logger::log(LogLevel::ERROR, "Failed to ping: %s", buff.c_str());
				continue;
			}
			if (buff.find("OK") != std::string::npos) {
				return true;
			}
			if (buff.find("ERROR") != std::string::npos) {
				continue;
			}
		}
	}
	return false;
}

bool UsbDongle::getSignal(int &signal)
{
	std::string command = "AT+CSQ\r\n";
	if (!this->io.write(command)) {
		return false;
	}
	for (int retry = 10; retry > 0; retry--) {
		std::string buff;
		if (this->io.read(buff, 5000)) {
			if (echoError(buff)) {
				Logger::log(LogLevel::ERROR, "Failed to get signal: %s", buff.c_str());
				continue;
			}
			if (buff.find("+CSQ:") == std::string::npos) {
				continue;
			}
			size_t idx = 0;
			while (idx < buff.size() && !(buff[idx] >= '0' && buff[idx] <= '9')) {
				idx++;
			}
			if (idx >= buff.size()) {
				continue;
			}
			std::string s = buff.substr(idx, 2);
			signal = stoi_custom(s);
			return true;
		}
	}
	return false;
}

int UsbDongle::getScanMode()
{
	std::string command = "AT+QCFG=\"nwscanmode\"\r\n";
	if (!this->io.write(command)) {
		return -1;
	}
	for (int retry = 10; retry > 0; retry--) {
		std::string buff;
		if (this->io.read(buff, 5000)) {
			if (echoError(buff)) {
				Logger::log(LogLevel::ERROR, "Failed to get scan mode: %s", buff.c_str());
				continue;
			}
			if (buff.find("+QCFG: \"nwscanmode\"") == std::string::npos) {
				continue;
			}
			size_t idx = 0;
			while (idx < buff.size() && !(buff[idx] >= '0' && buff[idx] <= '9')) {
				idx++;
			}
			if (idx >= buff.size()) {
				continue;
			}
			std::string s = buff.substr(idx, 1);
			return stoi_custom(s);
		}
	}
	return -1;
}

bool UsbDongle::loadDriver() {
    // 首先运行depmod -a
    if (have("depmod")) {
        std::string command = "depmod -a > /dev/null 2>&1";
        Misc::syscall(command.c_str());
    }
    
    // 需要加载的USB-Dongle相关模块列表
    std::vector<std::string> modules = {
        "usbnet",
        "cdc_ether",
        "cdc_ncm",
        "cdc_mbim",
        "rndis_host",
        "qmi_wwan",
        "usbserial",
        "usb_wwan",
        "option"
    };
    
    // 加载所有模块
    bool all_loaded = true;
    for (const auto& module : modules) {
        if (!load_one(module)) {
            all_loaded = false;
        }
    }
    
    return all_loaded;
}

bool UsbDongle::getAct(int &act)
{
	std::string command = "AT+QNWINFO\r\n";
	if (!this->io.write(command)) {
		return false;
	}
	for (int retry = 10; retry > 0; retry--) {
		std::string buff;
		if (this->io.read(buff, 5000)) {
			if (echoError(buff)) {
				Logger::log(LogLevel::ERROR, "Failed to get ACT: %s", buff.c_str());
				continue;
			}
			if (buff.find("OK") == std::string::npos) {
				continue;
			}
			size_t colon = buff.find(':');
			if (colon == std::string::npos || colon + 3 >= buff.size()) {
				return false;
			}
			colon += 3;
			size_t comma = buff.find(',', colon + 1);
			if (comma == std::string::npos || comma == 0) {
				return false;
			}
			if (comma <= colon) {
				return false;
			}
			std::string result = buff.substr(colon, comma - colon - 1);
			if (result == "GSM") act = 2;
			else if (result == "GPRS") act = 3;
			else if (result == "EDGE") act = 4;
			else if (result == "CDMA1X AND EHRPD") act = 8;
			else if (result == "HDR") act = 9;
			else if (result == "HDR-EHRPD") act = 10;
			else if (result == "HSDPA") act = 11;
			else if (result == "HSUPA") act = 12;
			else if (result == "HSPA+") act = 13;
			else if (result == "TDSCDMA") act = 15;
			else if (result == "LTE") act = 17;
			else if (result == "TDD LTE") act = 18;
			else if (result == "FDD LTE") act = 19;
			else if (result == "NR5G-SA") act = 20;
			else if (result == "TDD NR5G") act = 21;
			else if (result == "FDD NR5G") act = 22;
			else act = -1;
			return true;
		}
	}
	return false;
}

bool UsbDongle::sleep()
{
	if (!setEcho(0)) return false;
	if (!setErrMsgFmt(1)) return false;
	std::string command = "AT+QSCLK=1\r\n";
	if (!this->io.write(command)) {
		return false;
	}
	std::string buff;
	if (this->io.read(buff, 5000)) {
		if (echoError(buff)) {
			Logger::log(LogLevel::ERROR, "Failed to set sleep mode: %s", buff.c_str());
			return false;
		}
		return buff.find("OK") != std::string::npos;
	}
	return false;
}

bool UsbDongle::powerOff()
{
	if (!setEcho(0)) return false;
	if (!setErrMsgFmt(1)) return false;
	std::string command = "AT+QPOWD=0\r\n";
	if (!this->io.write(command)) {
		return false;
	}
	std::string buff;
	if (this->io.read(buff, 5000)) {
		if (echoError(buff)) {
			Logger::log(LogLevel::ERROR, "Failed to power off: %s", buff.c_str());
			return false;
		}
		return buff.find("OK") != std::string::npos;
	}
	return false;
}

bool UsbDongle::querySimReady(int &status)
{
	std::string command = "AT+CPIN?\r\n";
	for (int retry = 3; retry > 0; retry--) {
		if (!this->io.write(command)) {
			return false;
		}
		std::string buff;
		if (this->io.read(buff, 5000)) {
			if (echoError(buff)) {
				Logger::log(LogLevel::ERROR, "Failed to query SIM ready status: %s", buff.c_str());
				continue;
			}
			if (buff.find("+CME ERROR: 10") != std::string::npos) {
				status = -2;
				return true;
			} else if (buff.find("+CME ERROR: 14") != std::string::npos) {
				status = -3;
				continue;
			} else if (buff.find("+CPIN: SIM PIN") != std::string::npos) {
				status = 1;
				return true;
			} else if (buff.find("+CPIN: READY") != std::string::npos) {
				status = 0;
				return true;
			}
		}
	}
	return false;
}

bool UsbDongle::setPinInternal(const std::string &pin)
{
	if (pin.empty()) return false;
	std::string command = "AT+CPIN=\"" + pin + "\"\r\n";
	if (!this->io.write(command)) {
		return false;
	}
	for (int retry = 10; retry > 0; retry--) {
		std::string buff;
		if (this->io.read(buff, 5000)) {
			if (echoError(buff)) {
				Logger::log(LogLevel::ERROR, "Failed to set PIN: %s", buff.c_str());
				continue;
			}
			if (buff.find("+CPIN: READY") != std::string::npos) {
				return true;
			}
		}
	}
	return false;
}

bool UsbDongle::getApn(std::string &apn)
{
	std::string command = "AT+CIMI\r\n";
	if (!this->io.write(command)) {
		return false;
	}
	for (int retry = 10; retry > 0; retry--) {
		std::string buff;
		if (this->io.read(buff, 5000)) {
			if (echoError(buff)) {
				Logger::log(LogLevel::ERROR, "Failed to get APN: %s", buff.c_str());
				continue;
			}
			size_t idx = 0;
			while (idx < buff.size() && !(buff[idx] >= '0' && buff[idx] <= '9')) {
				idx++;
			}
			if (idx >= buff.size()) {
				continue;
			}
			if (idx + 15 > buff.size()) {
				continue;
			}
			std::string imsi = buff.substr(idx, 15);
			if (imsi.size() == 15 && imsi[0] == '4' && imsi[1] == '6' && imsi[2] == '0') {
				if ((imsi[3] == '0' && (imsi[4] == '0' || imsi[4] == '2' || imsi[4] == '7')) || (imsi[3] == '2' && imsi[4] == '0')) {
					apn = "cmnet";
					return true;
				} else if ((imsi[3] == '0' && (imsi[4] == '1' || imsi[4] == '6' || imsi[4] == '9'))) {
					apn = "uninet";
					return true;
				} else if ((imsi[3] == '0' && (imsi[4] == '3' || imsi[4] == '5')) || (imsi[3] == '1' && imsi[4] == '1')) {
					apn = "ctnet";
					return true;
				}
			}
		}
	}
	return false;
}

bool UsbDongle::setContextProfile(const std::string &apn)
{
	if (apn.empty()) return false;
	std::string command = "AT+QICSGP=1,1,\"" + apn + "\",\"\",\"\",1\r\n";
	if (!this->io.write(command)) {
		return false;
	}
	for (int retry = 10; retry > 0; retry--) {
		std::string buff;
		if (this->io.read(buff, 5000)) {
			if (echoError(buff)) {
				Logger::log(LogLevel::ERROR, "Failed to set context profile: %s", buff.c_str());
				continue;
			}
			if (buff.find("OK") != std::string::npos) {
				return true;
			}
		}
	}
	return false;
}

bool UsbDongle::activateContextProfile()
{
	std::string command = "AT+QIACT=1\r\n";
	if (!this->io.write(command)) {
		return false;
	}
	for (int retry = 10; retry > 0; retry--) {
		std::string buff;
		if (this->io.read(buff, 5000)) {
			if (echoError(buff)) {
				Logger::log(LogLevel::ERROR, "Failed to activate context profile: %s", buff.c_str());
				continue;
			}
			if (buff.find("OK") != std::string::npos) {
				return true;
			}
			if (buff.find("ERROR") != std::string::npos) {
				return false;
			}
		}
	}
	return false;
}

bool UsbDongle::deactivateContextProfile()
{
	std::string command = "AT+QIDEACT=1\r\n";
	if (!this->io.write(command)) {
		return false;
	}
	for (int retry = 10; retry > 0; retry--) {
		std::string buff;
		if (this->io.read(buff, 5000)) {
			if (echoError(buff)) {
				Logger::log(LogLevel::ERROR, "Failed to deactivate context profile: %s", buff.c_str());
				continue;
			}
			if (buff.find("OK") != std::string::npos) {
				return true;
			}
		}
	}
	return false;
}

bool UsbDongle::start()
{
	clear();
	if (!setEcho(0)) return false;
	if (!setErrMsgFmt(1)) return false;
	int simStatus = -1;
	if (!querySimReady(simStatus)) return false;
	if (simStatus == 1) {
		if (!setPinInternal(simPinCode)) return false;
	}
	std::string apn;
	if (!getApn(apn)) return false;
	if (!setContextProfile(apn)) return false;
	if (!activateContextProfile()) return false;
	return true;
}

bool UsbDongle::stop()
{
	return deactivateContextProfile();
}

bool UsbDongle::preconfig()
{
	clear();
	if (!setEcho(0)) return false;
	if (!setErrMsgFmt(1)) return false;
	if (!setNetType(3)) return false;
	if (this->model == UsbDongleModel::EC200A || this->model == UsbDongleModel::EG800K) {
		if (!setDevCtrl(3, 1)) return false;
	}
	if (!setNetScanMode(3, 1)) return false;
	if (!setWakeupConfig()) return false;
	return true;
}

bool UsbDongle::setSimPin(const std::string &pin)
{
	if (pin.empty()) return false;
	simPinCode = pin;
	return true;
}

bool UsbDongle::echoError(const std::string &resp)
{
	if (resp.find("+CME ERROR:") != std::string::npos) {
		std::string code = resp.substr(resp.find("+CME ERROR:") + 11, 2);
		int code_int = stoi_custom(code);
		echoCMEError(code_int);
		return true;
	} else if (resp.find("+CMS ERROR:") != std::string::npos) {
		std::string code = resp.substr(resp.find("+CMS ERROR:") + 11, 2);
		int code_int = stoi_custom(code);
		echoCMSError(code_int);
		return true;
	}
	
	return false;
}

void UsbDongle::echoCMEError(int code)
{
	for (int i = 0; i < sizeof(cme_error) / sizeof(cme_error[0]); i++) {
		if (cme_error[i].code == code) {
			Logger::log(LogLevel::ERROR, "CME ERROR %d: %s", code, cme_error[i].message);
			return;
		}
	}
	Logger::log(LogLevel::ERROR, "CME ERROR %d: UNKNOWN", code);
}

void UsbDongle::echoCMSError(int code)
{
	for (int i = 0; i < sizeof(cms_error) / sizeof(cms_error[0]); i++) {
		if (cms_error[i].code == code) {
			Logger::log(LogLevel::ERROR, "CMS ERROR %d: %s", code, cms_error[i].message);
			return;
		}
	}
	Logger::log(LogLevel::ERROR, "CMS ERROR %d: UNKNOWN", code);
}
