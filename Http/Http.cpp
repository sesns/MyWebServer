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
#include "Log.h"
#include <string.h>
#include<sstream>
using namespace std;

locker m_loc;//保护数据库插入、保护username_to_password
locker m_loc2;//保护user_count;
unordered_map<string,string> username_to_password;//web页面用户注册的帐号密码
atomic<int> Http::m_user_count(0);
int Http::m_epoll_fd=-1;
MySQL_connection_pool* Http::m_conn_pool=NULL;

void Http::init()//维持同一个连接下的初始化
{
        m_check_status=CHECK_REQUESTLINE;
        m_content_length=0;
        m_string="";
        m_method="GET";
        m_url="";
        m_version="HTTP/1.1";
        m_linger=false;
        m_host="";
        m_file_type="text/html";
        m_real_file="";
        m_file_addres=0;

        //范围请求
        if_range=false;
        range_left=-1;
        range_right=-1;

        //http缓存控制
        if_request_has_Last_Modified=false;
        if_request_has_If_None_Match=false;

        //token
        if_request_has_token=false;
        log_succ=false;

        m_boundary="";

}
void Http::init(int sockfd, const sockaddr_in &addr,Timer* t,TimerManager* timerheap)
{
    //m_loc2.lock();
        m_user_count++;
    //m_loc2.unlock();
        m_timer=t;
        m_timerheap=timerheap;

        m_socket=sockfd;
        m_client_address=addr;
        cgi_succ=false;
        add_fd_to_epoll(m_socket);
        m_readbuffer.init();
        m_writebuffer.init();
        init();
}

void Http::close_conn()//关闭连接
{
    //m_loc2.lock();
        m_user_count--;
    //m_loc2.unlock();
        remove_fd_from_epoll(m_socket);//从epoll空间删除fd
        close(m_socket);//关闭连接

        //Log::getInstance()->write_log(INFO,"server close connection");
}

void Http::mysqlInit_userAndpawd()//将数据库的帐号密码加载到username_to_password
{
    //Log::getInstance()->write_log(DEBUG,"in Http::mysqlInit_userAndpawd");
    if(!m_conn_pool)
        return;
    MYSQL* conn=NULL;
    MySQLconRAII(&conn,m_conn_pool);

    if(mysql_query(conn,"SELECT username,passwd FROM user"))
    {
        Log::getInstance()->write_log(ERRO,"in Http::mysqlInit_userAndpawd,mysql_query failed");
        return;
    }

    MYSQL_RES* res=mysql_store_result(conn);

    if(!res)
    {
        Log::getInstance()->write_log(ERRO,"in Http::mysqlInit_userAndpawd,mysql_store_result failed");
        return;
    }
    else
    {
        MYSQL_ROW sql_row;
        while(sql_row=mysql_fetch_row(res))
        {
            m_loc.lock();
            username_to_password[sql_row[0]]=sql_row[1];
            m_loc.unlock();
        }
    }

    if(res)
        mysql_free_result(res);

}
Http::LINE_STATUS Http::parse_line()//从状态机解析缓冲区中的一行
{
    //Log::getInstance()->write_log(DEBUG,"in Http::parse_line");
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
                return LINE_OK;
            }
            else
            {
                Log::getInstance()->write_log(WARN,"in  Http::parse_line,the message has sytax error");
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
                return LINE_OK;
            }
            else
            {
                Log::getInstance()->write_log(WARN,"in  Http::parse_line,the message has sytax error");
                return LINE_BAD;
            }
        }

        cur=m_readbuffer.read_only(&flag);
    }

    return LINE_OPEN;
}

