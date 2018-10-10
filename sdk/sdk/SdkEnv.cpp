#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <memory.h>
#include <stdarg.h>
#include <stdlib.h>
#include <sys/time.h>
#include <thread>
#include <string>
#include <tuple>

#include "YMESdk.h"
#include "ConnectMgr.h"
#include "YMEUtils.h"

/*
	To test the library, include "sdk.h" from an application project.
	
	Do not forget to add the library to Project Dependencies in Visual Studio.
*/

//<== sdk
static int sdk_running = 0;    //0-off  1-on
std::thread sdk_thread;

//<== tms client pool
struct RecycledClient
{
	YME_HANDLE handle_;
	std::string reply_;
};
static std::map<std::string, RecycledClient> ConnPool;

//<== global variables
U64 g_aval_traffic = 0;
volatile U64 g_rpt_traff_cost = 0;
std::mutex rw_lock;
U64 g_phone = 0;
U64 g_appid = 0;
U64 g_tmsport = 0;
std::string g_tmsip;
std::string g_auth;
std::string g_error;
int pipe_fd[2];

extern "C" U64 YMERptTraffCost(U64* cost)
{
	rw_lock.lock();
	U64 rpt = g_rpt_traff_cost;
	g_rpt_traff_cost = 0;
	rw_lock.unlock();
	*cost = rpt;
	return rpt;
}

extern "C" char* YMEGet(char* key)
{
	static char tmp[32];
	static char sz_traffic[32];
	static char sz_appid[32];
	static char sz_phone[32];
	static char sz_tmsport[8];
	if (!key)
	{
		tmp[0] = '\0';
		return nullptr;
	}
	if (key[0] == 'T' || key[0] == 't')
	{
		if (0 == strncasecmp(key + 1, "raffic", 6))
		{
			sprintf(sz_traffic, "%llu", g_aval_traffic);
			return sz_traffic;
		}
	}
	else if (key[0] == 'A' || key[0] == 'a')
	{
		if (0 == strncasecmp(key + 1, "uth", 3))
		{
			return (char*)g_auth.c_str();
		}
		if (0 == strncasecmp(key + 1, "ppid", 4))
		{
			sprintf(sz_appid, "%llu", g_appid);
			return sz_appid;
		}
	}
	if (key[0] == 'E' || key[0] == 'e')
	{
		if (0 == strncasecmp(key + 1, "rror", 4))
		{
			return (char*)g_error.c_str();
		}
	}
	else if (key[0] == 'P' || key[0] == 'p')
	{
		if (0 == strncasecmp(key + 1, "hone", 4))
		{
			sprintf(sz_phone, "%llu", g_phone);
			return sz_phone;
		}
	}
	else if (key[0] == 'T' || key[0] == 't')
	{
		if (0 == strncmp(key + 1, "msip", 4))
		{
			return (char*)g_tmsip.c_str();
		}
		if (0 == strncmp(key + 1, "msport", 6))
		{
			sprintf(sz_tmsport, "%llu", g_tmsport);
			return sz_tmsport;
		}
	}
	return nullptr;
}
int YMESet(char* k, char* v)
{
	if (k[0] == 'P' || k[0] == 'p')
	{
		if (0 == strncmp(k + 1, "hone", 4))
		{
			g_phone = atoll(v);
			return 0;
		}
	}
	else if(k[0] == 'A' || k[0] == 'a')
	{
		if (0 == strncmp(k + 1, "ppid", 4))
		{
			g_appid = atoll(v);
			return 0;
		}
		if (0 == strncmp(k + 1, "uth", 3))
		{
			g_auth = v;
			return 0;
		}
	}
	if (k[0] == 'E' || k[0] == 'e')
	{
		if (0 == strncmp(k + 1, "rror", 4))
		{
			g_error = v;
			return 0;
		}
	}
	if (k[0] == 'T' || k[0] == 't')
	{
		if (0 == strncasecmp(k + 1, "msip", 4))
		{
			g_tmsip = v;
			return 0;
		}
		else if(0 == strncasecmp(k + 1, "msport", 6))
		{
			g_tmsport = atoll(v);
			return 0;
		}
	}
	return -1;
}
static U64 Connfd;
/*std::string YMEGetParameter(std::string key)
{
	static std::map<std::string, int> ParameterKv = { { "Traffic", 1 }, { "Socket", 2 } };
	int keyId = ParameterKv[key];
	char tmp[32] = { 0 };
	switch (keyId)
	{
	case 1 : 
		{
			sprintf(tmp, "%llu", g_aval_traffic);
			return std::move(std::string(tmp));
		}
		break;
	case 2 : 
		{
			sprintf(tmp, "%llu", Connfd);
			return tmp;
		}
		;
	}
	return "";
}
*/
static ConnectMgr& cm = ConnectMgr::Instance();

