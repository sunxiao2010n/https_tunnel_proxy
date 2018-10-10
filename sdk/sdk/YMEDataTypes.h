#pragma once
#ifndef __YME_DATA_TYPES__
#define __YME_DATA_TYPES__

typedef unsigned char U8;
typedef unsigned short U16;
typedef unsigned U32;
typedef unsigned long long U64;

typedef int(*callback_handler)();

#define _Pr(fmt,args...) printf(fmt"\n",##args)
#include <stdio.h>

constexpr int Tcp_Ack_Len()
{
	return 20 + 20;
}

static int Traffic_Est(int x) {
#ifdef ACCURATE_TRAFFIC
	if (x < 5)
		return x;
	else
		return x + Tcp_Ack_Len() * 2  /*报文 + ack*/
		 + (x / 1400) * Tcp_Ack_Len()   /* 额外分片 + 额外分片的ack(认为额外分片使用sack) */
		;
#else
	return x;
#endif
}

//tcp handshakes' traffic
constexpr int Traffic_TCP_hss()
{
#ifdef ACCURATE_TRAFFIC
	return 7 * Tcp_Ack_Len() + 2 * 20 ;
#else
	return 0;
#endif
}

#endif