Http::HTTP_CODE Http::parse_request_line(const string& text)//解析请求行
{
    //Log::getInstance()->write_log(DEBUG,"in Http::parse_request_line");
    //找到第一个空格的位置
    int first_space_pos=text.find(" ");
    if(first_space_pos==-1)
    {
        Log::getInstance()->write_log(WARN,"in Http::parse_request_line,the request line has sytax error");
        return BAD_REQUEST;
    }

    //处理请求方式
    string method=text.substr(0,first_space_pos);
    if(method=="GET")
        m_method="GET";
    else if(method=="POST")
        m_method="POST";
    else
    {
        Log::getInstance()->write_log(WARN,"in Http::parse_request_line,the request line has sytax error");
        return BAD_REQUEST;
    }

    //如果前两个字段间有多余的空格
    if((first_space_pos+1)>=text.size() || text[first_space_pos+1]==' ')
    {
        Log::getInstance()->write_log(WARN,"in Http::parse_request_line,the request line has sytax error");
        return BAD_REQUEST;
    }

    //找到第二个空格的位置
    int second_space_pos=text.find(" ",first_space_pos+1);
    if(second_space_pos==-1)
    {
        Log::getInstance()->write_log(WARN,"in Http::parse_request_line,the request line has sytax error");
        return BAD_REQUEST;
    }

    //处理请求url和版本
    string url=text.substr(first_space_pos+1,second_space_pos-first_space_pos-1);
    string version=text.substr(second_space_pos+1,8);
    if(version=="HTTP/1.1" || version=="HTTP/1.0")//仅支持HTTP1.1和1.0
        m_version=version;
    else
    {
        Log::getInstance()->write_log(WARN,"in Http::parse_request_line,don't support ohter http version except http1.1 ");
        return BAD_REQUEST;
    }

    if(url=="" || url[0]!='/')
    {
        Log::getInstance()->write_log(WARN,"in Http::parse_request_line,the request line has sytax error");
        return BAD_REQUEST;
    }
    else
        m_url=url;


    m_check_status=CHECK_HEADER;//状态转移
    return NO_REQUEST;
}

Http::HTTP_CODE Http::parse_header(const string& text)//解析请求首部
{
    //Log::getInstance()->write_log(DEBUG,"in Http::parse_header");
    //判断是空行还是请求首部
    //如果text=='\r\n'并且报文为GET方法，直接返回GET_REQUEST
    //如果text=='\r\n'并且报文为POST方法,状态转移到CHECK_CONTENT
    if(text=="")
    {
        if(m_method=="GET")
        {
            return GET_REQUEST;
        }
        else if(m_method=="POST")
        {
            m_check_status=CHECK_CONTENT;
            return NO_REQUEST;
        }
        else
        {
            //Log::getInstance()->write_log(WARN,"in Http::parse_header,don't support other method");
            return BAD_REQUEST;
        }
    }
    else if(text.find("Connection:")!=-1)//Connection字段
    {
        if(text.find("keep-alive")!=-1)
            m_linger=true;
        else if(text.find("close")!=-1)
            m_linger=false;
        else
        {
            //Log::getInstance()->write_log(WARN,"in Http::parse_header,the header connection has syntax error");
            return BAD_REQUEST;
        }
    }
    else if(text.find("Content-Length: ")!=-1)//Content-length字段
    {
        string len=text.substr(16,text.size()-16);
        for(int i=0;i<len.size();i++)
            if(!isdigit(len[i]))
            {
                //Log::getInstance()->write_log(WARN,"in Http::parse_header,the header contentlength has syntax error");
                return BAD_REQUEST;
            }
        m_content_length=stoi(len);
    }
    else if(text.find("Host:")!=-1)//Host字段
    {
        m_host=text.substr(5,text.size()-5);
    }
    else if(text.find("Content-Type:")!=-1)
    {
        int boundary_pos=text.find("boundary=");
        if(boundary_pos!=-1)
        {
            m_boundary=text.substr(boundary_pos+9,text.size()-boundary_pos-9);
        }
        else
            m_boundary="";
    }
    else if(text.find("If-None-Match: ")!=-1)
    {
        if_request_has_If_None_Match=true;
        int temp_pos=15;
        If_None_Match_Etag=text.substr(temp_pos);
    }
    else if(text.find("If-Modified-Since:")!=-1)
    {
        if_request_has_Last_Modified=true;
        int temp_pos=19;
        Modified_Since=text.substr(temp_pos);
    }
    else if(text.find("Range: bytes=")!=-1)
    {
        if_range=true;
        int temp_pos=text.find("Range: bytes=");
        size_t start_pos=temp_pos+13;
        size_t xiahua_pos=text.find("-",start_pos);

        range_str=text.substr(start_pos);

        if(start_pos==xiahua_pos)//说明没有指定起始位置
        {
            range_left=-1;
            range_right=stoll(text.substr(xiahua_pos+1));
        }
        else//说明指定起始位置
        {
            if(xiahua_pos==(text.size()-1))//没有指定终止位置
            {
                   range_left=stoll(text.substr(start_pos,xiahua_pos-start_pos));
                   range_right=-1;
            }
            else//有指定终止位置
            {
                    range_left=stoll(text.substr(start_pos,xiahua_pos-start_pos));
                    range_right=stoll(text.substr(xiahua_pos+1));
            }


        }

    }
    /*
    else if(text.find("Cookie: ")!=-1)//token
    {
        if_request_has_token=true;
        size_t token_pos=text.find("=");
        string token=text.substr(token_pos+1);
        m_token.set_recv_token(token);
    }*/
    else
    {
        //Log::getInstance()->write_log(WARN,"in Http::parse_header,unknown header");
    }

    return NO_REQUEST;
}

