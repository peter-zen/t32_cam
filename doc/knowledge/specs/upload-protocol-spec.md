# T32 Camera 上传协议规范

**文档版本**: 1.0
**创建日期**: 2026-06-27
**适用范围**: T32 设备上传 desc + 媒体文件到 aidetcloud management server

---

## 1. 概述

本规范定义 T32 camera 设备与 aidetcloud management server 之间的**上传协议**(auth + 文件传输),覆盖拍照/录影产物(desc JSON + jpg/mp4 文件)从设备到 server 的完整链路。

读完本规范,任何语言的实现者都可以独立写出一个 client,完成"拍照/录影后 → 上传 desc + media file → server"端到端流程,无需阅读 T32 项目源码。

### 1.1 核心原则

- **单 socket 多路复用**:不另起端口,所有消息通过 12B header 中的 `msg_type` 字段区分(auth / upload / cmd)
- **session 模型**:每次 TCP 连接都必须重新 auth,拿到 session-scoped `Comm_Code` 后才能上传
- **断点续传**:通过 desc JSON 内的 `F_UploadedTag` 字段(desc 级 + file 级)实现,server 端不感知此字段
- **CRC16 必传**:每条 file 上传都带 `F_CheckCode`,server 用它校验完整性
- **ISO 8601 时间戳**:所有 `Upload_Date` 字段必须带 timezone offset(`+08:00` 之类,不能是 `Z`)

### 1.2 不在本规范范围

- 拍照/录影/编码(`snap_task` / `record_task` 的活)
- NTP / DHCP / WiFi 连接(`net` 的活)
- RTSP / HTTP server / MDNS(本地服务发现)
- 心跳协议(`MSG_TYPE_UPLOAD_JSON`)
- Server 主动下发的 cmd(`MSG_TYPE_SETTING` / `MSG_TYPE_RTMP` / `MSG_TYPE_DOWNLOAD_FILE`)

### 1.3 源码定位

| 主题 | 文件 | 行 |
|------|------|-----|
| Frame 编解码 | `src/network/Client.cpp` | `sendMessage` |
| Sync_Key 算法 | `src/network/MgmtServClient.cpp` | 68-129 |
| Auth JSON 构造 | `src/network/MgmtServClient.cpp` | 131-184 |
| Auth 等待 ack | `src/network/MgmtServClient.cpp` | 172-180 |
| upload file_info 构造 | `src/network/MgmtServClient.cpp` | `sendHeartbeat`,683-712 |
| CRC16 | `src/common/utils/crc/CRC.cpp` | 7-22 |
| StorageServClient 上传线程 | `src/network/StorageServClient.cpp` | 全文 |
| UploadWorker 主控循环 | `src/app/workmode/upload_worker.cpp` | 95-238 |
| 参考实现 | `tools/upload_test.cpp` | 全文 162 行 |

---

## 2. 协议总览

### 2.1 网络模型

```
┌──────────────┐                    ┌──────────────┐
│              │     TCP :8899       │              │
│  Client      │ <─────────────────> │  Mgmt Server │
│  (camera)    │     单 socket        │              │
│              │     消息 type 区分   │              │
└──────────────┘                    └──────────────┘
```

- **端口**:`8899`(硬编码)
- **域名**:`www.aidetcloud.com`(`config.ini [SERVER] MS=`)
- **同 socket 多 type 复用**:port 8899 同时管 auth + upload + cmd,不要尝试另起端口

### 2.2 协议族

| 阶段 | 消息 type (uint32 LE) | 方向 | 说明 |
|------|------------------------|------|------|
| 设备认证 | `MSG_TYPE_AUTH = 0x00000000` | C → S → C | 一问一答,获取 session token |
| 文件上传(desc / file) | `MSG_TYPE_UPLOAD_FILE = 0x00000001` | C → S | server 回 Status_ID |
| 通用 cmd | `MSG_TYPE_SETTING` 等 | S → C | 不在本规范范围 |
| 心跳 | `MSG_TYPE_UPLOAD_JSON` | C → S | 不在本规范范围 |

### 2.3 帧格式

