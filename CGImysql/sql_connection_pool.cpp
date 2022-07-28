#include <mysql/mysql.h>
#include <stdio.h>
#include <string>
#include <string.h>
#include <stdlib.h>
#include <list>
#include <pthread.h>
#include <iostream>
#include "sql_connection_pool.h"

using namespace std;

connection_pool::connection_pool()
{
	m_CurConn = 0;
	m_FreeConn = 0;
}

connection_pool *connection_pool::GetInstance()
{
	static connection_pool connPool;
	return &connPool;
}

//构造初始化
void connection_pool::init(string url, string User, string PassWord, string DBName, int Port, int MaxConn, int close_log)
{
	m_url = url;// localhost
	m_Port = Port; // 3306
	m_User = User; // 
	m_PassWord = PassWord;
	m_DatabaseName = DBName;
	m_close_log = close_log;

	//创建MaxConn条数据库连接
	for (int i = 0; i < MaxConn; i++)
	{
		MYSQL *con = NULL;
		con = mysql_init(con);

		if (con == NULL)
		{
			LOG_ERROR("MySQL Error");
			exit(1);
		}
		con = mysql_real_connect(con, url.c_str(), User.c_str(), PassWord.c_str(), DBName.c_str(), Port, NULL, 0);

		if (con == NULL)
		{
			LOG_ERROR("MySQL Error");
			exit(1);
		}
		//更新连接池和空闲连接数量
		connList.push_back(con);
		++m_FreeConn;
	}
	 //将信号量初始化为最大连接次数
	reserve = sem(m_FreeConn);

	m_MaxConn = m_FreeConn;
}

/*
当线程数量大于数据库连接数量时，使用信号量进行同步，每次取出连接，信号量原子减1，释放连接原子加1，若连接池内没有连接了，则阻塞等待。

另外，由于多线程操作连接池，会造成竞争，这里使用互斥锁完成同步，具体的同步机制均使用lock.h中封装好的类。
*/

//当有请求时，从数据库连接池中返回一个可用连接，更新使用和空闲连接数
MYSQL *connection_pool::GetConnection()
{
	MYSQL *con = NULL;

	if (0 == connList.size())
		return NULL;

	//取出连接，信号量原子减1，为0则等待
	reserve.wait();
	
	lock.lock();

	con = connList.front();
	connList.pop_front();

	//这里的两个变量，并没有用到，非常鸡肋...
	--m_FreeConn;
	++m_CurConn;

	lock.unlock();
	return con;
}

//释放当前使用的连接
bool connection_pool::ReleaseConnection(MYSQL *con)
{
	if (NULL == con)
		return false;

	lock.lock();

	connList.push_back(con);
	++m_FreeConn;
	--m_CurConn;

	lock.unlock();

	//释放连接原子加1
	reserve.post();
	return true;
}

//销毁数据库连接池
void connection_pool::DestroyPool()
{

	lock.lock();
	if (connList.size() > 0)
	{
		list<MYSQL *>::iterator it;
		for (it = connList.begin(); it != connList.end(); ++it)
		{
			MYSQL *con = *it;
			mysql_close(con);
		}
		m_CurConn = 0;
		m_FreeConn = 0;
		connList.clear();
	}

	lock.unlock();
}

//当前空闲的连接数
int connection_pool::GetFreeConn()
{
	return this->m_FreeConn;
}


connection_pool::~connection_pool()
{
	DestroyPool();
}

//将数据库连接的获取与释放通过RAII机制封装，避免手动释放
//不直接调用获取和释放连接的接口，将其封装起来，通过RAII机制进行获取和释放。
connectionRAII::connectionRAII(MYSQL **SQL, connection_pool *connPool){
	/*
	//先从连接池中取一个连接
    MYSQL *mysql = NULL;
    connectionRAII mysqlcon(&mysql, connPool);
	*/
	//mysql存的是MYSQL的地址，&mysql是mysql的地址
	//*&mysql 那就是指向MYSQL的地址， **&mysql那就是MYSQL本身实例
	*SQL = connPool->GetConnection();
	
	conRAII = *SQL;//MYSQL *conRAII;
	poolRAII = connPool;//connection_pool *poolRAII;
}

connectionRAII::~connectionRAII(){
	poolRAII->ReleaseConnection(conRAII);
}