Http::HTTP_CODE Http::do_request()//报文响应函数
{
    //Log::getInstance()->write_log(DEBUG,"in Http::do_request");
    m_real_file=m_doc_root;

    //token校验
    bool token_check_succ=false;
    string token_uid;
    if(m_string!="" && m_string.find("token")!=-1)
    {
        if_request_has_token=true;
        if(m_boundary!="" && m_string.find(m_boundary)!=-1)
        {
            size_t token_pos=m_string.find("token");
            size_t start_pos=token_pos+10;
            size_t end_pos=m_string.find(m_boundary,start_pos);
            end_pos-=4;
            string token_recv=m_string.substr(start_pos,end_pos-start_pos);
            token_check_succ=m_token.check_token(token_recv,token_uid);
        }
        else
        {
            string token_recv=m_string.substr(6);
            token_check_succ=m_token.check_token(token_recv,token_uid);
        }


    }

    //进行登陆校验和注册校验
    if(m_url=="/T")//用于测试有mysql场景下的性能
    {
        string query("SELECT * FROM user WHERE username = ‘zhanghao’");

                MYSQL* conn=NULL;
                MySQLconRAII(&conn,m_conn_pool);

                m_loc.lock();
                int res=mysql_query(conn,query.c_str());//查询数据库
                m_loc.unlock();
            m_real_file+="/judge.html";
        m_file_type="text/html";

    }

    else if(m_method=="POST" && (m_url=="/2" || m_url=="/3"))
    {
        //从请求报文的报文体中将帐号密码提取出来
        //帐号密码格式为 user=123&password=123
        //帐号密码格式为 multipart格式

        string username;
        string password;
        if(m_string.find("&")!=-1)
        {
            int pos=m_string.find("&");
            username=m_string.substr(5,pos-5);
            password=m_string.substr(pos+10);
        }
        else
        {
            int rn1=m_string.find("\r\n");
            int rn2=m_string.find("\r\n",rn1+2);
            int rn3=m_string.find("\r\n",rn2+2);
            int rn4=m_string.find("\r\n",rn3+2);

            int rn5=m_string.find("\r\n",rn4+2);
            int rn6=m_string.find("\r\n",rn5+2);
            int rn7=m_string.find("\r\n",rn6+2);
            int rn8=m_string.find("\r\n",rn7+2);

            username=m_string.substr(rn3+2,rn4-rn3-2);
            password=m_string.substr(rn7+2,rn8-rn7-2);
        }


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

                    //Log::getInstance()->write_log(INFO,"in Http::do_request,register success");
                }
                else//插入失败,跳转到注册失败的页面
                {
                    m_real_file+="/registerError.html";
                    m_file_type="text/html";

                    //Log::getInstance()->write_log(INFO,"in Http::do_request,register error");
                }

            }
            else//注册校验失败，跳转到注册失败的页面
            {
                m_real_file+="/registerError.html";
                m_file_type="text/html";

                //Log::getInstance()->write_log(INFO,"in Http::do_request,register error");
            }
        }
        else if(m_url[1]=='2')//2,登陆校验
        {
            if(username_to_password.find(username)!=username_to_password.end() && username_to_password[username]==password)
            {
                //cgi_succ=true;
                m_real_file+="/welcome.html";
                m_file_type="text/html";
                log_succ=true;
                m_user_id=username;
                //Log::getInstance()->write_log(INFO,"in Http::do_request,log success");
            }
            else
            {
                m_real_file+="/logError.html";
                m_file_type="text/html";

                //Log::getInstance()->write_log(INFO,"in Http::do_request,log error");
            }
        }
    }


    //主页面
    else if(m_url=="/")
    {

            m_real_file+="/judge.html";
            m_file_type="text/html";
        //Log::getInstance()->write_log(DEBUG,"in Http::do_request,request file is /judge.html");
    }

    else if(m_url=="/autolog")//自动登陆
    {
        if(if_request_has_token && token_check_succ)
        {
            m_real_file+="/welcome.html";
            m_file_type="text/html";
        }
        else
        {
            m_real_file+="/log.html";
            m_file_type="text/html";
        }
    }

    //表示请求注册页面
    else if(m_url=="/0")
    {
        m_real_file+="/register.html";
        m_file_type="text/html";
        //Log::getInstance()->write_log(DEBUG,"in Http::do_request,request file is /register.html");
    }

    //表示请求登陆页面
    else if(m_url=="/1")
    {
        m_real_file+="/log.html";
        m_file_type="text/html";
        //Log::getInstance()->write_log(DEBUG,"in Http::do_request,request file is /log.html");
    }

    //图片页面
    else if(m_url=="/5" && if_request_has_token && token_check_succ)
    {
        m_real_file+="/picture.html";
        m_file_type="text/html";
       // Log::getInstance()->write_log(DEBUG,"in Http::do_request,request file is /picture.html");
    }

    //视频页面
    else if(m_url=="/6" && if_request_has_token && token_check_succ)
    {
        m_real_file+="/video.html";
        m_file_type="text/html";
        //Log::getInstance()->write_log(DEBUG,"in Http::do_request,request file is /video.html");
    }

    else if(m_url=="/UploadFile" && if_request_has_token && token_check_succ)//上传文件
    {
        upload_status=upload_manager_.UploadFile(m_string,m_boundary);
        return FILE_UPLOAD;
    }
    else if(if_request_has_token && token_check_succ)
    {
        if(m_url.size()>=3)
        {
            m_real_file+=m_url;
            m_file_type="text/html";
            string file_type=m_url.substr(m_url.size()-3,3);
            if(file_type=="jpg")
                m_file_type="image/jpeg";
            else if(file_type=="mp4")
                m_file_type="video/mpeg4";
        }
        else
        {
            m_real_file+="/judge.html";
            m_file_type="text/html";
        }
    }
    else
        return FORBIDDEN_REQUEST;

    //检查是否存在这样的文件
    if(stat(m_real_file.c_str(),&m_file_stat)<0)
    {
        //Log::getInstance()->write_log(INFO,"in Http::do_request,no_resource");
        return NO_RESOURCE;
    }

    //检查是否有权限请求该文件
    if(!(m_file_stat.st_mode & S_IROTH))
    {
        //Log::getInstance()->write_log(INFO,"in Http::do_request,forbidden_request");
        return FORBIDDEN_REQUEST;
    }
    //检查文件类型，如果为目录则返回语法错误
    if(S_ISDIR(m_file_stat.st_mode))
    {
        //Log::getInstance()->write_log(INFO,"in Http::do_request,can't request dir");
        return BAD_REQUEST;
    }
    //以只读的方式打开文件
    int fd=open(m_real_file.c_str(),O_RDONLY);

    if(fd<0)
        Log::getInstance()->write_log(ERRO,"in Http::do_request,open(),%s",strerror(errno));
    //将文件映射到虚拟内存中,对此区域作的任何修改都不会写回原来的文件内容
    m_file_addres=(char*)mmap(0,m_file_stat.st_size,PROT_READ,MAP_PRIVATE,fd,0);
    if(m_file_addres==((void*)-1))
    {
        Log::getInstance()->write_log(ERRO,"in Http::do_request,mmap(),%s",strerror(errno));
    }
    close(fd);

    //范围请求
    if(if_range)
    {
        if_range=false;


        if(range_right!=-1 && range_right>=m_file_stat.st_size)
        {
            return RANGE_NO_SATISFIABLE;
        }

        return FILE_PARTIAL_REQUEST;
    }

    //http缓存控制
    if(if_request_has_If_None_Match)//存在If_None_Match字段
    {
        if(If_None_Match_Etag==get_etag())
            return FILE_NO_MODIFIED;
    }
    else//不存在If_None_Match字段
    {
        if(if_request_has_Last_Modified)
        {
            if(Modified_Since==get_timestamp(m_file_stat.st_mtime))
                return FILE_NO_MODIFIED;
        }
    }





    return FILE_REQUEST;
}

