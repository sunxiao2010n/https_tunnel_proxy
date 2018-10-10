//==========================================================================
/**
 * @file        : redis_connect.h
 *
 * @description : redis单个连接管理
 *
 * $tag         :
 *
 * @author      : 
 */
//==========================================================================

#ifndef __REDIS_CONNECT_H__
#define __REDIS_CONNECT_H__

#include <string>
#include <vector>
#include "TRTypeDefine.h"

extern "C"
{
#include "hiredis.h"
}

enum SCAN_TYPE{
	REDIS_SCAN_TYPE_KEY = 1,
	REDIS_SCAN_TYPE_VAL = 2,
	REDIS_SCAN_TYPE_ALL = 3
};

using namespace std;
class RedisConnect
{
public:
    RedisConnect();
    virtual ~RedisConnect();

    void           init(string& host_ip, __uint16 port, string& passwd, int timeout = 1);
    bool           try_connect();
    redisContext*  connect_with_timeout(string& host_ip, __uint16 port, int timeout);
    bool           auth(string& passwd);

    redisReply*    exec_command(const char* format, ...);
    redisReply*    exec_v_command(const char* format, va_list ap);

    //适合返回结果为int型命令，例如：set、llen、hset、del、hdel等
    int            exec_command_int(long long& result, const char* format, ...);
	int            exec_v_command_int(long long& result, const char* format, va_list ap);

    //适合返回结果为字符串命令，例如：get、hget、lpop等
    int            exec_command_string(string& result, const char* format, ...);
    int            exec_v_command_string(string& result, const char* format, va_list ap);
    //适合返回结果为vector<string>，例如：hvals等
    int            exec_command_vec_string(vector<string>& result, const char* format, ...);
    int            exec_v_command_vec_string(vector<string>& result, const char* format, va_list ap);
    //
	template<SCAN_TYPE Stype>
	int            exec_command_hscan(__int64& curserid, vector<string>& result, const char* format, ...);

	template<SCAN_TYPE SType>
	int            exec_v_command_hscan(__int64& curserid, vector<string>& result, const char* format, va_list ap);

    bool           check_reply(const redisReply* reply);
	int            exec_command_pipeline(vector<string>& result, int cmd_count, const char* format, ...);

public:
    string          host_ip_;
    __uint16        port_;
    string          passwd_;
    int             timeout_;         // 单个请求连接超时时长，单位:秒
    redisContext*   redis_ctx_;
    bool            b_connect_;
};

#endif /*__REDIS_CONNECT_H__*/