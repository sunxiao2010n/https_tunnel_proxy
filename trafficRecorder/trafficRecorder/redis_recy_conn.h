//==========================================================================
/**
 * @file        : redis_recy_conn.h
 *
 * @description : redis命令返回的reply管理，避免reply忘记delete而泄漏
 *
 * $tag         :
 *
 * @author      : 
 */
//==========================================================================

#ifndef __REDIS_RECY_CONN_H__
#define __REDIS_RECY_CONN_H__

class RedisRecycConn
{
public:
    RedisRecycConn();
    RedisRecycConn& operator = (RedisConnect* conn) = delete;
    RedisRecycConn& operator = (RedisRecycConn& free_reply) = delete;
    void operator()(const RedisRecycConn&) = delete;
    RedisConnect* operator->() { return rds_conn_; }
    ~RedisRecycConn();

    void init(RedisConnect* conn, RedisConnPool* conn_pool);

public:
    RedisConnect*  rds_conn_;
    RedisConnPool* redis_pool_;
};

#endif /*__REDIS_RECY_CONN_H__*/