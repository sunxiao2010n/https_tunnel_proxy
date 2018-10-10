#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <memory.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "coroutine.h"
#include "TRTypeDefine.h"
#include "redis_conn_pool.h"
#include "TMSMessage.h"


static short TcpRcvPort = 10170;
static RedisConnPool& redis_pool = RedisConnPool::Instance();


int SendMsg2Tms(int sock_fd, U8* msg, U32 msglen)
{
	hdr_t hdr;
	hdr.len = htons(msglen);
	U8 tmsMsg[1024];
	memcpy(tmsMsg, &hdr, sizeof hdr);
	memcpy(tmsMsg + sizeof hdr, msg, msglen);
	int to_send_len = sizeof(hdr) + msglen;

	int sentlen = write(sock_fd, tmsMsg, to_send_len);
	
	if (sentlen < 1)
	{
		_Pr("SendMsg2Tms error = %d", errno);
		return sentlen;
	}
	return sentlen;
}

int NetRead(int fd)
{
	int length;
	while (1)
	{
		struct msg_t
		{
			hdr_t hdr;
			U8 rz_buf[1024];
		};

		msg_t* pmsg = new msg_t;
		size_t read_pos=0;
		size_t message_len=0;
		
		//ReadTokenReq* msg;
		length = read(fd, (char*)&(pmsg->hdr), sizeof(pmsg->hdr));
		if (length <= 0)
		{
			close(fd);
			ERROR("connection closed");
			return 0;
		}
		
		length = read(fd, (char*)(pmsg->rz_buf), ntohs(pmsg->hdr.len));
		if (length <= 0)
		{
			close(fd);
			ERROR("connection closed");
			return 0;
		}
		
		int msgid = ((MessageID*)pmsg->rz_buf)->msg_id;
		ERROR("new message : id=%d",msgid);
		
		switch (msgid)
		{
		case READ_TOKEN_REQ:
			{
				go[=]
				{
					ReadTokenReq* req = (ReadTokenReq*)pmsg->rz_buf;
					char getcmd[1024];
					char sz_buf[1024];
					char print_buf[1024];
					ReadTokenRsp* rsp = (ReadTokenRsp*)sz_buf;

					//第一次查询 用户口令
					sprintf(getcmd,
						"get auth:%0.11lu:%0.8lu",
						req->mobile,
						req->app
						);
					//ERROR("<--%s", getcmd);
					RedisFreeReply r = redis_pool.exec_command(getcmd);
					
					rsp->msg_id = READ_TOKEN_RSP;
					rsp->cid = req->cid;
					rsp->app = req->app;
					rsp->mobile = req->mobile;
					
					if (!r.reply_)
					{
						ERROR("fail get redis reply, redis not started?");
					}
					
					if (r.reply_->type == REDIS_REPLY_NIL)
					{
						//用户不存在
						rsp->rsp_code = USER_NOT_EXIST;
						SendMsg2Tms(fd, (U8*)rsp, sizeof(ReadTokenRsp));
						delete pmsg;
						return;
					}
					int req_token_len = strlen((char*)req->token);
					int len = r.reply_->len > req_token_len ? r.reply_->len : req_token_len;
					char* str = r.reply_->str;
					if (0 != memcmp(req->token, str, len))
					{
						//token错误
						rsp->rsp_code = INCORRECT_TOKEN;
						SendMsg2Tms(fd, (U8*)rsp, sizeof(ReadTokenRsp));
						delete pmsg;
						return;
					}
					memcpy(rsp->token, str, len);
					rsp->token[len] = 0;
					
					//日志
					snprintf(print_buf, len + 1, str);
					ERROR("%s , %s", getcmd, print_buf);
					
					//第二次查询 流量余额
					sprintf(getcmd,
						"get bal:%0.11lu:%0.8lu",
						req->mobile,
						req->app);
					
					RedisFreeReply rt = redis_pool.exec_command(getcmd);
					if (rt.reply_->type == REDIS_REPLY_NIL)
					{
						//没查询到余量
						ERROR("error-- %s", getcmd);
						rsp->rsp_code = NO_BALANCE;
						SendMsg2Tms(fd, (U8*)rsp, sizeof(ReadTokenRsp));
						delete pmsg;
						return;
					}
					
					rsp->balance = atoll(rt.reply_->str);
					//日志
					ERROR("%s , %ld", getcmd, rsp->balance);

					if (rsp->balance < 4000)
						rsp->rsp_code = NO_BALANCE;
					else
					{
						rsp->rsp_code = 0;
						//SendMsg2Dpi((U8*)rsp, sizeof(ReadTokenRsp));
					}

					SendMsg2Tms(fd, (U8*)rsp, sizeof(ReadTokenRsp));
					delete pmsg;
				}
				;
			}
			break;
		case TRAFFIC_REPORT:
			{
				go[=]
				{
					TrafficReportRsp rsp;
					TrafficReport* req = (TrafficReport*)pmsg->rz_buf;
					char detailcmd[1024];
					char getcmd[1024];
					char setcmd[1024];
					char log[1024];
					
					rsp.mobile = req->mobile;
					rsp.app = req->app;
					rsp.traffic = req->traffic;
					rsp.ip = req->ip;
					//
					//
					//后期需考虑同步机制
					//
					sprintf(getcmd,
						"get bal:%0.11lu:%0.8lu",
						req->mobile,
						req->app);
					RedisFreeReply r = redis_pool.exec_command(getcmd);
					if (!r.reply_ || r.reply_->type != REDIS_REPLY_STRING)
					{
						//查询余额失败
						ERROR("[fail] reducing traffic 1|%llu|bal:%0.11lu:%0.8lu",
							req->traffic,
							req->mobile,
							req->app);
						rsp.rsp_code = 0xff;
						SendMsg2Tms(fd, (U8*)&rsp, sizeof(TrafficReportRsp));
						delete pmsg;
						return;
					}
					
					long long balance = atoll(r.reply_->str);
					long long remain = balance - req->traffic;
					
					sprintf(setcmd,
						"set bal:%0.11lu:%0.8lu %ld",
						req->mobile,
						req->app,
						remain);
					RedisFreeReply r2 = redis_pool.exec_command(setcmd);
					if (!r2.reply_ || r2.reply_->type != REDIS_REPLY_STATUS
						|| r2.reply_->str[0] != 'O' || r2.reply_->str[1] != 'K')
					{
						//更新余额到redis失败
						ERROR("[fail] reducing traffic 2|%llu|bal:%0.11lu:%0.8lu",
							req->traffic,
							req->mobile,
							req->app);
						rsp.rsp_code = 0xff;
						SendMsg2Tms(fd, (U8*)&rsp, sizeof(TrafficReportRsp));
						delete pmsg;
						return;
					}
					
					ERROR("update balance %lu->%lu", remain, balance);

					//记录详单
					//
					//
					timeval *bt = &req->btime;
					time_t  *bsec = &bt->tv_sec;
					timeval *et = &req->etime;
					time_t  *esec = &et->tv_sec;
					
					sprintf(detailcmd,
						"rpush dtl:%0.11lu:%0.8lu "
						"%s,%lu,"
						"%u%0.3d,%u%0.3d"
						,req->mobile,
						req->app,
						req->host,
						req->traffic,
						bt->tv_sec,bt->tv_usec/1000,
						et->tv_sec,et->tv_usec/1000
						);
					RedisFreeReply r3 = redis_pool.exec_command(detailcmd);
					if (!r3.reply_ || r3.reply_->type != REDIS_REPLY_INTEGER || r3.reply_->integer < 1)
					{
						//插入明细到redis失败
						ERROR("[fail] inserting detail|%s", detailcmd);
						rsp.rsp_code = 0xff;
						SendMsg2Tms(fd, (U8*)&rsp, sizeof(TrafficReportRsp));
						delete pmsg;
						return;
					}

					sprintf(setcmd, "expire auth:%0.11lu:%0.8lu 360000000", req->mobile, req->app);
					//ERROR("[set expire time]%s", setcmd);
					redis_pool.exec_command(setcmd);
					
					sprintf(log,
						"[dbchange]%llu|%llu|%llu|%llu|%llu|%u|%u|%d",
						req->mobile,
						req->app,
						req->traffic,
						balance,
						remain,
						bt->tv_sec,
						et->tv_sec,
						r3.reply_->integer);
					
					ERROR("%s", log);
					
					SendMsg2Tms(fd, (U8*)&rsp, sizeof(TrafficReportRsp));
					delete pmsg;
					
					return;
				}
				;
			}
			break;
		case KEEP_ALIVE_TOKEN:
			{
				go[=]
				{
					KeepAliveToken* req = (KeepAliveToken*)pmsg->rz_buf;
					char setcmd[1024];
					int i = 0, p = 0;

					for (; i < 16 && req->mobile[i]>0; i++)
					{
						p += sprintf(setcmd + p, "expire auth:%0.11lu:%0.8lu 360000000", req->mobile[i], req->app[i]);
					}
					if (i > 0)
					{
						vector<string> v;
						redis_pool.exec_command_pipeline(v,i,setcmd);
					}
				}
				;
			}
		}
		
	}
}

