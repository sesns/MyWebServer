#include<sys/socket.h>
#include "Http.h"
#include "Buffer.h"
#include "MySQL_connection_pool.h"
#include<string>
#include<iostream>
#include<sys/stat.h>
#include<sys/types.h>
#include<fcntl.h>
#include<sys/mman.h>
#include<unistd.h>
using namespace std;

unordered_map<string,string> username_to_password;//web页面用户注册的帐号密码
int Http::m_user_count=0;
int Http::m_epoll_fd=-1;
MySQL_connection_pool* Http::m_conn_pool=NULL;

void Http::mysqlInit_userAndpawd()//将数据库的帐号密码加载到username_to_password
{
    if(!m_conn_pool)
        return;
    MYSQL* conn=NULL;
    MySQLconRAII(&conn,m_conn_pool);

    if(mysql_query(conn,"SELECT username,passwd FROM user"))
    {
        //日志
    }

    MYSQL_RES* res=mysql_store_result(conn);

    if(!res)
    {
        //日志
    }
    else
    {
        MYSQL_ROW sql_row;
        while(sql_row=mysql_fetch_row(res))
        {
            username_to_password[sql_row[0]]=sql_row[1];
        }
    }

    if(res)
        mysql_free_result(res);

}
Http::LINE_STATUS Http::parse_line()//从状态机解析缓冲区中的一行
{
    char cur=' ';
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
                m_readbuffer.set_char(idx2,'\0');
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
                m_readbuffer.set_char(idx2,'\0');
                return LINE_OK;
            }
            else
                return LINE_BAD;
        }

        cur=m_readbuffer.read_only(&flag);
    }

    return LINE_OPEN;
}

Http::HTTP_CODE Http::parse_request_line(const string& text)//解析请求行
{
    //找到第一个空格的位置
    int first_space_pos=text.find(" ");
    if(first_space_pos==-1)
        return BAD_REQUEST;

    //处理请求方式
    string method=text.substr(0,first_space_pos);
    if(method=="GET")
        m_method="GET";
    else if(method=="POST")
        m_method="POST";
    else
        return BAD_REQUEST;

    //如果前两个字段间有多余的空格
    if((first_space_pos+1)>=text.size() || text[first_space_pos+1]==' ')
        return BAD_REQUEST;

    //找到第二个空格的位置
    int second_space_pos=text.find(" ",first_space_pos+1);
    if(second_space_pos==-1)
        return BAD_REQUEST;

    //处理请求url和版本
    string url=text.substr(first_space_pos+1,second_space_pos-first_space_pos-1);
    string version=text.substr(second_space_pos+1,8);

    if(version=="HTTP/1.1")//仅支持HTTP1.1
        m_version=version;
    else
        return BAD_REQUEST;

    if(url=="" || url[0]!='/')
        return BAD_REQUEST;
    else
        m_url=url;


    m_check_status=CHECK_HEADER;//状态转移
    return NO_REQUEST;
}

Http::HTTP_CODE Http::parse_header(const string& text)//解析请求首部
{
    //判断是空行还是请求首部
    //如果text=='\r\n'并且报文为GET方法，直接返回GET_REQUEST
    //如果text=='\r\n'并且报文为POST方法,状态转移到CHECK_CONTENT
    if(text=="\0")
    {
        if(m_method=="GET")
            return GET_REQUEST;
        else if(m_method=="POST")
        {
            m_check_status=CHECK_CONTENT;
            return NO_REQUEST;
        }
        else
            return BAD_REQUEST;
    }
    else if(text.find("Connection:")!=-1)//Connection字段
    {
        if(text.find("keep-alive")!=-1)
            m_linger=true;
        else if(text.find("close")!=-1)
            m_linger=false;
        else
            return BAD_REQUEST;
    }
    else if(text.find("Content-length:")!=-1)//Content-length字段
    {
        string len=text.substr(15,text.size()-15);
        for(int i=0;i<len.size();i++)
            if(!isdigit(len[i]))
                return BAD_REQUEST;
        m_content_length=stoi(len);
    }
    else if(text.find("Host:")!=-1)//Host字段
    {
        m_host=text.substr(5,text.size()-5);
    }
    else
    {
        std::cout<<"unknown header!\n";
    }

    return NO_REQUEST;
}

