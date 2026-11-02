#include "window_internal.h"
#include <string.h>

/* Translation only: USB HID keyboard usages are the public physical identity.
 * Scan is PC set-1 with bit 8 for E0; evdev and XKB names describe Linux positions. */
typedef struct maage_native_key {
    unsigned int usage, evdev, scan;
    int mac;
    char xkb[5];
} maage_native_key;
static const maage_native_key maage_native_keys[] = {
    {4U, 30U, 30U, 0, "AC01"},
    {5U, 48U, 48U, 11, "AB05"},
    {6U, 46U, 46U, 8, "AB03"},
    {7U, 32U, 32U, 2, "AC03"},
    {8U, 18U, 18U, 14, "AD03"},
    {9U, 33U, 33U, 3, "AC04"},
    {10U, 34U, 34U, 5, "AC05"},
    {11U, 35U, 35U, 4, "AC06"},
    {12U, 23U, 23U, 34, "AD08"},
    {13U, 36U, 36U, 38, "AC07"},
    {14U, 37U, 37U, 40, "AC08"},
    {15U, 38U, 38U, 37, "AC09"},
    {16U, 50U, 50U, 46, "AB07"},
    {17U, 49U, 49U, 45, "AB06"},
    {18U, 24U, 24U, 31, "AD09"},
    {19U, 25U, 25U, 35, "AD10"},
    {20U, 16U, 16U, 12, "AD01"},
    {21U, 19U, 19U, 15, "AD04"},
    {22U, 31U, 31U, 1, "AC02"},
    {23U, 20U, 20U, 17, "AD05"},
    {24U, 22U, 22U, 32, "AD07"},
    {25U, 47U, 47U, 9, "AB04"},
    {26U, 17U, 17U, 13, "AD02"},
    {27U, 45U, 45U, 7, "AB02"},
    {28U, 21U, 21U, 16, "AD06"},
    {29U, 44U, 44U, 6, "AB01"},
    {30U, 2U, 2U, 18, "AE01"},
    {31U, 3U, 3U, 19, "AE02"},
    {32U, 4U, 4U, 20, "AE03"},
    {33U, 5U, 5U, 21, "AE04"},
    {34U, 6U, 6U, 23, "AE05"},
    {35U, 7U, 7U, 22, "AE06"},
    {36U, 8U, 8U, 26, "AE07"},
    {37U, 9U, 9U, 28, "AE08"},
    {38U, 10U, 10U, 25, "AE09"},
    {39U, 11U, 11U, 29, "AE10"},
    {40U, 28U, 28U, 36, "RTRN"},
    {41U, 1U, 1U, 53, "ESC"},
    {42U, 14U, 14U, 51, "BKSP"},
    {43U, 15U, 15U, 48, "TAB"},
    {44U, 57U, 57U, 49, "SPCE"},
    {45U, 12U, 12U, 27, "AE11"},
    {46U, 13U, 13U, 24, "AE12"},
    {47U, 26U, 26U, 33, "AD11"},
    {48U, 27U, 27U, 30, "AD12"},
    {49U, 43U, 43U, 42, "BKSL"},
    {51U, 39U, 39U, 41, "AC10"},
    {52U, 40U, 40U, 39, "AC11"},
    {53U, 41U, 41U, 50, "TLDE"},
    {54U, 51U, 51U, 43, "AB08"},
    {55U, 52U, 52U, 47, "AB09"},
    {56U, 53U, 53U, 44, "AB10"},
    {57U, 58U, 58U, 57, "CAPS"},
    {58U, 59U, 59U, 122, "FK01"},
    {59U, 60U, 60U, 120, "FK02"},
    {60U, 61U, 61U, 99, "FK03"},
    {61U, 62U, 62U, 118, "FK04"},
    {62U, 63U, 63U, 96, "FK05"},
    {63U, 64U, 64U, 97, "FK06"},
    {64U, 65U, 65U, 98, "FK07"},
    {65U, 66U, 66U, 100, "FK08"},
    {66U, 67U, 67U, 101, "FK09"},
    {67U, 68U, 68U, 109, "FK10"},
    {68U, 87U, 87U, 103, "FK11"},
    {69U, 88U, 88U, 111, "FK12"},
    {70U, 99U, 311U, -1, "PRSC"},
    {71U, 70U, 70U, -1, "SCLK"},
    {72U, 119U, 325U, -1, "PAUS"},
    {73U, 110U, 338U, 114, "INS"},
    {74U, 102U, 327U, 115, "HOME"},
    {75U, 104U, 329U, 116, "PGUP"},
    {76U, 111U, 339U, 117, "DELE"},
    {77U, 107U, 335U, 119, "END"},
    {78U, 109U, 337U, 121, "PGDN"},
    {79U, 106U, 333U, 124, "RGHT"},
    {80U, 105U, 331U, 123, "LEFT"},
    {81U, 108U, 336U, 125, "DOWN"},
    {82U, 103U, 328U, 126, "UP"},
    {83U, 69U, 69U, 71, "NMLK"},
    {84U, 98U, 309U, 75, "KPDV"},
    {85U, 55U, 55U, 67, "KPMU"},
    {86U, 74U, 74U, 78, "KPSU"},
    {87U, 78U, 78U, 69, "KPAD"},
    {88U, 96U, 284U, 76, "KPEN"},
    {89U, 79U, 79U, 83, "KP1"},
    {90U, 80U, 80U, 84, "KP2"},
    {91U, 81U, 81U, 85, "KP3"},
    {92U, 75U, 75U, 86, "KP4"},
    {93U, 76U, 76U, 87, "KP5"},
    {94U, 77U, 77U, 88, "KP6"},
    {95U, 71U, 71U, 89, "KP7"},
    {96U, 72U, 72U, 91, "KP8"},
    {97U, 73U, 73U, 92, "KP9"},
    {98U, 82U, 82U, 82, "KP0"},
    {99U, 83U, 83U, 65, "KPDL"},
    {100U, 86U, 86U, 10, "LSGT"},
    {101U, 127U, 349U, 110, "MENU"},
    {103U, 117U, 89U, 81, "KPEQ"},
    {135U, 89U, 115U, 94, "AB11"},
    {137U, 124U, 125U, 93, "AE13"},
    {144U, 122U, 113U, 104, "HKTG"},
    {145U, 123U, 114U, 102, "HJCV"},
    {224U, 29U, 29U, 59, "LCTL"},
    {225U, 42U, 42U, 56, "LFSH"},
    {226U, 56U, 56U, 58, "LALT"},
    {227U, 125U, 347U, 55, "LWIN"},
    {228U, 97U, 285U, 62, "RCTL"},
    {229U, 54U, 54U, 60, "RTSH"},
    {230U, 100U, 312U, 61, "RALT"},
    {231U, 126U, 348U, 54, "RWIN"},
    {104U, 183U, 100U, 105, "FK13"},
    {105U, 184U, 101U, 107, "FK14"},
    {106U, 185U, 102U, 113, "FK15"},
    {107U, 186U, 103U, 106, "FK16"},
    {108U, 187U, 104U, 64, "FK17"},
    {109U, 188U, 105U, 79, "FK18"},
    {110U, 189U, 106U, 80, "FK19"},
    {111U, 190U, 107U, 90, "FK20"},
    {112U, 191U, 108U, -1, "FK21"},
    {113U, 192U, 109U, -1, "FK22"},
    {114U, 193U, 110U, -1, "FK23"},
    {115U, 194U, 118U, -1, "FK24"}};
