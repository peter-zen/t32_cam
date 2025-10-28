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
    #if !MCU_EXIST
    static void setWorkingModePins(int pin0, int pin1);
    #endif
    static enum workingMode getWorkingMode();

private:
        static enum workingMode working_mode;
        static bool already_get_mode;
        #if !MCU_EXIST
        static int mode_pin_0;
        static int mode_pin_1;
        #endif
};

#endif // WORK_MODE_H