#include "ns.h"

extern union Nsipc nsipcbuf;

void
input(envid_t ns_envid)
{
	binaryname = "ns_input";

	// LAB 6: Your code here:
	// 	- read a packet from the device driver
	//	- send it to the network server
	// Hint: When you IPC a page to the network server, it will be
	// reading from it for a while, so don't immediately receive
	// another packet in to the same physical page.
	int r;
	for(;;) {
		r = sys_net_rx(nsipcbuf.pkt.jp_data, PGSIZE);
		if(r == -E_RXQUEUE_EMPTY)
			continue;
		nsipcbuf.pkt.jp_len = r;
		ipc_send(ns_envid, NSREQ_INPUT, &nsipcbuf, PTE_U|PTE_W);
	}
}
