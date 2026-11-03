#ifndef MAAGE_EXECUTION_H
#define MAAGE_EXECUTION_H

/* Value context: copy into module-owned state for later callbacks. No mutable
 * process-global side exists; integrated Server and Client may run concurrently. */
typedef enum maage_execution_side {
    MAAGE_EXECUTION_CLIENT = 1,
    MAAGE_EXECUTION_INTEGRATED_SERVER = 2,
    MAAGE_EXECUTION_DEDICATED_SERVER = 4
} maage_execution_side;
enum {
    MAAGE_EXECUTION_AUTHORITY = 1,
    MAAGE_EXECUTION_PRESENTATION = 2,
    MAAGE_EXECUTION_HEADLESS = 4
};
typedef struct maage_execution_context {
    maage_execution_side side;
    unsigned int capabilities;
} maage_execution_context;
/* Capabilities describe engine facilities, not device handles or host OS.
 * PRESENTATION permits presentation configuration; it does not promise an open
 * rendering/audio device. Server contexts never own Client device services. */
#endif
