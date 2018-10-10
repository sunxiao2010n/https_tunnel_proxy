#include <time.h>
#include "UserMgr.h"

static ConnectMgr& cm  = ConnectMgr::Instance();
static SCBMgr&     scm = SCBMgr::Instance();

int User::TransferDataToWeb()
{
	return 0;
}

int User::TransferDataToSDK()
{
	return 0;
}

void User::ResetUser()
{
	app = 0;
	status = 0;
	forward_id = 0;
	reporting = 0;

	
	//其他归零操作

	return;
}

/*
User* UserMgr::ResetUser(UserId &id, Connect* tc, Connect* ac)
{
	User* u = nullptr;
	if (_UserMap.count(id) > 0)
	{
		u = _UserMap[id];
		//u->Reset();
	}
	else
	{
		do {
			u = new User;
		} while (u == nullptr);

		_UserMap.insert(std::pair<UserId, User*>(id, u));
		//_UserMap[id] = u;
	}
	u->status = 0;
	u->forward_id = 0;
	u->balance = 0;
	u->traffic = 0;
	
	if (tc && u->tunnel_conn != tc)
	{
		_Pr("reset user: delete online connection");
		if (u->tunnel_conn)
		{
			cm._ConnMap.erase(u->tunnel_conn->sock_fd);
			delete u->tunnel_conn;
		}

		u->tunnel_conn = tc;
	}
	if (ac && u->agent_conn != ac)
	{
		_Pr("reset user: delete agent connection");
		if (u->agent_conn)
		{
			cm._ConnMap.erase(u->agent_conn->sock_fd);
			delete u->agent_conn;
		}
		u->agent_conn = ac;
	}
	return u;
}
*/

int UserMgr::TraceUser(User* u)
{
	_ExpireList[RETRY_TIMES - 1].push_back(u);
	return 0;
}

int UserMgr::ExpireLocalUser()
{
	{
		auto bg = _ExpireList[0].begin();
		while (bg != _ExpireList[0].end())
		{
			User *u = *bg;
//			if (u->tunnel_conn && u->agent_conn)
//			{
//				TraceUser(u);
//			}
//			else
//			{
//				//expire
//				
//			}
			_ExpireList[0].pop_front();
			bg = _ExpireList[0].begin();
		}
	}
	
	for (int i = 1; i < RETRY_TIMES; i++)
	{
		auto bg = _ExpireList[i].begin();
		while (bg != _ExpireList[i].end())
		{
			User *u = *bg;
//			if (u->tunnel_conn && u->agent_conn)
//			{
//				TraceUser(u);
//			}
//			else
//			{
//				_ExpireList[i - 1].push_back(u);
//			}
			_ExpireList[i].pop_front();
			bg = _ExpireList[i].begin();
		}
	}
	//_ExpireList[RETRY_TIMES - 1].push_back(u);
	return 0;
}

int UserMgr::IncreaseExpireTime()
{
	//delete inactive user

	ExpireLocalUser();
	
	//increase redis expire time of active user
	KeepAliveToken t;
	Connect* c = cm.GetCenterConn();
	int i;
	auto ui = _UserMap.begin();
	while (ui != _UserMap.end())
	{
		User* u = ui->second;
		if (i == 16)
		{
			c->SendTmsMsg((U8*)&t, sizeof t);
			i = 0;
		}
//		if (!u->tunnel_conn || !u->agent_conn)
//		{
//			TraceUser(u);
//			t.app[i] = u->app;
//			t.mobile[i] = u->mobile;
//			i++;
//		}
		ui++;
	}
	while (i > 0 && i < 16)
	{
		t.app[i] = 0;
		t.mobile[i] = 0;
		i++;
	}
	if(i>0)
		c->SendTmsMsg((U8*)&t, sizeof t);
	
	return 0;
}

/* create agent connection 
 * stream is created 
 *
 * le interface
 * 
 */
