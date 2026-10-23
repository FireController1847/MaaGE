#include "front_internal.h"
#include <math.h>
#include <stddef.h>
/* Six verified cubemap faces packed into the existing atlas. View-space
 * tessellation provides panorama rotation without a second renderer. */
/* The panorama must project each quad into ONE cubemap face. The old
 * per-corner face choice interpolated across unrelated atlas tiles whenever
 * a cube boundary passed beneath the camera, creating rotating glitches. */
typedef struct title_camera {
    double cy,sy,cp,sp;
    float width,height;
} title_camera;
static void title_ray(const title_camera *camera,float x,float y,double ray[3])
{
    double dx=(x-camera->width*0.5f)*2.0/camera->height;
    double dy=(y-camera->height*0.5f)*2.0/camera->height;
    double z=camera->cy-dx*camera->sy;
    ray[0]=dx*camera->cy+camera->sy;
    ray[1]=dy*camera->cp-z*camera->sp;
    ray[2]=dy*camera->sp+z*camera->cp;
}
static int title_face(const double ray[3])
{
    double ax=fabs(ray[0]),ay=fabs(ray[1]),az=fabs(ray[2]);
    if(az>=ax&&az>=ay)return ray[2]>=0.0?0:2;
    if(ax>=ay)return ray[0]>=0.0?1:3;
    return ray[1]>=0.0?5:4;
}
static void title_uv(const maage_client_ui *ui,const double ray[3],int face,
    float *u,float *v)
{
    double a,uu,vv;
    if(face==0||face==2) {
        a=fabs(ray[2]);uu=(face==0?ray[0]:-ray[0])/a;vv=ray[1]/a;
    } else if(face==1||face==3) {
        a=fabs(ray[0]);uu=(face==1?-ray[2]:ray[2])/a;vv=ray[1]/a;
    } else {
        a=fabs(ray[1]);uu=ray[0]/a;vv=(face==5?-ray[2]:ray[2])/a;
    }
    /* Clamp cube-edge straddling cells to their center-selected face. */
    if(uu < -1.0)uu=-1.0;
    if(uu > 1.0)uu=1.0;
    if(vv < -1.0)vv=-1.0;
    if(vv > 1.0)vv=1.0;
    /* Use full 256x256 face bounds at texel centers. The previous two-pixel
     * inset cut away scenery at every face boundary and made seams visible. */
    *u=(float)(512+((face+1)%4)*256+0.5+(uu+1.0)*127.5)/ui->resources.atlas_edge;
    *v=(float)(((face+1)/4)*256+0.5+(vv+1.0)*127.5)/ui->resources.atlas_edge;
}
/* The original panorama remains animated, but each face is Gaussian filtered
 * ONCE during asset loading. Render one continuous projection per frame:
 * an 8-pixel mosaic with five offset passes is not a Gaussian blur. */
