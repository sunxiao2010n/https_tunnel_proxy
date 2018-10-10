#include <unistd.h>
#include <memory.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <iostream>
#include <string>
#include <algorithm>

#include "LvldbMgr.h"
#include "ConnectMgr.h"
#include "TMSMessage.h"

#include "leveldb/db.h"
#include "leveldb/cache.h"
#include "leveldb/slice.h"

static ConnectMgr& cm = ConnectMgr::Instance();
static LvldbMgr&  lm = LvldbMgr::Instance();

int Connect::Reset()
{

	//	if (sock_fd > 0)
	//	{
	//		cm.RemoveEvent(cm._Epoll_FD, sock_fd, EPOLLIN|EPOLLOUT);
	//	}
	stillneedtoread = 0;
	cstate = s_header;
	status = sta_offline;
	type = 0;  			//0-tunnel 1-agent

	U16 offset = 0;
	U16 datalen = 0;
	sock_fd = -1;

	return 0;
}
	
Connect::~Connect()
{
	_Pr("connection % d destroyed", sock_fd);
	if (sock_fd > 0)
	{
		cm.RemoveEvent(cm._Epoll_FD, sock_fd, EPOLLIN | EPOLLOUT | EPOLLET);
		close(sock_fd);
	}
}

//
//https://stackoverflow.com/questions/5103282/are-repeated-recv-calls-expensive
//
int Connect::StreamRecvEvent()
{
	int ret;
	int fd = sock_fd;
	int length;
	while ((length = read(fd, buff + offset, stillneedtoread)) > 0)
	{
	
		if (length > 0)
		{
			if (cstate != s_forward && length < stillneedtoread) {
				offset += length;
				stillneedtoread -= length;
				//goto NextEvent;
				return - 2;
			}
			switch (cstate) {
			case s_header:
				{
					memcpy(&h, buff, sizeof(hdr_t));
					if (h.delim0 != VALIDATER0 || h.delim1 != VALIDATER1) {
						_Pr("error packet, VALIDATER0=%d, VALIDATER1 = %d, fd=%d", h.delim0, h.delim1, fd);
						//goto REMOVE_EVENT;
						return - 1;
					}
					h.len = ntohs(h.len);
					cstate = s_body;
					offset = 0;
					stillneedtoread = h.len;
				}
				break;
			case s_body:
				{
					//_Pr("run on receive");
					cstate = s_header;
					stillneedtoread = sizeof(hdr_t);
					offset = 0;
					//handle message
					HandleRecvMsg(buff, h.len);
				}
				break;
			}
		}
	}
	return length;
}

int Connect::HandleRecvMsg(U8* msg, U32 msglen)
{
	int ret = 0;
	if (msglen < 4)
	{
		std::cout << "HandleRecvMsg Error:msglen exception" << std::endl;
		return -1;
	}
	int msg_id = ((MessageID*)msg)->msg_id;
	std::cout << "HandleRecvMsg msgid=" << msg_id << " msglen=" << msglen << std::endl;
	switch (msg_id)
	{
	case SYN_USER_TUPLE:
		{
			SynUserTuple *u = (SynUserTuple*)msg;
			_Pr("Handle Msg Syn User Tuple");
			
			lm.OnUserLogin(u);
		}
		break;
	case TRAFFIC_REPORT_RSP:
		{
			TrafficReportRsp *r = (TrafficReportRsp*)msg;
			_Pr("Handle Msg TrafficReportRsp");
			if (r->rsp_code != 0)
				_Pr("incorrect respcode of TrafficReportRsp");

			lm.OnTrafficReportReply(r->ip);
		}
	}

	return ret;
}

int Connect::SendMsg(U8* msg, U32 msglen)
{
	int sentlen = send(sock_fd, msg, msglen, MSG_NOSIGNAL);
	if (sentlen != msglen) 
		_Pr("SendMsg error = %d, sentlen=%d, msglen=%d", errno, sentlen, msglen);

	return sentlen;
}

int Connect::SendTmsMsg(U8* msg, U32 msglen)
{
	hdr_t hdr;
	hdr.len = htons(msglen);
	static U8 tmsMsg[1024];
	memcpy(tmsMsg, &hdr, sizeof hdr);
	memcpy(tmsMsg + sizeof hdr, msg, msglen);
	int to_send_len = sizeof(hdr) + msglen;
	int rec_ret;

	int sentlen = send(sock_fd, tmsMsg, to_send_len, MSG_NOSIGNAL);
	
	if (sentlen != to_send_len)
	{
		_Pr("SendTmsMsg error = %d, sentlen=%d, msglen=%d", errno, sentlen, msglen);
		return sentlen;
	}

	return sentlen;
}

int ConnectMgr::HandleAcceptEvent(U64 listen_fd)
{
	int newfd = accept(listen_fd, NULL, NULL);
	if (newfd > 0) {
		
		int nSndBuf = 1024 * 1024;   //设置为1M
		setsockopt(newfd, SOL_SOCKET, SO_SNDBUF, (const char*)&nSndBuf, sizeof(int));
		
		Connect* p;
		auto iter = _ConnMap.find(newfd);
		if (iter != _ConnMap.end())
		{
			p = iter->second;
			p->Reset();
			_Pr("reuse of connection object");
		}
		else 
		{
			do {
				p = new Connect;
			} while (p == nullptr);
		}

		p->sock_fd = newfd;
		_ConnMap[newfd] = p;
		
		//向dpi发起的连接全部视为tms，如果有变更再改动
		p->type = t_tms;

		p->stillneedtoread = sizeof(hdr_t);
		_Pr("new connection , fd = %d, type = %d", newfd, p->type);
	}
	return newfd;
}