maage_input_key maage_window_key_evdev(unsigned int code) {
    size_t i;
    for (i = 0; i < sizeof(maage_native_keys) / sizeof(maage_native_keys[0]); ++i)
        if (code && maage_native_keys[i].evdev == code) return maage_native_keys[i].usage;
    return 0;
}
maage_input_key maage_window_key_scan(unsigned int code, int extended) {
    size_t i;
    code |= extended ? 256U : 0U;
    for (i = 0; i < sizeof(maage_native_keys) / sizeof(maage_native_keys[0]); ++i)
        if (code && maage_native_keys[i].scan == code) return maage_native_keys[i].usage;
    return 0;
}
maage_input_key maage_window_key_macos(unsigned int code) {
    size_t i;
    for (i = 0; i < sizeof(maage_native_keys) / sizeof(maage_native_keys[0]); ++i)
        if (maage_native_keys[i].mac >= 0 && (unsigned int) maage_native_keys[i].mac == code) return maage_native_keys[i].usage;
    return 0;
}
maage_input_key maage_window_key_xkb(const char *name) {
    size_t i;
    for (i = 0; i < sizeof(maage_native_keys) / sizeof(maage_native_keys[0]); ++i)
        if (!strncmp(name, maage_native_keys[i].xkb, 4)) return maage_native_keys[i].usage;
    return 0;
}
