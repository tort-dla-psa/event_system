#pragma once
#include <vector>
#include <memory>
#include "module.h"
#include "ports.h"

namespace ev_sys{

class env{
	std::vector<std::unique_ptr<module_interface>> modules;
public:
	env(){}
	env(const env &e) = delete;
	env(env &&e) = delete;

	void add_module(std::unique_ptr<module_interface> mdl){
		modules.emplace_back(std::move(mdl));
	}

	template<class c1, class c2>
	void tie(initiator_port<c1> &p1, target_port<c2> &t1){
		p1.q = std::make_shared<conc_queue<std::unique_ptr<payload>>>();
		t1.q = p1.q;
	}

	template<class c1, class c2>
	void tie(initiator_port<c1> &p1, std::unique_ptr<target_port<c2>> &t1){
		p1.q = std::make_shared<conc_queue<std::unique_ptr<payload>>>();
		t1->q = p1.q;
	}

	template<class c1, class c2>
	void tie(std::unique_ptr<initiator_port<c1>> &p1, target_port<c2> &t1){
		p1->q = std::make_shared<conc_queue<std::unique_ptr<payload>>>();
		t1.q = p1->q;
	}

	template<class c1, class c2>
	void tie(std::unique_ptr<initiator_port<c1>> &p1, std::unique_ptr<target_port<c2>> &t1){
		p1->q = std::make_shared<conc_queue<std::unique_ptr<payload>>>();
		t1->q = p1->q;
	}
	
	void start(){
        for(auto &m:modules){
            m->start();
        }
    };

	void stop(){
        for(auto &m:modules){
            m->stop();
        }
	}
};
}//namespace ev_sys