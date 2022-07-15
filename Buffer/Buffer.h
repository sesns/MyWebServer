#ifndef BUFFER_H_INCLUDED
#define BUFFER_H_INCLUDED
#include <vector>
#include <assert.h>
#include<string>
#include <sys/uio.h>
using namespace std;
class Buffer
{
private:
    std::vector<char> m_buffer;
    size_t readeridx;//读指针的位置
    size_t writeidx;//写指针的位置
    size_t read_only_idx;//只用于读，不会更改读指针

    size_t m_bytes_have_send;//已经向socket发送的数据
    size_t m_bytes_to_send;//要向socket发送的数据
    struct iovec* m_iov;
    size_t m_iov_cnt;
    int len1;
    int len2;
    char* start1;
    char* start2;


private:
    char* getBegin()
    {
        return &(*m_buffer.begin());
    }
    char* getWriteBegin()
    {
        return &(*(m_buffer.begin()+writeidx));
    }

    char* getReadBegin()
    {
        return &(*(m_buffer.begin()+readeridx));
    }

    char* getKprependBegin()
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
    read_only_idx(kCheapPrepend),
    m_bytes_have_send(0),
    m_bytes_to_send(0)
    {
        len1=0;
        len2=0;
        start1=nullptr;
        start2=nullptr;
        assert(prepenableBytes()==8);
        assert(readableBytes()==0);
        assert(writableBytes()==initialSize);
        m_iov_cnt=1;
        m_iov=nullptr;
        m_iov=(struct iovec*)malloc(m_iov_cnt*sizeof(struct iovec));
    }

    ~Buffer()
    {
        if(m_iov)
            free(m_iov);
    }

    void init()
    {
        readeridx=kCheapPrepend;
        writeidx=kCheapPrepend;
        read_only_idx=kCheapPrepend;
        m_bytes_have_send=0;
        m_bytes_to_send=0;

        len1=0;
        len2=0;
        start1=nullptr;
        start2=nullptr;

        assert(prepenableBytes()==8);
        assert(readableBytes()==0);
    }
    bool readFD(int sockfd);//将内核读缓冲区的数据读到应用层读缓冲区中，返回false表示读取错误出错或者对方关闭连接
    int writeFD(int sockfd);
    //将数据从用户写缓冲区、文件映射地址 写到内核写缓冲区中，返回-1关闭连接，返回0数据因内核缓冲区满没有写完，返回1数据全部发送完毕

    string retrieveAsString(size_t len);//从缓冲区读取长为len的数据

    string retriveOneLine();//从缓冲区读取一行

    void append(const char* data,size_t len);//将数据加到m_buffer里面

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
            char res=' ';
            (*status)=false;
            return res;
        }

        (*status)=true;
        return m_buffer[idx];
    }

    size_t readableBytes()
    {
        return writeidx-readeridx;
    }

    size_t writableBytes()
    {
        return m_buffer.size()-writeidx;
    }

    size_t prepenableBytes()
    {
        return readeridx;
    }

    size_t get_read_only_idx()
    {
        return read_only_idx;
    }

    void set_iov(struct iovec* i,size_t cnt)
    {
        m_bytes_have_send=0;
        m_iov[0].iov_base=getReadBegin();
        m_iov[0].iov_len=readableBytes();

        if(cnt==1)
        {
            m_iov_cnt=1;
            m_bytes_to_send=m_iov[0].iov_len;
            len1=m_iov[0].iov_len;
            len2=0;
            start1=(char*)m_iov[0].iov_base;
            return;
        }
        else if(cnt==2)
        {
            m_iov_cnt=2;
            m_iov[1].iov_base=i[1].iov_base;
            m_iov[1].iov_len=i[1].iov_len;
            m_bytes_to_send=m_iov[0].iov_len+m_iov[1].iov_len;
            len1=m_iov[0].iov_len;
            len2=m_iov[1].iov_len;
            start1=(char*)m_iov[0].iov_base;
            start2=(char*)m_iov[1].iov_base;
            return;
        }
    }
};

#endif // BUFFER_H_INCLUDED
