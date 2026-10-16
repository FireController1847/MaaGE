#ifndef MAAGE_LAUNCHER_LINKS_H
#define MAAGE_LAUNCHER_LINKS_H
#include "maage/window.h"
/* Main-thread native URL routing. Install before Cocoa finishes launching;
 * poll after native event dispatch. Return links never establish authentication. */
int maage_launcher_links_init(void);
int maage_launcher_links_poll(maage_window *window);
void maage_launcher_links_shutdown(void);
#endif
