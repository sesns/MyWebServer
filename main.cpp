#include <iostream>
#include <unistd.h>
#include <signal.h>
#include <memory.h>
#include <sys/timerfd.h>
#include <sys/time.h>
#include "Server.h"
#include "Token.h"
#include "urlcode.h"
using namespace std;

int main()
{
    cout << "Run\n" ;
    printf("pid=%d\n", getpid());


    unsigned short m_port=9000;
    size_t m_mysql_con_num=4;
    int m_thread_num=3;
    bool m_closelog=false;
    bool m_isaync=true;
    string DBuser="root";
    string DBpawd="1234";
    string DBname="webcounts";


    Server* m_webserver=NULL;
    m_webserver=new Server(m_port,m_mysql_con_num,DBuser,DBpawd,DBname,m_thread_num,m_closelog,m_isaync);

    m_webserver->enentloop();

    if(m_webserver)
        delete m_webserver;

    return 0;
}
