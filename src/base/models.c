#include "base_internal.h"
#include "maage/models.h"
#include <string.h>

static maage_status base_chest_animate(const void *context, const maage_model_animation *animation, size_t part, maage_model_transform *transform) {
    double remaining = 1 - animation->charge;
    (void) context;
    if (remaining < 0) remaining = 0;
    if (remaining > 1) remaining = 1;
    if (part == 1) transform->rotation[0] = (1 - remaining * remaining * remaining) * 1.5707963267948966;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_UPDATE, 0);
}
maage_status maage_base_chest_models(maage_content_context *context, maage_base_ids *ids) {
    static const char *const names[2][2] = {{"minecraft:chest", "minecraft:large_chest"}, {"minecraft:trapped_chest", "minecraft:large_trapped_chest"}};
    static const char *const paths[2][2] = {{"minecraft/textures/entity/chest/normal.png", "minecraft/textures/entity/chest/normal_double.png"}, {"minecraft/textures/entity/chest/trapped.png", "minecraft/textures/entity/chest/trapped_double.png"}};
    maage_model_cuboid boxes[3];
    maage_model_part parts[3];
    maage_model_definition definition;
    maage_texture_definition texture;
    maage_status result;
    unsigned int trapped, large, part, width;
    for (trapped = 0; trapped < 2; ++trapped)
        for (large = 0; large < 2; ++large) {
            width = large ? 30 : 14;
            memset(boxes, 0, sizeof(boxes));
            memset(parts, 0, sizeof(parts));
            boxes[0].minimum[0] = -(double) width / 2;
            boxes[0].minimum[2] = -7;
            boxes[0].size[0] = width;
            boxes[0].size[2] = 14;
            boxes[0].size[1] = 10;
            boxes[0].texture_y = 19;
            boxes[1].minimum[2] = -14;
            boxes[1].size[0] = width;
            boxes[1].size[2] = 14;
            boxes[1].size[1] = 5;
            boxes[2].minimum[0] = -1;
            boxes[2].minimum[1] = -2;
            boxes[2].minimum[2] = -15;
            boxes[2].size[0] = 2;
            boxes[2].size[1] = 4;
            boxes[2].size[2] = 1;
            parts[0].name = "body";
            parts[0].channel = "body";
            parts[1].name = "lid";
            parts[1].channel = "open";
            parts[1].pivot[0] = -(double) width / 2;
            parts[1].pivot[1] = 9;
            parts[1].pivot[2] = 7;
            parts[2].name = "latch";
            parts[2].channel = "open";
            parts[2].pivot[0] = (double) width / 2;
            for (part = 0; part < 3; ++part) {
                parts[part].parent = part == 2 ? 1 : MAAGE_CONTENT_ID_INVALID;
                parts[part].texture_id = MAAGE_CONTENT_ID_INVALID;
                parts[part].cuboids = &boxes[part];
                parts[part].cuboid_count = 1;
            }
            memset(&definition, 0, sizeof(definition));
            definition.parts = parts;
            definition.part_count = 3;
            definition.texture_width = large ? 128 : 64;
            definition.texture_height = 64;
            definition.scale = 1.0 / 16;
            definition.animate = base_chest_animate;
            memset(&texture, 0, sizeof(texture));
            texture.fallback = MAAGE_CONTENT_ID_INVALID;
            texture.sheet_width = definition.texture_width;
            texture.sheet_height = definition.texture_height;
            result = maage_presentation_source_register(context->presentation, paths[trapped][large], &texture.source);
            if (!maage_status_failed(result)) result = maage_presentation_texture_register(context->presentation, &texture, &definition.texture_id);
            if (!maage_status_failed(result)) result = maage_model_registry_register(context->models, names[trapped][large], &definition, &ids->chest_models[trapped][large]);
            if (maage_status_failed(result)) return result;
        }
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_REGISTER, 0);
}
maage_status maage_base_chest_models_bind(maage_content_context *context, const maage_base_ids *ids) {
    maage_item_model_transform transform;
    maage_status result;
    unsigned int trapped;
    memset(&transform, 0, sizeof(transform));
    /* Center the registered Y-up model around the item origin, then present
     * its latch toward the inventory light rig. Every item context shares it. */
    transform.translation[0] = transform.translation[2] = 0.5;
    transform.translation[1] = 1.0 / 16;
    transform.rotation[1] = 3.14159265358979323846;
    transform.scale = 1;
    for (trapped = 0; trapped < 2; ++trapped) {
        result = maage_item_registry_bind_model(context->items, trapped ? ids->item_trapped_chest : ids->item_chest, ids->chest_models[trapped][0], &transform);
        if (maage_status_failed(result)) return result;
    }
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_SET, 0);
}