SCB* User::ConnectWebServer(Connect *tunnel, U64 ip, U16 port)
{	
	ConnectHostRsp rsp;
	U64 agent_fd;
	//U32 ip = inet_addr("111.13.101.208");

	_Pr("connecting web server");
	Connect* agent = cm.CreateAgentConn(agent_fd, ip, port);
	if (!agent)
	{
		_Pr("agent connection get error");
		rsp.rsp_code = FAIL;
		if (tunnel)
			tunnel->SendTmsMsg((U8*)&rsp, sizeof rsp);
		
		return nullptr;
	}
	
	SCB* scb = tunnel->scb;
	if (scb)
	{
		scb->dest_port = port;
		scb->agent_conn = agent;
		scb->agent_fd = agent->sock_fd;
		agent->user = this;
		agent->scb = scb;
	}
	else
		scb = CreateSCB(tunnel, agent);
	
	cm.AddEvent(cm._Epoll_FD, agent_fd, EPOLLOUT | EPOLLET);
	rsp.rsp_code = PROG;
	if (tunnel)
		tunnel->SendTmsMsg((U8*)&rsp, sizeof rsp);

	return scb;
}

SCB* User::CreateSCB(Connect *tunnel, Connect* agent)
{
	if (tunnel->scb)
		return tunnel->scb;
	
	SCB* scb = scm.NewSCB();	
	scb->user = this;
	scb->dest_port = tunnel->dest_port;
	scb->tunnel_conn = tunnel;
	scb->tunn_fd = tunnel->sock_fd;
	gettimeofday(&scb->last_active_time, nullptr);
	
	tunnel->user = this;
	tunnel->scb = scb;

	
	if (agent)
	{
		scb->agent_conn = agent;
		scb->agent_fd = agent->sock_fd;
		agent->user = this;
		agent->scb = scb;
	}
	
	scb_tree.insert(std::pair<U64, decltype(scb)>(scb->scbid, scb));
	
	return scb;
}

int User::RecordTraffic(SCB* scb,U64 datalen)
{
	if (!scb)
	{
		_Pr("error : no scb");
		return -1;
	}
	//sdk端的数据，从这里记录
	//traffic += datalen;
	scb->traffic += datalen;
	balance -= datalen;   //考虑异常情况，数据库掉线又恢复，怎样同步余量？
	if(scb->status == u_validated && balance < 4000)
	{
		_Pr("insufficient balance of %0.11lu|%0.8lu",mobile,app);
		return -1;
		//need to disconnect
	}
	return 0;
}

int User::ReportTraffic(SCB* scb)
{
	if (scb->traffic == 0 || scb->status != u_validated)
		return 0;
	
	scb->traffic += Traffic_TCP_hss();   //tcp创建及断开的流量
	
	TrafficReport r;

	r.mobile = mobile;
	r.app = app;
	r.traffic = scb->traffic;
	gettimeofday(&r.etime, nullptr);
	
	if (scb->tunnel_conn)
	{
		r.btime.tv_sec = scb->tunnel_conn->begin_time.tv_sec;
		r.btime.tv_usec = scb->tunnel_conn->begin_time.tv_usec;
		memcpy(r.host, scb->tunnel_conn->curr_host.c_str(), scb->tunnel_conn->curr_host.size());
	}
	else
	{
		_Pr("I don't wanna reach here, fix me pls");
	}
	
	char timestamp[32];
	char timestamp1[32];
	struct tm tm_now;
	localtime_r(&r.btime.tv_sec, &tm_now);
	strftime(timestamp, sizeof timestamp, "%Y-%m-%d %H:%M:%S", &tm_now);
	localtime_r(&r.etime.tv_sec, &tm_now);
	strftime(timestamp1, sizeof timestamp1, "%Y-%m-%d %H:%M:%S", &tm_now);
	
	_Pr("[traffic report] %0.11llu|%0.8llu|%llu|%s,%lu|%s,%lu|%s",
		mobile,
		app,
		scb->traffic,
		timestamp,
		r.btime.tv_usec/1000,
		timestamp1,
		r.etime.tv_usec/1000,
		r.host);
	//
	//
	Connect* c = cm.GetCenterConn();
	
	//屏蔽原因：放到dpi做
	//
	//c->SendTmsMsg((U8*)&r, sizeof r);
	
	scb->traffic = 0;
	return 0;
}

