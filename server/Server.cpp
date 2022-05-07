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
        m_thread_pool->init(thread_num,10000);
    }

void Server::loginit(bool close_log,bool is_async)
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

void Server::timerinit(int epoll_fd)
    {
        m_sigframe=SigFrame::getInstace();
        m_sigframe->init(epoll_fd,TIME_SLOT);

        m_sigframe->create_pip();//创建双向管道以统一事件源
        m_read_pipfd=m_sigframe->getpip0();
        global_pipfds[0]=m_sigframe->getpip0();
        global_pipfds[1]=m_sigframe->getpip1();

        //设置信号
        m_sigframe->setsig(SIGALRM,sig_handler);
        m_sigframe->setsig(SIGTERM,sig_handler);

        m_client_timer=new client_timer[MAX_FD];
    }

void Server::eventlisten()//创建监听socket、创建epoll
    {
        m_listenfd=socket(PF_INET,SOCK_STREAM,0);
        assert(m_listenfd!=-1);
        //网络地址初始化
        struct sockaddr_in addr;
        addr.sin_port=htons(m_port);
        addr.sin_addr.s_addr=inet_addr("127.0.0.1");
        addr.sin_family=AF_INET;

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
                Log::getInstance()->write_log(ERRO,"listenfd accept failed");
                return;
            }

            if(Http::m_user_count>=MAX_FD)
            {
                Log::getInstance()->write_log(WARN,"server internal bussy");
                return;
            }

            //http类对象初始化
            m_users[connfd].init(connfd,client_address);

            //生成定时器
            time_t cur = time(NULL);
            Timer* t=m_sigframe->insert(cur+3*TIME_SLOT,&m_users[connfd]);
            m_client_timer[connfd].m_sockfd=connfd;
            m_client_timer[connfd].m_timer=t;
        }

        return;
    }


void Server::enentloop()
    {
        bool timeout=false;
        bool stop_server=false;//如果有信号SIGTERM，就可以将其置为true以关闭服务器
        m_sigframe->start_tick();
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
                    dealwith_conn();
                }
                else if(m_events[i].events & (EPOLLHUP | EPOLLERR | EPOLLRDHUP))//关闭连接
                {
                    m_users[sockfd].close_conn();
                    Timer* t=m_client_timer[sockfd].m_timer;
                    m_sigframe->remove(t);

                    Log::getInstance()->write_log(INFO,"client close connection");

                }
                else if(sockfd==m_read_pipfd && (m_events[i].events & EPOLLIN))//信号事件
                {
                    char signals[1024];
                    int ret=recv(m_read_pipfd,signals,sizeof(signals),0);
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
                            timeout=true;
                            Log::getInstance()->write_log(INFO,"receive SIGALRM");
                            break;
                        }
                    }
                }
                //处理客户连接上的数据
                else if(m_events[i].events & EPOLLIN)//可读事件
                {
                    bool ret=m_users[sockfd].Read();
                    if(ret==false)//关闭连接
                    {
                        m_users[sockfd].close_conn();
                        Timer* t=m_client_timer[sockfd].m_timer;
                        m_sigframe->remove(t);

                        Log::getInstance()->write_log(INFO,"server close connection");
                    }
                    else
                    {
                        //将任务加入阻塞队列
                        if(m_thread_pool->append(&m_users[sockfd])==false)
                        {
                            m_users[sockfd].close_conn();
                        }

                        //调整定时器
                        Timer* t=m_client_timer[sockfd].m_timer;
                        time_t cur=time(NULL);
                        t->m_expected_time=cur+3*TIME_SLOT;
                        m_sigframe->adjust(t);
                    }


                }
                else if(m_events[i].events & EPOLLOUT)//可写事件
                {
                    bool ret=m_users[sockfd].Write();
                    if(ret==false)//关闭连接
                    {
                        m_users[sockfd].close_conn();
                        Timer* t=m_client_timer[sockfd].m_timer;
                        m_sigframe->remove(t);

                        Log::getInstance()->write_log(INFO,"server close connection");
                    }
                    else
                    {
                        //调整定时器
                        Timer* t=m_client_timer[sockfd].m_timer;
                        time_t cur=time(NULL);
                        t->m_expected_time=cur+3*TIME_SLOT;
                        m_sigframe->adjust(t);
                    }

                }

                if(timeout)//将超时事件延后到此处，是因为处理客户连接的数据更加重要
                {
                    m_sigframe->tick();
                    timeout=false;
                }
            }
        }
    }
