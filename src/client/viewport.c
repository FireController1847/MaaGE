#include "maage/window.h"
#include "maage/render.h"
#include "maage/log.h"
/* The Client owns its native window and graphics device in its own process.
 * This initial viewport is not gameplay and does not acquire Minecraft assets. */
int maage_client_run(int argc, char *const argv[])
{
    maage_window_system *system=NULL;
    maage_window *window=NULL;
    maage_renderer *renderer=NULL;
    maage_window_config window_config;
    maage_window_state state;
    maage_render_desc render_config;
    maage_window_result result;
    maage_render_result rendered;
    int ok=0;
    maage_render_desc_init(&render_config);
    rendered=maage_render_desc_parse_args(argc,argv,&render_config);
    if(rendered!=MAAGE_RENDER_OK) goto done;
    result=maage_window_system_create(MAAGE_WINDOW_AUTO,&system);
    if(result!=MAAGE_WINDOW_OK) goto window_failed;
    maage_window_config_init(&window_config);
    window_config.title="MaaGE Client"; window_config.visible=0;
    result=maage_window_create(system,&window_config,&window);
    if(result!=MAAGE_WINDOW_OK) goto window_failed;
    render_config.window=window;
    rendered=maage_renderer_create(&render_config,&renderer);
    if(rendered!=MAAGE_RENDER_OK) goto done;
    rendered=maage_renderer_set_clear_color(renderer,.08f,.08f,.10f,1.0f);
    if(rendered!=MAAGE_RENDER_OK) goto done;
    rendered=maage_renderer_prepare_frame(renderer);
    if(rendered==MAAGE_RENDER_UNSUPPORTED) rendered=MAAGE_RENDER_OK;
    if(rendered!=MAAGE_RENDER_OK && rendered!=MAAGE_RENDER_SKIPPED) goto done;
    result=maage_window_set_visible(window,1);
    if(result!=MAAGE_WINDOW_OK) goto window_failed;
    maage_log_info("maage.client","Client viewport initialized; gameplay is not implemented.");
    for(;;) {
        result=maage_window_poll_events(system,0);
        if(result!=MAAGE_WINDOW_OK) goto window_failed;
        result=maage_window_get_state(window,&state);
        if(result!=MAAGE_WINDOW_OK) goto window_failed;
        if(state.close_requested) { ok=1; break; }
        rendered=maage_renderer_present(renderer);
        if(rendered!=MAAGE_RENDER_OK && rendered!=MAAGE_RENDER_SKIPPED) goto done;
        result=maage_window_wait_events(system,rendered==MAAGE_RENDER_SKIPPED ? 100 : 16);
        if(result!=MAAGE_WINDOW_OK) goto window_failed;
    }
    goto done;
window_failed:
    maage_log_error("maage.client","Native window failed: %s.",maage_window_result_string(result));
done:
    if(rendered!=MAAGE_RENDER_OK && rendered!=MAAGE_RENDER_SKIPPED)
        maage_log_error("maage.client","Renderer failed: %s.",maage_render_result_string(rendered));
    maage_renderer_destroy(renderer);
    if(window) maage_window_destroy(window);
    if(system) maage_window_system_destroy(system);
    return ok ? 0 : 1;
}
