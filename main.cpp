#include <iostream>
#include <functional>
#include <thread>

#include "ev_sys.h"

using namespace ev_sys;
class sender:public module{
public:
	initiator_port p1;
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
			while(i<5){
				auto pl = std::make_shared<payload>();
				pl->src = get_name();
				pl->data = {(uint8_t)data[i]};
				p1.send(std::move(pl));
				i++;
			}
			auto pl = std::make_shared<payload>();
			pl->src = get_name();
			pl->data = std::vector<uint8_t>(data.data(), data.data()+data.size());
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

class getter:public module{
public:
	target_port t1;
	target_port from_finalizer;
	initiator_port to_finalizer;

	getter(const std::string &name)
		:module(name),
		t1("trg"),
		from_finalizer("from_finalizer"),
		to_finalizer("to_finalizer")
	{
		t1.set_func([this](std::shared_ptr<payload> pl){
			func(std::move(pl));
		});

		from_finalizer.set_func([this](std::shared_ptr<payload> pl){
			if(t1.ended()){
				pl->data.at(0) = 0x01;
			}else{
				pl->data.at(0) = 0x00;
			}
			to_finalizer.send(pl);
		});
	}

	void func(std::shared_ptr<payload> pl){
		std::cout<<get_name()<<", from "<<pl->src<<":";
		for(auto &itm:pl->data){
			std::cout<<itm<<" ";
		}
		std::cout<<"\n";
	}

	void start()override{
		t1.start();
		from_finalizer.start();
	}

	void stop()override{
		t1.stop();
	}
};

class multi_getter:public module{
public:
	std::vector<std::unique_ptr<target_port>> ports;
	target_port from_finalizer;
	initiator_port to_finalizer;

	multi_getter(size_t size)
		:module("mutli_getter"),
		from_finalizer("from_finalizer"),
		to_finalizer("to_finalizer")
	{
		ports.reserve(size);
		for(size_t i=0; i<size; i++){
			auto name = std::string("in_port")+std::to_string(i);
			auto port = std::make_unique<target_port>(std::move(name));
			auto lambda = [this](std::shared_ptr<payload> pl){
				func(std::move(pl));
			};
			port->set_func(lambda);
			ports.emplace_back(std::move(port));
		}

		from_finalizer.set_func([this](std::shared_ptr<payload> pl){
			bool ended = true;
			for(auto &p:ports){
				ended &= p->ended();
			}
			if(ended){
				pl->data.at(0) = 0x01;
			}else{
				pl->data.at(0) = 0x00;
			}
			to_finalizer.send(std::move(pl));
		});
	}

	void func(std::shared_ptr<payload> pl){
		std::cout<<get_name()<<", from "<<pl->src<<":";
		for(auto &itm:pl->data){
			std::cout<<itm<<" ";
		}
		std::cout<<"\n";
	}

	void start()override{
		for(auto &p:ports){
			p->start();
		}
		from_finalizer.start();
	}

	void stop()override{
		for(auto &p:ports){
			p->stop();
		}
	}
};

class finalizer:public module{
	std::atomic_bool ended;
public:
	initiator_port to_getter;
	target_port from_getter;

	finalizer()
		:module("finalizer"),
		to_getter("to_getter"),
		from_getter("from_getter")
	{
		from_getter.set_func([this](std::shared_ptr<payload> pl){
			if(pl->data.at(0) == 0x01){
				this->ended = true;
			}else{
				this->ended = false;
			}
		});
	}

	void start()override{
		ended = false;
		from_getter.start();
		do{
			auto pl = std::make_shared<payload>();
			pl->data = {0x00};
			to_getter.send(std::move(pl));
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
	e.tie(finlzr->to_getter, gtr->from_finalizer);
	e.tie(gtr->to_finalizer, finlzr->from_getter);
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
		e.tie(senders[i]->p1, mul_get->ports[i]);
		e.add_module(std::move(senders[i]));
	}
	auto finlzr = std::make_unique<finalizer>();
	e.tie(finlzr->to_getter, mul_get->from_finalizer);
	e.tie(mul_get->to_finalizer, finlzr->from_getter);
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