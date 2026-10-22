#include "frontend.h"
#include "pacing.h"
#include "ui/front.h"
#include "maage/world_store.h"
#include "maage/jobs.h"
#include "maage/input.h"
#include "maage/time.h"
#include "maage/log.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
typedef struct maage_front_io {
    const char *root;
    const maage_item_registry *items;
    maage_world_summary *entries;
    size_t count;
    char id[37], name[128], error[256];
    unsigned int operation;
    maage_status result;
} maage_front_io;
struct maage_client_frontend {
    maage_client_ui *ui;
    maage_client_ui_frontend *view;
    const char *root;
    const maage_item_registry *items;
    maage_jobs *jobs;
    maage_front_io io;
    maage_world_summary *entries;
    size_t count;
    maage_client_options saved_options;
    const maage_instance *instance;
    int busy;
};
static void front_io_run(void *context,const maage_job_token *token)
{
    maage_front_io *io=(maage_front_io *)context;
    maage_status_trace *trace=maage_job_trace(token);
    io->result=0;
    if(io->operation==2)io->result=maage_world_store_rename(io->root,io->id,io->name,io->items,io->error,trace);
    if(io->operation==3)io->result=maage_world_store_delete(io->root,io->id,io->error,trace);
    if(!maage_status_failed(io->result))io->result=maage_world_store_list(io->root,io->items,&io->entries,&io->count,io->error,trace);
    maage_job_set_result(token,io->result);
}
maage_status maage_client_frontend_create(maage_client_ui *ui,const maage_instance *instance,
    const maage_item_registry *items,maage_client_frontend **output,maage_status_trace *trace)
{
    maage_client_frontend *f;
    maage_status result;
    if(output)*output=NULL;
    if(!ui||!instance||!items||!output)
        return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT,MAAGE_CONTEXT_GUI,MAAGE_OPERATION_CREATE,0);
    f=(maage_client_frontend *)calloc(1,sizeof(*f));
    if(!f)return maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED,MAAGE_CONTEXT_GUI,MAAGE_OPERATION_CREATE,0);
    f->ui=ui;f->root=maage_instance_directory(instance);f->instance=instance;f->items=items;
    result=maage_jobs_create(1,1,&f->jobs,trace);
    if(maage_status_failed(result)){free(f);return result;}
    result=maage_client_ui_front_register(ui,&f->view);
    if(maage_status_failed(result)){maage_jobs_destroy(f->jobs,NULL);free(f);return result;}
    *output=f;return result;
}
maage_status maage_client_frontend_run(maage_client_frontend *f,maage_window_system *system,
    maage_client_world_choice *choice,maage_status_trace *trace)
{
    maage_client_ui *ui=f->ui;
    maage_window_state state;
    maage_input_snapshot input;
    maage_client_ui_input routed;
    maage_job_completion completion;
    maage_job_id job;
    const maage_render_ui_data *draw;
    maage_status result;
    unsigned int actions;
    double start;
    maage_client_pacing pacing = {0, 0.0};
    int resize_pending = 0;
    float matrix[16];
    int i, was_options, was_transitioning;
    maage_client_options options;
    maage_client_ui_front_request request;
    memset(choice,0,sizeof(*choice));memset(matrix,0,sizeof(matrix));for(i=0;i<4;++i)matrix[i*5]=1;
    maage_client_ui_get_options(ui,&f->saved_options);
    maage_client_ui_front_begin(f->view);
    result=maage_client_ui_open_root(ui,MAAGE_CLIENT_UI_TITLE);if(maage_status_failed(result))return result;
    result=maage_window_capture_mouse(maage_client_ui_window(ui),0);if(maage_status_failed(result))return result;
    result=maage_window_set_visible(maage_client_ui_window(ui),1);if(maage_status_failed(result))return result;
    for(;;) {
        start=maage_time_monotonic_seconds();if(start<0)return maage_status_make(MAAGE_STATUS_ERROR,MAAGE_CONTEXT_CORE,MAAGE_OPERATION_QUERY,0);
        result=maage_window_poll_events(system,0);if(maage_status_failed(result))return result;
        result=maage_window_get_state(maage_client_ui_window(ui),&state);if(maage_status_failed(result))return result;
        if(state.close_requested)return maage_status_make(MAAGE_STATUS_NOOP,0,MAAGE_OPERATION_SELECT,0);
        result=maage_window_get_input(maage_client_ui_window(ui),&input);if(maage_status_failed(result))return result;
        was_transitioning=maage_client_ui_window_mode_changing(ui);
        result=maage_client_ui_set_window_state(ui,&state);if(maage_status_failed(result))maage_client_ui_notify(ui,result,start);
        maage_client_ui_set_safe_area(ui,&state.safe_area);maage_client_ui_notice_time(ui,start);
        memset(&routed,0,sizeof(routed));
        routed.escape=input.pressed[MAAGE_INPUT_KEY_ESCAPE];routed.f11=input.pressed[MAAGE_INPUT_KEY_F11];
        routed.accept=input.pressed[MAAGE_INPUT_KEY_ENTER];routed.next=input.pressed[MAAGE_INPUT_KEY_TAB]||input.pressed[MAAGE_INPUT_KEY_DOWN];
        if(maage_client_ui_current(ui)==MAAGE_CLIENT_UI_WORLDS) {
            routed.next=input.pressed[MAAGE_INPUT_KEY_TAB];routed.list_up=input.pressed[MAAGE_INPUT_KEY_UP];routed.list_down=input.pressed[MAAGE_INPUT_KEY_DOWN];
        }
        routed.previous=maage_client_ui_current(ui)==MAAGE_CLIENT_UI_WORLDS?0:input.pressed[MAAGE_INPUT_KEY_UP];routed.left=input.pressed[MAAGE_INPUT_KEY_LEFT];routed.right=input.pressed[MAAGE_INPUT_KEY_RIGHT];
        routed.primary_pressed=input.primary_pressed;routed.primary_down=input.primary_down;
        routed.mouse_x=(float)input.mouse_x;routed.mouse_y=(float)input.mouse_y;routed.text=input.text;routed.scroll=input.scroll_y;
        was_options=maage_client_ui_options_visible(ui);
        result=maage_client_ui_update(ui,&routed,state.framebuffer_width,state.framebuffer_height,state.width,state.height,&actions);
        if(maage_status_failed(result))return result;
        if((was_options&&!maage_client_ui_options_visible(ui)) ||
            (was_transitioning&&!maage_client_ui_window_mode_changing(ui))) {
            maage_client_ui_get_options(ui,&options);
            if(!maage_client_options_equal(&options,&f->saved_options)) {
                result=maage_client_options_save(f->instance,&options);
                if(maage_status_failed(result))maage_client_ui_notify(ui,result,start);
                else f->saved_options=options;
            }
        }
        if(f->busy&&maage_status_code(maage_jobs_poll(f->jobs,&completion))==MAAGE_STATUS_SUCCESS) {
            f->busy=0;
            if(maage_status_failed(completion.result)) {
                maage_log_trace_status(MAAGE_LOG_WARNING,"maage.client.worlds",
                    completion.result,&completion.trace,"World catalog operation failed: %s",f->io.error);
                maage_client_ui_front_error(f->view,"World operation failed; see logs/client.log");
            } else {
                free(f->entries);f->entries=f->io.entries;f->count=f->io.count;f->io.entries=NULL;
                maage_client_ui_front_complete(f->view,f->entries,f->count,f->io.operation);
            }
        }
        if(maage_client_ui_front_take(f->view,&request)) {
            if(request.action==6)return maage_status_make(MAAGE_STATUS_NOOP,0,MAAGE_OPERATION_SELECT,0);
            if((request.action==4||request.action==5)&&!f->busy) {
                if(request.action==4) {
                    if(request.selected>=f->count)return maage_status_make(
                        MAAGE_STATUS_INVALID_STATE,MAAGE_CONTEXT_GUI,MAAGE_OPERATION_SELECT,0);
                    strcpy(choice->id,f->entries[request.selected].id);
                } else {
                    maage_world_seed parsed;
                    if(request.seed[0]&&maage_status_failed(maage_world_seed_parse(request.seed,&parsed))) {
                        maage_client_ui_front_error(f->view,"Enter a signed 64-bit seed.");
                        continue;
                    }
                    choice->create=1;
                    strcpy(choice->name,request.name);strcpy(choice->seed,request.seed);
                }
                choice->mode=request.mode;
                return maage_status_make(MAAGE_STATUS_SUCCESS,0,MAAGE_OPERATION_SELECT,0);
            }
            if(request.action<=3&&!f->busy) {
                memset(&f->io,0,sizeof(f->io));
                f->io.root=f->root;f->io.items=f->items;f->io.operation=request.action;
                if(request.selected<f->count)strcpy(f->io.id,f->entries[request.selected].id);
                strcpy(f->io.name,request.name);
                result=maage_jobs_submit(f->jobs,0,front_io_run,&f->io,&job,trace);
                if(maage_status_failed(result))return result;
                f->busy=1;maage_client_ui_front_busy(f->view,1);
            }
        }
        result=maage_client_ui_draw(ui,NULL,&draw);if(maage_status_failed(result))return result;
        result=maage_renderer_draw_frame(maage_client_ui_renderer(ui),NULL,0,matrix,draw,trace);if(maage_status_failed(result))return result;
        if(maage_status_code(result)==MAAGE_STATUS_NOOP) {
            result=maage_window_wait_events(system,state.visible?1:16);
            if(maage_status_failed(result))return result;
        }
        result=maage_client_pacing_wait(&pacing,system,maage_client_ui_window(ui),
            maage_client_ui_max_fps(ui),start,&resize_pending);
        if(maage_status_failed(result))return result;
    }
}
maage_status maage_client_frontend_destroy(maage_client_frontend *f)
{
    maage_status result;
    if(!f)return maage_status_make(MAAGE_STATUS_NOOP,0,MAAGE_OPERATION_DESTROY,0);
    result=maage_jobs_destroy(f->jobs,NULL);if(maage_status_failed(result))return result;
    maage_client_ui_front_destroy(f->view);
    free(f->entries);free(f->io.entries);free(f);return result;
}

maage_status maage_client_frontend_loading(maage_client_frontend *f, maage_status_trace *trace)
{
    maage_window_state state;
    maage_client_ui_input input;
    const maage_render_ui_data *draw;
    maage_status result;
    unsigned int actions;
    float matrix[16];
    int i;
    if (maage_client_ui_current(f->ui) != MAAGE_CLIENT_UI_LOADING) {
        result = maage_client_ui_open_root(f->ui,MAAGE_CLIENT_UI_LOADING);
        if (maage_status_failed(result)) return result;
    }
    result = maage_window_get_state(maage_client_ui_window(f->ui), &state);
    if (maage_status_failed(result)) return result;
    memset(&input, 0, sizeof(input));
    result = maage_client_ui_update(f->ui, &input, state.framebuffer_width,
        state.framebuffer_height, state.width, state.height, &actions);
    if (maage_status_failed(result)) return result;
    result = maage_client_ui_draw(f->ui, NULL, &draw);
    if (maage_status_failed(result)) return result;
    memset(matrix, 0, sizeof(matrix));
    for (i = 0; i < 4; ++i) matrix[i*5] = 1;
    return maage_renderer_draw_frame(maage_client_ui_renderer(f->ui), NULL, 0, matrix, draw, trace);
}