//隧道收到数据
//@retval
int ConnectMgr::HandleRecvEvent(U64 fd)
{
	int ret;
	
	auto iter = _ConnMap.find(fd);
	if (iter == _ConnMap.end())
	{
		return -2; //问题x
	}
	Connect* c = iter->second;

	ret = c->StreamRecvEvent();	
	
	if (ret != -1 && c->exit)
	{
		cm.RemoveConn(c->sock_fd);
	}
	
	return ret;
}

//
int ConnectMgr::HandleEPOEvent(U64 fd)
{
	return 0;
}

Connect* ConnectMgr::CreateTmsConn(U64& socket_fd, U64 ip, U16 port)
{
	Connect* conn = nullptr;
	int ret = 0;
	int s;
	struct sockaddr_in addr_to_in;
	addr_to_in.sin_family = AF_INET;
	addr_to_in.sin_port = htons(port);
	addr_to_in.sin_addr.s_addr = ip;

	s = socket(AF_INET, SOCK_STREAM, 0);
	if (s <= 0) {
		_Pr("dpi connection stream : create err %d", errno);
		return conn;
	}

	ret = connect(s, const_cast<sockaddr*>((sockaddr*)&addr_to_in), sizeof(struct sockaddr_in));
	if (ret < 0) {
		printf("dpi connection stream : ret code %d,err %d", ret, errno);
		close(s);
		return conn;
	}
	
	if (ret == 0) {
		Connect* p;
		do {
			p = new Connect;
		} while (p == nullptr);

		if (_ConnMap.count(s) > 0)
		{
			//close stream
		}
		_ConnMap[s] = p;
		_TmsConnPool.push_back(p);

		p->sock_fd = s;
		p->type = t_dpi;
		p->cstate = s_header;
		p->datalen = 0;
		p->stillneedtoread = sizeof(hdr_t);

		socket_fd = s;
		nonblocking(s);
		conn = p;
		
		_Pr("dpi connection established fd=%d", s);
	}
	return conn;
}

Connect* ConnectMgr::GetConn(U64 fd)
{
	Connect *p = nullptr;
	auto iter = _ConnMap.find(fd);
	if (iter != _ConnMap.end())
	{
		p = iter->second;
	}
	return p;
}

int ConnectMgr::AddEvent(long epoll_fd, long fd, U64 event)
{
	if (!epoll_fd || !fd || !event)
	{
		_Pr("AddEvent parament error");
		return -1;
	}
	struct epoll_event ev;
	ev.events = event;
	ev.data.fd = fd;

	if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &ev) == -1) {
		_Pr("epoll add error, err[%d]", errno);
		return -1;
	}
	return 0;
}

int ConnectMgr::RemoveEvent(long epoll_fd, long fd, U64 event)
{
	if (!epoll_fd || !fd || !event)
	{
		_Pr("RemoveEvent parament error");
		return -1;
	}
	struct epoll_event ev;
	ev.events = event;
	ev.data.fd = fd;

	if (epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, &ev) == -1) {
		_Pr("epoll remove error, fd=%d, err=%d", fd, errno);
		return -1;
	}
	return 0;
}

int ConnectMgr::ModifyEvent(long epoll_fd, long fd, U64 event)
{
	if (!epoll_fd || !fd || !event)
	{
		_Pr("RemoveEvent parament error");
		return -1;
	}
	struct epoll_event ev;
	ev.events = event;
	ev.data.fd = fd;

	if (epoll_ctl(epoll_fd, EPOLL_CTL_MOD, fd, &ev) == -1) {
		_Pr("epoll remove error, err[%d]", errno);
		return -1;
	}
	return 0;
}

int ConnectMgr::RemoveConn(long long fd)
{
	if (fd < 0)
		return -1;
	
	auto iter = _ConnMap.find(fd);
	if (iter != _ConnMap.end())
	{
		Connect* c = iter->second;
		if (c->type == t_center)
		{
			_Pr("center connection down, would you please restart dpi");
			auto f = std::find(_CenterConnPool.begin(), _CenterConnPool.end(), c);
			if (f != _CenterConnPool.end())
				_CenterConnPool.erase(f);
			
			// reconnect after a few seconds 
		}
		else if (c->type == t_tms)
		{
			_Pr("tms connection down, tms id %llu", c->tmsid);
			_Pr("如有需要请在这里完善重连");
		}

		delete c;
		_ConnMap.erase(fd);
	}
	return 0;
}

//创建Center连接
Connect* ConnectMgr::CreateCenterConn(U64& socket_fd, U64 ip, U16 port)
{
	Connect* conn = nullptr;
	int ret = 0;
	int s;
	struct sockaddr_in addr_to_in;
	addr_to_in.sin_family = AF_INET;
	addr_to_in.sin_port = htons(port);
	addr_to_in.sin_addr.s_addr = ip;

	s = socket(AF_INET, SOCK_STREAM, 0);
	if (s <= 0) {
		_Pr("center connect : tcp create err %d", errno);
		return conn;
	}

	ret = connect(s, const_cast<sockaddr*>((sockaddr*)&addr_to_in), sizeof(struct sockaddr_in));
	if (ret < 0) {
		printf("center connect : tcp connect ret code %d,err %d", ret, errno);
		close(s);
		return conn;
	}
	
	if (ret == 0) {
		Connect* p;
		do {
			p = new Connect;
		} while (p == nullptr);

		if (_ConnMap.count(s) > 0)
		{
			//close stream
		}
		_ConnMap[s] = p;
		_CenterConnPool.push_back(p);

		p->sock_fd = s;
		p->type = t_center;
		p->cstate = s_header;
		p->datalen = 0;
		p->stillneedtoread = sizeof(hdr_t);

		socket_fd = s;
		nonblocking(s);
		conn = p;
		
		_Pr("center connection established fd=%d", s);
	}
	return conn;
}