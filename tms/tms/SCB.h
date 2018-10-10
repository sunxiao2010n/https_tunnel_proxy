#pragma once
#ifndef SCB_H
#define SCB_H

#include "TMSDataTypes.h"
#include "ConnectMgr.h"
#include "UserMgr.h"
#include <sys/time.h>
#include <list>
#include <queue>

/*
 *  stream control block 
 *  
 *	create sunxiao 20180417
 *
 **/
class User;

class SCB //stream control block
{
public:
	void Reset()
	{
		scbid = 0;
		user  = nullptr;
		agent_fd = 0;
		tunn_fd  = 0;
		agent_conn = nullptr;
		tunnel_conn = nullptr;
		last_active_time = { 0, 0 };
		status = 0;
		recycled = 0;
		forward_id = 0;
		traffic = 0;
		dest_port = 0;
		connected = 0;
	}
public:
	SCB(U64 id):scbid(id) {}
	U64    scbid = 0;
	User*  user  = nullptr;
	U64    forward_id = 0;
	U64	agent_fd = 0;
	U64 tunn_fd  = 0;
	U64 traffic  = 0;
	Connect* agent_conn = nullptr;
	Connect* tunnel_conn = nullptr;
	timeval last_active_time = { 0, 0 };
	U16 dest_port = 80;
	U8 status = 0;   //verified
	U8 recycled = 0;
	U8 waiting_dns = 0;
	U8 connected = 0;
};

struct SCBWatchNode
{
	U64  scbid;
	U64  traffic;
	SCB* scb;
};

class SCBMgr
{
public:
	U64 aval_pcb_id = 0;
	std::list<SCB*> scb_free_list;
	std::list<SCBWatchNode> scb_watch_list;
	std::queue<SCBWatchNode> scb_expire_list;

	static SCBMgr& Instance()
	{
		static SCBMgr instance;
		return instance;
	}
	int SCBWatchScan()
	{
		std::list<SCBWatchNode>::iterator n = scb_watch_list.begin();
		
		while (n != scb_watch_list.end())
		{
			auto m = n;
			m++;
			if (n->scbid == n->scb->scbid)
			{
				if (n->traffic == n->scb->traffic)
				{
					scb_expire_list.push(*n);
					scb_watch_list.erase(n);
				}
			}
			else
			{
				scb_watch_list.erase(n);
			}
			n = m;
		}

		while (scb_expire_list.size() != 0)
		{
			SCBWatchNode &watchnode = scb_expire_list.front();
			if (watchnode.scbid == watchnode.scb->scbid)
			{
				if (watchnode.traffic == watchnode.scb->traffic)
				{
					//expire
					//watchnode
				}
				else
				{
					scb_watch_list.push_back(watchnode);
				}
			}
			else
				;
			scb_expire_list.pop();
		}
		
		return 0;
	}
	SCB* NewSCB()
	{
		SCB* scb;
		if (!scb_free_list.empty())
		{
			scb = scb_free_list.front();
			scb_free_list.pop_front();
			scb->scbid = ++aval_pcb_id;
		}
		else
		{
			scb = new SCB(++aval_pcb_id);
		}
		return scb;
	}
	void RecycleSCB(SCB* scb)
	{
		scb->Reset();
		scb_free_list.push_back(scb);
	}
	void RecursiveRecycle(SCB* scb);
};

#endif // !SCB_H