Http::HTTP_CODE Http::process_read()
{
    //Log::getInstance()->write_log(DEBUG,"in Http::process_read");
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
        //cout<<text<<"\n";
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
                ret=parse_header(text);
                if(ret==BAD_REQUEST)
                    return BAD_REQUEST;
                else if(ret==GET_REQUEST)
                {
                    return do_request();
                }//报文响应函数
                break;
            default:
                return INTERNAL_ERROR;
        }

    }

    //m_check_status==CHECK_CONTENT
    if(m_readbuffer.readableBytes()>=m_content_length)//说明报文体已完整地在缓冲区内
    {
        m_string=m_readbuffer.retrieveAsString(m_content_length);
        //cout<<m_string;
        return do_request();//报文响应函数
    }

    return NO_REQUEST;
}

bool Http::Read()//将数据从内核读缓冲区读取到用户的读缓冲区,返回false说明对方关闭连接或读取出错
{
    //Log::getInstance()->write_log(DEBUG,"in Http::Read");
    return m_readbuffer.readFD(m_socket);
}

void Http::add_response(string text)//将text写入到用户写缓冲区中
{
    //Log::getInstance()->write_log(DEBUG,"in Http::add_response");
    m_writebuffer.append(text.c_str(),text.size());
}

void Http::add_status_line(string status_code,string reason)//生成状态行，将其写入用户写缓冲区中
{
    //Log::getInstance()->write_log(DEBUG,"in Http::add_status_line");
    add_response("HTTP/1.1 "+status_code+" "+reason+"\r\n");
}

