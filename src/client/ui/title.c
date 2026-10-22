#include "front_internal.h"
#include <math.h>
#include <stddef.h>
/* Six verified cubemap faces packed into the existing atlas. View-space
 * tessellation provides panorama rotation without a second renderer. */
static void title_sample(const maage_client_ui *ui,float x,float y,double now,
    float *u,float *v,int *face)
{
    double yaw=now*0.004,pitch=0.16+sin(now*0.012)*0.06;
    double dx=(x-ui->width*0.5)*2.0/ui->height,dy=(y-ui->height*0.5)*2.0/ui->height;
    double xx=dx*cos(yaw)+sin(yaw),zz=cos(yaw)-dx*sin(yaw),yy,t,ax,ay,az,uu,vv;
    yy=dy*cos(pitch)-zz*sin(pitch);t=dy*sin(pitch)+zz*cos(pitch);zz=t;
    ax=fabs(xx);ay=fabs(yy);az=fabs(zz);
    if(az>=ax&&az>=ay) {
        if(zz>=0){*face=0;uu=xx/az;vv=yy/az;}
        else {*face=2;uu=-xx/az;vv=yy/az;}
    } else if(ax>=ay) {
        if(xx>=0){*face=1;uu=-zz/ax;vv=yy/ax;}
        else {*face=3;uu=zz/ax;vv=yy/ax;}
    } else {
        if(yy>=0){*face=5;uu=xx/ay;vv=-zz/ay;}
        else {*face=4;uu=xx/ay;vv=zz/ay;}
    }
    *u=(float)(512+((*face+1)%4)*256+2+(uu+1)*126)/ui->resources.atlas_edge;
    *v=(float)(((*face+1)/4)*256+2+(vv+1)*126)/ui->resources.atlas_edge;
}
static void title_panorama(maage_client_ui *ui)
{
    maage_render_ui_vertex vertices[4];
    float u[4],v[4],left,top,right,bottom;
    int face[4],pass,i,x,y;
    size_t j;
    const int step=12;
    double now=ui->notice_now;
    maage_client_ui_rect full;
    for(i=1;i<=6;++i)if(!ui->resources.title_available[i])return;
    full.x=full.y=0;full.width=ui->width;full.height=ui->height;
    maage_client_ui_panel(ui,full,0.48f,0.55f,0.61f,1);
    for(pass=0;pass<3;++pass) {
        float offset=(float)(pass-1)*1.3f,alpha=pass==0?1.0f:0.38f;
        for(y=0;y<(int)ui->height;y+=step)for(x=0;x<(int)ui->width;x+=step) {
            left=(float)x;top=(float)y;right=left+step;bottom=top+step;
            if(right>ui->width)right=ui->width;
            if(bottom>ui->height)bottom=ui->height;
            title_sample(ui,left+offset,top,now,&u[0],&v[0],&face[0]);
            title_sample(ui,right+offset,top,now,&u[1],&v[1],&face[1]);
            title_sample(ui,right+offset,bottom,now,&u[2],&v[2],&face[2]);
            title_sample(ui,left+offset,bottom,now,&u[3],&v[3],&face[3]);
            if(face[0]!=face[1]||face[1]!=face[2]||face[2]!=face[3]) {
                float cu,cv;int cf;
                title_sample(ui,(left+right)*0.5f+offset,(top+bottom)*0.5f,now,&cu,&cv,&cf);
                for(i=0;i<4;++i)if(face[i]!=cf){u[i]=cu;v[i]=cv;}
            }
            for(j=0;j<4;++j) {
                vertices[j].position[0]=j==0||j==3?left:right;
                vertices[j].position[1]=j<2?top:bottom;
                vertices[j].uv[0]=u[j];vertices[j].uv[1]=v[j];
                vertices[j].color[0]=vertices[j].color[1]=vertices[j].color[2]=1;
                vertices[j].color[3]=alpha;
            }
            if(!maage_client_ui_append_quads(ui,vertices,1))return;
        }
    }
    maage_client_ui_panel(ui,full,0.38f,0.36f,0.43f,0.28f);
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
    float x=ui->content.x+ui->content.width*0.5f+90,y=ui->content.y+70;
    maage_client_ui_text(ui,"Engage!",x,y,MAAGE_CLIENT_UI_CENTER,1,1,0);
    for(i=first;i<ui->batch.vertex_count;++i) {
        double dx=(ui->vertices[i].position[0]-x)*1.8;
        double dy=(ui->vertices[i].position[1]-y-4)*1.8;
        ui->vertices[i].position[0]=(float)(x+dx*c-dy*s);
        ui->vertices[i].position[1]=(float)(y+dx*s+dy*c);
    }
}
void maage_client_ui_title_draw(maage_client_ui *ui,void *context)
{
    maage_client_ui_frontend *f=(maage_client_ui_frontend *)context;
    maage_client_ui_rect r;
    title_panorama(ui);title_logo(ui);title_splash(ui);
    r=title_button(ui,0);maage_client_ui_button(ui,r,"Singleplayer",f->focus==0);
    r=title_button(ui,1);maage_client_ui_button(ui,r,"Multiplayer",0);
    r=title_button(ui,2);maage_client_ui_button(ui,r,"Minecraft Realms",0);
    r=title_button(ui,3);maage_client_ui_button(ui,r,"",0);
    if(ui->resources.title_available[7]) {
        maage_client_ui_rect icon=r;icon.x+=2;icon.y+=2;icon.width=icon.height=16;
        maage_client_ui_rectangle(ui,icon,1280.0f/ui->resources.atlas_edge,
            256.0f/ui->resources.atlas_edge,1296.0f/ui->resources.atlas_edge,
            272.0f/ui->resources.atlas_edge,1,1,1,1);
    }
    r=title_button(ui,4);maage_client_ui_button(ui,r,"Options...",f->focus==4);
    r=title_button(ui,5);maage_client_ui_button(ui,r,"Quit Game",f->focus==5);
    maage_client_ui_text(ui,"Minecraft 1.7.10",2,ui->height-10,MAAGE_CLIENT_UI_LEFT,1,1,1);
    maage_client_ui_text(ui,"Copyright Mojang AB. Do not distribute!",
        ui->width-2,ui->height-10,MAAGE_CLIENT_UI_RIGHT,1,1,1);
}
unsigned int maage_client_ui_title_input(maage_client_ui *ui,
    const maage_client_ui_input *input,void *context)
{
    maage_client_ui_frontend *f=(maage_client_ui_frontend *)context;
    int hover=-1;
    unsigned int ids[3]={0,4,5},k;
    if(input->next||input->previous) {
        for(k=0;k<3;++k)if(f->focus==ids[k])break;
        if(k==3)k=0;
        if(input->next)k=(k+1)%3;else k=(k+2)%3;
        f->focus=ids[k];
    }
    for(k=0;k<3;++k)if(title_hit(ui,title_button(ui,ids[k])))hover=(int)ids[k];
    if(hover>=0&&(input->primary_pressed||ui->mouse_x!=ui->previous_mouse_x||
        ui->mouse_y!=ui->previous_mouse_y))f->focus=(unsigned int)hover;
    if(!(input->accept||(input->primary_pressed&&hover>=0)))return 0;
    if(input->primary_pressed)f->focus=(unsigned int)hover;
    if(f->focus==0){f->action=1;ui->route_result=maage_client_ui_open(ui,MAAGE_CLIENT_UI_WORLDS);}
    else if(f->focus==4)ui->route_result=maage_client_ui_open(ui,MAAGE_CLIENT_UI_OPTIONS);
    else if(f->focus==5)f->quit=1;
    return 0;
}
