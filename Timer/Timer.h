#ifndef TIMER_H_INCLUDED
#define TIMER_H_INCLUDED
#include "Locker.h"
#include <time.h>
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

const int TIME_SLOT=5;//最小超时单位

class TimerNode;

struct client_data {
    int sockfd;
    TimerNode* timer;
};

class TimerNode
{
private:
    time_t m_expec;//到期时间
public:
    std::function<void(client_data *)> callBackFunc; //回调函数
    client_data * m_userdata;

    TimerNode(time_t delay,client_data* userdata,std::function<void(client_data *)> fun):
        m_userdata(userdata),callBackFunc(fun)
        {
            time_t cur=time(nullptr);
            m_expec=cur+delay;
        }
    time_t GetExpeTime()
    {
        return m_expec;
    }

    void addTime(time_t delay)
    {
        m_expec+=delay;
    }

    void changeTime(time_t newtime)
    {
        m_expec=newtime;
    }

};


class TimerHeap//最小堆
{
private:
    TimerNode* shaobing;//哨兵
    vector<TimerNode*> heap;
    unordered_map<TimerNode*,int> umap;//timernode->index,用于快速找到heap中对应位置
    locker loc;
private:
    void swim(int pos)
    {
        while(heap[pos]->GetExpeTime()<heap[pos/2]->GetExpeTime())
        {
            swap(umap[heap[pos]],umap[heap[pos/2]]);
            swap(heap[pos],heap[pos/2]);
            pos/=2;
        }
    }


    void sink(int pos)
    {
        int next;
        while(2*pos<(heap.size()))
        {
            next=2*pos;
            if(next<=(heap.size()-2) && heap[next+1]->GetExpeTime()<heap[next]->GetExpeTime())
                next++;
            if(heap[pos]->GetExpeTime()<=heap[next]->GetExpeTime())
                break;

            swap(umap[heap[pos]],umap[heap[next]]);
            swap(heap[pos],heap[next]);
            pos=next;
        }
    }
public:
    TimerHeap()
    {
        shaobing=new TimerNode(0,nullptr,nullptr);
        shaobing->changeTime(1);
        heap.push_back(shaobing);//哨兵
        umap[shaobing]=0;
    }
    ~TimerHeap()
    {
        if(shaobing)
            delete shaobing;
    }
    void push(TimerNode* ele)
    {

        heap.push_back(ele);
        umap[ele]=heap.size()-1;
        swim(heap.size()-1);

    }

    void pop()
    {
        TimerNode* topNode=heap[1];
        umap.erase(topNode);
        heap[1]=heap.back();
        umap[heap[1]]=1;
        heap.pop_back();
        sink(1);

    }

    TimerNode* top()
    {
        return heap[1];
    }

    void addjust(TimerNode* ele,time_t delay)//延长定时器的到期时间
    {

        ele->addTime(delay);
        int index=umap[ele];
        sink(index);

    }

    void deleteTimer(TimerNode* ele)
    {

        ele->changeTime(1);
        int index=umap[ele];
        swim(index);
        pop();

    }

    int size()
    {
        return heap.size()-1;
    }

    void processTop()//假设堆顶超时，处理堆顶计时器（调用回调函数并pop）
    {

        if(size())
        {
            TimerNode* node=top();
            if(node->callBackFunc)
                    node->callBackFunc(node->m_userdata);
            pop();
        }

    }

    void Print()
    {
        for(int i=0;i<heap.size();i++)
        {
            cout<<i<<": "<<heap[i]->GetExpeTime()<<"\n";
        }
        cout<<"*******************\n";
    }
};


#endif // TIMER_H_INCLUDED
