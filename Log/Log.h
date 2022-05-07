#ifndef LOG_H_INCLUDED
#define LOG_H_INCLUDED
#include<queue>
#include<string>
#include<pthread.h>
#include<stdio.h>
#include<sys/time.h>
#include<stdio.h>
#include<stdarg.h>
#include "Locker.h"
using namespace std;

enum Log_mode
{
    DEBUG=0,
    INFO,
    WARN,
    ERRO
};

class BlockQueue
{
private:
    queue<string> m_q;
    size_t m_max_queue_size;
    locker loc;
    cond cnd;
public:
    BlockQueue(size_t max_queue_size=1000):m_max_queue_size(max_queue_size)
    {

    }

    bool full()
    {
        return m_max_queue_size==m_q.size();
    }
    bool push(const string& task);//生产者
    void pop(string& ret);//消费者
};



class Log
{
private:
    Log()
    {
        close_thread=false;
        m_bqueue=NULL;
        m_buffer=NULL;
        m_is_asy=false;
        m_fp=NULL;
    }
    ~Log()
    {
        if(m_bqueue)
            delete m_bqueue;
        if(m_buffer)
            delete[] m_buffer;
    }
public:
    Log(const Log& l)=delete;
    Log(Log&& l)=delete;
    Log& operator=(const Log& l)=delete;
    Log& operator=(Log&& l)=delete;
private:
    bool m_is_asy;//是否为异步
    bool close_thread;//是否停止异步写线程
    BlockQueue* m_bqueue;//阻塞队列
    size_t m_max_lines;//一个日志文件的最大行数
    size_t m_count;//记录当前日志文件的行数
    char m_dir_name[128];
    char m_file_name[128];
    char logfile_fullname[256];//日志文件的全名（路径+文件名）
    char* m_buffer;//主要用于生成格式化的日志内容，以及将被用于转换成string
    size_t m_buffer_size;//buffer大小
    pthread_t m_thread;//异步写入的写线程
    FILE* m_fp;//指向打开的日志文件
    int m_today;//按天分文件,记录当前时间是那一天
    locker loc;
public:
    static Log* getInstance()//单例模式之局部变量懒汉模式
    {
        static Log m_log_instance;
        return &m_log_instance;
    }
    bool init(const char* dir,const char* filename,size_t max_queue_size=0,size_t max_lines=5000000,size_t buffer_size=2000);


    //将日志信息格式化输出（同步则直接输出到日志文件，异步则将其加入阻塞队列）
    void write_log(Log_mode level, const char *format, ...);


    static void* process_task(void* arg)//从阻塞队列取出日志信息写入到日志文件中
    {
        getInstance()->async_write_log();
        return getInstance();
    }

    void close_log()
    {
        close_thread=true;
    }

private:
    void async_write_log()//从阻塞队列取出日志信息写入到日志文件中
    {
        string single_log;

        while(!close_thread)
        {
            m_bqueue->pop(single_log);
            loc.lock();
            fputs(single_log.c_str(),m_fp);
            fflush(m_fp);
            loc.unlock();
        }
    }

};



#endif // LOG_H_INCLUDED
