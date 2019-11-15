#include <iostream>
#include <functional>
#include <thread>

#include "ev_sys.h"

using namespace ev_sys;

class my_payload:public payload_data_holder{
public:
	std::string src;
	std::vector<uint8_t> data;
};

class sender:public module<sender>{
	std::unique_ptr<payload> make_pl(std::vector<uint8_t> &&data){
		auto my_pl = std::make_shared<my_payload>();
		my_pl->src = get_name();
		my_pl->data = std::move(data);
		auto pl = std::make_unique<payload>();
		pl->data = my_pl;
		return std::move(pl);
	}
public:
	initiator_port<sender> p1;
	std::thread thr;

	sender(const std::string &name)
		:module(name),
		p1("init_port")
	{}
	
	~sender(){
		stop();
	}

	void start()override{
		thr = std::thread([this](){
			size_t i=0;
			std::string data = "hello";
			while(i<data.size()){
				auto pl = make_pl({(uint8_t)data[i]});
				p1.send(std::move(pl));
				i++;
			}
			auto pl = make_pl(std::vector<uint8_t>(data.data(), data.data()+data.size()));
			p1.send(std::move(pl));
			while(p1.get_queue_size() != 0){
				std::this_thread::sleep_for(std::chrono::milliseconds(5));
				continue;
			}
			p1.stop();
			std::cout<<get_name()<<" ended\n";
		});
	}

	void stop()override{
		if(thr.joinable()){
			thr.join();
		}
	}
};

class getter:public module<getter>{
	std::unique_ptr<payload> make_pl(std::vector<uint8_t> &&data){
		auto my_pl = std::make_shared<my_payload>();
		my_pl->src = get_name();
		my_pl->data = std::move(data);
		auto pl = std::make_unique<payload>();
		pl->data = my_pl;
		return std::move(pl);
	}
public:
	target_port<getter> t1;
	dual_port<getter> dp;

	getter(const std::string &name)
		:module(name),
		t1("trg"),
		dp("finalizer_port")
	{
		t1.set_func([this](std::unique_ptr<payload> &&pl){
			func(std::move(pl));
		});

		dp.set_func([this](std::unique_ptr<payload> &&pl){
			auto data = std::dynamic_pointer_cast<my_payload>(pl->data);
			if(!data){
				return;
			}
			if(t1.ended()){
				data->data.at(0) = 0x01;
			}else{
				data->data.at(0) = 0x00;
			}
			dp.send(std::move(pl));
		});
	}

	void func(std::unique_ptr<payload> &&pl){
		auto data = std::dynamic_pointer_cast<my_payload>(pl->data);
		std::cout<<get_name()<<", from "<<data->src<<":";
		for(auto &itm:data->data){
			std::cout<<itm<<" ";
		}
		std::cout<<"\n";
	}

	void stop()override{
		t1.stop();
	}
};

class multi_getter:public module<multi_getter>{
public:
	std::vector<std::unique_ptr<target_port<multi_getter>>> in_ports;
	dual_port<multi_getter> dp;

	multi_getter(size_t size)
		:module("mutli_getter"),
		dp("finalizer_port")
	{
		in_ports.reserve(size);
		for(size_t i=0; i<size; i++){
			auto name = std::string("in_port")+std::to_string(i);
			auto port = std::make_unique<target_port<multi_getter>>(std::move(name));
			auto lambda = [this](std::unique_ptr<payload> &&pl){
				func(std::move(pl));
			};
			port->set_func(lambda);
			in_ports.emplace_back(std::move(port));
		}

		dp.set_func([this](std::unique_ptr<payload> &&pl){
			bool ended = true;
			for(auto &p:in_ports){
				ended &= p->ended();
			}
			auto pl_cast = std::dynamic_pointer_cast<my_payload>(pl->data);
			if(ended){
				pl_cast->data.at(0) = 0x01;
			}else{
				pl_cast->data.at(0) = 0x00;
			}
			dp.send(std::move(pl));
		});
	}

	void func(std::unique_ptr<payload> &&pl){
		auto pl_cast = std::dynamic_pointer_cast<my_payload>(pl->data);
		std::cout<<get_name()<<", from "<<pl_cast->src<<":";
		for(auto &itm:pl_cast->data){
			std::cout<<itm<<" ";
		}
		std::cout<<"\n";
	}
};

class finalizer:public module<finalizer>{
	std::atomic_bool ended;

	std::unique_ptr<payload> make_pl(std::vector<uint8_t> &&data){
		auto my_pl = std::make_shared<my_payload>();
		my_pl->src = get_name();
		my_pl->data = std::move(data);
		auto pl = std::make_unique<payload>();
		pl->data = my_pl;
		return std::move(pl);
	}
public:
	dual_port<finalizer> dp;

	finalizer()
		:module("finalizer"),
		dp("getter_port")
	{
		dp.set_func([this](std::unique_ptr<payload> &&pl){
			auto pl_cast = std::dynamic_pointer_cast<my_payload>(pl->data);
			if(!pl_cast){
				throw std::runtime_error("wrong payload");
			}
			if(pl_cast->data.at(0) == 0x01){
				this->ended = true;
			}else{
				this->ended = false;
			}
		});
	}

	void start()override{
		ended = false;
		dp.start();
		do{
			auto pl = make_pl({0x00});
			dp.send(std::move(pl));
			std::this_thread::sleep_for(std::chrono::milliseconds(5));
		}while(!ended);
	}

	void stop()override{}
};

void test_one_to_one(){
	env e;
	auto snd = std::make_unique<sender>("sender");
	auto gtr = std::make_unique<getter>("getter");
	auto finlzr = std::make_unique<finalizer>();
	e.tie(snd->p1, gtr->t1);
	e.tie(finlzr->dp, gtr->dp);
	e.add_module(std::move(snd));
	e.add_module(std::move(gtr));
	e.add_module(std::move(finlzr));
	e.start();
	e.stop();
};

void test_many_to_one(){
	env e;
	size_t size = 5;
	std::vector<std::unique_ptr<sender>> senders;
	senders.reserve(size);
	for(size_t i=0; i<size; i++){
		auto name = std::string("sender")+std::to_string(i);
		auto snd = std::make_unique<sender>(name);
		senders.emplace_back(std::move(snd));
	}
	auto mul_get = std::make_unique<multi_getter>(size);
	for(size_t i=0; i<size; i++){
		e.tie(senders[i]->p1, mul_get->in_ports[i]);
		e.add_module(std::move(senders[i]));
	}
	auto finlzr = std::make_unique<finalizer>();
	e.tie(finlzr->dp, mul_get->dp);
	e.add_module(std::move(mul_get));
	e.add_module(std::move(finlzr));
	e.start();
	e.stop();
};

int main(){
	std::cout << "testing modules and env\n";
	std::cout << "testing one to one\n";
	test_one_to_one();
	std::cout << "testing many to one\n";
	test_many_to_one();
}