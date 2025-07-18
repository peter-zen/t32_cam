#ifndef BASE64_H
#define BASE64_H

#include <stdint.h>

class Base64 {
    public:
	static bool encode(char *input_data, uint32_t input_data_len, char *output_data, uint32_t *output_data_len);
	static bool decode(char *input_data, uint32_t input_data_len, char *output_data, uint32_t *output_data_len);

    private:
	Base64();
	~Base64();
	Base64(const Base64 &) = delete;
	Base64 &operator=(const Base64 &) = delete;
};

#endif
