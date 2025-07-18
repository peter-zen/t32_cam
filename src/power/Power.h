#ifndef POWER_H
#define POWER_H

#include <memory>

class Power {
    public:
	static std::shared_ptr<Power> getInstance();
	void requestShutdown();
	~Power();
	
    private:
	Power();
	Power(const Power &) = delete;
	Power &operator=(const Power &) = delete;
};
#endif // POWER_H