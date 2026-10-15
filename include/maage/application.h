#ifndef MAAGE_APPLICATION_H
#define MAAGE_APPLICATION_H
/* Each graphical executable implements this callback. Shared native entry glue
 * supplies WinMain or main and initializes Core logging with a per-user log file.
 * It does not create a window, choose a renderer, or own application resources. */
int maage_application_main(int argc, char *const argv[]);
#endif
