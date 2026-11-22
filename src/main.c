#include <kernel.h>
#include <stdlib.h>
#include <malloc.h>
#include <math.h>
#include <string.h>
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
#include "nexon_engine.h"

#define SW 640
#define SH 448
#define OBJECTS 15

static framebuffer_t frame;
static zbuffer_t zbuf;
static unsigned char padBuf[256] __attribute__((aligned(64)));
static struct padButtonStatus pad;
static int padReady=0, mission=0, logoFrames=0;
static int padConnected=0, padInitialized=0, padOpened=0;
static NX_Scene nxScene;
static NX_Camera nxCamera;
static NX_Time nxClock;
static int nxPlayer=-1;
static int nxTraffic[4];
static int nxPedestrians[4];
static const float missionTargets[10]={-18.0f,-33.0f,-48.0f,-63.0f,-78.0f,-93.0f,-108.0f,-123.0f,-138.0f,-153.0f};
static float px=0.0f, pz=0.0f, heading=0.0f;
static float cameraYaw=0.0f, cameraPitch=0.34f, cameraDistance=42.0f;
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

/* Compact 5x7 bitmap font: digits, A-Z, dash, slash and space. */
static const unsigned char font5x7[39][7]={
 {14,17,19,21,25,17,14},{4,12,4,4,4,4,14},{14,17,1,2,4,8,31},
 {30,1,1,14,1,1,30},{2,6,10,18,31,2,2},{31,16,16,30,1,1,30},
 {6,8,16,30,17,17,14},{31,1,2,4,8,8,8},{14,17,17,14,17,17,14},
 {14,17,17,15,1,2,12},
 {14,17,17,31,17,17,17},{30,17,17,30,17,17,30},{14,17,16,16,16,17,14},
 {30,17,17,17,17,17,30},{31,16,16,30,16,16,31},{31,16,16,30,16,16,16},
 {14,17,16,23,17,17,15},{17,17,17,31,17,17,17},{14,4,4,4,4,4,14},
 {7,2,2,2,18,18,12},{17,18,20,24,20,18,17},{16,16,16,16,16,16,31},
 {17,27,21,21,17,17,17},{17,25,21,19,17,17,17},{14,17,17,17,17,17,14},
 {30,17,17,30,16,16,16},{14,17,17,17,21,18,13},{30,17,17,30,20,18,17},
 {15,16,16,14,1,1,30},{31,4,4,4,4,4,4},{17,17,17,17,17,17,14},
 {17,17,17,17,17,10,4},{17,17,17,21,21,21,10},{17,17,10,4,10,17,17},
 {17,17,10,4,4,4,4},{31,1,2,4,8,16,31},
 {0,0,0,31,0,0,0},{1,2,2,4,8,8,16},{0,0,0,0,0,0,0}
};
static int glyphIndex(char ch){
 if(ch>='0'&&ch<='9') return ch-'0';
 if(ch>='A'&&ch<='Z') return 10+ch-'A';
 if(ch=='-') return 36;
 if(ch=='/') return 37;
 return 38;
}
static qword_t *drawText(qword_t *q,int x,int y,int scale,const char *s,int r,int g,int b){
 int i,row,col;
 for(i=0;s[i];i++){
  int gi=glyphIndex(s[i]);
  for(row=0;row<7;row++) for(col=0;col<5;col++)
   if(font5x7[gi][row]&(1<<(4-col)))
    q=rect(q,x+i*6*scale+col*scale,y+row*scale,scale,scale,r,g,b);
 }
 return q;
}

