#ifndef TIMER_H_INCLUDED
#define TIMER_H_INCLUDED
#include <time.h>

class Timer
{
public:
    Timer(time_t expected_time):m_expected_time(expected_time){}
    void timeout_event();
    time_t m_expected_time;
    Timer* next;
    Timer* pre;
};

class TimerList//升序定时器列表,并且进行定时器资源的分配与释放
{
private:
    Timer* head;
    Timer* tail;
private:
    void Insert(Timer* t);//插入定时器(不分配资源)
    void Remove(Timer* t);//移除定时器(不释放资源)
public:
    TimerList()
    {
        head=new Timer(0);
        tail=new Timer(0);

        head->next=tail;
        tail->pre=head;
    }
    ~TimerList()
    {
        while(head->next && head->next!=tail)//删除所有节点（除了头尾节点）
        {
            Timer* temp=head->next;
            Remove(head->next);
            delete temp;
        }

        delete head;
        delete tail;

    }

    Timer* Insert(time_t t);//动态分配定时器并插入到合适位置
    void Adjust(Timer* t);//Timer的超时时间被更改，因此需要调整定时器的位置
    void ProcessTimeout();//处理超时的定时器，执行其对应超时事件,从List移除并释放资源
};

#endif // TIMER_H_INCLUDED
