#ifndef TIMER_H_INCLUDED
#define TIMER_H_INCLUDED
#include "Locker.h"
#include <sys/timerfd.h>
#include <sys/time.h>
#include <unordered_map>
#include <functional>
#include <vector>
#include <sys/types.h>
#include <sys/socket.h>
#include <assert.h>
#include<sys/epoll.h>
#include<signal.h>
#include <unistd.h>
#include <fcntl.h>



const int TIME_SLOT=1000;//超时时长为1s

class Timer;

struct client_data {
    int sockfd;
    Timer* timer;
};


class Timer
{
private:
    std::function<void(client_data *)> callBackFunc; //回调函数
    client_data * m_userdata;
    unsigned long long expire_;
public:
    Timer(unsigned long long expire,std::function<void(client_data *)> fun,client_data* userdata)
    :expire_(expire),callBackFunc(fun),m_userdata(userdata)
    {

    }

    void active()
    {
        callBackFunc(m_userdata);
    }

    unsigned long long getExpire()
    {
        return expire_;
    }

    void changeTimer(int t)
    {
        expire_=t;
    }

    void addTime(int t)
    {
        expire_+=t;
    }

};

class TimerManager
{
private:
    Timer* shaobing;//哨兵
    vector<Timer*> heap;
    unordered_map<Timer*,int> umap;//timernode->index,用于快速找到heap中对应位置
    locker loc;

    bool cmp(Timer* lhs, Timer* rhs) const { return lhs->getExpire() < rhs->getExpire(); }
    void swim(int pos)
    {
        while(cmp(heap[pos],heap[pos/2]))
        {
            swap(umap[heap[pos]],umap[heap[pos/2]]);
            swap(heap[pos],heap[pos/2]);
            pos/=2;
        }
    }

    void sink(int pos)
    {
        int next;
        while((2*pos)<heap.size())
        {
            next=2*pos;
            if(next<=(heap.size()-2) && cmp(heap[next+1],heap[next]))
                next++;
            if(!cmp(heap[next],heap[pos]))
                break;
            swap(umap[heap[pos]],umap[heap[next]]);
            swap(heap[pos],heap[next]);
            pos=next;
        }
    }

    void push(Timer* ele)
    {

        heap.push_back(ele);
        umap[ele]=heap.size()-1;
        swim(heap.size()-1);

    }

    void pop()
    {
        Timer* topNode=heap[1];
        umap.erase(topNode);
        heap[1]=heap.back();
        umap[heap[1]]=1;
        heap.pop_back();
        sink(1);

    }

    Timer* top()
    {
        return heap[1];
    }
public:
    TimerManager()
    {
        shaobing=new Timer(0,nullptr,nullptr);
        heap.push_back(shaobing);//哨兵
        umap[shaobing]=0;
    }

    ~TimerManager()
    {
        if(shaobing)
            delete shaobing;
    }

    unsigned long long getCurrentMillisecs()
    {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return ts.tv_sec * 1000 + ts.tv_nsec / (1000 * 1000);
    }

    Timer* addTimer(int delay,std::function<void(client_data *)> fun,client_data* userdata)
    {
        if(delay<=0)
            return nullptr;

        loc.lock();
        unsigned long long now=getCurrentMillisecs();
        Timer* timer=new Timer(now+delay,fun,userdata);

        push(timer);
        loc.unlock();
        return timer;
    }

    void delTimer(Timer* timer)
    {
        loc.lock();
        timer->changeTimer(1);
        int index=umap[timer];
        swim(index);
        pop();
        if(timer)
        {
            delete timer;
        }
        loc.unlock();
    }

    void addjust(Timer* timer,int delay)
    {
        loc.lock();
        timer->addTime(delay);
        int index=umap[timer];
        sink(index);
        loc.unlock();
    }

    void takeAllTimeout()
    {
        unsigned long long now=getCurrentMillisecs();
        loc.lock();
        while(!empty())
        {
            Timer* timer=top();
            if(timer->getExpire()<=now)
            {
                pop();
                timer->active();
                delete timer;
            }
            else
                break;
        }
        loc.unlock();
    }

    unsigned long long getTopTime()
    {
        return top()->getExpire();
    }
    bool empty()
    {
        return (heap.size()-1)==0;
    }
};

#endif // TIMER_H_INCLUDED