void Http::add_content_length(size_t len)//添加内容长度
{
    //Log::getInstance()->write_log(DEBUG,"in Http::add_content_length");
    add_response("Content-Length:"+to_string(len)+"\r\n");
}

void Http::add_content_type()//添加内容类型
{
    //Log::getInstance()->write_log(DEBUG,"in Http::add_content_type");
    add_response("Content-Type:"+m_file_type+"\r\n");
}

void Http::add_connection()//添加连接状态
{
    //Log::getInstance()->write_log(DEBUG,"in Http::add_connection");
    string s1=(m_linger==true?"keep-alive":"close");
    add_response("Connection:"+s1+"\r\n");
}

void Http::add_content_disposition()//添加content_disposition字段用于弹出文件下载对话框
{
    std::size_t temp_pos=m_real_file.find_last_of("/");
    string file_name=m_real_file.substr(temp_pos+1);
    add_response("Content-Disposition: attachment; filename="+file_name+"\r\n");
}

void Http::add_Accept_Ranges()//添加Accept_Ranges字段
{
    add_response("Accept-Ranges: bytes\r\n");
}

void Http::add_Content_Range()//添加Content_Range字段
{
    add_response("Content-Range: bytes "+to_string(range_left)+"-"+to_string(range_right)+"/"+to_string(m_file_stat.st_size)+"\r\n");
}

