#ifndef SERVER_H_INCLUDED
#define SERVER_H_INCLUDED
#include<errno.h>
#include "MySQL_connection_pool.h"
#include "Timer.h"
#include "Threadpool.h"
#include "Log.h"
#include "Http.h"
const int MAX_FD=65536;//最大文件描述符数
const int MAX_EPOLL_EVENTS=65536;//最大事件数
extern int errno;

class Server
{
private:
    int m_listenfd;//监听socket采用ET模式
    unsigned short m_port;
    int m_epollfd;
    epoll_event* m_events;//epoll事件表

    //http类相关
    Http* m_users;//http类对象

    //日志相关
    Log* m_log;

    //线程池相关
    Threadpool<Http*>* m_thread_pool;

    //数据库相关
    MySQL_connection_pool* m_mysql_conn_pool;//数据库连接池

    //定时器相关
    TimerManager m_timerheap;
    client_data* m_timerdata;
    int m_timerfd;
    struct itimerspec startTimer;//用于设置定时器的时长

private:
    void httpinit();
    void mysqlinit(size_t mysql_con_num,string user,string pawd,string dbname);
    void threadinit(int thread_num);
    void loginit(bool close_log,bool is_async);
    void eventlisten();//创建监听socket、创建epoll
    void dealwith_conn();

    void timerinit();
    void startTiming();//开始计时
    void timeout(client_data* userdata);//定时器回调函数

public:
    Server(unsigned short port,size_t mysql_con_num,string user,string pawd,string dbname,int thread_num,bool close_log,bool is_async)
    {
        m_users=NULL;
        m_events=NULL;
        m_port=port;

        loginit(close_log,is_async);

        eventlisten();

        httpinit();

        mysqlinit(mysql_con_num,user,pawd,dbname);

        threadinit(thread_num);

        timerinit();

    }
    ~Server()
    {
        if(m_users)
            delete[] m_users;
        if(m_events)
            delete[] m_events;
        if(m_timerdata)
            delete[] m_timerdata;
        close(m_listenfd);
        close(m_epollfd);

    }

    void enentloop();

};

#endif // SERVER_H_INCLUDED
