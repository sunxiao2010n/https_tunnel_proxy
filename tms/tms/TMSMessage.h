#ifndef TMS_MSG
#define TMS_MSG

#include <memory.h>
#include <sys/time.h>
#include "TMSDataTypes.h"

#pragma pack(1)

#define VALIDATER0 0x86
#define VALIDATER1 0x68

struct hdr_t {
	U8 delim0 = VALIDATER0;
	U8 delim1 = VALIDATER1;
	U16 len = 0;
	U16 cid = 0;
} ;

enum  MessageEnum
{
	LOGIN_REQ = 1,
	LOGIN_RSP = 2,
	CONNECT_HOST_REQ = 3, /*预留给连接复用*/
	CONNECT_HOST_RSP = 4, /*预留给连接复用*/
	BEGIN_TRANSMIT = 5,
	READ_TOKEN_REQ = 7,
	READ_TOKEN_RSP = 8,
	TRAFFIC_REPORT = 9,
	TRAFFIC_REPORT_RSP = 10,
	KEEP_ALIVE_TOKEN   = 11,
	SYN_USER_TUPLE     = 12,
};

enum RespCode
{
	SUCC = 0, READY = 0,
	PROG = 1,             /*正在登陆（连接、查询、切换、删除）*/
	USER_NOT_EXIST = 2,
	NO_BALANCE = 3,
	PARAMET_ERR = 4,
	TOKEN_VERIFIED = 5,
	INCORRECT_TOKEN = 6,
	AGENT_ESTASBLISHED = 7,
	AGENT_NOT_CONNECTED = 8,
	HOST_NOT_EXIST = 9,
	FAIL = 0xFFFF,
};

struct MessageID
{
	U16 msg_id;
};

struct LoginReq
{
	U16 msg_id = LOGIN_REQ;
	U64 app = 0;
	U64 mobile = 0;
	U8 token[32] = {0};
	U16 port = 80;
	U16 host_len = 0;
	U8  host[1] = { 0 };
};

struct LoginRsp
{
	U16 msg_id = LOGIN_RSP;
	U16 rsp_code = 0;
	long long avalable_traffic = 0;
	U64 forward_id = 0;
};

struct ConnectHostReq
{
	U16 msg_id = CONNECT_HOST_REQ;
	U16 port = 80;
	U16 host_len = 0;
	U8  host[1] = {0};
};

struct ConnectHostRsp
{
	U16 msg_id = CONNECT_HOST_RSP;
	U16 rsp_code = 0;
	U32 forward_id = 0;
};

struct BeginTransmit
{
	U16 msg_id = BEGIN_TRANSMIT;
	U32 forward_id = 0;
	//U8  data[1] = { 0 };//不定长
};

struct ReadTokenReq
{
	U16 msg_id = READ_TOKEN_REQ;
	U64 cid = 0;  //tms connetion id
	U64 app = 0;
	U64 mobile = 0;
//	U32 src_ip_be = 0;
//	U16 src_port_be = 0;
	U8 token[32] = { 0 };
};

struct ReadTokenRsp
{
	U16 msg_id = READ_TOKEN_RSP;
	U16 rsp_code = 0;
	U64 cid = 0;   //须回传
	U64 app = 0;
	U64 mobile = 0;
	long long balance = 0;
	U8  token[32] = { 0 };
};

struct SynUserTuple
{
	U16 msg_id = SYN_USER_TUPLE;
	U64 app = 0;
	U64 mobile = 0;
	U32 src_ip_be = 0;
	U16 src_port_be = 0;
};

struct TrafficReport
{
	U16 msg_id = TRAFFIC_REPORT;
	U64 app = 0;
	U64 mobile = 0;
	U64 traffic = 0;
	timeval btime = { 0, 0 };
	timeval etime = { 0, 0 };
	U32 ip        = 0;
	U8  hlen = 0;
	U8  host[256] = { 0 };
};

struct TrafficReportRsp
{
	U16 msg_id = TRAFFIC_REPORT_RSP;
	U64 app     = 0;
	U64 mobile  = 0;
	U64 traffic = 0;
	U32 ip      = 0;
	U8  rsp_code = 0;
};

struct KeepAliveToken
{
	U16 msg_id = KEEP_ALIVE_TOKEN;
	U64 app[16] = { 0 };
	U64 mobile[16] = { 0 };
};

#pragma pack()

#endif

