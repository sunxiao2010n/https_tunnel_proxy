#pragma once
#ifndef pcap_module_h
#define pcap_module_h

#include <pcap/pcap.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include "TMSDataTypes.h"

enum FinType
{
	fin_type_n = 0,
	fin_type_c = 1,
	fin_type_s = 1<<1,
	fin_type_r = 1<<2,
	fin_type_err = 1<<7
};

#define MinAckLen 40

static inline volatile int 
atomic_cas_x86(volatile int *lock, volatile int  old, volatile int  set)
{
	u_char  res;
	__asm__ volatile(
		
	 "lock;"
	 "    cmpxchgl  %3, %1;   "
	 "    sete      %0;       "

	 : "=a" (res) : "m" (*lock),
		"a" (old),
		"r" (set) : "cc",
		"memory");

	return res;
}

//static inline volatile bool atomic_cas(volatile int *lock, volatile int old, volatile int set)
//{
//	return __sync_bool_compare_and_swap(lock, old, set);
//}
#define atomic_cas __sync_bool_compare_and_swap

pcap_t *dpi_pcap_init(const char *intf,const char *filter,pcap_direction_t direction,int snaplen);

void dpi_packet_process(U8 *arg, const struct pcap_pkthdr *pkthdr, const U8 *packet);

void pcap_run();

#endif // !pcap_module_h