每个 TCP 消息 = **12 字节定长 header + JSON body + (可选)binary 附加段**:

```
┌──────────────────┬──────────────────┬──────────────────┐
│ total_length     │ msg_type         │ json_length      │  ← 12B header (小端 uint32 × 3)
│ (uint32, 4B)     │ (uint32, 4B)     │ (uint32, 4B)     │
├──────────────────┴──────────────────┴──────────────────┤
│ JSON body (json_length 字节)                            │
│ 例:{"PID":"...","Comm_Code":"...","File":...}          │
├────────────────────────────────────────────────────────┤
│ Binary 附加段 (total_length - 12 - json_length 字节)     │
│ 例:desc/json/jpg/mp4 文件二进制                         │
└────────────────────────────────────────────────────────┘
```

- `total_length = 12 + json_length + binary 段长度`
- `json_length = JSON body 字节数(不含 binary 段)`
- 所有 multi-byte 整数用 **little-endian**(x86 / MIPS 小端字节序)

---

## 3. 设备标识体系

每个 device 有 3 层身份,层级关系:

| 名字 | 长度 | 来源 | 用途 | 生命周期 |
|------|------|------|------|----------|
| **PID** | 16-24 字节 ASCII | 工厂烧录 → `config.ini [DEVICE] PID=` | 设备**永久**身份 | 永久 |
| **EUID** | 20 字节 ASCII | server 首次 auth 时分配,`Settings::euid` 缓存 | server 端唯一标识 | 永久 |
| **DUID** | 16-24 字节 | server 回包给,通常 = PID | device under server | session |
| **Comm_Code** | 16 字节 ASCII | server 每次 auth **新生成** | session 临时身份 | **每次连接** |

**关键约束**:
- client 必须先发送 PID 才能拿到 EUID 和 Comm_Code
- 之后**每条 upload frame 的 JSON 里都要带 Comm_Code**,server 用它识别 session
- session 中断(TCP 断开)后 Comm_Code 失效,必须重新 auth

---

## 4. Auth 协议

### 4.1 Client → Server

```json
{
  "PID": "T152T20250624001",
  "Sync_Key": "m400120VmT3jd0i187M7m3BxVk",
  "EUID": "1",
  "FW_Version": "1.0.0",
  "PName": "\"CAMERA\"",
  "API_Version": "V1"
}
```

字段说明:

| 字段 | 必填 | 说明 |
|------|------|------|
| `PID` | 是 | 工厂烧录,`config.ini [DEVICE] PID=` |
| `Sync_Key` | 是 | 算法见 §4.3 |
| `EUID` | 是 | 字符串 `"1"` 或 `"0"`,标识是否需要 server 分配 EUID |
| `FW_Version` | 否 | server 用于固件匹配 |
| `PName` | 否 | 设备类型 |
| `API_Version` | 否 | 固定 `"V1"` |

### 4.2 Server → Client(成功)

```json
{
  "Status_ID": 0,
  "EUID": "KH7BPFV23TG3EZYR111A",
  "DUID": "T152T20250624001",
  "Comm_Code": "UAP4GO61UVQ9CY0L",
  "Setting_Mark": 0,
  "Firmware_Update": 0,
  "Voice_Broadcast": 0,
  "Error_Description": null
}
```

| 字段 | 说明 |
|------|------|
| `Status_ID` | `0` = 接受,非 `0` = 拒 |
| `EUID` | server 分配的新 EUID,client 缓存到 `Settings::euid` |
| `DUID` | 通常 = PID |
| `Comm_Code` | **关键**:session token,后续每条 upload frame 必带 |
| `Setting_Mark` | bit flags,指示 server 是否有 settings 要下发 |
| `Firmware_Update` | 0/1,是否需要固件升级 |
| `Voice_Broadcast` | 0/1,是否启用语音广播 |
| `Error_Description` | 错误描述,成功时 `null` |

### 4.3 Sync_Key 算法

实现位置:`src/network/MgmtServClient.cpp:68-129` (`generateSyncKey`)

**输入**:`PID`(非空)、`security_code`(`config.ini [BOOT] SMode=`,默认 `0`)

**分支决策**:

```
if security_code != 0:
    Sync_Key = base64(PID)        # 完全 deterministic
else:
    # security_code == 0 时的伪随机算法(绝大多数设备)
    dev_type   = pid[0] % 4
    c          = pid[len-1]
    if c in '0'..'9':             code_len = 25 if (c-'0')%2==0 else 20
    elif c in "QWERTYUIOP":       code_len = 26
    elif c in "ASDFGHJKL":        code_len = 24
    elif c in "ZXCVBNM":          code_len = 18
    else:                         return ""  # 同步失败,auth 必拒

    code_pos   = dev_type + (getCode(pid[2]) % 4) + (getCode(pid[4]) % 4)
    # getCode: '0'..'9' → 0..9; 'A'..'Z' → 1..26 (A=1)

    d = 0
    for i in 0..3:
        k = getCode(pid[len-4+i])
        d = (k<10) ? d*10+k : d*100+k

    srand(d * code_pos * 100 + code_len)
    sync_key[0..code_pos-1] = alpha[rand()%62]  for i in 0..code_pos
    # alpha = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ"

    sprintf(&sync_key[code_pos], "%d%d", d*code_pos, code_len)

    # 后缀填充到 code_len
    for i in code_pos..code_len-1:
        sync_key[len+i-code_pos] = alpha[rand()%62]
```

**实现约束**:
- `srand` 同一进程内单次播种,**重新 auth 时必须确保 `srand` 在同状态**,否则 Sync_Key 会变
- 必须按 PID 末位字符分类写 4 个分支,任一分支错就 auth 拒
- 这不是安全机制,只是"防止完全无身份发送"的最低门槛

### 4.4 错误处理

| Server 回包 | Client 行为 |
|-------------|-----------|
| `Status_ID: 0` | 拿到 `Comm_Code` + `EUID`,进入上传阶段 |
| `Status_ID: 非0` | auth 失败,client 端 log ERROR,放弃本次上传(desc 留本地) |
| 超时(默认 >10s 无回) | `authenticate` 返 `EC_FAILED`,client 跳过 upload 阶段 |

Auth 等待超时:**默认 10 秒**(`HTC_AUTH_TIMEOUT_MS` 可覆盖)。

---

## 5. 文件上传协议(两段式)

每张 desc 必须**先传 desc(JSON metadata),再传 file(每个文件一次)**,顺序不可乱。

### 5.1 阶段 1:上传 desc

#### 5.1.1 Client → Server(传 desc 文件本身)

**JSON body**:
```json
{
  "File": "/tmp/upload_test_desc.json",
  "FileName": "upload_test_desc.json",
  "FileType": "json",
  "FileSize": 155,
  "PID": "T152T20250624001",
  "Comm_Code": "UAP4GO61UVQ9CY0L",
  "F_CheckCode": 24215,
  "Upload_Date": "2026-06-27T15:20:32.000+08:00"
}
```

**Binary 附加段**:**desc 文件本身的完整内容**(`FileSize` 字节)。

> ⚠️ **重要陷阱**:这是 T32 上传协议的一个反直觉点。desc **只是元数据**,server 收到 JSON 就把 desc 的全部信息入库,**不**真正再读一遍 desc 文件。client 必须把 desc 解析后**所有字段**(包括 file_inf 列表)塞进 JSON body —— 而 JSON body 又不包含 desc 文件本身,只描述 desc。
>
> 历史实现曾把 desc 当成纯 metadata 说明。当前产品代码统一走 `StorageServClient::upload()`:
> JSON body 是文件信息, binary 段是 `File` 指向的文件内容,因此 desc 阶段也会附带 desc JSON 文件内容。

#### 5.1.2 desc 文件格式(client 落盘,供本地断点续传)

```json
{
  "F_UploadedTag": 0,
  "device": {
    "PID": "T152T20250624001"
  },
  "file_inf": [
    {
      "F_FileName": "IMG_20260119_112056.jpg",
      "F_FilePath": "/mnt/huntcam/DCIM",
      "F_UploadedTag": 0
    }
  ]
}
```

