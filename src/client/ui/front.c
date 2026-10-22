#include "front_internal.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
static maage_client_ui_rect front_rect(maage_client_ui_frontend *f, float x, float y, float w, float h)
{
    maage_client_ui_rect r;
    r.x = f->ui->content.x + x; r.y = f->ui->content.y + y;
    r.width = w; r.height = h; return r;
}
static int front_hit(maage_client_ui_frontend *f, maage_client_ui_rect r)
{
    return f->ui->mouse_x >= r.x && f->ui->mouse_x < r.x+r.width &&
        f->ui->mouse_y >= r.y && f->ui->mouse_y < r.y+r.height;
}
static maage_client_ui_rect front_button_rect(maage_client_ui_frontend *f, unsigned int n)
{
    float w = f->ui->content.width, h = f->ui->content.height;
    unsigned int screen = maage_client_ui_current(f->ui);
    if (screen == MAAGE_CLIENT_UI_WORLDS)
        return front_rect(f, w/2-150+(n%2)*152, h-76+(n/2)*24, 148, 20);
    if (screen == MAAGE_CLIENT_UI_CREATE_WORLD && n == 2)
        return front_rect(f, w/2-100, 120, 200, 20);
    return front_rect(f, w/2-150+n*152, h-30, 148, 20);
}
static void front_button(maage_client_ui_frontend *f, unsigned int n, const char *text, int enabled)
{
    maage_client_ui_rect r = front_button_rect(f,n);
    maage_client_ui_button_state(f->ui,r,text,
        !enabled?MAAGE_CLIENT_UI_BUTTON_DISABLED:
        ((front_hit(f,r)||(f->focus_keyboard&&f->focus==n))?
            MAAGE_CLIENT_UI_BUTTON_HOVER:MAAGE_CLIENT_UI_BUTTON_NORMAL));
}
static size_t front_rows(maage_client_ui_frontend *f)
{
    float available = f->ui->content.height-120;
    return available >= 40 ? (size_t)(available/40) : 1;
}
static void front_center(maage_client_ui_frontend *f, const char *text, float y, float r,float g,float b)
{
    maage_client_ui_text(f->ui,text,f->ui->content.x+f->ui->content.width/2,
        f->ui->content.y+y,MAAGE_CLIENT_UI_CENTER,r,g,b);
}
static void front_field(maage_client_ui_frontend *f, const char *text, float y, unsigned int focus)
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
    maage_client_ui_frontend *f = (maage_client_ui_frontend *)context;
    unsigned int screen = maage_client_ui_current(ui), n;
    size_t i, end;
    float w=ui->content.width,h=ui->content.height,y,x;
    maage_client_ui_rect r;
    char line[192];
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
    maage_client_ui_frontend *f=(maage_client_ui_frontend *)context;
    unsigned int screen=maage_client_ui_current(ui),n,count=screen==MAAGE_CLIENT_UI_WORLDS?6:screen==MAAGE_CLIENT_UI_CREATE_WORLD?5:screen==MAAGE_CLIENT_UI_RENAME_WORLD?4:2;
    int hover=-1;
    float w=ui->content.width;
    size_t row;
    if(screen==MAAGE_CLIENT_UI_LOADING)return 0;
    if(input->escape) { if(!f->busy&&screen!=MAAGE_CLIENT_UI_TITLE)ui->route_result=maage_client_ui_back(ui);return 0; }
    if(ui->mouse_x!=ui->previous_mouse_x||ui->mouse_y!=ui->previous_mouse_y||
        input->primary_pressed)f->focus_keyboard=0;
    if(input->previous){f->focus=(f->focus+count-1)%count;f->focus_keyboard=1;}
    if(input->next){f->focus=(f->focus+1)%count;f->focus_keyboard=1;}
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
    if(screen==MAAGE_CLIENT_UI_WORLDS) {
        if(n==0&&f->selected<f->count&&!maage_status_failed(f->entries[f->selected].status))f->play=1;
        if(n==1) { strcpy(f->name,"New World");f->seed[0]=0;f->mode=1;f->focus=3;ui->route_result=maage_client_ui_open(ui,MAAGE_CLIENT_UI_CREATE_WORLD); }
        if(n==2&&f->selected<f->count&&!maage_status_failed(f->entries[f->selected].status)) { strcpy(f->name,f->entries[f->selected].name);f->focus=3;ui->route_result=maage_client_ui_open(ui,MAAGE_CLIENT_UI_RENAME_WORLD); }
        if(n==3&&f->selected<f->count) { f->focus=0;ui->route_result=maage_client_ui_open(ui,MAAGE_CLIENT_UI_DELETE_WORLD); }
        if(n==4)f->action=1;
        if(n==5)ui->route_result=maage_client_ui_back(ui);
    } else if(screen==MAAGE_CLIENT_UI_CREATE_WORLD) {
        if(n==2)f->mode=!f->mode;
        if(n==0&&f->name[0])f->play=2;
        if(n==1)ui->route_result=maage_client_ui_back(ui);
    } else if(screen==MAAGE_CLIENT_UI_RENAME_WORLD) {
        if(n==0&&f->name[0])f->action=2;
        if(n==1)ui->route_result=maage_client_ui_back(ui);
    } else { if(n==0)f->action=3;if(n==1)ui->route_result=maage_client_ui_back(ui); }
    return 0;
}

