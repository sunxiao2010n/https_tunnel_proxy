
#include "redis_conn_pool.h"
#include "redis_recy_conn.h"

RedisRecycConn::RedisRecycConn()
{
    rds_conn_   = NULL;
    redis_pool_ = NULL;
}

RedisRecycConn::~RedisRecycConn()
{
    if (redis_pool_ != NULL)
        redis_pool_->push_back_connection(rds_conn_);
}

void RedisRecycConn::init(RedisConnect* conn, RedisConnPool* conn_pool)
{
    rds_conn_   = conn;
    redis_pool_ = conn_pool;
}