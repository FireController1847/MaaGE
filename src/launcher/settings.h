#ifndef MAAGE_LAUNCHER_SETTINGS_H
#define MAAGE_LAUNCHER_SETTINGS_H

/* Launcher configuration only. State is session-local; a future persistence
 * adapter may subscribe to changes without involving presentation code. */
typedef enum maage_setting_key {
    MAAGE_SETTING_UI_SCALE, MAAGE_SETTING_KEEP_OPEN, MAAGE_SETTING_DEFAULT_INSTALLATION,
    MAAGE_SETTING_GAME_DIRECTORY,
    MAAGE_SETTING_MEMORY_MIB, MAAGE_SETTING_COUNT
} maage_setting_key;
typedef enum maage_setting_type { MAAGE_SETTING_NUMBER, MAAGE_SETTING_BOOLEAN, MAAGE_SETTING_TEXT } maage_setting_type;
typedef struct maage_setting_value {
    maage_setting_type type;
    union { float number; int boolean; char text[256]; } data;
} maage_setting_value;
typedef struct maage_setting_definition {
    maage_setting_key key;
    const char *label;
    maage_setting_type type;
    float minimum, maximum, default_number;
    const char *default_text;
} maage_setting_definition;
typedef struct maage_launcher_settings {
    maage_setting_value values[MAAGE_SETTING_COUNT];
    void *context;
    /* Synchronous notification after a changed, valid value is committed.
     * The borrowed value belongs to settings; callbacks must not reenter set. */
    void (*changed)(void *context, maage_setting_key key, const maage_setting_value *value);
} maage_launcher_settings;
const maage_setting_definition *maage_setting_describe(maage_setting_key key);
void maage_launcher_settings_init(maage_launcher_settings *settings);
/* Invalid types/ranges/unterminated text leave state unchanged. Returns 0 on
 * rejection, 1 on acceptance (unchanged values do not emit notifications). */
int maage_launcher_settings_set(maage_launcher_settings *settings, maage_setting_key key, const maage_setting_value *value);
#endif
