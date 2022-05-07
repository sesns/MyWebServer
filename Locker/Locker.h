#ifndef LOCKER_H_INCLUDED
#define LOCKER_H_INCLUDED

#include<iostream>
#include<exception>
#include<pthread.h>
#include<semaphore.h>
#include<errno.h>
using namespace std;
class sem
{
private:
	sem_t sem_id;
public:
	sem()//默认构造函数
	{
		int ret=sem_init(&sem_id,0,0);
		if(ret!=0)
			cout<<"sem_init error!\n";
	}

	sem(int val)//构造函数
	{
		int ret=sem_init(&sem_id,0,val);
		if(ret!=0)
			cout<<"sem_init with value error!\n";
	}

	~sem()
	{
		sem_destroy(&sem_id);
	}

	bool wait()//消费资源
	{
		int ret=sem_wait(&sem_id);
		if(ret!=0)
			throw exception();
		return ret==0;
	}

	bool post()//释放资源
	{
		int ret=sem_post(&sem_id);
		if(ret!=0)
			throw exception();
		return ret==0;
	}

};

class locker
{
private:
	pthread_mutex_t mtx;
public:
	locker()
	{
		int ret=pthread_mutex_init(&mtx,NULL);
		if(ret!=0)
			cout<<"mutex_init error!\n";
	}

	~locker()
	{
		int ret=pthread_mutex_destroy(&mtx);
		if(ret!=0)
        {
            cout<<"mutex_destroy error!\n";
            cout<<ret<<"\n";
        }
	}

	bool lock()
	{
		int ret=pthread_mutex_lock(&mtx);
		if(ret!=0)
			throw exception();
		return ret==0;
	}

	bool unlock()
	{
		int ret=pthread_mutex_unlock(&mtx);
		if(ret!=0)
			throw exception();
		return ret==0;
	}

	pthread_mutex_t* get()
	{
		return &mtx;
	}
};

class cond
{
private:
	pthread_cond_t cnd;
public:
	cond()
	{
		int ret=pthread_cond_init(&cnd,NULL);
		if(ret!=0)
			cout<<"cond_init error!\n";
	}

	~cond()
	{
		int ret=pthread_cond_destroy(&cnd);
		if(ret!=0)
        {
            cout<<"cond_destroy error!\n";
            cout<<ret<<"\n";
        }
	}

	bool wait(pthread_mutex_t* m_mutex)
	{
		int ret=pthread_cond_wait(&cnd,m_mutex);
		if(ret!=0)
			throw exception();
		return ret==0;
	}

	bool signal()
	{
		int ret=pthread_cond_signal(&cnd);
		if(ret!=0)
			throw exception();
		return ret==0;
	}

	bool broadcast()
	{
		int ret=pthread_cond_broadcast(&cnd);
		if(ret!=0)
			throw exception();
		return ret==0;
	}


};

#endif // LOCKER_H_INCLUDED
