#include <unistd.h>
#include <memory.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <string.h>
#include <stdlib.h>

#include <iostream>
#include <string>

#include "ConnectMgr.h"
#include "YMEMessage.h"
#include "YMESdk.h"
#include "YMEUtils.h"
#include "Recycler.h"

static ConnectMgr& cm = ConnectMgr::Instance();
static U8 tmp_buff[1024];
extern U64 g_aval_traffic;
extern volatile U64 g_rpt_traff_cost;
extern std::mutex rw_lock;

int Connect::WakeupUser()
{
	std::unique_lock<std::mutex> lock(mutex);
	condition.notify_one();
	return 0;
}

int Connect::AckUserFail(int failcode)
{
	static char buf[32];
	if (peer && peer->type == t_proxy)
	{
		std::string s("HTTP/1.1 200 OK\r\n");
		s += std::string("Content-Type: html/text;charset=utf-8") + "\r\n";
		s += std::string("Connection: close") + "\r\n";
		s += std::string("Location: localhost sdk") + "\r\n";

		
		std::string c("<html>\r\n");
		c += std::string("<html>\r\n");
		c += std::string("<head>") + YMEReturnCode(failcode) + "</head>\r\n";
		c += std::string("<body>") + YMEReturnCode(failcode) + "</body>\r\n";
		c += std::string("</html>\r\n\r\n");
		
//		sprintf(buf, "%lu", c.length());
//		s += std::string("Content-Length: ") + buf + "\r\n";
		s += "\r\n";
		
		s += c;
		
		write(peer->sock_fd, s.c_str(), s.length());
		
		_Pr("sent \'http fail\' response to fd=%d",peer->sock_fd);
		shutdown(peer->sock_fd, SHUT_RDWR);
//		cm._RecycleList.push_back(this->peer);
//		cm._RecycleList.push_back(this);
	}
	else 
		WakeupUser();
	return 0;
}

int Connect::WaitReply(int ms)
{
	std::unique_lock<std::mutex> lock(mutex);
	if (ms != 0)
	{
		auto cvsta = condition.wait_for(lock, std::chrono::milliseconds(ms)); 
		if (std::cv_status::timeout != cvsta && status != sta_offline)
			return 0;
		else return -1;
	}
	else
	{
		condition.wait(lock);
		if (status == sta_offline)  //连接不成功
			return -1;
		return 0;
	}
}

int Connect::Reset()
{
	stillneedtoread = 0;
	cstate = s_header;
	status = sta_offline;
	
	type = 0; 			//0-tunnel 1-agent
	uid = 0;
	sock_fd = -1;

	return 0;
}
	
Connect::~Connect()
{
	if (sock_fd > 0)
	{
		//#ifdef __APPLE__
		//#else
		cm.RemoveEvent(cm._Reactor_FD, sock_fd);
		shutdown(sock_fd, SHUT_RDWR);
		//close(sock_fd);
		_Pr("connection %llu is shut down", sock_fd);
		//#endif
	}
	if (type == t_proxy)
	{
		ConnRecycleBlock::CRBDelete(crb_it);
	}
	if (http_transmit_len > 0)
	{
		//g_rpt_traff_cost += http_transmit_len;
		http_transmit_len = 0;
	}
}

