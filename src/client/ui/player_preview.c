#include "internal.h"
#include "../player_model.h"
#include <math.h>
#include <string.h>

/* Original biped cuboids, independently projected into the shared GUI batch.
 * Back-to-front faces and polygon clipping avoid a second GUI renderer/depth
 * surface. Both native backends consume precisely the same textured vertices. */
typedef struct maage_preview_face { maage_render_ui_vertex vertices[4]; double depth; } maage_preview_face;
static double maage_preview_positive(double value) { return value > 0 ? value : 0; }
static void maage_preview_rotate(double point[3], double yaw, double pitch)
{
    double x = point[0], y = point[1], z = point[2];
    point[0] = cos(yaw)*x + sin(yaw)*z;
    z = -sin(yaw)*x + cos(yaw)*z;
    point[1] = cos(pitch)*y - sin(pitch)*z;
    point[2] = sin(pitch)*y + cos(pitch)*z;
}
static void maage_preview_box(maage_preview_face faces[72], unsigned int *count, const maage_client_ui *ui,
    const maage_player_box *box, double yaw, double body_pitch,
    const maage_player_pose *rest, const float matrix[16],
    float center_x, float feet_y)
{
    double point[4][3], normal[3], a[3], b[3], local[3], transformed[3], light, length;
    float pixels[4][2];
    unsigned int face, corner, axis, k;
    for (face = 0; face < 6; ++face) {
        maage_player_box_face(box,face,point,pixels);
        for (corner = 0; corner < 4; ++corner) {
            for (axis=0;axis<3;++axis) local[axis]=point[corner][axis]*(axis==0 ? -1 : 1)-rest->pivot[box->part][axis];
            for (axis=0;axis<3;++axis) {
                transformed[axis]=matrix[12+axis];
                for (k=0;k<3;++k) transformed[axis]+=matrix[k*4+axis]*local[k];
                point[corner][axis]=transformed[axis]*(axis==0 ? -1 : 1);
            }
            maage_preview_rotate(point[corner],yaw,body_pitch);
        }
        for (axis = 0; axis < 3; ++axis) { a[axis] = point[1][axis]-point[0][axis]; b[axis] = point[2][axis]-point[0][axis]; }
        for (axis = 0; axis < 3; ++axis) normal[axis] = a[(axis+1)%3]*b[(axis+2)%3]-a[(axis+2)%3]*b[(axis+1)%3];
        if (normal[2] >= 0) continue;
        length = sqrt(normal[0]*normal[0]+normal[1]*normal[1]+normal[2]*normal[2]);
        for (axis = 0; axis < 3; ++axis) normal[axis] /= length;
        light = 0.4 + 0.6*maage_preview_positive(normal[0]*0.1616904+normal[1]*0.8084521-normal[2]*0.5659165) +
            0.6*maage_preview_positive(-normal[0]*0.1616904+normal[1]*0.8084521+normal[2]*0.5659165);
        if (light > 1) light = 1;
        faces[*count].depth = 0;
        for (corner = 0; corner < 4; ++corner) {
            maage_render_ui_vertex *vertex = &faces[*count].vertices[corner];
            vertex->position[0] = center_x + (float)(point[corner][0]*30/16);
            vertex->position[1] = feet_y - (float)(point[corner][1]*30/16);
            vertex->uv[0]=ui->resources.skin_uv[0]+pixels[corner][0]/64*(ui->resources.skin_uv[2]-ui->resources.skin_uv[0]);
            vertex->uv[1]=ui->resources.skin_uv[1]+pixels[corner][1]/64*(ui->resources.skin_uv[3]-ui->resources.skin_uv[1]);
            vertex->color[0] = vertex->color[1] = vertex->color[2] = (float)light; vertex->color[3] = 1;
            faces[*count].depth += point[corner][2]*0.25;
        }
        ++*count;
    }
}
static void maage_preview_clip(maage_client_ui *ui, const maage_render_ui_vertex source[4], maage_client_ui_rect clip)
{
    maage_render_ui_vertex a[12], b[12], quad[4];
    unsigned int count = 4, next, edge, i, component;
    int axis, inside, previous_inside;
    float boundary, denominator, t;
    const maage_render_ui_vertex *previous, *current;
    memcpy(a,source,4*sizeof(*a));
    for (edge = 0; edge < 4 && count; ++edge) {
        axis = (int)(edge/2); boundary = axis == 0 ? clip.x : clip.y;
        if (edge & 1) boundary += axis == 0 ? clip.width : clip.height;
        next = 0; previous = &a[count-1];
        previous_inside = edge & 1 ? previous->position[axis] <= boundary : previous->position[axis] >= boundary;
        for (i = 0; i < count; ++i) {
            current = &a[i]; inside = edge & 1 ? current->position[axis] <= boundary : current->position[axis] >= boundary;
            if (inside != previous_inside) {
                denominator = current->position[axis]-previous->position[axis];
                t = denominator ? (boundary-previous->position[axis])/denominator : 0;
                for (component = 0; component < 2; ++component) {
                    b[next].position[component] = previous->position[component]+t*(current->position[component]-previous->position[component]);
                    b[next].uv[component] = previous->uv[component]+t*(current->uv[component]-previous->uv[component]);
                }
                memcpy(b[next].color,current->color,sizeof(b[next].color)); ++next;
            }
            if (inside) b[next++] = *current;
            previous = current; previous_inside = inside;
        }
        memcpy(a,b,next*sizeof(*a)); count = next;
    }
    for (i = 1; i+1 < count; ++i) {
        quad[0] = a[0]; quad[1] = a[i]; quad[2] = a[i+1]; quad[3] = a[0];
        maage_client_ui_append_quads(ui,quad,1);
    }
}
void maage_client_ui_player_preview(maage_client_ui *ui, maage_client_ui_rect frame)
{
    maage_player_box boxes[MAAGE_PLAYER_BOX_COUNT];
    maage_player_pose pose, rest;
    float matrices[MAAGE_PLAYER_PARTS][16];
    maage_preview_face faces[72], temporary;
    maage_client_ui_rect clip;
    unsigned int count = 0, box, i, j;
    double horizontal = atan((frame.x+51-ui->mouse_x)/40.0), vertical = atan((frame.y+25-ui->mouse_y)/40.0);
    double yaw = horizontal*20*3.141592653589793/180, pitch = vertical*20*3.141592653589793/180;
    clip.x = frame.x+26; clip.y = frame.y+8; clip.width = 50; clip.height = 66;
    maage_player_model_boxes(ui->resources.skin_model,boxes);
    maage_player_pose_calculate(ui->resources.skin_model,0,0,0,
        horizontal*40*3.141592653589793/180-yaw,-pitch,0,0,0,&pose);
    maage_player_pose_calculate(ui->resources.skin_model,0,0,0,0,0,0,0,0,&rest);
    for (i=0;i<MAAGE_PLAYER_PARTS;++i) maage_player_part_matrix(matrices[i],&pose,i);
    for (box = 0; box < MAAGE_PLAYER_BOX_COUNT; ++box)
        maage_preview_box(faces,&count,ui,&boxes[box],yaw,pitch,
            &rest,matrices[boxes[box].part],frame.x+51,frame.y+75);
    for (i = 1; i < count; ++i) {
        temporary = faces[i]; j = i;
        while (j && faces[j-1].depth < temporary.depth) { faces[j] = faces[j-1]; --j; }
        faces[j] = temporary;
    }
    for (i = 0; i < count; ++i) maage_preview_clip(ui,faces[i].vertices,clip);
}
