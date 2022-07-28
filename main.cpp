#include "config.h"

int main(int argc, char *argv[])
{
    //需要修改的数据库信息,登录名,密码,库名
    string user = "root"; //登录数据库的用户名
    string passwd = "1234567890";//数据库密码
    string databasename = "webserver";

    //命令行解析
    Config config;
    config.parse_arg(argc, argv);

    WebServer server;//调用默认初始化函数，新建65535个http类对象，获取项目根目录之类的，新建65535个client_data类，这个类包含定时器指针。

    //初始化
    server.init(config.PORT, user, passwd, databasename, config.LOGWrite, 
                config.OPT_LINGER, config.TRIGMode,  config.sql_num,  config.thread_num, 
                config.close_log, config.actor_model);//初始化各种一维变量
    

    //日志
    server.log_write();

    //数据库
    server.sql_pool();

    //线程池
    server.thread_pool();

    //触发模式
    server.trig_mode();

    //监听
    server.eventListen();

    //运行
    server.eventLoop();

    return 0;
}