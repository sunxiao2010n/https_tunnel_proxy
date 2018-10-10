#include "DnsCache.h"
#include "SCB.h"


DnsCache::DnsCache()
{
	U32 leip = inet_addr("127.0.0.1");
	_Cache[std::string("localhost")] = htonl(leip);
	U32 alip = inet_addr("10.24.171.135");
	_Cache[std::string("aliyuntest")] = htonl(alip);
	U32 icloudata = inet_addr("10.25.231.165");
	_Cache[std::string("icloudata")] = htonl(icloudata);

	dns_fail_rsp.rsp_code = HOST_NOT_EXIST;
}

void DnsCache::AddWaitLists(const std::string &s, SCB* scb)
{
	UserId uid(scb->user->mobile, scb->user->app);
	U64 sid = scb->scbid;
	time_t tt = scb->last_active_time.tv_sec;
	_waiting_scb.insert(std::pair<std::string, USCBId>(s, USCBId(uid, sid)));
	_expiring_scb.insert(std::pair<time_t, USCBId>(tt, USCBId(uid, sid)));
}

void DnsCache::InformClients(const std::string &s, U64 ip)
{
	static SCBMgr& scm = SCBMgr::Instance();
	static UserMgr& um = UserMgr::Instance();
	auto start = _waiting_scb.lower_bound(s);
	auto end = _waiting_scb.upper_bound(s);
	int InformCount = 0;

	for (auto iter = start; iter != end; iter++)
	{
		InformCount++;
		USCBId &id = iter->second;
		UserId &iuid = std::get<0>(id);
		U64    &iscbid = std::get<1>(id);
		auto u = um._UserMap.find(iuid);
		if (u == um._UserMap.end())
			_Pr("user not exist");
		else
		{
			User *user = u->second;
			std::map<U64, SCB*>& scbt = user->scb_tree;
			auto sc = scbt.find(iscbid);
			if (sc == scbt.end())
				_Pr("stream control block not exist");
			else
			{
				SCB *scb = sc->second;
				if (!scb)
					continue;
				
				SCB* s;
				if (ip)
				{
					U16 port = scb->dest_port;
					//
					if(!scb->tunnel_conn)
						continue;
					s = user->ConnectWebServer(scb->tunnel_conn, ip, port);
				}
				if(!ip || !s)
					scb->tunnel_conn->SendTmsMsg((U8*)&dns_fail_rsp, sizeof dns_fail_rsp);
			}
		}
	}
	_Pr("inform count %d", InformCount);
	_waiting_scb.erase(s);
	return ;
}

void DnsCache::ExpireWaitLists()
{
	timeval tv;
	gettimeofday(&tv, nullptr);
	time_t tvs = tv.tv_sec;
	
	decltype(_expiring_scb)::iterator iter = _expiring_scb.begin();
	while(iter != _expiring_scb.end())
	{
		if (iter->first < tvs - 5)
		{
			//5 seconds
			
		}
		else 
			break;
		
		_expiring_scb.erase(iter);
		iter = _expiring_scb.begin();
	}
	
	return;
}

int req_fill(U8* buff, const U8* const host, const size_t hostlen)
{
	dns_hdr* start = (dns_hdr*)buff;
	if (!start)
		return -1;
	
	memset(buff, 0, sizeof(dns_hdr));
	srand(clock());
	int rv = random();
	start->qid = (rv >> 16) ^ (rv | 0xffff);
	start->rd = 1;
	start->qdcount = htons(1);

	U8* data_curr = (U8*)(start + 1);
	//put host
	int section_start = 0;
	for (int i = 0; i < hostlen; i++)
	{
		if (host[i] == '.')
		{
			*data_curr = i - section_start;
			memcpy(data_curr + 1, host + section_start, i - section_start);
			data_curr += i - section_start + 1;
				
			section_start = i + 1;
		}
	}
	*data_curr = hostlen - section_start;
	memcpy(data_curr + 1, host + section_start, hostlen - section_start + 1);
	data_curr += hostlen - section_start + 1;
	*(data_curr++) = 0;
		
	//put
	*(data_curr++) = 0x0;
	*(data_curr++) = 0x1;
	*(data_curr++) = 0x0;
	*(data_curr++) = 0x1;
		
	return data_curr - buff;//整个报文长
}

int res_parse(U8* buff, TmsDnsReplys &replys)
{
	int hostlen = 0;
	dns_hdr* start = (dns_hdr*)buff;
	if (!start)
		return -1;
	
	int questions = ntohs(start->qdcount);
	int answers = ntohs(start->ancount);

	U8* data_curr = (U8*)(start + 1);
	//read host
	int section_start = 0;
	int section_length = 0;
	
	for (int j = 0; j < questions; j++)
	{
		Rep reply;
		reply._ip = 0;
		std::string s;
		do
		{
			section_length = data_curr[0];
			data_curr++;
			if (section_length == 0) 
				break;
			else
			{
				if (!s.empty())
					s.append(1, '.');
				s.insert(s.end(), data_curr, data_curr + section_length);
			}
			data_curr += section_length;
		} while (1);

		data_curr += 4;
		reply._host = s;
		replys._r.push_back(reply);
		
	}
	
	for (int k = 0; questions > 0 && k < answers;)
	{
		dns_ans_hdr* ans = (dns_ans_hdr*)data_curr;
		int len = ntohs(ans->_datalen);
		static const int dns_rhsize = sizeof(dns_ans_hdr);
		data_curr += dns_rhsize;
		if (ntohs(ans->_type) != 1)
		{
			data_curr += len;
			continue;
		}

		if (len == 4)
		{
			U32 ipaddr = *(int*)data_curr;
			
			//todo:
			//dns请求返回地址优化
			replys._r[0]._ip = ntohl(ipaddr);
			break;
		}
		else 
		{
			_Pr("error dns parse: ans data len [%d] not implemented", len);
		}

		data_curr += len;
		k++;
	}

	return 0;
}
