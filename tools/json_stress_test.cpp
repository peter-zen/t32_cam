// json_stress_test.cpp - 模拟 generateDescInfo 行为的最小压力测试
//
// 目的: 验证 jsoncpp 路径（Json::Value 构建 + writeString 序列化 + 文件写）是否独立触发 zram
// 跑法: 在 T32 上 build 后跑 /mnt/huntcam/bin/json_stress_test, 统计 dmesg zram 错误条数
//
// 不依赖任何 SDK / HAL / CameraRecorder,纯用户态 JSON 库压力

#include <json/json.h>
#include <fstream>
#include <string>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <chrono>

static std::string generateDescJson() {
    // 模拟 main_app.cpp generateDescInfo 的结构（约 30+ JSON 字段）
    Json::Value json_root;
    json_root["F_UploadedTag"] = 0;

    // file_inf 数组（1 个文件 + 5 字段）
    Json::Value file_inf_array(Json::arrayValue);
    {
        Json::Value file_item;
        file_item["F_FilePath"] = "/mnt/sdcard/media/20260609_120000.mp4";
        file_item["F_FileName"] = "20260609_120000.mp4";
        file_item["F_FileTime"] = "2026-06-09 12:00:00";
        file_item["F_UploadedTag"] = 0;
        file_item["F_CheckCode"] = 12345;
        file_inf_array.append(file_item);
    }
    json_root["file_inf"] = file_inf_array;

    // device 对象（20+ 字段，模拟真实 desc 信息）
    Json::Value device_obj;
    device_obj["PID"] = "ABCD1234";
    device_obj["EUID"] = "";
    device_obj["IP"] = "192.168.1.100";
    device_obj["GP"] = "";  // 模拟 mcu 失败时的空字符串
    device_obj["Battery1"] = 0;
    device_obj["Battery2"] = 0;
    device_obj["SPower"] = "0";
    device_obj["EPower"] = "0";
    char mem_buf[32];
    snprintf(mem_buf, sizeof(mem_buf), "12.3/64.0 G");
    device_obj["Memory"] = mem_buf;
    device_obj["WMode"] = 0;
    device_obj["ONTime"] = 30;
    device_obj["NStatus"] = 0;
    device_obj["AStatus"] = 22;
    device_obj["BAT1_Level"] = 0;
    device_obj["Low_PWR_Val"] = 0;
    device_obj["Loff_PWR_Val"] = 0;
    device_obj["UTime"] = "2026-06-09 12:00:00";
    device_obj["FileSize"] = 23000000;  // ~23MB mp4
    json_root["device"] = device_obj;

    // 序列化
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    return Json::writeString(builder, json_root);
}

int main(int argc, char* argv[]) {
    int iterations = (argc > 1) ? std::atoi(argv[1]) : 1;
    const char* out_path = (argc > 2) ? argv[2] : "/tmp/json_stress_test.json";

    auto t0 = std::chrono::steady_clock::now();

    std::string lastJson;
    for (int i = 0; i < iterations; i++) {
        std::string json = generateDescJson();
        lastJson = std::move(json);  // 保留最后一个不被释放
    }

    auto t1 = std::chrono::steady_clock::now();
    auto build_ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
    fprintf(stderr, "json_stress_test: built %d JSON(s) in %lld ms (size=%zu bytes)\n",
            iterations, (long long)build_ms, lastJson.size());

    // 写文件（模拟 writeWorkModeDescJson）
    std::ofstream ofs(out_path, std::ios::binary | std::ios::trunc);
    if (!ofs.is_open()) {
        fprintf(stderr, "json_stress_test: open %s failed\n", out_path);
        return 1;
    }
    ofs.write(lastJson.data(), static_cast<std::streamsize>(lastJson.size()));
    ofs.close();

    auto t2 = std::chrono::steady_clock::now();
    auto write_ms = std::chrono::duration_cast<std::chrono::milliseconds>(t2 - t1).count();
    fprintf(stderr, "json_stress_test: wrote %zu bytes to %s in %lld ms\n",
            lastJson.size(), out_path, (long long)write_ms);

    return 0;
}
