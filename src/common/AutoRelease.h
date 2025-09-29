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
private:
    std::function<void()> releaseFunc;
};

#endif // AUTO_RELEASE_H
