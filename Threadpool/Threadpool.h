#ifndef THREADPOOL_H_INCLUDED
#define THREADPOOL_H_INCLUDED

#include<iostream>
#include<queue>
#include <vector>
#include "Locker.h"
#include "Log.h"
#include "LockFreeQueue.h"
#include "CasLockFreeQueue.h"


using namespace std;

/*
//有锁队列，工作线程竞争资源

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

            //Log::getInstance()->write_log(INFO,"pthread:%d begin to execute process()",cur_id);
            cur_task->process();//执行任务
        }
}

#endif // THREADPOOL_H_INCLUDED
*/


//无锁队列，工作线程不竞争资源

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
        robin=0;
        stop=false;
        m_threads=new pthread_t[m_thread_num];
        m_queues=vector<LockFreeQueue<T>>(m_thread_num);

        //初始化信号量
        block=vector<sem>(thread_num);
        emptyBlock=vector<sem>(thread_num);
        for(int i=0;i<thread_num;i++)
        {
            block[i]=sem(0);
            emptyBlock[i]=sem(max_queue_num);
        }

        //初始化无锁队列
        for(int i=0;i<m_thread_num;i++)
            m_queues[i]=LockFreeQueue<T>(max_queue_num);

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
    vector<LockFreeQueue<T>> m_queues;//一个线程对应一个无锁队列
    int m_queue_num;//请求队列的最大长度
    vector<sem> block;//用于表示资源块数目的信号量
    vector<sem> emptyBlock;//用于表示空闲块数目的信号量
    unsigned int robin;//用于轮流分配任务给各个无锁队列
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
        //采用robin分配任务
        unsigned int cur=robin%m_thread_num;
        robin++;

        //消费者模型来实现进程同步
        emptyBlock[cur].wait();//消费空闲块

        m_queues[cur].push(task);

        block[cur].post();//生产资源块

        return true;
}

template<typename T>
void Threadpool<T>::work()
{
    pthread_t m_pid=pthread_self();
    unsigned int cur;//找到该线程对应的下标
    for(int i=0;i<m_thread_num;i++)
        if(m_threads[i]==m_pid)
            cur=i;

    while(!stop)
        {
                //消费者模型来实现进程同步
                block[cur].wait();//消费资源块

                T cur_task=m_queues[cur].pop();

                emptyBlock[cur].post();//生产空闲块

                cur_task->process();//执行任务
        }
}

#endif // THREADPOOL_H_INCLUDED


/*
// 无所队列+无信号liang
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
        robin=0;
        stop=false;
        m_threads=new pthread_t[m_thread_num];
        m_queues=vector<LockFreeQueue<T>>(m_thread_num);

        //初始化信号量
        block=vector<sem>(thread_num);
        emptyBlock=vector<sem>(thread_num);
        for(int i=0;i<thread_num;i++)
        {
            block[i]=sem(0);
            emptyBlock[i]=sem(max_queue_num);
        }

        //初始化无锁队列
        for(int i=0;i<m_thread_num;i++)
            m_queues[i]=LockFreeQueue<T>(max_queue_num);

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
    vector<LockFreeQueue<T>> m_queues;//一个线程对应一个无锁队列
    int m_queue_num;//请求队列的最大长度
    vector<sem> block;//用于表示资源块数目的信号量
    vector<sem> emptyBlock;//用于表示空闲块数目的信号量
    unsigned int robin;//用于轮流分配任务给各个无锁队列
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
        //采用robin分配任务
        unsigned int cur=robin%m_thread_num;
        robin++;

        //消费者模型来实现进程同步
        //emptyBlock[cur].wait();//消费空闲块

        m_queues[cur].push(task);

        //block[cur].post();//生产资源块

        return true;
}


template<typename T>
void Threadpool<T>::work()
{
    pthread_t m_pid=pthread_self();
    unsigned int cur;//找到该线程对应的下标
    for(int i=0;i<m_thread_num;i++)
        if(m_threads[i]==m_pid)
            cur=i;
    T cur_task;
    while(!stop)
        {
                //消费者模型来实现进程同步
                //block[cur].wait();//消费资源块
                if(!m_queues[cur].empty())
                    cur_task=m_queues[cur].pop();

                //emptyBlock[cur].post();//生产空闲块
                if(cur_task)
                    cur_task->process();//执行任务

                cur_task=nullptr;
        }
}

#endif // THREADPOOL_H_INCLUDED
*/

/*
//cas无锁队列
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
        robin=0;
        stop=false;
        m_threads=new pthread_t[m_thread_num];
        m_queues=vector<LockFreeQueueCpp11<T>>(m_thread_num);

        //初始化信号量
        block=vector<sem>(thread_num);
        emptyBlock=vector<sem>(thread_num);
        for(int i=0;i<thread_num;i++)
        {
            block[i]=sem(0);
            emptyBlock[i]=sem(max_queue_num);
        }

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
    vector<LockFreeQueueCpp11<T>> m_queues;//一个线程对应一个无锁队列
    int m_queue_num;//请求队列的最大长度
    vector<sem> block;//用于表示资源块数目的信号量
    vector<sem> emptyBlock;//用于表示空闲块数目的信号量
    unsigned int robin;//用于轮流分配任务给各个无锁队列
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
        //采用robin分配任务
        unsigned int cur=robin%m_thread_num;
        robin++;

        //消费者模型来实现进程同步
        //emptyBlock[cur].wait();//消费空闲块

        m_queues[cur].push(task);

        //block[cur].post();//生产资源块

        return true;
}


template<typename T>
void Threadpool<T>::work()
{
    pthread_t m_pid=pthread_self();
    unsigned int cur;//找到该线程对应的下标
    for(int i=0;i<m_thread_num;i++)
        if(m_threads[i]==m_pid)
            cur=i;
    T cur_task;
    while(!stop)
        {
                //消费者模型来实现进程同步
                //block[cur].wait();//消费资源块
                if(m_queues[cur].size()!=0)
                    m_queues[cur].pop(cur_task);
                else
                    cur_task=T();

                //emptyBlock[cur].post();//生产空闲块
                if(cur_task)
                    cur_task->process();//执行任务

        }
}

#endif // THREADPOOL_H_INCLUDED

*/
