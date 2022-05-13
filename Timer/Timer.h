#ifndef TIMER_H_INCLUDED
#define TIMER_H_INCLUDED
#include "Locker.h"
#include <time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <assert.h>
#include<sys/epoll.h>
#include<signal.h>
#include <unistd.h>
#include <fcntl.h>

const int TIME_SLOT=5;//最小超时单位

class Timer
{
public:
    Timer(time_t expected_time,void* user):m_expected_time(expected_time),m_user(user){}
    void timeout_event();
    void* m_user;
    time_t m_expected_time;
    Timer* next;
    Timer* pre;
};

class TimerList//升序定时器列表,并且进行定时器资源的分配与释放
{
private:
    Timer* head;
    Timer* tail;
    locker loc;//定时器列表为共享资源
public:
    TimerList()
    {
        head=NULL;
        tail=NULL;

        head=new Timer(0,NULL);
        tail=new Timer(0,NULL);

        head->next=tail;
        tail->pre=head;
    }
    ~TimerList()
    {
        Timer* p=head->next;
        while(p!=tail)//删除所有节点（除了头尾节点）
        {
            Timer* temp=p;
            p=p->next;

            delete temp;
        }

        if(head)
            delete head;
        if(tail)
            delete tail;

    }

    Timer* Insert(time_t t,void* user);//动态分配定时器并插入到合适位置
    void Adjust(Timer* t);//Timer的超时时间被更改，因此需要调整定时器的位置
    void ProcessTimeout();//处理超时的定时器，执行其对应超时事件,从List移除并释放资源
};


class SigFrame
{
private:
    SigFrame()=default;
    ~SigFrame()
    {
        close(pipfds[0]);
        close(pipfds[1]);
    }
public:
    SigFrame(const SigFrame&)=delete;
    SigFrame(SigFrame&&)=delete;
    SigFrame& operator=(const SigFrame&)=delete;
    SigFrame& operator=(SigFrame&&)=delete;
private:
    TimerList m_timerlist;//定时器列表
    int m_epoll_fd;
    int pipfds[2];//双向管道，【0】为读端，【1】为写端
    char msg;
    int m_time_slot;
    void set_noblocking(int fd)//将文件描述符设置为非阻塞
    {
        int old_option=fcntl(fd,F_GETFL);//获取文件状态标志
        int new_option=old_option | O_NONBLOCK;//设置为非阻塞
        fcntl(fd,F_SETFL,new_option);//设置文件状态标志
    }
public:
    static SigFrame* getInstace()//获取单例
    {
        static SigFrame m_instance;
        return &m_instance;
    }
    void init(int epoll_fd,int time_slot)//初始化
    {
        m_epoll_fd=epoll_fd;
        m_time_slot=time_slot;
    }
    void create_pip()
    {
        //创建双向管道
        int ret = socketpair(PF_UNIX, SOCK_STREAM, 0, pipfds);
        assert(ret!=-1);

        //将管道描述符设置为非阻塞
        set_noblocking(pipfds[0]);
        set_noblocking(pipfds[1]);

        //向epoll空间注册读端管道,以统一事件源
        epoll_event event;
        event.data.fd=pipfds[0];
        event.events=EPOLLIN;
        epoll_ctl(m_epoll_fd,EPOLL_CTL_ADD,pipfds[0],&event);

    }
    void setsig(int sig,void(handler)(int))//设置信号
    {
        struct sigaction sa;
        sa.sa_handler=handler;
        sigfillset(&sa.sa_mask);//屏蔽所有信号以避免信号竞态
        assert(sigaction(sig,&sa,NULL)!=-1);//注册信号处理函数
    }

    int getpip0()
    {
        return pipfds[0];
    }

    int getpip1()
    {
        return pipfds[1];
    }

    void start_tick()//开始跳动
    {
        alarm(m_time_slot);
    }
    void tick()//脉搏函数
    {
        m_timerlist.ProcessTimeout();
        alarm(m_time_slot);
    }



    Timer* insert(time_t t,void* user)//动态分配定时器并插入到合适位置
    {
        return m_timerlist.Insert(t,user);
    }
    void adjust(Timer* t)//Timer的超时时间被更改，因此需要调整定时器的位置
    {
        m_timerlist.Adjust(t);
    }
};
#endif // TIMER_H_INCLUDED
