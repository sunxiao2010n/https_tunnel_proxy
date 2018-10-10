#include "SCB.h"
#include "ConnectMgr.h"


static ConnectMgr& cm = ConnectMgr::Instance();

void SCBMgr::RecursiveRecycle(SCB* scb)
{
	if (scb->agent_conn && scb->agent_conn->sock_fd == scb->agent_fd)
	{
		cm.RemoveConn(scb->agent_fd);
		return;
	}
	if (scb->tunnel_conn && scb->tunnel_conn->sock_fd == scb->tunn_fd)
	{
		cm.RemoveConn(scb->tunn_fd);
		return;
	}
	return;
}