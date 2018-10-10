#pragma once
#ifndef __Recycler__
#define __Recycler__

#include <sys/time.h>
#include "YMEDataTypes.h"
#include <list>
class Connect;

struct ConnRecycleBlock
{
	timeval tv = { 0, 0 };
	U64 fd = 0;
	U64 traffic = 0;
	Connect *conn = nullptr;
	void Reset()
	{
		tv = { 0, 0 };
		fd = 0;
		conn = nullptr;
		traffic = 0;
	}
	void Delay()
	{
		gettimeofday(&tv, nullptr);
	}
	static void CRBNew(std::list<ConnRecycleBlock*>::iterator& it);
	static void CRBDelay(std::list<ConnRecycleBlock*>::iterator& it);
	static void CRBDelete(std::list<ConnRecycleBlock*>::iterator it);
	static void CRBScan();
	static std::list<ConnRecycleBlock*> expire_list;
	static std::list<ConnRecycleBlock*> recycle_list;
};

#endif