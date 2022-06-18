#include<errno.h>
#include<memory.h>
#include "MySQL_connection_pool.h"
#include "Timer.h"
#include "Threadpool.h"
#include "Log.h"
#include "Http.h"
#include "Server.h"
using namespace std;

int global_pipfds[2];
void sig_handler(int sig)//信号处理函数
{
    char msg=(char)sig;
    send(global_pipfds[1],&msg,1,0);
}


void Server::httpinit()
    {
        m_users=new Http[MAX_FD];
        Http::m_user_count=0;
    }

void Server::mysqlinit(size_t mysql_con_num,string user,string pawd,string dbname)
    {
        m_mysql_conn_pool=MySQL_connection_pool::getInstance();
        m_mysql_conn_pool->init(mysql_con_num,"localhost",user,pawd,dbname,0);
        Http::m_conn_pool=m_mysql_conn_pool;
        Http::mysqlInit_userAndpawd();//将数据库的web帐号密码加载到username_to_password
    }

void Server::threadinit(int thread_num)
    {
        m_thread_pool=Threadpool<Http*>::getInstance();
        m_thread_pool->init(thread_num,100000);
    }

void Server::loginit(bool close_log,bool is_async)
    {
        if(!close_log)
        {
            m_log=Log::getInstance();
            if(is_async)//异步日志
                m_log->init(false,"/home/moocos/webserver_logfiles","webserverLog",100000,800000,2000);
            else//同步日志
                m_log->init(false,"/home/moocos/webserver_logfiles","webserverLog",0,800000,2000);
        }
        else//关闭日志系统
        {
            m_log=Log::getInstance();
            m_log->init(true,NULL,NULL,0,0,0);
        }
    }

void Server::timeout(client_data* userdata)
{
    if(!userdata)
    {
        cout<<"timeout():nullpointer\n";
        return;
    }
    int sockfd=userdata->sockfd;
    m_users[sockfd].close_conn();//关闭连接

    userdata->timer=nullptr;
    userdata->sockfd=-1;

}
void Server::timerinit()
{
    m_timerdata=new client_data[MAX_FD];

    // 创建 timerfd
    m_timerfd = timerfd_create(CLOCK_MONOTONIC, 0); //创建定时器的文件描述符

    //注册到epoll
    epoll_event event;
    event.data.fd=m_timerfd;
    event.events=EPOLLIN;
    epoll_ctl(m_epollfd,EPOLL_CTL_ADD,m_timerfd,&event);

}

