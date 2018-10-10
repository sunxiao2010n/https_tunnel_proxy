#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <memory.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/time.h>
#ifdef __APPLE__
#include <sys/event.h>
#else
#include <sys/epoll.h>
#endif
#include <fcntl.h>
#include <unistd.h>

#include <iostream>

#include "YMESdk.h"
#include "YMEDataTypes.h"
#include "ConnectMgr.h"
#include "Recycler.h"

#ifdef __APPLE__
struct kevent* ev_list;
#else
struct epoll_event *ev_list;
#endif
ConnectMgr& g_ConnMgr = ConnectMgr::Instance();

#define nonblocking(s)  fcntl(s, F_SETFL, fcntl(s, F_GETFL) | O_NONBLOCK)
#define blocking(s)     fcntl(s, F_SETFL, fcntl(s, F_GETFL) & ~O_NONBLOCK)

timeval AllowBeginTime = {0,0};
extern int pipe_fd[2];
const static U8 init_ok=0, init_fail=-1;

int SdkEventCenterInit()
{
	int kqfd;
#ifdef __APPLE__
	kqfd = kqueue();
#else
	kqfd = epoll_create(EVLIST_SIZE);
#endif
	if (kqfd < 0) {
		std::string e("event center fd create error. ");
		e += strerror(errno);
		_Pr("%s", e.c_str());
		YMESet("Error",(char*)e.c_str());
		write(pipe_fd[1], &init_fail, 1);
		return -1;
	}

	g_ConnMgr._Reactor_FD = kqfd;

#ifdef __APPLE__
	ev_list = (struct kevent*)malloc(EVLIST_SIZE * sizeof(struct kevent));
#else
	struct epoll_event ev;
	ev_list = (struct epoll_event*)malloc(EVLIST_SIZE * sizeof(struct epoll_event));
#endif
	if (!ev_list)
	{
		std::string e("no free heap space");
		_Pr("%s", e.c_str());
		YMESet("Error", (char*)e.c_str());
		write(pipe_fd[1], &init_fail, 1);
		return -1;
	}

	return 0;
}

