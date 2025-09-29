#ifndef GPIO_H
#define GPIO_H

#include <string>
#include <thread>
#include <mutex>
#include <memory>

// GPIO端口宏定义
#define PA(x)  (0 * 32 + (x))  // Port A
#define PB(x)  (1 * 32 + (x))  // Port B
#define PC(x)  (2 * 32 + (x))  // Port C

// GPIO方向枚举
enum GPIO_DIRECTION {
    INPUT = 0,
    OUTPUT = 1
};

// GPIO值枚举
enum GPIO_VALUE {
    LOW = 0,
    HIGH = 1
};

// GPIO触发方式枚举
enum GPIO_EDGE {
    EDGE_NONE = 0,      // 无触发
    EDGE_RISING = 1,    // 上升沿触发
    EDGE_FALLING = 2,   // 下降沿触发
    EDGE_BOTH = 3       // 双边沿触发
};

class GPIO
{
public:
    /**
     * @brief 构造函数，初始化GPIO对象
     * @param num GPIO引脚编号，可以使用PA(x), PB(x), PC(x)宏定义
     */
    GPIO(int num);
    
    /**
     * @brief 析构函数，释放GPIO资源
     */
    ~GPIO();
    
    /**
     * @brief 复制构造函数
     */
    GPIO(const GPIO& other);
    
    /**
     * @brief 赋值运算符
     */
    GPIO& operator=(const GPIO& other);
    
    /**
     * @brief 导出GPIO引脚（使其在sysfs中可见）
     * @return 成功返回true，失败返回false
     */
    bool exportGPIO();
    
    /**
     * @brief 取消导出GPIO引脚
     * @return 成功返回true，失败返回false
     */
    bool unexportGPIO();
    
    /**
     * @brief 设置GPIO方向
     * @param dir 方向，可以是INPUT或OUTPUT
     * @return 成功返回true，失败返回false
     */
    bool setDirection(GPIO_DIRECTION dir);
    
    /**
     * @brief 获取GPIO方向
     * @param dir 用于存储方向的变量指针
     * @return 成功返回true，失败返回false
     */
    bool getDirection(GPIO_DIRECTION &dir);
    
    /**
     * @brief 设置GPIO输出值
     * @param value 值，可以是LOW或HIGH
     * @return 成功返回true，失败返回false
     */
    bool setValue(GPIO_VALUE value);
    
    /**
     * @brief 读取GPIO输入值
     * @param value 用于存储值的变量指针
     * @return 成功返回true，失败返回false
     */
    bool getValue(GPIO_VALUE &value);
    
    /**
     * @brief 设置GPIO触发方式
     * @param edge 触发方式，可以是EDGE_NONE, EDGE_RISING, EDGE_FALLING, EDGE_BOTH
     * @return 成功返回true，失败返回false
     */
    bool setEdge(GPIO_EDGE edge);
    
    /**
     * @brief 设置GPIO的活跃电平（是否反向）
     * @param isActiveLow true表示低电平有效，false表示高电平有效
     * @return 成功返回true，失败返回false
     */
    bool setActiveLow(bool isActiveLow);
    
    /**
     * @brief 获取GPIO的活跃电平设置
     * @param isActiveLow 用于存储结果的变量指针
     * @return 成功返回true，失败返回false
     */
    bool getActiveLow(bool &isActiveLow);
    
    /**
     * @brief 获取GPIO引脚编号
     * @return GPIO引脚编号
     */
    int getPinNum() const;
    
    /**
     * @brief 检查GPIO是否已导出
     * @return 已导出返回true，未导出返回false
     */
    bool isExported() const;
    
    /**
     * @brief 实现LED呼吸灯效果（同步版本）
     * @param frequency 呼吸频率，单位为Hz（每秒完成的周期数）
     * @param duration 持续时间，单位为毫秒（0表示无限循环，直到程序结束或被中断）
     * @param dutyCycle 占空比，表示高电平在一个周期中所占的比例（0.0-1.0）
     * @return 成功返回true，失败返回false
     */
    bool blink(double frequency, int duration = 0, double dutyCycle = 0.5);
    
    /**
     * @brief 异步实现LED呼吸灯效果
     * @param frequency 呼吸频率，单位为Hz（每秒完成的周期数）
     * @param duration 持续时间，单位为毫秒（0表示无限循环，直到程序结束或被中断）
     * @param dutyCycle 占空比，表示高电平在一个周期中所占的比例（0.0-1.0）
     * @return 成功返回true，失败返回false
     */
    bool asyncBlink(double frequency, int duration = 0, double dutyCycle = 0.5);
    
    /**
     * @brief 停止LED闪烁
     * @return 成功返回true，失败返回false
     */
    bool stopBlink();
    
    /**
     * @brief 设置LED常亮或常灭
     * @param value LED状态，HIGH表示常亮，LOW表示常灭
     * @return 成功返回true，失败返回false
     */
    bool setConstant(GPIO_VALUE value);
    
private:
    int m_gpioNum;           // GPIO引脚编号
    bool m_isExported;       // 是否已导出
    std::string m_gpioPath;  // GPIO在sysfs中的路径
    
    // 异步闪烁相关成员变量
    bool m_blinkActive;  // 闪烁是否活跃
    std::shared_ptr<std::thread> m_blinkThread;        // 闪烁线程
    std::shared_ptr<std::mutex> m_blinkMutex;          // 保护闪烁参数的互斥锁
    
    // 当前闪烁参数
    double m_currentFrequency;  // 当前频率
    int m_currentDuration;      // 当前持续时间
    double m_currentDutyCycle;  // 当前占空比
    
    /**
     * @brief 闪烁线程函数
     * @param frequency 呼吸频率
     * @param duration 持续时间
     * @param dutyCycle 占空比
     */
    void blinkThreadFunc(double frequency, int duration, double dutyCycle);
    
    /**
     * @brief 写入字符串到指定文件
     * @param path 文件路径
     * @param value 要写入的字符串
     * @return 成功返回true，失败返回false
     */
    bool writeString(const std::string &path, const std::string &value);
    
    /**
     * @brief 从指定文件读取字符串
     * @param path 文件路径
     * @param value 用于存储读取结果的字符串引用
     * @return 成功返回true，失败返回false
     */
    bool readString(const std::string &path, std::string &value);
};
        
#endif // GPIO_H