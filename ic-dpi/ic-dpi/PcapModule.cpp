#include "PcapModule.h"
#include "LvldbMgr.h"

/*
 * Dpi module tms
 * 20180517 sunxiao 
 * current version weakness:
 *     1.lookup hash table on each packet arrive
 *     2.if concurrent, multiple insert of hash table
 *
 * 风险点：
 *     3.用32位有符号整数记录流量，2G流量以后溢出（高风险）
 *     
 * optimize the above by the kind of you
 * 
 **/

const U16 tms_port = 10180;
extern in_addr_t tms_addr;
extern const char* dev;
char* DpiDeployInterface() {return (char*)dev;}
char* DpiDeployFilter()    {return (char*) "tcp port 10180";}

static LvldbMgr&  lm = LvldbMgr::Instance();

pcap_t *dpi_pcap_init(const char *intf,
	const char *filter,
	pcap_direction_t direction,
	int snaplen)
{

	pcap_t *handle;
	bpf_u_int32 network, netmask;
	struct bpf_program fcode;
	char errbuf[PCAP_ERRBUF_SIZE];

	if (intf == NULL && (intf = pcap_lookupdev(errbuf)) == NULL) {
		return NULL;
	}

	if ((handle = pcap_open_live(intf, snaplen, 1, snaplen, errbuf)) == NULL) {
		return NULL;
	}

	if (pcap_setdirection(handle, direction) == -1) {
		return NULL;
	}
	if (pcap_lookupnet(intf, &network, &netmask, errbuf) == -1) {
		_Pr("pcap_lookupnet: %s", errbuf);
		// return NULL;
	}
	if (pcap_compile(handle, &fcode, filter, 1 /* optimize */, netmask) < 0) {
		return NULL;
	}

	if (pcap_setfilter(handle, &fcode) == -1) {
		return NULL;
	}

	return handle;
}

void pcap_run()
{
	const char *device = DpiDeployInterface();
	const char *pcap_filter = DpiDeployFilter();
	pcap_direction_t direction = (pcap_direction_t)(PCAP_D_IN | PCAP_D_OUT);
	const int pcap_timeout = 1000;

	pcap_t *handler = dpi_pcap_init(device, pcap_filter, direction, pcap_timeout);
	if (!handler)
	{
		_Pr("pcap init fail");
		return;
	}

	int ret = pcap_loop(handler, -1, dpi_packet_process, NULL);

	if (ret == -1)
		_Pr("pcap_loop: %s", pcap_geterr(handler));
}

void dpi_packet_process(U8 *arg, const struct pcap_pkthdr *pkthdr, const U8 *packet) {
	//_Pr("packet len %d", pkthdr->len);
	
#define DPI_GET_SRC_IP(packet)  (((struct ip*)packet)->ip_src.s_addr)
#define DPI_GET_DST_IP(packet)  (((struct ip*)packet)->ip_dst.s_addr)
#define DPI_GET_TCP_HEAD(packet)    (packet + ((struct ip*)packet)->ip_hl * 4)
#define DPI_GET_SRC_PORT(p_tcp_hdr)  (((struct tcphdr*)p_tcp_hdr)->source)
#define DPI_GET_DST_PORT(p_tcp_hdr)  (((struct tcphdr*)p_tcp_hdr)->dest)
#define DPI_GET_FIN(p_tcp_hdr)  (((struct tcphdr*)p_tcp_hdr)->fin)
#define DPI_GET_RST(p_tcp_hdr)  (((struct tcphdr*)p_tcp_hdr)->rst)
	
	const U8 *eth_hdr = packet;
	const U8 *ip_hdr = packet + 14;
	const U8 *tcp_hdr = DPI_GET_TCP_HEAD(ip_hdr);
	
	in_addr src_addr, dst_addr;
	src_addr.s_addr = DPI_GET_SRC_IP(ip_hdr);
	dst_addr.s_addr = DPI_GET_DST_IP(ip_hdr);
	
	U16 sport, dport;
	U8  fin, rst, fin_type = 0;
	sport = DPI_GET_SRC_PORT(tcp_hdr);
	dport = DPI_GET_DST_PORT(tcp_hdr);
	fin   = DPI_GET_FIN(tcp_hdr);
	rst   = DPI_GET_RST(tcp_hdr);
	
	char * ip = inet_ntoa(src_addr);
	
	in_addr client_addr;
	
	if (src_addr.s_addr == tms_addr)
	{
		client_addr.s_addr = dst_addr.s_addr;
		if (fin)
			fin_type = fin_type_s;
	}
	else
	{
		client_addr.s_addr = src_addr.s_addr;
		if (fin)
			fin_type = fin_type_c;
	}
	if (rst)
		fin_type = fin_type_r;
	
	int payload_len = TelecomTrafficPredict(pkthdr->len - 14 - Tcp_Ack_Len());
	//_Pr("packet ip %s, client_ip=%s, len = %d", ip, inet_ntoa(client_addr), pkt_true_len);
	lm.OnTrafficReport(client_addr.s_addr, payload_len, fin_type);

	return;
}
