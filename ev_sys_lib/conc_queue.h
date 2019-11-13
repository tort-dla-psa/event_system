#pragma once
#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <initializer_list>

namespace ev_sys{
template<typename T>
class conc_queue{
	std::queue<T> q;
	std::mutex m;
	std::condition_variable cv;
	std::atomic_bool force_exit;
public:
	conc_queue(){
		force_exit = false;
	}

	~conc_queue(){
		force_exit = true;
	}

	void push(const T &data){
		force_exit = false;
		{
			std::unique_lock<std::mutex> lk(m);
			q.push(data);
		}
		cv.notify_one();
	}

	void push(const std::initializer_list<T> &lst){
		force_exit = false;
		{
			std::unique_lock<std::mutex> lk(m);
			for(auto it = lst.begin(); it != lst.end(); it++){
				q.push(*it);
			}
		}
		cv.notify_one();
	}

	void emplace (T &&data){
		force_exit = false;
		{
			std::unique_lock<std::mutex> lk(m);
			q.emplace(data);
		}
		cv.notify_one();
	}

	void emplace (std::initializer_list<T> &&lst){
		force_exit = false;
		{
			std::unique_lock<std::mutex> lk(m);
			for(auto it = lst.begin(); it != lst.end(); it++){
				q.emplace(std::move(*it));
			}
		}
		cv.notify_one();
	}
	
	bool empty(){
		std::unique_lock<std::mutex> lk(m);
		return q.empty();
	}

	std::size_t size(){
		std::unique_lock<std::mutex> lk(m);
		return q.size();
	}

	bool pop(T &data){
		std::unique_lock<std::mutex> lk(m);
		if(force_exit || q.empty()){
			return false;
		}
		data = q.front();
		q.pop();
		return true;
	}

	bool pop_b(T &data){
		std::unique_lock<std::mutex> lk(m);
		cv.wait(lk, [&]()->bool{
			return !q.empty() || force_exit;
		});
		if(force_exit || q.empty()){
			return false;
		}
		data = q.front();
		q.pop();
		return true;
	}

	bool pop_b(T &data, std::chrono::nanoseconds time){
		std::unique_lock<std::mutex> lk(m);
		cv.wait_for(lk, time, [&]()->bool{
			return !q.empty() || force_exit;
		});
		if(force_exit || q.empty()){
			return false;
		}
		data = q.front();
		q.pop();
		return true;
	}

	void clear(){ 
		force_exit = true;
		{
			std::unique_lock<std::mutex> lk(m);
			q = std::queue<T>();
		}
		cv.notify_one();
	}

	bool exit_requested()const{
		return force_exit;
	}
};
}//namespace ev_sys