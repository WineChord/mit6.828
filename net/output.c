#include "ns.h"

extern union Nsipc nsipcbuf;

void
output(envid_t ns_envid)
{
	binaryname = "ns_output";

	// LAB 6: Your code here:
	// 	- read a packet from the network server
	//	- send the packet to the device driver
	envid_t env;
	int perm, r;
	for(;;) {
		r = ipc_recv(&env, &nsipcbuf, &perm);
		do {
			r = sys_net_tx(nsipcbuf.pkt.jp_data, nsipcbuf.pkt.jp_len);
			if(r == -E_TXQUEUE_FULL)
				cprintf("tx queue full\n");
		}while(r == -E_TXQUEUE_FULL);
	}
}
