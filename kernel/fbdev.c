#include "kernel.h"
#include <minix/com.h>
#include <minix/type.h>

PUBLIC void fbdev_task(void)
{
	message m;
	int r;

	while (TRUE) {
		if ((r = receive(ANY, &m)) != OK)
			panic("fbdev: receive failed", r);
		switch (m.m_type) {
		default: break;
		}
	}
}
