#ifndef HTTP_H_INCLUDED
#define HTTP_H_INCLUDED
#include<sys/stat.h>
#include<sys/epoll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include<sys/mman.h>
#include<unistd.h>
#include<unordered_map>
#include <atomic>
#include<sstream>
#include "Buffer.h"
#include "MySQL_connection_pool.h"
#include "Locker.h"
#include "Log.h"
#include "Timer.h"
#include "Threadpool.h"
#include "UpLoadManager.h"
#include "Token.h"

const string m_doc_root="/home/moocos/CodeBlockWebServer/WebServer/html_files";
const int cache_delay_time=20;//http缓存过期时间
//定义http响应的一些状态信息
const string ok_200_title = "OK";
const string ok_206_title = "Partial Content";
const string ok_304_title="Not Modified";
const string error_416_title="Range Not Satisfiable";
const string error_416_form="Your range request is out of range";
const string error_400_title = "Bad Request";
const string error_400_form = "Your request has bad syntax or is inherently impossible to staisfy.\n";
const string error_403_title = "Forbidden";
const string error_403_form = "You do not have permission to get file form this server.\n";
const string error_404_title = "Not Found";
const string error_404_form = "The requested file was not found on this server.\n";
const string error_500_title = "Internal Error";
const string error_500_form = "There was an unusual problem serving the request file.\n";

class Http
{
public:
    enum LINE_STATUS
    {
        LINE_OK=0,//完整读取一行
        LINE_BAD,//该行语法错误
        LINE_OPEN//读取的行不完整
    };

    enum CHECK_STATUS
    {
        CHECK_REQUESTLINE=0,//解析请求行
        CHECK_HEADER,//解析请求首部
        CHECK_CONTENT//解析报文体
    };

    enum HTTP_CODE//http请求的处理结果
    {
        NO_REQUEST=0,//请求不完整
        GET_REQUEST,//获得了完整的请求
        BAD_REQUEST,//请求报文的语法有错误
        INTERNAL_ERROR,//服务器内部错误
        NO_RESOURCE,
        FORBIDDEN_REQUEST,
        FILE_REQUEST,
        CLOSED_CONNECTION,
        FILE_UPLOAD,//上传文件
        FILE_PARTIAL_REQUEST,//范围文件请求
        RANGE_NO_SATISFIABLE,//范围文件请求的range超出范围
        FILE_NO_MODIFIED//用于缓存控制，表示可以使用缓存文件
    };
private:
    int m_socket;//该http对象对应的连接socket
    sockaddr_in m_client_address;//用户地址
    Buffer m_readbuffer;//用户读缓冲区
    Buffer m_writebuffer;//用户写缓冲区
    CHECK_STATUS m_check_status;//主状态机的状态
    size_t m_content_length;//请求报文的报文体大小
    string m_string;//请求报文的报文体
    string m_method;//请求报文中的方法
    string m_url;//请求报文中的url
    string m_version;//请求报文中http的版本
    bool m_linger;//为true表示为长连接
    string m_host;//主机名
    string m_real_file;//所请求文件的jue对路径
    char* m_file_addres;//所请求文件映射到内存后的路径
    string m_file_type;//所请求文件的类型
    struct stat m_file_stat;//文件状态
    struct iovec* m_iov;//用于发送响应报文的iovec,第一个iovec指向用户写缓冲区，第二个iovec指向要发送的文件
    int m_iov_cnt;//iovec数组元素个数
    bool cgi_succ;//登陆校验成功

    Timer* m_timer;
    TimerManager* m_timerheap;
    //locker m_loc1;//用于保护任意时刻http对象只能被一个对象操纵

    UploadManager upload_manager_;//文件上传
    string m_boundary;
    string upload_status;//文件上传状态

    //范围请求
    bool if_range;
    long long  range_left;
    long long range_right;
    string range_str;

    //http缓存控制
    bool if_request_has_Last_Modified;
    string Modified_Since;
    bool if_request_has_If_None_Match;
    string If_None_Match_Etag;

    //token,token放置在cookie
    bool if_request_has_token;
    bool log_succ;//登陆成功
    Token m_token;
    string m_user_id;

private:
    LINE_STATUS parse_line();//从状态机解析缓冲区中的一行
    HTTP_CODE process_read();//主状态机解析http请求报文
    HTTP_CODE parse_request_line(const string& text);//解析请求行
    HTTP_CODE parse_header(const string& text);//解析请求首部
    HTTP_CODE do_request();//报文响应函数
    void add_response(string text);//将text写入到用户写缓冲区中
    void add_status_line(string status_code,string reason);//生成状态行，将其写入用户写缓冲区中
    void add_content_length(size_t len);//添加内容长度
    void add_content_type();//添加内容类型
    void add_connection();//添加连接状态

    void add_content_disposition();//添加content_disposition字段用于弹出文件下载对话框

    //范围请求
    void add_Accept_Ranges();//添加Accept_Ranges字段
    void add_Content_Range();//添加Content_Range字段

    //缓存控制
    void add_Date();//添加Date字段
    void add_Cache_Control();//添加cache-control字段
    void add_Expires();//添加Expires字段
    void add_Last_Modified();//添加Last_Modified字段
    void add_Etag();

