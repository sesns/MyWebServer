#include "Buffer.h"
#include <vector>
#include <sys/uio.h>
#include<errno.h>
#include <assert.h>
#include <string>
#include <string.h>
#include "Log.h"
int extern errno;
using namespace std;

void Buffer::retrieve(size_t len)//调整缓冲区
{
    if(len<readableBytes())//读完后还有数据
        readeridx+=len;
    else//数据全部读完，将指针搬迁到初始位置
    {
        readeridx=kCheapPrepend;
        writeidx=kCheapPrepend;
        read_only_idx=kCheapPrepend;
    }
}

string Buffer::retrieveAsString(size_t len)//从缓冲区读取长为len的数据
{
    assert(len<=readableBytes());

    string res(getReadBegin(),len);

    //调整缓冲区
    retrieve(len);

    return res;
}

string Buffer::retriveOneLine()//从缓冲区读取一行
{
    size_t idx=readeridx;
    while(idx< writeidx && m_buffer[idx]!='\r')
    {
        idx++;
    }

    size_t len=idx-readeridx;
    if(m_buffer[idx]=='\r' && idx<writeidx-1 && m_buffer[idx+1]=='\n')
    {
        string res=retrieveAsString(len);
        retrieveAsString(2);
        return res;
    }

    return retrieveAsString(len);
}
void Buffer::make_space(size_t len)
{
    if(writableBytes()>=len)//已经有了足够的空间
        return;

    if((writableBytes()+prepenableBytes()-kCheapPrepend)<len)//剩余可用空间小于len
    {
        m_buffer.resize(writeidx+len);
    }
    else//剩余可用空间足够，进行数据的搬迁
    {
        size_t dif=readeridx-kCheapPrepend;
        std::copy(getReadBegin(),getWriteBegin(),getKprependBegin());
        readeridx-=dif;
        writeidx-=dif;
        read_only_idx-=dif;
    }


}
void Buffer::append(const char* data,size_t len)//将数据加到m_buffer里面
{
    make_space(len);//使得有足够的空间容纳数据
    std::copy(data,data+len,getWriteBegin());//复制数据
    writeidx+=len;//更新writeidx
}

bool Buffer::readFD(int sockfd)
{
    char external_buffer[65536];//用于暂存超出应用层缓冲区大小的数据
    struct iovec vec[2];
    size_t writable_bytes;
    int iovcnt;
    while(true)//非阻塞+ET
    {
        writable_bytes=writableBytes();

        vec[0].iov_base=getBegin()+writeidx;
        vec[0].iov_len=writable_bytes;

        vec[1].iov_base=external_buffer;
        vec[1].iov_len=sizeof(external_buffer);

        //如果此时应用层缓冲区已经很大了（大于128k,一开始才1k），就不往stackbuffer写入数据了
        iovcnt=writable_bytes>=sizeof(external_buffer)?1:2;

        int bytes_recv=readv(sockfd,vec,iovcnt);

        if(bytes_recv<0)
        {
            if(errno==EAGAIN || errno==EWOULDBLOCK)//内核读缓冲区数据已经读取完毕
                break;
            Log::getInstance()->write_log(ERRO,"in Buffer::readFD,fd is:%d,%s",sockfd,strerror(errno));
            return false;//读取遇到其他错误
        }
        else if(bytes_recv==0)//对方关闭连接
        {
            //Log::getInstance()->write_log(INFO,"in Buffer::readFD,client close connection");
            return false;
        }

        if(bytes_recv<=writable_bytes)//第一块缓冲区足以容纳数据
            writeidx+=bytes_recv;
        else//第一块缓冲区不足以容纳数据
        {
            size_t len=bytes_recv-writable_bytes;
            writeidx = m_buffer.size();
            append(external_buffer, len);//将external_buffer的数据加到m_buffer里面
        }

    }

    return true;
}

//将数据从用户写缓冲区、文件映射地址 写到内核写缓冲区中，返回-1关闭连接，返回0数据因内核缓冲区满没有写完，返回1数据全部发送完毕
//ET模式
int Buffer::writeFD(int sockfd)
{
        //cout<<m_iov_cnt<<"\n";
        //cout<<*(char*)(m_iov[0].iov_base)<<"\n";
        //cout<<m_iov[0].iov_len<<"\n";
        //cout<<*(char*)(m_iov[1].iov_base)<<"\n";
        //cout<<m_iov[1].iov_len<<"\n";

    int ret;
    while(true)
    {
        ret=writev(sockfd,m_iov,m_iov_cnt);

        if(ret>0)
            m_bytes_have_send+=ret;
        else if(ret<0)
        {
            if(errno==EAGAIN || errno==EWOULDBLOCK )
                return 0;
            //printf("%s\n", strerror(errno));
            Log::getInstance()->write_log(ERRO,"in Buffer::writeFD,fd is:%d,%s",sockfd,strerror(errno));
            return -1;
        }

        if(m_bytes_have_send<m_bytes_to_send)//数据没发完
        {
            if(m_bytes_have_send<len1)//第一块的数据没有发完
            {
                m_iov[0].iov_base=start1+m_bytes_have_send;
                m_iov[0].iov_len=len1-m_bytes_have_send;
            }
            else//第一块的数据已经发完
            {
                m_iov[0].iov_len=0;
                m_iov[1].iov_base=start2+m_bytes_have_send-len1;
                m_iov[1].iov_len=m_bytes_to_send-m_bytes_have_send;
            }
        }
        else//所有数据已发送完
        {
            //retrieve(m_bytes_have_send);
            retrieve(len1);
            return 1;
        }
    }
}
