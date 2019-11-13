#pragma once
#include <string>

class env_obj{
	const std::string name;
protected:
	env_obj(const std::string &name)
		:name(name)
	{}

	void throw_ex(const std::string &mes){
		throw std::runtime_error("["+name+"]:"+mes);
	}
public:
	env_obj(const env_obj &e)=delete;
	env_obj(env_obj &&e)=delete;

	auto get_name()const{ return name; }
};