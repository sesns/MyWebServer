#include <iostream>
#include <time.h>
#include "Timer.h"
#include "Log.h"
#include "Http.h"

/*
void Timer::timeout_event()
{
    Http* m_user_p=(Http*)m_user;
    m_user_p->close_conn();//关闭连接
    Log::getInstance()->write_log(INFO,"concection timeout so the server close connection");
}

Timer* TimerList::Insert(time_t cur_time,void* user)
{
    loc.lock();

    Http* user_p=(Http*)user;
    Timer* t=new Timer(cur_time,user_p);

    Timer* cur=t;
    Timer* p=head->next;
    while(p!=tail && ((p->m_expected_time)<(t->m_expected_time)))
        p=p->next;

    Timer* Pre_node=p->pre;
    Pre_node->next=cur;
    cur->next=p;
    p->pre=cur;
    cur->pre=Pre_node;

    loc.unlock();

    return t;
}

void TimerList::Adjust(Timer* t)
{
    if(!t)
        return;

    loc.lock();

    //从链表中删除
    Timer* p_node=t->pre;
    Timer* n_node=t->next;

    p_node->next=n_node;
    n_node->pre=p_node;

    //加入链表
    Timer* cur=t;
    Timer* p=head->next;
    while(p!=tail && p->m_expected_time<t->m_expected_time)
        p=p->next;

    Timer* Pre_node=p->pre;
    Pre_node->next=cur;
    cur->next=p;
    p->pre=cur;
    cur->pre=Pre_node;

    loc.unlock();
}


void TimerList::ProcessTimeout()
{
    loc.lock();

    Timer* cur=head->next;
    time_t cur_time=time(NULL);
    while(cur!=nullptr && cur!=tail && cur->m_expected_time<cur_time)
    {
        Timer* temp=cur;
        cur=cur->next;

        temp->timeout_event();//执行超时对应的动作

        //删除定时器
        Timer* Pre_node=temp->pre;
        Timer* Next_node=temp->next;
        Pre_node->next=Next_node;
        Next_node->pre=Pre_node;

        delete temp;

    }

    loc.unlock();

}
*/
