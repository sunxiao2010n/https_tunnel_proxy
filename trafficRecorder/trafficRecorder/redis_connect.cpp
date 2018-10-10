#include <string.h>
#include "redis_connect.h"
#include "redis_free_reply.h"
#include "TRTypeDefine.h"


// RedisConnect
RedisConnect::RedisConnect()
{
    b_connect_ = false;
    redis_ctx_ = NULL;
}

RedisConnect::~RedisConnect()
{
    if (NULL != redis_ctx_)
    {
        redisFree(redis_ctx_);
        redis_ctx_ = NULL;
    }
}

void RedisConnect::init(string& host_ip, __uint16 port, string& passwd, int timeout)
{
    host_ip_ = host_ip;
    port_    = port;
    passwd_  = passwd;
    timeout_ = timeout;
}

bool RedisConnect::try_connect()
{
    bool ret = false;
    if (host_ip_.empty())
        return ret;

    if (NULL != redis_ctx_)
    {
        redisFree(redis_ctx_);
        redis_ctx_ = NULL;
        b_connect_ = false;
    }

    redis_ctx_ = connect_with_timeout(host_ip_, port_, timeout_);
    if (NULL == redis_ctx_)
    {
        b_connect_ = false;
        return ret;
    }
    else
    {
        struct timeval tmout_val;
        tmout_val.tv_sec = timeout_;
        tmout_val.tv_usec = 0;
        redisSetTimeout(redis_ctx_, tmout_val);
        ret = auth(passwd_);
        if (!ret)
        {
            redisFree(redis_ctx_);
            redis_ctx_ = NULL;
            b_connect_ = false;
            return ret;
        }
    }
    b_connect_ = true;
    return ret;
}

redisContext* RedisConnect::connect_with_timeout(string& host_ip, __uint16 port, int timeout)
{
    redisContext* context = NULL;
    if (timeout != 0)
    {
        struct timeval timeoutVal;
        timeoutVal.tv_sec = timeout;
        timeoutVal.tv_usec = 0;
        context = redisConnectWithTimeout(host_ip.c_str(), port, timeoutVal);
    }
    else
        context = redisConnect(host_ip.c_str(), port);

    if (NULL == context || context->err != 0)
    {
        if (NULL != context)
        {
            ERROR("redis_connect error: %s, ip = %s, port = %d"
                , context->errstr, host_ip.c_str(), port);
            redisFree(context);
            context = NULL;
        }
        else
            ERROR("redis_connect error:can't allocate redis context");
    }
    return context;
}

bool RedisConnect::auth(string& passwd)
{
    bool bRet = false;
    redisReply *reply = static_cast<redisReply *>(redisCommand(redis_ctx_, "AUTH %s", passwd.c_str()));
    if (NULL == reply ||
        (strcasecmp(reply->str, "OK") != 0 &&                                        // AUTH success
         strcasecmp(reply->str, "ERR Client sent AUTH, but no password is set") != 0 // Don't need password auth
        )
       )
    {
        /* Authentication failed */
        ERROR("redis_connect password error, password = %s", passwd.c_str());
        if (NULL != reply)
        {
            ERROR("redis_connect reply error: %s", reply->str);
            freeReplyObject(reply);
        }
        bRet = false;
        return bRet;
    }

    bRet = true;
    freeReplyObject(reply);
    return bRet;
}

redisReply* RedisConnect::exec_command(const char* format, ...)
{
    redisReply* reply = NULL;
    va_list ap;
    va_start(ap, format);
    reply = exec_v_command(format, ap);
    va_end(ap); 
    return reply;
}

redisReply* RedisConnect::exec_v_command(const char* format, va_list ap)
{
    redisReply* reply = NULL;
    if (!b_connect_ || (redis_ctx_ == NULL))
    {
        bool ret = try_connect();
        if (!ret)
            return NULL;
    }

    va_list tmp_ap;
    va_copy(tmp_ap, ap);
    reply = static_cast<redisReply*>(redisvCommand(redis_ctx_, format, ap));
    if ((NULL == reply) || (redis_ctx_->err == REDIS_ERR_EOF
        && strcmp(redis_ctx_->errstr, "Server closed the connection") == 0)
        )
    {
        if (reply != NULL)
        {
            freeReplyObject(reply);
            reply = NULL;
        }
        int reconn = redisReconnect(redis_ctx_);
        if (REDIS_OK == reconn)
        {
            b_connect_ = true;
            reply = static_cast<redisReply*>(redisvCommand(redis_ctx_, format, tmp_ap));
        }
    }
    va_end(tmp_ap);

    check_reply(reply);
    return reply;
}

