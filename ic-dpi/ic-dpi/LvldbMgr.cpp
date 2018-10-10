#include "LvldbMgr.h"
#include "PcapModule.h"
#include "ConnectMgr.h"

#include <sys/time.h>
#include <iostream>

LvldbMgr LvldbMgr::inst;
static ConnectMgr& cm = ConnectMgr::Instance();

void deleter(const leveldb::Slice& key, void* value)
{
	std::cout << key.data() << "  " << *(int*)value << std::endl;
	std::cout << "DELETE\n";
}

int test_lvldb()
{
	leveldb::Cache* c = leveldb::NewLRUCache(1);
	
	//  virtual Handle* Insert(const Slice& key, void* value, size_t charge,
	//                         void (*deleter)(const Slice& key, void* value)) = 0;
	leveldb::Cache::Handle *h;
	leveldb::Slice k1("k1");
	leveldb::Slice k2("k2");
	leveldb::Slice k3("k3");
	leveldb::Slice k4("k4");
	leveldb::Slice k5("k5");
	leveldb::Slice k6("k6");
	leveldb::Slice k7("k7");
	leveldb::Slice k8("k8");
	leveldb::Slice k9("k9");
	leveldb::Slice k10("k10");
	int v1 = 1, v2 = 2, v3 = 3;

	h = c->Insert(k1, &v1, 1,::deleter);
	c->Release(h);
	h = c->Insert(k2, &v2, 1, deleter);
	c->Release(h);
	h = c->Insert(k3, &v3, 1, deleter);
	c->Release(h);
	h = c->Insert(k4, &v3, 1, deleter);
	c->Release(h);
	h = c->Insert(k5, &v3, 1, deleter);
	c->Release(h);

	//c->Erase(k1);
	std::cout << "LRU TEST OVER\n";
	return 0;
}

int LvldbMgr::OnUserLogin(SynUserTuple *u)
{
	IpUserTraffic *dbu;
	leveldb::Slice key((char*)&u->src_ip_be, sizeof u->src_ip_be);
	leveldb::Cache::Handle *h;
	
	lrucache c = cache_tbl[0];
	h = c->Lookup(key);
	if (!h)
	{
		dbu = (IpUserTraffic*)calloc(1,sizeof(IpUserTraffic));
		h = c->Insert(key, (char*)dbu, 1, ::deleter);
	}
	else
	{
		//Èë¿â²Ù×÷?
	}
	
	dbu = (IpUserTraffic *)c->Value(h);
	dbu->ip_be  = u->src_ip_be;
	dbu->mobile = u->mobile;
	dbu->app    = u->app;
	
	c->Release(h);
}

int LvldbMgr::Init()
{
	lrucache c = leveldb::NewLRUCache(hash_capacity);
	if (c)
		cache_tbl.push_back(c);
	else
	{
		_Pr("lruhash init fail");
		return -1;
	}
	return 0;
}

int LvldbMgr::OnTrafficReport(U32 ip_be, U64 traffic, int fin_type)
{
	IpUserTraffic *dbu;
	leveldb::Slice key((char*)&ip_be, sizeof ip_be);
	leveldb::Cache::Handle *h;
	
	lrucache c = cache_tbl[0];
	h = c->Lookup(key);
	if (!h)
	{
		dbu = (IpUserTraffic*)calloc(1, sizeof(IpUserTraffic));
		h = c->Insert(key, (char*)dbu, 1, ::deleter);
	}
	else
	{
		//?
		//h = c->Insert(key, (char*)dbu, 1, ::deleter);
	}
	
	dbu = (IpUserTraffic *)c->Value(h);
	//	while (!atomic_cas(&dbu->fins, dbu->fins, dbu->fins | fin_type))
	//		;
	__sync_synchronize();
	dbu->fins |= fin_type;
	bool fin = dbu->fins >= (fin_type_c | fin_type_s);

	while (true)
	{
		if (!fin)
		{
			if (!atomic_cas(&dbu->traffic, dbu->traffic, dbu->traffic + traffic))
				continue;
			break;
		}
		else if ( fin && !dbu->reporting && dbu->traffic > 256 )
		{
			if (dbu->mobile == 0)
				break;
			
			if (!atomic_cas(&dbu->reporting, 0, 1))
				continue;

			//report(to_report);
			_Pr("[traffic] %llu|%llu|%llu", dbu->mobile, dbu->app, dbu->traffic + traffic + MinAckLen);

			//cas operate me
			U64 to_report = dbu->traffic + traffic + MinAckLen;
			dbu->traffic = - MinAckLen ;
			//dbu->fins = 0;
			while(!atomic_cas(&dbu->fins, dbu->fins, 0))
				;
			
			TrafficReport r;
			r.mobile = dbu->mobile;
			r.app    = dbu->app;
			r.ip     = ip_be;
			r.traffic = to_report;
			gettimeofday(&r.btime,nullptr);
			gettimeofday(&r.etime, nullptr);
			memcpy(r.host, "-", 2);
			r.hlen = 1;
			Connect *c = cm.GetCenterConn();
			c->SendTmsMsg((U8*)&r, sizeof r);
			break;
		}
		else /*fin && dbu->reporting*/
		{
			if (!atomic_cas(&dbu->traffic, dbu->traffic, dbu->traffic + traffic))
				continue;
			break;
		}
	}
	c->Release(h);
	return 0;
}

int LvldbMgr::OnTrafficReportReply(U32 ip_be)
{
	IpUserTraffic *dbu;
	leveldb::Slice key((char*)&ip_be, sizeof ip_be);
	leveldb::Cache::Handle *h;
	
	lrucache c = cache_tbl[0];
	h = c->Lookup(key);
	if (!h)
	{
		dbu = (IpUserTraffic*)calloc(1, sizeof(IpUserTraffic));
		h = c->Insert(key, (char*)dbu, 1, ::deleter);
	}
	else
	{
		//?
		//h = c->Insert(key, (char*)dbu, 1, ::deleter);
	}
	
	dbu = (IpUserTraffic *)c->Value(h);
	in_addr client_addr;
	client_addr.s_addr = ip_be;

	__sync_synchronize();
	for (; dbu->reporting; )
	{
		//_Pr("dpi received traffic report reply from center, uip=%s", inet_ntoa(client_addr));
		//_Pr("user:%llu|%llu,traffic:%d",dbu->mobile,dbu->app,dbu->traffic);

		if (dbu->traffic > 256 && dbu->fins & (fin_type_c | fin_type_s))
		{
			//continue report
			int to_report = dbu->traffic;
			if (!atomic_cas(&dbu->traffic, to_report, (-MinAckLen)))
				continue;
			
			while(!atomic_cas(&dbu->fins, dbu->fins, 0))
				;
			
			_Pr("[traffic 1] %llu|%llu|%llu", dbu->mobile, dbu->app, to_report + MinAckLen);
			TrafficReport r;
			r.mobile = dbu->mobile;
			r.app    = dbu->app;
			r.ip     = ip_be;
			r.traffic = to_report + MinAckLen;
			gettimeofday(&r.btime, nullptr);
			gettimeofday(&r.etime, nullptr);
			memcpy(r.host, "-", 2);
			r.hlen = 1;
			Connect *c = cm.GetCenterConn();
			c->SendTmsMsg((U8*)&r, sizeof r);
			break;
		}
		else 
		{
			while(!atomic_cas(&dbu->reporting, 1, 0))
				;
			_Pr("[traffic] ok");
			break;
		}
	}
	c->Release(h);
	return 0;
}
