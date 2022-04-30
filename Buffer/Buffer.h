#ifndef BUFFER_H_INCLUDED
#define BUFFER_H_INCLUDED
#include <vector>
#include <assert.h>
#include<string>
class Buffer
{
private:
    std::vector<char> m_buffer;
    size_t readeridx;//读指针的位置
    size_t writeidx;//写指针的位置
    size_t read_only_idx;//只用于读，不会更改读指针
private:
    char* getBegin() const
    {
        return &(*m_buffer.begin());
    }
    char* getWriteBegin() const
    {
        return &(*(m_buffer.begin()+writeidx));
    }

    char* getReadBegin() const
    {
        return &(*(m_buffer.begin()+readeridx));
    }

    char* getKprependBegin() const
    {
        return &(*(m_buffer.begin()+kCheapPrepend));
    }

    void retrieve(size_t len);
    void make_space(size_t len);//使得有足够的空间容纳数据
public:
    static const size_t kCheapPrepend = 8;
    static const size_t kInitialSize = 1024;

    Buffer(size_t initialSize=kInitialSize)
    :m_buffer(kCheapPrepend+initialSize),
    readeridx(kCheapPrepend),
    writeidx(kCheapPrepend),
    read_only_idx(kCheapPrepend)
    {
        assert(prepenableBytes==8);
        assert(readableBytes==0);
        assert(writableBytes==initialSize);
    }

    void init()
    {
        m_buffer(kCheapPrepend+initialSize);
        readeridx(kCheapPrepend);
        writeidx(kCheapPrepend);
        read_only_idx(kCheapPrepend);
    }

    bool readFD(int sockfd);//将内核读缓冲区的数据读到应用层读缓冲区中，返回false表示读取错误出错或者对方关闭连接
    int writeFD(int sockfd,struct iovec* iov,int iovcnt);
    //将数据从用户写缓冲区、文件映射地址 写到内核写缓冲区中，返回-1关闭连接，返回0数据因内核缓冲区满没有写完，返回1数据全部发送完毕

    string retrieveAsString(size_t len);//从缓冲区读取长为len的数据

    string retriveOneLine();//从缓冲区读取一行

    void append(char* data,size_t len);//将数据加到m_buffer里面

    char read_only(bool* status)//只用于读，不会更改readeridx指针,若缓冲区以空则status=false
    {
        char res=' ';
        if(read_only_idx>=writeidx)//缓冲区以空
        {
            (*status)=false;
            return res;
        }

        res=m_buffer[read_only_idx];
        read_only_idx+=1;
        (*status)=true;
        return res;
    }

    char get_char(size_t idx,bool* status)//获取idx位置的字符
    {

        if(idx<readeridx || idx>=writeidx)//位置出错
        {
            char res='';
            (*status)=false;
            return res;
        }

        (*status)=true;
        return m_buffer[idx];
    }

    void set_char(size_t idx,char c)//修改idx位置的字符为c
    {
        m_buffer[idx]=c;
    }

    size_t readableBytes() const
    {
        return writeidx-readeridx;
    }

    size_t writableBytes() const
    {
        return m_buffer.size()-writeidx;
    }

    size_t prepenableBytes() const
    {
        return readeridx;
    }

    size_t get_read_only_idx() const
    {
        return read_only_idx;
    }
};

#endif // BUFFER_H_INCLUDED
