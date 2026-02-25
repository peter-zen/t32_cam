# CME ERROR & CMS ERROR Codes Reference

本文档整理了4G模块中常见的CME ERROR和CMS ERROR错误码及其含义。

- **CME ERROR**: 移动设备错误码（Mobile Equipment Error），用于报告设备相关的错误
- **CMS ERROR**: 短消息服务错误码（Short Message Service Error），用于报告SMS相关的错误

## SIM卡相关错误

| 错误码 | 含义 | 说明 |
|--------|------|------|
| 0 | Phone failure | 电话功能失败 |
| 1 | No connection to phone | 无连接到电话 |
| 2 | Phone-adaptor link reserved | 电话适配器链接已保留 |
| 3 | Operation not allowed | 不允许的操作 |
| 4 | Operation not supported | 不支持的操作 |
| 5 | PH-SIM PIN required | 需要PH-SIM PIN |
| 6 | PH-FSIM PIN required | 需要PH-FSIM PIN |
| 7 | PH-FSIM PUK required | 需要PH-FSIM PUK |
| 10 | (U)SIM not inserted | (U)SIM卡未插入 |
| 11 | (U)SIM PIN required | 需要(U)SIM PIN码 |
| 12 | (U)SIM PUK required | 需要(U)SIM PUK码 |
| 13 | (U)SIM failure | (U)SIM卡故障 |
| 14 | (U)SIM busy | (U)SIM卡忙 |
| 15 | (U)SIM wrong | (U)SIM卡错误 |
| 16 | Incorrect password | 密码错误 |
| 17 | (U)SIM PIN2 required | 需要(U)SIM PIN2码 |
| 18 | (U)SIM PUK2 required | 需要(U)SIM PUK2码 |

## 网络相关错误

| 错误码 | 含义 | 说明 |
|--------|------|------|
| 20 | Memory full | 内存已满 |
| 21 | Invalid index | 无效的索引 |
| 22 | Not found | 未找到 |
| 23 | Memory failure | 内存故障 |
| 24 | Text string too long | 文本字符串过长 |
| 25 | Invalid characters in text string | 文本字符串中包含无效字符 |
| 26 | Dial string too long | 拨号字符串过长 |
| 27 | Invalid characters in dial string | 拨号字符串中包含无效字符 |
| 30 | No network service | 无网络服务 |
| 31 | Network timeout | 网络超时 |
| 32 | Network not allowed - emergency calls only | 网络不允许 - 仅允许紧急呼叫 |

## 网络个性化错误

| 错误码 | 含义 | 说明 |
|--------|------|------|
| 40 | Network personalization PIN required | 需要网络个性化PIN码 |
| 41 | Network personalization PUK required | 需要网络个性化PUK码 |
| 42 | Network subset personalization PIN required | 需要网络子集个性化PIN码 |
| 43 | Network subset personalization PUK required | 需要网络子集个性化PUK码 |
| 44 | Service provider personalization PIN required | 需要服务提供商个性化PIN码 |
| 45 | Service provider personalization PUK required | 需要服务提供商个性化PUK码 |
| 46 | Corporate personalization PIN required | 需要企业个性化PIN码 |
| 47 | Corporate personalization PUK required | 需要企业个性化PUK码 |


## 音频相关错误

| 错误码 | 含义 | 说明 |
|--------|------|------|
| 901 | Audio unknown error | 音频未知错误 |
| 902 | Audio invalid parameters | 音频参数无效 |
| 903 | Audio operation not supported | 不支持的音频操作 |
| 904 | Audio device busy | 音频设备忙 |

# CMS ERROR Codes Reference

CMS ERROR（Short Message Service Error）是专门用于短消息服务（SMS）的错误码，当使用AT命令发送、接收或管理短信时可能会遇到这些错误。

## 短消息发送错误 (300-399)

| 错误码 | 含义 | 说明 |
|--------|------|------|
| 300 | ME failure | 移动设备故障 |
| 301 | SMS ME reserved | SMS服务被保留 |
| 302 | Operation not allowed | 不允许的操作 |
| 303 | Operation not supported | 不支持的操作 |
| 304 | Invalid PDU mode | 无效的PDU模式 |
| 305 | Invalid text mode | 无效的文本模式 |
| 310 | (U)SIM not inserted | (U)SIM卡未插入 |
| 311 | (U)SIM pin necessary | 需要(U)SIM PIN码 |
| 312 | PH SIM pin necessary | 需要PH-SIM PIN码 |
| 313 | (U)SIM failure | (U)SIM卡故障 |
| 314 | (U)SIM busy | (U)SIM卡忙 |
| 315 | (U)SIM wrong | (U)SIM卡错误 |
| 316 | (U)SIM PUK required | 需要(U)SIM PUK码 |
| 317 | (U)SIM PIN2 required | 需要(U)SIM PIN2码 |
| 318 | (U)SIM PUK2 required | 需要(U)SIM PUK2码 |
| 320 | Memory failure | 内存故障 |
| 321 | Invalid memory index | 无效的内存索引 |
| 322 | Memory full | 内存已满 |
| 330 | SMSC address unknown | SMSC地址未知 |
| 331 | No network | 无网络服务 |
| 332 | Network timeout | 网络超时 |

## 短消息接收错误 (500-599)

| 错误码 | 含义 | 说明 |
|--------|------|------|
| 500 | Unknown | 未知错误 |
| 512 | (U)SIM not ready | (U)SIM卡未就绪 |
| 513 | Message length exceeds | 消息长度超出限制 |
| 514 | Invalid request parameters | 无效的请求参数 |
| 515 | ME storage failure | 移动设备存储故障 |
| 517 | Invalid service mode | 无效的服务模式 |
| 528 | More message to send state error | 更多消息发送状态错误 |
| 529 | MO SMS is not allowed | 不允许发送移动发起的短信 |
| 530 | GPRS is suspended | GPRS已暂停 |
| 531 | ME storage full | 移动设备存储已满 |

## CMS ERROR与CME ERROR的区别

| 特性 | CME ERROR | CMS ERROR |
|------|-----------|-----------|
| 全称 | Mobile Equipment Error | Short Message Service Error |
| 适用范围 | 移动设备相关的所有错误 | 仅适用于短消息服务（SMS） |
| 错误码范围 | 0-599 | 300-699 |
| 常见场景 | SIM卡错误、网络错误、设备故障 | 短信发送、接收、存储错误 |
| AT命令示例 | AT+CPIN?, AT+CGATT | AT+CMGS, AT+CMGR, AT+CMGL |

