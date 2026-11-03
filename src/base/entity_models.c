#include "base_internal.h"
#include "maage/models.h"
#include "maage/entities.h"
#include <string.h>
#include <math.h>

/* These authoring buffers describe the finite Base roster, not registry or
 * runtime limits. Registration copies all geometry before they leave scope. */
typedef struct base_model_builder {
    maage_model_part parts[16];
    maage_model_cuboid boxes[24];
    size_t count, box_count;
} base_model_builder;

static maage_status base_model_texture(maage_content_context *context, const char *path, unsigned int width, unsigned int height, maage_texture_id *id) {
    maage_texture_definition texture;
    maage_status result;
    memset(&texture, 0, sizeof(texture));
    texture.fallback = MAAGE_CONTENT_ID_INVALID;
    texture.sheet_width = width;
    texture.sheet_height = height;
    result = maage_presentation_source_register(context->presentation, path, &texture.source);
    if (!maage_status_failed(result)) result = maage_presentation_texture_register(context->presentation, &texture, id);
    return result;
}
static void base_model_box(base_model_builder *builder, double x, double y, double z, double width, double height, double depth, unsigned int u, unsigned int v, double inflate) {
    maage_model_cuboid *box = &builder->boxes[builder->box_count++];
    memset(box, 0, sizeof(*box));
    box->minimum[0] = x;
    box->minimum[1] = y;
    box->minimum[2] = z;
    box->size[0] = width;
    box->size[1] = height;
    box->size[2] = depth;
    box->texture_x = u;
    box->texture_y = v;
    box->inflate = inflate;
    box->material = MAAGE_MESH_MATERIAL_CUTOUT;
    ++builder->parts[builder->count - 1].cuboid_count;
}
static void base_model_part(base_model_builder *builder, const char *name, const char *channel, double x, double y, double z) {
    maage_model_part *part = &builder->parts[builder->count++];
    memset(part, 0, sizeof(*part));
    part->name = name;
    part->channel = channel;
    part->parent = MAAGE_CONTENT_ID_INVALID;
    part->texture_id = MAAGE_CONTENT_ID_INVALID;
    part->pivot[0] = x;
    part->pivot[1] = y;
    part->pivot[2] = z;
    part->cuboids = &builder->boxes[builder->box_count];
}
static maage_status base_model_publish(maage_content_context *context, const char *name, base_model_builder *builder, maage_texture_id texture, unsigned int height, maage_model_animator animator, maage_model_id *id) {
    maage_model_definition model;
    memset(&model, 0, sizeof(model));
    model.parts = builder->parts;
    model.part_count = builder->count;
    model.texture_id = texture;
    model.texture_width = maage_presentation_texture(context->presentation, texture)->sheet_width;
    model.texture_height = height;
    model.scale = 1.0 / 16;
    model.animate = animator;
    return maage_model_registry_register(context->models, name, &model, id);
}
static maage_status base_quadruped_animate(const void *context, const maage_model_animation *animation, size_t part, maage_model_transform *transform) {
    (void) context;
    if (part == 0) {
        transform->rotation[0] = -animation->pitch;
        transform->rotation[1] = animation->yaw;
    } else if (part >= 2 && part < 6) transform->rotation[0] = cos(animation->walk_phase * 0.6662 + (part == 2 || part == 5 ? 0 : 3.141592653589793)) * animation->walk_amount * 1.4;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_UPDATE, 0);
}
static maage_status base_sheep_animate(const void *context, const maage_model_animation *animation, size_t part, maage_model_transform *transform) {
    static const double colors[16][3] = {
        {1, 1, 1}, {0.85, 0.5, 0.2}, {0.7, 0.3, 0.85}, {0.4, 0.6, 0.85}, {0.9, 0.9, 0.2}, {0.5, 0.8, 0.1}, {0.95, 0.5, 0.65}, {0.3, 0.3, 0.3}, {0.6, 0.6, 0.6}, {0.3, 0.5, 0.6}, {0.5, 0.25, 0.7}, {0.2, 0.3, 0.7}, {0.4, 0.3, 0.2}, {0.4, 0.5, 0.2}, {0.6, 0.2, 0.2}, {0.1, 0.1, 0.1}};
    unsigned int channel;
    if (part < 6) return base_quadruped_animate(context, animation, part, transform);
    transform->visible = !(animation->flags & MAAGE_ENTITY_STATE_SHEARED);
    for (channel = 0; channel < 3; ++channel)
        transform->tint[channel] = colors[animation->variant & 15UL][channel];
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_UPDATE, 0);
}
static maage_status base_chicken_animate(const void *context, const maage_model_animation *animation, size_t part, maage_model_transform *transform) {
    maage_status result = base_quadruped_animate(context, animation, part < 4 ? part : 1, transform);
    if (part == 4 || part == 5) transform->rotation[2] = (1 + sin(animation->age * 2)) * animation->airborne * (part == 4 ? 1 : -1);
    return result;
}
static maage_status base_zombie_animate(const void *context, const maage_model_animation *animation, size_t part, maage_model_transform *transform) {
    maage_status result = base_quadruped_animate(context, animation, part < 4 ? part : 1, transform);
    if (part == 4 || part == 5) {
        transform->rotation[0] = 1.5707963267948966 - sin(animation->attack * 3.141592653589793) * 1.2 + sin((1 - (1 - animation->attack) * (1 - animation->attack)) * 3.141592653589793) * 0.4;
        transform->rotation[0] += sin(animation->age * 0.067) * 0.05;
        transform->rotation[2] = (part == 4 ? -1 : 1) * (0.05 + sin(animation->age * 0.07) * 0.025);
    }
    return result;
}
static maage_status base_skeleton_animate(const void *context, const maage_model_animation *animation, size_t part, maage_model_transform *transform) {
    maage_status result = base_zombie_animate(context, animation, part, transform);
    if (part == 4 || part == 5) {
        transform->rotation[0] = 1.5707963267948966 - animation->pitch;
        transform->rotation[1] = animation->yaw + (part == 4 ? -0.25 : 0.45);
    }
    return result;
}
static maage_status base_creeper_animate(const void *context, const maage_model_animation *animation, size_t part, maage_model_transform *transform) {
    maage_status result = part ? base_quadruped_animate(context, animation, part - 1, transform) : maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_UPDATE, 0);
    if (!part && animation->charge > 0) {
        double charge = animation->charge, pulse;
        if (charge > 1) charge = 1;
        pulse = sin(charge * 100) * charge * 0.01;
        charge *= charge;
        charge *= charge;
        transform->scale[0] = transform->scale[2] = (1 + charge * 0.4) * (1 + pulse);
        transform->scale[1] = (1 + charge * 0.1) / (1 + pulse);
    }
    return result;
}
static maage_status base_spider_animate(const void *context, const maage_model_animation *animation, size_t part, maage_model_transform *transform) {
    (void) context;
    if (!part) {
        transform->rotation[0] = -animation->pitch;
        transform->rotation[1] = animation->yaw;
    } else if (part >= 3) {
        static const double phases[4] = {0, 3.141592653589793, 1.5707963267948966, 4.71238898038469};
        double phase = phases[(part - 3) / 2], walk = animation->walk_phase * 0.6662;
        double side = part & 1U ? -1 : 1;
        transform->rotation[1] = -cos(walk * 2 + phase) * animation->walk_amount * 0.4 * side;
        transform->rotation[2] = side * fabs(sin(walk + phase)) * animation->walk_amount * 0.4;
    }
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_UPDATE, 0);
}
static void base_quadruped(base_model_builder *builder, double leg, double head_y, double head_z, double head_width, double head_height, double head_depth) {
    static const char *const names[] = {"rear_left_leg", "rear_right_leg", "front_left_leg", "front_right_leg"};
    unsigned int i;
    memset(builder, 0, sizeof(*builder));
    base_model_part(builder, "head", "look", 0, head_y, head_z);
    base_model_box(builder, -head_width / 2, -head_height / 2, -head_depth, head_width, head_height, head_depth, 0, 0, 0);
    base_model_part(builder, "body", "body", 0, leg + 7, 2);
    builder->parts[1].rotation[0] = -1.5707963267948966;
    base_model_box(builder, -5, -6, -7, 10, 16, 8, 28, 8, 0);
    for (i = 0; i < 4; ++i) {
        base_model_part(builder, names[i], "walk", i & 1U ? 3 : -3, leg, i < 2 ? 7 : -5);
        base_model_box(builder, -2, -leg, -2, 4, leg, 4, 0, 16, 0);
    }
}
static maage_status base_pig_model(maage_content_context *context, maage_base_ids *ids) {
    base_model_builder builder;
    base_quadruped(&builder, 6, 12, -6, 8, 8, 8);
    base_model_part(&builder, "snout", "look", 0, 0, 0);
    builder.parts[6].parent = 0;
    base_model_box(&builder, -2, -1, -9, 4, 3, 1, 16, 16, 0);
    return base_model_publish(context, "minecraft:pig", &builder, ids->mob_textures[MAAGE_BASE_MOB_PIG], 32, base_quadruped_animate, &ids->mob_models[MAAGE_BASE_MOB_PIG]);
}
static maage_status base_cow_model(maage_content_context *context, maage_base_ids *ids) {
    base_model_builder builder;
    base_quadruped(&builder, 12, 20, -8, 8, 8, 6);
    builder.parts[1].pivot[1] = 19;
    builder.boxes[1].minimum[0] = -6;
    builder.boxes[1].minimum[1] = -8;
    builder.boxes[1].size[0] = 12;
    builder.boxes[1].size[1] = 18;
    builder.boxes[1].size[2] = 10;
    builder.boxes[1].texture_x = 18;
    builder.boxes[1].texture_y = 4;
    base_model_part(&builder, "horns", "look", 0, 0, 0);
    builder.parts[6].parent = 0;
    base_model_box(&builder, -5, 2, -4, 1, 3, 1, 22, 0, 0);
    base_model_box(&builder, 4, 2, -4, 1, 3, 1, 22, 0, 0);
    base_model_part(&builder, "udder", "body", 0, 0, 0);
    builder.parts[7].parent = 1;
    base_model_box(&builder, -2, -8, -8, 4, 6, 1, 52, 0, 0);
    return base_model_publish(context, "minecraft:cow", &builder, ids->mob_textures[MAAGE_BASE_MOB_COW], 32, base_quadruped_animate, &ids->mob_models[MAAGE_BASE_MOB_COW]);
}
static maage_status base_sheep_model(maage_content_context *context, maage_base_ids *ids) {
    static const char *const names[] = {"head_fleece", "body_fleece", "rear_left_fleece", "rear_right_fleece", "front_left_fleece", "front_right_fleece"};
    base_model_builder builder;
    unsigned int i;
    base_quadruped(&builder, 12, 18, -8, 6, 6, 8);
    builder.parts[1].pivot[1] = 19;
    builder.boxes[1].minimum[0] = -4;
    builder.boxes[1].size[0] = 8;
    builder.boxes[1].size[2] = 6;
    for (i = 0; i < 6; ++i) {
        maage_model_cuboid box = builder.boxes[i];
        base_model_part(&builder, names[i], "fleece", 0, 0, 0);
        builder.parts[builder.count - 1].parent = i;
        builder.parts[builder.count - 1].texture_id = ids->sheep_fleece;
        if (i == 0) {
            box.minimum[2] = -6;
            box.size[2] = 6;
        } else if (i >= 2) {
            box.minimum[1] = -6;
            box.size[1] = 6;
        }
        base_model_box(&builder, box.minimum[0], box.minimum[1], box.minimum[2], box.size[0], box.size[1], box.size[2], box.texture_x, box.texture_y, i == 1 ? 1.75 : (i == 0 ? 0.6 : 0.5));
    }
    return base_model_publish(context, "minecraft:sheep", &builder, ids->mob_textures[MAAGE_BASE_MOB_SHEEP], 32, base_sheep_animate, &ids->mob_models[MAAGE_BASE_MOB_SHEEP]);
}
static maage_status base_chicken_model(maage_content_context *context, maage_base_ids *ids) {
    base_model_builder builder;
    memset(&builder, 0, sizeof(builder));
    base_model_part(&builder, "head", "look", 0, 9, -4);
    base_model_box(&builder, -2, 0, -2, 4, 6, 3, 0, 0, 0);
    base_model_box(&builder, -2, 2, -4, 4, 2, 2, 14, 0, 0);
    base_model_box(&builder, -1, 0, -3, 2, 2, 2, 14, 4, 0);
    base_model_part(&builder, "body", "body", 0, 8, 0);
    builder.parts[1].rotation[0] = -1.5707963267948966;
    base_model_box(&builder, -3, -4, -3, 6, 8, 6, 0, 9, 0);
    base_model_part(&builder, "left_leg", "walk", -2, 5, 1);
    base_model_box(&builder, -1, -5, -3, 3, 5, 3, 26, 0, 0);
    base_model_part(&builder, "right_leg", "walk", 1, 5, 1);
    base_model_box(&builder, -1, -5, -3, 3, 5, 3, 26, 0, 0);
    base_model_part(&builder, "left_wing", "flap", -4, 11, 0);
    base_model_box(&builder, 0, -4, -3, 1, 4, 6, 24, 13, 0);
    base_model_part(&builder, "right_wing", "flap", 4, 11, 0);
    base_model_box(&builder, -1, -4, -3, 1, 4, 6, 24, 13, 0);
    return base_model_publish(context, "minecraft:chicken", &builder, ids->mob_textures[MAAGE_BASE_MOB_CHICKEN], 32, base_chicken_animate, &ids->mob_models[MAAGE_BASE_MOB_CHICKEN]);
}
static maage_status base_biped_model(maage_content_context *context, maage_base_ids *ids, unsigned int mob, int skeleton) {
    base_model_builder builder;
    unsigned int i;
    double limb = skeleton ? 2 : 4;
    memset(&builder, 0, sizeof(builder));
    base_model_part(&builder, "head", "look", 0, 24, 0);
    base_model_box(&builder, -4, 0, -4, 8, 8, 8, 0, 0, 0);
    base_model_part(&builder, "body", "body", 0, 12, 0);
    base_model_box(&builder, -4, 0, -2, 8, 12, 4, 16, 16, 0);
    for (i = 0; i < 2; ++i) {
        base_model_part(&builder, i ? "right_leg" : "left_leg", "walk", i ? 2 : -2, 12, 0);
        base_model_box(&builder, -limb / 2, -12, -limb / 2, limb, 12, limb, 0, 16, 0);
    }
    for (i = 0; i < 2; ++i) {
        base_model_part(&builder, i ? "right_arm" : "left_arm", "attack", i ? 6 : -6, 22, 0);
        base_model_box(&builder, -limb / 2, -10, -limb / 2, limb, 12, limb, 40, 16, 0);
    }
    return base_model_publish(context, skeleton ? "minecraft:skeleton" : "minecraft:zombie", &builder, ids->mob_textures[mob], skeleton ? 32 : 64, skeleton ? base_skeleton_animate : base_zombie_animate, &ids->mob_models[mob]);
}
static maage_status base_creeper_model(maage_content_context *context, maage_base_ids *ids) {
    base_model_builder builder;
    unsigned int i;
    static const char *const names[] = {"rear_left_leg", "rear_right_leg", "front_left_leg", "front_right_leg"};
    memset(&builder, 0, sizeof(builder));
    base_model_part(&builder, "swell", "charge", 0, 0, 0);
    base_model_part(&builder, "head", "look", 0, 18, 0);
    base_model_box(&builder, -4, 0, -4, 8, 8, 8, 0, 0, 0);
    base_model_part(&builder, "body", "body", 0, 6, 0);
    base_model_box(&builder, -4, 0, -2, 8, 12, 4, 16, 16, 0);
    for (i = 0; i < 4; ++i) {
        base_model_part(&builder, names[i], "walk", i & 1U ? 2 : -2, 6, i < 2 ? 4 : -4);
        base_model_box(&builder, -2, -6, -2, 4, 6, 4, 0, 16, 0);
    }
    for (i = 1; i < builder.count; ++i)
        builder.parts[i].parent = 0;
    return base_model_publish(context, "minecraft:creeper", &builder, ids->mob_textures[MAAGE_BASE_MOB_CREEPER], 32, base_creeper_animate, &ids->mob_models[MAAGE_BASE_MOB_CREEPER]);
}
static maage_status base_spider_model(maage_content_context *context, maage_base_ids *ids) {
    base_model_builder builder;
    static const char *const names[] = {"rear_left_leg", "rear_right_leg", "middle_rear_left_leg", "middle_rear_right_leg", "middle_front_left_leg", "middle_front_right_leg", "front_left_leg", "front_right_leg"};
    unsigned int i;
    memset(&builder, 0, sizeof(builder));
    base_model_part(&builder, "head", "look", 0, 9, -3);
    base_model_box(&builder, -4, -4, -8, 8, 8, 8, 32, 4, 0);
    base_model_part(&builder, "thorax", "body", 0, 9, 0);
    base_model_box(&builder, -3, -3, -3, 6, 6, 6, 0, 0, 0);
    base_model_part(&builder, "abdomen", "body", 0, 9, 9);
    base_model_box(&builder, -5, -4, -6, 10, 8, 12, 0, 12, 0);
    for (i = 0; i < 8; ++i) {
        double side = i & 1U ? 1 : -1;
        base_model_part(&builder, names[i], "crawl", side * 4, 9, 2 - (double) (i / 2));
        builder.parts[builder.count - 1].rotation[1] = -side * (i / 2 == 0 ? 0.7853981633974483 : i / 2 == 1 ? 0.3926990816987241
                                                                                              : i / 2 == 2   ? -0.3926990816987241
                                                                                                             : -0.7853981633974483);
        builder.parts[builder.count - 1].rotation[2] = -side * (i / 2 == 0 || i / 2 == 3 ? 0.7853981633974483 : 0.5811946409141118);
        base_model_box(&builder, side < 0 ? -15 : -1, -1, -1, 16, 2, 2, 18, 0, 0);
    }
    return base_model_publish(context, "minecraft:spider", &builder, ids->mob_textures[MAAGE_BASE_MOB_SPIDER], 32, base_spider_animate, &ids->mob_models[MAAGE_BASE_MOB_SPIDER]);
}
maage_status maage_base_entity_models(maage_content_context *context, maage_base_ids *ids) {
    static const char *const paths[] = {"minecraft/textures/entity/pig/pig.png", "minecraft/textures/entity/cow/cow.png", "minecraft/textures/entity/sheep/sheep.png", "minecraft/textures/entity/chicken.png", "minecraft/textures/entity/zombie/zombie.png", "minecraft/textures/entity/skeleton/skeleton.png", "minecraft/textures/entity/creeper/creeper.png", "minecraft/textures/entity/spider/spider.png"};
    unsigned int i;
    maage_status result;
    base_model_builder arrow;
    for (i = 0; i < MAAGE_BASE_MOB_COUNT; ++i) {
        result = base_model_texture(context, paths[i], 64, i == MAAGE_BASE_MOB_ZOMBIE ? 64 : 32, &ids->mob_textures[i]);
        if (maage_status_failed(result)) return result;
    }
    result = base_model_texture(context, "minecraft/textures/entity/sheep/sheep_fur.png", 64, 32, &ids->sheep_fleece);
    if (!maage_status_failed(result)) result = base_pig_model(context, ids);
    if (!maage_status_failed(result)) result = base_cow_model(context, ids);
    if (!maage_status_failed(result)) result = base_sheep_model(context, ids);
    if (!maage_status_failed(result)) result = base_chicken_model(context, ids);
    if (!maage_status_failed(result)) result = base_biped_model(context, ids, MAAGE_BASE_MOB_ZOMBIE, 0);
    if (!maage_status_failed(result)) result = base_biped_model(context, ids, MAAGE_BASE_MOB_SKELETON, 1);
    if (!maage_status_failed(result)) result = base_creeper_model(context, ids);
    if (!maage_status_failed(result)) result = base_spider_model(context, ids);
    if (!maage_status_failed(result)) result = base_model_texture(context, "minecraft/textures/entity/arrow.png", 32, 32, &ids->arrow_texture);
    if (maage_status_failed(result)) return result;
    memset(&arrow, 0, sizeof(arrow));
    base_model_part(&arrow, "shaft", "flight", 0, 0, 0);
    base_model_box(&arrow, -0.5, -0.5, -6, 1, 1, 12, 0, 0, 0);
    base_model_part(&arrow, "fletching", "flight", 0, 0, 4);
    base_model_box(&arrow, -2, -0.15, -2, 4, 0.3, 4, 0, 0, 0);
    return base_model_publish(context, "minecraft:arrow", &arrow, ids->arrow_texture, 32, NULL, &ids->arrow_model);
}
