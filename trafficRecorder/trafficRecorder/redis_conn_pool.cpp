
#include "redis_conn_pool.h"
#include "redis_free_reply.h"

#define DEF_REDIS_CONN_CNT 100

RedisConnPool::RedisConnPool()
	: ready_chan_(NULL)
{
    port_      = 0;
    timeout_   = 0;
    pool_size_ = 0;
}

RedisConnPool::~RedisConnPool()
{
	if (!ready_chan_)return;

	RedisConnect* my_conn = NULL;
	while (!redis_conn_stack_.empty())
	{
		my_conn = redis_conn_stack_.top();
		redis_conn_stack_.pop();
		if (!my_conn)continue;
		delete my_conn;
		my_conn = NULL;
	}
	delete ready_chan_;
	ready_chan_ = NULL;
}

int RedisConnPool::init(string& host_ip, __uint16 port
    , string& passwd, int timeout, int pool_size)
{
    host_ip_   = host_ip;
    port_      = port;
    passwd_    = passwd;
    timeout_   = timeout;
    pool_size_ = pool_size;
    return init_redis_pool();
}

int RedisConnPool::init_redis_pool()
{
	///inited
	if (ready_chan_)
		return 0;

	if (pool_size_ <= 0)
	{
		ERROR("RedisConnPool::init_redis_pool pool size is zero\n");
		return -1;
	}

	if (host_ip_.empty())
	{
		ERROR("RedisConnPool::init_redis_pool pool host ip is empty\n");
		return -1;
	}
    if (port_ == 0)
        port_ = 6379;

	ready_chan_ = new co_chan<int>(pool_size_);
	for (int i = 0; i < pool_size_; i++)
	{
		RedisConnect* rds_conn = new RedisConnect();
		rds_conn->init(host_ip_, port_, passwd_, timeout_);
		redis_conn_stack_.push(rds_conn);
		co_ready();
	}
	
    return 0;
}

redisReply* RedisConnPool::exec_command(const char* format, ...)
{
    RedisConnect* rds_conn = get_connection();
    if (NULL == rds_conn)
        return NULL;

    redisReply* reply = NULL;
    va_list ap;
    va_start(ap, format);
    reply = rds_conn->exec_v_command(format, ap);
    va_end(ap);
    
    push_back_connection(rds_conn);
    return reply;
}

int RedisConnPool::exec_command_int(long long& result, const char* format, ...)
{
    RedisConnect* rds_conn = get_connection();
    if (NULL == rds_conn)
        return -1;

    va_list ap;
    va_start(ap, format);
    int ret = rds_conn->exec_v_command_int(result, format, ap);
    va_end(ap);

    push_back_connection(rds_conn);
    return ret;
}

int RedisConnPool::exec_command_string(string& result, const char* format, ...)
{
    RedisConnect* rds_conn = get_connection();
    if (NULL == rds_conn)
        return -1;

    va_list ap;
    va_start(ap, format);
    int ret = rds_conn->exec_v_command_string(result, format, ap);
    va_end(ap);

    push_back_connection(rds_conn);
    return ret;
}

int RedisConnPool::exec_command_pipeline(vector<string>& result, int cmd_count, const char* format, ...)
{
	RedisConnect* rds_conn = get_connection();
	if (NULL == rds_conn)
		return -1;

	va_list ap;
	va_start(ap, format);
	int ret = rds_conn->exec_command_pipeline(result, cmd_count, format, ap);
	va_end(ap);

	push_back_connection(rds_conn);
	return ret;
}

int RedisConnPool::exec_command_vec_string(vector<string>& result, const char* format, ...)
{
    RedisConnect* rds_conn = get_connection();
    if (NULL == rds_conn)
        return -1;

    va_list ap;
    va_start(ap, format);
    int ret = rds_conn->exec_v_command_vec_string(result, format, ap);
    va_end(ap);

    push_back_connection(rds_conn);
    return ret;
}

void RedisConnPool::co_wait()
{
	static int i = 0;
	*ready_chan_ >> i;
}

void RedisConnPool::co_ready()
{
	*ready_chan_ << 1;
}

RedisConnect* RedisConnPool::get_connection()
{
	RedisConnect* rds_conn = NULL;
	co_wait();
	rds_conn = redis_conn_stack_.top();
	redis_conn_stack_.pop();
    return rds_conn;
}

void RedisConnPool::get_connection(RedisRecycConn& recyc_conn)
{
    RedisConnect* rds_conn = get_connection();
    recyc_conn.init(rds_conn, this);
}

void RedisConnPool::push_back_connection(RedisConnect* rds_conn)
{
	co_ready();
	redis_conn_stack_.push(rds_conn);
}

size_t RedisConnPool::get_chan_size()
{
	if(!ready_chan_)
		return 0;
	return ready_chan_->size();
}