maage_status maage_client_ui_front_register(maage_client_ui *ui,maage_client_ui_frontend **output)
{
    maage_client_ui_frontend *f;
    const maage_block_state_definition *dirt;
    maage_status result;
    unsigned int id;
    if(output)*output=NULL;
    if(!ui||!output)return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT,MAAGE_CONTEXT_GUI,MAAGE_OPERATION_REGISTER,0);
    f=(maage_client_ui_frontend *)calloc(1,sizeof(*f));
    if(!f)return maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED,MAAGE_CONTEXT_GUI,MAAGE_OPERATION_REGISTER,0);
    f->ui=ui;f->last_row=(size_t)-1;
    dirt=maage_block_registry_state(ui->blocks,MAAGE_STATE_DIRT);
    if(dirt) {
        f->dirt_uv[0]=dirt->faces[0].uv[0]*ui->resources.block_atlas_width/ui->resources.atlas_edge;
        f->dirt_uv[2]=dirt->faces[0].uv[2]*ui->resources.block_atlas_width/ui->resources.atlas_edge;
        f->dirt_uv[1]=(dirt->faces[0].uv[1]*ui->resources.block_atlas_height+MAAGE_CLIENT_UI_BLOCK_ATLAS_Y)/ui->resources.atlas_edge;
        f->dirt_uv[3]=(dirt->faces[0].uv[3]*ui->resources.block_atlas_height+MAAGE_CLIENT_UI_BLOCK_ATLAS_Y)/ui->resources.atlas_edge;
    }
    for(id=MAAGE_CLIENT_UI_TITLE;id<=MAAGE_CLIENT_UI_LOADING;++id) {
        result=maage_client_ui_register_screen(ui,id,
            id==MAAGE_CLIENT_UI_TITLE?maage_client_ui_title_draw:front_draw,
            id==MAAGE_CLIENT_UI_TITLE?maage_client_ui_title_input:front_input,f);
        if(maage_status_failed(result)){free(f);return result;}
    }
    *output=f;return result;
}
void maage_client_ui_front_destroy(maage_client_ui_frontend *f){free(f);}
void maage_client_ui_front_begin(maage_client_ui_frontend *f)
{
    if(!f)return;
    f->quit=f->play=0;f->action=f->focus=0;f->busy=0;
    f->focus_keyboard=0;f->message[0]=0;
}
void maage_client_ui_front_busy(maage_client_ui_frontend *f,int busy){if(f)f->busy=busy;}
void maage_client_ui_front_complete(maage_client_ui_frontend *f,
    const maage_world_summary *entries,size_t count,unsigned int operation)
{
    if(!f)return;
    f->entries=entries;f->count=count;f->busy=0;
    if(f->selected>=f->count)f->selected=f->count?f->count-1:0;
    f->first=0;f->last_row=(size_t)-1;f->message[0]=0;
    if(operation!=1)f->ui->route_result=maage_client_ui_back(f->ui);
}
void maage_client_ui_front_error(maage_client_ui_frontend *f,const char *message)
{
    if(!f)return;
    f->busy=0;if(!message)message="";
    strncpy(f->message,message,sizeof(f->message)-1);
    f->message[sizeof(f->message)-1]=0;
}
int maage_client_ui_front_take(maage_client_ui_frontend *f,maage_client_ui_front_request *r)
{
    if(!f||!r)return 0;
    memset(r,0,sizeof(*r));
    r->selected=f->selected;r->mode=f->mode;
    strcpy(r->name,f->name);strcpy(r->seed,f->seed);
    if(f->quit){r->action=6;f->quit=0;}
    else if(f->play){r->action=f->play==2?5:4;f->play=0;}
    else if(f->action){r->action=f->action;f->action=0;}
    return r->action!=0;
}
