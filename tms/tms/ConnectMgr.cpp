#include <unistd.h>
#include <memory.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <iostream>
#include <string>
#include <algorithm>

#include "ConnectMgr.h"
#include "UserMgr.h"
#include "DnsCache.h"
#include "TMSMessage.h"

static ConnectMgr& cm = ConnectMgr::Instance();
static UserMgr&    um = UserMgr::Instance();
static DnsCache&   dc = DnsCache::Instance();

int Connect::Reset()
{

	//	if (sock_fd > 0)
	//	{
	//		cm.RemoveEvent(cm._Epoll_FD, sock_fd, EPOLLIN|EPOLLOUT);
	//	}
	stillneedtoread = 0;
	cstate = s_header;
	status = sta_offline;
	sdk_verified = 0;
	agent_status = 0;
	
	type = 0; 			//0-tunnel 1-agent
	app  = 0;
	user = nullptr;
	scb  = nullptr;

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
		cm.RemoveEvent(cm._Epoll_FD, sock_fd, EPOLLIN|EPOLLOUT|EPOLLET);
		close(sock_fd);
	}

	if (scb && type == 0)
	{
		scb->tunnel_conn = nullptr;
	}
	if (scb && type == 1)
	{
		scb->agent_conn = nullptr;
	}
}

int Connect::IpOrHost(std::string& host)
{
	const char* p = host.c_str();
	int point_count = 0;
	size_t len = host.length();
	for (int l = 0; l < len; l++)
	{
		if (p[l] == '.')
			point_count++;
		else if (p[l]<'0'&&p[l]>'9')
			return -1;
	}
	if (point_count == 3)
		return 0;
	
	return -1;
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
					//charge
					HandleRecvMsg(buff, h.len);
					if (sdk_verified && user)
					{
						ret = user->RecordTraffic(scb, TrafficEst(length + sizeof(hdr_t)) );
						if (ret == -1)
							return ret;
					}
					else if (type == t_tunnel)
					{
						traffic_tmp += TrafficEst(length + sizeof(hdr_t));
					}
				}
				break;
			case s_forward:
				{
					//
					//没有考虑的地方
					//一对多的传输
					//
					
					if(!scb || !scb->tunnel_conn || !scb->agent_conn)
					{
						if (!scb)
						{
							_Pr("Error : no user");
							return 0; //断开连接
						}
						if (!scb->tunnel_conn)
						{
							_Pr("Error : no tunnel, mobile = %lu , app = %d", user->mobile, user->app);
							return 0;
						}
						if (!scb->agent_conn)
						{
							_Pr("Error : no agent, mobile = %lu , app = %d", user->mobile, user->app);
							return 0;
						}
						break;
					}
				
					int forw_fd;
					int wrote_len;
					if (type == t_agent)
					{
						//下行流量传完再计费
						forw_fd = scb->tunnel_conn->sock_fd;
						wrote_len = send(forw_fd, buff, length, MSG_NOSIGNAL);
						if (wrote_len != -1)
						{
							ret = user->RecordTraffic(scb, TrafficEst(wrote_len));
							if (ret != 0)
								return -1;
						}

						if (wrote_len != length || length == 0)
						{
							if (wrote_len == -1)
								wrote_len = 0;
							
							_Pr("download data: send buff full. wrote_len = %d, data_len = %d, fd=%d, errno = %d",
								wrote_len,length,forw_fd,errno);
							datalen = length - wrote_len;
							offset = wrote_len;
							status = sta_hangup;
							ret = cm.RemoveEvent(cm._Epoll_FD, sock_fd, EPOLLIN);
							//ret = cm.AddEvent(cm._Epoll_FD, scb->tunnel_conn->sock_fd, EPOLLOUT);
							if (ret != 0)
							{
								_Pr("RemoveEvent error");
								return -1;
							}
							return -2;
						}
						_Pr("download data");
					}
					else if (type == t_tunnel)
					{
						//收到用户的上行流量先计费
						ret = user->RecordTraffic(scb, TrafficEst(length));
						if (ret != 0)
							return -1;
						forw_fd = scb->agent_conn->sock_fd;
						_Pr("uploading data");
						wrote_len = send(forw_fd, buff, length, MSG_NOSIGNAL);
						if (wrote_len != length || length==0)
						{
							_Pr("upload data error: wrote_len = %d, data_len = %d, errno = %d", wrote_len, length, errno);
							//return 0;
						}
					}
					//
					//不知是否有异步写问题
					//
				}
			}
		}
	}
	return length;
	
}

