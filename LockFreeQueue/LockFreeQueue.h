#ifndef LOCKFREEQUEUE_H_INCLUDED
#define LOCKFREEQUEUE_H_INCLUDED

#include<vector>
#include"Log.h"
using namespace std;

#if defined(__GNUC__) || defined(__x86_64__)
#define TPOOL_COMPILER_BARRIER() __asm__ __volatile("" : : : "memory")

static inline void FullMemoryBarrier()
{
    __asm__ __volatile__("mfence": : : "memory");
}

#define smp_mb() FullMemoryBarrier()
#define smp_rmb() TPOOL_COMPILER_BARRIER()
#define smp_wmb() TPOOL_COMPILER_BARRIER()

#else
#error "smp_mb has not been implemented for this architecture."
#endif

template<typename T>
class LockFreeQueue
{
private:
    vector<T> m_buffer;
    unsigned int m_in;//指向尾后元素
    unsigned int m_out;
public:
    LockFreeQueue(unsigned int Size):m_buffer(Size),m_in(0),m_out(0)
    {

    }
    LockFreeQueue():m_buffer(0),m_in(0),m_out(0)
    {
    }
    void push(T ele)
    {

        unsigned int In=m_in;
        unsigned int Out=m_out;
        smp_mb();


        if(((In+1)%m_buffer.size())==(Out%m_buffer.size()))//使用一个空的槽位来判断队列是否满
        {
            //Log::getInstance()->write_log(ERRO,"lockfreequeue is full! push() failed");
            //cout<<"lockfreequeue is full! push() failed\n";
            return;
        }


        m_buffer[In%m_buffer.size()]=ele;

        smp_wmb();

        m_in+=1;

    }

    T pop()
    {
        unsigned int In=m_in;
        unsigned int Out=m_out;
        smp_rmb();

        if((In%m_buffer.size())==(Out%m_buffer.size()))//判断是否为空
        {
            //Log::getInstance()->write_log(ERRO,"lockfreequeue is empty! pop() failed");
            //cout<<"lockfreequeue is empty! pop() failed\n";
            return T();
        }

        T res=m_buffer[Out%m_buffer.size()];
        smp_mb();

        m_out+=1;
        return res;

    }

    unsigned int size()
    {
        unsigned int In=m_in%m_buffer.size();
        unsigned int Out=m_out%m_buffer.size();
        if(Out<=In)
            return In-Out;
        return m_buffer.size()-Out+In;
    }

    bool empty()
    {
        unsigned int In=m_in%m_buffer.size();
        unsigned int Out=m_out%m_buffer.size();
        return In==Out;
    }

    bool full()
    {
        unsigned int In=m_in%m_buffer.size();
        unsigned int Out=m_out%m_buffer.size();
        return (In+1)==Out;
    }

};

#endif // LOCKFREEQUEUE_H_INCLUDED
