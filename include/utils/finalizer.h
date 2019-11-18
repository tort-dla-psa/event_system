#pragma once
#include<vector>
#include<string>
#include<atomic>
#include<memory>
#include "module.h"

class finalizer:public ev_sys::module<finalizer>{
	std::atomic_bool ended;
	std::vector<std::unique_ptr<std::atomic_bool>> flags;
public:

	class ready_payload:public ev_sys::payload_data_holder{
	public:
		bool ready;
	};

	std::vector<std::unique_ptr<ev_sys::dual_port<finalizer>>> dps;

	finalizer(const std::string &name, size_t ports)
		:module(name)
	{
		flags.reserve(ports);
		for(size_t i=0; i<ports; i++){
			flags.emplace_back(new std::atomic_bool());
		}

		dps.reserve(ports);
		for(size_t i=0; i<ports; i++){
			auto name = std::string("dp")+std::to_string(i);
			dps.emplace_back(new ev_sys::dual_port<finalizer>(std::move(name)));
			dps.back()->set_func([this, i](std::unique_ptr<ev_sys::payload> &&pl){
				auto pl_cast = std::dynamic_pointer_cast<ready_payload>(pl->data);
				if(!pl_cast){
					return;
				}
				flags.at(i)->store(pl_cast->ready);
			});
		}
	}

	void start()override{
		for(auto &p:dps){
			p->start();
		}
		bool ended;
		do{
			ended = true;
			for(auto &p:dps){
				auto pl = std::make_unique<ev_sys::payload>();
				pl->data = std::make_shared<ready_payload>();
				p->send(std::move(pl));
			}
			for(const auto &val:flags){
				ended &= val->load();
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(5));
		}while(!ended);
	}

	void stop()override{}
};