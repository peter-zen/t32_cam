/*********************************************************************
 * INCLUDES
 */
#include "Base64.h"
#include <stddef.h>
#include <stdint.h>

static char base64_table[] = { 'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P', 'Q',
			       'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z', 'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h',
			       'i', 'j', 'k', 'l', 'm', 'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y',
			       'z', '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', '+', '/', '\0' };

/**
 * @brief Base64 encoding
 * @param pInData -[in] Source string
 * @param input_data_len -[in] Length of the source string
 * @param pOutData -[out] Encoded string
 * @param pOutLen -[out] Length of the encoded string
 * @return true - Success; false - Failure
 */
bool Base64::encode(char *input_data, uint32_t input_data_len, char *output_data, uint32_t *output_data_len)
{
	if (NULL == input_data || 0 == input_data_len) {
		return false;
	}

	uint32_t i = 0;
	uint32_t j = 0;
	uint32_t temp = 0;
	// Convert in groups of 3 bytes
	for (i = 0; i < input_data_len; i += 3) {
		// Get the first 6 bits
		temp = (*(input_data + i) >> 2) & 0x3F;
		*(output_data + j++) = base64_table[temp];

		// Get the first two bits of the second 6 bits
		temp = (*(input_data + i) << 4) & 0x30;
		// Special handling if there is only one character
		if (input_data_len <= (i + 1)) {
			*(output_data + j++) = base64_table[temp];
			*(output_data + j++) = '=';
			*(output_data + j++) = '=';
			break;
		}
		// Get the last four bits of the second 6 bits
		temp |= (*(input_data + i + 1) >> 4) & 0x0F;
		*(output_data + j++) = base64_table[temp];

		// Get the first four bits of the third 6 bits
		temp = (*(input_data + i + 1) << 2) & 0x3C;
		if (input_data_len <= (i + 2)) {
			*(output_data + j++) = base64_table[temp];
			*(output_data + j++) = '=';
			break;
		}
		// Get the last two bits of the third 6 bits
		temp |= (*(input_data + i + 2) >> 6) & 0x03;
		*(output_data + j++) = base64_table[temp];

		// Get the fourth 6 bits
		temp = *(input_data + i + 2) & 0x3F;
		*(output_data + j++) = base64_table[temp];
	}
	*(output_data + j) = '\0';
	// Length of encoded data
	*output_data_len = input_data_len * 8 / 6;
	return true;
}

/**
 * @brief Base64 decoding
 * @param pInData -[in] Source string
 * @param input_data_len -[in] Length of the source string
 * @param pOutData -[out] Decoded string
 * @param pOutLen -[out] Length of the decoded string
 * @return true - Success; false - Failure
 */
bool Base64::decode(char *input_data, uint32_t input_data_len, char *output_data, uint32_t *output_data_len)
{
	if (NULL == input_data || 0 == input_data_len || input_data_len % 4 != 0) {
		return false;
	}

	uint32_t i = 0;
	uint32_t j = 0;
	uint32_t k = 0;
	char temp[4] = "";
	// Convert in groups of 4 bytes
	for (i = 0; i < input_data_len; i += 4) {
		// Find the corresponding value in the encoding index table
		for (j = 0; j < 64; j++) {
			if (*(input_data + i) == base64_table[j]) {
				temp[0] = j;
			}
		}
		for (j = 0; j < 64; j++) {
			if (*(input_data + i + 1) == base64_table[j]) {
				temp[1] = j;
			}
		}
		for (j = 0; j < 64; j++) {
			if (*(input_data + i + 2) == base64_table[j]) {
				temp[2] = j;
			}
		}
		for (j = 0; j < 64; j++) {
			if (*(input_data + i + 3) == base64_table[j]) {
				temp[3] = j;
			}
		}

		// Combine the first 6 bits and the first two bits of the second 6 bits into
		// an 8-bit byte
		*(output_data + k++) = ((temp[0] << 2) & 0xFC) | ((temp[1] >> 4) & 0x03);
		if (*(input_data + i + 2) == '=') {
			break;
		}
		// Combine the first four bits of the second 6 bits and the first four bits
		// of the third 6 bits into an 8-bit byte
		*(output_data + k++) = ((temp[1] << 4) & 0xF0) | ((temp[2] >> 2) & 0x0F);
		if (*(input_data + i + 3) == '=') {
			break;
		}
		// Combine the last two bits of the third 6 bits and the fourth 6 bits into
		// an 8-bit byte
		*(output_data + k++) = ((temp[2] << 6) & 0xF0) | (temp[3] & 0x3F);
	}

	*output_data_len = k;

	return true;
}
