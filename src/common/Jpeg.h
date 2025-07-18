#ifndef JPEG_H
#define JPEG_H

#include <string>
#include <memory>
#include <vector>

class Jpeg {
    public:
	static const std::vector<char> extractThumbnail(const std::string &filename);
};

#endif
