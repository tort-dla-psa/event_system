#pragma once
#include <string>
#include <vector>
#include "env_obj.h"
#include "ports.h"

namespace ev_sys{

class module_interface:public env_obj{
public:
	module_interface(const std::string name)
		:env_obj(name)
	{}

	virtual ~module_interface(){}
	virtual void start()=0;
	virtual void stop()=0;
};

template<typename T>
class module:public module_interface{
private:
	const std::string name;
	static std::vector<port<T>*> ports;
protected:
	module(const std::string &name)
		:module_interface(name)
	{}

public:
	module(const module &rhs) = delete;
	module(module &&rhs) = delete;
	virtual ~module(){};
};

template<typename T>
std::vector<port<T>*> module<T>::ports;

}//namespace ev_sys