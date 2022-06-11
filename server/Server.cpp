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

void set_noblocking(int fd)//将文件描述符设置为非阻塞
    {
        int old_option=fcntl(fd,F_GETFL);//获取文件状态标志
        int new_option=old_option | O_NONBLOCK;//设置为非阻塞
        fcntl(fd,F_SETFL,new_option);//设置文件状态标志
    }

void setsig(int sig,void(handler)(int))//设置信号
    {
        struct sigaction sa;
        sa.sa_handler=handler;
        sa.sa_flags=SA_RESTART;//如果系统调用被打断了则重新执行
        sigfillset(&sa.sa_mask);//屏蔽所有信号以避免信号竞态
        assert(sigaction(sig,&sa,NULL)!=-1);//注册信号处理函数
    }

void Server::timerinit()
    {
        //创建双向管道以统一事件源
        int ret = socketpair(PF_UNIX, SOCK_STREAM, 0, m_pipfds);
        assert(ret!=-1);

        //将管道描述符设置为非阻塞
        set_noblocking(m_pipfds[0]);
        set_noblocking(m_pipfds[1]);

        //向epoll空间注册读端管道,以统一事件源
        epoll_event event;
        event.data.fd=m_pipfds[0];
        event.events=EPOLLIN;
        epoll_ctl(m_epollfd,EPOLL_CTL_ADD,m_pipfds[0],&event);

        global_pipfds[0]=m_pipfds[0];
        global_pipfds[1]=m_pipfds[1];

        //设置信号
        setsig(SIGALRM,sig_handler);
        setsig(SIGTERM,sig_handler);

        m_issigalarming=false;

    }

void Server::timeoutCallBack(client_data* Data)
{
    Log::getInstance()->write_log(INFO,"server close connection becuase conn timeout,fd is:%d",Data->sockfd);
    m_users[Data->sockfd].close_conn();

}

void Server::tick()
{
    while(m_timerheap.size())
    {
        TimerNode* temp=m_timerheap.top();
        int tempfd=temp->m_userdata->sockfd;
        time_t tp=m_timerheap.top()->GetExpeTime();
        time_t cur=time(nullptr);
        if(tp<=cur)//堆顶计时器过期了
        {
            m_timer_user_data[tempfd].timer=nullptr;
            m_timerheap.processTop();

            //Log::getInstance()->write_log(INFO,"conn timwout so server close connection");
        }
        else
            break;
    }
    if(m_timerheap.size()==0)
    {
        m_issigalarming=false;
        return;
    }
    time_t cur=time(nullptr);
    time_t delay=m_timerheap.top()->GetExpeTime()-cur;
    if(delay<=0)
        delay=5;


    //cur=time(nullptr);
    //cout<<"in tick("<<delay<<"): "<<cur<<"\n";
    alarm(delay);
    m_issigalarming=true;

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

            //生成定时器
            //向定时器注册回调函数
            std::function<void(client_data *)> cb=std::bind(&Server::timeoutCallBack,this, std::placeholders::_1);
            //修改m_timer_user_data
            m_timer_user_data[connfd].sockfd=connfd;
            m_timer_user_data[connfd].timer=nullptr;
            TimerNode* newTimer=new TimerNode(3*TIME_SLOT,&m_timer_user_data[connfd],cb);
            m_timer_user_data[connfd].timer=newTimer;
            m_timerheap.push(newTimer);
            if(!m_issigalarming)//如果当前没有SIGALARM定时，则触发SIGALARM
            {
                //time_t cur=time(nullptr);
                //cout<<"in dealwithconn(): "<<cur<<"\n";
                alarm(3*TIME_SLOT);
                m_issigalarming=true;
            }
            //http类对象初始化
            m_users[connfd].init(connfd,client_address,newTimer,&m_timerheap);
        }

        return;
    }


void Server::enentloop()
    {
        bool is_timeout=false;
        bool stop_server=false;//如果有信号SIGTERM，就可以将其置为true以关闭服务器
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
                    TimerNode* t=m_timer_user_data[sockfd].timer;
                    m_timer_user_data[sockfd].timer=nullptr;
                    m_timerheap.deleteTimer(t);
                    m_users[sockfd].close_conn();
                    Log::getInstance()->write_log(INFO,"client close connection,fd is:%d",sockfd);

                }

                else if(sockfd==m_pipfds[0] && (m_events[i].events & EPOLLIN))//信号事件
                {
                    char signals[1024];
                    int ret=recv(m_pipfds[0],signals,sizeof(signals),0);
                    if(ret==-1)
                        continue;
                    for(int i=0;i<ret;i++)
                    {
                        switch(signals[i])
                        {
                        case SIGTERM:
                            stop_server=true;
                            Log::getInstance()->write_log(INFO,"receive SIGTERM");
                            Log::getInstance()->close_log();//停止写线程的循环
                            Threadpool<Http*>::getInstance()->stop_threads();//停止线程池线程的循环
                            break;
                        case SIGALRM:
                            Log::getInstance()->write_log(INFO,"receive SIGALRM");
                            is_timeout=true;
                            break;
                        }
                    }
                }

                //处理客户连接上的数据
                else if(m_events[i].events & EPOLLIN)//可读事件
                {
                    TimerNode* temp_timer=m_timer_user_data[sockfd].timer;
                    m_timerheap.addjust(temp_timer,3*TIME_SLOT);
                    m_users[sockfd].task_type=1;
                    Threadpool<Http*>::getInstance()->append(&m_users[sockfd]);


                }
                else if(m_events[i].events & EPOLLOUT)//可写事件
                {
                    TimerNode* temp_timer=m_timer_user_data[sockfd].timer;
                    m_timerheap.addjust(temp_timer,3*TIME_SLOT);
                    m_users[sockfd].task_type=2;
                    Threadpool<Http*>::getInstance()->append(&m_users[sockfd]);

                }

            }

            if(is_timeout)
            {
                tick();
                is_timeout=false;
            }
        }
    }
