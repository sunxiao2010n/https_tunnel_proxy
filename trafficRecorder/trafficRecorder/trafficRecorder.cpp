/************************************************
 * 这个工程依赖于libgo和hiredis，请预先确认动态库正常
*************************************************/
#include <stdio.h>
#include <string>
#include "coroutine.h"
#include "redis_conn_pool.h"
#include "TRNet.h"

//#define REDIS_IP   "120.55.162.100"
//#define REDIS_IP   "127.0.0.1"
#define REDIS_IP   "139.196.232.70"

#define REDIS_PORT  6379

RedisConnPool& conn_pool = RedisConnPool::Instance();

int main(int argc, char **argv)
{
	std::string ip(REDIS_IP);
	std::string pass("");
	int ret = conn_pool.init(ip, REDIS_PORT, pass, 1, 200);
	setbuf(stdout, nullptr);
	
	if (ret == 0)
		ERROR("redis connected");
	else
	{
		ERROR("redis connect error Traffic Recorder exited");
		exit(0);
	}
	
	NetInit();
	
	go[] {
		ERROR("Process reach here if you got an error\n");
		co_yield;
	}
	;
	
	while (1)
	{
		co_sleep(500);
	}
	
	return 0;
}

