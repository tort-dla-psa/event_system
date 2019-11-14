#pragma once
#include <memory>
#include "conc_queue.h"
#include "env_obj.h"

namespace ev_sys{

class payload_data_holder{
public:
	virtual ~payload_data_holder(){};
};

class payload{
public:
	std::shared_ptr<payload_data_holder> data;

	payload(){};
	virtual ~payload(){};
};

template<typename T>
class port:public env_obj{
	friend class env;
	std::shared_ptr<conc_queue<std::unique_ptr<payload>>> q;
protected:
	void clear_queue(){
		q->clear();
	}

	bool queue_is_set(){
		return q!=nullptr;
	}

	bool get_payload(std::unique_ptr<payload> &pl){
		return q->pop_b(pl);
	}

	void send_payload(std::unique_ptr<payload> &&pl){
		q->emplace(std::move(pl));
	}
public:
	port(const std::string &name)
		:env_obj(name)
	{
		T::m_ports.emplace_back(this);
	}
	virtual ~port(){}

	size_t get_queue_size(){
		return q->size();
	}

	bool ended(){
		return (queue_is_set() && 
			(this->get_queue_size() == 0) &&
			q->exit_requested());
	}

	virtual void stop()=0;
};

template<typename T>
class initiator_port:public port<T>{
public:
	initiator_port(const std::string &name)
		:port<T>(name)
	{}

	~initiator_port(){
		stop();
	}

	void stop()override{
		this->clear_queue();
	}

	void send(std::unique_ptr<payload> &&pl){
		this->send_payload(std::move(pl));
	}
};

template<typename T>
class target_port:public port<T>{
	friend class env;
	std::function<void(std::unique_ptr<payload>&&)> func;
	std::thread thr;
	std::chrono::milliseconds retry_time;
public:
	target_port(const std::string &name)
		:port<T>(name)
	{
		retry_time = std::chrono::milliseconds(2);
	}

	~target_port(){
		stop();
	}

	void stop()override{
		this->clear_queue();
		if(thr.joinable()){
			thr.join();
		}
	}

	void set_func(std::function<void(std::unique_ptr<payload>&&)> func){
		if(this->func){
			std::string mes = "function was already bound ";
			this->throw_ex(mes);
		}
		this->func = func;
	}

	void start(){
		thr = std::thread([this](){
			if(!this->queue_is_set()){
				this->throw_ex("port not bound");
			}
			while (true){
				std::unique_ptr<payload> pl;
				if(!this->get_payload(pl)){
					break;
				}
				func(std::move(pl));
			}
		});
	}

};

}//namespace ev_sys