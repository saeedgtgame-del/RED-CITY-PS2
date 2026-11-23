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
#define SH 512
#define OBJECTS 15

static framebuffer_t frame;
static zbuffer_t zbuf;
static unsigned char padBuf[256] __attribute__((aligned(64)));
static struct padButtonStatus pad;
static int padReady=0, mission=0, logoFrames=0;
static int padConnected=0, padInitialized=0, padOpened=0, padPort=0;
static int inCar=0, mapVisible=0, playerVehicle=-1;
static int phoneVisible=0, phonePage=0, phoneSelection=0;
static int lastTriangle=0, lastStart=0, lastSelect=0, lastCross=0, lastCircle=0, lastUp=0, lastDown=0;
static float carSpeed=0.0f, carHeading=0.0f;
static NX_Scene nxScene;
static NX_Camera nxCamera;
static NX_Time nxClock;
static int nxPlayer=-1;
static float gameMinute=480.0f, daylight=0.81f, weatherTimer=0.0f, windStrength=0.18f, windTarget=0.18f, gustTimer=0.0f, dustPhase=0.0f;
static unsigned int weatherSeed=0x51A7u;
static int weatherType=0;
static int nxTraffic[5];
static int nxPedestrians[4];
static const float missionTargets[15]={-70.0f,-145.0f,-220.0f,-295.0f,-370.0f,-445.0f,-520.0f,-595.0f,-670.0f,-745.0f,-820.0f,-895.0f,-970.0f,-1045.0f,-1120.0f};
static float px=0.0f, pz=0.0f, heading=0.0f;
static float cameraYaw=0.0f, cameraPitch=0.35f, cameraDistance=8.0f;
static int cameraReady=0;
static VECTOR cameraPos={-29.0f,5.5f,-14.0f,1.0f};
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
 q=drawText(q,205,190,5,"RED CITY",245,245,245);
 q=drawText(q,253,245,2,"NEXON GAMES",175,185,205);
 q=drawText(q,176,310,2,"A NEXON GAMES PRODUCTION",180,190,205);
 q=draw_finish(q);
 dma_wait_fast();
 dma_channel_send_normal(DMA_CHANNEL_GIF,p->data,q-p->data,0,0);
 draw_wait_finish(); graph_wait_vsync(); packet_free(p);
}
static void initPad(void) {
 int i,port;
 if(!padInitialized) {
  if(padInit(0)!=1) { padConnected=0; return; }
  padInitialized=1;
 }
 if(!padOpened) {
  /* Try both physical controller ports instead of assuming port 1. */
  for(port=0;port<2;port++) {
   if(padPortOpen(port,0,padBuf)) {
    padPort=port;
    padOpened=1;
    break;
   }
  }
  if(!padOpened) { padConnected=0; return; }
  /* Explicitly enable DualShock analog sticks. */
  padSetMainMode(padPort,0,PAD_MMODE_DUALSHOCK,PAD_MMODE_LOCK);
  for(i=0;i<30 && padGetReqState(padPort,0)==PAD_RSTAT_BUSY;i++) graph_wait_vsync();
 }
 padReady=0;
 for(i=0;i<120;i++) {
  int s=padGetState(padPort,0);
  if(s==PAD_STATE_STABLE) { padReady=1; padConnected=1; break; }
  if(s==PAD_STATE_DISCONN) { padConnected=0; break; }
  graph_wait_vsync();
 }
}
static void initNexonGame(void) {
 NX_Vec3 p;
 int i;
 nx_init(&nxScene);
 nxPlayer=nx_spawn(&nxScene,NX_ENTITY_PLAYER,(NX_Vec3){-29.0f,-0.35f,0.0f});
 px=-29.0f; pz=0.0f; heading=0.0f;
 nx_set_player(&nxScene,nxPlayer);
 nxScene.entities[nxPlayer].radius=1.8f;
 playerVehicle=nx_spawn(&nxScene,NX_ENTITY_VEHICLE,(NX_Vec3){-19.0f,-5.0f,-5.0f});
 nxScene.entities[playerVehicle].radius=4.2f;
 nx_camera_init(&nxCamera,32.0f,12.0f);
 nxCamera.position.x=0.0f; nxCamera.position.y=14.0f; nxCamera.position.z=42.0f;
 for(i=0;i<4;i++) {
  p.x=(i&1)?11.0f:-11.0f; p.y=-5.0f; p.z=-55.0f-(float)i*38.0f;
  nxTraffic[i]=nx_spawn(&nxScene,NX_ENTITY_VEHICLE,p);
  nxScene.entities[nxTraffic[i]].radius=3.5f;
  nxScene.entities[nxTraffic[i]].velocity.z=13.0f+(float)(i%2)*3.0f;
  p.x=(i&1)?-38.0f:38.0f; p.y=0.0f; p.z=-20.0f-(float)i*42.0f;
  nxPedestrians[i]=nx_spawn(&nxScene,NX_ENTITY_NPC,p);
  nxScene.entities[nxPedestrians[i]].radius=1.25f;
  nxScene.entities[nxPedestrians[i]].velocity.x=(i&1)?-1.5f:1.5f;
 }
 /* Fifth traffic entity is a patrol car with a slower city巡回 route. */
 p.x=0.0f; p.y=-5.0f; p.z=-285.0f;
 nxTraffic[4]=nx_spawn(&nxScene,NX_ENTITY_VEHICLE,p);
 nxScene.entities[nxTraffic[4]].radius=3.8f;
 nxScene.entities[nxTraffic[4]].velocity.z=9.0f;
 memset(&nxClock,0,sizeof(nxClock));
}
static unsigned int worldRand(void) { weatherSeed=weatherSeed*1664525u+1013904223u; return weatherSeed; }
static void updateWorld(void) {
 int i;
 NX_Entity *e;
 const float dt=1.0f/60.0f;
 nx_time_step(&nxClock,dt);
 gameMinute+=24.0f*dt; while(gameMinute>=1440.0f) gameMinute-=1440.0f;
 /* Smooth 24-hour ambient cycle: dim blue nights, warm dawn/dusk, bright noon. */
 daylight=0.25f+0.75f*(0.5f+0.5f*sinf((gameMinute-360.0f)*0.00436332313f));
 gustTimer-=dt; if(gustTimer<=0.0f) { windTarget=0.08f+(float)(worldRand()%1000u)/1000.0f*1.45f; gustTimer=5.0f+(float)(worldRand()%1200u)/100.0f; }
 windStrength+=(windTarget-windStrength)*0.008f; dustPhase+=dt*(1.0f+windStrength*4.0f);
 weatherTimer+=dt; if(weatherTimer>600.0f+(float)(worldRand()%1200u)) { int n=(int)(worldRand()%5u); if(n==weatherType)n=(n+1+(int)(worldRand()%4u))%5; weatherType=n; weatherTimer=0.0f; }
 for(i=0;i<5;i++) {
  e=&nxScene.entities[nxTraffic[i]];
  e->transform.position.z+=e->velocity.z*nxClock.deltaTime;
  if(e->transform.position.z>22.0f) e->transform.position.z=-820.0f-(float)i*24.0f;
  e=&nxScene.entities[nxPedestrians[i]];
  e->transform.position.x+=e->velocity.x*nxClock.deltaTime;
  if(e->transform.position.x>40.0f) e->velocity.x=-1.5f;
  if(e->transform.position.x< -40.0f) e->velocity.x=1.5f;
 }
 if(mission<15 && pz<=missionTargets[mission]+3.0f) {
  mission++;
 }
}
/* Planar contact response keeps the player from walking through traffic or pedestrians. */
static void resolvePlayerContacts(NX_Entity *player) {
 int i;
 if(!player || !player->active) return;
 for(i=0;i<5;i++) {
  NX_Entity *other=&nxScene.entities[nxTraffic[i]];
  float dx=player->transform.position.x-other->transform.position.x;
  float dz=player->transform.position.z-other->transform.position.z;
  float minDist=player->radius+other->radius;
  float d2=dx*dx+dz*dz;
  if(d2<minDist*minDist && d2>0.0001f) {
   float d=sqrtf(d2), push=(minDist-d);
   dx/=d; dz/=d;
   player->transform.position.x+=dx*push;
   player->transform.position.z+=dz*push;
   player->velocity.x*=0.45f; player->velocity.z*=0.45f;
  }
 }
 for(i=0;i<4;i++) {
  NX_Entity *other=&nxScene.entities[nxPedestrians[i]];
  float dx=player->transform.position.x-other->transform.position.x;
  float dz=player->transform.position.z-other->transform.position.z;
  float minDist=player->radius+other->radius;
  float d2=dx*dx+dz*dz;
  if(d2<minDist*minDist && d2>0.0001f) {
   float d=sqrtf(d2), push=(minDist-d);
   dx/=d; dz/=d;
   player->transform.position.x+=dx*push;
   player->transform.position.z+=dz*push;
   player->velocity.x*=0.65f; player->velocity.z*=0.65f;
  }
 }
 if(player->transform.position.x < -52.0f) player->transform.position.x=-52.0f;
 if(player->transform.position.x > 52.0f) player->transform.position.x=52.0f;
}

