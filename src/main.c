#include <kernel.h>
#include <stdlib.h>
#include <malloc.h>
#include <math.h>
#include <tamtypes.h>
#include <math3d.h>
#include <packet.h>
#include <dma_tags.h>
#include <gif_tags.h>
#include <gs_psm.h>
#include <dma.h>
#include <graph.h>
#include <draw.h>
#include <draw3d.h>
#include <libpad.h>
#include "mesh_data.c"

#define SW 640
#define SH 448
#define OBJECTS 15

static framebuffer_t frame;
static zbuffer_t zbuf;
static unsigned char padBuf[256] __attribute__((aligned(64)));
static struct padButtonStatus pad;
static int padReady=0, mission=0, logoFrames=0;
static float px=0.0f, pz=0.0f, heading=0.0f;
static VECTOR cameraPos={0.0f,28.0f,100.0f,1.0f};
static VECTOR cameraRot={0.0f,0.0f,0.0f,1.0f};
static VECTOR objPos={0.0f,0.0f,0.0f,1.0f};
static VECTOR objRot={0.0f,0.0f,0.0f,1.0f};

static void initGS(void) {
 frame.width=SW; frame.height=SH; frame.mask=0; frame.psm=GS_PSM_32;
 frame.address=graph_vram_allocate(SW,SH,GS_PSM_32,GRAPH_ALIGN_PAGE);
 zbuf.enable=DRAW_ENABLE; zbuf.mask=0; zbuf.method=ZTEST_METHOD_GREATER_EQUAL; zbuf.zsm=GS_ZBUF_32;
 zbuf.address=graph_vram_allocate(SW,SH,GS_ZBUF_32,GRAPH_ALIGN_PAGE);
 graph_initialize(frame.address,SW,SH,GS_PSM_32,0,0);
 packet_t *p=packet_init(32,PACKET_NORMAL); qword_t *q=p->data;
 q=draw_setup_environment(q,0,&frame,&zbuf);
 q=draw_primitive_xyoffset(q,0,2048-SW/2,2048-SH/2);
 q=draw_finish(q);
 dma_channel_send_normal(DMA_CHANNEL_GIF,p->data,q-p->data,0,0);
 dma_wait_fast(); packet_free(p);
}

static qword_t *rect(qword_t *q,int x,int y,int w,int h,int r,int g,int b) {
 rect_t a; a.v0.x=x; a.v0.y=y; a.v0.z=1; a.v1.x=x+w; a.v1.y=y+h; a.v1.z=1;
 a.color.r=r; a.color.g=g; a.color.b=b; a.color.a=128; a.color.q=1.0f;
 return draw_rect_filled(q,0,&a);
}
static void showLogo(void) {
 packet_t *p=packet_init(2048,PACKET_NORMAL); qword_t *q=p->data;
 q=draw_disable_tests(q,0,&zbuf);
 q=draw_clear(q,0,2048-SW/2,2048-SH/2,SW,SH,3,4,9);
 q=rect(q,247,105,18,112,230,24,38); q=rect(q,265,190,108,18,230,24,38);
 q=rect(q,355,105,18,112,230,24,38);
 q=rect(q,265,105,18,86,245,245,245); q=rect(q,283,126,18,22,245,245,245);
 q=rect(q,301,148,18,22,245,245,245); q=rect(q,319,170,18,22,245,245,245);
 q=rect(q,337,192,18,18,245,245,245);
 /* Stylized NEXON / GAMES wordmark blocks */
 q=rect(q,172,260,18,48,245,245,245); q=rect(q,190,290,18,18,245,245,245);
 q=rect(q,208,260,18,48,245,245,245); q=rect(q,244,260,18,48,245,245,245);
 q=rect(q,262,260,18,18,245,245,245); q=rect(q,280,260,18,48,245,245,245);
 q=rect(q,298,260,18,48,245,245,245); q=rect(q,316,260,18,18,245,245,245);
 q=rect(q,334,260,18,48,245,245,245); q=rect(q,352,260,18,48,245,245,245);
 q=rect(q,150,370,340,4,230,24,38);
 q=draw_finish(q); dma_wait_fast();
 dma_channel_send_normal(DMA_CHANNEL_GIF,p->data,q-p->data,0,0);
 draw_wait_finish(); graph_wait_vsync(); packet_free(p);
}
static void initPad(void) {
 int i;
 if(padInit(0)!=1) return;
 if(!padPortOpen(0,0,padBuf)) return;
 for(i=0;i<120;i++) {
  int s=padGetState(0,0);
  if(s==PAD_STATE_STABLE) { padReady=1; break; }
  if(s==PAD_STATE_DISCONN) break;
  graph_wait_vsync();
 }
}
static void updatePad(void) {
 int s;
 if(!padReady) {
  s=padGetState(0,0);
  if(s==PAD_STATE_STABLE) padReady=1;
  return;
 }
 s=padGetState(0,0);
 if(s!=PAD_STATE_STABLE) { padReady=0; return; }
 if(!padRead(0,0,&pad)) return;
 /* libpad button bits are active-low: zero means pressed */
 if(!(pad.btns & PAD_LEFT))  px-=1.8f;
 if(!(pad.btns & PAD_RIGHT)) px+=1.8f;
 if(!(pad.btns & PAD_UP))    pz-=2.2f;
 if(!(pad.btns & PAD_DOWN))  pz+=2.2f;
 if(!(pad.btns & PAD_L1)) heading-=0.035f;
 if(!(pad.btns & PAD_R1)) heading+=0.035f;
 if(px < -28) px=-28;
 if(px > 28) px=28;
 if(pz < -180) pz=-180;
 if(pz > 20) pz=20;
 if(!(pad.btns & PAD_CIRCLE)) { px=0.0f; pz=12.0f; heading=0.0f; }
}