Http::HTTP_CODE Http::do_request()//报文响应函数
{
    m_real_file=m_doc_root;

    //进行登陆校验和注册校验
    if(m_url.size()>1 && m_method=="POST" && (m_url[1]=='2' || m_url[1]=='3'))
    {
        //从请求报文的报文体中将帐号密码提取出来,帐号密码格式为 user=123&password=123
        int pos=m_string.find("&");
        string username=m_string.substr(5,pos-5);
        string password=m_string.substr(pos+10);

        // 2为登陆校验，3为注册校验
        if(m_url[1]=='3')//注册校验
        {
            if(username_to_password.find(username)==username_to_password.end())//找不到同名的帐号
            {
                string sql_insert("INSERT INTO user(username, passwd) VALUES(");
                sql_insert+="'";
                sql_insert+=username;
                sql_insert+="'";
                sql_insert+=", '";
                sql_insert+=password;
                sql_insert+="'";
                sql_insert+=")";

                MYSQL* conn=NULL;
                MySQLconRAII(&conn,m_conn_pool);

                m_loc.lock();
                int res=mysql_query(conn,sql_insert.c_str());//向数据库中插入帐号密码
                m_loc.unlock();

                if(res==0)//插入成功,跳转到登陆页面
                {
                    m_loc.lock();
                    username_to_password[username]=password;
                    m_loc.unlock();

                    m_real_file+="/log.html";
                    m_file_type="text/html";
                }
                else//插入失败,跳转到注册失败的页面
                {
                    m_real_file+="/registerError.html";
                    m_file_type="text/html";
                }

            }
            else//注册校验失败，跳转到注册失败的页面
            {
                m_real_file+="/registerError.html";
                m_file_type="text/html";
            }
        }
        else if(m_url[1]=='2')//2,登陆校验
        {
            if(username_to_password.find(username)!=username_to_password.end() && username_to_password[username]==password)
            {
                m_real_file+="/welcome.html";
                m_file_type="text/html";
            }
            else
            {
                m_real_file+="/logError.html";
                m_file_type="text/html";
            }
        }
    }


    //主页面
    else if(m_url.size()==1 && m_url[0]=='/')
    {
        m_real_file+="/judge.html";
        m_file_type="text/html";
    }

    //表示请求注册页面
    else if(m_url[1]=='0')
    {
        m_real_file+="/register.html";
        m_file_type="text/html";
    }

    //表示请求登陆页面
    else if(m_url[1]=='1')
    {
        m_real_file+="/log.html";
        m_file_type="text/html";
    }

    //图片页面
    else if(m_url[1]=='5')
    {
        m_real_file+="/picture.html";
        m_file_type="text/html";
    }

    //视频页面
    else if(m_url[1]=='6')
    {
        m_real_file+="/video.html";
        m_file_type="text/html";
    }
    //否则就发送url实际请求的文件
    else
    {
        m_real_file+=m_url;
        m_file_type="text/html";
    }

    //检查是否存在这样的文件
    if(stat(m_real_file.c_str(),&m_file_stat)<0)
        return NO_RESOURCE;

    //检查是否有权限请求该文件
    if(!(m_file_stat.st_mode & S_IROTH))
        return FORBIDDEN_REQUEST;

    //检查文件类型，如果为目录则返回语法错误
    if(S_ISDIR(m_file_stat.st_mode))
        return BAD_REQUEST;

    //以只读的方式打开文件
    int fd=open(m_real_file.c_str(),O_RDONLY);

    //将文件映射到虚拟内存中
    m_file_addres=(char*)mmap(0,m_file_stat.st_size,PROT_READ,MAP_PRIVATE,fd,0);

    close(fd);

    return FILE_REQUEST;
}

