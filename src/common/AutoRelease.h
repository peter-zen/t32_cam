#ifndef AUTO_RELEASE_H
#define AUTO_RELEASE_H
#include <functional>

class AutoRelease
{
public:
    AutoRelease(std::function<void()> releaseFunc) {
        this->releaseFunc = releaseFunc;
    }
    ~AutoRelease() {
        if (releaseFunc) {
            releaseFunc();
        }
    }

    void release() {
        if (releaseFunc) {
            releaseFunc();
            releaseFunc = nullptr;
        }
    }
private:
    std::function<void()> releaseFunc;
};

#endif // AUTO_RELEASE_H