int User::ReportOrAccumulate(SCB* scb)
{
	if (!scb->traffic)
		return -1;
	scb->traffic += Traffic_TCP_hss();    //tcp创建及断开的流量
	
	TrafficReport r;
	r.mobile = mobile;
	r.app = app;
	
	if (scb->tunnel_conn)
	{
		r.btime.tv_sec = scb->tunnel_conn->begin_time.tv_sec;
		r.btime.tv_usec = scb->tunnel_conn->begin_time.tv_usec;
		memcpy(r.host, scb->tunnel_conn->curr_host.c_str(), scb->tunnel_conn->curr_host.size());
		
		if (con_report == 0)
		{
			gettimeofday(&accu_start_time, nullptr);
		}
		
		if (con_report == 0 || (accu_start_time.tv_sec < r.btime.tv_sec)
			|| ((accu_start_time.tv_sec == r.btime.tv_sec) && (accu_start_time.tv_usec < r.btime.tv_usec))
			)
		{
			;
		}
		else
		{
			accu_start_time = r.btime;
		}
	}
	else
	{
		_Pr("I don't wanna reach here, fix me pls");
		return -1;
	}
	
	char timestamp[32];
	char timestamp1[32];
	struct tm tm_now;
	r.btime = accu_start_time;
	localtime_r(&r.btime.tv_sec, &tm_now);
	strftime(timestamp, sizeof timestamp, "%Y-%m-%d %H:%M:%S", &tm_now);
	
	gettimeofday(&r.etime,nullptr);
	localtime_r(&r.etime.tv_sec, &tm_now);
	strftime(timestamp1, sizeof timestamp1, "%Y-%m-%d %H:%M:%S", &tm_now);
	
	_Pr("[traffic report lo] %0.11llu|%0.8llu|%llu|%s,%lu|%s,%lu|%s",
		mobile,
		app,
		scb->traffic,
		timestamp,
		r.btime.tv_usec/1000,
		timestamp1,
		r.etime.tv_usec/1000,
		r.host
		);
	//
	//
	
	if(reporting)
	{
		//local report
		con_report++;
		traffic += scb->traffic;
	}
	else
	{
		//report to tr
		reporting = 1;
		r.traffic = scb->traffic;
		
		_Pr("[traffic report tr] %0.11llu|%0.8llu|%llu|%s,%lu|%s,%lu|%s",
			mobile,
			app,
			scb->traffic,
			timestamp,
			r.btime.tv_usec/1000,
			timestamp1,
			r.etime.tv_usec/1000,
			r.host);
		
		Connect* c = cm.GetCenterConn();
		
		//屏蔽原因：放到dpi做
		//
		//c->SendTmsMsg((U8*)&r, sizeof r);
		
		traffic = 0;
	}
	scb->traffic = 0;
	
	return 0;
}

int User::ReportAccumulated()
{
	TrafficReport r;

	r.mobile = mobile;
	r.app = app;
	r.traffic = traffic;
	
	gettimeofday(&r.etime, nullptr);
	r.btime = accu_start_time;

	char timestamp[32];
	char timestamp1[32];
	struct tm tm_now;
	localtime_r(&r.btime.tv_sec, &tm_now);
	strftime(timestamp, sizeof timestamp, "%Y-%m-%d %H:%M:%S", &tm_now);
	localtime_r(&r.etime.tv_sec, &tm_now);
	strftime(timestamp1, sizeof timestamp1, "%Y-%m-%d %H:%M:%S", &tm_now);
	
	_Pr("[traffic report tr] %0.11llu|%0.8llu|%llu|%s,%lu|%s,%lu|-",
		mobile,
		app,
		traffic,
		timestamp,
		r.btime.tv_usec/1000,
		timestamp1,
		r.etime.tv_usec/1000
		);
	
	Connect* c = cm.GetCenterConn();
	//屏蔽原因：放到dpi做
	//
	//c->SendTmsMsg((U8*)&r, sizeof r);
	
	traffic = 0;
	con_report = 0;
	return 0;
}

User* UserMgr::RetrieveUser(UserId &id)
{
	User* u = nullptr;
	if (_UserMap.count(id) > 0)
	{
		u = _UserMap[id];
	}
	else
	{
		do {
			u = new User;
		} while (u == nullptr);

		_UserMap.insert(std::pair<UserId, User*>(id, u));
		//_UserMap[id] = u;
		
		u->status = 0;
		u->forward_id = 0;
		u->balance = 0;
		u->traffic = 0;
		u->app = id.app;
		u->mobile = id.mobile;
		gettimeofday(&u->accu_start_time, nullptr);
	}

	return u;
}

void User::ReleaseSCBList()
{
	for (auto& n : scb_tree)
	{
		scm.RecycleSCB(n.second);
	}
	scb_tree.clear();
	
	return;
}

void User::ReleaseSCB(SCB* scb)
{
	_Pr("user %llu|%llu releasing SCB %d", mobile,app,scb->scbid);
	scm.RecycleSCB(scb);
	return;
}