extern "C" int YMEConnectable()
{
	//record current time
	timeval time;
	gettimeofday(&time, nullptr);
	cm.accept_begin_time = time.tv_sec * 1000000 + time.tv_usec;
	cm.time_window = 1;
	return 0;
}

extern "C" int YMERunning()
{
	return sdk_running;
}

extern "C" int YMEInit()
{
	if (-1 == pipe(pipe_fd))
	{
		YMESet("error", "init error");
		return -1;
	}

	return SdkEventCenterInit();
}

extern "C" int YMEStart()
{
	if (sdk_running)
	{
		_Pr("sdk already running");
		return -1;
	}
	
	cm._user_thread = std::this_thread::get_id();
	std::thread t1(&SdkMainLoopRun);
	sdk_thread = std::move(t1);
	
	sdk_running = 1;
	
	U8 buf;
	read(pipe_fd[0], &buf, 1);

	if (buf==0)
	{
		sdk_running = 1;
		_Pr("sdk started");
		return 0;
	}
	else
	{
		return -1;
	}
}

extern "C" int YMECleanUp()
{
	//
	//
	//交互退出
	return 0;
}

extern "C" YME_HANDLE YMELoginTms(unsigned int ip,
	short port,
	U64 app,
	U64 mobile,
	char* auth,
	char* host,
	int host_len,
	U16 web_port,
	U64* aval_traffic)
{
	U64 socket;
	//std::string req((char*)request, (size_t)req_len);
	Connect* c = cm.CreateConn(socket, ip, port);

	if (c != nullptr)
	{
		memcpy(c->auth, auth, strlen((char*)c->auth)+1); 
		c->uid = app;
		c->mobile = mobile;
		//cm.AddREvent(cm._Reactor_FD, socket);
		c->host.assign(host, host_len);
		c->web_port = 80;
		//c->callback = callback;
		//c->s_data_len = req_len;
		//memcpy(c->s_buff, request, req_len);
		
		U8 login_str[512];
		LoginReq *r = (LoginReq *)login_str;
		r->msg_id = (U16)LOGIN_REQ;
		r->mobile = mobile;
		r->app = app;
		r->port = 80;
		memcpy(r->token, auth, strlen(auth)+1);
		r->host_len = host_len;
		memcpy(r->host, host, host_len);
		
		nonblocking(c->sock_fd);
		cm.AddREvent(cm._Reactor_FD, socket);
		c->SendTmsMsg((U8*)r, sizeof(LoginReq) + host_len - 1);

		int ret = c->WaitReply();
		int loginCode = c->login_code;
		if (loginCode<0)
		{
			YMEShutDown(c->sock_fd);
			return loginCode;
		}
		
		*aval_traffic = c->aval_traffic;
		Connfd = socket;
		
		return (YME_HANDLE)socket;
	}
	return -1;
}

extern "C" int YMEFreeBuff(YME_HANDLE fd)
{
	int ret = -1;
	Connect* c = cm._ConnMap[fd];

	if (c != nullptr)
	{
		c->http_reply_datalist.pop_front();
		ret = 0;
	}
	return ret;
}

extern "C" int YMESend(YME_HANDLE fd,
	unsigned char* msg,
	size_t len,
	unsigned char** reply,
	size_t* rlen)
{
	U64 socket;
	int ret = -1;
	Connect* c = cm._ConnMap[fd];

	if (c != nullptr && c->status == sta_online)
	{
		c->http_reply_datalist.clear();
		ret = c->SendMsg((U8*)msg, len);
		ret = c->WaitReply(300000000);
//		if (ret == -1)
//			return RT_RequestExpired;
		//treat expire as succeed
		
//		ret = c->WaitReply();
		
		auto r = c->http_reply_datalist.begin();
		if (r == c->http_reply_datalist.end())
			return RT_Exception;
		*reply = (unsigned char*)r->c_str();
		*rlen = r->length();
		
		if (!ret)
		{
			//这种情况：有数据，对方已断开
			//data available, remote peer disconnected
			c->status = sta_disconnected;
			return RT_DownWithData;
		}
		else
		{
			//这种情况：有数据，对方未断开
			//data available, socket available
			return RT_Data;
		}
	}
	//失败，需要重连
	//failed
	return RT_Down;
}

