#ifndef HTTP_H_INCLUDED
#define HTTP_H_INCLUDED
#include "Buffer.h"
#include<sys/stat.h>

const string m_doc_root="/home/moocos/CodeBlockWebServer/WebServer";//WebServer根目录
//定义http响应的一些状态信息
const string ok_200_title = "OK";
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
private:
    int m_socket;//该http对象对应的连接socket
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
    struct stat* m_file_stat;//文件状态
    struct iovec* m_iov;//用于发送响应报文的iovec,第一个iovec指向用户写缓冲区，第二个iovec指向要发送的文件
    int m_iov_cnt;//iovec数组元素个数

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
        CLOSED_CONNECTION
    };

private:
    LINE_STATUS parse_line();//从状态机解析缓冲区中的一行
    HTTP_CODE process_read();//主状态机解析http请求报文
    HTTP_CODE parse_request_line(const string& text);//解析请求行
    HTTP_CODE parse_header(const string& text);//解析请求首部
    HTTP_CODE do_request();//报文响应函数
    void add_response(const string text);//将text写入到用户写缓冲区中
    void add_status_line(const string status_code,const string reason);//生成状态行，将其写入用户写缓冲区中
    void add_content_length(size_t len);//添加内容长度
    void add_content_type();//添加内容类型
    void add_connection();//添加连接状态
    void add_black_line();//添加\r\n
    void add_headers(size_t len);//生成响应首部，将其写入用户写缓冲区中
    void add_content(const string text);//添加内容
    bool process_write(HTTP_CODE ret);//生成响应报文，将其写入用户写缓冲区中

public:
    Http(int sockfd)
    {
        m_socket=sockfd;
        m_iov_cnt=2;
        m_iov=(struct iovec*)malloc(m_iov_cnt*sizeof(struct iovec));
        init();
    }

    void init()
    {
        m_check_status=CHECK_REQUESTLINE;
        m_readbuffer.init();
        m_writebuffer.init();
        m_content_length=0;
        m_string="";
        m_method="GET";
        m_url="";
        m_version="";
        m_linger=false;
        m_host="";
        m_file_type="text/html";
    }

};
#endif // HTTP_H_INCLUDED
