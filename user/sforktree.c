// Fork a binary tree of processes and display their structure.

#include <inc/lib.h>

#define DEPTH 3

void sforktree(const char *cur);

void
sforkchild(const char *cur, char branch)
{
	char nxt[DEPTH+1];

	if (strlen(cur) >= DEPTH)
		return;

	snprintf(nxt, DEPTH+1, "%s%c", cur, branch);
	if (sfork() == 0) {
		sforktree(nxt);
		exit();
	}
}

void
sforktree(const char *cur)
{
	cprintf("%04x: I am '%s'\n", sys_getenvid(), cur);

	sforkchild(cur, '0');
	sforkchild(cur, '1');
}

void
umain(int argc, char **argv)
{
	sforktree("");
}