/* Vehicle-to-traffic contact: keep the driven car from passing through traffic. */
static void resolveVehicleContacts(void) {
 int i;
 NX_Entity *car=&nxScene.entities[playerVehicle];
 if(!car->active) return;
 for(i=0;i<4;i++) {
  NX_Entity *other=&nxScene.entities[nxTraffic[i]];
  float dx=car->transform.position.x-other->transform.position.x;
  float dz=car->transform.position.z-other->transform.position.z;
  float minDist=car->radius+other->radius;
  float d2=dx*dx+dz*dz;
  if(d2<minDist*minDist && d2>0.0001f) {
   float d=sqrtf(d2), push=minDist-d;
   dx/=d; dz/=d;
   car->transform.position.x+=dx*push;
   car->transform.position.z+=dz*push;
   carSpeed*= -0.22f;
   car->velocity.x*=0.25f; car->velocity.z*=0.25f;
  }
 }
 if(car->transform.position.x < -52.0f) car->transform.position.x=-52.0f;
 if(car->transform.position.x > 52.0f) car->transform.position.x=52.0f;
}
static void updatePad(void) {
 int s;
 if(!padInitialized || !padOpened) { initPad(); return; }
 if(!padReady) {
  s=padGetState(padPort,0);
  if(s==PAD_STATE_STABLE) { padReady=1; padConnected=1; }
  else { padConnected=0; return; }
 }
 s=padGetState(padPort,0);
 if(s!=PAD_STATE_STABLE) {
  padReady=0; padConnected=0;
  pad.btns=0xFFFF; pad.ljoy_h=128; pad.ljoy_v=128; pad.rjoy_h=128; pad.rjoy_v=128;
  return;
 }
 if(!padRead(padPort,0,&pad)) {
  padConnected=0;
  pad.btns=0xFFFF; pad.ljoy_h=128; pad.ljoy_v=128; pad.rjoy_h=128; pad.rjoy_v=128;
  return;
 }
 padConnected=1;
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
  if(padConnected && (rx>0.22f || rx< -0.22f)) cameraYaw-=rx*0.018f;
  if(padConnected && (ry>0.22f || ry< -0.22f)) {
   cameraPitch-=ry*0.007f;
   if(cameraPitch<0.12f) cameraPitch=0.12f;
   if(cameraPitch>0.78f) cameraPitch=0.78f;
  }
  if(padConnected && !(pad.btns & PAD_L1)) cameraYaw-=0.022f;
  if(padConnected && !(pad.btns & PAD_R1)) cameraYaw+=0.022f;
  if(!(pad.btns & PAD_L2)) in.turn=-1.0f;
  if(!(pad.btns & PAD_R2)) in.turn=1.0f;
  if(!(pad.btns & PAD_CROSS)) in.held=1; /* Cross = sprint */
  { int tri=(!(pad.btns & PAD_TRIANGLE)); int sta=(!(pad.btns & PAD_START));
    int sel=(!(pad.btns & PAD_SELECT)), cross=(!(pad.btns & PAD_CROSS));
    int circle=(!(pad.btns & PAD_CIRCLE)), up=(!(pad.btns & PAD_UP)), down=(!(pad.btns & PAD_DOWN));
    if(sel && !lastSelect) { phoneVisible=!phoneVisible; if(phoneVisible) { phonePage=0; phoneSelection=0; } }
    if(phoneVisible) {
      if(up && !lastUp) { phoneSelection=(phoneSelection+3)%4; }
      if(down && !lastDown) { phoneSelection=(phoneSelection+1)%4; }
      if(cross && !lastCross) {
        if(phonePage==0) {
          if(phoneSelection==0) phonePage=1;
          else if(phoneSelection==1) phonePage=2;
          else if(phoneSelection==2) { mapVisible=1; phoneVisible=0; }
          else phonePage=3;
        }
      }
      if(circle && !lastCircle) { if(phonePage) phonePage=0; else phoneVisible=0; }
      in.moveX=0.0f; in.moveZ=0.0f; in.held=0;
    } else {
      if(tri && !lastTriangle) {
        if(!inCar) {
          float dx=player->transform.position.x-nxScene.entities[playerVehicle].transform.position.x;
          float dz=player->transform.position.z-nxScene.entities[playerVehicle].transform.position.z;
          if(dx*dx+dz*dz<225.0f) { inCar=1; carSpeed=0.0f; }
        } else { inCar=0; player->transform.position.x=nxScene.entities[playerVehicle].transform.position.x+5.0f; player->transform.position.z=nxScene.entities[playerVehicle].transform.position.z; }
      }
      if(sta && !lastStart) mapVisible=!mapVisible;
    }
    lastTriangle=tri; lastStart=sta; lastSelect=sel; lastCross=cross;
    lastCircle=circle; lastUp=up; lastDown=down;
  }
  if(inCar) {
    NX_Entity *car=&nxScene.entities[playerVehicle];
    float throttle=-in.moveZ;
    carSpeed+=throttle*22.0f*(1.0f/60.0f); carSpeed*=0.988f;
    if(carSpeed>34.0f) carSpeed=34.0f; if(carSpeed< -12.0f) carSpeed=-12.0f;
    carHeading+=in.moveX*(carSpeed>=0.0f?1.0f:-1.0f)*(0.9f+fabsf(carSpeed)*0.035f)*(1.0f/60.0f);
    car->transform.rotation.y=carHeading;
    car->velocity.x=sinf(carHeading)*carSpeed; car->velocity.z=-cosf(carHeading)*carSpeed;
    car->transform.position.x+=car->velocity.x/60.0f; car->transform.position.z+=car->velocity.z/60.0f;
    if(!(pad.btns & PAD_SQUARE)) carSpeed*=0.90f;
    if(car->transform.position.x < -52.0f) { car->transform.position.x=-52.0f; carSpeed*=0.55f; }
    if(car->transform.position.x > 52.0f) { car->transform.position.x=52.0f; carSpeed*=0.55f; }
    if(car->transform.position.z < -900.0f) car->transform.position.z=-900.0f;
    if(car->transform.position.z > 20.0f) car->transform.position.z=20.0f;
    resolveVehicleContacts();
    px=car->transform.position.x; pz=car->transform.position.z; heading=carHeading;
  } else {
    /* Camera-relative third-person movement.
       The camera yaw defines forward/right; the avatar smoothly turns toward
       the actual world-space travel direction before moving. */
    {
      float forward=-in.moveZ, right=in.moveX;
      float mag=sqrtf(right*right+forward*forward);
      if(mag>1.0f) mag=1.0f;
      if(mag>0.12f) {
        float cs=cosf(cameraYaw), sn=sinf(cameraYaw);
        float worldX=right*cs-forward*sn;
        float worldZ=right*sn-forward*cs;
        float desired=atan2f(worldX,-worldZ);
        float diff=desired-player->transform.rotation.y;
        while(diff>3.14159265f) diff-=6.28318530f;
        while(diff< -3.14159265f) diff+=6.28318530f;
        player->transform.rotation.y+=diff*0.24f;
        in.moveX=0.0f;
        in.moveZ=-mag;
      } else {
        in.moveX=0.0f;
        in.moveZ=0.0f;
      }
    }
    nx_update_player(&nxScene,&in,1.0f/60.0f);
    if(!(pad.btns & PAD_CIRCLE)) { player->transform.position.x=-29.0f; player->transform.position.z=0.0f; player->transform.rotation.y=0.0f; cameraYaw=0.0f; cameraPitch=0.30f; cameraReady=0; mission=0; }
    if(player->transform.position.x < -52.0f) player->transform.position.x=-52.0f;
    if(player->transform.position.x > 52.0f) player->transform.position.x=52.0f;
    resolvePlayerContacts(player);
    if(player->transform.position.z < -900.0f) player->transform.position.z=-900.0f;
    if(player->transform.position.z > 20.0f) player->transform.position.z=20.0f;
    px=player->transform.position.x; pz=player->transform.position.z; heading=player->transform.rotation.y;
  }
 }
}

#define MAX_CLIPPED_VERTICES 256

/* Clip a homogeneous polygon against one of the six view-frustum planes.
   Unlike dropping every triangle that touches a clipped vertex, this keeps
   the visible part of roads, buildings and other large meshes. */
static float clipDistance(const VECTOR v,int plane) {
 switch(plane) {
  case 0: return v[3]+v[0];
  case 1: return v[3]-v[0];
  case 2: return v[3]+v[1];
  case 3: return v[3]-v[1];
  case 4: return v[3]+v[2];
  default:return v[3]-v[2];
 }
}
static int clipPolygonPlane(const VECTOR *input,int count,VECTOR *output,int plane) {
 int i,outCount=0;
 if(count<3) return 0;
 for(i=0;i<count;i++) {
  const VECTOR *s=&input[(i+count-1)%count];
  const VECTOR *e=&input[i];
  float ds=clipDistance(*s,plane),de=clipDistance(*e,plane);
  int inS=(ds>=0.0f),inE=(de>=0.0f),k;
  if(inS!=inE) {
   float den=ds-de;
   float t=(fabsf(den)>0.000001f)?ds/den:0.0f;
   if(outCount<12) {
    for(k=0;k<4;k++) output[outCount][k]=(*s)[k]+((*e)[k]-(*s)[k])*t;
    outCount++;
   }
  }
  if(inE && outCount<12) {
   for(k=0;k<4;k++) output[outCount][k]=(*e)[k];
   outCount++;
  }
 }
 return outCount;
}

