#include "settings.h"
#include <string.h>

static const maage_setting_definition maage_setting_definitions[] = {
    { MAAGE_SETTING_UI_SCALE, "Interface scale", MAAGE_SETTING_NUMBER, 0.85f, 1.25f, 1, "" },
    { MAAGE_SETTING_KEEP_OPEN, "Keep launcher open after launch", MAAGE_SETTING_BOOLEAN, 0, 1, 1, "" },
    { MAAGE_SETTING_DEFAULT_INSTALLATION, "Default installation", MAAGE_SETTING_TEXT, 0, 0, 0, "" },
    { MAAGE_SETTING_GAME_DIRECTORY, "Game directory", MAAGE_SETTING_TEXT, 0, 0, 0, "" },
    { MAAGE_SETTING_MEMORY_MIB, "Memory limit (MiB)", MAAGE_SETTING_NUMBER, 0, 65536, 0, "" }
};
const maage_setting_definition *maage_setting_describe(maage_setting_key key)
{
    return (unsigned int)key < MAAGE_SETTING_COUNT ? &maage_setting_definitions[key] : NULL;
}
void maage_launcher_settings_init(maage_launcher_settings *settings)
{
    int index;
    const maage_setting_definition *definition;
    memset(settings, 0, sizeof(*settings));
    for (index = 0; index < MAAGE_SETTING_COUNT; ++index) {
        definition = &maage_setting_definitions[index];
        settings->values[index].type = definition->type;
        switch (definition->type) {
        case MAAGE_SETTING_NUMBER: settings->values[index].data.number = definition->default_number; break;
        case MAAGE_SETTING_BOOLEAN: settings->values[index].data.boolean = (int)definition->default_number; break;
        case MAAGE_SETTING_TEXT: strcpy(settings->values[index].data.text, definition->default_text); break;
        }
    }
}
int maage_launcher_settings_set(maage_launcher_settings *settings, maage_setting_key key, const maage_setting_value *value)
{
    const maage_setting_definition *definition = maage_setting_describe(key);
    const maage_setting_value *old;
    size_t index;
    int changed;
    if (!settings || !definition || !value || value->type != definition->type) return 0;
    old = &settings->values[key];
    switch (value->type) {
    case MAAGE_SETTING_NUMBER:
        if (!(value->data.number >= definition->minimum && value->data.number <= definition->maximum)) return 0;
        changed = old->data.number != value->data.number;
        break;
    case MAAGE_SETTING_BOOLEAN:
        if (value->data.boolean != 0 && value->data.boolean != 1) return 0;
        changed = old->data.boolean != value->data.boolean;
        break;
    case MAAGE_SETTING_TEXT:
        if (!memchr(value->data.text, 0, sizeof(value->data.text))) return 0;
        for (index = 0; value->data.text[index]; ++index)
            if ((unsigned char)value->data.text[index] < 32) return 0;
        changed = strcmp(old->data.text, value->data.text) != 0;
        break;
    default: return 0;
    }
    if (changed) {
        settings->values[key] = *value;
        if (settings->changed) settings->changed(settings->context, key, &settings->values[key]);
    }
    return 1;
}
