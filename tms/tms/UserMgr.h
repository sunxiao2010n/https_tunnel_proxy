#ifndef USER_MGR
#define USER_MGR

#include "TMSMessage.h"
#include "SCB.h"
#include <limits.h>
#include <list>
#include <vector>
#include <unordered_map>

enum User_stat
{
	u_invalid = 0,
	u_validated,
	u_nobalance,
};

class Connect;

class User
{
public:
	U64 mobile = 0;
	U64 app = 0;
	U64 forward_id = 0;
	U64 traffic = 0;
	long long balance = 0;
	timeval accu_start_time = { LONG_MAX, LONG_MAX };
	U8 host[256] = { 0 };
//	Connect *tunnel_conn = nullptr;
//	Connect *agent_conn = nullptr;
	U8  status = 0;   //
	U8  reporting = 0;
	U16 con_report = 0;   //concurrent reporting
	std::map<U64,SCB*> scb_tree;
	void ResetUser();
	SCB* CreateSCB(Connect *tunnel, Connect* agent);
	void ReleaseSCB(SCB* scb);
	int HandleRecvMsg(U8* msg, U32 msglen);
	SCB* ConnectWebServer(Connect* tunnel, U64 ip, U16 port);
	int TransferDataToWeb();
	int TransferDataToSDK();
	int RecordTraffic(SCB* scb, U64 datalen);
	int ReportTraffic(SCB* scb);
	int ReportOrAccumulate(SCB* scb);
	int ReportAccumulated();
	
	void ReleaseSCBList();
};

class UserMgr
{
public:
	std::map<UserId, User*> _UserMap;
	U64 aval_forward_id = 1;
	std::list<User*> _ExpireList[RETRY_TIMES];
	inline U64 NewForwardId()
	{
		if (aval_forward_id == ULONG_MAX)
			aval_forward_id = 1;
		return aval_forward_id++;
	}
	static UserMgr& Instance()
	{
		static UserMgr t;
		return t;
	}
	User* ResetUser(UserId& app, Connect* tc, Connect* ac = nullptr);
	int TraceUser(User* u);
	int ExpireLocalUser();       //T2:thread 2
	int IncreaseExpireTime();        //T2:thread 2
	User*  RetrieveUser(UserId &id);
};

#endif