static qword_t *drawCube(qword_t *q, MATRIX viewScreen, VECTOR pos, VECTOR rot,
                     float sx,float sy,float sz,int cr,int cg,int cb,
                     VECTOR *tmp, xyz_t *xyz, color_t *rgbaq,
                     prim_t *prim,color_t *base) {
 MATRIX lw,wv,ls;
 int i,outCount=0;
 VECTOR scaled[24];
 color_f_t colours[MAX_CLIPPED_VERTICES];
 for(i=0;i<vertex_count;i++) {
  scaled[i][0]=vertices[i][0]*(sx*0.1f);
  scaled[i][1]=vertices[i][1]*(sy*0.1f);
  scaled[i][2]=vertices[i][2]*(sz*0.1f);
  scaled[i][3]=vertices[i][3];
 }
 create_local_world(lw,pos,rot);
 create_world_view(wv,cameraPos,cameraRot);
 create_local_screen(ls,lw,wv,viewScreen);

 /* Transform each triangle in homogeneous clip space, clip it against all
    six frustum planes, then perspective-divide only the surviving vertices. */
 for(i=0;i<points_count;i+=3) {
  VECTOR a[12],b[12];
  int n=3,plane,j,k;
  vector_apply(a[0],scaled[points[i]],ls);
  vector_apply(a[1],scaled[points[i+1]],ls);
  vector_apply(a[2],scaled[points[i+2]],ls);
  for(plane=0;plane<6 && n>=3;plane++) {
   n=clipPolygonPlane(a,n,b,plane);
   for(j=0;j<n;j++) for(k=0;k<4;k++) a[j][k]=b[j][k];
  }
  if(n<3) continue;
  for(j=1;j<n-1;j++) {
   int ids[3]={0,j,j+1};
   for(k=0;k<3;k++) {
    float *v=a[ids[k]];
    float w=v[3];
    if(fabsf(w)<0.000001f || outCount>=MAX_CLIPPED_VERTICES) break;
    tmp[outCount][0]=v[0]/w;
    tmp[outCount][1]=v[1]/w;
    tmp[outCount][2]=v[2]/w;
    tmp[outCount][3]=1.0f;
    outCount++;
   }
   if(k<3) break;
  }
 }
 if(outCount<3) return q;
 for(i=0;i<outCount;i++) {
  colours[i].r=cr/255.0f;
  colours[i].g=cg/255.0f;
  colours[i].b=cb/255.0f;
  colours[i].a=1.0f;
 }
 draw_convert_xyz(xyz,2048,2048,32,outCount,(vertex_f_t*)tmp);
 draw_convert_rgbq(rgbaq,outCount,(vertex_f_t*)tmp,colours,0x80);
 q=draw_prim_start(q,0,prim,base);
 for(i=0;i<outCount;i++) {
  q->dw[0]=rgbaq[i].rgbaq;
  q->dw[1]=xyz[i].xyz;
  q++;
 }
 q=draw_prim_end(q,2,DRAW_RGBAQ_REGLIST);
 return q;
}
static void render(void) {
 static packet_t *packets[2]={0,0}; static int ctx=0;
 static VECTOR *tmp=0; static xyz_t *xyz=0; static color_t *rgbaq=0;
 static prim_t prim; static color_t base; static MATRIX viewScreen;
 qword_t *q,*tag; int i;
 if(!packets[0]) {
  packets[0]=packet_init(65536,PACKET_NORMAL); packets[1]=packet_init(65536,PACKET_NORMAL);
  tmp=memalign(128,sizeof(VECTOR)*MAX_CLIPPED_VERTICES); xyz=memalign(128,sizeof(xyz_t)*MAX_CLIPPED_VERTICES);
  rgbaq=memalign(128,sizeof(color_t)*MAX_CLIPPED_VERTICES);
  prim.type=PRIM_TRIANGLE; prim.shading=PRIM_SHADE_GOURAUD; prim.mapping=DRAW_DISABLE;
  prim.fogging=DRAW_DISABLE; prim.blending=DRAW_DISABLE; prim.antialiasing=DRAW_ENABLE;
  prim.mapping_type=PRIM_MAP_ST; prim.colorfix=PRIM_UNFIXED;
  base.r=128;base.g=128;base.b=128;base.a=128;base.q=1.0f;
  create_view_screen(viewScreen,graph_aspect_ratio(),-3.0f,3.0f,-2.2f,2.2f,5.0f,2000.0f);
 }
 /* Third-person follow camera.
    PS2SDK create_world_view() internally negates camera translation and
    rotation. Camera position is placed BEHIND the character's -Z-forward
    direction, and camera world rotation points back toward the target. */
 { NX_Entity *follow=inCar?&nxScene.entities[playerVehicle]:&nxScene.entities[nxPlayer];
  float targetX=follow->transform.position.x;
  float targetZ=follow->transform.position.z;
  float targetY=follow->transform.position.y+(inCar?1.35f:1.55f);
  float targetHeading=inCar?carHeading:follow->transform.rotation.y;
  float cy=targetHeading+cameraYaw;
  float dist=inCar?11.0f:cameraDistance;
  float desiredHeight=dist*tanf(cameraPitch);
  float tx=targetX-sinf(cy)*dist;
  float ty=targetY+desiredHeight;
  float tz=targetZ+cosf(cy)*dist;
  float dx=targetX-tx;
  float dy=targetY-ty;
  float dz=targetZ-tz;
  px=targetX; pz=targetZ; heading=targetHeading;
  if(!cameraReady) {
   cameraPos[0]=tx; cameraPos[1]=ty; cameraPos[2]=tz; cameraReady=1;
  } else {
   cameraPos[0]+=(tx-cameraPos[0])*0.34f;
   cameraPos[1]+=(ty-cameraPos[1])*0.30f;
   cameraPos[2]+=(tz-cameraPos[2])*0.34f;
  }
  dx=targetX-cameraPos[0];
  dy=targetY-cameraPos[1];
  dz=targetZ-cameraPos[2];
  /* Camera's local forward axis is -Z. These signs are essential:
     positive pitch/yaw here used to point away from the character. */
  cameraRot[0]=atan2f(dy,sqrtf(dx*dx+dz*dz));
  cameraRot[1]=atan2f(-dx,-dz);
  cameraRot[2]=0.0f;
 }
 tag=packets[ctx]->data; q=tag+1;
 q=draw_disable_tests(q,0,&zbuf);
 { int sr=weatherType==3?72:(weatherType==4?145:(weatherType==2?112:90)); int sg=weatherType==3?88:(weatherType==4?155:(weatherType==2?112:135)); int sb=weatherType==3?105:(weatherType==4?175:(weatherType==2?112:165)); q=draw_clear(q,0,2048-SW/2,2048-SH/2,SW,SH,(int)(sr*daylight),(int)(sg*daylight),(int)(sb*(0.75f+daylight*0.25f))); }
 q=draw_enable_tests(q,0,&zbuf);
 /* Weather and skyline: layered clouds, a warm sun and distant hills. */
 { int w; VECTOR wr={0,0,0,1};
   VECTOR sun={px+82.0f,92.0f,pz-190.0f,1};
   q=drawCube(q,viewScreen,sun,wr,9.0f,9.0f,2.0f,255,205,105,tmp,xyz,rgbaq,&prim,&base);
   for(w=0;w<6;w++) {
    float wz=pz-100.0f-(float)w*115.0f;
    float wx=px-70.0f+(float)((w*31)%140);
    VECTOR cloud={wx,67.0f,wz,1};
    q=drawCube(q,viewScreen,cloud,wr,10.0f,2.6f,5.0f,205,215,220,tmp,xyz,rgbaq,&prim,&base);
    cloud[0]+=9.0f; cloud[1]-=1.0f;
    q=drawCube(q,viewScreen,cloud,wr,8.0f,2.0f,4.0f,220,226,228,tmp,xyz,rgbaq,&prim,&base);
   }
   for(w=0;w<10;w++) {
    VECTOR hill={-110.0f+(float)w*24.0f,-2.0f,-870.0f,1};
    q=drawCube(q,viewScreen,hill,wr,18.0f,24.0f,30.0f,78,105,105,tmp,xyz,rgbaq,&prim,&base);
   }
 }
 /* Sparse wind-driven dust / rain / snow particles near the player. */
 { int k; VECTOR pr={0,0,0,1}; for(k=0;k<12;k++) {
    float phase=dustPhase+(float)k*1.73f;
    VECTOR particle={px+sinf(phase*0.7f)* (8.0f+windStrength*8.0f),-8.0f+(float)(k%4)*2.2f,pz-10.0f-(float)k*4.5f,1};
    if(weatherType==3) { particle[1]=8.0f+(float)(k%5)*3.0f; particle[0]+=sinf(phase)*2.0f; q=drawCube(q,viewScreen,particle,pr,0.07f,1.1f,0.07f,165,185,205,tmp,xyz,rgbaq,&prim,&base); }
    else if(weatherType==4) { particle[1]=-3.0f+(float)(k%6)*3.0f; q=drawCube(q,viewScreen,particle,pr,0.18f,0.18f,0.18f,225,230,238,tmp,xyz,rgbaq,&prim,&base); }
    else if(weatherType==2 || windStrength>0.9f) { q=drawCube(q,viewScreen,particle,pr,0.16f,0.08f,0.16f,150,132,105,tmp,xyz,rgbaq,&prim,&base); }
   } }
 /* road slab and city blocks form a small 3D street */
 { VECTOR p={0,-13,-205,1},r={0,0,0,1};
   q=drawCube(q,viewScreen,p,r,44,1,925,58,61,67,tmp,xyz,rgbaq,&prim,&base);
   /* Subtle asphalt repair strips and aggregate bands break up the flat slab. */
   for(i=0;i<40;i++) {
     VECTOR patch={-27.0f+(float)((i*17)%55),-11.78f,-14.0f-(float)i*23.0f,1};
     q=drawCube(q,viewScreen,patch,r,1.2f+(float)(i%3)*0.45f,0.08f,3.0f+(float)(i%2)*1.5f,
                (i&1)?66:51,(i&1)?69:55,(i&1)?75:61,tmp,xyz,rgbaq,&prim,&base);
   }
   p[0]=-49; p[1]=-11; q=drawCube(q,viewScreen,p,r,4,1,925,105,105,110,tmp,xyz,rgbaq,&prim,&base);
   p[0]=49; q=drawCube(q,viewScreen,p,r,4,1,925,105,105,110,tmp,xyz,rgbaq,&prim,&base);
 }
 /* Non-grid city connections: angled cross streets and a curved bypass. */
 { int s; for(s=0;s<4;s++) {
    VECTOR cross={0.0f,-12.0f,-125.0f-(float)s*205.0f,1};
    VECTOR cr={0.0f,(s&1)?0.16f:-0.11f,0.0f,1};
    q=drawCube(q,viewScreen,cross,cr,96.0f,0.85f,13.0f,49,53,59,tmp,xyz,rgbaq,&prim,&base);
    cross[1]=-11.35f; cross[0]=-47.0f;
    q=drawCube(q,viewScreen,cross,cr,3.0f,0.55f,13.0f,128,126,116,tmp,xyz,rgbaq,&prim,&base);
    cross[0]=47.0f;
    q=drawCube(q,viewScreen,cross,cr,3.0f,0.55f,13.0f,128,126,116,tmp,xyz,rgbaq,&prim,&base);
  }
  /* A staggered diagonal avenue breaks the parallel street pattern. */
  for(s=0;s<5;s++) {
    VECTOR bend={-34.0f+(float)s*16.0f,-12.0f,-80.0f-(float)s*155.0f,1};
    VECTOR br={0.0f,0.28f,0.0f,1};
    q=drawCube(q,viewScreen,bend,br,42.0f,0.75f,8.0f,55,58,64,tmp,xyz,rgbaq,&prim,&base);
  }
 }
 /* raised sidewalks and curbs make the road read as a real street */
 { VECTOR p={-49.0f,-11.6f,-455.0f,1},r={0,0,0,1};
   q=drawCube(q,viewScreen,p,r,3.0f,0.7f,465.0f,125,125,118,tmp,xyz,rgbaq,&prim,&base);
   p[0]=49.0f; q=drawCube(q,viewScreen,p,r,3.0f,0.7f,465.0f,125,125,118,tmp,xyz,rgbaq,&prim,&base);
 }
 /* broken white centre line and road-edge paint */
 for(i=0;i<23;i++) {
   VECTOR p={0,-11.5f,-8.0f-i*19.0f,1},r={0,0,0,1};
   q=drawCube(q,viewScreen,p,r,0.45f,0.18f,6.0f,235,225,190,tmp,xyz,rgbaq,&prim,&base);
   p[0]=-44.0f; q=drawCube(q,viewScreen,p,r,0.35f,0.18f,7.0f,230,190,45,tmp,xyz,rgbaq,&prim,&base);
   p[0]=44.0f; q=drawCube(q,viewScreen,p,r,0.35f,0.18f,7.0f,230,190,45,tmp,xyz,rgbaq,&prim,&base);
 }
 for(i=0;i<20;i++) {
  VECTOR p1={-68,0,-25.0f-i*42.0f,1},p2={68,0,-25.0f-i*42.0f,1},r={0,0,0,1};
  q=drawCube(q,viewScreen,p1,r,12,18+(i%3)*4,12,120,78,62,tmp,xyz,rgbaq,&prim,&base);
  q=drawCube(q,viewScreen,p2,r,12,22+(i%2)*5,12,75,91,112,tmp,xyz,rgbaq,&prim,&base);
  /* bright window panels on the street-facing walls */
  p1[0]=-56.0f; p1[1]=4.0f; p1[2]=-25.0f-i*42.0f;
  q=drawCube(q,viewScreen,p1,r,0.35f,3.0f,3.5f,245,185,95,tmp,xyz,rgbaq,&prim,&base);
  p1[2]-=11.0f; q=drawCube(q,viewScreen,p1,r,0.35f,3.0f,3.5f,245,185,95,tmp,xyz,rgbaq,&prim,&base);
  p2[0]=56.0f; p2[1]=5.0f; p2[2]=-25.0f-i*42.0f;
  q=drawCube(q,viewScreen,p2,r,0.35f,3.5f,3.5f,120,190,225,tmp,xyz,rgbaq,&prim,&base);
  p2[2]-=11.0f; q=drawCube(q,viewScreen,p2,r,0.35f,3.5f,3.5f,120,190,225,tmp,xyz,rgbaq,&prim,&base);
  /* Shop signage, awnings and vertical facade trims give each block a distinct street identity. */
  { VECTOR sign={-56.0f,-0.5f,-25.0f-i*42.0f,1};
    q=drawCube(q,viewScreen,sign,r,0.45f,1.25f,5.2f,115,32,42,tmp,xyz,rgbaq,&prim,&base);
    sign[0]=56.0f;
    q=drawCube(q,viewScreen,sign,r,0.45f,1.25f,5.2f,32,72,105,tmp,xyz,rgbaq,&prim,&base);
    sign[1]=-3.2f; sign[2]-=1.2f;
    q=drawCube(q,viewScreen,sign,r,0.5f,0.35f,6.2f,190,145,75,tmp,xyz,rgbaq,&prim,&base);
    sign[0]=-56.0f;
    q=drawCube(q,viewScreen,sign,r,0.5f,0.35f,6.2f,190,145,75,tmp,xyz,rgbaq,&prim,&base);
  }
  /* Second storey windows and stone lintels add depth to the facades. */
  p1[1]=11.0f; p1[2]=-25.0f-i*42.0f;
  q=drawCube(q,viewScreen,p1,r,0.38f,2.5f,2.8f,95,165,195,tmp,xyz,rgbaq,&prim,&base);
  p1[2]-=12.0f;
  q=drawCube(q,viewScreen,p1,r,0.38f,2.5f,2.8f,95,165,195,tmp,xyz,rgbaq,&prim,&base);
  p2[1]=11.0f; p2[2]=-25.0f-i*42.0f;
  q=drawCube(q,viewScreen,p2,r,0.38f,2.5f,2.8f,175,195,205,tmp,xyz,rgbaq,&prim,&base);
  p2[2]-=12.0f;
  q=drawCube(q,viewScreen,p2,r,0.38f,2.5f,2.8f,175,195,205,tmp,xyz,rgbaq,&prim,&base);
  /* Window frames, stone sills and vertical mullions add facade depth. */
  { int side,level; for(side=-1;side<=1;side+=2) for(level=0;level<2;level++) {
     VECTOR wf={side*55.5f,level?8.2f:1.0f,-25.0f-i*42.0f,1};
     q=drawCube(q,viewScreen,wf,r,0.48f,0.20f,4.0f,185,178,160,tmp,xyz,rgbaq,&prim,&base);
     wf[1]=level?11.0f:4.0f;
     q=drawCube(q,viewScreen,wf,r,0.48f,2.7f,0.16f,55,65,72,tmp,xyz,rgbaq,&prim,&base);
  }}
  /* Facade cornices, balcony slabs and railings for a less boxy skyline. */
  { VECTOR trim={-68.0f,19.0f,-25.0f-i*42.0f,1};
    q=drawCube(q,viewScreen,trim,r,12.8f,0.55f,12.8f,145,132,115,tmp,xyz,rgbaq,&prim,&base);
    trim[0]=68.0f; q=drawCube(q,viewScreen,trim,r,12.8f,0.55f,12.8f,112,126,139,tmp,xyz,rgbaq,&prim,&base);
    trim[1]=3.0f; trim[2]=-25.0f-i*42.0f-5.0f;
    q=drawCube(q,viewScreen,trim,r,3.0f,0.28f,0.45f,75,82,88,tmp,xyz,rgbaq,&prim,&base);
    trim[1]=5.0f; q=drawCube(q,viewScreen,trim,r,3.0f,0.22f,0.22f,185,190,185,tmp,xyz,rgbaq,&prim,&base);
    trim[0]=-68.0f; q=drawCube(q,viewScreen,trim,r,3.0f,0.28f,0.45f,75,82,88,tmp,xyz,rgbaq,&prim,&base);
    trim[1]=5.0f; q=drawCube(q,viewScreen,trim,r,3.0f,0.22f,0.22f,185,190,185,tmp,xyz,rgbaq,&prim,&base);
  }
 }
 /* shopfront doors and simple street trees add scale and depth */
 for(i=0;i<20;i++) {
  VECTOR d={-55.2f,-6.0f,-25.0f-i*42.0f,1},r={0,0,0,1};
  q=drawCube(q,viewScreen,d,r,0.4f,4.0f,2.5f,65,43,32,tmp,xyz,rgbaq,&prim,&base);
  d[0]=55.2f; q=drawCube(q,viewScreen,d,r,0.4f,4.0f,2.5f,55,58,65,tmp,xyz,rgbaq,&prim,&base);
  /* Storefront metal frames and door handles. */
  d[1]=-5.8f; d[2]=-25.0f-i*42.0f+1.35f; d[0]=-54.75f;
  q=drawCube(q,viewScreen,d,r,0.18f,3.8f,0.14f,175,165,145,tmp,xyz,rgbaq,&prim,&base);
  d[0]=54.75f;
  q=drawCube(q,viewScreen,d,r,0.18f,3.8f,0.14f,175,165,145,tmp,xyz,rgbaq,&prim,&base);
  d[0]=-54.65f; d[1]=-6.0f; d[2]=-25.0f-i*42.0f+2.15f;
  q=drawCube(q,viewScreen,d,r,0.12f,0.45f,0.12f,215,195,125,tmp,xyz,rgbaq,&prim,&base);
  d[0]=54.65f;
  q=drawCube(q,viewScreen,d,r,0.12f,0.45f,0.12f,215,195,125,tmp,xyz,rgbaq,&prim,&base);
 }
 for(i=0;i<18;i++) {
  VECTOR t={-58.0f,-6.5f,-38.0f-i*48.0f,1},r={0,0,0,1};
  q=drawCube(q,viewScreen,t,r,1.0f,5.5f,1.0f,95,65,42,tmp,xyz,rgbaq,&prim,&base);
  t[1]=1.0f; q=drawCube(q,viewScreen,t,r,4.0f,4.0f,4.0f,42,105,62,tmp,xyz,rgbaq,&prim,&base);
  t[0]=58.0f; t[1]=-6.5f; q=drawCube(q,viewScreen,t,r,1.0f,5.5f,1.0f,95,65,42,tmp,xyz,rgbaq,&prim,&base);
  t[1]=1.0f; q=drawCube(q,viewScreen,t,r,4.0f,4.0f,4.0f,42,105,62,tmp,xyz,rgbaq,&prim,&base);
 }
 /* street lamps: poles and warm lamp heads */
 for(i=0;i<20;i++) {
  VECTOR lp={-49.0f,-1.0f,-18.0f-i*48.0f,1},lr={0,0,0,1};
  q=drawCube(q,viewScreen,lp,lr,0.65f,10.0f,0.65f,35,38,43,tmp,xyz,rgbaq,&prim,&base);
  lp[0]=49.0f; q=drawCube(q,viewScreen,lp,lr,0.65f,10.0f,0.65f,35,38,43,tmp,xyz,rgbaq,&prim,&base);
  lp[1]=10.0f; q=drawCube(q,viewScreen,lp,lr,2.0f,0.8f,1.0f,255,205,115,tmp,xyz,rgbaq,&prim,&base);
  lp[0]=-49.0f; q=drawCube(q,viewScreen,lp,lr,2.0f,0.8f,1.0f,255,205,115,tmp,xyz,rgbaq,&prim,&base);
 }
 /* Sophia's house: facade, roof, door, windows and front path. */
 { VECTOR h={-47.0f,-1.0f,13.0f,1},hr={0,0,0,1};
   q=drawCube(q,viewScreen,h,hr,13.0f,11.0f,12.0f,190,174,150,tmp,xyz,rgbaq,&prim,&base);
   h[1]=11.5f; q=drawCube(q,viewScreen,h,hr,14.0f,1.8f,13.0f,105,35,42,tmp,xyz,rgbaq,&prim,&base);
   h[0]=-47.0f; h[1]=-5.5f; h[2]=25.3f; q=drawCube(q,viewScreen,h,hr,2.0f,4.5f,0.35f,75,45,35,tmp,xyz,rgbaq,&prim,&base);
   h[0]=-53.0f; h[1]=1.0f; h[2]=25.4f; q=drawCube(q,viewScreen,h,hr,2.2f,2.0f,0.35f,75,155,195,tmp,xyz,rgbaq,&prim,&base);
   h[0]=-41.0f; q=drawCube(q,viewScreen,h,hr,2.2f,2.0f,0.35f,75,155,195,tmp,xyz,rgbaq,&prim,&base);
   h[0]=-47.0f; h[1]=-9.0f; h[2]=30.0f; q=drawCube(q,viewScreen,h,hr,7.0f,0.25f,7.0f,135,130,120,tmp,xyz,rgbaq,&prim,&base);
 }
 /* Mission beacon advances through the ten-stage opening story. */
 if(mission<15) {
  VECTOR wp={0,8,missionTargets[mission],1},wr={0,0,0,1};
  q=drawCube(q,viewScreen,wp,wr,1.2f,15.0f,1.2f,245,185,35,tmp,xyz,rgbaq,&prim,&base);
  wp[1]=24.0f; q=drawCube(q,viewScreen,wp,wr,4.0f,1.0f,4.0f,255,220,75,tmp,xyz,rgbaq,&prim,&base);
 }
 /* Moving traffic vehicles and walking pedestrians are engine entities. */
 for(i=0;i<5;i++) {
  NX_Entity *v=&nxScene.entities[nxTraffic[i]];
  VECTOR vp={v->transform.position.x,-5.0f,v->transform.position.z,1},vr={0,0,0,1};
  vp[1]=-10.1f;
  q=drawCube(q,viewScreen,vp,vr,4.0f,1.8f,7.0f,i==4?225:((i&1)?175:35),i==4?230:((i&1)?35:95),i==4?235:((i&1)?35:165),tmp,xyz,rgbaq,&prim,&base);
  if(i==4) {
   VECTOR stripe={v->transform.position.x,-9.0f,v->transform.position.z,1};
   q=drawCube(q,viewScreen,stripe,vr,4.08f,0.45f,2.0f,25,55,95,tmp,xyz,rgbaq,&prim,&base);
   stripe[1]=-6.65f; stripe[2]=v->transform.position.z;
   q=drawCube(q,viewScreen,stripe,vr,1.8f,0.32f,0.65f,35,40,48,tmp,xyz,rgbaq,&prim,&base);
   stripe[0]=v->transform.position.x-1.0f;
   q=drawCube(q,viewScreen,stripe,vr,0.55f,0.42f,0.38f,((int)(nxClock.elapsed*7.0f)&1)?245:35,35,245,tmp,xyz,rgbaq,&prim,&base);
   stripe[0]=v->transform.position.x+1.0f;
   q=drawCube(q,viewScreen,stripe,vr,0.55f,0.42f,0.38f,35,90,245,((int)(nxClock.elapsed*7.0f)&1)?245:35,tmp,xyz,rgbaq,&prim,&base);
  }
  vp[1]=-7.9f;
  q=drawCube(q,viewScreen,vp,vr,3.0f,1.0f,3.8f,85,145,175,tmp,xyz,rgbaq,&prim,&base);
  /* Roof, front/rear glass, bumpers and grille give each vehicle a silhouette. */
  { VECTOR detail={v->transform.position.x,-6.95f,v->transform.position.z,1};
    q=drawCube(q,viewScreen,detail,vr,2.5f,0.35f,2.8f,45,68,82,tmp,xyz,rgbaq,&prim,&base);
    detail[1]=-8.0f; detail[2]=v->transform.position.z-3.65f;
    q=drawCube(q,viewScreen,detail,vr,2.45f,0.7f,0.25f,115,175,195,tmp,xyz,rgbaq,&prim,&base);
    detail[2]=v->transform.position.z+3.65f;
    q=drawCube(q,viewScreen,detail,vr,2.45f,0.7f,0.25f,55,85,105,tmp,xyz,rgbaq,&prim,&base);
    detail[1]=-10.4f; detail[2]=v->transform.position.z-7.15f;
    q=drawCube(q,viewScreen,detail,vr,3.6f,0.45f,0.35f,48,50,55,tmp,xyz,rgbaq,&prim,&base);
    detail[2]=v->transform.position.z+7.15f;
    q=drawCube(q,viewScreen,detail,vr,3.6f,0.45f,0.35f,48,50,55,tmp,xyz,rgbaq,&prim,&base);
    detail[1]=-9.9f; detail[2]=v->transform.position.z-7.28f;
    q=drawCube(q,viewScreen,detail,vr,1.1f,0.3f,0.18f,45,45,48,tmp,xyz,rgbaq,&prim,&base);
  }
  /* Four visible dark wheels and front headlights on each traffic car. */
  { int wx,wz; for(wx=-1;wx<=1;wx+=2) for(wz=-1;wz<=1;wz+=2) {
    VECTOR wheel={v->transform.position.x+wx*3.2f,-11.35f,v->transform.position.z+wz*4.6f,1};
    q=drawCube(q,viewScreen,wheel,vr,0.8f,1.5f,1.1f,22,24,28,tmp,xyz,rgbaq,&prim,&base);
   }
   /* Extra side glass, mirrors, door seams and alloy wheel centers. */
   { int side; for(side=-1;side<=1;side+=2) {
      VECTOR sideGlass={v->transform.position.x+side*3.02f,-8.15f,v->transform.position.z,1};
      q=drawCube(q,viewScreen,sideGlass,vr,0.18f,0.82f,3.0f,75,125,150,tmp,xyz,rgbaq,&prim,&base);
      sideGlass[1]=-9.45f; sideGlass[2]=v->transform.position.z-1.0f;
      q=drawCube(q,viewScreen,sideGlass,vr,0.16f,0.12f,2.8f,95,100,108,tmp,xyz,rgbaq,&prim,&base);
      sideGlass[0]=v->transform.position.x+side*3.75f; sideGlass[1]=-7.55f; sideGlass[2]=v->transform.position.z-2.0f;
      q=drawCube(q,viewScreen,sideGlass,vr,0.65f,0.25f,0.55f,35,40,48,tmp,xyz,rgbaq,&prim,&base);
      sideGlass[0]=v->transform.position.x+side*3.2f; sideGlass[1]=-11.0f;
      sideGlass[2]=v->transform.position.z-4.4f;
      q=drawCube(q,viewScreen,sideGlass,vr,0.18f,0.12f,0.18f,155,160,165,tmp,xyz,rgbaq,&prim,&base);
      sideGlass[2]=v->transform.position.z+4.4f;
      q=drawCube(q,viewScreen,sideGlass,vr,0.18f,0.12f,0.18f,155,160,165,tmp,xyz,rgbaq,&prim,&base);
   }}
   { int wx,wz; for(wx=-1;wx<=1;wx+=2) for(wz=-1;wz<=1;wz+=2) {
      VECTOR hub={v->transform.position.x+wx*3.72f,-11.35f,v->transform.position.z+wz*4.6f,1};
      q=drawCube(q,viewScreen,hub,vr,0.28f,0.65f,0.42f,145,150,158,tmp,xyz,rgbaq,&prim,&base);
   }}   VECTOR lamp={v->transform.position.x-2.3f,-9.7f,v->transform.position.z-6.8f,1};
   q=drawCube(q,viewScreen,lamp,vr,0.45f,0.45f,0.3f,255,225,145,tmp,xyz,rgbaq,&prim,&base);
   lamp[0]=v->transform.position.x+2.3f;
   q=drawCube(q,viewScreen,lamp,vr,0.45f,0.45f,0.3f,255,225,145,tmp,xyz,rgbaq,&prim,&base);
  }
  {
   NX_Entity *n=&nxScene.entities[nxPedestrians[i]];
   VECTOR np={n->transform.position.x,-5.2f,n->transform.position.z,1};
   { int shirtR[4]={34,145,48,175},shirtG[4]={36,65,125,105},shirtB[4]={43,75,75,55};
     /* NPC 0 is Alex: dark jacket; the remaining pedestrians keep varied outfits. */
     q=drawCube(q,viewScreen,np,vr,1.5f,3.0f,1.5f,shirtR[i],shirtG[i],shirtB[i],tmp,xyz,rgbaq,&prim,&base);
     /* Layered collars, shoulder panels, shirt fronts and waist details. */
     { VECTOR cloth; memcpy(cloth,np,sizeof(VECTOR));
       cloth[1]=-3.65f; cloth[2]=n->transform.position.z+1.35f;
       q=drawCube(q,viewScreen,cloth,vr,0.82f,0.42f,0.18f,205,195,175,tmp,xyz,rgbaq,&prim,&base);
       cloth[1]=-4.25f; cloth[2]=n->transform.position.z+1.55f;
       q=drawCube(q,viewScreen,cloth,vr,0.12f,1.15f,0.10f,105,110,118,tmp,xyz,rgbaq,&prim,&base);
       cloth[1]=-6.55f; cloth[2]=n->transform.position.z+1.25f;
       q=drawCube(q,viewScreen,cloth,vr,1.35f,0.18f,0.12f,35,35,40,tmp,xyz,rgbaq,&prim,&base);
       cloth[1]=-2.55f; cloth[0]=n->transform.position.x-1.18f;
       q=drawCube(q,viewScreen,cloth,vr,0.28f,0.65f,0.18f,shirtR[i]/2,shirtG[i]/2,shirtB[i]/2,tmp,xyz,rgbaq,&prim,&base);
       cloth[0]=n->transform.position.x+1.18f;
       q=drawCube(q,viewScreen,cloth,vr,0.28f,0.65f,0.18f,shirtR[i]/2,shirtG[i]/2,shirtB[i]/2,tmp,xyz,rgbaq,&prim,&base);
     }
     if(i==0) {
       VECTOR lapel; memcpy(lapel,np,sizeof(VECTOR)); lapel[1]=-4.4f; lapel[2]=n->transform.position.z+1.45f;
       q=drawCube(q,viewScreen,lapel,vr,0.24f,1.4f,0.12f,105,105,110,tmp,xyz,rgbaq,&prim,&base);
       lapel[0]+=0.55f; q=drawCube(q,viewScreen,lapel,vr,0.24f,1.4f,0.12f,105,105,110,tmp,xyz,rgbaq,&prim,&base);
     } }
   np[1]=-1.2f;
   q=drawCube(q,viewScreen,np,vr,1.2f,1.2f,1.2f,(i&1)?190:220,(i&1)?135:175,(i&1)?110:145,tmp,xyz,rgbaq,&prim,&base);
   /* hair cap and face details are placed on the camera-facing (+Z) side */
   np[1]=-0.35f; np[2]=n->transform.position.z+0.72f;
   q=drawCube(q,viewScreen,np,vr,0.9f,0.35f,0.35f,(i&1)?45:65,(i&1)?30:42,(i&1)?28:35,tmp,xyz,rgbaq,&prim,&base);
   /* Long-haired female pedestrians get two independently swaying hair locks. */
   if((i&1)==0) { VECTOR nh; memcpy(nh,np,sizeof(VECTOR)); float sway=sinf(nxClock.elapsed*4.4f+(float)i)*windStrength*0.45f; nh[1]=-2.7f; nh[2]=n->transform.position.z-0.55f; nh[0]=n->transform.position.x-0.62f+sway; q=drawCube(q,viewScreen,nh,vr,0.34f,2.2f,0.38f,48,30,27,tmp,xyz,rgbaq,&prim,&base); nh[0]=n->transform.position.x+0.62f-sway; q=drawCube(q,viewScreen,nh,vr,0.34f,2.2f,0.38f,48,30,27,tmp,xyz,rgbaq,&prim,&base); }
   np[0]-=2.1f; np[1]=-5.2f; q=drawCube(q,viewScreen,np,vr,0.65f,2.5f,0.7f,45,65,115,tmp,xyz,rgbaq,&prim,&base);
   np[0]+=4.2f; q=drawCube(q,viewScreen,np,vr,0.65f,2.5f,0.7f,45,65,115,tmp,xyz,rgbaq,&prim,&base);
   np[0]-=2.1f; np[1]=-9.7f; np[2]-=0.4f;
   q=drawCube(q,viewScreen,np,vr,0.7f,2.1f,0.8f,35,40,55,tmp,xyz,rgbaq,&prim,&base);
   np[0]+=1.5f; q=drawCube(q,viewScreen,np,vr,0.7f,2.1f,0.8f,35,40,55,tmp,xyz,rgbaq,&prim,&base);
   np[0]=n->transform.position.x; np[1]=-0.1f; np[2]=n->transform.position.z;
   q=drawCube(q,viewScreen,np,vr,1.25f,0.45f,1.25f,35,28,24,tmp,xyz,rgbaq,&prim,&base);
   np[1]=-1.2f; np[2]=n->transform.position.z+1.30f; np[0]-=0.35f;
   q=drawCube(q,viewScreen,np,vr,0.13f,0.14f,0.10f,18,18,18,tmp,xyz,rgbaq,&prim,&base);
   np[0]+=0.7f; q=drawCube(q,viewScreen,np,vr,0.13f,0.14f,0.10f,18,18,18,tmp,xyz,rgbaq,&prim,&base);
   np[0]-=0.35f; np[1]=-1.65f;
   q=drawCube(q,viewScreen,np,vr,0.28f,0.10f,0.10f,105,55,48,tmp,xyz,rgbaq,&prim,&base);
   /* More readable low-poly faces: ears, brows, nose bridge and hairline. */
   { VECTOR f; memcpy(f,np,sizeof(VECTOR)); f[1]=-1.15f; f[2]=n->transform.position.z+1.42f;
     f[0]=n->transform.position.x-0.72f;
     q=drawCube(q,viewScreen,f,vr,0.18f,0.42f,0.18f,190,135,112,tmp,xyz,rgbaq,&prim,&base);
     f[0]=n->transform.position.x+0.72f;
     q=drawCube(q,viewScreen,f,vr,0.18f,0.42f,0.18f,190,135,112,tmp,xyz,rgbaq,&prim,&base);
     f[0]=n->transform.position.x; f[1]=-1.28f; f[2]=n->transform.position.z+1.48f;
     q=drawCube(q,viewScreen,f,vr,0.12f,0.28f,0.12f,185,125,105,tmp,xyz,rgbaq,&prim,&base);
     f[1]=-0.78f; f[2]=n->transform.position.z+1.38f;
     f[0]-=0.42f; q=drawCube(q,viewScreen,f,vr,0.30f,0.10f,0.10f,42,30,28,tmp,xyz,rgbaq,&prim,&base);
     f[0]=n->transform.position.x+0.42f;
     q=drawCube(q,viewScreen,f,vr,0.30f,0.10f,0.10f,42,30,28,tmp,xyz,rgbaq,&prim,&base);
   }
  }
 }
 /* Soft contact shadow under Sophia makes her feet read against the asphalt. */
 if(!inCar) {
  VECTOR sh={px,-11.82f,pz,1},sr={0,0,0,1};
  q=drawCube(q,viewScreen,sh,sr,2.7f,0.10f,1.8f,28,30,32,tmp,xyz,rgbaq,&prim,&base);
  sh[1]=-11.78f; sh[2]=pz-0.9f;
  q=drawCube(q,viewScreen,sh,sr,1.7f,0.08f,0.7f,35,36,38,tmp,xyz,rgbaq,&prim,&base);
 }
 /* Render the drivable car when occupied; otherwise render the player. */
 if(inCar) {
  NX_Entity *car=&nxScene.entities[playerVehicle];
  VECTOR cp={car->transform.position.x,-5.0f,car->transform.position.z,1},cr={0,carHeading,0,1};
  cp[1]=-10.1f;
  q=drawCube(q,viewScreen,cp,cr,4.8f,1.7f,8.0f,155,28,42,tmp,xyz,rgbaq,&prim,&base);
  cp[1]=-7.9f; q=drawCube(q,viewScreen,cp,cr,3.5f,1.25f,3.8f,55,115,145,tmp,xyz,rgbaq,&prim,&base);
  { VECTOR d={car->transform.position.x,-6.6f,car->transform.position.z,1};
    q=drawCube(q,viewScreen,d,cr,2.8f,0.35f,3.1f,45,65,78,tmp,xyz,rgbaq,&prim,&base);
    d[1]=-8.0f; d[2]=car->transform.position.z-4.0f;
    q=drawCube(q,viewScreen,d,cr,2.8f,0.7f,0.25f,115,175,195,tmp,xyz,rgbaq,&prim,&base);
    d[2]=car->transform.position.z+4.0f;
    q=drawCube(q,viewScreen,d,cr,2.8f,0.7f,0.25f,55,85,105,tmp,xyz,rgbaq,&prim,&base);
    d[1]=-10.5f; d[2]=car->transform.position.z-8.0f;
    q=drawCube(q,viewScreen,d,cr,4.2f,0.45f,0.35f,40,42,48,tmp,xyz,rgbaq,&prim,&base);
    /* Twin headlamps respond to the day-night cycle. */
    d[1]=-9.55f; d[2]=car->transform.position.z-8.18f; d[0]=car->transform.position.x-2.75f;
    q=drawCube(q,viewScreen,d,cr,0.75f,0.38f,0.22f,255,(int)(190+65*(1.0f-daylight)),(int)(105+130*(1.0f-daylight)),tmp,xyz,rgbaq,&prim,&base);
    d[0]=car->transform.position.x+2.75f;
    q=drawCube(q,viewScreen,d,cr,0.75f,0.38f,0.22f,255,(int)(190+65*(1.0f-daylight)),(int)(105+130*(1.0f-daylight)),tmp,xyz,rgbaq,&prim,&base);
  }
  /* Premium body details: side windows, mirrors, door handles, grille and wheel hubs. */
  { int side; for(side=-1;side<=1;side+=2) {
     VECTOR sd={car->transform.position.x+side*3.72f,-8.05f,car->transform.position.z-0.15f,1};
     q=drawCube(q,viewScreen,sd,cr,0.18f,0.92f,3.35f,65,115,145,tmp,xyz,rgbaq,&prim,&base);
     sd[1]=-9.25f; sd[2]=car->transform.position.z-0.7f;
     q=drawCube(q,viewScreen,sd,cr,0.18f,0.12f,2.8f,115,120,125,tmp,xyz,rgbaq,&prim,&base);
     sd[0]=car->transform.position.x+side*4.15f; sd[1]=-7.35f; sd[2]=car->transform.position.z-2.2f;
     q=drawCube(q,viewScreen,sd,cr,0.75f,0.28f,0.65f,35,38,45,tmp,xyz,rgbaq,&prim,&base);
     sd[0]=car->transform.position.x+side*4.88f; sd[1]=-9.65f; sd[2]=car->transform.position.z-8.1f;
     q=drawCube(q,viewScreen,sd,cr,0.28f,0.55f,0.18f,255,220,155,tmp,xyz,rgbaq,&prim,&base);
     sd[2]=car->transform.position.z+8.1f;
     q=drawCube(q,viewScreen,sd,cr,0.28f,0.55f,0.18f,210,45,48,tmp,xyz,rgbaq,&prim,&base);
  }}
  { int wx,wz; for(wx=-1;wx<=1;wx+=2) for(wz=-1;wz<=1;wz+=2) {
     VECTOR wh={car->transform.position.x+wx*4.1f,-11.35f,car->transform.position.z+wz*5.2f,1};
     q=drawCube(q,viewScreen,wh,cr,0.8f,1.8f,1.2f,18,20,24,tmp,xyz,rgbaq,&prim,&base);
     wh[0]=car->transform.position.x+wx*4.65f; wh[1]=-11.35f;
     q=drawCube(q,viewScreen,wh,cr,0.25f,0.75f,0.48f,155,160,170,tmp,xyz,rgbaq,&prim,&base);
  }}
 } else {
 /* Player avatar with layered clothes, head, hair and face details. */
 { VECTOR p={px,-2.35f,pz,1},r={0,heading,0,1};
  q=drawCube(q,viewScreen,p,r,3.2f,7.0f,2.4f,28,29,33,tmp,xyz,rgbaq,&prim,&base);
  p[1]=5.65f; q=drawCube(q,viewScreen,p,r,2.6f,2.6f,2.6f,218,174,143,tmp,xyz,rgbaq,&prim,&base);
  p[0]=px-4.0f; p[1]=-1.85f; p[2]=pz; q=drawCube(q,viewScreen,p,r,1.0f,4.5f,1.0f,190,35,42,tmp,xyz,rgbaq,&prim,&base);
  p[0]=px+4.0f; q=drawCube(q,viewScreen,p,r,1.0f,4.5f,1.0f,190,35,42,tmp,xyz,rgbaq,&prim,&base);
  /* Extra layered jacket, shoulders, forearms, trousers and shoes. */
  p[0]=px; p[1]=1.2f; p[2]=pz-0.15f;
  q=drawCube(q,viewScreen,p,r,3.35f,3.6f,2.55f,32,34,39,tmp,xyz,rgbaq,&prim,&base);
  /* Leather-jacket seams and front zipper highlight, kept deliberately low-poly. */
  p[0]=px-1.25f; p[1]=1.0f; p[2]=pz+1.25f;
  q=drawCube(q,viewScreen,p,r,0.12f,2.4f,0.10f,125,125,132,tmp,xyz,rgbaq,&prim,&base);
  p[0]=px+1.25f;
  q=drawCube(q,viewScreen,p,r,0.12f,2.4f,0.10f,125,125,132,tmp,xyz,rgbaq,&prim,&base);
  p[0]=px-4.0f; p[1]=-4.3f; p[2]=pz-0.15f;
  q=drawCube(q,viewScreen,p,r,0.85f,2.0f,0.9f,205,145,115,tmp,xyz,rgbaq,&prim,&base);
  p[0]=px+4.0f;
  q=drawCube(q,viewScreen,p,r,0.85f,2.0f,0.9f,205,145,115,tmp,xyz,rgbaq,&prim,&base);
  p[0]=px-1.5f; p[1]=-9.65f; p[2]=pz;
  q=drawCube(q,viewScreen,p,r,1.1f,3.0f,1.2f,28,32,42,tmp,xyz,rgbaq,&prim,&base);
  p[0]=px+1.5f;
  q=drawCube(q,viewScreen,p,r,1.1f,3.0f,1.2f,28,32,42,tmp,xyz,rgbaq,&prim,&base);
  p[0]=px-1.5f; p[1]=-12.15f; p[2]=pz-0.35f;
  q=drawCube(q,viewScreen,p,r,1.25f,0.75f,1.8f,18,20,25,tmp,xyz,rgbaq,&prim,&base);
  p[0]=px+1.5f;
  q=drawCube(q,viewScreen,p,r,1.25f,0.75f,1.8f,18,20,25,tmp,xyz,rgbaq,&prim,&base);
  p[0]=px; p[1]=7.65f; p[2]=pz; q=drawCube(q,viewScreen,p,r,2.65f,0.55f,2.65f,35,27,23,tmp,xyz,rgbaq,&prim,&base);
  /* Long layered hair falling behind the head to shoulder level. */
  p[1]=3.0f; p[2]=pz-1.8f; p[0]=px-1.9f+sinf(nxClock.elapsed*5.2f)*windStrength*0.55f; q=drawCube(q,viewScreen,p,r,0.8f,4.8f,0.8f,48,28,24,tmp,xyz,rgbaq,&prim,&base);
  p[0]=px+1.9f+sinf(nxClock.elapsed*5.2f+0.8f)*windStrength*0.55f; q=drawCube(q,viewScreen,p,r,0.8f,4.8f,0.8f,48,28,24,tmp,xyz,rgbaq,&prim,&base);
  p[1]=6.0f; p[2]=pz-1.7f; p[0]=px; q=drawCube(q,viewScreen,p,r,2.1f,1.0f,1.1f,48,28,24,tmp,xyz,rgbaq,&prim,&base);
  p[1]=5.65f; p[2]=pz+2.48f; p[0]=px-0.72f;
  q=drawCube(q,viewScreen,p,r,0.22f,0.24f,0.16f,18,18,18,tmp,xyz,rgbaq,&prim,&base);
  p[0]=px+0.72f; q=drawCube(q,viewScreen,p,r,0.22f,0.24f,0.16f,18,18,18,tmp,xyz,rgbaq,&prim,&base);
  p[0]=px; p[1]=5.0f; p[2]=pz+2.55f; q=drawCube(q,viewScreen,p,r,0.18f,0.18f,0.16f,145,85,70,tmp,xyz,rgbaq,&prim,&base);
  p[1]=4.55f; q=drawCube(q,viewScreen,p,r,0.38f,0.10f,0.12f,105,38,42,tmp,xyz,rgbaq,&prim,&base);
  /* Sophia's face framing: ears, eyebrows, nose and cheek highlights. */
  p[0]=px-1.48f; p[1]=5.0f; p[2]=pz+2.28f;
  q=drawCube(q,viewScreen,p,r,0.28f,0.52f,0.20f,195,145,119,tmp,xyz,rgbaq,&prim,&base);
  p[0]=px+1.48f;
  q=drawCube(q,viewScreen,p,r,0.28f,0.52f,0.20f,195,145,119,tmp,xyz,rgbaq,&prim,&base);
  p[0]=px; p[1]=5.18f; p[2]=pz+2.72f;
  q=drawCube(q,viewScreen,p,r,0.16f,0.38f,0.14f,190,132,112,tmp,xyz,rgbaq,&prim,&base);
  p[0]=px-0.70f; p[1]=6.0f; p[2]=pz+2.52f;
  q=drawCube(q,viewScreen,p,r,0.48f,0.13f,0.12f,45,29,27,tmp,xyz,rgbaq,&prim,&base);
  p[0]=px+0.70f;
  q=drawCube(q,viewScreen,p,r,0.48f,0.13f,0.12f,45,29,27,tmp,xyz,rgbaq,&prim,&base);
  p[0]=px-0.62f; p[1]=4.78f; p[2]=pz+2.52f;
  q=drawCube(q,viewScreen,p,r,0.28f,0.10f,0.08f,225,176,145,tmp,xyz,rgbaq,&prim,&base);
  p[0]=px+0.62f;
  q=drawCube(q,viewScreen,p,r,0.28f,0.10f,0.08f,225,176,145,tmp,xyz,rgbaq,&prim,&base);
 }
 }
 /* Full HUD: minimap bottom-left, health directly underneath. */
 q=draw_disable_tests(q,0,&zbuf);
 q=rect(q,12,12,250,34,10,15,24);
 q=rect(q,20,20,12,18,padConnected?45:220,padConnected?220:45,55);
 q=drawText(q,42,19,1,"RED CITY",235,235,240);
 { int hh=(int)(gameMinute/60.0f),mm=(int)gameMinute%60; char tm[5]; tm[0]='0'+hh/10; tm[1]='0'+hh%10; tm[2]='0'+mm/10; tm[3]='0'+mm%10; tm[4]=0; q=drawText(q,42,SH-172,1,tm,245,235,180); }
 q=drawText(q,145,19,1,padConnected?"PAD OK":"NO PAD",padConnected?90:245,padConnected?225:85,padConnected?130:85);
 { const char *w=weatherType==0?"CLEAR":weatherType==1?"CLOUDY":weatherType==2?"POLLUTED":weatherType==3?"RAIN":"SNOW"; q=drawText(q,SW-105,19,1,w,210,210,190); }
 q=rect(q,SW/2-118,12,236,34,10,15,24);
 if(mission<30) { char ms[24]="MISSION 01/15 - MARKER"; ms[8]=(char)('0'+(mission+1)/10); ms[9]=(char)('0'+(mission+1)%10); q=drawText(q,SW/2-108,19,1,ms,245,210,105); }
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
 { int mx=21+(int)((px+50.0f)*0.92f); int my=SH-140+(int)((-pz)*0.095f);
   if(mx<21)mx=21; if(mx>111)mx=111;
   if(my<SH-140)my=SH-140; if(my>SH-51)my=SH-51;
   q=rect(q,mx-3,my-3,7,7,255,55,55);
 }
 /* Health bar under the minimap. */
 q=rect(q,12,SH-34,112,22,8,12,18);
 q=drawText(q,18,SH-28,1,"HP",225,230,235);
 q=rect(q,39,SH-27,77,8,65,35,35);
 q=rect(q,39,SH-27,70,8,55,205,105);
 /* Controller help, vehicle speed and full-screen map overlay. */
 q=rect(q,SW-232,SH-60,220,48,8,12,18);
 q=drawText(q,SW-222,SH-51,1,inCar?"DRIVE - LEFT STICK":"MOVE - LEFT STICK",225,230,235);
 q=drawText(q,SW-222,SH-37,1,"TRIANGLE - ENTER CAR",150,190,220);
 q=drawText(q,SW-222,SH-23,1,"START MAP - SELECT PHONE",150,190,220);
 if(inCar) { q=rect(q,SW/2-55,SH-72,110,18,10,15,24); q=drawText(q,SW/2-45,SH-67,1,"SPEED",245,220,120); { int sp=(int)fabsf(carSpeed); if(sp>34)sp=34; q=rect(q,SW/2+2,SH-65,sp,6,210,45,50); } }
 if(phoneVisible) {
  /* Keep the entire phone inside a conservative TV-safe area (overscan-safe). */
  int phoneW=190,phoneH=350;
  int pxp=(SW-phoneW)/2,py=(SH-phoneH)/2;
  q=rect(q,pxp-4,py-4,phoneW+8,phoneH+8,3,4,7);
  q=rect(q,pxp,py,phoneW,phoneH,18,22,30);
  q=rect(q,pxp+7,py+7,phoneW-14,25,35,42,54);
  q=drawText(q,pxp+13,py+15,1,"RED CITY PHONE",235,225,205);
  q=drawText(q,pxp+12,py+43,1,"09:41  SIGNAL  BATTERY",130,220,165);
  if(phonePage==0) {
    q=drawText(q,pxp+50,py+76,2,"PHONE",245,245,245);
    { const char *apps[4]={"CONTACTS","MESSAGES","CITY MAP","SETTINGS"};
      int k; for(k=0;k<4;k++) {
        int ay=py+108+k*47;
        q=rect(q,pxp+12,ay,166,38,(k==phoneSelection)?110:43,(k==phoneSelection)?35:50,(k==phoneSelection)?48:62);
        q=rect(q,pxp+20,ay+7,22,22,(k==0)?70:(k==1)?65:(k==2)?55:90,(k==0)?145:(k==1)?105:(k==2)?130:95,(k==0)?205:(k==1)?155:(k==2)?95:125);
        q=drawText(q,pxp+50,ay+15,1,apps[k],235,235,235);
      }
    }
    q=drawText(q,pxp+14,py+321,1,"UP DOWN X OPEN O BACK",175,190,205);
  } else if(phonePage==1) {
    q=drawText(q,pxp+14,py+80,2,"CONTACTS",245,225,170);
    q=drawText(q,pxp+14,py+125,1,"JACK - GARAGE",225,230,235);
    q=drawText(q,pxp+14,py+160,1,"ALEX MORGAN",225,230,235);
    q=drawText(q,pxp+14,py+195,1,"MARCUS",225,230,235);
    q=drawText(q,pxp+14,py+230,1,"VICTOR CROSS",225,230,235);
    q=drawText(q,pxp+14,py+321,1,"O BACK TO PHONE",175,190,205);
  } else if(phonePage==2) {
    q=drawText(q,pxp+14,py+80,2,"MESSAGES",245,225,170);
    q=drawText(q,pxp+14,py+125,1,"JACK: CALL ME",225,230,235);
    q=drawText(q,pxp+14,py+160,1,"ALEX: WE NEED TO TALK",225,230,235);
    q=drawText(q,pxp+14,py+195,1,"NEW CITY MESSAGE",150,210,185);
    q=drawText(q,pxp+22,py+359,1,"O BACK TO PHONE",175,190,205);
  } else {
    q=drawText(q,pxp+14,py+80,2,"SETTINGS",245,225,170);
    q=drawText(q,pxp+14,py+125,1,"SOUND - GAME DEFAULT",225,230,235);
    q=drawText(q,pxp+14,py+160,1,"DISPLAY - AUTO",225,230,235);
    q=drawText(q,pxp+14,py+195,1,"LANGUAGE - PERSIAN",225,230,235);
    q=drawText(q,pxp+22,py+359,1,"O BACK TO PHONE",175,190,205);
  }
 } 
 if(mapVisible) {
  q=rect(q,SW/2-190,SH/2-145,380,290,5,9,16); q=rect(q,SW/2-180,SH/2-135,360,270,24,38,42);
  for(i=0;i<7;i++) { q=rect(q,SW/2-165+i*54,SH/2-125,5,250,105,112,105); q=rect(q,SW/2-170,SH/2-120+i*40,340,5,105,112,105); }
  /* Map is world-relative: the red marker is Sophia's exact current position. */
  for(i=0;i<20;i++) {
   int ry=SH/2-120+i*12;
   q=rect(q,SW/2-170,ry,340,2,75,88,82);
  }
  for(i=0;i<5;i++) {
   int rx=SW/2-160+i*80;
   q=rect(q,rx,SH/2-120,2,240,75,88,82);
  }
  q=rect(q,SW/2-4,SH/2-120,8,240,160,160,150);
  q=rect(q,SW/2-170,SH/2-4,340,8,160,160,150);
  { int hx=SW/2-170+(int)((-47.0f+50.0f)*3.4f);
    int hy=SH/2-120+(int)(13.0f*0.26f);
    int sx=SW/2-170+(int)((px+50.0f)*3.4f);
    int sy=SH/2-120+(int)((-pz)*0.26f);
    q=rect(q,hx-3,hy-3,7,7,235,190,90);
    if(sx<SW/2-168)sx=SW/2-168; if(sx>SW/2+168)sx=SW/2+168;
    if(sy<SH/2-118)sy=SH/2-118; if(sy>SH/2+118)sy=SH/2+118;
    q=rect(q,sx-5,sy-5,11,11,255,45,55);
  }
  q=drawText(q,SW/2-60,SH/2-128,2,"CITY MAP",245,225,165);
  q=drawText(q,SW/2-170,SH/2+119,1,"START - CLOSE MAP",220,225,235);
 }
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
 for(;;) { updateWorld(); updatePad(); render(); }
 return 0;
}
