#include<queue>
#include<string>
#include<pthread.h>
#include<stdio.h>
#include<sys/time.h>
#include<stdio.h>
#include<stdarg.h>
#include<string.h>
#include "Locker.h"
#include "Log.h"
using namespace std;

bool BlockQueue::push(const string& task)//生产者
{
        loc.lock();

        if(m_q.size()>=m_max_queue_size)
        {
            loc.unlock();
            return false;
        }


        m_q.push(task);
        loc.unlock();

        cnd.broadcast();

        return true;
}

void BlockQueue::pop(string& ret)//消费者
{
        loc.lock();
        while(m_q.size()==0)
            cnd.wait(loc.get());

        ret=m_q.front();
        m_q.pop();

        loc.unlock();
}


bool Log::init(const char* dir,const char* filename,size_t max_queue_size,size_t max_lines,size_t buffer_size)
    {
        if(max_queue_size>0)//采取异步写入机制
        {
            m_is_asy=true;
            m_bqueue=new BlockQueue(max_queue_size);

            pthread_create(&m_thread,NULL,process_task,NULL);
            pthread_detach(m_thread);
        }
        else
        {
            m_is_asy=false;
            m_bqueue=NULL;
        }

        m_buffer_size=buffer_size;
        m_buffer=new char[m_buffer_size];
        m_max_lines=max_lines;
        strcpy(m_dir_name,dir);
        strcpy(m_file_name,filename);

        time_t t = time(NULL);
        struct tm *sys_tm = localtime(&t);
        struct tm my_tm = *sys_tm;

        m_today=my_tm.tm_mday;

        //写好文件全名
        snprintf(logfile_fullname,255,"%s/%d_%02d_%02d_%s",dir,my_tm.tm_year+1900,my_tm.tm_mon+1,my_tm.tm_mday,filename);

        //打开日志文件，如果不存在就创建文件
        m_fp=fopen(logfile_fullname,"a");
        if(m_fp==NULL)
            return false;
        return true;
    }


//将日志信息格式化输出（同步则直接输出到日志文件，异步则将其加入阻塞队列）
    void Log::write_log(Log_mode level, const char *format, ...)
    {
        char mode[16];
        time_t t = time(NULL);
        struct tm *sys_tm = localtime(&t);
        struct tm my_tm = *sys_tm;

        //日志分级
        switch(level)
        {
       case DEBUG:
          strcpy(mode, "[debug]:");
           break;
       case INFO:
           strcpy(mode, "[info]:");
           break;
       case WARN:
           strcpy(mode, "[warn]:");
           break;
       case ERRO:
           strcpy(mode, "[erro]:");
           break;
       default:
           strcpy(mode, "[info]:");
           break;
       }

       loc.lock();

       //判断是否需要打开另一个日志文件
       if(m_today!=my_tm.tm_mday || (m_count!=0 && (m_count%m_max_lines)==0))//当前日期与日志文件日期不符合，或者当前行数超出了最大行数
       {
           fflush(m_fp);
           fclose(m_fp);

           if(m_today!=my_tm.tm_mday)//当前日期与日志文件日期不符合
           {
                snprintf(logfile_fullname,255,"%s/%d_%02d_%02d_%s",m_dir_name,my_tm.tm_year+1900,my_tm.tm_mon+1,my_tm.tm_mday,m_file_name);
                m_today=my_tm.tm_mday;
                m_count=0;
           }
           else//当前行数超出了最大行数
           {
                snprintf(logfile_fullname,255,"%s/%d_%02d_%02d_%s_%d",m_dir_name,my_tm.tm_year+1900,my_tm.tm_mon+1,my_tm.tm_mday,m_file_name,m_count/m_max_lines);
           }

           m_fp=fopen(logfile_fullname,"a");
       }

       loc.unlock();

        //日志内容格式：时间+内容
        //根据函数参数生成格式化的日志内容，若为同步机制直接写入到日志文件中，若为异步机制则将其加到阻塞队列中
        va_list args;
        va_start(args,format);

        loc.lock();

        int n = snprintf(m_buffer, 48, "%d-%02d-%02d %02d:%02d:%02d %s ",
                     my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday,
                    my_tm.tm_hour, my_tm.tm_min, my_tm.tm_sec,mode);

        //内容格式化，用于向字符串中打印数据、数据格式用户自定义，返回写入到字符数组str中的字符个数(不包含终止符)
        int m = vsnprintf(m_buffer + n, m_buffer_size-n-2, format, args);
        m_buffer[n + m] = '\n';
        m_buffer[n + m + 1] = '\0';

        string log_content(m_buffer);

        loc.unlock();
        va_end(args);

        if(m_is_asy && !m_bqueue->full())//异步写入并且阻塞队列不满
        {
            m_bqueue->push(log_content);
        }
        else//同步写入
        {
            loc.lock();
            fputs(log_content.c_str(),m_fp);
            fflush(m_fp);
            loc.unlock();
        }


    }
