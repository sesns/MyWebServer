#include "MySQL_connection_pool.h"
#include<queue>
#include<mysql.h>
#include<vector>
#include "Log.h"
using namespace std;

void MySQL_connection_pool::init(size_t max_con_num,string host,string username,string password,string DBname,unsigned int port)
{
    m_max_con_num=max_con_num;
    m_host=host;
    m_username=username;
    m_password=password;
    m_DBname=DBname;
    m_port=port;

    for(int i=0;i<max_con_num;i++)
    {
        MYSQL* conn_ptr=NULL;
        conn_ptr=mysql_init(conn_ptr);
        if(!conn_ptr)
        {
            Log::getInstance()->write_log(ERRO,"MySQL_connection_pool::init,mysql_init failed. %s",mysql_error(conn_ptr));
            exit(1);
        }

        conn_ptr=mysql_real_connect(conn_ptr,m_host.c_str(),m_username.c_str(),m_password.c_str(),m_DBname.c_str(),m_port,NULL,0);
        if(!conn_ptr)
        {
            Log::getInstance()->write_log(ERRO,"MySQL_connection_pool::init,mysql_connection failed. %s",mysql_error(conn_ptr));
            exit(1);
        }

        m_queue.push(conn_ptr);
        m_conns.push_back(conn_ptr);
        m_free_con_num+=1;

    }

    xinhaoliang=sem(m_free_con_num);
}

void MySQL_connection_pool::DestroyPool()//销毁连接池
{
    loc.lock();
    for(int i=0;i<m_conns.size();i++)
    {
        MYSQL* con=m_conns[i];
        mysql_close(con);
    }

    loc.unlock();
}

MYSQL* MySQL_connection_pool::getCon()//获取连接
{
    xinhaoliang.wait();

    loc.lock();

    MYSQL* con=m_queue.front();
    m_queue.pop();
    m_free_con_num--;
    m_used_con_num++;

    loc.unlock();

    return con;
}

bool MySQL_connection_pool::releaseCon(MYSQL* conn)//释放连接
{
    if(!conn)
        return NULL;

    loc.lock();

    m_queue.push(conn);
    m_free_con_num++;
    m_used_con_num--;

    loc.unlock();

    xinhaoliang.post();
    return true;
}
