#include "Buffer.h"
#include <vector>
#include <sys/uio.h>
#include<errno.h>
#include <assert.h>
#include <string>
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
    retrieve(size_t len);

    return res;
}

string Buffer::retriveOneLine()//从缓冲区读取一行
{
    size_t idx=readeridx;
    while(idx< writeidx && m_buffer[idx]!='\0')
    {
        idx++;
    }
    size_t len=idx-readeridx;
    if(m_buffer[idx]=='\0')
        len+=2;
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
void Buffer::append(char* data,size_t len)//将数据加到m_buffer里面
{
    make_space(len);//使得有足够的空间容纳数据
    std::copy(data,data+len,getWriteBegin());//复制数据
    writeidx+=len;//更新writeidx
}

bool Buffer::readFD(int sockfd)
{
    char[65536] external_buffer;//用于暂存超出应用层缓冲区大小的数据
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
        iovcnt=writable_bytes>=sizeof(external_buffer)?1:2

        int bytes_recv=readv(sockfd,vec,iovcnt);

        if(bytes_recv<0)
        {
            if(errno==EAGAIN || errno==EWOULDBLOCK)//内核读缓冲区数据已经读取完毕
                break;
            return false;//读取遇到其他错误
        }
        else if(bytes_recv==0)//对方关闭连接
            return false;

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
