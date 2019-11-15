#include <cmath>
#include <thread>
#include <iostream>
#include <random>
#include <iomanip>
#include "ev_sys.h"

using namespace ev_sys;

class my_payload:public ev_sys::payload_data_holder{
public:
	enum type{
		write,
		read
	}t;
	size_t addr;
	std::vector<uint8_t> data; 

	my_payload(type t, size_t addr, std::vector<uint8_t> &&data)
		:t(t),
		addr(addr),
		data(std::move(data))
	{}
};

class output_filter{
	std::mutex mt;
public:
	template<typename T>
	void print(const std::vector<T> &data){
		std::lock_guard<std::mutex> lk(mt);
		for(const auto &val:data){
			std::cout<<val<<" ";
		}
		std::cout<<"\n";
	}

	void print(const std::vector<uint8_t> &data){
		std::lock_guard<std::mutex> lk(mt);
		std::cout << std::hex << std::setfill('0');
		for(const auto &val:data){
			std::cout<<std::hex << std::setw(2)<<static_cast<int>(val)<<" ";
		}
		std::cout<<"\n";
	}

	void print(const std::string &data){
		std::lock_guard<std::mutex> lk(mt);
		std::cout<<data;
	}
};

class unit:public module<unit>{
	std::thread send_thr;
	output_filter &fl;
public:
	dual_port<unit> mem_dp;
	std::queue<std::shared_ptr<my_payload>> data;

	unit(const std::string &name,
		output_filter &fl)
		:module(name),
		fl(fl),
		mem_dp("mem_dp")
	{
		mem_dp.set_func([this, &fl](std::unique_ptr<payload> &&pl){
			auto data = std::dynamic_pointer_cast<my_payload>(pl->data);
			fl.print("got data\n");
			if(!data){
				return;
			}
			fl.print(data->data);
		});
	}

	~unit(){
		if(send_thr.joinable()){
			send_thr.join();
		}
	}

	void start()override{
		send_thr = std::thread([this](){
			while(data.size() != 0){
				auto pl = std::make_unique<payload>();
				auto custom_pl = std::move(data.front());
				pl->data = custom_pl;
				mem_dp.send(std::move(pl));
				data.pop();
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
			}
		});
		mem_dp.start();
	}
};

class memory:public module<memory>{
	std::vector<uint8_t> mem;
public:
	std::vector<std::unique_ptr<dual_port<memory>>> unit_dps;
	output_filter &fl;

	memory(const std::string &name, size_t bytes, size_t ports, output_filter &fl)
		:module(name),
		fl(fl)
	{
		for(size_t i=0; i<ports; i++){
			auto name = std::string("unit_dp")+std::to_string(i);
			unit_dps.emplace_back(new dual_port<memory>(std::move(name)));
			unit_dps.back()->set_func([this, &fl, i](std::unique_ptr<payload> &&pl){
				auto pl_cast = std::dynamic_pointer_cast<my_payload>(pl->data);
				if(!pl_cast){
					return;
				}
				unsigned int addr = pl_cast->addr;
				unsigned int len = pl_cast->data.size();
				if(addr >= mem.size() || addr+len >= mem.size()){
					throw_ex("reading outside of memory boundaries");
				}
				if(pl_cast->t == my_payload::read){
					std::string str = "reading addr="+std::to_string(addr)+
						" len="+std::to_string(len)+"\n";
					fl.print(std::move(str));
					std::copy(mem.begin()+addr, mem.begin()+addr+len, pl_cast->data.begin());
					unit_dps.at(i)->send(std::move(pl));
				}else{
					std::string str = "writing addr="+std::to_string(addr)+"\n";
					fl.print(std::move(str));
					std::copy(pl_cast->data.begin(), pl_cast->data.end(), mem.begin()+addr);
				}
			});
		}

		mem.resize(bytes);
	}
};

std::queue<std::shared_ptr<my_payload>> gen_data(std::vector<std::shared_ptr<my_payload>> &&cmd){
	std::queue<std::shared_ptr<my_payload>> q;
	for(size_t i=0; i<cmd.size(); i++){
		q.emplace(std::move(cmd[i]));
	}
	return std::move(q);
}

int main(){
	output_filter f;
	env e;
	auto mem_size = std::pow(2,10);
	auto mem = std::make_unique<memory>("ddr1", mem_size, 2,f);
	auto u1 = std::make_unique<unit>("u1",f);
	auto u2 = std::make_unique<unit>("u2",f);
	{
		std::vector<std::shared_ptr<my_payload>> cmds;
		cmds.reserve(3);
		cmds.emplace_back(new my_payload(my_payload::write, 0, {0x01, 0x02}));
		cmds.emplace_back(new my_payload(my_payload::write, 2, {0x03, 0x04}));
		cmds.emplace_back(new my_payload(my_payload::read, 0, std::move(std::vector<uint8_t>(4))));
		u1->data = gen_data(std::move(cmds));
	}
	{
		std::vector<std::shared_ptr<my_payload>> cmds;
		cmds.reserve(3);
		cmds.emplace_back(new my_payload(my_payload::write, 4, {0xFF, 0xFE, 0xFD, 0xFC}));
		cmds.emplace_back(new my_payload(my_payload::write, 8, {0xFB, 0xFA, 0xEF, 0xEE}));
		cmds.emplace_back(new my_payload(my_payload::read, 4, std::move(std::vector<uint8_t>(8))));
		u2->data = gen_data(std::move(cmds));
	}
	e.tie(mem->unit_dps[0], u1->mem_dp);
	e.tie(mem->unit_dps[1], u2->mem_dp);
	e.add_module(std::move(mem));
	e.add_module(std::move(u1));
	e.add_module(std::move(u2));
	e.start();
	std::this_thread::sleep_for(std::chrono::milliseconds(50));
}