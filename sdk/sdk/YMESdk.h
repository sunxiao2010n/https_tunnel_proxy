#pragma once
#ifndef __YME_SDK_H__
#define __YME_SDK_H__

#include "YMEMessage.h"
#define EVLIST_SIZE 1024

#ifdef __cplusplus
extern "C" {
#endif

	//SDK API
	typedef long YME_HANDLE;
	
	//订单
	struct OrderItem
	{
		int userPackage; 
		char mobile[16];
	};

	//<===YMESdk_Client是封装DMS client用的，TMS不需要client结构
	struct YMESdk_Client
	{
		struct OrderItem* items;
		int item_count;
	
		YME_HANDLE tms_handle;
		char reply_buff[1024];
		unsigned char sign[32];
		char reqid[32];
		char sendid[22];
		char appid[10];
		char mobile[16];
		char mobileip[16];
		unsigned char req_md5[32];
		char token[32];
		char tms_ip[16];
		unsigned int tms_port;
		int resp_code;
	};
	
	
	int YMEInit();
	int YMEStart();
	int YMERunning();
	int YMEDetach();
	int YMEWait();
	int YMEPost();

#ifdef DMS
	int YMESubmitOrder(YMESdk_Client* client);
	int YMEReqToken(YMESdk_Client *c);
#endif

	YME_HANDLE YMELoginTms(unsigned int ip,
			short port,
			U64 app,
			U64 mobile,
			char* auth,
			char* host,
			int host_len,
			U16 web_port,
			U64* aval_traffic);
	
	/*
	 *send and receive message
	 *
	 *@retval       -1:fail；-2:got reply and need to disconnect；0:success
	 *@msg len      data to send
	 *@reply rlen   reply data
	 **/
	int YMESend(YME_HANDLE fd,
			unsigned char* msg,
			size_t len,
			unsigned char** reply,
			size_t* rlen);

	
	/*
	 * free storage buff of connection 'fd'
	 **/
	int YMEFreeBuff(YME_HANDLE fd);

	
	/*
	 * shutdown connection 'fd'
	 * clean all resources which is related with 'fd'
	 **/
	int YMEShutDown(YME_HANDLE fd);
	
	YME_HANDLE YMEReqPage(unsigned int ip,
			short port,
			U64 app,
			U64 mobile,
			char* auth,
			char* host,
			size_t host_len,
			U16 web_port,
			unsigned char* request,
			size_t req_len,
			U64* traffic_cost,
			void (*func)(void*),
			void* arglist,
			unsigned char** reply_addr,
			size_t* reply_len
			);
	
	int SdkMainLoopRun();
	int SdkEventCenterInit();

	#define _Pr(fmt,args...) printf(fmt"\n",##args)
	
	char* YMEGet(char* key);         //"traffic" "auth" ...
	int YMESet(char* k, char* v);    //"phone" "appid" "auth"
	int YMEConnectable();   /*     <==   yme accepts connections when
							             1.     in 5 seconds after YMEConnectable() being called
										 or 2.  YMEConnectable is never called
								   */
	U64 YMERptTraffCost(U64* cost);
	
#ifdef __cplusplus
}

#endif // c++

#endif /*__YME_SDK_H__*/