#ifndef CONN_MGR
#define CONN_MGR

#include <sys/epoll.h>
#include <sys/time.h>
#include <fcntl.h>
#include <map>
#include <vector>
#include <memory>
//#include "TMSDataTypes.h"
#include "TMSMessage.h"

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
	/*sdk*/
};

enum Sock_stat
{
	sta_offline    = 0,
	sta_online     = 1,
	sta_inprogress = 2,
	sta_hangup     = 3,
};

class Connect
{
public:
	Connect() {}
	;
	~Connect();
	U64  sock_fd = 0;
	U8   status = 0; 		//0-offline 1-online 2-connecting
	U8   type = 0;  			//0-tunnel 1-agent 2-dns
	U8   exit = 0;
	U64  tmsid = 0;

	enum Trans_stat cstate = s_header;
	hdr_t h;
	U16 stillneedtoread = 0;
	U16 offset = 0;
	U16 datalen = 0;
	U8  buff[8192];

	int Reset();
	int StreamRecvEvent();
	int HandleRecvMsg(U8* msg, U32 msglen);
	int SendMsg(U8* msg, U32 msglen);
	int SendTmsMsg(U8* msg, U32 msglen);
};

class ConnectMgr
{
public:
	int _Epoll_FD = -1;
	std::map<U64, Connect*> _ConnMap;
	std::vector<Connect*> _CenterConnPool;
	std::vector<Connect*> _TmsConnPool;
	//std::map<U64, Connect*> _AgentMap;
	int Establish(U64 fd);
	int HandleEPOEvent(U64 fd);
	int HandleAcceptEvent(U64 fd);
	int RemoveConn(long long fd);
	Connect* CreateTmsConn(U64& socket_fd, U64 ip, U16 port);
	Connect* CreateCenterConn(U64& socket_fd, U64 ip, U16 port);
	Connect* GetConn(U64 fd);
	Connect* GetCenterConn() {return _CenterConnPool[0];}
	Connect* GetTmsConn()    {return _TmsConnPool[0];}
	Connect* CreateDpiConn(U64& socket_fd, U64 ip, U16 port);
	
	int AddEvent(long epoll_fd, long fd, U64 event = EPOLLIN | EPOLLOUT | EPOLLET);
	int RemoveEvent(long epoll_fd, long fd, U64 event = EPOLLIN | EPOLLOUT | EPOLLET);
	int ModifyEvent(long epoll_fd, long fd, U64 event = EPOLLIN | EPOLLOUT | EPOLLET);
	
	int HandleRecvEvent(U64 fd);
	
	static ConnectMgr& Instance()
	{
		static ConnectMgr t;
		return t;
	}
};

#endif