Http::HTTP_CODE Http::process_read()
{
    LINE_STATUS line_status = LINE_OK;
    HTTP_CODE ret = NO_REQUEST;
    string text;

    while(m_check_status!=CHECK_CONTENT)
    {
        line_status=parse_line();
        if(line_status==LINE_OPEN)
            return NO_REQUEST;
        if(line_status==LINE_BAD)
            return BAD_REQUEST;

        text=m_readbuffer.retriveOneLine();

        switch(m_check_status)
        {
            case CHECK_REQUESTLINE:
                ret=parse_request_line(text);//解析请求行成功则从CHECK_REQUESTLINE转移到CHECK_HEADER，否则返回错误类型
                if(ret==BAD_REQUEST)
                    return BAD_REQUEST;
                break;
            case CHECK_HEADER:
                //如果text=='\r\n'并且报文为GET方法，直接返回GET_REQUEST
                //如果text=='\r\n'并且报文为POST方法,状态转移到CHECK_CONTENT
                ret==parse_header(text);
                if(ret==BAD_REQUEST)
                    return BAD_REQUEST;
                else if(ret==GET_REQUEST)
                    return do_request();//报文响应函数
                break;
            default:
                return INTERNAL_ERROR;
        }

    }

    //m_check_status==CHECK_CONTENT
    if(m_readbuffer.readableBytes()>=m_content_length)//说明报文体已完整地在缓冲区内
    {
        m_string=m_readbuffer.retrieveAsString(m_content_length);
        return do_request();//报文响应函数
    }

    return NO_REQUEST;
}

bool Http::Read()//将数据从内核读缓冲区读取到用户的读缓冲区,返回false说明对方关闭连接或读取出错
{
    return m_readbuffer.readFD(m_socket);
}

void Http::add_response(string text)//将text写入到用户写缓冲区中
{
    m_writebuffer.append(text.c_str(),text.size());
}

void Http::add_status_line(string status_code,string reason)//生成状态行，将其写入用户写缓冲区中
{
    add_response("HTTP/1.1 "+status_code+" "+reason+"\r\n");
}

void Http::add_content_length(size_t len)//添加内容长度
{
    add_response("Content-Length:"+to_string(len)+"\r\n");
}

void Http::add_content_type()//添加内容类型
{
    add_response("Content-Type:"+m_file_type+"\r\n");
}

void Http::add_connection()//添加连接状态
{
    string s1=(m_linger==true?"keep-alive":"close");
    add_response("Connection:"+s1+"\r\n");
}

void Http::add_black_line()//添加\r\n
{
    add_response("\r\n");
}

void Http::add_headers(size_t len)//生成响应首部，将其写入用户写缓冲区中
{
    add_connection();
    add_content_length(len);
    add_content_type();
}
void Http::add_content(string text)//添加内容
{
    add_response(text);
}

bool Http::process_write(HTTP_CODE ret)//生成响应报文，将其写入用户写缓冲区中
{
    switch(ret)
    {
    case BAD_REQUEST:
        m_linger=false;
        add_status_line("400",error_400_title);
        add_headers(error_400_form.size());
        add_black_line();
        add_content(error_400_form);
        break;

    case FORBIDDEN_REQUEST:
        add_status_line("403",error_403_title);
        add_headers(error_403_form.size());
        add_black_line();
        add_content(error_403_form);
        break;

    case NO_RESOURCE:
        add_status_line("404",error_404_title);
        add_headers(error_404_form.size());
        add_black_line();
        add_content(error_404_form);
        break;

    case INTERNAL_ERROR:
        add_status_line("500",error_500_title);
        add_headers(error_500_form.size());
        add_black_line();
        add_content(error_500_form);
        break;

    case FILE_REQUEST:
        add_status_line("200",ok_200_title);
        add_headers(m_file_stat.st_size);
        add_black_line();
        m_iov[1].iov_base=m_file_addres;
        m_iov[1].iov_len=m_file_stat.st_size;
        m_iov_cnt=2;
        return true;
        break;

    default:
        return false;

    }

    m_iov_cnt=1;
    return true;
}

bool Http::Write()//将数据从用户写缓冲区、文件映射地址 写到内核写缓冲区中，返回false说明要关闭连接
{
    int ret=m_writebuffer.writeFD(m_socket,m_iov,m_iov_cnt);
    if(ret==-1)//出错，应关闭连接
    {
        unmap();
        return false;
    }
    else if(ret==0)//数据因内核缓冲区满而没有完整写完
    {
        mod_fd_in_epoll(m_socket,EPOLLOUT);//重置EPOLLONESHOT可写事件
        return true;
    }
    else if(ret==1)//数据完整写到内核缓冲区中
    {
        unmap();
        mod_fd_in_epoll(m_socket,EPOLLIN);//重置EPOLLONESHOT可读事件

        if(m_linger)
        {
            init();
            return true;
        }
        else
            return false;
    }

    return false;
}
