#include "internal.h"
#include "maage/log.h"
#include <limits.h>
#include <stdlib.h>

/* Retain geometry between frames; index offsets are limited by unsigned int. */
static int maage_client_ui_reserve_quad(maage_client_ui *ui)
{
    size_t maximum, required, capacity;
    maage_render_ui_vertex *vertices;
    unsigned int *indices;
    if (ui->overflow) return 0;
    maximum = ((size_t)-1) / sizeof(*ui->vertices) / 4;
    if (maximum > ((size_t)-1) / sizeof(*ui->indices) / 6)
        maximum = ((size_t)-1) / sizeof(*ui->indices) / 6;
    if (maximum > (size_t)(UINT_MAX / 4U)) maximum = (size_t)(UINT_MAX / 4U);
    required = ui->batch.vertex_count / 4 + 1;
    if (required <= ui->quad_capacity) return 1;
    if (required > maximum) goto failed;
    capacity = ui->quad_capacity ? ui->quad_capacity : 256;
    while (capacity < required) {
        if (capacity > maximum / 2) { capacity = maximum; break; }
        capacity *= 2;
    }
    vertices = (maage_render_ui_vertex *)realloc(ui->vertices,
        capacity * 4 * sizeof(*vertices));
    if (!vertices) goto failed;
    ui->vertices = vertices;
    ui->batch.vertices = vertices;
    indices = (unsigned int *)realloc(ui->indices, capacity * 6 * sizeof(*indices));
    if (!indices) goto failed;
    ui->indices = indices;
    ui->batch.indices = indices;
    ui->quad_capacity = capacity;
    return 1;
failed:
    ui->overflow = 1;
    if (!ui->overflow_reported) {
        maage_log_error("maage.client.ui", "GUI geometry allocation or index range exhausted; frame rejected.");
        ui->overflow_reported = 1;
    }
    return 0;
}

void maage_client_ui_rectangle(maage_client_ui *ui, maage_client_ui_rect rectangle,
    float u0, float v0, float u1, float v1, float red, float green, float blue, float alpha)
{
    static const unsigned int order[6] = {0, 1, 2, 0, 2, 3};
    maage_render_ui_vertex *vertex;
    float x0, y0, x1, y1, uv[4], positions[4][2];
    size_t first, i;
    if (!ui || rectangle.width <= 0 || rectangle.height <= 0) return;
    x0 = rectangle.x > ui->clip.x ? rectangle.x : ui->clip.x;
    y0 = rectangle.y > ui->clip.y ? rectangle.y : ui->clip.y;
    x1 = rectangle.x + rectangle.width;
    y1 = rectangle.y + rectangle.height;
    if (x1 > ui->clip.x + ui->clip.width) x1 = ui->clip.x + ui->clip.width;
    if (y1 > ui->clip.y + ui->clip.height) y1 = ui->clip.y + ui->clip.height;
    if (x1 <= x0 || y1 <= y0) return;
    if (!maage_client_ui_reserve_quad(ui)) return;
    uv[0] = u0 + (u1 - u0) * (x0 - rectangle.x) / rectangle.width;
    uv[1] = v0 + (v1 - v0) * (y0 - rectangle.y) / rectangle.height;
    uv[2] = u0 + (u1 - u0) * (x1 - rectangle.x) / rectangle.width;
    uv[3] = v0 + (v1 - v0) * (y1 - rectangle.y) / rectangle.height;
    positions[0][0] = positions[3][0] = x0;
    positions[1][0] = positions[2][0] = x1;
    positions[0][1] = positions[1][1] = y0;
    positions[2][1] = positions[3][1] = y1;
    first = ui->batch.vertex_count;
    for (i = 0; i < 4; ++i) {
        vertex = &ui->vertices[first + i];
        vertex->position[0] = positions[i][0]; vertex->position[1] = positions[i][1];
        vertex->uv[0] = (i == 0 || i == 3) ? uv[0] : uv[2];
        vertex->uv[1] = i < 2 ? uv[1] : uv[3];
        vertex->color[0] = red; vertex->color[1] = green;
        vertex->color[2] = blue; vertex->color[3] = alpha;
    }
    for (i = 0; i < 6; ++i) ui->indices[ui->batch.index_count + i] = (unsigned int)first + order[i];
    ui->batch.vertex_count += 4; ui->batch.index_count += 6;
}

