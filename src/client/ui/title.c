#include "front_internal.h"
#include <math.h>
#include <stddef.h>
#include <string.h>

/* Project the six atlas faces through the shared UI pipeline. Camera depths
 * preserve perspective interpolation across each face's triangles. */
typedef struct title_camera {
    double cy, sy, cp, sp;
    double width, height, focal;
} title_camera;
typedef struct title_vertex {
    double point[3], uv[2];
} title_vertex;

static void title_face(const title_camera *camera, int face, title_vertex vertices[8]) {
    double s, t, point[3], x, z;
    unsigned int i;
    for (i = 0; i < 4; ++i) {
        s = i == 0 || i == 3 ? -1.0 : 1.0;
        t = i < 2 ? -1.0 : 1.0;
        /* Y and texture V increase downward. Side faces run front, right,
         * back, left; the upper/lower faces preserve their shared edges. */
        if (face == 0 || face == 2) {
            point[0] = face == 0 ? s : -s;
            point[1] = t;
            point[2] = face == 0 ? 1.0 : -1.0;
        } else if (face == 1 || face == 3) {
            point[0] = face == 1 ? 1.0 : -1.0;
            point[1] = t;
            point[2] = face == 1 ? -s : s;
        } else {
            point[0] = s;
            point[1] = face == 4 ? -1.0 : 1.0;
            point[2] = face == 4 ? t : -t;
        }
        /* Rotate the cubemap about Y, then X in the projected frame. Texture V
         * and screen Y both increase downward, fixing the pitch orientation. */
        x = camera->cy * point[0] - camera->sy * point[2];
        z = camera->sy * point[0] + camera->cy * point[2];
        vertices[i].point[0] = x;
        vertices[i].point[1] = camera->cp * point[1] - camera->sp * z;
        vertices[i].point[2] = camera->cp * z + camera->sp * point[1];
        vertices[i].uv[0] = s;
        vertices[i].uv[1] = t;
    }
}

static double title_clip_distance(const title_camera *camera, const title_vertex *vertex, unsigned int plane) {
    unsigned int axis = plane / 2;
    double extent = axis == 0 ? camera->width : camera->height;
    double offset = camera->focal * vertex->point[axis];
    return extent * 0.5 * vertex->point[2] + (plane & 1 ? -offset : offset);
}

static unsigned int title_clip(const title_camera *camera, title_vertex vertices[8]) {
    title_vertex output[8];
    const title_vertex *previous, *current;
    double previous_distance, distance, t;
    unsigned int count = 4, next, plane, i, component;
    /* A convex quad gains at most one vertex per clipping plane. These four
     * planes also exclude Z <= 0: their only Z == 0 point is the origin,
     * which cannot lie on a face of the cube surrounding the camera. */
    for (plane = 0; plane < 4 && count; ++plane) {
        next = 0;
        previous = &vertices[count - 1];
        previous_distance = title_clip_distance(camera, previous, plane);
        for (i = 0; i < count; ++i) {
            current = &vertices[i];
            distance = title_clip_distance(camera, current, plane);
            if ((previous_distance < 0.0 && distance > 0.0) || (previous_distance > 0.0 && distance < 0.0)) {
                t = previous_distance / (previous_distance - distance);
                for (component = 0; component < 3; ++component)
                    output[next].point[component] = previous->point[component] + t * (current->point[component] - previous->point[component]);
                for (component = 0; component < 2; ++component)
                    output[next].uv[component] = previous->uv[component] + t * (current->uv[component] - previous->uv[component]);
                ++next;
            }
            if (distance >= 0.0) output[next++] = *current;
            previous = current;
            previous_distance = distance;
        }
        memcpy(vertices, output, next * sizeof(*vertices));
        count = next;
    }
    return count;
}

