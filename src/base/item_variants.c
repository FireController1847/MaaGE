#include "base_internal.h"
#include <string.h>

static maage_status base_variant_status(unsigned char code) {
    return maage_status_make(code, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_REGISTER, 0);
}
/* Identity names and ordinal source tables are consumed only while composing.
 * Runtime stacks address copied typed configurations and appearance rows. */
static maage_status base_variant_property(maage_content_context *context, const char *name, const char *const *values, size_t count, maage_property_id *property) {
    maage_property_definition definition;
    maage_property_value value;
    maage_status result;
    size_t i;
    result = maage_property_find(context->traits, name, property);
    if (!maage_status_failed(result)) return result;
    memset(&definition, 0, sizeof(definition));
    definition.kind = MAAGE_PROPERTY_IDENTITY;
    result = maage_property_register(context->traits, name, &definition);
    if (maage_status_failed(result)) return result;
    *property = definition.id;
    for (i = 0; i < count; ++i) {
        result = maage_property_value_register(context->traits, *property, values[i], &value);
        if (maage_status_failed(result)) return result;
    }
    return base_variant_status(MAAGE_STATUS_SUCCESS);
}
maage_status maage_base_item_variants(maage_content_context *context, const maage_item_definition *item, const char *name) {
    static const char *const woods[] = {"minecraft:oak", "minecraft:spruce", "minecraft:birch", "minecraft:jungle", "minecraft:acacia", "minecraft:dark_oak"};
    static const char *const coals[] = {"minecraft:coal", "minecraft:charcoal"};
    static const char *const fish[] = {"minecraft:cod", "minecraft:salmon", "minecraft:clownfish", "minecraft:pufferfish"};
    const maage_base_ids *ids = (const maage_base_ids *) item->behavior_context;
    maage_item_variant_definition row;
    maage_property_assignment assignment;
    maage_property_id property = MAAGE_CONTENT_ID_INVALID;
    maage_property_value value;
    maage_tag_definition tag_definition;
    maage_tag_id tags[4], category;
    maage_status result;
    const char *property_name = NULL, *category_name = NULL;
    size_t variant, count = item->variant_count ? item->variant_count : 1, tag_count = 0, wood_offset = 0;
    int wood = 0, identity = 0;
    if (!strcmp(name, "minecraft:log") || !strcmp(name, "minecraft:log2")) {
        category_name = "minecraft:log";
        wood = 1;
        wood_offset = !strcmp(name, "minecraft:log2") ? 4 : 0;
    } else if (!strcmp(name, "minecraft:planks")) {
        category_name = "minecraft:planks";
        wood = 1;
    } else if (!strcmp(name, "minecraft:leaves") || !strcmp(name, "minecraft:sapling")) wood = 1;
    if (wood) property_name = "minecraft:wood_type";
    else if (!strcmp(name, "minecraft:sand")) property_name = "minecraft:sand_type";
    else if (!strcmp(name, "minecraft:wool") || !strcmp(name, "minecraft:dye")) property_name = "minecraft:color";
    else if (!strcmp(name, "minecraft:coal")) {
        property_name = "minecraft:coal_type";
        identity = 1;
        result = base_variant_property(context, property_name, coals, 2, &property);
        if (maage_status_failed(result)) return result;
    } else if (!strcmp(name, "minecraft:fish") || !strcmp(name, "minecraft:cooked_fish")) {
        property_name = "minecraft:fish_type";
        identity = 2;
        result = base_variant_property(context, property_name, fish, 4, &property);
        if (maage_status_failed(result)) return result;
    }
    if (property_name && property == MAAGE_CONTENT_ID_INVALID) {
        result = maage_property_find(context->traits, property_name, &property);
        if (maage_status_failed(result)) return result;
    }
    if (wood) {
        result = maage_tag_find(context->traits, "minecraft:wood", &tags[tag_count]);
        if (maage_status_failed(result)) return result;
        ++tag_count;
    }
    if (category_name) {
        result = maage_tag_find(context->traits, category_name, &tags[tag_count]);
        if (maage_status_failed(result)) return result;
        ++tag_count;
    }
    result = maage_tag_find(context->traits, "minecraft:item", &category);
    if (maage_status_failed(result)) {
        memset(&tag_definition, 0, sizeof(tag_definition));
        tag_definition.domains = MAAGE_TRAIT_ITEM;
        result = maage_tag_register(context->traits, "minecraft:item", &tag_definition);
        if (maage_status_failed(result)) return result;
        category = tag_definition.id;
    }
    tags[tag_count++] = category;
    if (item->has_tool_tag) tags[tag_count++] = item->tool_tag;
    result = maage_traits_assign(context->traits, MAAGE_TRAIT_ITEM, item->id, tags, tag_count, 0);
    if (maage_status_failed(result)) return result;
    for (variant = 0; variant < count; ++variant) {
        memset(&row, 0, sizeof(row));
        row.icon_state_id = item->icon_state ? item->icon_state(item, (maage_item_variant_id) variant) : item->icon_state_id;
        row.placement_state_id = item->has_block ? row.icon_state_id : item->placement_state_id;
        row.texture_id = item->icon_texture ? item->icon_texture(item, (maage_item_variant_id) variant) : item->texture_id;
        row.display_name = item->variant_name ? item->variant_name(item, (maage_item_variant_id) variant) : item->display_name;
        if (!strcmp(name, "minecraft:sapling")) row.placement_state_id = row.icon_state_id = ids->saplings[variant];
        if (property != MAAGE_CONTENT_ID_INVALID) {
            assignment.property = property;
            value = (maage_property_value) variant;
            if (wood || identity) {
                const char *value_name = wood ? woods[variant + wood_offset] : (identity == 1 ? coals[variant] : fish[variant]);
                result = maage_property_value_find(context->traits, property, value_name, &value);
                if (maage_status_failed(result)) return result;
            }
            assignment.value = value;
            result = maage_property_set_register(context->traits, &assignment, 1, &row.properties);
            if (maage_status_failed(result)) return result;
        } else if (count > 1) return base_variant_status(MAAGE_STATUS_INVALID_STATE);
        result = maage_item_registry_variant_bind(context->items, item->id, (maage_item_variant_id) variant, &row);
        if (maage_status_failed(result)) return result;
    }
    return base_variant_status(MAAGE_STATUS_SUCCESS);
}
