#ifndef DAYNIGHT_SWITCH_H
#define DAYNIGHT_SWITCH_H

#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>

enum DayNightState {
	DAY,
	NIGHT,
    MAX
};

class DayNightSwitch {  
public:
	static std::shared_ptr<DayNightSwitch> getInstance();
    void setCdsPins(int pin);
    void setIRLedPins(int pin);
    void setIRCutPins(int enable_pin, int ctrl_pin);

	enum DayNightState getDayNightState();
    bool controlIRCut(DayNightState state);
    bool controlIRLed(DayNightState state);
    bool controlISP(DayNightState state);
    bool startAutoSwithch();
    bool suspendAutoSwitch();
    bool resumeAutoSwitch();
    bool stopAutoSwitch();

    ~DayNightSwitch();
private:
	DayNightSwitch();
	DayNightSwitch(const DayNightSwitch &) = delete;
	DayNightSwitch &operator=(const DayNightSwitch &) = delete;
    enum DayNightState dayNightState;
    int cdsPin;
    int irledPin;
    int irCutEnablePin;
    int irCutCtrlPin;
    bool autoSwitchThreadRunning;
    bool autoSwitchThreadSuspended;
    std::shared_ptr<std::thread> autoSwitchThread;
    std::mutex threadMutex;        // Mutex for thread synchronization
    std::condition_variable threadCV; // Condition variable for suspend/resume
    
};

#endif // DAYNIGHT_SWITCH_H