static void title_panorama(maage_client_ui *ui) {
    title_camera camera;
    title_vertex polygon[8];
    maage_render_ui_vertex vertices[4];
    maage_client_ui_rect full;
    double yaw, pitch;
    float depths[4];
    size_t first;
    unsigned int count, i, j, corner;
    int face;
    if (ui->width <= 0.0f || ui->height <= 0.0f) return;
    for (i = 1; i <= 6; ++i)
        if (!ui->resources.title_available[i]) return;
    /* A constant elevation keeps the horizon stationary during the horizontal
     * sweep. Monotonic seconds set the yaw rate independently of frame rate. */
    yaw = ui->notice_now * 0.03490658504;
    pitch = 20.0 * 0.01745329252;
    camera.cy = cos(yaw);
    camera.sy = sin(yaw);
    camera.cp = cos(pitch);
    camera.sp = sin(pitch);
    camera.width = ui->width;
    camera.height = ui->height;
    /* Equal X/Y focal scale crops a square 120-degree view to the viewport. */
    camera.focal = (camera.width > camera.height ? camera.width : camera.height) / 3.46410161514;
    full.x = full.y = 0;
    full.width = ui->width;
    full.height = ui->height;
    maage_client_ui_panel(ui, full, 0.48f, 0.55f, 0.61f, 1.0f);
    for (face = 0; face < 6; ++face) {
        title_face(&camera, face, polygon);
        count = title_clip(&camera, polygon);
        for (i = 1; i + 1 < count; ++i) {
            for (j = 0; j < 4; ++j) {
                corner = j == 0 || j == 3 ? 0 : (j == 1 ? i : i + 1);
                vertices[j].position[0] = (float) (camera.width * 0.5 + camera.focal * polygon[corner].point[0] / polygon[corner].point[2]);
                vertices[j].position[1] = (float) (camera.height * 0.5 + camera.focal * polygon[corner].point[1] / polygon[corner].point[2]);
                /* Full face bounds at texel centers avoid neighboring atlas tiles. */
                vertices[j].uv[0] = (float) (512 + ((face + 1) % 4) * 256 + 0.5 + (polygon[corner].uv[0] + 1.0) * 127.5) / ui->resources.atlas_edge;
                vertices[j].uv[1] = (float) (((face + 1) / 4) * 256 + 0.5 + (polygon[corner].uv[1] + 1.0) * 127.5) / ui->resources.atlas_edge;
                vertices[j].color[0] = vertices[j].color[1] = vertices[j].color[2] = vertices[j].color[3] = 1.0f;
                depths[j] = (float) polygon[corner].point[2];
            }
            first = ui->batch.vertex_count;
            if (!maage_client_ui_append_quads(ui, vertices, 1)) return;
            for (j = 0; j < 4; ++j)
                ui->projective_depth[first + j] = depths[j];
        }
    }
    maage_client_ui_panel(ui, full, 0.38f, 0.36f, 0.43f, 0.28f);
    /* Finished panorama. The renderer filters this composed segment before
     * drawing the crisp logo, splash text, widgets, and footer. */
    ui->batch.gaussian_background_indices = ui->batch.index_count;
}
static maage_client_ui_rect title_button(const maage_client_ui *ui, unsigned int id) {
    maage_client_ui_rect r;
    float cx = ui->content.x + ui->content.width * 0.5f;
    float y = ui->content.y + (float) (int) (ui->content.height / 4) + 48;
    r.y = y + (id < 3 ? 24 * (float) id : 84);
    r.height = 20;
    r.width = id == 3 ? 20 : (id < 3 ? 200 : 98);
    r.x = id == 3 ? cx - 124 : (id == 4 ? cx - 100 : (id == 5 ? cx + 2 : cx - 100));
    return r;
}
static int title_hit(const maage_client_ui *ui, maage_client_ui_rect r) {
    return ui->mouse_x >= r.x && ui->mouse_x < r.x + r.width && ui->mouse_y >= r.y && ui->mouse_y < r.y + r.height;
}
static void title_logo(maage_client_ui *ui) {
    maage_client_ui_rect r;
    float u = 512.0f / ui->resources.atlas_edge;
    float cx = ui->content.x + (float) (int) (ui->content.width * 0.5f);
    if (!ui->resources.title_available[0]) return;
    r.x = cx - 137;
    r.y = ui->content.y + 30;
    r.width = 155;
    r.height = 44;
    maage_client_ui_rectangle(ui, r, u, 0, u + 155.0f / ui->resources.atlas_edge, 44.0f / ui->resources.atlas_edge, 1, 1, 1, 1);
    r.x += 155;
    r.width = 155;
    maage_client_ui_rectangle(ui, r, u, 45.0f / ui->resources.atlas_edge, u + 155.0f / ui->resources.atlas_edge, 89.0f / ui->resources.atlas_edge, 1, 1, 1, 1);
}
static void title_splash(maage_client_ui *ui) {
    size_t first = ui->batch.vertex_count, i;
    double radians = -0.3490658504, c = cos(radians), s = sin(radians);
    double scale = (1.8 - fabs(sin(ui->notice_now * 6.28318530718) * 0.1)) * 100.0 / (maage_client_ui_text_width(ui, "Engage!") + 32);
    float x = ui->content.x + ui->content.width * 0.5f + 90, y = ui->content.y + 70;
    maage_client_ui_text(ui, "Engage!", x + 1, y + 1, MAAGE_CLIENT_UI_CENTER, 0.18f, 0.18f, 0);
    maage_client_ui_text(ui, "Engage!", x, y, MAAGE_CLIENT_UI_CENTER, 1, 1, 0);
    for (i = first; i < ui->batch.vertex_count; ++i) {
        double dx = (ui->vertices[i].position[0] - x) * scale;
        double dy = (ui->vertices[i].position[1] - y - 4) * scale;
        ui->vertices[i].position[0] = (float) (x + dx * c - dy * s);
        ui->vertices[i].position[1] = (float) (y + dx * s + dy * c);
    }
}
void maage_client_ui_title_draw(maage_client_ui *ui, void *context) {
    maage_client_ui_frontend *f = (maage_client_ui_frontend *) context;
    maage_client_ui_rect r;
    title_panorama(ui);
    title_logo(ui);
    title_splash(ui);
    r = title_button(ui, 0);
    maage_client_ui_button(ui, r, "Singleplayer", (f->focus_keyboard ? f->focus == 0 : title_hit(ui, r)));
    r = title_button(ui, 1);
    maage_client_ui_button_state(ui, r, "Multiplayer", MAAGE_CLIENT_UI_BUTTON_DISABLED);
    r = title_button(ui, 2);
    maage_client_ui_button_state(ui, r, "Minecraft Realms", MAAGE_CLIENT_UI_BUTTON_DISABLED);
    /* The 1.7.10 language globe is in widgets.png at (0,106/126),
     * not a separate language.png resource. */
    r = title_button(ui, 3);
    maage_client_ui_rectangle(ui, r, 256.0f / ui->resources.atlas_edge, 106.0f / ui->resources.atlas_edge, 276.0f / ui->resources.atlas_edge, 126.0f / ui->resources.atlas_edge, 1, 1, 1, 1);
    r = title_button(ui, 4);
    maage_client_ui_button(ui, r, "Options...", (f->focus_keyboard ? f->focus == 4 : title_hit(ui, r)));
    r = title_button(ui, 5);
    maage_client_ui_button(ui, r, "Quit Game", (f->focus_keyboard ? f->focus == 5 : title_hit(ui, r)));
    maage_client_ui_text(ui, "Minecraft 1.7.10", 3, ui->height - 9, MAAGE_CLIENT_UI_LEFT, 0.14f, 0.14f, 0.14f);
    maage_client_ui_text(ui, "Minecraft 1.7.10", 2, ui->height - 10, MAAGE_CLIENT_UI_LEFT, 1, 1, 1);
    maage_client_ui_text(ui, "Copyright Mojang AB. Do not distribute!", ui->width - 1, ui->height - 9, MAAGE_CLIENT_UI_RIGHT, 0.14f, 0.14f, 0.14f);
    maage_client_ui_text(ui, "Copyright Mojang AB. Do not distribute!", ui->width - 2, ui->height - 10, MAAGE_CLIENT_UI_RIGHT, 1, 1, 1);
}
unsigned int maage_client_ui_title_input(maage_client_ui *ui, const maage_client_ui_input *input, void *context) {
    maage_client_ui_frontend *f = (maage_client_ui_frontend *) context;
    int hover = -1;
    unsigned int ids[3] = {0, 4, 5}, k;
    /* Keyboard focus is retained only after keyboard navigation. Pointer
     * movement clears it even when the pointer leaves every button. */
    if (ui->mouse_x != ui->previous_mouse_x || ui->mouse_y != ui->previous_mouse_y || input->primary_pressed) f->focus_keyboard = 0;
    if (input->next || input->previous) {
        for (k = 0; k < 3; ++k)
            if (f->focus == ids[k]) break;
        if (k == 3) k = 0;
        if (input->next) k = (k + 1) % 3;
        else
            k = (k + 2) % 3;
        f->focus = ids[k];
        f->focus_keyboard = 1;
    }
    for (k = 0; k < 3; ++k)
        if (title_hit(ui, title_button(ui, ids[k]))) hover = (int) ids[k];
    if (!f->focus_keyboard && hover >= 0) f->focus = (unsigned int) hover;
    if (!(input->accept || (input->primary_pressed && hover >= 0))) return 0;
    if (input->primary_pressed && hover >= 0) f->focus = (unsigned int) hover;
    /* Inactive buttons are neither selectable nor audible. The UI callback
     * is invoked only when a real title action is accepted. */
    if (f->focus == 0) {
        if (ui->click) ui->click(ui->click_context);
        f->action = 1;
        ui->route_result = maage_client_ui_open(ui, MAAGE_CLIENT_UI_WORLDS);
    } else if (f->focus == 4) {
        if (ui->click) ui->click(ui->click_context);
        ui->route_result = maage_client_ui_open(ui, MAAGE_CLIENT_UI_OPTIONS);
    } else if (f->focus == 5) {
        if (ui->click) ui->click(ui->click_context);
        f->quit = 1;
    }
    return 0;
}
