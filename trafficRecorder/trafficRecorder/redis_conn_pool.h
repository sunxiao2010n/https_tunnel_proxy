//==========================================================================
/**
 * @file        : redis_conn_pool.h
 *
 * @description : 只能单线程使用，多线程使用非线程安全
 *
 * $tag         :
 *
 * @author      : 
 */
 //==========================================================================

#ifndef __REDIS_CONN_POOL_H__
#define __REDIS_CONN_POOL_H__

#include <deque>
#include <stack>
#include "coroutine.h"
#include "redis_connect.h"
#include "redis_free_reply.h"
#include "redis_recy_conn.h"
#include "TRTypeDefine.h"

class RedisConnPool
{
public:
	RedisConnPool();
	virtual ~RedisConnPool();

	int           init(string& host_ip, __uint16 port, string& passwd
		, int timeout = 1, int pool_size = 10);

	//直接调用执行一个redis命令，无需手动get_connection和push_back_connection
	redisReply*    exec_command(const char* format, ...);
	int            exec_command_int(long long& result, const char* format, ...);
	int            exec_command_string(string& result, const char* format, ...);
	int            exec_command_vec_string(vector<string>& result, const char* format, ...);
	int            exec_command_pipeline(vector<string>& result, int cmd_count, const char* format, ...);

	//从连接池get一个连接，使用完成后必须手动调用push_back_connection函数放回池中
	RedisConnect*  get_connection();
	void           get_connection(RedisRecycConn& recyc_conn);
	void           push_back_connection(RedisConnect* rds_conn);
	size_t			get_chan_size();
	
	static RedisConnPool& Instance()
	{
		static RedisConnPool inst_;
		return inst_;
	}
protected:
	int            init_redis_pool();
	void			co_wait();
	void			co_ready();
protected:
	string                host_ip_;
	__uint16              port_;
	string                passwd_;
	int                   timeout_;         // 单个请求连接超时时长，单位:秒
	int                   pool_size_;       // 连接池初始时的连接数
	std::stack<RedisConnect*> redis_conn_stack_;
	co_chan<int>*		  ready_chan_;
};

#endif /*__REDIS_CONN_POOL_H__*/