- `F_UploadedTag`:desc 级别(0=未上传,1=已上传)
- `file_inf[i].F_UploadedTag`:单文件级别
- 真实 desc 在产品代码里字段更多(IP / battery / GPS / WMode / NStatus / UTime 等),但**上传协议层面只关心 PID + file_inf**

#### 5.1.3 Server → Client(desc ack)

```json
{
  "Status_ID": 0,
  "Msg_Type": 1,
  "File_Name": null,
  "File_Position": null,
  "Error_Description": null
}
```

client 必须等这个 ack 回来才进 §5.2。**超时默认 8 秒**(硬编码,`upload_worker.cpp:173`)。

#### 5.1.4 客户端回写(本地)

收到 desc ack 后,client 改 desc 的 `F_UploadedTag=1` 落盘。**这是断点续传的关键**:下次开机扫 desc 时,看到 `F_UploadedTag=1` 就跳过整个 desc。

### 5.2 阶段 2:循环传 file_inf

对 desc 里的每个元素,顺序执行:

#### 5.2.1 Client → Server(传 file)

**JSON body** 和 desc 阶段同结构,改 `File/FileName/FileType/FileSize/F_CheckCode`:

```json
{
  "File": "/mnt/huntcam/DCIM/IMG_20260119_112056.jpg",
  "FileName": "IMG_20260119_112056.jpg",
  "FileType": "jpg",
  "FileSize": 77081,
  "PID": "T152T20250624001",
  "Comm_Code": "UAP4GO61UVQ9CY0L",
  "F_CheckCode": 24222,
  "Upload_Date": "2026-06-27T15:20:32.000+08:00"
}
```

**Binary 附加段**:**实际文件全部内容**(`FileSize` 字节)。`total_length = 12 + json_length + FileSize`。

#### 5.2.2 Server → Client(file ack)

同 §5.1.3,`Status_ID: 0` 收下,非 0 拒。

#### 5.2.3 客户端回写

收到 file ack 后:
1. desc 元素的 `F_UploadedTag` 改 1
2. 如果 `config.ini [POLICY] FILE_MANAGE=DELETE`:**删本地原文件**(回收 SD)
3. desc 写盘持久化

### 5.3 完整上传循环伪代码

```python
def upload_one_desc(desc_path):
    desc = read_json(desc_path)
    pid = desc["device"]["PID"]
    if desc["F_UploadedTag"] == 0:
        desc_info = {
            "File": desc_path,
            "FileName": basename(desc_path),
            "FileType": "json",
            "FileSize": file_size(desc_path),
            "PID": pid,
            "Comm_Code": session.comm_code,
            "F_CheckCode": crc16(desc_path),
            "Upload_Date": iso8601_now_with_tz(),
        }
        send_frame(MSG_TYPE_UPLOAD_FILE, json=desc_info, binary=b"")
        if wait_ack_status() != 0:
            return
        desc["F_UploadedTag"] = 1
        write_json(desc_path, desc)

    for i, f in enumerate(desc["file_inf"]):
        if f["F_UploadedTag"] == 1:
            continue
        full_path = f["F_FilePath"] + "/" + f["F_FileName"]
        file_info = {
            "File": full_path,
            "FileName": f["F_FileName"],
            "FileType": ext(f["F_FileName"]),  # "jpg" / "mp4"
            "FileSize": file_size(full_path),
            "PID": pid,
            "Comm_Code": session.comm_code,
            "F_CheckCode": crc16(full_path),
            "Upload_Date": iso8601_now_with_tz(),
        }
        send_frame(MSG_TYPE_UPLOAD_FILE, json=file_info, binary=read(full_path))
        if wait_ack_status() != 0:
            continue
        f["F_UploadedTag"] = 1
        if config.FILE_MANAGE == FILE_MANAGE_DELETE:
            delete(full_path)
    write_json(desc_path, desc)
```

### 5.4 CRC16 算法

client 必须实现 `CRC::calculate_crc16(file_path, out_code)`:

