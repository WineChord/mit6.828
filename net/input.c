#include "ns.h"

extern union Nsipc nsipcbuf;
void
sleep(int ms)
{
	unsigned now = sys_time_msec();
	unsigned end = now + ms;

	if ((int)now < 0 && (int)now > -MAXERROR)
		panic("sys_time_msec: %e", (int)now);
	if (end < now)
		panic("sleep: wrap");

	while (sys_time_msec() < end)
		sys_yield();
}

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
	// sys_page_alloc(ns_envid, &nsipcbuf, PTE_SYSCALL);
	// int len=1518;
	int len=2048;
	char mbuf[len];
	int r;
	for(;;) {
		// r = sys_net_rx(nsipcbuf.pkt.jp_data, len);
		r = sys_net_rx(mbuf, len);
		if(r == -E_RXQUEUE_EMPTY)
			continue;
		memcpy(nsipcbuf.pkt.jp_data, mbuf, r);
		nsipcbuf.pkt.jp_len = r;
		ipc_send(ns_envid, NSREQ_INPUT, &nsipcbuf, PTE_U|PTE_W|PTE_P);
		sleep(50);
	}
}
