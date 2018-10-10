
#include "Recycler.h"
#include "ConnectMgr.h"

std::list<ConnRecycleBlock*> ConnRecycleBlock::expire_list;
std::list<ConnRecycleBlock*> ConnRecycleBlock::recycle_list;

static ConnectMgr& cm = ConnectMgr::Instance();

void ConnRecycleBlock::CRBNew(std::list<ConnRecycleBlock*>::iterator& it)
{
	ConnRecycleBlock* crb;
	if (!recycle_list.empty())
	{
		crb = recycle_list.front();
		recycle_list.pop_front();
	}
	else
	{
		crb = new ConnRecycleBlock();
	}
	
	it = expire_list.insert(expire_list.end(), crb);
	return;
}

void ConnRecycleBlock::CRBDelay(std::list<ConnRecycleBlock*>::iterator& it)
{
	ConnRecycleBlock *to_remove = *it;
	std::list<ConnRecycleBlock*>::iterator it_new;
	CRBNew(it_new);
	(*it_new)->conn = to_remove->conn;
	(*it_new)->fd = to_remove->fd;
	(*it_new)->traffic = to_remove->traffic;
	gettimeofday(&(*it_new)->tv, nullptr);
	recycle_list.push_back(to_remove);
	to_remove->Delay();
	it = expire_list.erase(it);
	to_remove->conn->crb_it = it;
	return;
}

void ConnRecycleBlock::CRBDelete(std::list<ConnRecycleBlock*>::iterator it)
{
	if (it == expire_list.end())
		return;
	ConnRecycleBlock *to_remove = *it;
	if (to_remove->conn && to_remove->conn->crb_it != expire_list.end())
		to_remove->conn->crb_it = expire_list.end();
	recycle_list.push_back(to_remove);
	to_remove->Reset();
	it = expire_list.erase(it);
}

#define Expire_Seconds 12
#define Expire_Count   5
void ConnRecycleBlock::CRBScan()
{
	int i = Expire_Count;
	timeval tv;
	gettimeofday(&tv, nullptr);
	
	for (auto iter = expire_list.begin()
		; iter != expire_list.end() && i --> 0
		; )
	{
		ConnRecycleBlock *to_scan = *iter;
		timeval& last_acc = to_scan->tv;
		if (last_acc.tv_sec + Expire_Seconds < tv.tv_sec)
		{
			//检查流量有没有增加
			auto j = cm._ConnMap.find(to_scan->fd);
			if (j != cm._ConnMap.end())
			{
				if (to_scan->traffic == j->second->http_transmit_len)
				{
					//断连
					_Pr("fd %llu is no longer transmitting", to_scan->fd);
					iter++;
					cm.RemoveConnect(to_scan->fd);
					//CRBDelete(iter);
				}
				else
				{
					to_scan->traffic = j->second->http_transmit_len;
					CRBDelay(iter);
				}
			}
			else
			{
				CRBDelete(iter);
			}
		}
		else
			break;
	}
	return;
}