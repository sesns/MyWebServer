#ifndef MYSQL_CONNECTION_POOL_H_INCLUDED
#define MYSQL_CONNECTION_POOL_H_INCLUDED
#include<queue>
#include<mysql.h>
#include<vector>
#include"Locker.h"
using namespace std;
class MySQL_connection_pool
{
private:
    MySQL_connection_pool()
    {
        m_free_con_num=0;
        m_used_con_num=0;
    }
    ~MySQL_connection_pool()
    {
        DestroyPool();
    }
public:
    MySQL_connection_pool(const MySQL_connection_pool& mcp)=delete;
    MySQL_connection_pool(MySQL_connection_pool&& mcp)=delete;
    MySQL_connection_pool& operator=(const MySQL_connection_pool& mcp)=delete;
    MySQL_connection_pool& operator=(MySQL_connection_pool&& mcp)=delete;
    static MySQL_connection_pool* getInstance()//单例模式
    {
        static MySQL_connection_pool m_instance;
        return &m_instance;
    }
private:
    void DestroyPool();//销毁连接池
    size_t m_max_con_num;//最大连接数
    size_t m_free_con_num;//当前空闲连接数
    size_t m_used_con_num;//当前已使用连接数
    queue<MYSQL*> m_queue;//连接池
    vector<MYSQL*> m_conns;//保存所有连接的指针，将来用户释放连接池

    sem xinhaoliang;//信号量
    locker loc;//互斥锁

    string m_host;//主机名
    string m_username;//用户名
    string m_password;//密码
    string m_DBname;//要访问的数据库的名字
    unsigned int m_port;//端口号
public:
    void init(size_t max_con_num,string host,string username,string password,string DBname,unsigned int port);
    MYSQL* getCon();//获取连接
    bool releaseCon(MYSQL* conn);//释放连接
    size_t get_freecon_num()//获取当前空闲连接个数
    {
        return m_free_con_num;
    }

};

class MySQLconRAII
{
private:
    MySQL_connection_pool* m_mysql_con_pool;
    MYSQL* m_con;
public:
    MySQLconRAII(MYSQL** con,MySQL_connection_pool* mysql_con_pool)
    {
        m_mysql_con_pool=mysql_con_pool;
        m_con=m_mysql_con_pool->getCon();

        (*con)=m_con;
    }
    ~MySQLconRAII()
    {
        m_mysql_con_pool->releaseCon(m_con);
    }
};
#endif // MYSQL_CONNECTION_POOL_H_INCLUDED
