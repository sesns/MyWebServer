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
	locker m_lock;//互斥锁
	cond m_cond;//条件变量
	bool stop;
private:

public:
	ThreadPoll(int thread_num,int maxTaskNum):m_thread_num(thread_num),max_task_num(maxTaskNum),stop(false)
	{
		if(thread_num<=0 || maxTaskNum<=0)
			throw exception();
		m_threads=new pthread_t[m_thread_num];
		if(!m_threads)
			throw excpetion();
		for(int i=0;i<m_thread_num;i++)
		{
			//初始化线程池中的线程
			if(pthread_create_init(m_threads[i],NULL,worker,this)!=0)
			{
				delete[] m_threads;
				throw excpetion();
			}

			//将线程状态转换为unjoinable
			if(pthread_detach(m_threads[i])!=0)
			{	
				delete[] m_threads;
				throw excpetion();
			}
		}
	}
	~ThreadPoll()
	{
		if(m_threads)
			delete[] m_threads;

	}
}
