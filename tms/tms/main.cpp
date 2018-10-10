#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <memory.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>

#include <iostream>
#include <thread>

#include "TMSDataTypes.h"
#include "ConnectMgr.h"
#include "UserMgr.h"

#define DNS_SERVER "114.114.114.114"
#define CENTER     "127.0.0.1"
#define CENTERPORT  10170
#define DPI_IP     "127.0.0.1"
#define DPI_PORT    10160

ConnectMgr& g_ConnMgr = ConnectMgr::Instance();
static UserMgr&    um = UserMgr::Instance();

int count;
int m_socket, s_socket;
short TcpRcvPort = 10180;
short UdpSndPort = 1813;
short UdpToPort = 1812;
struct sockaddr addr_to;
struct sockaddr_in addr_to_in[3];
struct sockaddr_in addr_from_in[2];
char* ip_str_to[] = {
	"172.16.211.22",
	"172.16.211.11",
	"192.168.1.1"
};
int which_sender = 2;

#define nonblocking(s)  fcntl(s, F_SETFL, fcntl(s, F_GETFL) | O_NONBLOCK)
#define blocking(s)     fcntl(s, F_SETFL, fcntl(s, F_GETFL) & ~O_NONBLOCK)

int init(){

	addr_from_in[0].sin_family = AF_INET;
	addr_from_in[0].sin_port = htons(TcpRcvPort);
	addr_from_in[0].sin_addr.s_addr = htonl(INADDR_ANY);
	memset(&addr_from_in[0].sin_zero, 0, sizeof addr_from_in[0].sin_zero);

	addr_from_in[1].sin_family = AF_INET;
	addr_from_in[1].sin_port = htons(UdpSndPort);
	addr_from_in[1].sin_addr.s_addr = htonl(INADDR_ANY);
	memset(&addr_from_in[1].sin_zero, 0, sizeof addr_from_in[1].sin_zero);
}

std::thread g_thread_t2;

void T2LoopRun()
{
	while (1)
	{
		sleep(60);
		um.IncreaseExpireTime();
	}
}

int T2Init()
{
	std::thread t2(&T2LoopRun);
	g_thread_t2 = std::move(t2);
	_Pr("thread 2 started");
}

