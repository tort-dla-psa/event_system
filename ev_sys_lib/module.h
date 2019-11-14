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
	friend class port<T>;
	static std::vector<port<T>*> m_ports;
protected:
	module(const std::string &name)
		:module_interface(name)
	{}

public:
	module(const module &rhs) = delete;
	module(module &&rhs) = delete;
	virtual ~module(){};

	virtual void start(){
		for(auto p:m_ports){
			auto cast = dynamic_cast<target_port<T>*>(p);
			if(cast){
				cast->start();
			}
		}
	};

	virtual void stop(){
		for(auto p:m_ports){
			p->stop();
		}
	};
};

template<typename T>
std::vector<port<T>*> module<T>::m_ports;

}//namespace ev_sys