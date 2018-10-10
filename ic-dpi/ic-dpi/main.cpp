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
#include <thread>

#define CENTER     "127.0.0.1"
#define CENTERPORT  10170
#define DPI_IP      INADDR_ANY
#define DPI_PORT    10160

#include "PcapModule.h"
#include "ConnectMgr.h"
#include "LvldbMgr.h"
static ConnectMgr& cm = ConnectMgr::Instance();
static LvldbMgr&  lm = LvldbMgr::Instance();


int dpi_run()
{	
	lm.Init();
	
	U64 m_socket;
	int ret;
	//create receive socket
	if((m_socket = socket(AF_INET, SOCK_STREAM, 0)) < 0)
	{
		printf("socket create ret code %d, err %d", m_socket, errno);
		return -1;
	}

	//set socket reuse addr
	int opt = 1;
	socklen_t len = sizeof(opt);
	setsockopt(m_socket, SOL_SOCKET, SO_REUSEADDR, &opt, len);

	nonblocking(m_socket);

	struct sockaddr_in dpi_addr;
	dpi_addr.sin_family = AF_INET;
	dpi_addr.sin_port = htons(DPI_PORT);
	dpi_addr.sin_addr.s_addr = htonl(DPI_IP);
	memset(&dpi_addr.sin_zero, 0, sizeof dpi_addr.sin_zero);
	
	ret = bind(m_socket, (struct sockaddr *)&dpi_addr, sizeof dpi_addr);
	if (ret < 0) {
		printf("socket bind ret code %d, err %d", ret, errno);
		return -1;
	}

	if (listen(m_socket, 3) < 0) {
		perror("listen");
		return -1;
	}

#define EP_EVLIST_SIZE 1024
	int epfd = epoll_create(1024);
	if (epfd < 0) {
		perror("epoll create");
		return -1;
	}

	cm._Epoll_FD = epfd;

	struct epoll_event ev;
	memset(&ev, 0, sizeof ev);
	ev.events = EPOLLIN | EPOLLET;
	ev.data.fd = m_socket;
	if (epoll_ctl(epfd, EPOLL_CTL_ADD, m_socket, &ev) == -1) {
		_Pr("epoll error, err[%d]", errno);
		return -1;
	}
	
	U64 center_fd;
	Connect* retc = cm.CreateCenterConn(center_fd, inet_addr(CENTER), CENTERPORT);
	if (!retc)
	{
		_Pr("fail creating Center connection,err=%d", errno);
		return -1;
	}
	cm.AddEvent(cm._Epoll_FD, center_fd, EPOLLIN | EPOLLET);
	

	struct epoll_event *ev_list;
	ev_list = (struct epoll_event*)malloc(EP_EVLIST_SIZE * sizeof(struct epoll_event));
	int evcount, readsize, err;
	Connect *c;
	int i;
	int currfd;

	while (1) {

		evcount = epoll_wait(cm._Epoll_FD, ev_list, EP_EVLIST_SIZE, 1000);

		//std::cout << "new event" << std::endl;

		for(i = 0 ; i < evcount ; i++)
		{
			U64 events = ev_list[i].events;
			
			if (ev_list[i].events & EPOLLIN)
			{
				if (ev_list[i].data.fd == m_socket) {
					currfd = cm.HandleAcceptEvent(m_socket);
					if (currfd > 0) {
						nonblocking(currfd);
						cm.AddEvent(cm._Epoll_FD, currfd, EPOLLIN | EPOLLOUT | EPOLLET);
					}
				}
				else {
					currfd = ev_list[i].data.fd;
					int ret;
					do {
						ret = cm.HandleRecvEvent(currfd);
						if (ret == -2) goto NextEvent; 
						else if (ret == -1)
						{
							if (errno == EWOULDBLOCK || errno == EAGAIN)
								goto NextEvent;
							else if (ev_list[i].events & EPOLLERR)
							{
								_Pr("failed to connect web server, fd=%d, errno=%d", currfd, errno);
								cm.RemoveConn(currfd);
								close(currfd);
								goto NextEvent;
							}
							else
							{
								_Pr("exception close connection,fd=%d,err=%d", ev_list[i].data.fd, errno);
								cm.RemoveConn(ev_list[i].data.fd);
							}
							goto NextEvent;
						}
						else if (ret == 0)
						{
							if (err != EINTR)
							{
								_Pr("close connection,fd=%d,%d", ev_list[i].data.fd, errno);
								cm.RemoveConn(ev_list[i].data.fd);
								
								//删除相关连接
								goto NextEvent;
							}
							break;
						}
						else
						{
							_Pr("new HandleRecvEvent event");
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
					ret = cm.HandleEPOEvent(currfd);
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
	
//	lm.OnTrafficReport(1, 1, 0);
//	lm.OnTrafficReport(1, 1, 1);
	return 0;
}


const char* dev;
in_addr_t tms_addr;

/*
 * 启动程序
 * 
 * 正确的打开方式
 * icdpi [dev] [tmsip]
 **/

int main(int argc, char** argv)
{
	if (argc < 3)
	{
		dev = "eno16777736";
		tms_addr = inet_addr("192.168.7.147");
	}
	else
	{
		dev = argv[1];
		tms_addr = inet_addr(argv[2]);
	}
	
	int length;
	int ret;
	int currfd, i;
	setbuf(stdout, NULL);
	signal(SIGPIPE, SIG_IGN);

	std::thread t1(&dpi_run);
	t1.detach();
	sleep(1);
	
	std::thread t2(&pcap_run);
	t2.detach();

	while (1)
		sleep(20);
	return 0;
}