int SdkMainLoopRun()
{
#define AGENT_PORT  23515
#define AGENT_SPORT 23516
	int length;
	int ret;
	int currfd, i;

	int evcount, readsize;
	Connect *c;
	int kqfd = g_ConnMgr._Reactor_FD;
	
	//open local port
	struct sockaddr_in addr_from_in[2];
	addr_from_in[0].sin_family = AF_INET;
	addr_from_in[0].sin_port = htons(AGENT_PORT);
	addr_from_in[0].sin_addr.s_addr = htonl(INADDR_ANY);
	memset(&addr_from_in[0].sin_zero, 0, sizeof addr_from_in[0].sin_zero);

	addr_from_in[1].sin_family = AF_INET;
	addr_from_in[1].sin_port = htons(AGENT_SPORT);
	addr_from_in[1].sin_addr.s_addr = htonl(INADDR_ANY);
	memset(&addr_from_in[0].sin_zero, 0, sizeof addr_from_in[0].sin_zero);
	//create receive socket
	int m_socket,s_socket;
	if((m_socket = socket(AF_INET, SOCK_STREAM, 0)) < 0)
	{
		std::string e("listening socket create error. ");
		e += strerror(errno);
		_Pr("%s", e.c_str());
		YMESet("Error", (char*)e.c_str());
		write(pipe_fd[1], &init_fail, 1);
		return -1;
	}

	//set socket reuse addr
	int opt = 1;
	socklen_t len = sizeof(opt);
	setsockopt(m_socket, SOL_SOCKET, SO_REUSEADDR, &opt, len);

	nonblocking(m_socket);

	ret = bind(m_socket, (struct sockaddr *)&addr_from_in[0], sizeof addr_from_in[0]);
	if (ret < 0) {
		std::string e("socket bind error. ");
		e += strerror(errno);
		_Pr("%s", e.c_str());
		YMESet("Error", (char*)e.c_str());
		write(pipe_fd[1], &init_fail, 1);
		return -1;
	}

	if (listen(m_socket, 3) < 0) {
		std::string e("socket listen error. ");
		e += strerror(errno);
		_Pr("%s", e.c_str());
		YMESet("Error", (char*)e.c_str());
		write(pipe_fd[1], &init_fail, 1);
		return -1;
	}
	
	write(pipe_fd[1], &init_ok, 1);
	
#ifndef __APPLE__
	struct epoll_event ev, *ev_list;
	ev.events = EPOLLIN | EPOLLET;
	ev.data.fd = m_socket;

	ev_list = (struct epoll_event*)malloc(EVLIST_SIZE * sizeof(struct epoll_event));

	if (epoll_ctl(kqfd, EPOLL_CTL_ADD, m_socket, &ev) == -1) {
		_Pr("epoll error, err[%d]", errno);
		return -errno;
	}
	
	while (1) {
		evcount = epoll_wait(kqfd, ev_list, EVLIST_SIZE, 4000000);
		
		for (i = 0; i < evcount; i++) {
			if (ev_list[i].events & EPOLLIN)
			{
				if (ev_list[i].data.fd == m_socket)
				{
					currfd = g_ConnMgr.HandleAcceptEvent(m_socket);
					if (currfd > 0)
					{
						nonblocking(currfd);
						//g_ConnMgr.AddREvent(g_ConnMgr._Reactor_FD, currfd);
						g_ConnMgr.AddEvent(g_ConnMgr._Reactor_FD, currfd, YM_EV_RD | YM_EV_WR | YM_EV_ONCE);
					}
				}
				else
				{
					currfd = ev_list[i].data.fd;
					do {
						ret = g_ConnMgr.HandleRecvEvent(currfd);
						if (ret == -2) goto NextEvent; 
						else if (ret == -1)
						{
							if (errno == EWOULDBLOCK || errno == EAGAIN)
								;
							else
							{
								_Pr("exception close connection,fd=%d,err=%d", ev_list[i].data.fd, errno);
								g_ConnMgr.RemoveConnect(ev_list[i].data.fd);
								goto NextEvent;
							}
						}
						else if (ret == 0)
						{
							if (errno != EINTR)
							{
								_Pr("ready to close connection,fd=%d,err=%d", ev_list[i].data.fd, errno);
								g_ConnMgr.RemoveConnect(ev_list[i].data.fd);
								goto NextEvent;
							}
							break;
						}
						else
						{
							std::cout << "new HandleRecvEvent event" << std::endl;
						}
					} while (errno == EINTR) ;
				}
			}
			if (ev_list[i].events & EPOLLOUT)
			{
				currfd = ev_list[i].data.fd;
				
				int err;
				socklen_t len = sizeof(err);
				int retval = getsockopt(currfd, SOL_SOCKET, SO_ERROR, &err, &len);
				
				if (err == 0 || err == EAGAIN)
				{
					ret = g_ConnMgr.HandleEPOEvent(currfd);
					if (ret == -1)
					{
						g_ConnMgr.RemoveConnect(currfd);
					}
				}
				if (ret != -2)
					//ret==-2是一次多余的通知，可优化
					continue;
			}
NextEvent:
			;
		} // for event count
		ConnRecycleBlock::CRBScan();
	}//while 1
#else
    
    struct kevent add_ev;
    EV_SET(&add_ev, m_socket, EVFILT_READ, EV_ADD, 0, 0, NULL);
    kevent(kqfd, &add_ev, 1, NULL, 0, NULL);
    
    //set kevent wait time
    timespec ts = {4,0};
    
	while(1){
		evcount = kevent(kqfd, NULL, 0, ev_list, EVLIST_SIZE, &ts);
		
		std::cout << "new event" << std::endl;

		for (i = 0; i < evcount; i++) {
			if (ev_list[i].filter & EVFILT_READ)
			{
				currfd = ev_list[i].ident;
				int ret;
				
				if (currfd == m_socket)
				{
					currfd = g_ConnMgr.HandleAcceptEvent(m_socket);
					if (currfd > 0)
					{
						nonblocking(currfd);
						g_ConnMgr.AddREvent(g_ConnMgr._Reactor_FD, currfd);
					}
				}
				else
				{
					do {
						ret = g_ConnMgr.HandleRecvEvent(currfd);
						if (ret == -2) break;
						else if (ret == -1)
						{
							if (errno == EWOULDBLOCK || errno == EAGAIN)
								break;
							
							_Pr("exception need to close connection,fd=%lu,err=%d", ev_list[i].ident, errno);
							g_ConnMgr.RemoveConnect(ev_list[i].ident);
							goto NextEvent;
						}
						else if (ret == 0)
						{
							if (errno != EINTR)
							{
								_Pr("need to close connection,fd=%d,err=%d", ev_list[i].ident, errno);
								g_ConnMgr.RemoveConnect(ev_list[i].ident);	
								goto NextEvent;
							}
							break;
						}
						else
						{
							std::cout << "new HandleRecvEvent event" << std::endl;
						}
					} while (errno == EINTR);					
				}
			}
			if (ev_list[i].filter & EVFILT_WRITE)
			{
				currfd = ev_list[i].ident;
				
				int err;
				socklen_t len = sizeof(err);
				int retval = getsockopt(currfd, SOL_SOCKET, SO_ERROR, &err, &len);
				if (currfd == m_socket)
				{
					continue;
				}
				if (err == 0 || err == EAGAIN)
				{
					ret = g_ConnMgr.HandleEPOEvent(currfd);
					if (ret == -1)
					{
						g_ConnMgr.RemoveConnect(currfd);
					}
				}
				if (ret != -2)
					//ret==-2是一次多余的通知，可优化
					continue;
			}
NextEvent:
			;
		}
		ConnRecycleBlock::CRBScan();
	}//while 1
#endif
	
	return 0;
}
