#include<queue>
#include<exception>
#inlcude<pthread.h>
#include "locker.h"
using namespace std;

template<typename T>
class ThreadPoll
{
private:
	int m_thread_num;//线程池大小
	pthread_t* m_threads;//线程动态数组
	queue<T*> task_queue;//任务队列
	int max_task_num;//最大待处理任务数量，即任务队列最大大小
	locker m_locker;//互斥锁
	cond m_cond;//条件变量
	bool stop;

private:
	//工作线程运行的函数，它不断从任务队列取出任务执行
	//为什么是static，为什么下面pthread_create参数传入的是this，详见412笔记
	static void* worker(void* arg);
	void run();//worker的核心函数，worker主要是获取ThreadPoll实例对象的this指针

public:
	ThreadPoll(int,int);
	~ThreadPoll()
	{
		stop=true;
		if(m_threads)
			delete[] m_threads;
	}
	bool append(T* request)
	{
		m_locker.lock();
		if(task_queue.size()>max_task_num)//队列长度已经超过最大待处理任务数量
		{
			m_locker.unlock();
			return false;
		}

		task_queue.push(request);
		m_cond.signal();//发出信号让工作线程处理请求

		m_locker.unlock();
	}
}
