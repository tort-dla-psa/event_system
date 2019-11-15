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

class ready_payload:public ev_sys::payload_data_holder{
public:
	bool ready;
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
	target_port<unit> from_mem;
	initiator_port<unit> to_mem;
	target_port<unit> from_finalizer;
	initiator_port<unit> to_finalizer;
	std::queue<std::unique_ptr<payload>> data;

	unit(const std::string &name,
		output_filter &fl)
		:module(name),
		fl(fl),
		to_mem("to_mem"),
		from_mem("from_mem"),
		from_finalizer("from_finalizer"),
		to_finalizer("to_finalizer")
	{
		from_mem.set_func([this, &fl](std::unique_ptr<payload> &&pl){
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

		from_finalizer.set_func([this](std::unique_ptr<payload> &&pl){
			auto rd_pl = std::dynamic_pointer_cast<ready_payload>(pl->data);
			if(!rd_pl){
				return;
			}
			rd_pl->ready = (data.empty());
			to_finalizer.send(std::move(pl));
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
				to_mem.send(std::move(data.front()));
				data.pop();
			}
		});
		from_mem.start();
		from_finalizer.start();
	}
};

class memory:public module<memory>{
	std::vector<uint8_t> mem;
public:
	std::vector<std::unique_ptr<target_port<memory>>> from_unit;
	std::vector<std::unique_ptr<initiator_port<memory>>> to_unit;
	output_filter &fl;

	target_port<memory> from_finalizer;
	initiator_port<memory> to_finalizer;

	memory(const std::string &name, size_t bytes, size_t ports, output_filter &fl)
		:module(name),
		from_finalizer("from_finalizer"),
		to_finalizer("to_finalizer"),
		fl(fl)
	{
		mem.resize(bytes);
		to_unit.reserve(ports);
		for(size_t i=0; i<ports; i++){
			auto name = std::string("to_unit")+std::to_string(i);
			to_unit.emplace_back(new initiator_port<memory>(std::move(name)));
		}

		from_unit.reserve(ports);
		for(size_t i=0; i<ports; i++){
			auto name = std::string("from_unit")+std::to_string(i);
			from_unit.emplace_back(new target_port<memory>(std::move(name)));
			from_unit.back()->set_func([this, &fl, i](std::unique_ptr<payload> &&pl){
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
					to_unit.at(i)->send(std::move(pl));
				}else{
					std::string str = "writing "+mes;
#ifdef DBG
					fl.print(std::move(str));
#endif
					std::copy(pl_cast->data.begin(), pl_cast->data.end(), mem.begin()+addr);
				}
			});
		}

		from_finalizer.set_func([this](std::unique_ptr<payload> &&pl){
			auto rd_pl = std::dynamic_pointer_cast<ready_payload>(pl->data);
			if(!rd_pl){
				return;
			}
			rd_pl->ready = true;
			for(auto &p:from_unit){
				rd_pl->ready &= (p->get_queue_size() == 0) && !p->processing();
			}
			to_finalizer.send(std::move(pl));
		});

	}
};

class finalizer:public module<finalizer>{
	std::atomic_bool ended;

	std::vector<std::unique_ptr<std::atomic_bool>> flags;
public:
	std::vector<std::unique_ptr<initiator_port<finalizer>>> to_units;
	std::vector<std::unique_ptr<target_port<finalizer>>> from_units;
	initiator_port<finalizer> to_mem;
	target_port<finalizer> from_mem;

	finalizer(const std::string &name, size_t ports)
		:module(name),
		to_mem("to_getter"),
		from_mem("from_getter")
	{
		flags.reserve(ports+1);
		for(size_t i=0; i<ports+1; i++){
			flags.emplace_back(new std::atomic_bool());
		}
		to_units.reserve(ports);
		for(size_t i=0; i<ports; i++){
			auto name = std::string("to_unit")+std::to_string(i);
			to_units.emplace_back(new initiator_port<finalizer>(std::move(name)));
		}

		from_units.reserve(ports);
		for(size_t i=0; i<ports; i++){
			auto name = std::string("from_unit")+std::to_string(i);
			from_units.emplace_back(new target_port<finalizer>(std::move(name)));
			from_units.back()->set_func([this, i](std::unique_ptr<payload> &&pl){
				auto pl_cast = std::dynamic_pointer_cast<ready_payload>(pl->data);
				if(!pl_cast){
					return;
				}
				flags.at(i)->store(pl_cast->ready);
			});
		}
		from_mem.set_func([this](std::unique_ptr<payload> &&pl){
			auto pl_cast = std::dynamic_pointer_cast<ready_payload>(pl->data);
			if(!pl_cast){
				return;
			}
			flags.back()->store(pl_cast->ready);
		});
	}

	void start()override{
		for(auto &p:from_units){
			p->start();
		}
		from_mem.start();
		bool ended;
		do{
			ended = true;
			for(auto &p:to_units){
				auto pl = std::make_unique<payload>();
				pl->data = std::make_shared<ready_payload>();
				p->send(std::move(pl));
			}
			{
				auto pl = std::make_unique<payload>();
				pl->data = std::make_shared<ready_payload>();
				to_mem.send(std::move(pl));
			}
			for(const auto &val:flags){
				ended &= val->load();
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(5));
		}while(!ended);
	}

	void stop()override{}
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
	auto fin = std::make_unique<finalizer>("finalizer", ports);
	for(size_t i=0; i<ports; i++){
		std::string name = "unit"+std::to_string(i);
		auto u = std::make_unique<unit>(name, f);
		u->data = gen_data(mem_size, std::atoi(argv[3]));
		e.tie(mem->to_unit[i], u->from_mem);
		e.tie(u->to_mem, mem->from_unit[i]);
		e.tie(fin->to_units[i], u->from_finalizer);
		e.tie(u->to_finalizer, fin->from_units[i]);
		e.add_module(std::move(u));
	}
	e.tie(fin->to_mem, mem->from_finalizer);
	e.tie(mem->to_finalizer, fin->from_mem);
	e.add_module(std::move(mem));
	e.add_module(std::move(fin));
	e.start();
}