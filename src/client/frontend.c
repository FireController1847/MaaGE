#include "frontend.h"
#include "pacing.h"
#include "ui/internal.h"
#include "maage/world_store.h"
#include "maage/jobs.h"
#include "maage/input.h"
#include "maage/time.h"
#include "maage/log.h"
#include "maage/client_version.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* Original MaaGE implementation; Minecraft menus supplied layout/flow reference. */
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
    const char *root;
    const maage_item_registry *items;
    maage_jobs *jobs;
    maage_front_io io;
    maage_world_summary *entries;
    size_t count, selected, first;
    unsigned int action, focus;
    maage_client_options saved_options;
    const maage_instance *instance;
    int busy, quit, play, mode;
    char name[128], seed[64], message[256];
    float dirt_uv[4];
    double last_click;
    size_t last_row;
};
static maage_client_ui_rect front_rect(maage_client_frontend *f, float x, float y, float w, float h)
{
    maage_client_ui_rect r;
    r.x = f->ui->content.x + x; r.y = f->ui->content.y + y;
    r.width = w; r.height = h; return r;
}
static int front_hit(maage_client_frontend *f, maage_client_ui_rect r)
{
    return f->ui->mouse_x >= r.x && f->ui->mouse_x < r.x+r.width &&
        f->ui->mouse_y >= r.y && f->ui->mouse_y < r.y+r.height;
}
static maage_client_ui_rect front_button_rect(maage_client_frontend *f, unsigned int n)
{
    float w = f->ui->content.width, h = f->ui->content.height;
    unsigned int screen = maage_client_ui_current(f->ui);
    if (screen == MAAGE_CLIENT_UI_TITLE) {
        if (n < 2) return front_rect(f, (w-200)/2, h*0.5f + n*24, 200, 20);
        return front_rect(f, w/2-100+(n-2)*102, h*0.5f+60, 98, 20);
    }
    if (screen == MAAGE_CLIENT_UI_WORLDS)
        return front_rect(f, w/2-150+(n%2)*152, h-76+(n/2)*24, 148, 20);
    if (screen == MAAGE_CLIENT_UI_CREATE_WORLD && n == 2)
        return front_rect(f, w/2-100, 120, 200, 20);
    return front_rect(f, w/2-150+n*152, h-30, 148, 20);
}
static void front_button(maage_client_frontend *f, unsigned int n, const char *text, int enabled)
{
    maage_client_ui_rect r = front_button_rect(f,n);
    maage_client_ui_button(f->ui, r, text, enabled && f->focus == n);
    if (!enabled) maage_client_ui_panel(f->ui, r, 0,0,0,0.55f);
}
static size_t front_rows(maage_client_frontend *f)
{
    float available = f->ui->content.height-120;
    return available >= 40 ? (size_t)(available/40) : 1;
}
static void front_center(maage_client_frontend *f, const char *text, float y, float r,float g,float b)
{
    maage_client_ui_text(f->ui,text,f->ui->content.x+f->ui->content.width/2,
        f->ui->content.y+y,MAAGE_CLIENT_UI_CENTER,r,g,b);
}
static void front_field(maage_client_frontend *f, const char *text, float y, unsigned int focus)
{
    maage_client_ui *ui = f->ui;
    maage_client_ui_rect r = front_rect(f, ui->content.width/2-100, y, 200, 20);
    const char *visible = text;
    float edge = f->focus == focus ? 0.85f : 0.4f;
    maage_client_ui_panel(ui, r, edge, edge, edge, 1);
    r.x += 1; r.y += 1; r.width -= 2; r.height -= 2;
    maage_client_ui_panel(ui, r, 0, 0, 0, 1);
    while (*visible && maage_client_ui_text_width(ui, visible) > r.width-12) ++visible;
    ui->clip = r;
    maage_client_ui_text(ui, visible, r.x+4, r.y+5, MAAGE_CLIENT_UI_LEFT, 1, 1, 1);
    if (f->focus == focus)
        maage_client_ui_text(ui, "_", r.x+4+maage_client_ui_text_width(ui, visible),
            r.y+5, MAAGE_CLIENT_UI_LEFT, 1, 1, 1);
    ui->clip = ui->content;
}
static void front_draw(maage_client_ui *ui, void *context)
{
    maage_client_frontend *f = (maage_client_frontend *)context;
    unsigned int screen = maage_client_ui_current(ui), n;
    size_t i, end, first_vertex;
    float w=ui->content.width,h=ui->content.height,y,x;
    maage_client_ui_rect r;
    char line[192];
    /* Layered voxel landscape made from our own GUI geometry; no Java renderer. */
    if (screen == MAAGE_CLIENT_UI_TITLE) {
        r=ui->content; maage_client_ui_panel(ui,r,0.20f,0.38f,0.51f,1);
        for(n=0;n<20;++n) {
            float ridge = (float)((n*17+n*n*7)%43);
            r=front_rect(f,n*w/20,h*0.48f+ridge,w/20+1,h);
            maage_client_ui_panel(ui,r,0.16f,0.27f,0.25f,1);
            r=front_rect(f,n*w/20,h*0.69f+ridge/2,w/20+1,h);
            maage_client_ui_panel(ui,r,0.09f,0.18f,0.14f,1);
        }
        first_vertex=ui->batch.vertex_count;
        front_center(f,"MaaGE",35,0.9f,0.9f,0.9f);
        for(i=first_vertex;i<ui->batch.vertex_count;++i) {
            ui->vertices[i].position[0]=ui->content.x+w/2+(ui->vertices[i].position[0]-ui->content.x-w/2)*4;
            ui->vertices[i].position[1]=ui->content.y+35+(ui->vertices[i].position[1]-ui->content.y-35)*4;
        }
        front_center(f,"A world of possibilities",82,1,0.86f,0.2f);
        front_button(f,0,"Singleplayer",1);
        front_button(f,1,"Multiplayer (unavailable)",0);
        front_button(f,2,"Options...",1); front_button(f,3,"Quit Game",1);
        maage_client_ui_text(ui,MAAGE_CLIENT_SEMANTIC_VERSION,ui->content.x+4,ui->content.y+h-12,MAAGE_CLIENT_UI_LEFT,0.8f,0.8f,0.8f);
        return;
    }
    r=ui->content; maage_client_ui_panel(ui,r,0.16f,0.13f,0.10f,1);
    if(f->dirt_uv[2]>f->dirt_uv[0])for(y=0;y<h;y+=32)for(x=0;x<w;x+=32) {
        r=front_rect(f,x,y,32,32);
        maage_client_ui_rectangle(ui,r,f->dirt_uv[0],f->dirt_uv[1],f->dirt_uv[2],f->dirt_uv[3],0.35f,0.35f,0.35f,1);
    }
    if(screen == MAAGE_CLIENT_UI_LOADING) {
        front_center(f,"Loading world...",h/2-12,1,1,1);
        front_center(f,"Preparing terrain and starting simulation",h/2+8,0.7f,0.7f,0.7f);
        return;
    }
    if(screen == MAAGE_CLIENT_UI_WORLDS) {
        front_center(f,"Select World",14,1,1,1);
        r=front_rect(f,0,36,w,h-116);maage_client_ui_panel(ui,r,0.04f,0.04f,0.04f,0.8f);
        end=f->first+front_rows(f);if(end>f->count)end=f->count;
        for(i=f->first;i<end;++i) {
            y=40+(float)(i-f->first)*40;
            r=front_rect(f,8,y,w-16,38);
            if(i==f->selected)maage_client_ui_panel(ui,r,0.42f,0.42f,0.42f,1);
            r.x+=1;r.y+=1;r.width-=2;r.height-=2;maage_client_ui_panel(ui,r,0.09f,0.09f,0.09f,1);
            ui->clip=r;
            maage_client_ui_text(ui,f->entries[i].name,r.x+5,r.y+3,MAAGE_CLIENT_UI_LEFT,1,1,1);
            sprintf(line,"%.20s | %s",f->entries[i].created_utc,
                maage_status_failed(f->entries[i].status)?"Unreadable / unsupported":f->entries[i].game_mode?"Creative":"Survival");
            maage_client_ui_text(ui,line,r.x+5,r.y+15,MAAGE_CLIENT_UI_LEFT,0.65f,0.65f,0.65f);
            ui->clip=ui->content;
        }
        if(!f->count)front_center(f,f->busy?"Reading worlds...":"No worlds yet. Create your first world!",65,0.8f,0.8f,0.8f);
        front_button(f,0,"Play Selected World",!f->busy&&f->selected<f->count&&!maage_status_failed(f->entries[f->selected].status));
        front_button(f,1,"Create New World",!f->busy);
        front_button(f,2,"Rename",!f->busy&&f->selected<f->count&&!maage_status_failed(f->entries[f->selected].status));
        front_button(f,3,"Delete",!f->busy&&f->selected<f->count);
        front_button(f,4,"Refresh",!f->busy);front_button(f,5,"Cancel",1);
    } else if(screen==MAAGE_CLIENT_UI_CREATE_WORLD || screen==MAAGE_CLIENT_UI_RENAME_WORLD) {
        front_center(f,screen==MAAGE_CLIENT_UI_CREATE_WORLD?"Create New World":"Rename World",16,1,1,1);
        front_center(f,"World Name",42,0.7f,0.7f,0.7f);
        front_field(f,f->name,56,3);
        if(screen==MAAGE_CLIENT_UI_CREATE_WORLD) {
            front_center(f,"Seed (blank for random)",80,0.7f,0.7f,0.7f);
            front_field(f,f->seed,94,4);
            front_button(f,2,f->mode?"Game Mode: Creative":"Game Mode: Survival",1);
        }
        front_button(f,0,screen==MAAGE_CLIENT_UI_CREATE_WORLD?"Create New World":"Save Name",!f->busy&&f->name[0]);front_button(f,1,"Cancel",!f->busy);
    } else {
        front_center(f,"Delete this world permanently?",48,1,1,1);
        if(f->selected<f->count)front_center(f,f->entries[f->selected].name,72,1,0.8f,0.3f);
        front_center(f,"This cannot be undone.",94,0.8f,0.8f,0.8f);
        front_button(f,0,"Delete",!f->busy);front_button(f,1,"Cancel",!f->busy);
    }
    if(f->message[0])front_center(f,f->message,29,1,0.65f,0.3f);
}
static void front_edit(char *text, size_t capacity, const maage_client_ui_input *input)
{
    size_t length=strlen(text),i;
    for(i=0;input->text&&input->text[i];++i) {
        if(input->text[i]=='\b') { if(length)text[--length]=0; }
        else if(length+1<capacity) { text[length++]=input->text[i];text[length]=0; }
    }
}
static unsigned int front_input(maage_client_ui *ui,const maage_client_ui_input *input,void *context)
{
    maage_client_frontend *f=(maage_client_frontend *)context;
    unsigned int screen=maage_client_ui_current(ui),n,count=screen==MAAGE_CLIENT_UI_TITLE?4:screen==MAAGE_CLIENT_UI_WORLDS?6:screen==MAAGE_CLIENT_UI_CREATE_WORLD?5:screen==MAAGE_CLIENT_UI_RENAME_WORLD?4:2;
    int hover=-1;
    float w=ui->content.width;
    size_t row;
    if(screen==MAAGE_CLIENT_UI_LOADING)return 0;
    if(input->escape) { if(!f->busy&&screen!=MAAGE_CLIENT_UI_TITLE)ui->route_result=maage_client_ui_back(ui);return 0; }
    if(input->previous)f->focus=(f->focus+count-1)%count;
    if(input->next)f->focus=(f->focus+1)%count;
    if(screen==MAAGE_CLIENT_UI_RENAME_WORLD&&f->focus==2)f->focus=input->previous?1:3;
    for(n=0;n<(screen==MAAGE_CLIENT_UI_CREATE_WORLD?3:screen==MAAGE_CLIENT_UI_RENAME_WORLD?2:count);++n)
        if(front_hit(f,front_button_rect(f,n)))hover=(int)n;
    if(hover>=0 && (input->primary_pressed || ui->mouse_x!=ui->previous_mouse_x || ui->mouse_y!=ui->previous_mouse_y))f->focus=(unsigned int)hover;
    if(screen==MAAGE_CLIENT_UI_CREATE_WORLD||screen==MAAGE_CLIENT_UI_RENAME_WORLD) {
        if(front_hit(f,front_rect(f,w/2-100,56,200,20)))hover=3;
        if(screen==MAAGE_CLIENT_UI_CREATE_WORLD&&front_hit(f,front_rect(f,w/2-100,94,200,20)))hover=4;
        if(input->primary_pressed&&hover>=0)f->focus=(unsigned int)hover;
        if(f->focus==3)front_edit(f->name,sizeof(f->name),input);
        if(f->focus==4)front_edit(f->seed,sizeof(f->seed),input);
    }
    if(screen==MAAGE_CLIENT_UI_WORLDS) {
        if((input->list_up||input->left)&&f->selected) --f->selected;
        if((input->list_down||input->right)&&f->selected+1<f->count) ++f->selected;
        if(input->list_up||input->list_down||input->left||input->right) {
            if(f->selected<f->first)f->first=f->selected;
            if(f->selected>=f->first+front_rows(f))f->first=f->selected-front_rows(f)+1;
        }
        if(input->scroll>0&&f->first) --f->first;
        if(input->scroll<0&&f->first+front_rows(f)<f->count)++f->first;
        if(input->primary_pressed&&front_hit(f,front_rect(f,8,40,w-16,(float)front_rows(f)*40))) {
            row=f->first+(size_t)((ui->mouse_y-ui->content.y-40)/40);
            if(row<f->count) {
                if(row==f->last_row&&ui->notice_now-f->last_click<0.3&&!f->busy&&!maage_status_failed(f->entries[row].status)) { f->selected=row;f->play=1; }
                f->selected=row;f->last_row=row;f->last_click=ui->notice_now;
            }
        }
    }
    if(!(input->accept||(input->primary_pressed&&hover>=0)))return 0;
    if(input->primary_pressed)f->focus=(unsigned int)hover;
    n=f->focus;if(f->busy)return 0;
    if(input->accept&&n>=3&&(screen==MAAGE_CLIENT_UI_CREATE_WORLD||screen==MAAGE_CLIENT_UI_RENAME_WORLD))n=0;
    if(screen==MAAGE_CLIENT_UI_TITLE) {
        if(n==0) { f->action=1;ui->route_result=maage_client_ui_open(ui,MAAGE_CLIENT_UI_WORLDS); }
        else if(n==2)ui->route_result=maage_client_ui_open(ui,MAAGE_CLIENT_UI_OPTIONS);
        else if(n==3)f->quit=1;
    } else if(screen==MAAGE_CLIENT_UI_WORLDS) {
        if(n==0&&f->selected<f->count&&!maage_status_failed(f->entries[f->selected].status))f->play=1;
        if(n==1) { strcpy(f->name,"New World");f->seed[0]=0;f->mode=1;f->focus=3;ui->route_result=maage_client_ui_open(ui,MAAGE_CLIENT_UI_CREATE_WORLD); }
        if(n==2&&f->selected<f->count&&!maage_status_failed(f->entries[f->selected].status)) { strcpy(f->name,f->entries[f->selected].name);f->focus=3;ui->route_result=maage_client_ui_open(ui,MAAGE_CLIENT_UI_RENAME_WORLD); }
        if(n==3&&f->selected<f->count) { f->focus=0;ui->route_result=maage_client_ui_open(ui,MAAGE_CLIENT_UI_DELETE_WORLD); }
        if(n==4)f->action=1;
        if(n==5)ui->route_result=maage_client_ui_back(ui);
    } else if(screen==MAAGE_CLIENT_UI_CREATE_WORLD) {
        if(n==2)f->mode=!f->mode;
        if(n==0&&f->name[0]) {
            maage_world_seed seed;
            if(f->seed[0]&&maage_status_failed(maage_world_seed_parse(f->seed,&seed)))strcpy(f->message,"Enter a signed 64-bit seed.");
            else f->play=2;
        }
        if(n==1)ui->route_result=maage_client_ui_back(ui);
    } else if(screen==MAAGE_CLIENT_UI_RENAME_WORLD) {
        if(n==0&&f->name[0])f->action=2;
        if(n==1)ui->route_result=maage_client_ui_back(ui);
    } else { if(n==0)f->action=3;if(n==1)ui->route_result=maage_client_ui_back(ui); }
    return 0;
}
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
    unsigned int id;
    const maage_block_state_definition *dirt;
    if (output) *output=NULL;
    if (!ui || !instance || !items || !output)
        return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT,MAAGE_CONTEXT_GUI,MAAGE_OPERATION_CREATE,0);
    f=(maage_client_frontend *)calloc(1,sizeof(*f));
    if(!f)return maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED,MAAGE_CONTEXT_GUI,MAAGE_OPERATION_CREATE,0);
    f->ui=ui;f->root=maage_instance_directory(instance);f->instance=instance;f->items=items;f->last_row=(size_t)-1;
    dirt=maage_block_registry_state(ui->blocks,MAAGE_STATE_DIRT);
    if (dirt) {
        f->dirt_uv[0]=dirt->faces[0].uv[0]*ui->resources.block_atlas_width/ui->resources.atlas_edge;
        f->dirt_uv[2]=dirt->faces[0].uv[2]*ui->resources.block_atlas_width/ui->resources.atlas_edge;
        f->dirt_uv[1]=(dirt->faces[0].uv[1]*ui->resources.block_atlas_height+MAAGE_CLIENT_UI_BLOCK_ATLAS_Y)/ui->resources.atlas_edge;
        f->dirt_uv[3]=(dirt->faces[0].uv[3]*ui->resources.block_atlas_height+MAAGE_CLIENT_UI_BLOCK_ATLAS_Y)/ui->resources.atlas_edge;
    }
    result=maage_jobs_create(1,1,&f->jobs,trace);
    if(maage_status_failed(result)){free(f);return result;}
    *output=f;
    for(id=MAAGE_CLIENT_UI_TITLE;id<=MAAGE_CLIENT_UI_LOADING;++id) {
        result=maage_client_ui_register_screen(ui,id,front_draw,front_input,f);
        if(maage_status_failed(result))return result;
    }
    return result;
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
    memset(choice,0,sizeof(*choice));memset(matrix,0,sizeof(matrix));for(i=0;i<4;++i)matrix[i*5]=1;
    ui->has_snapshot=0;ui->command_read=ui->command_write=ui->command_count=0;
    maage_client_ui_get_options(ui,&f->saved_options);
    ui->navigation.depth=0;f->quit=f->play=0;f->focus=0;f->message[0]=0;
    result=maage_client_ui_open(ui,MAAGE_CLIENT_UI_TITLE);if(maage_status_failed(result))return result;
    result=maage_window_capture_mouse(ui->window,0);if(maage_status_failed(result))return result;
    result=maage_window_set_visible(ui->window,1);if(maage_status_failed(result))return result;
    for(;;) {
        start=maage_time_monotonic_seconds();if(start<0)return maage_status_make(MAAGE_STATUS_ERROR,MAAGE_CONTEXT_CORE,MAAGE_OPERATION_QUERY,0);
        result=maage_window_poll_events(system,0);if(maage_status_failed(result))return result;
        result=maage_window_get_state(ui->window,&state);if(maage_status_failed(result))return result;
        if(state.close_requested||f->quit)return maage_status_make(MAAGE_STATUS_NOOP,0,MAAGE_OPERATION_SELECT,0);
        result=maage_window_get_input(ui->window,&input);if(maage_status_failed(result))return result;
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
                maage_log_trace_status(MAAGE_LOG_WARNING,"maage.client.worlds",completion.result,&completion.trace,"World catalog operation failed: %s",f->io.error);
                strcpy(f->message,"World operation failed; see logs/client.log");
            } else {
                free(f->entries);f->entries=f->io.entries;f->count=f->io.count;f->io.entries=NULL;
                if(f->selected>=f->count)f->selected=f->count?f->count-1:0;
                f->first=0;f->last_row=(size_t)-1;f->message[0]=0;
                if(f->io.operation!=1)maage_client_ui_back(ui);
            }
        }
        if(f->action&&!f->busy) {
            memset(&f->io,0,sizeof(f->io));f->io.root=f->root;f->io.items=f->items;f->io.operation=f->action;
            if(f->selected<f->count)strcpy(f->io.id,f->entries[f->selected].id);
            strcpy(f->io.name,f->name);
            result=maage_jobs_submit(f->jobs,0,front_io_run,&f->io,&job,trace);if(maage_status_failed(result))return result;
            f->busy=1;f->action=0;
        }
        if(f->play) {
            choice->create=f->play==2;choice->mode=f->mode;
            if(choice->create){strcpy(choice->name,f->name);strcpy(choice->seed,f->seed);}
            else strcpy(choice->id,f->entries[f->selected].id);
            ui->navigation.depth=0;return maage_status_make(MAAGE_STATUS_SUCCESS,0,MAAGE_OPERATION_SELECT,0);
        }
        result=maage_client_ui_draw(ui,NULL,&draw);if(maage_status_failed(result))return result;
        result=maage_renderer_draw_frame(ui->renderer,NULL,0,matrix,draw,trace);if(maage_status_failed(result))return result;
        if(maage_status_code(result)==MAAGE_STATUS_NOOP) {
            result=maage_window_wait_events(system,state.visible?1:16);
            if(maage_status_failed(result))return result;
        }
        result=maage_client_pacing_wait(&pacing,system,ui->window,
            maage_client_ui_max_fps(ui),start,&resize_pending);
        if(maage_status_failed(result))return result;
    }
}
maage_status maage_client_frontend_destroy(maage_client_frontend *f)
{
    maage_status result;
    if(!f)return maage_status_make(MAAGE_STATUS_NOOP,0,MAAGE_OPERATION_DESTROY,0);
    result=maage_jobs_destroy(f->jobs,NULL);if(maage_status_failed(result))return result;
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
        f->ui->navigation.depth = 0;
        result = maage_client_ui_open(f->ui, MAAGE_CLIENT_UI_LOADING);
        if (maage_status_failed(result)) return result;
    }
    result = maage_window_get_state(f->ui->window, &state);
    if (maage_status_failed(result)) return result;
    memset(&input, 0, sizeof(input));
    result = maage_client_ui_update(f->ui, &input, state.framebuffer_width,
        state.framebuffer_height, state.width, state.height, &actions);
    if (maage_status_failed(result)) return result;
    result = maage_client_ui_draw(f->ui, NULL, &draw);
    if (maage_status_failed(result)) return result;
    memset(matrix, 0, sizeof(matrix));
    for (i = 0; i < 4; ++i) matrix[i*5] = 1;
    return maage_renderer_draw_frame(f->ui->renderer, NULL, 0, matrix, draw, trace);
}