```cpp
// src/common/utils/crc/CRC.cpp:7
bool CRC::calculate_crc16(const std::string &file_path, uint16_t &crc16) {
    FILE *fp = fopen(file_path.c_str(), "rb");
    uint8_t buf[1024];
    uint16_t crc = 0;
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), fp)) > 0) {
        crc = cal_crc16(buf, n, crc);   // 第三方库 third_party/crc16
    }
    fclose(fp);
    crc16 = crc;
    return true;
}
```

底层 `cal_crc16` 是 C 函数,在 `third_party/crc16/`。**实现者可以选标准 CRC-16/MODBUS 或 CRC-16/CCITT,只要 server 端用同一种**。T32 用的是 C 库 `cal_crc16` 第三方实现(具体 poly 看 `third_party/crc16/include/crc16.h`)。

### 5.5 Upload_Date 格式

**ISO 8601 with timezone offset**(server 端按这个解析):

```
2026-06-27T15:20:32.000+08:00
```

字段:
- 日期:`YYYY-MM-DD`
- 分隔:`T`
- 时间:`HH:MM:SS.mmm`(毫秒 3 位)
- 时区:**`+HH:MM` 或 `-HH:MM`**(不能是 `Z`,server 端按 +08:00 算)

client 端从系统时区算(代码:`Timezone::getFormattedTimeWithTimezone(tv.tv_sec)`),不传 UTC。

### 5.6 大文件分片

`sendHeartbeat` 实现里**有**分片逻辑(`MgmtServClient.cpp:695-713`),但 desc 路径走 `total_length <= send_buffer_size` 分支(单 send),**大 file 走 while 循环分段 send**。server 端在 `handleDownloadFileCommand` 看到客户端的 `File_Position` 字段——这是为断点续传设计的,client 在大 file 中途断线后可重发并标注 `File_Position = <已传字节>`。

`upload_worker.cpp` 当前实现**不分片**(单 send),**适用于 T32 24MB RAM + SD 卡 jpg/mp4 单文件 ≤100MB 场景**。实现者若处理大文件必须加分片。

---

## 6. 会话生命周期

```
┌────────────────────┐
│  TCP connect       │
│  client.connect()  │  ← MgmtServClient::connect(timeout_ms=3000)
└────────┬───────────┘
         ↓
┌────────────────────┐
│  Auth exchange     │  ← MgmtServClient::authenticate()
│  get Comm_Code     │     1s 超时等待 ack
└────────┬───────────┘
         ↓
┌────────────────────┐
│  Upload loop       │  ← for each desc in queue:
│  (single or batch) │        1. upload desc
│                    │        2. for each file_inf: upload file
└────────┬───────────┘
         ↓
┌────────────────────┐
│  TCP close         │  ← 析构时 close fd
└────────────────────┘
```

- 整个会话**只用 1 个 TCP 连接**
- `MgmtServClient` 构造时**不**立即 connect;真正 connect 延迟到 `UploadWorker::start`(`upload_worker.cpp:35`)或 `ensureConnected`(`upload_worker.cpp:109`)—— 这是 lazy 模式,避免启动阶段拖慢拍照/录影
- session 中断后必须**完全重新 auth**(因为 Comm_Code 失效)

---

## 7. Client 实现 checklist

### 7.1 必实现

- [ ] **TCP socket**:`connect(server, 8899)`,timeout 3s
- [ ] **Frame 编解码**:12B header(3×uint32 LE) + JSON + binary
- [ ] **send_frame(type, json_dict, binary=b'')**:`total_length = 12+len(json)+len(binary)`
- [ ] **recv_frame()**:读 12B,parse header,读 json_length 字节 JSON,读剩余 binary
- [ ] **Sync_Key 生成器**:§4.3 算法(PID 末位字符 4 分支 + base64 fallback)
- [ ] **CRC16 算子**:`CRC::calculate_crc16(file_path)`,与 server 端同 poly
- [ ] **ISO 8601 timestamp**:`YYYY-MM-DDTHH:MM:SS.mmm+08:00`,带本地时区
- [ ] **Auth 状态机**:默认 10s 内等 ack(`HTC_AUTH_TIMEOUT_MS` 可覆盖),失败 → 上传阶段跳过
- [ ] **文件上传循环**:desc 优先 + file_inf 顺序,断点续传(F_UploadedTag 持久化)
- [ ] **FILE_MANAGE 策略**:可选删除本地原文件
- [ ] **重连**:session 失败后 backoff 重连 + 重 auth

