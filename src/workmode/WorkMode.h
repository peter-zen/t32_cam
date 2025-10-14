#ifndef WORK_MODE_H
#define WORK_MODE_H

//working mode
enum workingMode {
    WORKING_MODE_SNAP_ONLY = 0,
    WORKING_MODE_UPLOAD_ONLY,
    WORKING_MODE_TEST_ONLY,
    WORKING_MODE_SNAP_UPLOAD,
    WORKING_MODE_UVC,
    WORKING_MODE_MAX
};

class WorkMode
{
public:
    static void setWorkingModePins(int pin0, int pin1);
    static enum workingMode getWorkingMode();
    static bool setWorkingMode(enum workingMode mode);
private:
        static enum workingMode working_mode;
        static bool already_get_mode;
        static int mode_pin_0;
        static int mode_pin_1;
        static int rgb_led_pin;
};

#endif // WORK_MODE_H