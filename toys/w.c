/* vi: set sw=4 ts=4:
 *
 * w.c - shows logged in users
 *
 * Copyright 2012 Gaurang Shastri <gmshastri@gmail.com>
 *
 * Not in SUSv4.

USE_W(NEWTOY(w, NULL, TOYFLAG_USR|TOYFLAG_BIN))

config W
	bool "w"
	default y
	help
	  usage: w 

	  Show who is logged on and since how long they logged in.

*/

#include "toys.h"

void w_main(void)
{
    struct utmpx *x;
    time_t time_val;
    xprintf("USER     TTY             LOGIN@              FROM");
    setutxent();
    x=getutxent();
    while(x!=NULL) {
        if(x->ut_type==7) {
	    xprintf("\n");
            xprintf("%-9.8s",x->ut_user);
            xprintf("%-9.8s",x->ut_line);

	    xprintf(" ");
	    time_val = (x->ut_tv.tv_sec);
	    xprintf("%-4.24s",ctime(&time_val));
	    
            xprintf(" (");
            xprintf("%-1.12s",x->ut_host);
            xprintf(")");
        }
    x=getutxent();
    }
    xprintf("\n");
}
