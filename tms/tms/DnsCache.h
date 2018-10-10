#ifndef DNS_CACHE
#define DNS_CACHE

#include <stdlib.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <memory.h>
#include <vector>
#include <map>
#include "TMSDataTypes.h"
#include "ConnectMgr.h"
#include "UserMgr.h"

struct dns_hdr {
	unsigned qid : 16;

#if (defined BYTE_ORDER && BYTE_ORDER == BIG_ENDIAN) || (defined __sun && defined _BIG_ENDIAN)
	unsigned qr : 1;
	unsigned opcode : 4;
	unsigned aa : 1;
	unsigned tc : 1;
	unsigned rd : 1;

	unsigned ra : 1;
	unsigned unused : 3;
	unsigned rcode : 4;
#else
	unsigned rd : 1;
	unsigned tc : 1;
	unsigned aa : 1;
	unsigned opcode : 4;
	unsigned qr : 1;

	unsigned rcode : 4;
	unsigned unused : 3;
	unsigned ra : 1;
#endif

	unsigned qdcount : 16;
	unsigned ancount : 16;
	unsigned nscount : 16;
	unsigned arcount : 16;
}; /* struct dns_header */
	
#pragma package(2)
struct dns_ans_hdr
{
	uint16_t _nm;
	uint16_t _type;
	uint16_t _cls;
	uint16_t _ttl1;    // if using uint32, compiler will pad struct.
	uint16_t _ttl2;
	uint16_t _datalen;
};
#pragma package()

struct Rep
{
	std::string _host;
	U32 _ip = 0;
};
struct TmsDnsReplys
{
	std::vector<Rep> _r;//一条应答
};

int req_fill(U8* buff, const U8* const host, const size_t hostlen);
int res_parse(U8* buff, TmsDnsReplys &replys);

class DnsCache
{
public:
	DnsCache();
	std::map<std::string, U32> _Cache;
	
	//有一个超时，都视为超时
	typedef std::tuple<UserId, U64>  USCBId;
	std::multimap<std::string, UserId> _WaitList;
	std::multimap<std::string, USCBId> _waiting_scb;
	std::multimap<time_t, USCBId> _expiring_scb;
	LoginRsp dns_fail_rsp;

	void AddWaitLists(const std::string &s, SCB* scb);
	void InformClients(const std::string &s, U64 ip);
	void ExpireWaitLists();
	
	inline void Update(TmsDnsReplys& replys)
	{
		for (auto &r : replys._r)
			_Cache[r._host] = r._ip;
	}
	static DnsCache& Instance()
	{
		static DnsCache t;
		return t;
	}
};


#endif