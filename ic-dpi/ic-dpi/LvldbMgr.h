#pragma once
#ifndef  LvldbMgr_h
#define  LvldbMgr_h

#include <cassert>
#include "leveldb/db.h"
#include "leveldb/cache.h"
#include "leveldb/slice.h"

#include "TMSDataTypes.h"
#include "TMSMessage.h"
#include <vector>

struct IpUserTraffic
{
	U32 ip_be;
	volatile int reporting;
	volatile int fins;
	volatile int traffic;
	U64 mobile;
	U64 app;
};

typedef leveldb::Cache* lrucache;

class LvldbMgr
{
public:
	static LvldbMgr& Instance()
	{
		return inst;
	}
	lrucache cache_slice(int x=0)
	{
		return cache_tbl[x];
	}
	int Init();
	int OnUserLogin(SynUserTuple *u);
	int	OnTrafficReport(U32 ip_be, U64 traffic, int fin_type);
	int OnTrafficReportReply(U32 ip_be);
private:
	static LvldbMgr inst;
	std::vector<lrucache> cache_tbl;
	static const int hash_capacity = 100000;
};

#endif // ! dpi_traffic_h
