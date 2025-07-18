#ifndef STRINGCONVERTER_H
#define STRINGCONVERTER_H

#include <string>
#include <cstdio>
#include <type_traits>
#include <cstdlib>
#include <cctype>
#include <climits>
#include <stdexcept>

#ifdef USE_UCLIBC
static int stoi_custom(const std::string& str, size_t* idx = nullptr, int base = 10)
{
    if (base < 2 || base > 36) {
        throw std::invalid_argument("Invalid base: base must be between 2 and 36");
    }

    const char* start = str.c_str();
    // 跳过前导空格
    while (isspace(*start)) {
        ++start;
    }

    // 处理符号
    int sign = 1;
    if (*start == '-') {
        sign = -1;
        ++start;
    } else if (*start == '+') {
        ++start;
    }

    char format[10];
    // 根据进制生成格式化字符串
    if (base == 16) {
        snprintf(format, sizeof(format), "%%x");
    } else if (base == 8) {
        snprintf(format, sizeof(format), "%%o");
    } else if (base == 2) {
        // 标准库不支持直接的二进制输入，需要手动处理
        int result = 0;
        const char* p = start;
        while (*p == '0' || *p == '1') {
            result = (result << 1) + (*p - '0');
            ++p;
        }
        if (idx != nullptr) {
            *idx = p - str.c_str();
        }
        if (result * sign < INT_MIN || result * sign > INT_MAX) {
            throw std::out_of_range("stoi_custom: out of range");
        }
        return result * sign;
    } else {
        snprintf(format, sizeof(format), "%%d");
    }

    int result;
    char* end;
    // 尝试转换
    int items_read = sscanf(start, format, &result);
    if (items_read != 1) {
        throw std::invalid_argument("stoi_custom: no conversion");
    }

    // 找到转换结束的位置
    end = const_cast<char*>(start);
    while (isalnum(*end)) {
        ++end;
    }

    if (idx != nullptr) {
        *idx = end - str.c_str();
    }

    // 检查溢出
    if (result * sign < INT_MIN || result * sign > INT_MAX) {
        throw std::out_of_range("stoi_custom: out of range");
    }

    return result * sign;
}

template <typename T, typename std::enable_if<std::is_integral<T>::value, int>::type = 0>
std::string to_string_custom(T value) {
    // 预留足够的空间，对于 64 位整数，十进制表示最多 20 位（含符号），再加上终止符
    char buffer[21]; 
    snprintf(buffer, sizeof(buffer), "%lld", static_cast<long long>(value));
    return std::string(buffer);
}

// 处理浮点类型
template <typename T, typename std::enable_if<std::is_floating_point<T>::value, int>::type = 0>
std::string to_string_custom(T value) {
    // 预留足够的空间，浮点数通常 32 位足够
    char buffer[32]; 
    // 控制小数点后 6 位
    snprintf(buffer, sizeof(buffer), "%f", static_cast<double>(value)); 
    return std::string(buffer);
}
#else
static int stoi_custom(const std::string& str, size_t* idx = nullptr, int base = 10)
{
    return std::stoi(str, idx, base);
}

template <typename T>
std::string to_string_custom(T value) {
    return std::to_string(value);
}
#endif

#endif // STRINGCONVERTER_H