extern "C" int YMEShutDown(YME_HANDLE fd)
{
	Connect* c = nullptr;
	std::map<U64, Connect*>::iterator it;
	it = cm._ConnMap.find(fd);
	if (it != cm._ConnMap.end())
	{
		c = it->second;
		delete c;
		cm._ConnMap.erase(it);
		_Pr("connection %d destroyed", fd);
		return 0;
	}

	return -1;
}

extern "C" int YMEDetach()
{
	sdk_thread.detach();
	return 0;
}

extern "C" int YMEWait()
{
	sdk_thread.join();
	return 0;
}

const char* YMEReturnCode(long return_code)
{
	static std::string retstr;
	retstr = "Return Code ";
	char tmp[32];
	sprintf(tmp, "%ld", return_code);
	retstr += tmp;
	if (return_code >= 0)
	{
		retstr += ": Success";
	}
	switch (return_code)
	{
	case -10:
		{
			retstr += ": ServerNotUp";
		}
		break;
	case -9 : 
		{
			retstr += ": HostNotExist";
		}
		break;
	case -8 : 
		{
			retstr += ": UserNotExist";
		}
		break;
	case -7 : 
		{
			retstr += ": Incorrect Token";
		}
		break;
	case -6 :
		{
			retstr += ": Insufficient Balance";
		}
		break;
	case -5 :
		{
			retstr += ": Tms Disconnected";
		}
		break;
	case -4 : 
		{
			retstr += ": Exception";
		}
		break;
	case -3 : 
		{
			retstr += ": Request Expired";
		}
		break;
	case -2 : 
		{
			retstr += ": Reply data available and connection is down";
		}
		break;
	case -1 : 
		{
			retstr += ": Connection down";
		}
		break;
	}
	return retstr.c_str();
}

extern "C" YME_HANDLE YMEReqPage(unsigned int ip,
	short port,
	U64 app,
	U64 mobile,
	char* auth,
	char* host,
	size_t host_len,
	U16 web_port,
	unsigned char* request,
	size_t req_len,
	U64* traffic_cost,
	void (*func)(void*),
	void* arglist,
	unsigned char** reply_addr,
	size_t* reply_len
	)
{
	std::string conn(host, host_len);
	int reconn = 1;
	YME_HANDLE LoginHandle;
	
	auto it = ConnPool.find(conn);
	if (it == ConnPool.end() || !it->second.handle_)
	{
REQ_RECONN:
		_Pr("creating new connection");
		reconn = 0;
		LoginHandle = YMELoginTms(ip,
			port,
			app,
			mobile,
			auth,
			host,
			host_len,
			web_port,
			&g_aval_traffic);
		
		if (LoginHandle < 0)
		{
			//if LoginHandle == RT_UserNotExist --> UserNotExist
			//	    RT_UserNotExist     = -8,
			//		RT_IncorrectToken   = -7,
			//		RT_NoBalance        = -6,
			//		RT_RemoteFinish     = -5,
			//		RT_Exception        = -4,
			//YMEDetach();
			return LoginHandle;
		}
		
		RecycledClient c;
		c.handle_ = LoginHandle;
		ConnPool[conn] = std::move(c);
	}
	else 
	{
		LoginHandle = it->second.handle_;
	}
	
	RecycledClient &rc = ConnPool[conn];
	unsigned char* reply = nullptr;
	size_t rlen = 0;
	int ret = YMESend(LoginHandle,
		request,
		req_len,
		&reply,
		&rlen) ;
	
	if (reply)
	{
		if (func)
			func(arglist);
		*reply_addr = reply;
		*reply_len = rlen;
	}

	//流量统计
	Connect* c = cm._ConnMap[LoginHandle];

	if (c != nullptr)
	{
		*traffic_cost = c->http_transmit_len;
		g_aval_traffic -= c->http_transmit_len;
		rw_lock.lock();
		//g_rpt_traff_cost = c->http_transmit_len;
		rw_lock.unlock();
		c->http_transmit_len = 0;
	}
	
	if (ret == RT_DownWithData)
	{
		//优化
		//须修改YMESend接口
		rc.reply_.assign((char*)reply,rlen);
		*reply_addr = (unsigned char*)rc.reply_.c_str();
		
		YMEShutDown(LoginHandle);
		rc.handle_ = 0;
		return 0;
	}
	else if(ret == RT_Down)
	{
		YMEShutDown(LoginHandle);
		rc.handle_ = 0;
		if (reconn != 0)
			goto REQ_RECONN;
		else
			return RT_Down;
	}
	return LoginHandle;
}
