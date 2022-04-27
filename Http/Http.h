#ifndef HTTP_H_INCLUDED
#define HTTP_H_INCLUDED
#include "Buffer.h"


class Http
{
private:
    int m_socket;//该http对象对应的连接socket
    Buffer m_readbuffer;//用户读缓冲区
    Buffer m_writebuffer;//用户写缓冲区
    CHECK_STATUS m_check_status;

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

public:
    Http(int sockfd)
    {
        m_socket=sockfd;
        init();
    }

    void init()
    {
        m_check_status=CHECK_REQUESTLINE;
    }

};
#endif // HTTP_H_INCLUDED
