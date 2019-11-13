#pragma once
#include <string>
#include "env_obj.h"

class module:public env_obj{
private:
	const std::string name;
protected:
	module(const std::string &name)
		:env_obj(name)
	{}

public:
	module(const module &rhs) = delete;
	module(module &&rhs) = delete;
	virtual ~module(){};
    virtual void start()=0;
	virtual void stop()=0;
};