    //Token
    void add_Access_Control_Expose_Headers();//允许客户端获取一些原本获取不到的首部
    void add_Access_Control_Allow_Origin();
    void add_Token();


    void add_black_line();//添加\r\n
    void add_headers(size_t len);//生成响应首部，将其写入用户写缓冲区中
    void add_content(string text);//添加内容
    bool process_write(HTTP_CODE ret);//生成响应报文，将其写入用户写缓冲区中

    string get_etag()
    {
        std::stringstream sstream;
        sstream << std::hex << m_file_stat.st_mtime;
        std::string Last_Modified_Time_Hex_String = sstream.str();

        std::stringstream sstream1;
        sstream1 << std::hex << m_file_stat.st_size;
        std::string Content_Length_Hex_String = sstream1.str();

        string Etag="W/";
        Etag+="\"";
        Etag+=Last_Modified_Time_Hex_String;
        Etag+="-";
        Etag+=Content_Length_Hex_String;
        Etag+="\"";

        return Etag;
    }
    string get_timestamp(time_t raw_time)//获取http 格林尼治时间戳
    {
        //格林尼治时间戳格式  <day-name>, <day> <month> <year> <hour>:<minute>:<second> GMT
        struct tm time_info;

        gmtime_r(&raw_time,&time_info);

        string Day_name[7]={"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
        string Month[12]={"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};
        string Date="";

        //day-name,一周中的第几天，范围从 0 到 6
        Date+=Day_name[time_info.tm_wday];
        Date+=", ";

        //一月中的第几天，范围从 1 到 31
        if(time_info.tm_mday<=9)
            Date+="0";
        Date+=to_string(time_info.tm_mday);
        Date+=" ";

        //月份，范围从 0 到 11
        Date+=Month[time_info.tm_mon];
        Date+=" ";

        //自 1900 起的年数
        Date+=to_string(time_info.tm_year+1900);
        Date+=" ";

        //小时，范围从 0 到 23
        if(time_info.tm_hour<=9)
            Date+="0";
        Date+=to_string(time_info.tm_hour);
        Date+=":";

        //分，范围从 0 到 59
        if(time_info.tm_min<=9)
            Date+="0";
        Date+=to_string(time_info.tm_min);
        Date+=":";

        //秒，范围从 0 到 59
        if(time_info.tm_sec<=9)
            Date+="0";
        Date+=to_string(time_info.tm_sec);
        Date+=" ";

        Date+="GMT";

        return Date;
    }
    void set_noblocking(int fd)//将文件描述符设置为非阻塞
    {
        int old_option=fcntl(fd,F_GETFL);//获取文件状态标志
        int new_option=old_option | O_NONBLOCK;//设置为非阻塞
        fcntl(fd,F_SETFL,new_option);//设置文件状态标志
    }
    void unmap()
    {
        if (m_file_addres)
        {
            munmap(m_file_addres, m_file_stat.st_size);
            m_file_addres = 0;
        }
    }

    void add_fd_to_epoll(int fd)//将fd添加到epoll空间，ET模式，EPOLLIN | EPOLLONESHOT
    {
        epoll_event event;
        event.data.fd=fd;
        event.events= EPOLLET | EPOLLIN | EPOLLONESHOT | EPOLLRDHUP;
        epoll_ctl(m_epoll_fd,EPOLL_CTL_ADD,fd,&event);
        set_noblocking(fd);
    }

    void remove_fd_from_epoll(int fd)//从epoll空间中删除fd
    {
        epoll_ctl(m_epoll_fd,EPOLL_CTL_DEL,fd,0);
    }

    void mod_fd_in_epoll(int fd,int old_events)//重置EPOLLONESHOT
    {
        epoll_event event;
        event.data.fd=fd;
        event.events= old_events | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;
        epoll_ctl(m_epoll_fd,EPOLL_CTL_MOD,fd,&event);
    }
public:
    static int m_epoll_fd;
    static atomic<int> m_user_count;
    static MySQL_connection_pool* m_conn_pool;//数据库连接池
    int task_type;//指示线程池请求队列中的http对象应执行的任务类型，1从socket读取数据,报文解析报文撰写，2向socket发送数据
public:
    Http()
    {
        m_iov=NULL;
        m_iov_cnt=1;
        m_iov=(struct iovec*)malloc(m_iov_cnt*sizeof(struct iovec));

    }
    ~Http()
    {
        if(m_iov)
            free(m_iov);
    }

    static void mysqlInit_userAndpawd();//将数据库的帐号密码加载到username_to_password
    void init();//维持同一个连接下的初始化;
    //新连接的初始化
    void init(int sockfd, const sockaddr_in &addr,Timer* t,TimerManager* timerheap);
    void close_conn();//关闭连接
    bool Read();//将数据从内核读缓冲区读取到用户的读缓冲区,返回false说明对方关闭连接或读取出错
    bool Write();//将数据从用户写缓冲区、文件映射地址 写到内核写缓冲区中，返回false说明要关闭连接
    void process();

};
#endif // HTTP_H_INCLUDED
