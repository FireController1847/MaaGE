#include "base_internal.h"
#include "maage/entities.h"
#include "maage/gameplay.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

typedef struct base_mob_state {
    unsigned long timer, random;
} base_mob_state;

static maage_status base_entity_status(unsigned char code, unsigned char operation) {
    return maage_status_make(code, MAAGE_CONTEXT_WORLD, operation, 0);
}
static unsigned long base_mob_random(base_mob_state *state) {
    state->random = (state->random * 1664525UL + 1013904223UL) & 0xffffffffUL;
    return state->random;
}
static maage_status base_mob_encode(const void *data, maage_entity_codec *codec) {
    base_mob_state state = *(const base_mob_state *) data;
    maage_status result = codec->word(codec->context, &state.timer, 4);
    if (!maage_status_failed(result)) result = codec->word(codec->context, &state.random, 4);
    return result;
}
static maage_status base_mob_decode(void *data, maage_entity_codec *codec) {
    base_mob_state *state = (base_mob_state *) data;
    maage_status result = codec->word(codec->context, &state->timer, 4);
    if (!maage_status_failed(result)) result = codec->word(codec->context, &state->random, 4);
    return result;
}
static int base_mob_valid(const maage_entity_type_definition *definition, const maage_entity *entity) {
    const base_mob_state *state = (const base_mob_state *) entity->state;
    const maage_base_ids *ids = (const maage_base_ids *) definition->behavior_context;
    return state && state->timer <= 12000 && state->random <= 4294967295UL && (definition->id != ids->mob_types[MAAGE_BASE_MOB_SHEEP] || entity->payload.living.variant < 16);
}
static maage_status base_chicken_initialize(const maage_entity_type_definition *definition, maage_entity *entity) {
    base_mob_state *state = (base_mob_state *) entity->state;
    (void) definition;
    state->random = entity->id ^ 0x632be5abUL;
    state->timer = 6000 + base_mob_random(state) % 6000;
    return base_entity_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_INITIALIZE);
}
static maage_status base_sheep_initialize(const maage_entity_type_definition *definition, maage_entity *entity) {
    base_mob_state *state = (base_mob_state *) entity->state;
    unsigned long choice;
    (void) definition;
    state->random = entity->id ^ 0x9e3779b9UL;
    state->timer = 1200 + base_mob_random(state) % 1200;
    choice = base_mob_random(state) % 10000;
    if (!entity->payload.living.variant) {
        if (choice < 500) entity->payload.living.variant = 15;
        else if (choice < 1000) entity->payload.living.variant = 7;
        else if (choice < 1500) entity->payload.living.variant = 8;
        else if (choice < 1800) entity->payload.living.variant = 12;
        else if (choice < 1816) entity->payload.living.variant = 6;
    }
    return base_entity_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_INITIALIZE);
}
static maage_status base_chicken_behavior(const maage_entity_type_definition *definition, maage_entity *entity, maage_entity_context *context) {
    const maage_base_ids *ids = (const maage_base_ids *) definition->behavior_context;
    base_mob_state *state = (base_mob_state *) entity->state;
    maage_item_stack egg;
    double position[3], velocity[3] = {0, 0.1, 0};
    maage_status result = maage_entities_behavior_standard(definition, entity, context);
    if (maage_status_failed(result) || !entity->payload.living.health) return result;
    if (state->timer) --state->timer;
    if (state->timer) return result;
    memset(&egg, 0, sizeof(egg));
    egg.item_id = ids->item_egg;
    egg.count = 1;
    memcpy(position, entity->position, sizeof(position));
    result = maage_entities_reserve(context->entities, 1);
    if (maage_status_failed(result)) return result;
    result = maage_entities_spawn(context->entities, context->items, &egg, position, velocity, 10);
    if (!maage_status_failed(result)) state->timer = 6000 + base_mob_random(state) % 6000;
    return result;
}
static maage_status base_sheep_behavior(const maage_entity_type_definition *definition, maage_entity *entity, maage_entity_context *context) {
    const maage_base_ids *ids = (const maage_base_ids *) definition->behavior_context;
    base_mob_state *state = (base_mob_state *) entity->state;
    const maage_block_state_definition *ground;
    int x, y, z;
    maage_status result = maage_entities_behavior_standard(definition, entity, context);
    if (maage_status_failed(result) || !entity->payload.living.health) return result;
    if (state->timer) --state->timer;
    if (state->timer || !(entity->payload.living.flags & MAAGE_ENTITY_STATE_SHEARED)) return result;
    x = (int) floor(entity->position[0]);
    y = (int) floor(entity->position[1] - 0.1);
    z = (int) floor(entity->position[2]);
    ground = maage_block_registry_state(maage_world_registry(context->world), maage_world_get(context->world, x, y, z));
    if (!ground || ground->block_id != ids->block_grass) return base_entity_status(MAAGE_STATUS_NOOP, MAAGE_OPERATION_TICK);
    result = maage_world_set(context->world, x, y, z, ground->id, ids->state_dirt);
    if (maage_status_code(result) == MAAGE_STATUS_SUCCESS) {
        entity->payload.living.flags &= ~((unsigned long) MAAGE_ENTITY_STATE_SHEARED);
        state->timer = 1200 + base_mob_random(state) % 1200;
    }
    return result;
}
static maage_status base_sheep_interact(const maage_entity_type_definition *definition, maage_entity *entity, maage_entity_context *context, unsigned int action) {
    const maage_base_ids *ids = (const maage_base_ids *) definition->behavior_context;
    const maage_item_definition *held = context->held ? maage_item_registry_get(context->items, context->held->item_id) : NULL;
    base_mob_state *state = (base_mob_state *) entity->state;
    maage_item_stack wool;
    unsigned long random;
    maage_status result;
    if (action != MAAGE_ENTITY_INTERACT_USE || !held || !maage_traits_has(ids->traits, MAAGE_TRAIT_ITEM, held->id, ids->tool_tags[MAAGE_TOOL_SHEARS]) || (entity->payload.living.flags & MAAGE_ENTITY_STATE_SHEARED)) return base_entity_status(MAAGE_STATUS_NOOP, MAAGE_OPERATION_UPDATE);
    random = state->random;
    memset(&wool, 0, sizeof(wool));
    wool.item_id = ids->item_wool;
    wool.count = 1 + (unsigned int) (((random * 1664525UL + 1013904223UL) & 0xffffffffUL) % 3);
    result = maage_item_variant_change(context->items, wool.item_id, 0, ids->property_color, entity->payload.living.variant, &wool.variant);
    if (!maage_status_failed(result)) result = maage_entities_shear(context, entity, &wool);
    if (!maage_status_failed(result) && context->handled) base_mob_random(state);
    return result;
}
static maage_status base_cow_interact(const maage_entity_type_definition *definition, maage_entity *entity, maage_entity_context *context, unsigned int action) {
    const maage_base_ids *ids = (const maage_base_ids *) definition->behavior_context;
    maage_item_stack milk;
    double position[3], velocity[3] = {0, 0.1, 0};
    unsigned int remaining;
    maage_status result;
    if (action != MAAGE_ENTITY_INTERACT_USE || !context->held || !context->held->count || context->held->item_id != ids->item_bucket) return base_entity_status(MAAGE_STATUS_NOOP, MAAGE_OPERATION_UPDATE);
    memset(&milk, 0, sizeof(milk));
    milk.item_id = ids->item_milk_bucket;
    milk.count = 1;
    if (context->held->count == 1 && !context->creative) {
        *context->held = milk;
        context->handled = 1;
        return base_entity_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_UPDATE);
    }
    memcpy(position, entity->position, sizeof(position));
    result = maage_entities_reserve(context->entities, 1);
    if (maage_status_failed(result)) return result;
    result = maage_inventory_add(&context->gameplay->inventory, context->items, &milk, &remaining);
    if (maage_status_failed(result)) return result;
    if (remaining) {
        result = maage_entities_spawn(context->entities, context->items, &milk, position, velocity, 10);
        if (maage_status_failed(result)) return result;
    }
    if (!context->creative) --context->held->count;
    context->handled = 1;
    return base_entity_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_UPDATE);
}
static maage_status base_entity_texture(maage_content_context *context, const char *path, maage_texture_id *id) {
    maage_texture_definition texture;
    maage_status result;
    memset(&texture, 0, sizeof(texture));
    texture.fallback = MAAGE_CONTENT_ID_INVALID;
    result = maage_presentation_source_register(context->presentation, path, &texture.source);
    if (!maage_status_failed(result)) result = maage_presentation_texture_register(context->presentation, &texture, id);
    return result;
}
static maage_status base_entity_item(maage_content_context *context, const char *name, const char *label, const char *path, unsigned int maximum, maage_item_id *id) {
    maage_item_definition item;
    maage_status result;
    memset(&item, 0, sizeof(item));
    item.display_name = label;
    item.max_stack = maximum;
    item.mining_speed = 1;
    item.model = MAAGE_ITEM_MODEL_SPRITE;
    item.behavior_context = context->module_data;
    result = base_entity_texture(context, path, &item.texture_id);
    if (!maage_status_failed(result)) result = maage_item_registry_register(context->items, name, &item);
    if (!maage_status_failed(result)) *id = item.id;
    return result;
}
static maage_block_state_id base_wool_icon(const maage_item_definition *item, maage_item_variant_id variant) {
    return variant < 16 ? ((const maage_base_ids *) item->behavior_context)->wool_states[variant] : MAAGE_CONTENT_ID_INVALID;
}
static const char *base_wool_name(const maage_item_definition *item, maage_item_variant_id variant) {
    static const char *const names[] = {"White Wool", "Orange Wool", "Magenta Wool", "Light Blue Wool", "Yellow Wool", "Lime Wool", "Pink Wool", "Gray Wool", "Light Gray Wool", "Cyan Wool", "Purple Wool", "Blue Wool", "Brown Wool", "Green Wool", "Red Wool", "Black Wool"};
    (void) item;
    return variant < 16 ? names[variant] : "";
}
static maage_status base_wool_drops(const maage_block_registry *blocks, const maage_item_registry *items, const maage_block_state_definition *state, const maage_item_definition *tool, unsigned long *random, maage_item_drops *drops) {
    const maage_base_ids *ids = maage_base_state_references(blocks, state);
    (void) items;
    (void) tool;
    (void) random;
    memset(drops, 0, sizeof(*drops));
    drops->count = 1;
    drops->stacks[0].item_id = ids->item_wool;
    drops->stacks[0].count = 1;
    drops->stacks[0].variant = 0;
    return maage_item_variant_change(items, drops->stacks[0].item_id, 0, ids->property_color, maage_base_state_value(blocks, state, ids->property_color), &drops->stacks[0].variant);
}
static maage_status base_sheep_loot(const maage_entity_type_definition *definition, const maage_entity *entity, maage_item_stack *stack) {
    const maage_base_ids *ids = (const maage_base_ids *) definition->behavior_context;
    if (stack->item_id == ids->item_wool) return maage_item_variant_change(ids->items, stack->item_id, 0, ids->property_color, entity->payload.living.variant, &stack->variant);
    return base_entity_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_UPDATE);
}
static maage_status base_wool(maage_content_context *context, maage_base_ids *ids) {
    static const char *const colors[] = {"white", "orange", "magenta", "light_blue", "yellow", "lime", "pink", "gray", "silver", "cyan", "purple", "blue", "brown", "green", "red", "black"};
    maage_block_definition block;
    maage_block_state_definition state;
    maage_item_definition item;
    maage_status result;
    unsigned int color, face, channel;
    maage_property_assignment property;
    char path[96];
    memset(&block, 0, sizeof(block));
    block.behavior_context = ids;
    block.geometry_id = ids->geometry_cube;
    block.solid = block.material_solid = block.selectable = block.opaque = 1;
    block.light_opacity = 15;
    block.hardness = 0.8;
    block.drop_count = 1;
    block.drops = base_wool_drops;
    block.special_tool_class = MAAGE_TOOL_SHEARS;
    block.special_tool_speed = 5;
    block.material = ids->material_cloth;
    block.item_drag = 0.588;
    block.collision_shape.maximum[0] = block.collision_shape.maximum[1] = block.collision_shape.maximum[2] = 1;
    block.selection_shape = block.collision_shape;
    result = maage_base_register_block(context->blocks, "minecraft:wool", &block);
    if (maage_status_failed(result)) return result;
    ids->block_wool = block.id;
    for (color = 0; color < 16; ++color) {
        sprintf(path, "minecraft/textures/blocks/wool_colored_%s.png", colors[color]);
        result = base_entity_texture(context, path, &ids->wool_textures[color]);
        if (maage_status_failed(result)) return result;
        memset(&state, 0, sizeof(state));
        state.block_id = ids->block_wool;
        property.property = ids->property_color;
        property.value = color;
        result = maage_property_set_register(context->traits, &property, 1, &state.properties);
        if (maage_status_failed(result)) return result;
        for (face = 0; face < MAAGE_FACE_COUNT; ++face) {
            state.faces[face].texture_id = ids->wool_textures[color];
            /* A texture identity does not substitute for a valid local UV
             * rectangle; the Client remaps these coordinates into its atlas. */
            state.faces[face].uv[2] = state.faces[face].uv[3] = 1;
            for (channel = 0; channel < 4; ++channel)
                state.faces[face].tint[channel] = 1;
        }
        result = maage_block_registry_register_state(context->blocks, &state);
        if (maage_status_failed(result)) return result;
        ids->wool_states[color] = state.id;
    }
    memset(&item, 0, sizeof(item));
    item.behavior_context = ids;
    item.display_name = "Wool";
    item.max_stack = 64;
    item.variant_count = 16;
    item.has_block = 1;
    item.block_id = ids->block_wool;
    item.icon_state_id = item.placement_state_id = ids->wool_states[0];
    item.icon_state = base_wool_icon;
    item.variant_name = base_wool_name;
    item.texture_id = ids->wool_textures[0];
    item.mining_speed = 1;
    result = maage_item_registry_register(context->items, "minecraft:wool", &item);
    if (!maage_status_failed(result)) result = maage_base_item_variants(context, &item, "minecraft:wool");
    if (!maage_status_failed(result)) ids->item_wool = item.id;
    return result;
}
static maage_status base_mob_sound(maage_content_context *context, const char *name, const char *path, maage_sound_id *id) {
    maage_sound_resource_definition resource;
    maage_sound_definition sound;
    maage_status result;
    memset(&resource, 0, sizeof(resource));
    resource.path = path;
    result = maage_sound_resource_register(context->sounds, name, &resource);
    if (maage_status_failed(result)) return result;
    memset(&sound, 0, sizeof(sound));
    sound.resources = &resource.id;
    sound.resource_count = 1;
    sound.bus = MAAGE_SOUND_BUS_AMBIENT;
    sound.positional = 1;
    sound.gain = sound.pitch = 1;
    sound.distance = 16;
    result = maage_sound_register(context->sounds, name, &sound);
    if (!maage_status_failed(result)) *id = sound.id;
    return result;
}
static void base_loot(maage_entity_loot *loot, maage_item_id item, unsigned int minimum, unsigned int maximum) {
    memset(loot, 0, sizeof(*loot));
    loot->stack.item_id = item;
    loot->stack.count = 1;
    loot->min_count = minimum;
    loot->max_count = maximum;
    loot->chance_numerator = loot->chance_denominator = 1;
}
static void base_mob_definition(maage_entity_type_definition *definition, const maage_base_ids *ids, unsigned int mob, const char *label, unsigned int health, double width, double height, double speed) {
    memset(definition, 0, sizeof(*definition));
    definition->behavior_context = ids;
    definition->display_name = label;
    definition->classification = mob < MAAGE_BASE_MOB_ZOMBIE ? MAAGE_ENTITY_CLASS_PASSIVE : MAAGE_ENTITY_CLASS_HOSTILE;
    definition->flags = MAAGE_ENTITY_CAN_SWIM;
    definition->behaviors = MAAGE_ENTITY_BEHAVIOR_WANDER;
    definition->health = health;
    definition->width = width;
    definition->height = height;
    definition->eye_height = height * 0.85;
    definition->speed = speed;
    definition->gravity = 0.08;
    definition->fall_speed_limit = 3.9;
    definition->jump_velocity = 0.42;
    definition->attack_range = 1.5;
    definition->attack_cooldown = 20;
    definition->invulnerability_ticks = 20;
    definition->follow_range = mob < MAAGE_BASE_MOB_ZOMBIE ? 6 : 24;
    definition->experience = mob < MAAGE_BASE_MOB_ZOMBIE ? 2 : 5;
    definition->model_id = ids->mob_models[mob];
    definition->texture_id = ids->mob_textures[mob];
    definition->behavior = maage_entities_behavior_standard;
    if (definition->classification == MAAGE_ENTITY_CLASS_HOSTILE) {
        definition->behaviors |= MAAGE_ENTITY_BEHAVIOR_PURSUE;
        definition->despawn_distance = 128;
        definition->despawn_ticks = 6000;
    }
}
static void base_spawn_rule(maage_entity_spawn_rule *spawn, const maage_base_ids *ids, int passive, maage_biome_id biomes[4]) {
    memset(spawn, 0, sizeof(*spawn));
    spawn->weight = passive ? 10 : 100;
    spawn->group_min = 2;
    spawn->group_max = 4;
    spawn->min_light = passive ? 9 : 0;
    spawn->max_light = passive ? 15 : 7;
    spawn->min_y = 1;
    spawn->max_y = 254;
    spawn->surface = passive ? ids->block_grass : MAAGE_CONTENT_ID_INVALID;
    spawn->require_grass = passive;
    spawn->min_player_distance = 24;
    spawn->max_player_distance = 96;
    spawn->population = passive ? 10 : 20;
    if (passive) {
        biomes[0] = ids->biome_plains;
        biomes[1] = ids->biome_forest;
        biomes[2] = ids->biome_taiga;
        biomes[3] = ids->biome_hills;
        spawn->biomes = biomes;
        spawn->biome_count = 4;
    }
}
static void base_mob_state_bind(maage_entity_type_definition *definition, maage_status (*initialize)(const maage_entity_type_definition *, maage_entity *)) {
    definition->state_size = sizeof(base_mob_state);
    definition->encoded_size = 8;
    definition->state_version = 1;
    definition->encode = base_mob_encode;
    definition->decode = base_mob_decode;
    definition->initialize = initialize;
    definition->valid = base_mob_valid;
}
maage_status maage_base_entities(maage_content_context *context, maage_base_ids *ids) {
    static const char *const names[] = {"minecraft:pig", "minecraft:cow", "minecraft:sheep", "minecraft:chicken", "minecraft:zombie", "minecraft:skeleton", "minecraft:creeper", "minecraft:spider"};
    static const char *const labels[] = {"Pig", "Cow", "Sheep", "Chicken", "Zombie", "Skeleton", "Creeper", "Spider"};
    static const char *const ambient[] = {"pig/say1", "cow/say1", "sheep/say1", "chicken/say1", "zombie/say1", "skeleton/say1", NULL, "spider/say1"};
    static const char *const hurt[] = {"pig/say1", "cow/hurt1", "sheep/say1", "chicken/hurt1", "zombie/hurt1", "skeleton/hurt1", "creeper/say1", "spider/say1"};
    static const char *const deaths[] = {"pig/death", "cow/hurt1", "sheep/say1", "chicken/hurt1", "zombie/death", "skeleton/death", "creeper/death", "spider/death"};
    static const unsigned int health[] = {10, 10, 8, 4, 20, 20, 20, 16};
    static const double widths[] = {0.9, 0.9, 0.9, 0.3, 0.6, 0.6, 0.6, 1.4};
    static const double heights[] = {0.9, 1.3, 1.3, 0.7, 1.8, 1.8, 1.7, 0.9};
    static const double speeds[] = {0.1, 0.08, 0.092, 0.1, 0.092, 0.1, 0.1, 0.16};
    maage_entity_type_definition definition;
    maage_entity_spawn_rule spawn;
    maage_entity_loot loot[4];
    maage_biome_id biomes[4];
    maage_item_definition egg, milk;
    maage_status result;
    maage_texture_id egg_texture, egg_overlay;
    maage_item_sprite_layer layers[2];
    static const unsigned long egg_colors[MAAGE_BASE_MOB_COUNT][2] = {{15771042UL, 14377823UL}, {4470310UL, 10592673UL}, {15198183UL, 16758197UL}, {10592673UL, 16711680UL}, {44975UL, 7969893UL}, {12698049UL, 4802889UL}, {894731UL, 0UL}, {3419431UL, 11013646UL}};
    unsigned int layer, channel;
    unsigned int mob, count;
    char name[96], label[96], path[128];
    result = base_entity_item(context, "minecraft:feather", "Feather", "minecraft/textures/items/feather.png", 64, &ids->item_feather);
    if (!maage_status_failed(result)) result = base_entity_item(context, "minecraft:leather", "Leather", "minecraft/textures/items/leather.png", 64, &ids->item_leather);
    if (!maage_status_failed(result)) result = base_entity_item(context, "minecraft:bone", "Bone", "minecraft/textures/items/bone.png", 64, &ids->item_bone);
    if (!maage_status_failed(result)) result = base_entity_item(context, "minecraft:rotten_flesh", "Rotten Flesh", "minecraft/textures/items/rotten_flesh.png", 64, &ids->item_rotten_flesh);
    if (!maage_status_failed(result)) result = base_entity_item(context, "minecraft:string", "String", "minecraft/textures/items/string.png", 64, &ids->item_string);
    if (!maage_status_failed(result)) result = base_entity_item(context, "minecraft:gunpowder", "Gunpowder", "minecraft/textures/items/gunpowder.png", 64, &ids->item_gunpowder);
    if (!maage_status_failed(result)) result = base_entity_item(context, "minecraft:egg", "Egg", "minecraft/textures/items/egg.png", 16, &ids->item_egg);
    if (!maage_status_failed(result)) result = base_entity_item(context, "minecraft:arrow", "Arrow", "minecraft/textures/items/arrow.png", 64, &ids->item_arrow);
    if (!maage_status_failed(result)) result = base_entity_item(context, "minecraft:spider_eye", "Spider Eye", "minecraft/textures/items/spider_eye.png", 64, &ids->item_spider_eye);
    if (!maage_status_failed(result)) result = base_wool(context, ids);
    if (maage_status_failed(result)) return result;
    memset(&milk, 0, sizeof(milk));
    milk.display_name = "Milk Bucket";
    milk.max_stack = 1;
    milk.mining_speed = 1;
    milk.model = MAAGE_ITEM_MODEL_SPRITE;
    milk.container_item = ids->item_bucket;
    result = base_entity_texture(context, "minecraft/textures/items/bucket_milk.png", &milk.texture_id);
    if (!maage_status_failed(result)) result = maage_item_registry_register(context->items, "minecraft:milk_bucket", &milk);
    if (!maage_status_failed(result)) ids->item_milk_bucket = milk.id;
    if (maage_status_failed(result)) return result;
    memset(&definition, 0, sizeof(definition));
    definition.behavior_context = ids;
    definition.display_name = "Arrow";
    definition.classification = MAAGE_ENTITY_CLASS_PROJECTILE;
    definition.health = 1;
    definition.width = definition.height = 0.2;
    definition.eye_height = 0.1;
    definition.gravity = 0.05;
    definition.fall_speed_limit = 3.9;
    definition.model_id = ids->arrow_model;
    definition.texture_id = ids->arrow_texture;
    result = maage_entity_type_register(context->entity_types, "minecraft:arrow", &definition);
    if (maage_status_failed(result)) return result;
    ids->arrow_type = definition.id;
    for (mob = 0; mob < MAAGE_BASE_MOB_COUNT; ++mob) {
        base_mob_definition(&definition, ids, mob, labels[mob], health[mob], widths[mob], heights[mob], speeds[mob]);
        base_spawn_rule(&spawn, ids, mob < MAAGE_BASE_MOB_ZOMBIE, biomes);
        definition.spawns = &spawn;
        definition.spawn_count = 1;
        count = 0;
        if (mob == MAAGE_BASE_MOB_PIG) {
            base_loot(&loot[count], ids->item_porkchop, 1, 3);
            loot[count++].excluded_flags = MAAGE_ENTITY_LOOT_BURNING;
            base_loot(&loot[count], ids->item_cooked_porkchop, 1, 3);
            loot[count++].required_flags = MAAGE_ENTITY_LOOT_BURNING;
        } else if (mob == MAAGE_BASE_MOB_COW) {
            base_loot(&loot[count++], ids->item_leather, 0, 2);
            base_loot(&loot[count], ids->item_beef, 1, 3);
            loot[count++].excluded_flags = MAAGE_ENTITY_LOOT_BURNING;
            base_loot(&loot[count], ids->item_cooked_beef, 1, 3);
            loot[count++].required_flags = MAAGE_ENTITY_LOOT_BURNING;
            definition.interact = base_cow_interact;
            spawn.weight = 8;
        } else if (mob == MAAGE_BASE_MOB_SHEEP) {
            base_loot(&loot[count], ids->item_wool, 1, 1);
            loot[count++].excluded_flags = MAAGE_ENTITY_LOOT_SHEARED;
            definition.behavior = base_sheep_behavior;
            definition.interact = base_sheep_interact;
            definition.loot_stack = base_sheep_loot;
            base_mob_state_bind(&definition, base_sheep_initialize);
            spawn.weight = 12;
        } else if (mob == MAAGE_BASE_MOB_CHICKEN) {
            base_loot(&loot[count++], ids->item_feather, 0, 2);
            base_loot(&loot[count], ids->item_chicken, 1, 1);
            loot[count++].excluded_flags = MAAGE_ENTITY_LOOT_BURNING;
            base_loot(&loot[count], ids->item_cooked_chicken, 1, 1);
            loot[count++].required_flags = MAAGE_ENTITY_LOOT_BURNING;
            definition.flags |= MAAGE_ENTITY_SLOW_FALL;
            definition.fall_speed_limit = 0.15;
            definition.behavior = base_chicken_behavior;
            base_mob_state_bind(&definition, base_chicken_initialize);
        } else if (mob == MAAGE_BASE_MOB_ZOMBIE) {
            base_loot(&loot[count++], ids->item_rotten_flesh, 0, 2);
            definition.flags |= MAAGE_ENTITY_BURNS_DAYLIGHT;
            definition.behaviors |= MAAGE_ENTITY_BEHAVIOR_MELEE;
            definition.attack_damage = 3;
            definition.follow_range = 40;
        } else if (mob == MAAGE_BASE_MOB_SKELETON) {
            base_loot(&loot[count++], ids->item_bone, 0, 2);
            base_loot(&loot[count++], ids->item_arrow, 0, 2);
            definition.flags |= MAAGE_ENTITY_BURNS_DAYLIGHT;
            definition.behaviors |= MAAGE_ENTITY_BEHAVIOR_RANGED;
            definition.attack_damage = 4;
            definition.attack_range = 15;
            definition.attack_cooldown = 40;
            definition.projectile_type = ids->arrow_type;
            definition.projectile_speed = 1.6;
        } else if (mob == MAAGE_BASE_MOB_CREEPER) {
            base_loot(&loot[count++], ids->item_gunpowder, 0, 2);
            definition.behaviors |= MAAGE_ENTITY_BEHAVIOR_EXPLODE;
            definition.attack_range = 3;
            definition.fuse_ticks = 30;
            definition.explosion_radius = 3;
        } else if (mob == MAAGE_BASE_MOB_SPIDER) {
            base_loot(&loot[count++], ids->item_string, 0, 2);
            base_loot(&loot[count], ids->item_spider_eye, 1, 1);
            loot[count].required_flags = MAAGE_ENTITY_LOOT_PLAYER;
            loot[count++].chance_denominator = 3;
            definition.flags |= MAAGE_ENTITY_CLIMBS;
            definition.behaviors |= MAAGE_ENTITY_BEHAVIOR_MELEE;
            definition.attack_damage = 2;
            definition.attack_range = 2;
            definition.eye_height = 0.65;
        }
        definition.loot = loot;
        definition.loot_count = count;
        if (ambient[mob]) {
            sprintf(name, "%s.ambient", names[mob]);
            sprintf(path, "minecraft/sounds/mob/%s.ogg", ambient[mob]);
            result = base_mob_sound(context, name, path, &definition.ambient_sound);
            if (maage_status_failed(result)) return result;
        }
        sprintf(name, "%s.hurt", names[mob]);
        sprintf(path, "minecraft/sounds/mob/%s.ogg", hurt[mob]);
        result = base_mob_sound(context, name, path, &definition.hurt_sound);
        if (maage_status_failed(result)) return result;
        sprintf(name, "%s.death", names[mob]);
        sprintf(path, "minecraft/sounds/mob/%s.ogg", deaths[mob]);
        result = base_mob_sound(context, name, path, &definition.death_sound);
        if (maage_status_failed(result)) return result;
        if (mob == MAAGE_BASE_MOB_SKELETON || mob == MAAGE_BASE_MOB_CREEPER) {
            sprintf(name, "%s.attack", names[mob]);
            result = base_mob_sound(context, name, mob == MAAGE_BASE_MOB_SKELETON ? "minecraft/sounds/random/bow.ogg" : "minecraft/sounds/random/explode1.ogg", &definition.attack_sound);
            if (maage_status_failed(result)) return result;
        }
        result = maage_entity_type_register(context->entity_types, names[mob], &definition);
        if (maage_status_failed(result)) return result;
        ids->mob_types[mob] = definition.id;
    }
    result = base_entity_texture(context, "minecraft/textures/items/spawn_egg.png", &egg_texture);
    if (!maage_status_failed(result)) result = base_entity_texture(context, "minecraft/textures/items/spawn_egg_overlay.png", &egg_overlay);
    if (maage_status_failed(result)) return result;
    for (mob = 0; mob < MAAGE_BASE_MOB_COUNT; ++mob) {
        memset(&egg, 0, sizeof(egg));
        sprintf(name, "%s_spawn_egg", names[mob]);
        sprintf(label, "Spawn %s", labels[mob]);
        egg.display_name = label;
        egg.behavior_context = ids;
        egg.max_stack = 64;
        egg.model = MAAGE_ITEM_MODEL_SPRITE;
        egg.texture_id = egg_texture;
        memset(layers, 0, sizeof(layers));
        layers[0].texture = egg_texture;
        layers[1].texture = egg_overlay;
        for (layer = 0; layer < 2; ++layer) {
            for (channel = 0; channel < 3; ++channel)
                layers[layer].tint[channel] = (float) ((egg_colors[mob][layer] >> ((2 - channel) * 8)) & 255UL) / 255;
            layers[layer].tint[3] = 1;
        }
        egg.sprite_layers = layers;
        egg.sprite_layer_count = 2;
        egg.spawn_type = ids->mob_types[mob];
        egg.mining_speed = 1;
        result = maage_item_registry_register(context->items, name, &egg);
        if (maage_status_failed(result)) return result;
        ids->spawn_eggs[mob] = egg.id;
    }
    return result;
}
maage_status maage_base_entity_creative(maage_content_context *context, const maage_base_ids *ids) {
    maage_creative_tab_id miscellaneous, materials, building, combat;
    maage_creative_entry entry;
    maage_item_id items[10];
    unsigned int i;
    maage_status result = maage_creative_tab_resolve(context->creative, "minecraft:miscellaneous", &miscellaneous);
    if (!maage_status_failed(result)) result = maage_creative_tab_resolve(context->creative, "minecraft:materials", &materials);
    if (!maage_status_failed(result)) result = maage_creative_tab_resolve(context->creative, "minecraft:building_blocks", &building);
    if (!maage_status_failed(result)) result = maage_creative_tab_resolve(context->creative, "minecraft:combat", &combat);
    if (maage_status_failed(result)) return result;
    memset(&entry, 0, sizeof(entry));
    entry.searchable = 1;
    entry.stack.count = 1;
    entry.tab = miscellaneous;
    for (i = 0; i < MAAGE_BASE_MOB_COUNT; ++i) {
        entry.stack.item_id = ids->spawn_eggs[i];
        entry.order = entry.search_order = 383 * 32 + (long) i;
        result = maage_creative_entry_register(context->creative, &entry);
        if (maage_status_failed(result)) return result;
    }
    items[0] = ids->item_feather;
    items[1] = ids->item_leather;
    items[2] = ids->item_bone;
    items[3] = ids->item_rotten_flesh;
    items[4] = ids->item_string;
    items[5] = ids->item_gunpowder;
    items[6] = ids->item_egg;
    items[7] = ids->item_arrow;
    items[8] = ids->item_spider_eye;
    items[9] = ids->item_milk_bucket;
    for (i = 0; i < 10; ++i) {
        entry.stack.item_id = items[i];
        entry.tab = i == 7 ? combat : (i == 6 || i == 9 ? miscellaneous : materials);
        entry.order = entry.search_order = 410 * 32 + (long) i;
        result = maage_creative_entry_register(context->creative, &entry);
        if (maage_status_failed(result)) return result;
    }
    entry.stack.item_id = ids->item_wool;
    entry.tab = building;
    for (i = 0; i < 16; ++i) {
        entry.stack.variant = i;
        entry.order = entry.search_order = 35 * 32 + (long) i;
        result = maage_creative_entry_register(context->creative, &entry);
        if (maage_status_failed(result)) return result;
    }
    return result;
}