static qword_t *drawCube(qword_t *q, MATRIX viewScreen, VECTOR pos, VECTOR rot,
                     float sx,float sy,float sz,int cr,int cg,int cb,
                     VECTOR *tmp, xyz_t *xyz, color_t *rgbaq,
                     prim_t *prim,color_t *base) {
 MATRIX lw,wv,ls; int i;
 VECTOR scaled[24]; int k;
 for(k=0;k<vertex_count;k++) {
  scaled[k][0]=vertices[k][0]*sx;
  scaled[k][1]=vertices[k][1]*sy;
  scaled[k][2]=vertices[k][2]*sz;
  scaled[k][3]=vertices[k][3];
 }
 create_local_world(lw,pos,rot);
 create_world_view(wv,cameraPos,cameraRot);
 create_local_screen(ls,lw,wv,viewScreen);
 calculate_vertices(tmp,vertex_count,scaled,ls);
 draw_convert_xyz(xyz,2048,2048,32,vertex_count,(vertex_f_t*)tmp);
 { color_f_t c[24]; for(i=0;i<24;i++){ c[i].r=cr/255.0f; c[i].g=cg/255.0f; c[i].b=cb/255.0f; c[i].a=1.0f; } 
   draw_convert_rgbq(rgbaq,vertex_count,(vertex_f_t*)tmp,c,0x80); }
 q=draw_prim_start(q,0,prim,base);
 for(i=0;i<points_count;i++){ q->dw[0]=rgbaq[points[i]].rgbaq; q->dw[1]=xyz[points[i]].xyz; q++; }
 /* qword alignment handled by primitive end */
 q=draw_prim_end(q,2,DRAW_RGBAQ_REGLIST);
 return q;
}
static void render(void) {
 static packet_t *packets[2]={0,0}; static int ctx=0;
 static VECTOR *tmp=0; static xyz_t *xyz=0; static color_t *rgbaq=0;
 static prim_t prim; static color_t base; static MATRIX viewScreen;
 qword_t *q,*tag; int i;
 if(!packets[0]) {
  packets[0]=packet_init(4096,PACKET_NORMAL); packets[1]=packet_init(4096,PACKET_NORMAL);
  tmp=memalign(128,sizeof(VECTOR)*vertex_count); xyz=memalign(128,sizeof(xyz_t)*vertex_count);
  rgbaq=memalign(128,sizeof(color_t)*vertex_count);
  prim.type=PRIM_TRIANGLE; prim.shading=PRIM_SHADE_GOURAUD; prim.mapping=DRAW_DISABLE;
  prim.fogging=DRAW_DISABLE; prim.blending=DRAW_DISABLE; prim.antialiasing=DRAW_ENABLE;
  prim.mapping_type=PRIM_MAP_ST; prim.colorfix=PRIM_UNFIXED;
  base.r=128;base.g=128;base.b=128;base.a=128;base.q=1.0f;
  create_view_screen(viewScreen,graph_aspect_ratio(),-3.0f,3.0f,-2.2f,2.2f,1.0f,2000.0f);
 }
 cameraPos[0]=px; cameraPos[1]=27.0f; cameraPos[2]=pz+92.0f;
 cameraRot[0]=0.12f; cameraRot[1]=0.0f; cameraRot[2]=0.0f;
 tag=packets[ctx]->data; q=tag+1;
 q=draw_disable_tests(q,0,&zbuf);
 q=draw_clear(q,0,2048-SW/2,2048-SH/2,SW,SH,90,135,165);
 q=draw_enable_tests(q,0,&zbuf);
 /* road slab and city blocks form a small 3D street */
 { VECTOR p={0,-13,-75,1},r={0,0,0,1};
   q=drawCube(q,viewScreen,p,r,35,1,125,42,47,55,tmp,xyz,rgbaq,&prim,&base);
   p[0]=-40; p[1]=-11; q=drawCube(q,viewScreen,p,r,5,1,130,105,105,110,tmp,xyz,rgbaq,&prim,&base);
   p[0]=40; q=drawCube(q,viewScreen,p,r,5,1,130,105,105,110,tmp,xyz,rgbaq,&prim,&base);
 }
 /* broken white centre line and road-edge paint */
 for(i=0;i<12;i++) {
   VECTOR p={0,-11.5f,-8.0f-i*19.0f,1},r={0,0,0,1};
   q=drawCube(q,viewScreen,p,r,0.45f,0.18f,6.0f,235,225,190,tmp,xyz,rgbaq,&prim,&base);
   p[0]=-33.0f; q=drawCube(q,viewScreen,p,r,0.35f,0.18f,7.0f,230,190,45,tmp,xyz,rgbaq,&prim,&base);
   p[0]=33.0f; q=drawCube(q,viewScreen,p,r,0.35f,0.18f,7.0f,230,190,45,tmp,xyz,rgbaq,&prim,&base);
 }
 for(i=0;i<6;i++) {
  VECTOR p1={-48,0,-25.0f-i*42.0f,1},p2={48,0,-25.0f-i*42.0f,1},r={0,0,0,1};
  q=drawCube(q,viewScreen,p1,r,12,18+(i%3)*4,12,120,78,62,tmp,xyz,rgbaq,&prim,&base);
  q=drawCube(q,viewScreen,p2,r,12,22+(i%2)*5,12,75,91,112,tmp,xyz,rgbaq,&prim,&base);
  /* bright window panels on the street-facing walls */
  p1[0]=-35.2f; p1[1]=4.0f; p1[2]=-25.0f-i*42.0f;
  q=drawCube(q,viewScreen,p1,r,0.35f,3.0f,3.5f,245,185,95,tmp,xyz,rgbaq,&prim,&base);
  p1[2]-=11.0f; q=drawCube(q,viewScreen,p1,r,0.35f,3.0f,3.5f,245,185,95,tmp,xyz,rgbaq,&prim,&base);
  p2[0]=35.2f; p2[1]=5.0f; p2[2]=-25.0f-i*42.0f;
  q=drawCube(q,viewScreen,p2,r,0.35f,3.5f,3.5f,120,190,225,tmp,xyz,rgbaq,&prim,&base);
  p2[2]-=11.0f; q=drawCube(q,viewScreen,p2,r,0.35f,3.5f,3.5f,120,190,225,tmp,xyz,rgbaq,&prim,&base);
 }
 /* street lamps: poles and warm lamp heads */
 for(i=0;i<5;i++) {
  VECTOR lp={-31.0f,-1.0f,-18.0f-i*48.0f,1},lr={0,0,0,1};
  q=drawCube(q,viewScreen,lp,lr,0.65f,10.0f,0.65f,35,38,43,tmp,xyz,rgbaq,&prim,&base);
  lp[0]=31.0f; q=drawCube(q,viewScreen,lp,lr,0.65f,10.0f,0.65f,35,38,43,tmp,xyz,rgbaq,&prim,&base);
  lp[1]=10.0f; q=drawCube(q,viewScreen,lp,lr,2.0f,0.8f,1.0f,255,205,115,tmp,xyz,rgbaq,&prim,&base);
  lp[0]=-31.0f; q=drawCube(q,viewScreen,lp,lr,2.0f,0.8f,1.0f,255,205,115,tmp,xyz,rgbaq,&prim,&base);
 }
 /* Yellow destination beacon: follow the road ahead */
 { VECTOR wp={0,8,-145,1},wr={0,0,0,1};
   q=drawCube(q,viewScreen,wp,wr,1.2f,15.0f,1.2f,245,185,35,tmp,xyz,rgbaq,&prim,&base);
   wp[1]=24.0f; q=drawCube(q,viewScreen,wp,wr,4.0f,1.0f,4.0f,255,220,75,tmp,xyz,rgbaq,&prim,&base);
 }
 /* Player avatar is a 3D block character; camera follows from behind */
 { VECTOR p={px,-2,pz,1},r={0,heading,0,1};
  q=drawCube(q,viewScreen,p,r,3.2f,7.0f,2.4f,190,35,42,tmp,xyz,rgbaq,&prim,&base);
  p[1]=6.0f; q=drawCube(q,viewScreen,p,r,2.6f,2.6f,2.6f,220,175,140,tmp,xyz,rgbaq,&prim,&base);
  p[0]=px-4.0f; p[1]=-1.5f; p[2]=pz; q=drawCube(q,viewScreen,p,r,1.0f,4.5f,1.0f,190,35,42,tmp,xyz,rgbaq,&prim,&base);
  p[0]=px+4.0f; q=drawCube(q,viewScreen,p,r,1.0f,4.5f,1.0f,190,35,42,tmp,xyz,rgbaq,&prim,&base);
  p[0]=px-1.5f; p[1]=-8.0f; q=drawCube(q,viewScreen,p,r,1.1f,3.0f,1.2f,28,32,42,tmp,xyz,rgbaq,&prim,&base);
  p[0]=px+1.5f; q=drawCube(q,viewScreen,p,r,1.1f,3.0f,1.2f,28,32,42,tmp,xyz,rgbaq,&prim,&base);
 }
 /* clear navigation HUD: mission target, mini-map and controller guide */
 q=rect(q,12,12,250,34,10,15,24);
 q=rect(q,20,20,12,18,245,185,35);
 q=rect(q,40,18,150,5,235,235,235);
 q=rect(q,40,29,205,5,90,190,220);
 q=rect(q,SW-116,12,104,104,10,15,24);
 q=rect(q,SW-108,20,88,88,55,63,70);
 q=rect(q,SW-106,59,84,3,225,215,185);
 q=rect(q,SW-66,24,3,80,225,215,185);
 q=rect(q,SW-66+(int)(px*0.7f),64+(int)(pz*0.18f),9,9,245,45,45);
 q=rect(q,SW-70,27,11,11,255,205,55);
 q=rect(q,12,SH-46,275,34,10,15,24);
 q=rect(q,22,SH-36,20,4,245,245,245);
 q=rect(q,30,SH-41,4,14,245,245,245);
 q=rect(q,62,SH-36,20,4,245,245,245);
 q=rect(q,70,SH-41,4,14,245,245,245);
 q=rect(q,105,SH-36,14,4,80,210,120);
 q=rect(q,110,SH-41,4,14,80,210,120);
 q=rect(q,145,SH-36,14,4,230,75,75);
 q=rect(q,150,SH-41,4,14,230,75,75);
 q=draw_finish(q);
 DMATAG_END(tag,(q-tag)-1,0,0,0);
 dma_wait_fast(); dma_channel_send_chain(DMA_CHANNEL_GIF,tag,q-tag,0,0);
 ctx^=1; draw_wait_finish(); graph_wait_vsync();
}
int main(void) {
 dma_channel_initialize(DMA_CHANNEL_GIF,NULL,0);
 dma_channel_fast_waits(DMA_CHANNEL_GIF);
 initGS();
 /* Logo intro is intentionally short while graphics initialize. */
 for(logoFrames=0;logoFrames<180;logoFrames++) showLogo();
 initPad();
 for(;;) { updatePad(); render(); }
 return 0;
}
