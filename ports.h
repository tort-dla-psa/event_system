#pragma once
#include <memory>
#include "conc_queue.h"
#include "env_obj.h"

struct payload{
	std::string src;
	std::vector<uint8_t> data;
};

class initiator_port:public env_obj{
friend class env;
	std::shared_ptr<conc_queue<std::shared_ptr<payload>>> q;
public:
	initiator_port(const std::string &name)
		:env_obj(name)
	{
		q = std::make_shared<conc_queue<std::shared_ptr<payload>>>();
	}

	~initiator_port(){
		stop();
	}

	void stop(){
		q->clear();
	}

	void send(const std::shared_ptr<payload> &pl){
		q->push(pl);
	}

	size_t get_queue_size(){
		return q->size();
	}
};

class target_port:public env_obj{
friend class env;
	std::shared_ptr<conc_queue<std::shared_ptr<payload>>> q;
	std::function<void(std::shared_ptr<payload>)> func;
	std::thread thr;
	std::chrono::milliseconds retry_time;
public:
	target_port(const std::string &name)
		:env_obj(name)
	{
		retry_time = std::chrono::milliseconds(2);
	}

	~target_port(){
		stop();
	}

	void stop(){
		q->clear();
		if(thr.joinable()){
			thr.join();
		}
	}

	void set_func(std::function<void(std::shared_ptr<payload>)> func){
		if(this->func){
			std::string mes = "function was already bound ";
			throw_ex(mes);
		}
		this->func = func;
	}

	void start(){
		thr = std::thread([this](){
			if(!this->q){
				throw_ex("port not bound");
			}
			while (true){
				std::shared_ptr<payload> pl;
				if(!q->pop_b(pl)){
					break;
				}
				func(std::move(pl));
			}
		});
	}

	bool ended(){
		return (q && q->exit_requested());
	}
};