static void showLogo(void) {
 packet_t *p=packet_init(2048,PACKET_NORMAL); qword_t *q=p->data;
 q=draw_disable_tests(q,0,&zbuf);
 q=draw_clear(q,0,2048-SW/2,2048-SH/2,SW,SH,5,7,13);
 /* Centered studio mark and readable RED CITY title. */
 q=rect(q,198,145,244,3,115,20,30);
 q=rect(q,198,274,244,3,115,20,30);
 q=rect(q,205,151,5,120,225,28,43);
 q=rect(q,430,151,5,120,225,28,43);
 q=drawText(q,213,185,5,"RED",235,45,55);
 q=drawText(q,315,185,5,"CITY",245,245,245);
 q=drawText(q,253,245,2,"NEXON GAMES",175,185,205);
 q=drawText(q,176,310,2,"A NEXON GAMES PRODUCTION",180,190,205);
 q=draw_finish(q);
 dma_wait_fast();
 dma_channel_send_normal(DMA_CHANNEL_GIF,p->data,q-p->data,0,0);
 draw_wait_finish(); graph_wait_vsync(); packet_free(p);
}
static void initPad(void) {
 int i;
 if(!padInitialized) {
  if(padInit(0)!=1) { padConnected=0; return; }
  padInitialized=1;
 }
 if(!padOpened) {
  if(!padPortOpen(0,0,padBuf)) { padConnected=0; return; }
  padOpened=1;
 }
 padReady=0;
 for(i=0;i<120;i++) {
  int s=padGetState(0,0);
  if(s==PAD_STATE_STABLE) { padReady=1; padConnected=1; break; }
  if(s==PAD_STATE_DISCONN) { padConnected=0; break; }
  graph_wait_vsync();
 }
}
static void initNexonGame(void) {
 NX_Vec3 p;
 int i;
 nx_init(&nxScene);
 nxPlayer=nx_spawn(&nxScene,NX_ENTITY_PLAYER,(NX_Vec3){0.0f,0.0f,0.0f});
 nx_set_player(&nxScene,nxPlayer);
 nx_camera_init(&nxCamera,42.0f,14.0f);
 nxCamera.position.x=0.0f; nxCamera.position.y=14.0f; nxCamera.position.z=42.0f;
 for(i=0;i<4;i++) {
  p.x=(i&1)?11.0f:-11.0f; p.y=-5.0f; p.z=-55.0f-(float)i*38.0f;
  nxTraffic[i]=nx_spawn(&nxScene,NX_ENTITY_VEHICLE,p);
  nxScene.entities[nxTraffic[i]].velocity.z=13.0f+(float)(i%2)*3.0f;
  p.x=(i&1)?-38.0f:38.0f; p.y=0.0f; p.z=-20.0f-(float)i*42.0f;
  nxPedestrians[i]=nx_spawn(&nxScene,NX_ENTITY_NPC,p);
  nxScene.entities[nxPedestrians[i]].velocity.x=(i&1)?-1.5f:1.5f;
 }
 memset(&nxClock,0,sizeof(nxClock));
}
static void updateWorld(void) {
 int i;
 NX_Entity *e;
 nx_time_step(&nxClock,1.0f/60.0f);
 for(i=0;i<4;i++) {
  e=&nxScene.entities[nxTraffic[i]];
  e->transform.position.z+=e->velocity.z*nxClock.deltaTime;
  if(e->transform.position.z>22.0f) e->transform.position.z=-205.0f-(float)i*18.0f;
  e=&nxScene.entities[nxPedestrians[i]];
  e->transform.position.x+=e->velocity.x*nxClock.deltaTime;
  if(e->transform.position.x>40.0f) e->velocity.x=-1.5f;
  if(e->transform.position.x< -40.0f) e->velocity.x=1.5f;
 }
 if(mission<10 && pz<=missionTargets[mission]+3.0f) {
  mission++;
 }
}
static void updatePad(void) {
 int s;
 if(!padInitialized || !padOpened) { initPad(); return; }
 if(!padReady) {
  s=padGetState(0,0);
  if(s==PAD_STATE_STABLE) { padReady=1; padConnected=1; }
  else { padConnected=0; return; }
 }
 s=padGetState(0,0);
 if(s!=PAD_STATE_STABLE) { padReady=0; padConnected=0; return; }
 if(!padRead(0,0,&pad)) return;
 {
  NX_Input in;
  NX_Entity *player=&nxScene.entities[nxPlayer];
  float ax=((float)pad.ljoy_h-128.0f)/127.0f;
  float ay=((float)pad.ljoy_v-128.0f)/127.0f;
  float rx=((float)pad.rjoy_h-128.0f)/127.0f;
  float ry=((float)pad.rjoy_v-128.0f)/127.0f;
  in.moveX=0.0f; in.moveZ=0.0f; in.turn=0.0f; in.pressed=0; in.held=0;
  /* Left stick movement with a dead zone; D-pad remains a reliable fallback. */
  if(ax>0.14f || ax< -0.14f) in.moveX=ax;
  if(ay>0.14f || ay< -0.14f) in.moveZ=ay;
  if(!(pad.btns & PAD_LEFT))  in.moveX=-1.0f;
  if(!(pad.btns & PAD_RIGHT)) in.moveX=1.0f;
  if(!(pad.btns & PAD_UP))    in.moveZ=-1.0f;
  if(!(pad.btns & PAD_DOWN))  in.moveZ=1.0f;
  /* Right stick and shoulder buttons orbit the third-person camera. */
  if(rx>0.14f || rx< -0.14f) cameraYaw+=rx*0.045f;
  if(ry>0.14f || ry< -0.14f) {
   cameraPitch+=ry*0.012f;
   if(cameraPitch<0.12f) cameraPitch=0.12f;
   if(cameraPitch>0.42f) cameraPitch=0.42f;
  }
  if(!(pad.btns & PAD_L1)) cameraYaw-=0.035f;
  if(!(pad.btns & PAD_R1)) cameraYaw+=0.035f;
  if(!(pad.btns & PAD_L2)) in.turn=-1.0f;
  if(!(pad.btns & PAD_R2)) in.turn=1.0f;
  if(!(pad.btns & PAD_CROSS)) in.held=1; /* Cross = sprint */
  nx_update_player(&nxScene,&in,1.0f/60.0f);
  if(!(pad.btns & PAD_CIRCLE)) {
   player->transform.position.x=0.0f;
   player->transform.position.z=0.0f;
   player->transform.rotation.y=0.0f;
   cameraYaw=0.0f; cameraPitch=0.34f; mission=0;
  }
  if(player->transform.position.x < -28.0f) player->transform.position.x=-28.0f;
  if(player->transform.position.x > 28.0f) player->transform.position.x=28.0f;
  if(player->transform.position.z < -180.0f) player->transform.position.z=-180.0f;
  if(player->transform.position.z > 20.0f) player->transform.position.z=20.0f;
  px=player->transform.position.x;
  pz=player->transform.position.z;
  heading=player->transform.rotation.y;
 }
 updateWorld();
}

