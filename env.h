#pragma once
#include <vector>
#include <memory>
#include "module.h"
#include "ports.h"

class env{
	std::vector<std::unique_ptr<module>> modules;
public:
	env(){};
	env(const env &e) = delete;
	env(env &&e) = delete;

	void add_module(std::unique_ptr<module> &&mdl){
		modules.emplace_back(std::move(mdl));
	}

	void tie(initiator_port &p1, target_port &t1){
		t1.q = p1.q;
	}

	void tie(initiator_port &p1, std::unique_ptr<target_port> &t1){
		t1->q = p1.q;
	}

	void tie(std::unique_ptr<initiator_port> &p1, target_port &t1){
		t1.q = p1->q;
	}

	void tie(std::unique_ptr<initiator_port> &p1, std::unique_ptr<target_port> &t1){
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