void maage_client_ui_panel(maage_client_ui *ui, maage_client_ui_rect rectangle,
    float red, float green, float blue, float alpha)
{
    float white;
    if (!ui || !ui->resources.atlas_edge) return;
    white = (ui->resources.atlas_edge - 0.5f) / ui->resources.atlas_edge;
    maage_client_ui_rectangle(ui, rectangle, white, white, white, white, red, green, blue, alpha);
}

void maage_client_ui_text(maage_client_ui *ui, const char *text, float x, float y,
    maage_client_ui_alignment alignment, float red, float green, float blue)
{
    float width;
    if (!ui || !text) return;
    width = maage_client_ui_text_width(ui, text);
    if (alignment == MAAGE_CLIENT_UI_CENTER) x -= width * 0.5f;
    else if (alignment == MAAGE_CLIENT_UI_RIGHT) x -= width;
    maage_client_ui_font_draw(ui, text, x, y, red, green, blue);
}

void maage_client_ui_button(maage_client_ui *ui, maage_client_ui_rect rectangle,
    const char *label, int highlighted)
{
    maage_client_ui_rect half;
    float v, center;
    if (!ui) return;
    v = (float)(highlighted ? 86 : 66) / ui->resources.atlas_edge;
    half = rectangle; half.width *= 0.5f;
    maage_client_ui_rectangle(ui, half, 256.0f / ui->resources.atlas_edge, v,
        356.0f / ui->resources.atlas_edge, v + 20.0f / ui->resources.atlas_edge, 1, 1, 1, 1);
    half.x += half.width;
    maage_client_ui_rectangle(ui, half, 356.0f / ui->resources.atlas_edge, v,
        456.0f / ui->resources.atlas_edge, v + 20.0f / ui->resources.atlas_edge, 1, 1, 1, 1);
    center = rectangle.x + rectangle.width * 0.5f;
    maage_client_ui_text(ui, label, center + 1, rectangle.y + 7,
        MAAGE_CLIENT_UI_CENTER, 0.22f, 0.22f, 0.22f);
    maage_client_ui_text(ui, label, center, rectangle.y + 6,
        MAAGE_CLIENT_UI_CENTER, highlighted ? 1 : 0.88f, highlighted ? 1 : 0.88f, highlighted ? 0.63f : 0.88f);
}

/* The GPU clips held-item faces; no world or texture work. */
void maage_client_ui_quad(maage_client_ui *ui, const float positions[4][2],
    const float uv[4], float shade)
{
    static const unsigned int order[6] = {0, 1, 2, 0, 2, 3};
    maage_render_ui_vertex *vertex;
    size_t first, i;
    if (!ui || !maage_client_ui_reserve_quad(ui)) return;
    first = ui->batch.vertex_count;
    for (i = 0; i < 4; ++i) {
        vertex = &ui->vertices[first + i];
        vertex->position[0] = positions[i][0]; vertex->position[1] = positions[i][1];
        vertex->uv[0] = i == 0 || i == 3 ? uv[0] : uv[2];
        vertex->uv[1] = i < 2 ? uv[1] : uv[3];
        vertex->color[0] = vertex->color[1] = vertex->color[2] = shade;
        vertex->color[3] = 1;
    }
    for (i = 0; i < 6; ++i)
        ui->indices[ui->batch.index_count + i] = (unsigned int)first + order[i];
    ui->batch.vertex_count += 4; ui->batch.index_count += 6;
}
