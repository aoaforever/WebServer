#ifndef HTTPCONNECTION_H
#define HTTPCONNECTION_H
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <sys/stat.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/uio.h>
#include <map>
#include <bcrypt/BCrypt.hpp>// 加密密码用

#include "../lock/locker.h"
#include "../CGImysql/sql_connection_pool.h"
#include "../timer/lst_timer.h"
#include "../log/log.h"


/*https://www.cnblogs.com/h-hg/p/8784432.html 类中的static是所有对象都共享的东西，必须要类外初始化*/

class http_conn
{
public:
    static const int FILENAME_LEN = 200;
    static const int READ_BUFFER_SIZE = 2048;
    static const int WRITE_BUFFER_SIZE = 1024;
    //报文的请求方法，本项目只用到GET和POST
    enum METHOD
    {
        GET = 0,
        POST,
        HEAD,
        PUT,
        DELETE,
        TRACE,
        OPTIONS,
        CONNECT,
        PATH
    };
    //主状态机的状态
    enum CHECK_STATE
    {
        CHECK_STATE_REQUESTLINE = 0,//解析请求行状态
        CHECK_STATE_HEADER,//解析请求头部状态
        CHECK_STATE_CONTENT//解析请求内容状态
    };
    //报文解析的结果
    enum HTTP_CODE
    {
        NO_REQUEST,//请求不完整，需要继续读取请求报文数据，跳转主线程继续监测读事件
        GET_REQUEST,//获得了完整的HTTP请求，调用do_request完成请求资源映射
        BAD_REQUEST,//HTTP请求的报文有语法错误,或者请求资源为目录,跳转process_write完成响应报文
        NO_RESOURCE,//请求资源不存在,跳转process_write完成响应报文
        FORBIDDEN_REQUEST,//请求资源禁止访问，没有读取权限
        FILE_REQUEST,//请求资源可以正常访问，跳转process_write完成响应报文
        INTERNAL_ERROR,//服务器内部错误，该结果在主状态机逻辑switch的default下，一般不会触发
        CLOSED_CONNECTION
    };
    //从状态机的状态
    enum LINE_STATUS
    {
        LINE_OK = 0,
        LINE_BAD,
        LINE_OPEN
    };

public:
    http_conn() {}
    ~http_conn() {}

public:
    //初始化套接字地址，函数内部会调用私有方法init，仅仅是对私有成员变量进行初始化
    void init(int sockfd, const sockaddr_in &addr, char *, int, int, string user, string passwd, string sqlname);
    //关闭http连接
    void close_conn(bool real_close = true);
    void process();

    //读取浏览器端发来的全部数据
    bool read_once();

    //响应报文写入函数
    bool write();
    sockaddr_in *get_address()
    {
        return &m_address;
    }

    //同步线程初始化数据库读取表 ， 这是sql的线程池？？
    void initmysql_result(connection_pool *connPool);

    int timer_flag;
    int improv;


private:
    void init();
    //从m_read_buf读取，并处理请求报文
    bool process_write(HTTP_CODE ret);

    HTTP_CODE process_read();//根据状态，调用下面4个函数
    HTTP_CODE parse_request_line(char *text);
    HTTP_CODE parse_headers(char *text);
    HTTP_CODE parse_content(char *text);
    //生成响应报文
    HTTP_CODE do_request();

    //m_start_line是已经解析的字符
    //get_line用于将指针向后偏移，指向未处理的字符
    char *get_line() { return m_read_buf + m_start_line; };

    //从状态机读取一行，分析是请求报文的哪一部分
    LINE_STATUS parse_line();//根据\r\n进行分行判断
    void unmap();

    //根据响应报文格式，生成对应8个部分，
    bool add_response(const char *format, ...);
    bool add_content(const char *content);
    bool add_status_line(int status, const char *title);
    bool add_headers(int content_length);
    bool add_content_type();
    bool add_content_length(int content_length);
    bool add_linger();
    bool add_blank_line();

public:
    static int m_epollfd;//记录该对象由哪个epoll内核事件表管理
    static int m_user_count;//该对象为什么会有多个用户？？
    MYSQL *mysql;
    int m_state;  //读为0, 写为1

private:
    int m_sockfd;//在init()被幅值
    sockaddr_in m_address;//在init()被幅值

    //存储读取的请求报文数据
    char m_read_buf[READ_BUFFER_SIZE];

    //缓冲区中m_read_buf中数据的最后一个字节的下一个位置
    int m_read_idx;//从socket中读入的数据的的最后一个位置。
    //m_read_buf读取的位置
    int m_checked_idx;//已经解析的数据的最后一个位置
    //m_read_buf中已经解析的字符个数
    int m_start_line;

    //存储发出的响应报文数据
    char m_write_buf[WRITE_BUFFER_SIZE];
    //指示buffer中的长度
    int m_write_idx;

    //主状态机的状态
    CHECK_STATE m_check_state;
    //请求方法
    METHOD m_method;

    //以下为解析请求报文中对应的6个变量
    //存储读取文件的名称
    char m_real_file[FILENAME_LEN];//在init中初始化为空，在do_request被赋值
    char *m_url;
    char *m_version;
    char *m_host;
    int m_content_length;
    bool m_linger;

    //读取服务器上的文件地址
    char *m_file_address;
    struct stat m_file_stat;//利用stat（）获取请求文件的属性
    struct iovec m_iv[2];//io向量机制iovec
    int m_iv_count;
    int cgi;        //是否启用的POST，GET的话cgi=0
    char *m_string; //存储请求头数据,存储用户密码user=***&passwd=***
    int bytes_to_send;//剩余发送字节数
    int bytes_have_send;//已经发送字节数
    char *doc_root;//服务器根目录？在init被初始化

    map<string, string> m_users;//未使用，而是直接在.cpp中用的另一个红黑树映射，避免了每个实例都保存一个这个
    int m_TRIGMode;//void WebServer::trig_mode()设置
    int m_close_log;

    char sql_user[100];//每个请求对象都有一个数据库用户账号与密码和用户名
    char sql_passwd[100];
    char sql_name[100];
};

#endif
