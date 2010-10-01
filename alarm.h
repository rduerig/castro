
#ifndef _ALARM_H_
#define _ALARM_H_

#include <tr1/functional>
#include <unistd.h>
#include <vector>
#include "time.h"

using namespace std;
using namespace tr1;

//used as Alarm->set(1.5, timeout_func);

class Alarm {
public:
	class Ctrl {
		int id;
	public:
		Ctrl(int i) : id(i) { }
		~Ctrl(){ cancel(); }
		void cancel(){ Alarm->cancel(id); }
	};
private:
	typedef function<void()> callback_t;

	struct Entry {
		int id;
		callback_t fn;
		Time timeout;
		Entry(int i, callback_t f, Time t) : id(i), fn(f), timeout(t) { }
	};

	int nextid;
	vector<Entry> alarms;
	static Alarm * alarm = NULL;

	Alarm() : nextid(0) {
		signal(SIGALRM, bind(&Alarm::reset, this));
	}

public:

	static Alarm * operator*(){
		if(!alarm)
			alarm = new Alarm;
		return alarm;
	}
	
	Ctrl set(double len, callback_t fn){
		Entry entry(nextid++, fn, Time() + len);
		alarms.push_back(entry);

		reset();

		return Ctrl(entry.id);
	}

	void cancel(int id){
		for(int i = 0; i < alarms.size(); i++){
			if(alarms[i].id == id){
				alarms.erase(i);
				return;
			}
		}
	}

	void reset(){
		Time now();
	
		int i = 0;
		while(i < alarms.size()){
			if(alarms[i].timeout < now){
				alarms[i].fn();
				alarms.erase(i);
			}else{
				i++;
			}
		}

		if(alarms.size() > 0){
			int maxi = 0;
			for(int i = 1; i < alarms.size(); i++)
				if(alarms[i].timeout < alarms[maxi].timeout)
					maxi = i;

			double len = alarms[maxi].timeout - now;

			struct itimerval timeout;
			timeout.it_interval.tv_usec = 0;
			timeout.it_interval.tv_sec = 0;
			timeout.it_value.tv_usec = (len - (int)len)*1000000;
			timeout.it_value.tv_sec = len;
			setitimer(ITIMER_REAL, &timeout, NULL);
		}
	}
};

#endif

