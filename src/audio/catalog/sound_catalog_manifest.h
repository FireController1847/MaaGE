#ifndef MAAGE_SOUND_CATALOG_MANIFEST_H
#define MAAGE_SOUND_CATALOG_MANIFEST_H

/* Family IDs are catalog-private numeric indices, not protocol event IDs. */
typedef enum maage_catalog_family {
    MAAGE_CAT_NONE = 0,
    MAAGE_CAT_STEP_STONE, MAAGE_CAT_STEP_GRASS, MAAGE_CAT_STEP_GRAVEL,
    MAAGE_CAT_STEP_WOOD, MAAGE_CAT_STEP_SAND, MAAGE_CAT_STEP_SNOW,
    MAAGE_CAT_STEP_CLOTH, MAAGE_CAT_DIG_STONE, MAAGE_CAT_DIG_GRASS,
    MAAGE_CAT_DIG_GRAVEL, MAAGE_CAT_DIG_WOOD, MAAGE_CAT_DIG_SAND,
    MAAGE_CAT_DIG_SNOW, MAAGE_CAT_DIG_CLOTH, MAAGE_CAT_GLASS,
    MAAGE_CAT_FALL_SMALL, MAAGE_CAT_FALL_BIG, MAAGE_CAT_HURT,
    MAAGE_CAT_SWIM, MAAGE_CAT_SPLASH, MAAGE_CAT_CLICK,
    MAAGE_CAT_CAVE, MAAGE_CAT_RAIN, MAAGE_CAT_THUNDER,
    MAAGE_CAT_WATER, MAAGE_CAT_LAVA, MAAGE_CAT_LAVA_POP,
    MAAGE_CAT_STEP_LADDER, MAAGE_CAT_ANVIL_LAND, MAAGE_CAT_POP, MAAGE_CAT_ITEM_BREAK,
    MAAGE_CAT_FAMILY_COUNT
} maage_catalog_family;

#define MAAGE_CAT_VARIANT_LIMIT 13U
#define MAAGE_CAT_PATH_LIMIT 96U

typedef struct maage_catalog_manifest_entry {
    const char *stem; /* Logical path relative to minecraft/sounds/ . */
    unsigned char first, count; /* first=0 starts unnumbered, followed by suffix 2 onward. */
} maage_catalog_manifest_entry;

extern const maage_catalog_manifest_entry maage_catalog_manifest[MAAGE_CAT_FAMILY_COUNT];

#endif