static void title_panorama(maage_client_ui *ui)
{
    title_camera camera;
    maage_render_ui_vertex vertices[4];
    maage_client_ui_rect full;
    double ray[3], yaw, pitch;
    float left, top, right, bottom, sx, sy;
    unsigned int j;
    int face, i, x, y;
    const int step = 3;
    for (i = 1; i <= 6; ++i)
        if (!ui->resources.title_available[i]) return;
    yaw = ui->notice_now * 0.004;
    pitch = 0.16 + sin(ui->notice_now * 0.012) * 0.06;
    camera.cy = cos(yaw); camera.sy = sin(yaw);
    camera.cp = cos(pitch); camera.sp = sin(pitch);
    camera.width = ui->width; camera.height = ui->height;
    full.x = full.y = 0; full.width = ui->width; full.height = ui->height;
    maage_client_ui_panel(ui, full, 0.48f, 0.55f, 0.61f, 1.0f);
    for (y = 0; y < (int)ui->height; y += step) {
        for (x = 0; x < (int)ui->width; x += step) {
            left = (float)x; top = (float)y;
            right = left + step; bottom = top + step;
            if (right > ui->width) right = ui->width;
            if (bottom > ui->height) bottom = ui->height;
            title_ray(&camera, (left + right) * 0.5f,
                (top + bottom) * 0.5f, ray);
            face = title_face(ray);
            for (j = 0; j < 4U; ++j) {
                sx = (j == 0U || j == 3U) ? left : right;
                sy = j < 2U ? top : bottom;
                vertices[j].position[0] = sx;
                vertices[j].position[1] = sy;
                title_ray(&camera, sx, sy, ray);
                title_uv(ui, ray, face, &vertices[j].uv[0], &vertices[j].uv[1]);
                vertices[j].color[0] = vertices[j].color[1] =
                    vertices[j].color[2] = vertices[j].color[3] = 1.0f;
            }
            if (!maage_client_ui_append_quads(ui, vertices, 1)) return;
        }
    }
    maage_client_ui_panel(ui, full, 0.38f, 0.36f, 0.43f, 0.28f);
    /* Finished panorama. The renderer filters this composed segment before
     * drawing the crisp logo, splash text, widgets, and footer. */
    ui->batch.gaussian_background_indices = ui->batch.index_count;
}
static maage_client_ui_rect title_button(const maage_client_ui *ui,unsigned int id)
{
    maage_client_ui_rect r;
    float cx=ui->content.x+ui->content.width*0.5f;
    float y=ui->content.y+(float)(int)(ui->content.height/4)+48;
    r.y=y+(id<3?24*(float)id:96);r.height=20;
    r.width=id==3?20:(id<3?200:98);
    r.x=id==3?cx-124:(id==4?cx-100:(id==5?cx+2:cx-100));
    return r;
}
static int title_hit(const maage_client_ui *ui,maage_client_ui_rect r)
{
    return ui->mouse_x>=r.x&&ui->mouse_x<r.x+r.width&&
        ui->mouse_y>=r.y&&ui->mouse_y<r.y+r.height;
}
static void title_logo(maage_client_ui *ui)
{
    maage_client_ui_rect r;
    float u=512.0f/ui->resources.atlas_edge;
    float cx=ui->content.x+(float)(int)(ui->content.width*0.5f);
    if(!ui->resources.title_available[0])return;
    r.x=cx-137;r.y=ui->content.y+30;r.width=155;r.height=44;
    maage_client_ui_rectangle(ui,r,u,0,u+155.0f/ui->resources.atlas_edge,
        44.0f/ui->resources.atlas_edge,1,1,1,1);
    r.x+=155;r.width=119;
    maage_client_ui_rectangle(ui,r,u,45.0f/ui->resources.atlas_edge,
        u+119.0f/ui->resources.atlas_edge,89.0f/ui->resources.atlas_edge,1,1,1,1);
}
static void title_splash(maage_client_ui *ui)
{
    size_t first=ui->batch.vertex_count,i;
    double radians=-0.3490658504,c=cos(radians),s=sin(radians);
    double scale=1.8-fabs(sin(ui->notice_now*5.0)*0.1);
    float x=ui->content.x+ui->content.width*0.5f+90,y=ui->content.y+70;
    maage_client_ui_text(ui,"Engage!",x+1,y+1,MAAGE_CLIENT_UI_CENTER,0.18f,0.18f,0);
    maage_client_ui_text(ui,"Engage!",x,y,MAAGE_CLIENT_UI_CENTER,1,1,0);
    for(i=first;i<ui->batch.vertex_count;++i) {
        double dx=(ui->vertices[i].position[0]-x)*scale;
        double dy=(ui->vertices[i].position[1]-y-4)*scale;
        ui->vertices[i].position[0]=(float)(x+dx*c-dy*s);
        ui->vertices[i].position[1]=(float)(y+dx*s+dy*c);
    }
}
void maage_client_ui_title_draw(maage_client_ui *ui,void *context)
{
    maage_client_ui_frontend *f=(maage_client_ui_frontend *)context;
    maage_client_ui_rect r;
    title_panorama(ui);title_logo(ui);title_splash(ui);
    r=title_button(ui,0);maage_client_ui_button(ui,r,"Singleplayer",
        (f->focus_keyboard?f->focus==0:title_hit(ui,r)));
    r=title_button(ui,1);
    maage_client_ui_button_state(ui,r,"Multiplayer",MAAGE_CLIENT_UI_BUTTON_DISABLED);
    r=title_button(ui,2);
    maage_client_ui_button_state(ui,r,"Minecraft Realms",MAAGE_CLIENT_UI_BUTTON_DISABLED);
    /* The 1.7.10 language globe is in widgets.png at (0,106/126),
     * not a separate language.png resource. */
    r=title_button(ui,3);
    maage_client_ui_pointer(ui,r);
    maage_client_ui_rectangle(ui,r,256.0f/ui->resources.atlas_edge,
        (title_hit(ui,r)?126.0f:106.0f)/ui->resources.atlas_edge,
        276.0f/ui->resources.atlas_edge,
        (title_hit(ui,r)?146.0f:126.0f)/ui->resources.atlas_edge,1,1,1,1);
    r=title_button(ui,4);maage_client_ui_button(ui,r,"Options...",
        (f->focus_keyboard?f->focus==4:title_hit(ui,r)));
    r=title_button(ui,5);maage_client_ui_button(ui,r,"Quit Game",
        (f->focus_keyboard?f->focus==5:title_hit(ui,r)));
    maage_client_ui_text(ui,"Minecraft 1.7.10",3,ui->height-9,MAAGE_CLIENT_UI_LEFT,0.14f,0.14f,0.14f);
    maage_client_ui_text(ui,"Minecraft 1.7.10",2,ui->height-10,MAAGE_CLIENT_UI_LEFT,1,1,1);
    maage_client_ui_text(ui,"Copyright Mojang AB. Do not distribute!",
        ui->width-1,ui->height-9,MAAGE_CLIENT_UI_RIGHT,0.14f,0.14f,0.14f);
    maage_client_ui_text(ui,"Copyright Mojang AB. Do not distribute!",
        ui->width-2,ui->height-10,MAAGE_CLIENT_UI_RIGHT,1,1,1);
}
unsigned int maage_client_ui_title_input(maage_client_ui *ui,
    const maage_client_ui_input *input,void *context)
{
    maage_client_ui_frontend *f=(maage_client_ui_frontend *)context;
    int hover=-1;
    unsigned int ids[3]={0,4,5},k;
    /* Keyboard focus is retained only after keyboard navigation. Pointer
     * movement clears it even when the pointer leaves every button. */
    if (ui->mouse_x != ui->previous_mouse_x ||
        ui->mouse_y != ui->previous_mouse_y || input->primary_pressed)
        f->focus_keyboard = 0;
    if (input->next || input->previous) {
        for (k = 0; k < 3; ++k) if (f->focus == ids[k]) break;
        if (k == 3) k = 0;
        if (input->next) k = (k + 1) % 3;
        else k = (k + 2) % 3;
        f->focus = ids[k];
        f->focus_keyboard = 1;
    }
    for (k = 0; k < 3; ++k)
        if (title_hit(ui, title_button(ui, ids[k]))) hover = (int)ids[k];
    if (!f->focus_keyboard && hover >= 0) f->focus = (unsigned int)hover;
    if (!(input->accept || (input->primary_pressed && hover >= 0))) return 0;
    if (input->primary_pressed && hover >= 0) f->focus = (unsigned int)hover;
    /* Inactive buttons are neither selectable nor audible. The UI callback
     * is invoked only when a real title action is accepted. */
    if(f->focus==0) {
        if(ui->click)ui->click(ui->click_context);
        f->action=1;ui->route_result=maage_client_ui_open(ui,MAAGE_CLIENT_UI_WORLDS);
    } else if(f->focus==4) {
        if(ui->click)ui->click(ui->click_context);
        ui->route_result=maage_client_ui_open(ui,MAAGE_CLIENT_UI_OPTIONS);
    } else if(f->focus==5) {
        if(ui->click)ui->click(ui->click_context);
        f->quit=1;
    }
    return 0;
}
