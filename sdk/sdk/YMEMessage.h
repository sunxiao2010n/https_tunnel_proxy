#pragma once
#ifndef __YME_MESSAGE__
#define __YME_MESSAGE__

#include <netinet/in.h>
#include "YMEDataTypes.h"

#define VALIDATER0 0x86
#define VALIDATER1 0x68

#pragma pack(1)
struct hdr_t {
	U8 delim0 = VALIDATER0;
	U8 delim1 = VALIDATER1;
	U16 len = 0;
	U16 cid = 0;
} ;

//tms comm interface
enum  MessageEnum
{
	LOGIN_REQ        = 1,
	LOGIN_RSP        = 2,
	CONNECT_HOST_REQ = 3,
	CONNECT_HOST_RSP = 4,
	BEGIN_TRANSMIT   = 5,
	READ_TOKEN_REQ   = 7,
	READ_TOKEN_RSP   = 8,
	TRAFFIC_REPORT   = 9,
	C_ACCQ_TOKEN_REQ = 101,
	C_ACCQ_TOKEN_RSP = 102,
};

enum  ReplyType
{
	RT_ServerNotUp      = -10,
	RT_HostNotExist     = -9,
	RT_UserNotExist     = -8,
	RT_IncorrectToken   = -7,
	RT_NoBalance        = -6,
	RT_RemoteFinish     = -5,
	RT_Exception        = -4,
	RT_RequestExpired   = -3,
	RT_DownWithData     = -2,
	RT_Down             = -1,
	RT_Data             = 0,
};

enum RespCode
{
	SUCC = 0,
	PROG = 1,
	USER_NOT_EXIST = 2,
	NO_BALANCE = 3,
	PARAMET_ERR = 4,
	INCORRECT_TOKEN = 6,
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
	U8 token[32] = { 0 };
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
	U8  host[1] = { 0 };
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
};

struct ReadTokenReq
{
	U16 msg_id = READ_TOKEN_REQ;
	U64 cid = 0;   //tms connetion id
	U64 app = 0;
	U64 mobile = 0;
	U8 token[32] = { 0 };
};

struct ReadTokenRsp
{
	U16 msg_id = READ_TOKEN_RSP;
	U16 rsp_code = 0;
	U64 cid = 0;
	U64 app = 0;
	U64 mobile = 0;
	long long balance = 0;
	U8  token[32] = { 0 };
};

struct TrafficReport
{
	U16 msg_id = TRAFFIC_REPORT;
	U64 app = 0;
	U64 mobile = 0;
	U64 traffic = 0;
	U64 btime = 0;
	U64 etime = 0;
	U8  hlen = 0;
	U8  host[256] = { 0 };
};

//dsp comm interface   --deprecated
struct CAccquireTokenReq
{
	hdr_t hdr;
	U16 msg_id = C_ACCQ_TOKEN_REQ;         //101
	U8  app[32] = { 0 };
	U8	pass[32] = { 0 };
	U8  padding[128 - sizeof hdr - 2 - 64] = { 0 };
};

struct CAccquireTokenRsp
{
	hdr_t hdr;
	U16 msg_id_le = C_ACCQ_TOKEN_RSP;      //101;
	U16 rsp_code  = 0;
	U8  user[32]  = { 0 };
	U8	token[32] = { 0 };
	U32 ip_le     = { 0 };
	U16 port_le   = 0;
	U8  padding[128 - sizeof hdr - 4 - 64 - 6];
};

struct UserKeepAlive
{
	hdr_t hdr;
	U64 app[16] = { 0 };
	U64 mobile[16] = { 0 };
};

#pragma pack()

int constexpr DspMsgLen()
{
	return sizeof(struct CAccquireTokenRsp);
}

#endif