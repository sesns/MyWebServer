#include <iostream>
#include <time.h>
#include "Timer.h"

void Timer::timeout_event()
{
    m_user->close_conn();//关闭连接
}

void TimerList::Insert(Timer* t)
{
    if(!t)
        return;
    Timer* cur=t;
    Timer* p=head->next;
    while(p!=tail && p->m_expected_time<t->m_expected_time)
        p=p->next;

    Timer* Pre_node=p->pre;
    Pre_node->next=cur;
    cur->next=p;
    p->pre=cur;
    cur->pre=Pre_node;

}


void TimerList::Remove(Timer* t)
{
    if(!t)
        return;
    Timer* Pre_node=t->pre;
    Timer* Next_node=t->next;

    Pre_node->next=Next_node;
    Next_node->pre=Pre_node;
}

Timer* TimerList::Insert(time_t t,Http* user)
{
    Timer* cur=new Timer(t,user);
    Insert(cur);
    return cur;
}

void TimerList::Adjust(Timer* t)
{
    Remove(t);
    if(t->next==tail)//该定时器超时时间最大，不用调整位置
        return;
    if(t->next->m_expected_time>=t->m_expected_time)//该定时器超时时间调整后仍<=下一个定时器超时时间，不用调整位置
        return;
    Insert(t);
}


void TimerList::ProcessTimeout()
{
    Timer* cur=head->next;
    time_t cur_time=time(NULL);
    while(cur!=tail && cur->m_expected_time<cur_time)
    {
        Timer* temp=cur;
        cur=cur->next;

        temp->timeout_event();//执行超时对应的动作
        Remove(temp);
        delete temp;

    }

}
