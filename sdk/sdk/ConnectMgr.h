#pragma once
#ifndef CONNECT_MGR
#define CONNECT_MGR

#ifdef __APPLE__
#include <sys/event.h>
#else
#include <sys/epoll.h>
#endif
#include <fcntl.h>
#include <string>
#include <map>
#include <vector>
#include <list>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>

#include "YMEDataTypes.h"
#include "YMEMessage.h"

#define nonblocking(s)  fcntl(s, F_SETFL, fcntl(s, F_GETFL) | O_NONBLOCK)
#define blocking(s)     fcntl(s, F_SETFL, fcntl(s, F_GETFL) & ~O_NONBLOCK)

enum Trans_stat {
	s_header,
	s_body,
	s_forward,
	s_wait,
};

enum Conn_type
{
	t_tunnel = 0,
	t_agent  = 1,
	t_dns    = 2,
	t_center = 3,
	t_proxy  = 4,
	t_dpi    = 5,
	t_tms    = 6,
	t_dsp    = 101,
};

enum Sock_stat
{
	sta_offline    = 0,
	sta_online     = 1,
	sta_inprogress = 2,
	sta_hangup     = 3,
	sta_disconnected = 4,
};

enum Yme_event
{
	YM_EV_RD     = 1,
	YM_EV_WR     = 2,
	YM_EV_EX     = 4,
	YM_EV_ONCE   = 8,
};

class User;
struct ConnRecycleBlock;

class Connect
{
public:
	Connect() {};
	virtual ~Connect();
	U64  sock_fd = 0;
	U8   status = 0;		//0-offline 1-online 2-connecting 3-disconnected
	U8   type = 0;			//0-tunnel 1-agent 2-dns ...t_proxy 101-dsp
	U8   verified = 0;
	U8   http_version = 0;  //0-http, 1-https
	Connect* peer = nullptr;

	U64	 forward_id = 0;
	U64	 http_transmit_len = 0;
	U64  aval_traffic = 0;
	
	U64  uid = 0;
	U64  mobile = 0;
	U8   dsp_uid[32];
	U8   dsp_pass[32];
	U8   auth[32];
	int  login_code=0;
	enum Trans_stat cstate = s_header;
	hdr_t h;
	U32 stillneedtoread = 0;

	std::string	host;			
	U16 web_port = 80;

	U32 s_offset = 0;
	U32 r_offset = 0;
	U32 s_data_len = 0;			//u
	U32 r_data_len = 0; 		//u
	U8	s_buff[4096]; 			//proxy received
	U8  r_buff[4096] = { 0 };	//tms client received
	callback_handler callback = nullptr;

	std::list<std::string> http_reply_datalist;
	std::string http_proxy_req_cache;
	std::mutex mutex;
	std::condition_variable condition;
	
	std::list<ConnRecycleBlock*>::iterator crb_it;

	int WakeupUser();
	int WaitReply(int ms=0);
	int AckUserFail(int failcode);

	int Reset();
	int StreamRecvEvent();
	int ProxyRecvEvent();
	int HandleDspMsg(U8* msg, U32 msglen);
	
	int DnsRecvEvent();
	int HandleRecvMsg(U8* msg, U32 msglen);
	int SendMsg(U8* msg, U32 msglen);
	int SendTmsMsg(U8* msg, U32 msglen);

	int HandleProxyProtocol(U8* msg, U32 msglen);
	int HandleRecvHttp(U8* msg, U32 msglen);
	int DetactTlsClientHello(char* start, size_t packet_len, char* host, size_t* hostlen);
	//the following are used for http reply parse
	void http_reset() {
		mf = 0;header = 1;keepalive = 0;
		chunked = 0;content_length = 0;need_length = 0;rsp_code = 0;
	};
	void SwitchConnModeForw() {
		cstate = s_forward;
		stillneedtoread = sizeof(r_buff);
		r_offset = 0;
		http_reset();
		http_transmit_len += Traffic_TCP_hss();
	};
	int mf = 0;
	int header = 1;
	int keepalive = 0;
	int chunked = 0;
	int content_length = 0;
	int need_length = 0;
	int rsp_code = 0;
};

class ConnectMgr
{
public:
	int _Reactor_FD = -1;
	int _ConnCount = 0;
	std::thread::id _user_thread;
	long long accept_begin_time;
	int time_window = 0;
	
	std::map<U64, Connect*> _ConnMap;
	std::list<Connect*> _RecycleList;

	int HandleAcceptEvent(U64 fd);
	int RemoveConnect(long long fd);

	Connect* CreateConn(U64& socket_fd, U64 ip, U16 port = 80);
	int AddEvent(long reactor_fd, long fd, int event);
	int AddREvent(long epoll_fd,long fd);
	int RemoveEvent(long reactor_fd, long fd, int rmevents = YM_EV_RD);
	int ModifyEvent(long reactor_fd, long fd, int event);
	int HandleRecvEvent(U64 fd);
	int HandleEPOEvent(U64 fd);


	static ConnectMgr& Instance()
	{
		static ConnectMgr t;
		return t;
	}
};

#endif