int RedisConnect::exec_command_int(long long& result, const char* format, ...)
{
    va_list ap;
    va_start(ap, format);
    long long ret = exec_v_command_int(result, format, ap);
    va_end(ap);
    return ret;
}

int RedisConnect::exec_v_command_int(long long& result, const char* format, va_list ap)
{
    RedisFreeReply free_ply = exec_v_command(format, ap);
    if (!check_reply(free_ply.reply_))
    {
        return -1;
    }

    if (free_ply.reply_->type != REDIS_REPLY_INTEGER && free_ply.reply_->type != REDIS_REPLY_STATUS)
        return -1;
    
    result = free_ply.reply_->integer;
    return 0;
}

int RedisConnect::exec_command_string(string& result, const char* format, ...)
{
    va_list ap;
    va_start(ap, format);
    int ret = exec_v_command_string(result, format, ap);
    va_end(ap);
    return ret;
}

int RedisConnect::exec_v_command_string(string& result, const char* format, va_list ap)
{
    RedisFreeReply free_ply = exec_v_command(format, ap);
    if (!check_reply(free_ply.reply_))
    {
        return -1;
    }
	
	//reply type must be string
    if (free_ply.reply_->type == REDIS_REPLY_STRING)
    {
        result.assign(free_ply.reply_->str, free_ply.reply_->len);
        return 0;
    }
    else if (free_ply.reply_->type == REDIS_REPLY_NIL)
        return 1;
    else
        return -1;
}

int RedisConnect::exec_command_vec_string(vector<string>& result, const char* format, ...)
{
    va_list ap;
    va_start(ap, format);
    int ret = exec_v_command_vec_string(result, format, ap);
    va_end(ap);
    return ret;
}

int RedisConnect::exec_v_command_vec_string(vector<string>& vec_result, const char* format, va_list ap)
{
    RedisFreeReply free_ply = exec_v_command(format, ap);
    if (!check_reply(free_ply.reply_))
    {
        return -1;
    }

    if (REDIS_REPLY_ARRAY == free_ply.reply_->type)
    {
        for (int i = 0; i < free_ply.reply_->elements; ++i)
        {
            if (!check_reply(free_ply.reply_->element[i]))
            {
                return -1;
            }
            if (REDIS_REPLY_STRING == free_ply.reply_->element[i]->type)
            {
                string result;
                result.assign(free_ply.reply_->element[i]->str, free_ply.reply_->element[i]->len);
                vec_result.push_back(result);
            }
        }
    }
    else if (REDIS_REPLY_STRING == free_ply.reply_->type)
    {
        if (!check_reply(free_ply.reply_))
        {
            return -1;
        }
        string result;
        result.assign(free_ply.reply_->str, free_ply.reply_->len);
        vec_result.push_back(result);
    }
    return 0;
}


template<>
int RedisConnect::exec_v_command_hscan<REDIS_SCAN_TYPE_KEY>(__int64& curserid, vector<string>& vresult, const char* format, va_list ap)
{
	RedisFreeReply free_ply = exec_v_command(format, ap);
	if (!check_reply(free_ply.reply_))
	{
		return -1;
	}

	if (REDIS_REPLY_ARRAY == free_ply.reply_->type)
	{
		//no more
		if (free_ply.reply_->elements == 0)
		{
			curserid = 0;
			return 0;
		}
		curserid = strtoull(free_ply.reply_->element[0]->str, NULL, 10);

		redisReply* elements = free_ply.reply_->element[1];
		redisReply* pReply = NULL;
		string result;

		if (vresult.capacity() < elements->elements / 2)
		{
			vresult.reserve(free_ply.reply_->elements / 2);
		}

		for (int i = 0; i < elements->elements; i += 2)
		{
			pReply = elements->element[i];
			if (!check_reply(pReply))
			{
				return -1;
			}
			if (REDIS_REPLY_STRING == pReply->type)
			{
				result.assign(pReply->str, pReply->len);
				vresult.push_back(std::move(result));
			}
		}
		return 0;
	}
	return -1;
}


template<>
int RedisConnect::exec_v_command_hscan<REDIS_SCAN_TYPE_VAL>(__int64& curserid, vector<string>& vresult, const char* format, va_list ap)
{
	RedisFreeReply free_ply = exec_v_command(format, ap);
	if (!check_reply(free_ply.reply_))
	{
		return -1;
	}

	if (REDIS_REPLY_ARRAY == free_ply.reply_->type)
	{
		//no more
		if (free_ply.reply_->elements == 0)
		{
			curserid = 0;
			return 0;
		}
		curserid = strtoull(free_ply.reply_->element[0]->str, NULL, 10);

		redisReply* elements = free_ply.reply_->element[1];
		redisReply* pReply = NULL;
		string result;

		if (vresult.capacity() < elements->elements / 2)
		{
			vresult.reserve(free_ply.reply_->elements / 2);
		}

		for (int i = 0; i < elements->elements; i += 2)
		{
			pReply = elements->element[i + 1];
			if (!check_reply(pReply))
			{
				return -1;
			}
			if (REDIS_REPLY_STRING == pReply->type)
			{
				result.assign(pReply->str, pReply->len);
				vresult.push_back(std::move(result));
			}
		}
		return 0;
	}
	return -1;
}


