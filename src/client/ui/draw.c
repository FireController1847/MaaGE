#include "internal.h"
#include "maage/log.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

/* Retain geometry between frames; index offsets are limited by unsigned int. */
static int maage_client_ui_reserve_quad(maage_client_ui *ui)
{
    size_t maximum, required, capacity;
    maage_render_ui_vertex *vertices;
    unsigned int *indices;
    unsigned char failure = MAAGE_STATUS_ALLOCATION_FAILED;
    if (maage_status_failed(ui->draw_result)) return 0;
    maximum = ((size_t)-1) / sizeof(*ui->vertices) / 4;
    if (maximum > ((size_t)-1) / sizeof(*ui->indices) / 6)
        maximum = ((size_t)-1) / sizeof(*ui->indices) / 6;
    if (maximum > (size_t)(UINT_MAX / 4U)) maximum = (size_t)(UINT_MAX / 4U);
    required = ui->batch.vertex_count / 4 + 1;
    if (required <= ui->quad_capacity) return 1;
    if (required > maximum) { failure = MAAGE_STATUS_BUFFER_OVERFLOW; goto failed; }
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
    ui->draw_result = maage_status_make(failure, MAAGE_CONTEXT_GUI, MAAGE_OPERATION_ATLAS, 0);
    return 0;
}

int maage_client_ui_append_quads(maage_client_ui *ui,
    const maage_render_ui_vertex *vertices, size_t count)
{
    static const unsigned int order[6] = {0, 1, 2, 0, 2, 3};
    size_t quad, first, i;
    for (quad = 0; quad < count; ++quad) {
        if (!maage_client_ui_reserve_quad(ui)) return 0;
        first = ui->batch.vertex_count;
        memcpy(ui->vertices + first, vertices + quad * 4, 4 * sizeof(*vertices));
        for (i = 0; i < 6; ++i)
            ui->indices[ui->batch.index_count + i] = (unsigned int)first + order[i];
        ui->batch.vertex_count += 4; ui->batch.index_count += 6;
    }
    return 1;
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


/* Draw the original tiled options dirt across the complete framebuffer,
 * including macOS borderless/notch margins. Only subsequent interactive
 * elements use ui->content for safe-area positioning. */
void maage_client_ui_menu_background(maage_client_ui *ui)
{
    const maage_block_state_definition *dirt;
    maage_client_ui_rect previous, full, tile;
    float uv[4], x, y;
    int available = 0;
    if (!ui) return;
    previous = ui->clip;
    full.x = full.y = 0.0f;
    full.width = ui->width;
    full.height = ui->height;
    ui->clip = full;
    maage_client_ui_panel(ui, full, 0.16f, 0.13f, 0.10f, 1.0f);
    if (ui->resources.menu_background_available) {
        memcpy(uv, ui->resources.menu_background_uv, sizeof(uv));
        available = 1;
    } else {
        dirt = ui->blocks ? maage_block_registry_state(ui->blocks, MAAGE_STATE_DIRT) : NULL;
        if (dirt && ui->resources.atlas_edge > 0) {
            uv[0] = dirt->faces[0].uv[0] *
                ui->resources.block_atlas_width / ui->resources.atlas_edge;
            uv[2] = dirt->faces[0].uv[2] *
                ui->resources.block_atlas_width / ui->resources.atlas_edge;
            uv[1] = (dirt->faces[0].uv[1]*ui->resources.block_atlas_height +
                MAAGE_CLIENT_UI_BLOCK_ATLAS_Y) / ui->resources.atlas_edge;
            uv[3] = (dirt->faces[0].uv[3]*ui->resources.block_atlas_height +
                MAAGE_CLIENT_UI_BLOCK_ATLAS_Y) / ui->resources.atlas_edge;
            available = uv[2] > uv[0] && uv[3] > uv[1];
        }
    }
    if (available) {
        tile.width = tile.height = 32.0f;
        for (y=0.0f; y < full.height; y+=32.0f)
            for (x=0.0f; x < full.width; x+=32.0f) {
                tile.x=x; tile.y=y;
                maage_client_ui_rectangle(ui,tile,uv[0],uv[1],uv[2],uv[3],
                    0.25f,0.25f,0.25f,1.0f);
            }
    }
    ui->clip = previous;
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

void maage_client_ui_button_state(maage_client_ui *ui, maage_client_ui_rect rectangle,
    const char *label, maage_client_ui_button_visual state)
{
    maage_client_ui_rect part;
    float v, center, tint, cap;
    float tx[4], uv[4], y0[3], ty[3];
    int col, row;
    if (!ui || !label || state < MAAGE_CLIENT_UI_BUTTON_DISABLED ||
        state > MAAGE_CLIENT_UI_BUTTON_HOVER ||
        rectangle.width <= 0.0f || rectangle.height <= 0.0f) return;
    /* Six-slice the 200x20 widgets.png button (3 columns x 2 rows).
     * Native-size endcaps preserve borders and corners on half-width buttons.
     * Only the long middle section is stretched. */
    cap = rectangle.width < 4.0f ? rectangle.width * 0.25f : 2.0f;
    v = 46.0f + (float)state * 20.0f;
    tx[0] = rectangle.x;
    tx[1] = tx[0] + cap;
    tx[2] = rectangle.x + rectangle.width - cap;
    tx[3] = rectangle.x + rectangle.width;
    uv[0] = 0.0f;
    uv[1] = cap;
    uv[2] = 200.0f - cap;
    uv[3] = 200.0f;
    /* The middle is a single stretchable region. Only its interior scales;
     * corner pixels are never stretched. */
    y0[0] = rectangle.y;
    y0[1] = rectangle.y + rectangle.height * 0.5f;
    y0[2] = rectangle.y + rectangle.height;
    ty[0] = v; ty[1] = v + 10.0f; ty[2] = v + 20.0f;
    for (row = 0; row < 2; ++row) {
        for (col = 0; col < 3; ++col) {
            part.x = tx[col]; part.y = y0[row];
            part.width = tx[col+1]-tx[col];
            part.height = y0[row+1]-y0[row];
            maage_client_ui_rectangle(ui,part,
                (256.0f+uv[col])/ui->resources.atlas_edge,
                ty[row]/ui->resources.atlas_edge,
                (256.0f+uv[col+1])/ui->resources.atlas_edge,
                ty[row+1]/ui->resources.atlas_edge,1,1,1,1);
        }
    }
    center=rectangle.x+rectangle.width*0.5f;
    maage_client_ui_text(ui,label,center+1,rectangle.y+7,
        MAAGE_CLIENT_UI_CENTER,0.22f,0.22f,0.22f);
    tint=state==MAAGE_CLIENT_UI_BUTTON_DISABLED?0.63f:
        (state==MAAGE_CLIENT_UI_BUTTON_HOVER?1.0f:0.88f);
    maage_client_ui_text(ui,label,center,rectangle.y+6,
        MAAGE_CLIENT_UI_CENTER,tint,tint,
        state==MAAGE_CLIENT_UI_BUTTON_HOVER?0.63f:tint);
}
void maage_client_ui_button(maage_client_ui *ui, maage_client_ui_rect rectangle,
    const char *label, int highlighted)
{
    maage_client_ui_button_state(ui,rectangle,label,
        highlighted?MAAGE_CLIENT_UI_BUTTON_HOVER:MAAGE_CLIENT_UI_BUTTON_NORMAL);
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