int Connect::DnsRecvEvent()
{
	int fd = sock_fd;
	int length;
	while ((length = read(fd, buff, sizeof(buff))) > 0)
	{
		if (length > 0)
		{
			TmsDnsReplys replys;
			res_parse(buff, replys);
			
			dc.Update(replys);
			//test
			//
			//
			auto r = replys._r;
			//_Pr("dns event handled");
			//_Pr("add %d dns answers", r.size());
			for(auto &rc : r)
			{
				_Pr("add dns answer for host [%s]", rc._host.c_str());
				dc.InformClients(rc._host, rc._ip);
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
		case LOGIN_REQ:
		{
			_Pr("Handle User Login");
			LoginRsp rsp;
			LoginReq* r = (LoginReq*)msg;

			app = r->app;
			mobile = r->mobile;
			dest_port = r->port;
			memcpy(token, r->token, strlen((char*)r->token)+1);
			r->host[r->host_len] = '\0';
			curr_host.assign((char*)r->host, r->host_len);
			std::string& host = curr_host;
			
			//std::cout << "request host is [" << host << "]" << std::endl;
			//
			if(!mobile || !host.length()) {
				rsp.rsp_code = PARAMET_ERR;
				SendTmsMsg((U8*)&rsp, sizeof rsp);
			}
			
			//到ce验证
			ReadTokenReq req;
			req.cid = sock_fd;
			req.app = r->app;
			req.mobile = r->mobile;
			memcpy(req.token,r->token,sizeof token);
			Connect* cn = cm.GetCenterConn();
			
			cn->SendTmsMsg((U8*)&req, sizeof req);
			
			//重置用户
			User *u;
			SCB	 *s;
			UserId id(mobile, app);
			u = um.RetrieveUser(id);
			if (!u)
			{
				_Pr("error retrieve user: mobile:%llu,app:%llu, no heap memory",mobile,app);
				abort();
			}
			user = u;

			if (0 == IpOrHost(host))
			{
				_Pr("connnecting host %s", host.c_str());
				U64 ip_be = inet_addr(host.c_str());
				s = user->ConnectWebServer(this, ntohl(ip_be), (short)r->port);
				if (!s)
					return -1;
			}
			else
			{
				//开始建立web连接，先查询dns缓存
				if(host.length() == 0)
				{
					_Pr("message error: host length 0");
				}
				auto iter = dc._Cache.find(host);
				if (iter != dc._Cache.end())
				{
					//找到了ip
					//
					//从连接池获取连接
					//
					_Pr("Dns cache exists, now fetching one connection");
					U64 ip = iter->second;
					if (!user)
						_Pr("error user not exist");
					else
					{
						s = user->ConnectWebServer(this, ip, (short)r->port);
						if (!s)
							return -1;
					}

				}
				else
				{
					//未找到host，发送查询请求
					U8 dns_req_buff[512];
					int len = req_fill((U8*)dns_req_buff, (const U8*)host.c_str(), host.length());
					
					//创建stream
					s = user->CreateSCB(this, nullptr);
					
					//把自己添加到wait list中
					_Pr("user %llu|%llu waiting dns reply", user->mobile, user->app);
					dc.AddWaitLists(host, s);
					
					//发出dns查询
					Connect *d = cm.GetDnsConn();
					d->SendMsg(dns_req_buff, len);
				}
			}
			rsp.rsp_code = PROG;   //正在登陆
			SendTmsMsg((U8*)&rsp,sizeof rsp);
		}
		break;
		case CONNECT_HOST_REQ:
		{
			//			std::cout << "Handle User Specify host" << std::endl;
			//			if (!app)
			//			{
			//				std::cout << "No user id" << std::endl;
			//				//return -1;
			//				//关闭连接
			//				//
			//				//
			//			}
			//			//分析host
			//			ConnectHostReq* h = (ConnectHostReq*)msg;
			//			curr_host.assign((char*)h->host, h->host_len);
			//			std::string& host = curr_host;
			//			std::cout << "request host is [" << host << "]" << std::endl;
			//			//
			//			
			//			//
			//			if(host.length() == 0)
			//			{
			//				_Pr("message error: host length 0");
			//			}
			//			//todo这里判断和返回host异常
			//			//
			//
			//			//查询dns缓存
			//			auto iter = dc._Cache.find(host);
			//			if (iter != dc._Cache.end())
			//			{
			//				//找到了ip
			//				//
			//				//从连接池获取连接
			//				//
			//				_Pr("Dns cache exists, now fetching one connection");
			//				U64 ip = iter->second;
			//				if(!user)
			//					_Pr("error user not exist");
			//				else 
			//					user->ConnectWebServer(this,ip,h->port);
			//			}
			//			else 
			//			{
			//				//未找到host，发送查询请求
			//				U8 dns_req_buff[512];
			//				int len = req_fill((U8*)dns_req_buff,(const U8*)host.c_str(),host.length());
			//				
			//				//把自己添加到wait list中
			//				_Pr("user %llu waiting dns reply",(unsigned long long)user->app);
			//				UserId id(mobile, app);
			//				dc.AddWaitList(host,id);
			//				//发出dns查询
			//				Connect *d = cm.GetDnsConn();
			//				d->SendMsg(dns_req_buff, len);
			//			}
			_Pr("Command [%d] no more supported", CONNECT_HOST_REQ);
		}
		break;
		case BEGIN_TRANSMIT:
		{
			std::cout << "Handle User Transmit" << std::endl;
			//验证隧道号
			//user->agent_conn
			BeginTransmit *t = (BeginTransmit *)msg;
			if (!scb)
			{
				_Pr("illegal command sequence");
				return -1;
			}
			if(scb->forward_id==t->forward_id && scb->agent_conn)
			{
				//
				_Pr("forward id right, switch conn mode [forward]");
				SwitchConnModeForw();
				
				break;
			}
			//返回失败码

		}
		break;
		case READ_TOKEN_RSP:
		{
			//勿踩坑：这里的this是center连接
			_Pr("Handle Msg ReadTokenRsp");
			ReadTokenRsp* r = (ReadTokenRsp*)msg;
			int cid = r->cid;
			
			Connect* client = cm.GetConn(cid);
			if (!client)
			{
				_Pr("read token replied but connection not found，cid=%d",cid);
				break;
			}
			
			LoginRsp rsp;

			if (r->rsp_code != 0 || strcmp((char*)client->token, (char*)r->token))
			{
				//验证失败，断开连接
				_Pr("user %llu|%llu login check, ret code = %d, connection will be closed"
					, r->mobile, r->app, r->rsp_code);
				rsp.rsp_code = r->rsp_code;
				client->SendTmsMsg((U8*)&rsp, sizeof rsp);

				client->exit = 1;
				cm.RemoveConn(client->sock_fd);
				break;
			}
			else
			{
				client->sdk_verified = 1;
				if (!client->scb)
				{
					//client->exit = 1;
					_Pr("err: client has no stream control block");
					cm.RemoveConn(client->sock_fd);
					break;
				}
				client->scb->status = u_validated;
				_Pr("user %llu|%llu login success", r->mobile,r->app);
				
				client->user->balance = r->balance;
				rsp.avalable_traffic = r->balance;
				//client->user->traffic += client->traffic_tmp;
				client->scb->traffic += client->traffic_tmp;
				client->traffic_tmp = 0;
				
				if (client->scb->connected)
				{
					//到这里所有的准备工作已经就绪，等待接收消息5来转发了
					rsp.rsp_code = READY;
					rsp.forward_id = client->scb->forward_id;
					client->SendTmsMsg((U8*)&rsp, sizeof rsp);
				}
				else 
				{
					rsp.rsp_code = TOKEN_VERIFIED;
					client->SendTmsMsg((U8*)&rsp, sizeof rsp);
				}
				
				SynUserTuple r;
				r.app = client->app;
				r.mobile = client->mobile;
				
				static struct sockaddr_in sa;
				socklen_t len = sizeof(sa);
				if (0 == getpeername(client->sock_fd, (struct sockaddr *)&sa, &len))
				{
					r.src_ip_be = sa.sin_addr.s_addr;
					r.src_port_be = sa.sin_port;
				}
				else
				{
					//异常了...可是os不会异常
					_Pr("fail getpeername, fd=%lu, %llu|%llu", client->sock_fd, r.mobile, r.app);
				}
				
				Connect* c = cm.GetDpiConn();
				if (!c)
				{
					_Pr("Sorry, but dpi's not online");
					break;
				}
				c->SendTmsMsg((U8*)&r, sizeof r);
			}
		}
		break;
	case TRAFFIC_REPORT_RSP:
		{
			//勿踩坑：这里的this是center连接
			_Pr("Handle Msg Traffic Report Rsp");
			TrafficReportRsp* r = (TrafficReportRsp*)msg;
			UserId uid(r->mobile,r->app);
			
			auto u = um._UserMap.find(uid);
			if (u == um._UserMap.end())
			{
				_Pr("traffic rsp user not exist");
				//
				//日志
				//_Info("");
			}
			else
			{
				User *user = u->second;
				if (user->traffic)
				{
					user->ReportAccumulated();
					user->reporting = 1;
				}
				else
				{
					user->reporting = 0;
				}
			}
		}
		break;
	}

	return ret;
}

int Connect::SendMsg(U8* msg, U32 msglen)
{
	int sentlen = send(sock_fd, msg, msglen, MSG_NOSIGNAL);
	if (sentlen != msglen) 
		_Pr("SendMsg error = %d, sentlen=%d, msglen=%d", errno,sentlen,msglen);

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
		_Pr("SendTmsMsg error = %d, sentlen=%d, msglen=%d", errno,sentlen,msglen);
		return sentlen;
	}

	if (type == t_tunnel)
	{
		if (!user)
		{
			_Pr("traffic record error: no user");
		}
		else
		{
			rec_ret = user->RecordTraffic(scb, TrafficEst(sentlen));
			if (rec_ret != 0 || !scb)
			{
				_Pr("traffic record error: insufficient balance or no scb");
				exit = 1;
				return -1;
			}
		}
	}
	return sentlen;
}

int ConnectMgr::HandleAcceptEvent(U64 listen_fd)
{
	int newfd = accept(listen_fd, NULL, NULL);
	if (newfd>0) {
		
		int nSndBuf = 16 * 1024;  //设置为1M
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

		p->stillneedtoread = sizeof(hdr_t);
		gettimeofday(&p->begin_time,NULL);

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

	if (c->type == t_dns)
	{
		ret = c->DnsRecvEvent();
	}
	else
	{
		ret = c->StreamRecvEvent();
	}
	
	if (ret != -1 && c->exit)
	{
		cm.RemoveConn(c->sock_fd);
	}
	
	return ret;
}

//
//此函数处理两类事件：连接建立、下载文件的发送缓冲区由满变为可用
int ConnectMgr::HandleEPOEvent(U64 fd)
{
	int ret;
	std::map<U64, Connect*>::iterator it
		= _ConnMap.find(fd);
	if (it == _ConnMap.end())
	{
		return -2;  //问题x
	}
	
	Connect* c = it->second;
	if (c->status == sta_inprogress)
	{
		//establish new connection
		c->status = sta_online;
		c->agent_status = 1;
		
		LoginRsp rsp;
		if (c->scb && c->scb->tunnel_conn)
		{
			c->scb->tunnel_conn->agent_status = 1;
			c->scb->forward_id = um.NewForwardId();
			c->scb->connected = 1;
			rsp.forward_id = c->scb->forward_id;
			
			if (c->scb->status)
			{
				c->status = s_forward;
				rsp.rsp_code = READY;
				rsp.forward_id = c->scb->forward_id;
				rsp.avalable_traffic = c->user->balance;
				c->scb->tunnel_conn->SendTmsMsg((U8*)&rsp, sizeof rsp);
				_Pr("web server connected, forward ready");

			}
			else
			{
				//
				//  todo:
				//    ack user
				//    that he is wait for authentication
				rsp.rsp_code = AGENT_ESTASBLISHED;
				c->scb->tunnel_conn->SendTmsMsg((U8*)&rsp, sizeof rsp);
				_Pr("web server connected, waiting for authentication");
			}
		}
		else
		{
			_Pr("web server connected but tunnel not found");
			_ConnMap.erase(fd);
			return -1;
		}
		
		ModifyEvent(_Epoll_FD, fd, EPOLLIN|EPOLLET);

		return 0;
	}
	else
	{
		//只对转发中的连接有效
		if(c->cstate != s_forward)
			return -2;
		//only cares about tunnel socket who is writeable
		if (c->type != t_tunnel)
			return 0;
		//_Pr("fd %d is writeable", fd);
		
		//清空缓存
		Connect *ac = c->scb->agent_conn;
		if (ac->datalen == 0)
		{
			_Pr("logical exceptrion, fd [%d] has no new data to write", fd);
			return 0;
		}

		int wrotelen = send(fd, ac->buff + ac->offset, ac->datalen, MSG_NOSIGNAL);
		_Pr("continue transmit: wrotelen = %d, data offset = %d ,data len = %d", wrotelen, ac->offset, ac->datalen);
		if (wrotelen == ac->datalen)
		{
			ac->offset = 0;
			ac->stillneedtoread = sizeof(ac->buff);
			ac->status = sta_online;
			//20180403 			AddEvent(_Epoll_FD, ac->sock_fd, EPOLLIN|EPOLLET);
			AddEvent(_Epoll_FD, ac->sock_fd, EPOLLIN);
			_Pr("add r event to %d by %d", ac->sock_fd, fd);
		}
		else if(wrotelen > 0)
		{
			ac->offset += wrotelen;
			ac->datalen -= wrotelen;
			_Pr("write buff of %d is full again", fd);
		}
		
		if (wrotelen > 0)
		{
			int cret = c->user->RecordTraffic(c->scb, TrafficEst(wrotelen));
			if (cret == -1)
				return -1;
		}
		return 0;
	}
	return -1;
}


//创建dns连接
Connect* ConnectMgr::CreateDnsConn(U64& socket_fd, U64 ip, U16 port)
{
	Connect* conn = nullptr;
	int ret = 0;
	int s;
	struct sockaddr_in addr_to_in;
	addr_to_in.sin_family = AF_INET;
	addr_to_in.sin_port = htons(port);
	addr_to_in.sin_addr.s_addr = ip;

	s = socket(AF_INET, SOCK_DGRAM, 0);
	if (s <= 0) {
		_Pr("udp create err %d", errno);
		return conn;
	}

	ret = connect(s, const_cast<sockaddr*>((sockaddr*)&addr_to_in), sizeof(struct sockaddr_in));
	if (ret < 0) {
		printf("udp connect ret code %d,err %d", ret, errno);
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
		_DnsConnPool.push_back(p);

		p->sock_fd = s;
		p->type = t_dns;
		p->datalen = 0;
		p->stillneedtoread = sizeof(p->buff);

		socket_fd = s;
		nonblocking(s);
		conn = p;
		
		_Pr("dns connection established fd=%d",s);
	}
	return conn;
}

Connect* ConnectMgr::CreateDpiConn(U64& socket_fd, U64 ip, U16 port)
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
		_DpiConnPool.push_back(p);

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

//创建上行代理连接
Connect* ConnectMgr::CreateAgentConn(U64& socket_fd, U64 ip, U16 port)
{
	Connect* conn = nullptr;
	int ret = 0;
	int s;
	struct sockaddr_in addr_to_in;
	addr_to_in.sin_family = AF_INET;
	addr_to_in.sin_port = htons(port);
	addr_to_in.sin_addr.s_addr = htonl((U32)ip);
	
	//
	_Pr("connecting %s:%d", inet_ntoa(addr_to_in.sin_addr), port);

	s = socket(AF_INET, SOCK_STREAM, 0);
	if (s <= 0){
		_Pr("socket create ret code %d, err=%d", s, errno);
		return conn;
	}
	nonblocking(s);
	//int nSndBuf = 64 * 1024; //设置为
	//setsockopt(s, SOL_SOCKET, SO_SNDBUF, (const char*)&nSndBuf, sizeof(int));
	
	ret = connect(s, const_cast<sockaddr*>((sockaddr*)&addr_to_in), sizeof(struct sockaddr_in));
	if (ret<0 &&errno!=EINPROGRESS){
		printf("socket connect ret code %d, err %d\n", ret, errno);
		close(s);
		return conn;
	}

	{
		Connect* p;
		do {
			p = new Connect;
		} while (p == nullptr);

		if (_ConnMap.count(s) > 0)
		{
			//close stream
		}
		_ConnMap[s] = p;

		p->sock_fd = s;
		p->status = sta_inprogress;
		p->cstate = s_forward;
		p->type   = t_agent;
		p->datalen = 0;
		p->stillneedtoread = sizeof(p->buff);

		socket_fd = s;
		conn = p;
	}
	return conn;
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
	if (!epoll_fd||!fd ||!event)
	{
		_Pr("AddEvent parament error");
		return -1;
	}
	struct epoll_event ev;
	ev.events = event;
	ev.data.fd = fd;

	if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &ev) == -1){
		_Pr("epoll add error, err[%d]", errno);
		return -1;
	}
	return 0;
}