int NetInit()
{
	int length;
	int ret;
	int currfd, i;
	int listen_fd;

	struct sockaddr_in addr_from_in[1];
	addr_from_in[0].sin_family = AF_INET;
	addr_from_in[0].sin_port = htons(TcpRcvPort);
	addr_from_in[0].sin_addr.s_addr = htonl(INADDR_ANY);
	memset(&addr_from_in[0].sin_zero, 0, sizeof addr_from_in[0].sin_zero);

	//create receive socket
	if((listen_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
	{
		ERROR("socket create ret code %d, err %d", listen_fd, errno);
		return -1;
	}

	int on = 1;
	setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
	
	ret = bind(listen_fd, (struct sockaddr *)&addr_from_in[0], sizeof addr_from_in[0]);
	if (ret < 0) {
		ERROR("socket bind ret code %d, err %d", ret, errno);
		return -1;
	}

	if (listen(listen_fd, 3) < 0) {
		perror("listen");
		return -1;
	}
	
	ERROR("listen on port %d, fd = %d", TcpRcvPort, listen_fd);
	int newfd;
	while (1)
	{
		newfd = accept(listen_fd, NULL, NULL);
		if (newfd <= 0)
		{
			ERROR("fail to accept new peer");
			continue;
		}
		ERROR("new connection established fd=%d", newfd);
		go[=] {NetRead(newfd);}
		;
	}
	return 0;
}