static qword_t *drawCube(qword_t *q, MATRIX viewScreen, VECTOR pos, VECTOR rot,
                     float sx,float sy,float sz,int cr,int cg,int cb,
                     VECTOR *tmp, xyz_t *xyz, color_t *rgbaq,
                     prim_t *prim,color_t *base) {
 MATRIX lw,wv,ls; int i;
 VECTOR scaled[24]; int k;
 for(k=0;k<vertex_count;k++) {
  /* mesh vertices are authored at +/-10 units; convert requested half-extents. */
  scaled[k][0]=vertices[k][0]*(sx*0.1f);
  scaled[k][1]=vertices[k][1]*(sy*0.1f);
  scaled[k][2]=vertices[k][2]*(sz*0.1f);
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
  packets[0]=packet_init(12288,PACKET_NORMAL); packets[1]=packet_init(12288,PACKET_NORMAL);
  tmp=memalign(128,sizeof(VECTOR)*vertex_count); xyz=memalign(128,sizeof(xyz_t)*vertex_count);
  rgbaq=memalign(128,sizeof(color_t)*vertex_count);
  prim.type=PRIM_TRIANGLE; prim.shading=PRIM_SHADE_GOURAUD; prim.mapping=DRAW_DISABLE;
  prim.fogging=DRAW_DISABLE; prim.blending=DRAW_DISABLE; prim.antialiasing=DRAW_ENABLE;
  prim.mapping_type=PRIM_MAP_ST; prim.colorfix=PRIM_UNFIXED;
  base.r=128;base.g=128;base.b=128;base.a=128;base.q=1.0f;
  create_view_screen(viewScreen,graph_aspect_ratio(),-3.0f,3.0f,-2.2f,2.2f,1.0f,2000.0f);
 }
 nx_camera_follow(&nxCamera,&nxScene.entities[nxPlayer],1.0f/60.0f);
 /* Keep the camera behind the player while allowing right-stick orbit. */
 cameraPos[0]=px+sinf(cameraYaw)*cameraDistance;
 cameraPos[1]=cameraPitch*cameraDistance;
 cameraPos[2]=pz+cosf(cameraYaw)*cameraDistance;
 cameraRot[0]=cameraPitch; cameraRot[1]=cameraYaw; cameraRot[2]=0.0f;
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
 /* raised sidewalks and curbs make the road read as a real street */
 { VECTOR p={-39.0f,-11.6f,-75.0f,1},r={0,0,0,1};
   q=drawCube(q,viewScreen,p,r,4.0f,0.7f,130.0f,125,125,118,tmp,xyz,rgbaq,&prim,&base);
   p[0]=39.0f; q=drawCube(q,viewScreen,p,r,4.0f,0.7f,130.0f,125,125,118,tmp,xyz,rgbaq,&prim,&base);
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
 /* shopfront doors and simple street trees add scale and depth */
 for(i=0;i<6;i++) {
  VECTOR d={-35.2f,-6.0f,-25.0f-i*42.0f,1},r={0,0,0,1};
  q=drawCube(q,viewScreen,d,r,0.4f,4.0f,2.5f,65,43,32,tmp,xyz,rgbaq,&prim,&base);
  d[0]=35.2f; q=drawCube(q,viewScreen,d,r,0.4f,4.0f,2.5f,55,58,65,tmp,xyz,rgbaq,&prim,&base);
 }
 for(i=0;i<4;i++) {
  VECTOR t={-40.0f,-6.5f,-38.0f-i*48.0f,1},r={0,0,0,1};
  q=drawCube(q,viewScreen,t,r,1.0f,5.5f,1.0f,95,65,42,tmp,xyz,rgbaq,&prim,&base);
  t[1]=1.0f; q=drawCube(q,viewScreen,t,r,4.0f,4.0f,4.0f,42,105,62,tmp,xyz,rgbaq,&prim,&base);
  t[0]=40.0f; t[1]=-6.5f; q=drawCube(q,viewScreen,t,r,1.0f,5.5f,1.0f,95,65,42,tmp,xyz,rgbaq,&prim,&base);
  t[1]=1.0f; q=drawCube(q,viewScreen,t,r,4.0f,4.0f,4.0f,42,105,62,tmp,xyz,rgbaq,&prim,&base);
 }
 /* street lamps: poles and warm lamp heads */
 for(i=0;i<5;i++) {
  VECTOR lp={-31.0f,-1.0f,-18.0f-i*48.0f,1},lr={0,0,0,1};
  q=drawCube(q,viewScreen,lp,lr,0.65f,10.0f,0.65f,35,38,43,tmp,xyz,rgbaq,&prim,&base);
  lp[0]=31.0f; q=drawCube(q,viewScreen,lp,lr,0.65f,10.0f,0.65f,35,38,43,tmp,xyz,rgbaq,&prim,&base);
  lp[1]=10.0f; q=drawCube(q,viewScreen,lp,lr,2.0f,0.8f,1.0f,255,205,115,tmp,xyz,rgbaq,&prim,&base);
  lp[0]=-31.0f; q=drawCube(q,viewScreen,lp,lr,2.0f,0.8f,1.0f,255,205,115,tmp,xyz,rgbaq,&prim,&base);
 }
 /* Mission beacon advances through the ten-stage opening story. */
 if(mission<10) {
  VECTOR wp={0,8,missionTargets[mission],1},wr={0,0,0,1};
  q=drawCube(q,viewScreen,wp,wr,1.2f,15.0f,1.2f,245,185,35,tmp,xyz,rgbaq,&prim,&base);
  wp[1]=24.0f; q=drawCube(q,viewScreen,wp,wr,4.0f,1.0f,4.0f,255,220,75,tmp,xyz,rgbaq,&prim,&base);
 }
 /* Moving traffic vehicles and walking pedestrians are engine entities. */
 for(i=0;i<4;i++) {
  NX_Entity *v=&nxScene.entities[nxTraffic[i]];
  VECTOR vp={v->transform.position.x,-5.0f,v->transform.position.z,1},vr={0,0,0,1};
  q=drawCube(q,viewScreen,vp,vr,4.0f,1.8f,7.0f,(i&1)?175:35,(i&1)?35:95,(i&1)?35:165,tmp,xyz,rgbaq,&prim,&base);
  vp[1]=-2.8f;
  q=drawCube(q,viewScreen,vp,vr,3.0f,1.0f,3.8f,85,145,175,tmp,xyz,rgbaq,&prim,&base);
  /* Four visible dark wheels and front headlights on each traffic car. */
  { int wx,wz; for(wx=-1;wx<=1;wx+=2) for(wz=-1;wz<=1;wz+=2) {
    VECTOR wheel={v->transform.position.x+wx*3.2f,-6.3f,v->transform.position.z+wz*4.6f,1};
    q=drawCube(q,viewScreen,wheel,vr,0.8f,1.5f,1.1f,22,24,28,tmp,xyz,rgbaq,&prim,&base);
   }
   VECTOR lamp={v->transform.position.x-2.3f,-4.9f,v->transform.position.z-6.8f,1};
   q=drawCube(q,viewScreen,lamp,vr,0.45f,0.45f,0.3f,255,225,145,tmp,xyz,rgbaq,&prim,&base);
   lamp[0]=v->transform.position.x+2.3f;
   q=drawCube(q,viewScreen,lamp,vr,0.45f,0.45f,0.3f,255,225,145,tmp,xyz,rgbaq,&prim,&base);
  }
  {
   NX_Entity *n=&nxScene.entities[nxPedestrians[i]];
   VECTOR np={n->transform.position.x,-2.0f,n->transform.position.z,1};
   q=drawCube(q,viewScreen,np,vr,1.5f,4.0f,1.5f,65,95,155,tmp,xyz,rgbaq,&prim,&base);
   np[1]=2.8f;
   q=drawCube(q,viewScreen,np,vr,1.2f,1.2f,1.2f,205,165,130,tmp,xyz,rgbaq,&prim,&base);
   np[0]-=2.1f; np[1]=-1.8f; q=drawCube(q,viewScreen,np,vr,0.65f,3.0f,0.7f,45,65,115,tmp,xyz,rgbaq,&prim,&base);
   np[0]+=4.2f; q=drawCube(q,viewScreen,np,vr,0.65f,3.0f,0.7f,45,65,115,tmp,xyz,rgbaq,&prim,&base);
   np[0]-=2.1f; np[1]=-5.0f; np[2]-=0.4f;
   q=drawCube(q,viewScreen,np,vr,0.7f,2.5f,0.8f,35,40,55,tmp,xyz,rgbaq,&prim,&base);
   np[0]+=1.5f; q=drawCube(q,viewScreen,np,vr,0.7f,2.5f,0.8f,35,40,55,tmp,xyz,rgbaq,&prim,&base);
   np[0]=n->transform.position.x; np[1]=3.9f; np[2]=n->transform.position.z;
   q=drawCube(q,viewScreen,np,vr,1.25f,0.45f,1.25f,35,28,24,tmp,xyz,rgbaq,&prim,&base);
   np[1]=2.8f; np[2]=n->transform.position.z-1.25f; np[0]-=0.35f;
   q=drawCube(q,viewScreen,np,vr,0.16f,0.18f,0.12f,20,20,20,tmp,xyz,rgbaq,&prim,&base);
   np[0]+=0.7f; q=drawCube(q,viewScreen,np,vr,0.16f,0.18f,0.12f,20,20,20,tmp,xyz,rgbaq,&prim,&base);
  }
 }
 /* Player avatar is a 3D block character; camera follows from behind */
 { VECTOR p={px,-2,pz,1},r={0,heading,0,1};
  q=drawCube(q,viewScreen,p,r,3.2f,7.0f,2.4f,190,35,42,tmp,xyz,rgbaq,&prim,&base);
  p[1]=6.0f; q=drawCube(q,viewScreen,p,r,2.6f,2.6f,2.6f,220,175,140,tmp,xyz,rgbaq,&prim,&base);
  p[0]=px-4.0f; p[1]=-1.5f; p[2]=pz; q=drawCube(q,viewScreen,p,r,1.0f,4.5f,1.0f,190,35,42,tmp,xyz,rgbaq,&prim,&base);
  p[0]=px+4.0f; q=drawCube(q,viewScreen,p,r,1.0f,4.5f,1.0f,190,35,42,tmp,xyz,rgbaq,&prim,&base);
  p[0]=px-1.5f; p[1]=-9.0f; q=drawCube(q,viewScreen,p,r,1.1f,3.0f,1.2f,28,32,42,tmp,xyz,rgbaq,&prim,&base);
  p[0]=px+1.5f; p[1]=-9.0f; q=drawCube(q,viewScreen,p,r,1.1f,3.0f,1.2f,28,32,42,tmp,xyz,rgbaq,&prim,&base);
  p[0]=px; p[1]=8.0f; p[2]=pz; q=drawCube(q,viewScreen,p,r,2.65f,0.55f,2.65f,35,27,23,tmp,xyz,rgbaq,&prim,&base);
  p[1]=6.0f; p[2]=pz-2.35f; p[0]=px-0.72f;
  q=drawCube(q,viewScreen,p,r,0.22f,0.24f,0.16f,18,18,18,tmp,xyz,rgbaq,&prim,&base);
  p[0]=px+0.72f; q=drawCube(q,viewScreen,p,r,0.22f,0.24f,0.16f,18,18,18,tmp,xyz,rgbaq,&prim,&base);
  p[0]=px; p[1]=5.35f; q=drawCube(q,viewScreen,p,r,0.18f,0.18f,0.16f,135,85,70,tmp,xyz,rgbaq,&prim,&base);
 }
 /* Full HUD: minimap bottom-left, health directly underneath. */
 q=draw_disable_tests(q,0,&zbuf);
 q=rect(q,12,12,250,34,10,15,24);
 q=rect(q,20,20,12,18,padConnected?45:220,padConnected?220:45,55);
 q=drawText(q,42,19,1,"RED CITY",235,235,240);
 q=drawText(q,145,19,1,padConnected?"PAD OK":"NO PAD",padConnected?90:245,padConnected?225:85,padConnected?130:85);
 q=rect(q,SW/2-118,12,236,34,10,15,24);
 if(mission<10) q=drawText(q,SW/2-108,19,1,"MISSION - REACH MARKER",245,210,105);
 else q=drawText(q,SW/2-65,19,1,"MISSION COMPLETE",100,235,140);
 /* Street-style minimap. */
 q=rect(q,12,SH-151,112,112,8,12,18);
 q=rect(q,18,SH-145,100,100,30,45,42);
 q=rect(q,65,SH-145,7,100,155,155,145);
 q=rect(q,18,SH-103,100,7,155,155,145);
 q=rect(q,39,SH-145,3,100,95,110,100);
 q=rect(q,18,SH-121,100,3,95,110,100);
 q=rect(q,18,SH-76,100,3,95,110,100);
 q=rect(q,86,SH-145,3,100,95,110,100);
 { int mx=68+(int)(px*0.22f); int my=SH-140+(int)(-pz*0.16f);
   if(mx<21)mx=21; if(mx>111)mx=111;
   if(my<SH-140)my=SH-140; if(my>SH-51)my=SH-51;
   q=rect(q,mx-3,my-3,7,7,255,55,55);
 }
 /* Health bar under the minimap. */
 q=rect(q,12,SH-34,112,22,8,12,18);
 q=drawText(q,18,SH-28,1,"HP",225,230,235);
 q=rect(q,39,SH-27,77,8,65,35,35);
 q=rect(q,39,SH-27,70,8,55,205,105);
 /* Controller help bottom-right. */
 q=rect(q,SW-212,SH-48,200,36,8,12,18);
 q=drawText(q,SW-202,SH-41,1,"MOVE - LEFT STICK",225,230,235);
 q=drawText(q,SW-202,SH-27,1,"CAMERA - RIGHT STICK",150,190,220);
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
 initNexonGame();
 initPad();
 for(;;) { updatePad(); render(); }
 return 0;
}
