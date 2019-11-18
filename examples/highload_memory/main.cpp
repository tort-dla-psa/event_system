#include <memory>
#include <vector>
#include <string>
#include <cmath>
#include <thread>
#include <mutex>
#include <iostream>
#include <random>
#include <iomanip>
#include "ev_sys.h"
#include "utils/finalizer.h"

using namespace ev_sys;

class my_payload:public ev_sys::payload_data_holder{
public:
	enum type{
		write,
		read
	}t;

	size_t addr;
	std::vector<uint8_t> data; 

	my_payload(size_t addr, std::vector<uint8_t> &&data)
		:t(write),
		addr(addr),
		data(std::move(data))
	{}
	my_payload(size_t addr, size_t len)
		:t(read),
		addr(addr),
		data(len)
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
	dual_port<unit> mem_dp, finalizer_dp;
	std::queue<std::unique_ptr<payload>> data;

	unit(const std::string &name,
		output_filter &fl)
		:module(name),
		fl(fl),
		mem_dp("mem_dp"),
		finalizer_dp("finalizer_dp")
	{
		mem_dp.set_func([this, &fl](std::unique_ptr<payload> &&pl){
			auto data = std::dynamic_pointer_cast<my_payload>(pl->data);
#ifdef DBG
			fl.print("got data\n");
#endif
			if(!data){
				return;
			}
#ifdef DBG
			fl.print(data->data);
#endif
		});

		finalizer_dp.set_func([this](std::unique_ptr<payload> &&pl){
			auto rd_pl = std::dynamic_pointer_cast<finalizer::ready_payload>(pl->data);
			if(!rd_pl){
				return;
			}
			rd_pl->ready = (data.empty());
			finalizer_dp.send(std::move(pl));
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
				mem_dp.send(std::move(data.front()));
				data.pop();
			}
		});
		mem_dp.start();
		finalizer_dp.start();
	}
};

class memory:public module<memory>{
	std::vector<uint8_t> mem;
public:
	std::vector<std::unique_ptr<dual_port<memory>>> unit_dps;
	output_filter &fl;
	dual_port<memory> finalizer_dp;

	memory(const std::string &name, size_t bytes, size_t ports, output_filter &fl)
		:module(name),
		finalizer_dp("finalizer_dp"),
		fl(fl)
	{
		mem.resize(bytes);

		unit_dps.reserve(ports);
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
				std::string mes = "addr="+std::to_string(addr)+
						" len="+std::to_string(len)+"\n";
				if(addr >= mem.size() || addr+len >= mem.size()){
					throw_ex("reading outside of memory boundaries: "+mes);
				}
				if(pl_cast->t == my_payload::read){
					std::string str = "reading "+mes;
#ifdef DBG
					fl.print(std::move(str));
#endif
					std::copy(mem.begin()+addr, mem.begin()+addr+len, pl_cast->data.begin());
					unit_dps.at(i)->send(std::move(pl));
				}else{
					std::string str = "writing "+mes;
#ifdef DBG
					fl.print(std::move(str));
#endif
					std::copy(pl_cast->data.begin(), pl_cast->data.end(), mem.begin()+addr);
				}
			});
		}

		finalizer_dp.set_func([this](std::unique_ptr<payload> &&pl){
			auto rd_pl = std::dynamic_pointer_cast<finalizer::ready_payload>(pl->data);
			if(!rd_pl){
				return;
			}
			rd_pl->ready = true;
			for(auto &p:unit_dps){
				rd_pl->ready &= (p->get_queue_size_in() == 0) &&
					(p->get_queue_size_out() == 0) &&
					!p->processing();
			}
			finalizer_dp.send(std::move(pl));
		});

	}
};

std::queue<std::unique_ptr<payload>> gen_data(size_t mem_size, size_t count){
	std::queue<std::unique_ptr<payload>> q;
	std::random_device rd;
	std::uniform_int_distribution<size_t> addr_dist(0, mem_size-2);
	std::mt19937 mt(rd());
	for(size_t i=0; i<count; i++){
		size_t addr = addr_dist(mt);
		std::uniform_int_distribution<size_t> len_dist(1, (mem_size-addr-1)/2+1);
		size_t len = len_dist(mt);
		//std::cout<<"addr:\t"<<addr<<"\t len:\t"<<len<<"\n";
		std::vector<uint8_t> data(len);

		std::shared_ptr<my_payload> data_pl;
		if(addr%2 == 0){
			std::iota(data.begin(), data.end(), 0);
			data_pl = std::make_shared<my_payload>(addr, std::move(data));
		}else{
			data_pl = std::make_shared<my_payload>(addr, len);
		}
		auto pl = std::make_unique<payload>();
		pl->data = std::move(data_pl);
		q.emplace(std::move(pl));
	}
	return std::move(q);
}

int main(int argc, char* argv[]){
	if(argc != 4){
		throw std::runtime_error("provide mem_size, units count and queries count");
	}
	output_filter f;
	env e;
	size_t mem_size = std::atoi(argv[1]);
	size_t ports = std::atoi(argv[2]);
	auto mem = std::make_unique<memory>("ddr1", mem_size, ports,f);
	auto fin = std::make_unique<finalizer>("finalizer", ports+1);
	for(size_t i=0; i<ports; i++){
		std::string name = "unit"+std::to_string(i);
		auto u = std::make_unique<unit>(name, f);
		u->data = gen_data(mem_size, std::atoi(argv[3]));
		e.tie(mem->unit_dps[i], u->mem_dp);
		e.tie(fin->dps[i], u->finalizer_dp);
		e.add_module(std::move(u));
	}
	e.tie(mem->finalizer_dp, fin->dps.back());
	e.add_module(std::move(mem));
	e.add_module(std::move(fin));
	e.start();
}