void Http::add_Last_Modified()//添加Last_Modified字段
{
    string Last_Modified="Last-Modified: ";
    Last_Modified+=get_timestamp(m_file_stat.st_mtime);
    add_response(Last_Modified+"\r\n");
}

void Http::add_Etag()
{
    string Etag="ETag: ";
    Etag+=get_etag();
    add_response(Etag+"\r\n");
}

void Http::add_Date()//添加Date字段
{
    string Date="Date: ";
    time_t raw_time;
    time(&raw_time);
    Date+=get_timestamp(raw_time);

    add_response(Date+"\r\n");
}

void Http::add_Expires()//添加Expires字段
{
    string Expires="Expires: ";
    time_t raw_time;
    time(&raw_time);
    raw_time+=cache_delay_time;
    Expires+=get_timestamp(raw_time);
    add_response(Expires+"\r\n");
}

void Http::add_Cache_Control()//添加cache-control字段
{
    string seconds=to_string(cache_delay_time);
    add_response("Cache-Control: max-age="+seconds+"\r\n");
}

/*
void Http::add_set_cookie()//如果登陆成功则添加Set-Cookie: 字段，该字段携带服务器生成的token
{
    if(log_succ)
    {
        string set_cookie="Set-Cookie: ";
        set_cookie+=m_user_id;
        set_cookie+="=";

        string alg="hmac_sha256";
        set_cookie+=m_token.create_token(alg,m_user_id,900);
        add_response(set_cookie+"\r\n");
    }

}
*/

void Http::add_Token()
{
    if(log_succ)
    {
        string h="Token: ";


        string alg="hmac_sha256";
        h+=m_token.create_token(alg,m_user_id,900);
        add_response(h+"\r\n");

    }

}

void Http::add_Access_Control_Expose_Headers()//允许客户端获取一些原本获取不到的首部
{
    string h="Access-Control-Expose-Headers:";
    h+="Authorization";
    h+=", ";
    h+="Token";

    add_response(h+"\r\n");
}

void Http::add_Access_Control_Allow_Origin()
{
    string h="Access-Control-Allow-Origin:*";;
    add_response(h+"\r\n");
}
void Http::add_black_line()//添加\r\n
{
    //Log::getInstance()->write_log(DEBUG,"in Http::add_black_line");
    add_response("\r\n");
}

void Http::add_headers(size_t len)//生成响应首部，将其写入用户写缓冲区中
{
    //Log::getInstance()->write_log(DEBUG,"in Http::add_headers");
    add_Access_Control_Allow_Origin();
    add_Access_Control_Expose_Headers();
    add_connection();
    add_content_type();
    add_Accept_Ranges();
    add_content_length(len);
    add_Date();
    add_Cache_Control();
    add_Expires();
    add_Token();

}
void Http::add_content(string text)//添加内容
{
    //Log::getInstance()->write_log(DEBUG,"in Http::add_content");
    add_response(text);
}

