#ifndef POWER_H
#define POWER_H

#include <memory>

class Power {
public:
	static std::shared_ptr<Power> getInstance();
	void requestShutdown();
	void requestChangeMode();
	bool isChangeModeRequested();
	bool isShutdownRequested();
	~Power();
	
private:
	Power();
	Power(const Power &) = delete;
	Power &operator=(const Power &) = delete;
	bool changeModeRequested;
	bool shutdownRequested;
};
#endif // POWER_H