#include "Usb4gDongle.h"
#include "Common.h"
#include "Logger.h"
#include "StringConvert.h"
using namespace network;

Usb4gDongle::Usb4gDongle()
{
	open();
}

Usb4gDongle::~Usb4gDongle()
{
	close();
}

std::shared_ptr<Usb4gDongle> Usb4gDongle::getInstance()
{
	static std::shared_ptr<Usb4gDongle> instance = nullptr;
	static std::once_flag flag;
	std::call_once(flag, []() { instance.reset(new Usb4gDongle()); });
	return instance;
}

void Usb4gDongle::setModel(const Usb4gDongleModel &model)
{
	this->model = model;
}

bool Usb4gDongle::open()
{
	if (this->model == Usb4gDongleModel::EC20) {
		Logger::log(LogLevel::INFO, "Opening EC20 dongle on /dev/ttyUSB2");
		return this->io.open("/dev/ttyUSB2", 115200);
	} else if (this->model == Usb4gDongleModel::EC200) {
		Logger::log(LogLevel::INFO, "Opening EC200 dongle on /dev/ttyUSB1");
		return this->io.open("/dev/ttyUSB1", 115200);
	}

	Logger::log(LogLevel::ERROR, "Unknown dongle model");
	return false;
}

bool Usb4gDongle::close()
{
	return this->io.close();
}

bool Usb4gDongle::getSimNumber(std::string &sim_number)
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

		if (buff.find("+CNUM") == std::string::npos) {
			Logger::log(LogLevel::DEBUG, "CNUM response missing +CNUM marker");
			return false;
		}

		size_t start_pos = buff.find(",\"");
		if (start_pos == std::string::npos) {
			Logger::log(LogLevel::DEBUG, "CNUM response format error: missing phone number start");
			return false;
		}
		start_pos += 2; /* Skip the ," characters */

		size_t end_pos = buff.find("\",", start_pos);
		if (end_pos == std::string::npos) {
			Logger::log(LogLevel::DEBUG, "CNUM response format error: missing phone number end");
			return false;
		}

		sim_number = buff.substr(start_pos, end_pos - start_pos);
		return true; /* Success */
	}

	Logger::log(LogLevel::ERROR, "Failed to get SIM number after %d retries", max_retries);
	return false;
}

void Usb4gDongle::clear()
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

bool Usb4gDongle::setEcho(bool enable)
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
		if (buff.find("OK") != std::string::npos) {
			Logger::log(LogLevel::DEBUG,  "Echo set successfully");
			return true;
		}
	}

	Logger::log(LogLevel::ERROR, "Failed to set echo");
	return false;
}

bool Usb4gDongle::setErrMsgFmt(int format)
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
		if (buff.find("OK") != std::string::npos) {
			Logger::log(LogLevel::DEBUG,  "Error message format set successfully");
			return true;
		}
	}

	Logger::log(LogLevel::ERROR, "Failed to set error message format");
	return false;
}

bool Usb4gDongle::setNetType(int type)
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
		if (buff.find("OK") != std::string::npos) {
			Logger::log(LogLevel::INFO, "Network type set to %d successfully", type);
			return true;
		}
	}

	Logger::log(LogLevel::ERROR, "Failed to set network type");
	return false;
}

bool Usb4gDongle::setDevCtrl(int type, int cid)
{
	std::string command = "AT+QNETDEVCTL=" + to_string_custom(type) + "," + to_string_custom(cid) + "\r\n";
	if (!this->io.write(command)) {
		return false;
	}

	std::string buff;
	if (this->io.read(buff, 5000)) {
		if (buff.find("OK") != std::string::npos) {
			return true;
		}
	}

	return false;
}

bool Usb4gDongle::setNetScanMode(int scanmode, int effect)
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
		if (buff.find("OK") != std::string::npos) {
			Logger::log(LogLevel::INFO, "Scan mode set to %d successfully", scanmode);
			return true;
		}
	}

	Logger::log(LogLevel::ERROR, "Failed to set scan mode");
	return false;
}

bool Usb4gDongle::setWakeupConfig()
{
	std::string command;
	std::string desire;

	if (this->model == Usb4gDongleModel::EC20) {
		/* AT+QINDCFG="ring" */
		desire = "+QINDCFG: \"ring\",1";
		command = "AT+QINDCFG=\"ring\"\r\n";
		if (!this->io.write(command)) {
			return false;
		}

		std::string buff;
		if (this->io.read(buff, 5000)) {
			if (buff.find(desire) == std::string::npos) {
				command = "AT+QINDCFG=\"ring\",1\r\n";
				if (!this->io.write(command)) {
					return false;
				}

				if (this->io.read(buff, 5000)) {
					if (buff.find("OK") != std::string::npos) {
						return true;
					}
				}
			}
		}

	} else if (this->model == Usb4gDongleModel::EC200) {
		/* AT+QINDCFG="call" */
		desire = "+QINDCFG: \"call\",1";
		command = "AT+QINDCFG=\"call\"\r\n";
		if (!this->io.write(command)) {
			return false;
		}

		std::string buff;
		if (this->io.read(buff, 5000)) {
			if (buff.find(desire) == std::string::npos) {
				command = "AT+QINDCFG=\"call\",1\r\n";
				if (!this->io.write(command)) {
					return false;
				}

				if (this->io.read(buff, 5000)) {
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
		if (buff.find(desire) == std::string::npos) {
			command = "AT+QINDCFG=\"smsincoming\",1\r\n";
			if (!this->io.write(command)) {
				return false;
			}

			if (this->io.read(buff, 5000)) {
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
		if (buff.find(desire) == std::string::npos) {
			command = "AT+QCFG=\"urc/ri/ring\",\"pulse\",120,1\r\n";
			if (!this->io.write(command)) {
				return false;
			}

			if (this->io.read(buff, 5000)) {
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
		if (buff.find(desire) == std::string::npos) {
			command = "AT+QCFG=\"risignaltype\",\"physical\"\r\n";
			if (!this->io.write(command)) {
				return false;
			}

			if (this->io.read(buff, 5000)) {
				if (buff.find("OK") != std::string::npos) {
					return true;
				}
			}
		}
	}

	return false;
}