### 7.2 选实现(产品级)

- [ ] **大文件分片**:>send_buffer_size 时分多次 send
- [ ] **文件锁 / 原子写**:写 desc 时防崩溃半写
- [ ] **连接心跳**:长时间无上传时发 `MSG_TYPE_UPLOAD_JSON` 维持 socket
- [ ] **超时控制**:每个 ack 等 N 秒,超时就 close + 重连
- [ ] **错误重试**:`Status_ID != 0` 时 backoff 重试 N 次
- [ ] **本地队列持久化**:重启用 SQLite 存 desc 队列

### 7.3 不必实现(超出协议)

- ❌ 拍照/录影/编码(那是 snap_task / record_task 的活)
- ❌ NTP / DHCP / WiFi 连接(由 `net` 完成)
- ❌ RTSP / HTTP server / MDNS
- ❌ 设备 GPS / 电池 / 温度上报(可选,但心跳协议范围)

---

## 8. 调试 / 验证

### 8.1 看真实跑通

`tools/upload_test` 是最小可运行 demo。`build_sim/bin/upload_test <file.jpg>` 跑一次:

```
mgmtServerAddr: www.aidetcloud.com:8899
desc_path: /tmp/upload_test_desc.json
source_file: build/media/20260627_215438_1.jpg
device_pid: T152T20250624001
...
UploadWorker: connected + authed [www.aidetcloud.com:8899] (lazy, after first record)
UploadWorker: desc_filename /tmp/upload_test_desc.json
...
upload descfile [...], error code: 0
...
upload ../build/media/20260627_215438_1.jpg, error code: 0
upload all files finished in /tmp/upload_test_desc.json
upload_test: flush_done=true rc=0 elapsed_ms=833
```

### 8.2 抓包验证

```bash
# 用 socat 监听 server 端 8899,模拟 server
socat -v TCP-LISTEN:8899,reuseaddr,fork SYSTEM:'cat' &
./build_sim/bin/upload_test /path/to/test.jpg
```

### 8.3 单元测试矩阵

| 测试场景 | 期望 |
|----------|------|
| PID 为空 | `authenticate` 返 EC_FAILED,worker 跳过上传 |
| `security_code != 0` | Sync_Key = base64(PID),确定值 |
| `security_code == 0` + PID 末位 0-9 偶数 | code_len=25 |
| `security_code == 0` + PID 末位 0-9 奇数 | code_len=20 |
| `security_code == 0` + 末位 QWERTYUIOP | code_len=26 |
| `security_code == 0` + 末位 ASDFGHJKL | code_len=24 |
| `security_code == 0` + 末位 ZXCVBNM | code_len=18 |
| `security_code == 0` + 末位非以上字符 | Sync_Key="" → auth 必拒 |
| F_CheckCode 错 | server 回 `Status_ID:!=0`,client 标记失败 |
| TCP 断开后重发 | 重新 auth,新 Comm_Code |
| desc `F_UploadedTag=1` | 跳过整个 desc |
| file `F_UploadedTag=1` | 跳过该 file |

---

## 9. 总结 — 实现路线图

1. **第 1 步**:实现 TCP client + frame 编解码(12B header + JSON + binary)
2. **第 2 步**:实现 Sync_Key 4 分支 + base64 fallback
3. **第 3 步**:实现 auth(发 PID+Sync_Key,等 Comm_Code)
4. **第 4 步**:实现 CRC16 + ISO 8601 timestamp 工具
5. **第 5 步**:实现单 desc 上传(desc 优先 + file_inf 顺序)
6. **第 6 步**:加 F_UploadedTag 持久化(断点续传)
7. **第 7 步**:加错误重试 + 重连
8. **第 8 步**:加文件分片(可选)
9. **第 9 步**:加 FILE_MANAGE 删除策略
10. **第 10 步**:对照 `tools/upload_test` 端到端跑通,验证 desc 落盘 + file 落库

按此规范,任何语言任何平台都能重写一个完整 client。
