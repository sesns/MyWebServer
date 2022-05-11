#ifndef THREADPOOL_H_INCLUDED
#define THREADPOOL_H_INCLUDED

#include<iostream>
#include<queue>
#include <vector>
#include "Locker.h"
#include "Log.h"

using namespace std;

template <typename T>
class Threadpool
{
private:
    Threadpool()
    {
        m_threads=NULL;
    }
    ~Threadpool()
    {
        stop=true;
        if(m_threads)
            delete[] m_threads;
    }
public:
    Threadpool(const Threadpool&)=delete;
    Threadpool(Threadpool&&)=delete;
    Threadpool& operator=(const Threadpool&)=delete;
    Threadpool& operator=(Threadpool&&)=delete;
    static Threadpool* getInstance()
    {
        static Threadpool m_instance;
        return &m_instance;
    }
    void stop_threads()
    {
        stop=true;
    }
    void init(int thread_num,int max_queue_num=100000)
    {
        m_thread_num=thread_num;
        m_queue_num=max_queue_num;
        stop=false;
        m_threads=new pthread_t[m_thread_num];

        //创建线程
        for(int i=0;i<m_thread_num;i++)
        {
            if(pthread_create(m_threads+i,NULL,run,this)!=0)
            {
                Log::getInstance()->write_log(ERRO,"Threadpool failed to create pthread");
                throw exception();
            }
            if(pthread_detach(*(m_threads+i))!=0)//将线程转换为unjoinable状态，这样线程结束时就可以自动回收资源
            {
                Log::getInstance()->write_log(ERRO,"Threadpool failed to detach pthread");
                throw exception();
            }
        }
    }
    bool append(T task);//将任务加入请求队列中
private:
    int m_thread_num;//线程池大小
    pthread_t* m_threads;//线程
    cond cnd;//条件变量
    locker loc;//互斥锁
    queue<T> m_queue;//请求队列
    int m_queue_num;//请求队列的最大长度
    bool stop;//用于停止所有线程

    static void* run(void* arg)//为了使得线程和类实例绑定
    {
        Threadpool<T> * m_threadpool=(Threadpool<T> *)arg;
        m_threadpool->work();
        return m_threadpool;
    }

    void work();//将任务从请求队列取出执行，请求队列为空时阻塞
};

template<typename T>
bool Threadpool<T>::append(T task)
{
        loc.lock();
        if(m_queue.size()>=m_queue_num)
        {
            loc.unlock();
            Log::getInstance()->write_log(WARN,"Threadpool failed to append task");
            return false;//超出最大队列长度，添加失败
        }

        m_queue.push(task);

        loc.unlock();

        cnd.signal();
        return true;
}

template<typename T>
void Threadpool<T>::work()
{
    while(!stop)
        {
            //采用条件变量来处理生产者消费者模型
            loc.lock();
            while(m_queue.size()==0)
                cnd.wait(loc.get());

            T cur_task=m_queue.front();
            m_queue.pop();
            loc.unlock();

            pthread_t cur_id=pthread_self();//获取当前线程id
            //Log::getInstance()->write_log(INFO,"pthread:%d begin to execute process()",cur_id);
            cur_task->process();//执行任务
        }
}

#endif // THREADPOOL_H_INCLUDED
