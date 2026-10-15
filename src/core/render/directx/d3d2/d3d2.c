#include "../../render_internal.h"

/* Reserved boundary only. A future adapter needs the historical DirectDraw /
 * IDirect3D2 SDK and its own compiler configuration. No invented declarations,
 * modern SDK dependency, fixed-function promise, or successful no-op rendering. */
static maage_render_result maage_d3d2_create(maage_renderer *renderer)
{
    (void)renderer;
    return MAAGE_RENDER_UNSUPPORTED;
}
static void maage_d3d2_destroy(maage_renderer *renderer) { (void)renderer; }
static maage_render_result maage_d3d2_present(maage_renderer *renderer, const maage_window_state *state)
{
    (void)renderer; (void)state;
    return MAAGE_RENDER_UNSUPPORTED;
}
const maage_render_backend_api maage_render_d3d2_api = {
    MAAGE_RENDER_D3D2, maage_d3d2_create, maage_d3d2_destroy, maage_d3d2_present, 0
};