int sender_create(int index){

	s_socket = socket(AF_INET, SOCK_DGRAM, 0);
	if (s_socket<0){
		_Pr("fail to create sender socker");
		return -1;
	}
	int on = 1;
	setsockopt(s_socket, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

	nonblocking(s_socket);

	int ret = bind(s_socket, (struct sockaddr *)&addr_from_in[index], sizeof addr_from_in[0]);
	if (ret<0){
		printf("socket bind ret code %d, err %d", ret, errno);
		return -1;
	}
	return 0;
}


int main(int argc, char** argv)
{
	if (argc == 2)
	{
		TcpRcvPort = atoi(argv[1]);
	}
	
	int length;
	int ret;
	int i;
	U64 currfd;
	setbuf(stdout, NULL);
	signal(SIGPIPE, SIG_IGN);

	init();
	//T2Init();

	//create receive socket
	if ((m_socket = socket(AF_INET, SOCK_STREAM, 0)) < 0)
	{
		printf("socket create ret code %d, err %d", m_socket, errno);
		return -1;
	}

	//set socket reuse addr
	int opt = 1;
	socklen_t len = sizeof(opt);
	setsockopt(m_socket, SOL_SOCKET, SO_REUSEADDR, &opt, len);

	nonblocking(m_socket);

	ret = bind(m_socket, (struct sockaddr *)&addr_from_in[0], sizeof addr_from_in[0]);
	if (ret<0){
		printf("socket bind ret code %d, err %d", ret, errno);
		return -1;
	}

	if (listen(m_socket, 3)<0){
		perror("listen");
		return -1;
	}

#define EP_EVLIST_SIZE 1024
	int epfd = epoll_create(1024);
	if (epfd<0) {
		perror("epoll create");
		return -1;
	}

	g_ConnMgr._Epoll_FD = epfd;

	struct epoll_event ev, *ev_list;
	memset(&ev, 0, sizeof ev);
	ev.events = EPOLLIN | EPOLLET;
	ev.data.fd = m_socket;

	ev_list = (struct epoll_event*)malloc(EP_EVLIST_SIZE * sizeof(struct epoll_event));

	if (epoll_ctl(epfd, EPOLL_CTL_ADD, m_socket, &ev) == -1){
		_Pr("epoll error, err[%d]", errno);
		return -1;
	}
	
	U64 dns_socket_fd, center_fd, dpi_fd;
	Connect* retd = g_ConnMgr.CreateDnsConn(dns_socket_fd, inet_addr(DNS_SERVER));
	if (!retd)
	{
		_Pr("fail creating Dns connection,err=%d",errno);
		return -1;
	}
	g_ConnMgr.AddEvent(g_ConnMgr._Epoll_FD, dns_socket_fd, EPOLLIN | EPOLLET);
	
	Connect* retc = g_ConnMgr.CreateCenterConn(center_fd, inet_addr(CENTER), CENTERPORT);
	if (!retc)
	{
		_Pr("fail creating Center connection,err=%d", errno);
		return -1;
	}
	g_ConnMgr.AddEvent(g_ConnMgr._Epoll_FD, center_fd, EPOLLIN | EPOLLET);
	
	Connect* reti = g_ConnMgr.CreateDpiConn(dpi_fd, inet_addr(DPI_IP), DPI_PORT);
	if (!reti)
	{
		_Pr("fail creating Dpi connection,err=%d", errno);
		return -1;
	}
	g_ConnMgr.AddEvent(g_ConnMgr._Epoll_FD, dpi_fd, EPOLLIN | EPOLLET);

	int evcount, readsize, err;
	Connect *c;
	while (1){

		evcount = epoll_wait(epfd, ev_list, EP_EVLIST_SIZE, -1);

		//std::cout << "new event" << std::endl;

		for (i = 0; i<evcount; i++)
		{
			U64 events = ev_list[i].events;
			
			if (ev_list[i].events & EPOLLIN)
			{
				if (ev_list[i].data.fd == m_socket){
					currfd = g_ConnMgr.HandleAcceptEvent(m_socket);
					if (currfd>0) {
						nonblocking(currfd);
						g_ConnMgr.AddEvent(g_ConnMgr._Epoll_FD, currfd, EPOLLIN | EPOLLOUT | EPOLLET);
					}
				}
				else{
					currfd = ev_list[i].data.fd;
					int ret;
					do{
						ret = g_ConnMgr.HandleRecvEvent(currfd);
						if (ret == -2) goto NextEvent; 
						else if (ret == -1)
						{
							if (errno == EWOULDBLOCK || errno == EAGAIN)
								goto NextEvent;
							else if (ev_list[i].events & EPOLLERR)
							{
								_Pr("failed to connect web server, fd=%d, errno=%d", currfd, errno);
								g_ConnMgr.InformShutdownUser(currfd);
								g_ConnMgr.RemoveConn(currfd);
								close(currfd);
								goto NextEvent;
							}
							else
							{
								_Pr("exception close connection,fd=%d,err=%d",ev_list[i].data.fd,errno);
								g_ConnMgr.RemoveConn(ev_list[i].data.fd);
							}
							goto NextEvent;
						}
						else if (ret == 0)
						{
							if (err != EINTR)
							{
								_Pr("close connection,fd=%d,%d", ev_list[i].data.fd, errno);
								g_ConnMgr.RemoveConn(ev_list[i].data.fd);
								
								//删除相关连接
								goto NextEvent;
							}
							break;
						}
						else
						{
							std::cout << "new HandleRecvEvent event" << std::endl;
						}
					} while (err == EINTR);
				}
			}
			if (ev_list[i].events & EPOLLOUT)
			{
				//这种情形是tms主动建立web连接
				currfd = ev_list[i].data.fd;
				
				socklen_t len = sizeof(err);
				int retval = getsockopt(currfd, SOL_SOCKET, SO_ERROR, &err, &len);
				
				if (err == 0 || err == EAGAIN)
				{
					ret = g_ConnMgr.HandleEPOEvent(currfd);
					if (ret == -1)
					{
						_Pr("exception close current fd %d", currfd);
						shutdown(currfd, SHUT_RDWR);
					}

				}
				if (ret != -2)
					//ret==-2是一次多余的通知，可优化
					continue;
			}
NextEvent:
			;
		}
	}
}



