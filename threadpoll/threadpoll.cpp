#include "threadpoll.h"

template<typename T>
ThreadPoll<T>::ThreadPoll(int thread_num,int maxTaskNum):m_thread_num(thread_num),max_task_num(maxTaskNum),stop(false)
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

			//将线程状态转换为unjoinable,使得线程执行完毕后能够自动释放资源
			if(pthread_detach(m_threads[i])!=0)
			{	
				delete[] m_threads;
				throw excpetion();
			}
		}
}

template<typename T>
void* ThreadPoll<T>::worker(void* arg)
{
	ThreadPoll* m_thread_poll=(ThreadPoll*)arg;
	m_thread_poll->run();
	return m_thread_poll;
}

//不断从任务队列取出任务执行
template<typename T>
void ThreadPoll<T>::run()
{
	while(!stop)
	{
		m_locker.lock();
		while(task_queue.size()==0)
			m_cond.wait(m_locker.get());
		T* cur_task=tast_queue.front();//取出任务
		task_queue.pop();

		m_locker.unlock();

		if(!cur_task)
			continue;
		cur_task->process();//调用任务自身的处理函数
	}
}
