#include "base_internal.h"
#include "maage/models.h"
#include <string.h>

/* The body, hinged lid and latch share a full 64x64 sheet. Item binding owns
 * orientation/projection; generic GUI and dropped-item paths consume the same
 * registered geometry without knowing the owning block identity. */
maage_status maage_base_chest_models(maage_content_context *context, const maage_base_ids *ids) {
    static const char *const names[2] = {"minecraft:chest", "minecraft:trapped_chest"};
    static const char *const paths[2] = {"minecraft/textures/entity/chest/normal.png", "minecraft/textures/entity/chest/trapped.png"};
    maage_model_cuboid boxes[3];
    maage_model_part parts[3];
    maage_model_definition definition;
    maage_texture_definition texture;
    maage_model_id model;
    maage_texture_id texture_id;
    maage_status result;
    unsigned int i, part;
    memset(boxes, 0, sizeof(boxes));
    memset(parts, 0, sizeof(parts));
    boxes[0].minimum[0] = boxes[0].minimum[2] = 1;
    boxes[0].size[0] = boxes[0].size[2] = 14;
    boxes[0].size[1] = 10;
    boxes[0].texture_y = 19;
    boxes[1].minimum[0] = boxes[1].minimum[2] = 1;
    boxes[1].minimum[1] = 10;
    boxes[1].size[0] = boxes[1].size[2] = 14;
    boxes[1].size[1] = 5;
    boxes[2].minimum[0] = 7;
    boxes[2].minimum[1] = 8;
    boxes[2].size[0] = 2;
    boxes[2].size[1] = 4;
    boxes[2].size[2] = 1;
    parts[0].name = "body";
    parts[0].channel = "body";
    parts[1].name = "lid";
    parts[1].channel = "open";
    parts[2].name = "latch";
    parts[2].channel = "open";
    for (part = 0; part < 3; ++part) {
        parts[part].parent = MAAGE_CONTENT_ID_INVALID;
        parts[part].texture_id = MAAGE_CONTENT_ID_INVALID;
        parts[part].cuboids = &boxes[part];
        parts[part].cuboid_count = 1;
    }
    memset(&definition, 0, sizeof(definition));
    definition.parts = parts;
    definition.part_count = 3;
    definition.texture_width = definition.texture_height = 64;
    definition.scale = 1.0 / 16;
    (void) ids;
    for (i = 0; i < 2; ++i) {
        memset(&texture, 0, sizeof(texture));
        texture.fallback = MAAGE_CONTENT_ID_INVALID;
        texture.sheet_width = texture.sheet_height = 64;
        result = maage_presentation_source_register(context->presentation, paths[i], &texture.source);
        if (!maage_status_failed(result)) result = maage_presentation_texture_register(context->presentation, &texture, &texture_id);
        if (maage_status_failed(result)) return result;
        definition.texture_id = texture_id;
        result = maage_model_registry_register(context->models, names[i], &definition, &model);
        if (maage_status_failed(result)) return result;
    }
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_REGISTER, 0);
}
maage_status maage_base_chest_models_bind(maage_content_context *context, const maage_base_ids *ids) {
    static const char *const names[2] = {"minecraft:chest", "minecraft:trapped_chest"};
    maage_item_model_transform transform;
    maage_model_id model;
    maage_status result;
    unsigned int i;
    memset(&transform, 0, sizeof(transform));
    transform.rotation[1] = 3.14159265358979323846;
    transform.translation[0] = transform.translation[2] = 1;
    transform.scale = 1;
    for (i = 0; i < 2; ++i) {
        result = maage_model_registry_resolve(context->models, names[i], &model);
        if (!maage_status_failed(result)) result = maage_item_registry_bind_model(context->items, i ? ids->item_trapped_chest : ids->item_chest, model, &transform);
        if (maage_status_failed(result)) return result;
    }
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_SET, 0);
}
