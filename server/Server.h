#ifndef SERVER_H_INCLUDED
#define SERVER_H_INCLUDED
#include<errno.h>
#include "MySQL_connection_pool.h"
#include "Timer.h"
#include "Threadpool.h"
#include "Log.h"
#include "Http.h"
const int MAX_FD=65536;//最大文件描述符数
const int MAX_EPOLL_EVENTS=10000;//最大事件数
const int TIME_SLOT=5;//最小超时单位
extern int errno;
class Server
{
private:
    int m_listenfd;//监听socket采用ET模式
    int m_port;
    int m_epollfd;
    epoll_event* m_events;//epoll事件表

    //http类相关
    Http* m_users;//http类对象

    //日志相关
    Log* m_log;

    //线程池相关
    Threadpool<Http*> m_thread_pool;

    //数据库相关
    MySQL_connection_pool* m_mysql_conn_pool;//数据库连接池

    //定时器相关
    SigFrame* m_sigframe;//定时器框架
    int m_read_pipfd;//管道读端

private:
    void httpinit()
    {
        m_users=new Http[MAX_FD];
        Http::m_user_count=0;
    }
    void mysqlinit(size_t mysql_con_num,string user,string pawd,string dbname)
    {
        m_mysql_conn_pool=MySQL_connection_pool::getInstance();
        m_mysql_conn_pool->init(mysql_con_num,"localhost",user,pawd,dbname,0);
        Http::m_conn_pool=m_mysql_conn_pool;
        Http::mysqlInit_userAndpawd();//将数据库的web帐号密码加载到username_to_password
    }
    void threadinit(int thread_num)
    {
        m_thread_pool=Threadpool::getInstance();
        m_thread_pool->init(thread_num,10000);
    }
    void loginit(bool close_log,bool is_async)
    {
        if(!close_log)
        {
            m_log=Log::getInstance();
            if(is_async)//异步日志
                m_log->init("/home/moocos/webserver_logfiles","webserverLog",1000,800000,2000);
            else
                m_log->init("/home/moocos/webserver_logfiles","webserverLog",0,800000,2000);
        }
    }
    void timerinit(int epoll_fd)
    {
        m_sigframe=SigFrame::getInstace();
        m_sigframe->init(epoll_fd,TIME_SLOT);

        m_read_pipfd=m_sigframe->create_pip();//创建双向管道以统一事件源
        //设置信号
        m_sigframe->setsig(SIGALRM);
        m_sigframe->setsig(SIGTERM);
    }
    void eventlisten()//创建监听socket、创建epoll
    {
        m_listenfd=socket(PF_INET,SOCK_STREAM,0);

        //网络地址初始化
        struct sockaddr_in addr;
        memset(addr,0,sizeof(addr));
        addr.sin_port=htons(m_port);
        addr.sin_addr=htonl(INADDR_ANY);

        //初始化监听socket
        int ret=bind(m_listenfd,(struct sockaddr*)&addr,sizeof(addr));
        assert(ret>=0);
        ret=listen(m_listenfd,128);
        assert(ret>=0);

        //初始化epoll
        m_epollfd=epoll_create(5);
        Http::m_epoll_fd=m_epollfd;
        m_events=new epoll_event[MAX_EPOLL_EVENTS];

        //设置为非阻塞
        int old_option=fcntl(m_listenfd,F_GETFL);//获取文件状态标志
        int new_option=old_option | O_NONBLOCK;//设置为非阻塞
        fcntl(m_listenfd,F_SETFL,new_option);//设置文件状态标志

        //向epoll注册listenfd
        epoll_event event;
        event.data.fd=m_listenfd;
        event.events=EPOLLET | EPOLLIN;//监听socket采用ET模式
        epoll_ctl(m_epollfd,EPOLL_CTL_ADD,m_listenfd,&event);
    }
public:
    Server(int port,size_t mysql_con_num,string user,string pawd,string dbname,int thread_num,bool close_log,bool is_async)
    {
        m_port=port;

        eventlisten();

        httpinit();

        mysqlinit(mysql_con_num,user,pawd,dbname);

        threadinit(thread_num);

        loginit(close_log,is_async);

        timerinit(m_epollfd);


    }
    ~Server()
    {
        if(m_users)
            delete[] m_users;
        if(m_events)
            delete[] m_events;
        close(m_listenfd);
        close(m_epollfd);
    }

    void enentloop()
    {
        bool timeout=false;
        bool stop_server=false;//如果有信号SIGTERM，就可以将其置为true以关闭服务器

        while(!stop_server)
        {
            int num=epoll_wait(m_epollfd,m_events,MAX_EPOLL_EVENTS,-1);

            if(num<0 && errno!=EINTR)
            {
                //日志
                break;
            }

            for(int i=0;i<num;i++)
            {
                int sockfd=m_events[i].data.fd;

                if(sockfd==m_listenfd)//处理新到来的连接
                {

                }
                else if(m_events[i] & (EPOLLHUP | EPOLLERR | EPOLLRDHUP))//关闭连接
                {

                }
                else if(sockfd==m_read_pipfd && (m_events[i] & EPOLLIN))//信号事件
                {

                }
                //处理客户连接上的数据
                else if(m_events[i] & EPOLLIN)//可读事件
                {

                }
                else if(m_events[i] & EPOLLOUT)//可写事件
                {

                }

                if(timeout)//将超时事件延后到此处，是因为处理客户连接的数据更加重要
                {

                }
            }
        }
    }


};

#endif // SERVER_H_INCLUDED
