#include<sys/socket.h>
#include "Http.h"
#include "Buffer.h"

Http::LINE_STATUS Http::parse_line()//从状态机解析缓冲区中的一行
{
    char cur='';
    bool flag;
    cur=m_readbuffer.read_only(&flag);//flag为true时说明读取成功
    while(flag)
    {
        if(cur=='\r')
        {
            cur=m_readbuffer.read_only(&flag);

            if(flag==false)
            {
                break;
            }

            if(cur=='\n')
            {
                size_t idx1=m_readbuffer.get_read_only_idx()-1;
                size_t idx2=m_readbuffer.get_read_only_idx()-2;
                m_readbuffer.set_char(idx1,'\0');
                m_readbuffer.set_char(idx1,'\0');
                return LINE_OK;
            }
            else
            {
                return LINE_BAD;
            }
        }
        else if(cur=='\n')//出现过LINE_OPEN后再进行parse_line()、或者语法错误,可能出现这种情况
        {
            bool temp;
            size_t idx=m_readbuffer.get_read_only_idx()-2;
            char prechar=m_readbuffer.get_char(idx,&temp);
            if(temp==true && prechar=='\r')
            {
                size_t idx1=m_readbuffer.get_read_only_idx()-1;
                size_t idx2=m_readbuffer.get_read_only_idx()-2;
                m_readbuffer.set_char(idx1,'\0');
                m_readbuffer.set_char(idx1,'\0');
                return LINE_OK;
            }
            else
                return LINE_BAD;
        }

        cur=m_readbuffer.read_only(&flag);
    }

    return LINE_OPEN；
}

Http::HTTP_CODE Http::process_read()
{
    LINE_STATUS line_status = LINE_OK;
    HTTP_CODE ret = NO_REQUEST;

    while(1)
    {
        line_status=parse_line();
        if(line_status==LINE_OPEN)
            return NO_REQUEST;
        if(line_status==LINE_BAD)
            return BAD_REQUEST;

        string text=m_readbuffer.retriveOneLine();

    }

}

bool Http::read()//将数据从内核读缓冲区读取到用户的读缓冲区,返回false说明对方关闭连接或读取出错
{
    return m_readbuffer.readFD(m_socket);
}