int ConnectMgr::RemoveEvent(long epoll_fd, long fd, U64 event)
{
	if(!epoll_fd||!fd||!event)
	{
		_Pr("RemoveEvent parament error");
		return -1;
	}
	struct epoll_event ev;
	ev.events = event;
	ev.data.fd = fd;

	if (epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, &ev) == -1){
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
		Connect* c=iter->second;
		if (c->type == t_center)
		{
			_Pr("center connection down, would you please restart tms");
			auto f = std::find(_CenterConnPool.begin(), _CenterConnPool.end(), c);
			if (f != _CenterConnPool.end())
				_CenterConnPool.erase(f);
			
			// reconnect after a few seconds 
		}
//		else if (c->type == t_dpi)
//		{
//			_Pr("center connection down, would you please restart tms");
//			auto f = std::find(_DpiConnPool.begin(), _DpiConnPool.end(), c);
//			if (f != _DpiConnPool.end())
//				_DpiConnPool.erase(f);
//		}
		else if(c->type == t_agent||c->type == t_tunnel)
		{
			// bugfix : if (c->user)
			if (c->user && c->scb)
			{
				//任意一方断开连接都进行一次流量统计
				c->user->ReportOrAccumulate(c->scb);

				if (c->type == t_tunnel)
				{
					c->scb->tunnel_conn = nullptr;
					Connect* agent = c->scb->agent_conn;
					if (agent)
					{
						_ConnMap.erase(agent->sock_fd);
						delete agent;
					}
				}

				else if (c->type == t_agent)
				{
					c->scb->agent_conn = nullptr;
					Connect* tn = c->scb->tunnel_conn;
					if (tn)
					{
						_ConnMap.erase(tn->sock_fd);
						delete tn;	
					}
				}
				
				c->user->ReleaseSCB(c->scb);
				c->user = nullptr;
			}
			else 
				_Pr("fail to report traffic");
		}

		delete c;
		_ConnMap.erase(fd);
	}
	return 0;
}


int ConnectMgr::InformShutdownUser(long long agent_fd)
{
	if (agent_fd < 0)
		return -1;
	//有没有可能tunnel_conn存在于等待队列里，同时agent_conn又挂起等待？
	//no
	auto iter = _ConnMap.find(agent_fd);
	if (iter != _ConnMap.end())
	{
		Connect* c = iter->second;
		if (c->type == t_agent)
		{
			if (c->user)
			{
				//send message
				c->user->ReportOrAccumulate(c->scb);
				_ConnMap.erase(agent_fd);
				
				auto tc = c->scb->tunnel_conn;
				if (tc)
				{
					_ConnMap.erase(tc->sock_fd);
					delete tc;
				}
				
				c->scb->agent_conn = nullptr;
				c->scb = nullptr;
				c->user = nullptr;
				return 0;
			}
		}
	}
	return -1;
}