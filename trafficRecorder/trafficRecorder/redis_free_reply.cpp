
#include "redis_free_reply.h"

RedisFreeReply::RedisFreeReply(redisReply* areply) : reply_(areply)
{
}

RedisFreeReply::~RedisFreeReply()
{
    try
    {
        if (NULL != reply_)
        {
            freeReplyObject(reply_);
            reply_ = NULL;
        }
    }
    catch (...)
    {
    }
}