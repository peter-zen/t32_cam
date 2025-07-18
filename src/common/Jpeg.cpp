#include "Jpeg.h"
#include <fstream>
#include <algorithm>
#include <iterator>
#include "Logger.h"

#define MAX_THM_SIZE            (64 * 1024)
const std::vector<char> Jpeg::extractThumbnail(const std::string &filename)
{
	int start_flag[2] = { 0xff, 0xd8 }; /* 2 is a number */
	int end_flag[2] = { 0xff, 0xd9 }; /* 2 is a number */

	std::ifstream file(filename, std::ios::binary);
	if (!file.is_open()) {
		Logger::log(LogLevel::ERROR, "Failed to open file: %s", filename.c_str());
		return std::vector<char>();
	}

	// Get file size
	file.seekg(0, std::ios::end);
	std::streamsize file_size = file.tellg();
	file.seekg(0, std::ios::beg);

	// Read file content into buffer
	std::vector<char> buffer(file_size);
	if (!file.read(buffer.data(), file_size)) {
		Logger::log(LogLevel::ERROR, "Failed to read file: %s", filename.c_str());
		return std::vector<char>();
	}

	// Search for JPEG start and end flags
	auto start_it = std::search(buffer.begin(), buffer.end(), std::begin(start_flag), std::end(start_flag));
	auto end_it = std::search(buffer.begin(), buffer.end(), std::begin(end_flag), std::end(end_flag));

	if (start_it == buffer.end() || end_it == buffer.end() || start_it >= end_it) {
		Logger::log(LogLevel::ERROR, "Failed to find JPEG start or end flags in file: %s", filename.c_str());
		return std::vector<char>();
	}

	// If the file starts with a JPEG start flag, skip the initial bytes
	if (start_it == buffer.begin()) {
		start_it = std::search(buffer.begin() + 2, buffer.end(), std::begin(start_flag), std::end(start_flag));
		if (start_it == buffer.end()) {
			Logger::log(LogLevel::ERROR, "Failed to find JPEG start flag after initial bytes in file: %s", filename.c_str());
			return std::vector<char>();
		}
	}

	// Calculate the start and end positions of the thumbnail
	std::size_t start_pos = std::distance(buffer.begin(), start_it);
	std::size_t end_pos = std::distance(buffer.begin(), end_it) + 2; // Include end flag

	// Check thumbnail size
	if (end_pos - start_pos > MAX_THM_SIZE || end_pos > static_cast<std::size_t>(file_size)) {
		Logger::log(LogLevel::ERROR, "Thumbnail size is too large or end position is out of bounds for file: %s", filename.c_str());
		return std::vector<char>();
	}

	// Copy data to a new vector
	std::vector<char> thumbnail;
	thumbnail.assign(buffer.data() + start_pos, buffer.data() + end_pos);

	return thumbnail;
}