void Server::startTiming()//开始计时
{
    unsigned long long thetime=m_timerheap.getTopTime();

    //根据堆顶元素的计时时刻计算下一次timerfd的超时时刻
    startTimer.it_value.tv_sec=thetime/1000;
    startTimer.it_value.tv_nsec=(thetime%1000)*1000000;
    startTimer.it_interval.tv_sec=0;
    startTimer.it_interval.tv_nsec=0;

    thetime=m_timerheap.getTopTime();
    unsigned long long targetTime=startTimer.it_value.tv_sec*1000+startTimer.it_value.tv_nsec/(1000*1000);
    if(targetTime<=thetime)
        startTimer.it_value.tv_sec+=1;

    if(timerfd_settime(m_timerfd, TFD_TIMER_ABSTIME, &startTimer, NULL)==(-1))
        Log::getInstance()->write_log(ERRO,"in evenloop,timerfd ",strerror(errno));
}
void Server::eventlisten()//创建监听socket、创建epoll
    {
        m_listenfd=socket(PF_INET,SOCK_STREAM,0);
        assert(m_listenfd!=-1);

        //设置listenfd为REUSEADDR
        int ret0 = 0;
        int reuse = 1;
        ret0 = setsockopt(m_listenfd, SOL_SOCKET, SO_REUSEADDR,(const void *)&reuse , sizeof(int));
        if (ret0 < 0) {
            Log::getInstance()->write_log(ERRO,"set listenfd SO_REUSEADDR failed");
        }

        //网络地址初始化
        struct sockaddr_in addr;
        addr.sin_port=htons(m_port);
        addr.sin_addr.s_addr=inet_addr("10.0.2.15");
        addr.sin_family=AF_INET;

        //初始化监听socket
        int ret=bind(m_listenfd,(struct sockaddr*)&addr,sizeof(addr));
        assert(ret>=0);
        ret=listen(m_listenfd,65535);
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

void Server::dealwith_conn()
    {

        while(1)//监听socket为ET模式
        {
            struct sockaddr_in client_address;
            socklen_t client_addrlength = sizeof(client_address);
            int connfd = accept(m_listenfd, (struct sockaddr *)&client_address, &client_addrlength);

            if(connfd<0)
            {

                if(errno==EAGAIN || errno==EWOULDBLOCK)
                    return;
                Log::getInstance()->write_log(ERRO,"listenfd accept failed,%s",strerror(errno));
                return;
            }

            if(Http::m_user_count>=MAX_FD)
            {
                Log::getInstance()->write_log(WARN,"server internal bussy");
                return;
            }

            bool is_timerheap_empty=m_timerheap.empty();

            //通过std::bind使得std::function能够指向类成员函数
            std::function<void(client_data *)> Func = std::bind(&Server::timeout,this, std::placeholders::_1);

            //为新连接生成定时器
            m_timerdata[connfd].sockfd=connfd;
            m_timerdata[connfd].timer=nullptr;
            Timer* newTimer=m_timerheap.addTimer(5*TIME_SLOT,Func,&m_timerdata[connfd]);
            m_timerdata[connfd].timer=newTimer;

            //初始化http对象
            m_users[connfd].init(connfd,client_address,newTimer,&m_timerheap);

            //如果一开始timerheap空了，新连接到来后应发起计时
            if(is_timerheap_empty)//此时由于新计时器push到timerheap中，timeheap非空
            {
                startTiming();
            }

        }

        return;
    }


void Server::enentloop()
    {
        bool stop_server=false;
        while(!stop_server)
        {
            int num=epoll_wait(m_epollfd,m_events,MAX_EPOLL_EVENTS,-1);

            if(num<0 && errno!=EINTR)
            {
                Log::getInstance()->write_log(ERRO,"epoll_wait error");
                continue;
            }

            for(int i=0;i<num;i++)
            {
                int sockfd=m_events[i].data.fd;

                if(sockfd==m_listenfd)//处理新到来的连接（ET模式）
                {
                    //Log::getInstance()->write_log(DEBUG,"in evenloop,new conn");
                    dealwith_conn();
                }
                else if(m_events[i].events & (EPOLLHUP | EPOLLERR | EPOLLRDHUP))//关闭连接
                {
                    //Log::getInstance()->write_log(DEBUG,"in evenloop,HUP OR ERR");
                    //删除定时器
                    Timer* t=m_timerdata[sockfd].timer;
                    m_timerheap.delTimer(t);
                    m_timerdata[sockfd].timer=nullptr;
                    m_timerdata[sockfd].sockfd=-1;

                    //关闭连接
                    m_users[sockfd].close_conn();
                    //Log::getInstance()->write_log(INFO,"client close connection,fd is:%d",sockfd);

                }


                else if(sockfd==m_timerfd)//超时事件
                {

                    //超时后必须读取timer 文件描述符上的数据，否则timerfd无法正常运行
                    uint64_t res;
                    read(m_timerfd, &res, sizeof(res));

                    //若堆顶计时器超时则处理它
                    m_timerheap.takeAllTimeout();

                    if(!m_timerheap.empty())
                    {
                        startTiming();
                    }

                }

                //处理客户连接上的数据
                else if(m_events[i].events & EPOLLIN)//可读事件
                {
                    Timer* t=m_timerdata[sockfd].timer;
                    m_timerheap.addjust(t,15*TIME_SLOT);//延长15s
                    m_users[sockfd].task_type=1;
                    Threadpool<Http*>::getInstance()->append(&m_users[sockfd]);


                }
                else if(m_events[i].events & EPOLLOUT)//可写事件
                {
                    Timer* t=m_timerdata[sockfd].timer;
                    m_timerheap.addjust(t,15*TIME_SLOT);//延长15s
                    m_users[sockfd].task_type=2;
                    Threadpool<Http*>::getInstance()->append(&m_users[sockfd]);

                }

            }

        }
    }