bool Http::process_write(HTTP_CODE ret)//生成响应报文，将其写入用户写缓冲区中
{
    //Log::getInstance()->write_log(DEBUG,"in Http::process_write");
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

    case FILE_NO_MODIFIED:
        add_status_line("304",ok_304_title);
        add_headers(0);
        if(if_request_has_If_None_Match)
            add_Etag();
        add_black_line();


    case FILE_UPLOAD:
        add_status_line("200",ok_200_title);
        add_headers(upload_status.size());
        add_black_line();
        add_content(upload_status);
        break;

    case RANGE_NO_SATISFIABLE:
        add_status_line("416",error_416_title);
        add_headers(error_416_form.size());
        add_black_line();
        add_content(error_416_form);
        break;

    case FILE_PARTIAL_REQUEST:
        size_t temp_len;
        if(range_left!=(-1) && range_right!=(-1))
        {
            temp_len=range_right-range_left+1;
            m_iov[1].iov_base=m_file_addres+range_left;
            m_iov[1].iov_len=temp_len;
            m_iov_cnt=2;
            m_writebuffer.set_iov(m_iov,m_iov_cnt);

        }
        else if(range_left==(-1) && range_right!=(-1))
        {
            temp_len=range_right;
            m_iov[1].iov_base=m_file_addres+m_file_stat.st_size-temp_len;
            m_iov[1].iov_len=temp_len;
            m_iov_cnt=2;
            m_writebuffer.set_iov(m_iov,m_iov_cnt);

            range_left=m_file_stat.st_size-temp_len;
            range_right=m_file_stat.st_size-1;
        }
        else if(range_left!=(-1) && range_right==(-1))
        {
            temp_len=m_file_stat.st_size-range_left;
            m_iov[1].iov_base=m_file_addres+range_left;
            m_iov[1].iov_len=temp_len;
            m_iov_cnt=2;

            range_right=m_file_stat.st_size-1;
        }

        add_status_line("206",ok_206_title);
        add_headers(temp_len);
        add_Content_Range();
        add_black_line();

        m_writebuffer.set_iov(m_iov,m_iov_cnt);
        return true;
        break;

    case FILE_REQUEST:
        add_status_line("200",ok_200_title);
        add_headers(m_file_stat.st_size);
        add_Etag();
        if(if_request_has_Last_Modified==false)
            add_Last_Modified();
        add_black_line();


        m_iov[1].iov_base=m_file_addres;
        m_iov[1].iov_len=m_file_stat.st_size;
        m_iov_cnt=2;
        m_writebuffer.set_iov(m_iov,m_iov_cnt);
        return true;
        break;

    default:
        break;

    }

    m_iov_cnt=1;
    m_writebuffer.set_iov(m_iov,m_iov_cnt);
    return true;
}

bool Http::Write()//将数据从用户写缓冲区、文件映射地址 写到内核写缓冲区中，返回false说明要关闭连接
{
    //Log::getInstance()->write_log(DEBUG,"in Http::Write");
    int ret=m_writebuffer.writeFD(m_socket);
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
        //Log::getInstance()->write_log(DEBUG,"Write complete");

        if(m_linger)
        {
            init();
            mod_fd_in_epoll(m_socket,EPOLLIN);//重置EPOLLONESHOT可读事件
            return true;
        }
        else
        {
            return false;
        }


    }

    return false;
}

void Http::process()//
    {
        if(task_type==1)//从socket读取数据,报文解析,报文撰写
        {

            bool ret=Read();

            if(ret==false)//关闭连接
            {
                m_timerheap->delTimer(m_timer);
                close_conn();
                return;
            }
            else
            {

                //解析报文
                HTTP_CODE temp_ret=process_read();

                if(temp_ret==NO_REQUEST)
                {
                    mod_fd_in_epoll(m_socket,EPOLLIN);//重置EPOLLONESHOT
                    return;
                }

                //生成响应报文，将其写入用户写缓冲区中
                process_write(temp_ret);

                mod_fd_in_epoll(m_socket,EPOLLOUT);//重置EPOLLONESHOT
                //Log::getInstance()->write_log(DEBUG,"read complete");
            }
        }

        else if(task_type==2)//向socket发送数据
        {
            bool ret=Write();

            if(ret==false)//关闭连接
            {
                m_timerheap->delTimer(m_timer);
                close_conn();
                return;
            }

        }


    }