template<>
int RedisConnect::exec_v_command_hscan<REDIS_SCAN_TYPE_ALL>(__int64& curserid, vector<string>& vresult, const char* format, va_list ap)
{
	RedisFreeReply free_ply = exec_v_command(format, ap);
	if (!check_reply(free_ply.reply_))
	{
		return -1;
	}

	if (REDIS_REPLY_ARRAY == free_ply.reply_->type)
	{
		//no more
		if (free_ply.reply_->elements == 0)
		{
			curserid = 0;
			return 0;
		}
		curserid = strtoull(free_ply.reply_->element[0]->str, NULL, 10);

		redisReply* elements = free_ply.reply_->element[1];
		redisReply* pKey = NULL;
		redisReply* pVal = NULL;
		string result;

		if (vresult.capacity() < elements->elements)
		{
			vresult.reserve(free_ply.reply_->elements);
		}

		for (int i = 0; i < elements->elements; i++)
		{
			pKey = elements->element[i];
			pVal = elements->element[i];
			if (!check_reply(pKey) || !check_reply(pVal))
			{
				return -1;
			}
			if (REDIS_REPLY_STRING == pKey->type && REDIS_REPLY_STRING == pVal->type)
			{
				result.assign(pKey->str, pKey->len);
				vresult.push_back(std::move(result));

				result.assign(pVal->str, pVal->len);
				vresult.push_back(std::move(result));
			}
		}
		return 0;
	}
	return -1;
}

template<>
int RedisConnect::exec_command_hscan<REDIS_SCAN_TYPE_KEY>(__int64& curserid, vector<string>& result, const char* format, ...)
{
	va_list ap;
	va_start(ap, format);
	int ret = exec_v_command_hscan<REDIS_SCAN_TYPE_KEY>(curserid, result, format, ap);
	va_end(ap);
	return ret;
}


template<>
int RedisConnect::exec_command_hscan<REDIS_SCAN_TYPE_VAL>(__int64& curserid, vector<string>& result, const char* format, ...)
{
	va_list ap;
	va_start(ap, format);
	int ret = exec_v_command_hscan<REDIS_SCAN_TYPE_VAL>(curserid, result, format, ap);
	va_end(ap);
	return ret;
}


template<>
int RedisConnect::exec_command_hscan<REDIS_SCAN_TYPE_ALL>(__int64& curserid, vector<string>& result, const char* format, ...)
{
	va_list ap;
	va_start(ap, format);
	int ret = exec_v_command_hscan<REDIS_SCAN_TYPE_ALL>(curserid, result, format, ap);
	va_end(ap);
	return ret;
}

bool RedisConnect::check_reply(const redisReply* reply)
{
    bool ret = true;
    if (NULL == reply)
    {
        redisFree(redis_ctx_);
        redis_ctx_ = NULL;
        b_connect_ = false;
        ERROR("redis command error : Server closed the connection");
        ret = false;
    }
    else
    {
        switch (reply->type)
        {
            case REDIS_REPLY_STRING:
            case REDIS_REPLY_ARRAY:
            case REDIS_REPLY_INTEGER:
            case REDIS_REPLY_NIL:
            {
                ret = true;
                break;
            }
            case REDIS_REPLY_STATUS:
            {
                ret = (strcasecmp(reply->str, "OK") == 0) ? true : false;
                break;
            }
            case REDIS_REPLY_ERROR:
            {
                ret = false;
                break;
            }
            default:
            {    
                ret = false;
            }
        }
	    if (!ret)
	    {
		    ERROR("redis command error : %s", reply->str);
	    }

    }
    return ret;
}

int RedisConnect::exec_command_pipeline(vector<string>& result, int cmd_count, const char* format, ...)
{
	redisReply *reply;
	redisAppendCommand(redis_ctx_, "incr foo");
	redisAppendCommand(redis_ctx_, "incr foo");
	redisGetReply(redis_ctx_, (void**)&reply);   // reply for SET
	freeReplyObject(reply);
	redisGetReply(redis_ctx_, (void**)&reply);    // reply for GET
	freeReplyObject(reply);
	return 0;
}