int Connect::StreamRecvEvent()
{
	int fd = sock_fd;
	int length;
	while ((length = recv(fd, r_buff + r_offset, stillneedtoread, 0)) > 0)
	{
		if (length > 0)
		{
			http_transmit_len += Traffic_Est(length);
			rw_lock.lock();
			g_rpt_traff_cost += Traffic_Est(length);
			rw_lock.unlock();
			
			//http_transmit_len = 0;
			if (cstate != s_forward && length < stillneedtoread)
			{
				r_offset += length;
				stillneedtoread -= length;
				//goto NextEvent;
				return - 2;
			}
			switch (cstate)
			{
			case s_header:
				{
					memcpy(&h, r_buff, sizeof(hdr_t));
					if (h.delim0 != VALIDATER0 || h.delim1 != VALIDATER1)
					{
						_Pr("error packet");
						//goto REMOVE_EVENT;
						return - 1;
					}
					h.len = ntohs(h.len);
					cstate = s_body;
					r_offset = 0;
					stillneedtoread = h.len;
				}
				break;
			case s_body:
				{
					//_Pr("run on receive");
					cstate = s_header;
					stillneedtoread = sizeof(hdr_t);
					r_offset = 0;
					HandleRecvMsg(r_buff, h.len);
				}
				break;
			case s_forward:
				{
					if (!peer)
					{
						//dispatch to user
						HandleRecvHttp(r_buff, length);
					}
					else 
					{
						//dispatch to proxy
						U64 forw_fd = peer->sock_fd;
						int wrote_len = write(forw_fd, r_buff, length);

						if (wrote_len != length || length == 0)
						{
							if (wrote_len == -1)
								wrote_len = 0;
								
							_Pr("download data: buff full. wrote_len = %d, data_len = %d, errno = %d", wrote_len, length, errno);
							r_data_len = length - wrote_len;
							r_offset = wrote_len;
							status = sta_hangup;
							
							/*
							 * hungup receiving fd
							 * */
							int ret = cm.RemoveEvent(cm._Reactor_FD, sock_fd, YM_EV_RD);
							if (ret != 0)
							{
								_Pr("err remove event %s,%d",__FILE__,__LINE__);
								return -1;
							}

							return -2;
						}
						_Pr("download data");
					}
				}
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
	case LOGIN_RSP:
		{

			LoginRsp* r = (LoginRsp*)msg;
			int respcode = r->rsp_code;
			if (r->avalable_traffic)
				aval_traffic = r->avalable_traffic;
			
			if (respcode == 0)
			{
				_Pr("User Login response code = %d, available traffic = %lu", respcode, r->avalable_traffic);
				
				ConnectHostReq* h = (ConnectHostReq*)tmp_buff;
				h->msg_id = CONNECT_HOST_REQ;
				h->port = web_port;
				h->host_len = host.size();
				memcpy(h->host, host.c_str(), h->host_len);
				
				//SendTmsMsg((U8*)h, sizeof(ConnectHostReq) + h->host_len - 1);
				
				//ready
				verified = 1;
				SwitchConnModeForw();
				
				BeginTransmit *t = (BeginTransmit *)tmp_buff;

				t->msg_id = BEGIN_TRANSMIT;
				t->forward_id = r->forward_id;
				SendTmsMsg((U8*)t, sizeof(BeginTransmit));
				
				//SendMsg((U8*)s_buff, s_data_len);
				
				//active proxy or wakeup
				if(!peer)
					WakeupUser();
				else
				{
					g_aval_traffic = aval_traffic; 
					
					if (peer->http_version == 0)
					{
						SendMsg((U8*)peer->r_buff, peer->r_data_len);
					}
					else if (peer->http_version == 1)
					{
						static char* connect_reply_OK = "HTTP/1.1 200 Connection established\r\n\r\n";
						peer->SendMsg((U8*)connect_reply_OK, strlen(connect_reply_OK));
					}
					//AddEvent
					peer->r_data_len = 0;
					peer->r_offset = 0;
					peer->stillneedtoread = sizeof r_buff;
					// ↓ not stable
					cm.AddREvent(cm._Reactor_FD, peer->sock_fd);
				}
			}
			else 
			{
				_Pr("User Login response code = %d", respcode);
				if (respcode == USER_NOT_EXIST)
				{
					login_code = RT_UserNotExist;
					AckUserFail(login_code);
				}
				else if (respcode == INCORRECT_TOKEN)
				{
					login_code = RT_IncorrectToken;
					AckUserFail(login_code);
				}
				else if (respcode == NO_BALANCE)
				{
					login_code = RT_NoBalance;
					AckUserFail(login_code);
				}
				else if (respcode == HOST_NOT_EXIST)
				{
					login_code = RT_HostNotExist;
					AckUserFail(login_code);
				}
				else if (respcode == FAIL)
				{
					login_code = RT_Exception;
					AckUserFail(login_code);
				}
			}
		}
		break;
	case CONNECT_HOST_RSP:
		{
			ConnectHostRsp* r = (ConnectHostRsp*)msg;
			int c = r->rsp_code;
			
			_Pr("Handle User Specify host... %d", c);
			if (c != SUCC)
				return c;
			else 
			{
				_Pr("User Login response code = %d", c);
				if (c == USER_NOT_EXIST)
				{
					login_code = RT_UserNotExist;
					AckUserFail(login_code);
				}
				else if (c == INCORRECT_TOKEN)
				{
					login_code = RT_IncorrectToken;
					AckUserFail(login_code);
				}
				else if (c == NO_BALANCE)
				{
					login_code = RT_NoBalance;
					AckUserFail(login_code);
				}
				else if (c == HOST_NOT_EXIST)
				{
					login_code = RT_HostNotExist;
					AckUserFail(login_code);
				}
				else if (c == FAIL)
				{
					login_code = RT_Exception;
					AckUserFail(login_code);
				}
			}
			
			SwitchConnModeForw();

			BeginTransmit *t = (BeginTransmit *)tmp_buff;

			t->msg_id = BEGIN_TRANSMIT;
			t->forward_id = r->forward_id;
			SendTmsMsg((U8*)t, sizeof(BeginTransmit));
			
			//SendMsg((U8*)s_buff, s_data_len);
			WakeupUser();
		}
		break;
	}

	return ret;
}

int Connect::ProxyRecvEvent()
{
	int fd = sock_fd;
	int length;
	int wrote_len;
	while ((length = recv(fd, r_buff + r_offset, stillneedtoread, 0)) > 0)
	{
		if (length > 0)
		{
			if (peer == nullptr)
			{
				//判断http
				//分配tms client
				int ret = HandleProxyProtocol(r_buff, r_offset + length);
				if (ret == -1)
				{
					_Pr("fail to connect tms");
					AckUserFail(RT_ServerNotUp);
					return ret;
				}
				else if (ret < 0)
				{
					return ret;
				}
			}
		}
		//判断代理是否连接
		if(peer != nullptr && peer->sock_fd)
		{
			if (peer->verified)
			{
				U64 forw_fd = peer->sock_fd;
				_Pr("uploading data");
				wrote_len = write(forw_fd, r_buff + r_offset, length);
				if (wrote_len != length || length == 0)
				{
					_Pr("upload data error: wrote_len = %d, data_len = %d, errno = %d"
						, wrote_len, length, errno);
				}
			}
			else //if (!peer->verified)
			{
				//删除proxy的event
				//挂起
//				length
//				r_buff + r_offset
				this->r_data_len += length;
				
				//tms通信
				const char* auth = YMEGet("Auth");
				const char* uid = YMEGet("Appid");
				const char* mobile = YMEGet("Phone");
					
				if (!auth || !uid || !mobile)
				{
					YMESet("Error", "incomplete auth/uid/mobile");
					_Pr("incomplete auth/uid/mobile");
					return -1;
				}
				memcpy(peer->auth, auth, strlen(auth)+1);
				peer->uid = atoll(uid);
				peer->mobile = atoll(mobile);
				peer->host = host;
				//优化
				peer->web_port = web_port;

				U8 login_str[512];
				LoginReq *r = (LoginReq *)login_str;
				r->msg_id = (U16)LOGIN_REQ;
				r->mobile = peer->mobile;
				r->app = peer->uid;
				r->port = peer->web_port;
				memcpy(r->token, peer->auth, strlen((char*)peer->auth) + 1);
				r->host_len = peer->host.length();
				memcpy(r->host, peer->host.c_str(), peer->host.length());
		
				U64 pfd = peer->sock_fd;

				peer->SendTmsMsg((U8*)r, sizeof(LoginReq) + peer->host.length() - 1);
                nonblocking(pfd);
                cm.AddEvent(cm._Reactor_FD, pfd, YM_EV_RD | YM_EV_WR | YM_EV_ONCE);
                
				_Pr("tms login request sent");
				
			}
		}
	}
	return length;
}

int Connect::HandleDspMsg(U8* msg, U32 msglen)
{
	int ret = 0;
	int msg_id = ((MessageID*)msg)->msg_id;
	std::cout << "HandleRecvMsg msgid=" << msg_id << " msglen=" << msglen << std::endl;
	switch (msg_id)
	{
	case C_ACCQ_TOKEN_RSP:
		{
			_Pr("accquire token resp...");
			CAccquireTokenRsp* r = (CAccquireTokenRsp*)msg;
			
			if (r->rsp_code != SUCC)
				return 0;
			
//			memcpy(auth, r->token, sizeof auth);
//
//			BeginTransmit *t = (BeginTransmit *)tmp_buff;
//
//			t->msg_id = BEGIN_TRANSMIT;
//			t->forward_id = r->forward_id;
//			SendTmsMsg((U8*)t, sizeof(BeginTransmit));
//			
//			SendMsg((U8*)s_buff, s_data_len);
		}
		break;
	}

	return ret;
}

int Connect::HandleProxyProtocol(U8* msg, U32 msglen)
{	
	char* cstr = (char*)msg;
	size_t slen = msglen;
	
	int ret0,ret;
	
	//tls
	static char connect_val_buff[256];
	size_t hlen;
	
	//port 443 https
	int retc;
	retc = strncasecmp("CONNECT", cstr, strlen("CONNECT"));
	if (retc == 0)
	{
		http_version = 1;
		cstr += strlen("CONNECT ");
		char* host_end = strchr(cstr, '\r');
		if (host_end - cstr > 256)
		{
			_Pr("exception parse connect protocol");
			return -1;
		}
		memcpy(connect_val_buff, cstr, host_end - cstr);
		connect_val_buff[host_end - cstr] = '\0';

		char *h, *p;
		h = strtok(connect_val_buff, ":");
		p = strtok(nullptr, "\r\n ");
		if (p[0] <= '9' && p[0] >= '0')
		{
			this->web_port = atoi(p);
		}
		this->host.assign(h, strlen(h));
	}
//	ret0 = DetactTlsClientHello((char*)msg, msglen, host_buff, &hlen);

	else
	{
		//http1.1(1.0)
		for(char* p = cstr ; p < cstr + slen ; p++)
		{
			if (*p == 'h' || *p == 'H')
			{
				ret = strncasecmp(p + 1, "ost: ", strlen("ost: "));
				if (!ret)
				{
					p += strlen("Host: ");
					char* r = "\r\n";
					char* pos = strstr(p, r);
					if (pos && pos < cstr + slen)
					{
						host.assign(p, pos - p);
						break;
					}
				}
			}
		}//end match head
	}
	
	if (host.length() > 0)
	{
		//create tms client
		U64 fd;
		const char *tmsip = YMEGet("Tmsip");
		const char *tmsport = YMEGet("Tmsport");
		if (!tmsip || !tmsport)
		{
			_Pr("tms ip/port not set correctly");
			_Pr("fail to connect tms");
			return -1;
		}
		Connect* pc = cm.CreateConn(fd, inet_addr(tmsip), htons((short)atol(tmsport)));
		if (pc)
		{
			peer = pc;
			//pc->peer = peer;
			//           ↑ error corrected
			pc->peer = this;
			
			//cm.AddEvent(cm._Reactor_FD, fd, YM_EV_WR|YM_EV_ONCE);
			return 0;
		}
		else
		{
			_Pr("fail to connect tms");
			return -1;
		}
	}
	
	return -2;  //EAGAIN
}

int Connect::HandleRecvHttp(U8* msg, U32 msglen)
{
	auto str_builder_rit = http_reply_datalist.rbegin();
	
	char* cstr = (char*)msg;

	size_t slen = msglen;
	char* body = nullptr;
	
	//port 80 http 1.1
	if (header)
	{
		int ret;
		
		ret = strncasecmp("HTTP/1.", cstr, strlen("HTTP/1."));
		if (!ret)
		{
			rsp_code = atoi(cstr + 8);
			header = 0;
			cstr += 8;
		}
		else 
		{
			//不带header的报文
		}
		//match

		for (char* p = cstr; p < cstr + slen; p++)
		{
			if (*p == 'C' || *p == 'c')
			{
				ret = strncasecmp(p + 1,"onnection:",strlen("onnection:"));
				if (!ret)
				{
					p += 1 + strlen("onnection:");
					if (*p == ' ') p++;
					if (!strncasecmp(p, "keep-alive", strlen("keep-alive")))
						keepalive = 1;
					else
						keepalive = -1;
				}

				ret = strncasecmp(p + 1, "ontent-Length:", strlen("ontent-Length:"));
				if (!ret)
				{
					p += 1 + strlen("ontent-Length:");
					if (*p == ' ') p++;
					content_length = atoi(p);
				}

			}
			
			else if (*p == 'T' || *p == 't')
			{
				ret = strncasecmp(p + 1, "ransfer-Encoding", strlen("ransfer-Encoding"));
				if (!ret)
				{
					p += 1 + strlen("ransfer-Encoding");
					if (*p == ' ') p++;
					if (!strncasecmp(p, "chunked", strlen("chunked")))
					{
						chunked = 1;
					}
					else
						chunked = -1;
				}
			}
			
			else if (*p == '\r')
			{
				ret = strncasecmp(p + 1, "\n\r\n", strlen("\n\r\n"));
				if (!ret)
				{
					p += 4;
					header = 0;
					mf = 0;
					body = p;
					if (content_length > 0)
						need_length = content_length;
					break;
				}
			}
		}//end match head
		
		if (!body) body = (char*)msg+msglen;
		std::string str;
		str.assign((char*)msg, body - (char*)msg);
		if (mf == 0)
		{
			http_reply_datalist.push_back(std::move(str));
			mf = 1;
		}
		else
			http_reply_datalist.rbegin()->append(str);
	}
	
	if(!header)
	{
		if (str_builder_rit == http_reply_datalist.rend())
			return 0;
		
		if (!body) body = (char*)msg;
		char* msg_end = (char*)msg + msglen;
		if (keepalive < 0 || chunked < 0)
		{
			//查找\r\n
		}
		else if (content_length > 0)
		{
			std::string str;

			if (need_length <= msg_end - body)
			{
				str.assign((char*)body, (int)content_length);
				http_reply_datalist.rbegin()->append(str);
				WakeupUser();
				
				//完全或有余
				http_reset();
				if (msg_end - body - need_length > 0)
				{
					//初始化
					HandleRecvHttp((U8*)body + need_length, msg_end - body + need_length);					
				}
			}
			else 
			{
				str.assign((char*)body, msg_end - body);
				http_reply_datalist.rbegin()->append(str);
				//不完全
				need_length -= msg_end - body;
			}
		}
		else /*if (rsp_code != 0 && rsp_code != 200)*/
		{
			std::string str;
			str.assign((char*)body, msg_end - body);
			http_reply_datalist.rbegin()->append(str);
		}
	}

	return 0;
}

int Connect::SendMsg(U8* msg, U32 msglen)
{
	int sendlen = write(sock_fd,msg,msglen);
	if (sendlen<0) 
		_Pr("SendMsg error = %d", errno);
	http_transmit_len += sendlen;
	rw_lock.lock();
	g_rpt_traff_cost += sendlen;
	rw_lock.unlock();
	return 0;
}

int Connect::SendTmsMsg(U8* msg, U32 msglen)
{
	hdr_t hdr;
	hdr.len = htons(msglen);
	U8 tmsMsg[1024];
	memcpy(tmsMsg, &hdr, sizeof hdr);
	memcpy(tmsMsg + sizeof hdr, msg, msglen);

	int sendlen = write(sock_fd, tmsMsg, sizeof(hdr) + msglen);
	
	if (sendlen < 1)
		_Pr("SendTmsMsg error = %d", errno);
	//if(status==s_forward)
	http_transmit_len += Traffic_Est(sendlen);
	rw_lock.lock();
	g_rpt_traff_cost += Traffic_Est(sendlen);
	rw_lock.unlock();
	return sendlen;
}

int ConnectMgr::HandleAcceptEvent(U64 listen_fd)
{
#define MINUTE 1000000
	int newfd = accept(listen_fd, nullptr, nullptr);
	if (newfd>0) {
		// time window
		if(time_window)
		{
			timeval now;
			gettimeofday(&now, nullptr);
			long long now_time = now.tv_sec * MINUTE + now.tv_usec;
			if (now_time - accept_begin_time > MINUTE * 5)
			{
				close(newfd);
				return 0;
			}
			
			accept_begin_time = now_time;
		}
//		if (_ConnCount>0)
//		{
//			close(newfd);
//			_Pr("reject new connection");
//			return 0;
//		}
		
//		int nSndBuf = 1024 * 1024;   //设置为1M
//		setsockopt(newfd, SOL_SOCKET, SO_SNDBUF, (const char*)&nSndBuf, sizeof(int));
		
		Connect* p;
		auto iter = _ConnMap.find(newfd);
		if (iter != _ConnMap.end())
		{
			p = iter->second;
			p->Reset();
		}
		else 
		{
			p = new Connect;
		}

		p->sock_fd = newfd;
		_ConnMap[newfd] = p;
	
		p->stillneedtoread = sizeof p->r_buff;
		p->type = t_proxy;
		_Pr("new connection , fd = %d, type = %d", newfd, p->type);
		
		ConnRecycleBlock::CRBNew(p->crb_it);
		ConnRecycleBlock *cb = *p->crb_it;
		cb->fd = newfd;
		cb->conn = p;
		gettimeofday(&cb->tv, nullptr);
		
		_ConnCount++;
	}
	return newfd;
}

//隧道收到数据
//@retval
int ConnectMgr::HandleRecvEvent(U64 fd)
{
	int ret;
    Connect* c;
    auto it = _ConnMap.find(fd);
    if(it==_ConnMap.end())
    {
        _Pr("Connection %d is not found in connection manager", fd);
	    //RemoveEvent(_Reactor_FD, fd, YM_EV_RD | YM_EV_WR);
        return -1;
    }

    c = it->second;
    if(!c)
    {
        _Pr("connection point to 0");
        return -1;
    }
    
	if (c->type==t_proxy)
	{
		return c->ProxyRecvEvent();
	}
	else
	{
		c->host.clear();
		return c->StreamRecvEvent();
	}
}


//创建上行代理连接
Connect* ConnectMgr::CreateConn(U64& socket_fd, U64 ip, U16 port)
{
	Connect* conn = nullptr;
	int ret = 0;
	int s;
	struct sockaddr_in addr_to_in;
	addr_to_in.sin_family = AF_INET;
	addr_to_in.sin_port = port;
	addr_to_in.sin_addr.s_addr = ip;
	//_Pr("port=%d,ip=%d,", ntohs(port), ntohl(ip));
	s = socket(AF_INET, SOCK_STREAM, 0);
	
	if (s <= 0){
		printf("socket create ret code %d", s);
		return conn;
	}
	
	int nSndBuf = 1 * 1024 * 1024;     //设置为1M
	setsockopt(s, SOL_SOCKET, SO_RCVBUF, (const char*)&nSndBuf, sizeof(int));
	
	_Pr("connecting TMS");
	ret = connect(s, const_cast<sockaddr*>((sockaddr*)&addr_to_in), sizeof(struct sockaddr_in));
	if (ret<0 && errno!=EINPROGRESS){
		printf("socket connect ret code %d,err %d\n", ret, errno);
		close(s);
		return conn;
	}

	{	
		Connect* p = new Connect;
		if (_ConnMap.count(s) > 0)
		{
			//close stream
			_ConnMap.erase(s);
		}
		_ConnMap[s] = p;

		p->sock_fd = s;
		p->status = sta_online;
		p->cstate = s_header;
		p->type   = t_tunnel;
		p->r_data_len = 0;
		p->stillneedtoread = sizeof(hdr_t);

		socket_fd = s;
		conn = p;
	}
	
	return conn;
}

int ConnectMgr::AddREvent(long reactor_fd, long fd)
{
	if (!reactor_fd||!fd)
	{
		_Pr("AddREvent parament error");
		return -1;
	}
#ifdef __APPLE__
	struct kevent ev;
	EV_SET(&ev, fd, EVFILT_READ, EV_ADD, 0, 0, NULL);
	if(kevent(reactor_fd, &ev, 1, NULL, 0, NULL)==-1)
    {
        _Pr("kevent add error, err[%d]", errno);
        return -1;
    }
#else
	struct epoll_event ev;
	ev.events = EPOLLIN | EPOLLET;
	ev.data.fd = fd;

	if (epoll_ctl(reactor_fd, EPOLL_CTL_ADD, fd, &ev) == -1) {
		_Pr("epoll add error, err[%d]", errno);
		return -1;
	}
#endif
	return 0;
}

int ConnectMgr::AddEvent(long reactor_fd, long fd, int event)
{
	if (!reactor_fd || !fd)
	{
		_Pr("AddEvent parament error");
		return -1;
	}
#ifdef __APPLE__
    U64 events=0, behav = EV_ADD;
    if (event & YM_EV_RD)
        events |= EVFILT_READ;
    if (event & YM_EV_WR)
        events |=  EVFILT_WRITE;
    //if (event & YM_EV_ONCE)
    //    behav |= EV_ONESHOT;

    struct kevent ev;
	EV_SET(&ev, fd, events, behav, 0, 0, NULL);
	if(kevent(reactor_fd, &ev, 1, NULL, 0, NULL) == -1)
    {
        _Pr("kevent add error, err[%d]", errno);
        return -1;
    }
#else
	struct epoll_event ev;
	ev.data.fd = fd;
	ev.events = 0;
	if (event & YM_EV_RD)
		ev.events |= EPOLLIN;
	if (event & YM_EV_WR)
		ev.events |=  EPOLLOUT;
	if (event & YM_EV_EX)
		ev.events |=  EPOLLOUT;
	if (event & YM_EV_ONCE)
		ev.events |=  EPOLLET;

	if (epoll_ctl(reactor_fd, EPOLL_CTL_ADD, fd, &ev) == -1) {
		_Pr("epoll add error, err[%d]", errno);
		return -1;
	}
#endif
	return 0;
}

int ConnectMgr::RemoveEvent(long reactor_fd, long fd, int rmevents)
{
	if(!reactor_fd||!fd)
	{
		_Pr("RemoveEvent parament error");
		return -1;
	}
#ifdef __APPLE__
	struct kevent ev;
	EV_SET(&ev, fd, EVFILT_READ | EVFILT_WRITE , EV_DELETE, 0, 0, NULL);
	if( kevent(reactor_fd, &ev, 1, NULL, 0, NULL) == -1 )
    {
        _Pr("kevent remove error, err[%d]", errno);
        return -1;
    }
#else
	struct epoll_event ev;
	ev.events = 0;
	if (rmevents & YM_EV_RD)
		ev.events |= EPOLLIN;
	if (rmevents & YM_EV_WR)
		ev.events |=  EPOLLOUT;
	if (rmevents & YM_EV_EX)
		ev.events |=  EPOLLOUT;

	ev.data.fd = fd;

	if (epoll_ctl(reactor_fd, EPOLL_CTL_DEL, fd, &ev) == -1) {
		_Pr("epoll remove error, err[%d]", errno);
		return -1;
	}
#endif
	return 0;
}

int ConnectMgr::ModifyEvent(long reactor_fd, long fd, int event)
{
	if (!reactor_fd || !fd)
	{
		_Pr("AddEvent parament error");
		return -1;
	}
#ifdef __APPLE__
    U64 events=0, behav = EV_ADD;
    if (event & YM_EV_RD)
        events |= EVFILT_READ;
    if (event & YM_EV_WR)
        events |=  EVFILT_WRITE;
    //if (event & YM_EV_ONCE)
    //    behav |= EV_ONESHOT;
    
	struct kevent ev;
    EV_SET(&ev, fd, EVFILT_READ | EVFILT_WRITE, EV_DELETE, 0, 0, NULL);
	EV_SET(&ev, fd, events, behav, 0, 0, NULL);
	int ret = kevent(reactor_fd, &ev, 1, NULL, 0, NULL);
#else
	struct epoll_event ev;
	ev.data.fd = fd;
	ev.events = 0;
	if (event | YM_EV_RD)
		ev.events |= EPOLLIN;
	if (event | YM_EV_WR)
		ev.events |=  EPOLLOUT;
	if (event | YM_EV_EX)
		ev.events |=  EPOLLOUT;
	if (event | YM_EV_ONCE)
		ev.events |=  EPOLLET;

	if (epoll_ctl(reactor_fd, EPOLL_CTL_MOD, fd, &ev) == -1) {
		_Pr("epoll add error, err[%d]", errno);
		return -1;
	}
#endif
	return 0;
}

int ConnectMgr::RemoveConnect(long long fd)
{
	if (fd < 0)
		return -1;
	
	auto iter = _ConnMap.find(fd);
	if (iter != _ConnMap.end())
	{
		Connect* c = iter->second;
		
		if (c->type == t_proxy && c->peer)
		{
			_Pr("total http transmit length=%d", c->peer->http_transmit_len);
			
		}
		
		//导致bug
		//ConnRecycleBlock::CRBDelete(c->crb_it);
		
		c->status = sta_disconnected;
		//		delete c;
		//		_ConnMap.erase(iter);

		if(c->type == t_proxy || c->peer->type == t_proxy)
		{
			if (c->peer)
			{
				_ConnMap.erase(c->peer->sock_fd);
				delete c->peer;
			}
			//_ConnMap.erase(iter);
			//   incorrect use ↑
			_ConnMap.erase(c->sock_fd);
			delete c;     // <==
		}
		else
			c->WakeupUser();
		
		_ConnCount--;
	}
	else
	{
		cm.RemoveEvent(cm._Reactor_FD, fd);
		//close(fd);
		shutdown(fd, SHUT_RDWR);
	}

	return 0;
}


//
//此函数处理两类事件：连接建立、下载文件的发送缓冲区由满变为可用
int ConnectMgr::HandleEPOEvent(U64 fd)
{
	int ret;
	Connect* c = _ConnMap[fd];
	
	if(!c)
	{
		_Pr("EPO event arrived but no relative connection");
		//RemoveEvent(_Reactor_FD, fd, YM_EV_RD | YM_EV_WR);
		return -1;
	}
	
	if (c && c->peer && c->peer->status == sta_hangup ) {

		//only cares about tunnel socket that is writeable
		if(c->type != t_proxy)
			return 0;
		_Pr("fd %d is writeable", fd);
		
		//清空缓存
		Connect *tc = c->peer;
		if (tc->r_data_len == 0)
			return 0;

		int wrotelen = write(fd, tc->r_buff + tc->r_offset, tc->r_data_len);
		_Pr("continue transmit: wrotelen = %d, data offset = %d ,data len = %d", wrotelen, tc->r_offset, tc->r_data_len);
		if (wrotelen == tc->r_data_len)
		{
			tc->r_offset = 0;
			tc->stillneedtoread = sizeof(tc->r_buff);
			tc->status = sta_online;
			AddREvent(_Reactor_FD, tc->sock_fd);
		}
		else if (wrotelen > 0)
		{
			tc->r_offset += wrotelen;
			tc->r_data_len -= wrotelen;
		}
		else
		{
			AddEvent(_Reactor_FD, c->sock_fd, YM_EV_WR);
		}
		
		if (wrotelen > 0)
		{
			//bug点
//			c->peer->http_transmit_len += wrotelen;
//			rw_lock.lock();
//			g_rpt_traff_cost += wrotelen;
//			rw_lock.unlock();
		}
		return 0;
	}
#ifdef __APPLE__
    //return -1;
#endif
	return 0;
}

