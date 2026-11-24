#include <kernel.h>
#include <tamtypes.h>
#include <stdlib.h>
#include <malloc.h>
#include <string.h>
#include <math.h>
#include <dma.h>
#include <packet.h>
#include <graph.h>
#include <draw.h>
#include <gs_psm.h>
#include <libpad.h>

#define SCREEN_W 640
#define SCREEN_H 448
#define PACKET_WORDS 8192
#define CITY_BLOCKS 12

static framebuffer_t frame;
static zbuffer_t zbuf;
static unsigned char pad_buffer[256] __attribute__((aligned(64)));
static struct padButtonStatus pad;
static int pad_port=0, pad_open=0, pad_ready=0;
static int px=320, py=250, heading=0;
static int mission=0, phone_open=0, map_open=0, frame_no=0;
static int last_select=0, last_start=0, last_cross=0, last_circle=0;
static int last_up=0, last_down=0, phone_cursor=0;
static const char *mission_titles[5]={
 "MISSION 1 - RETURN TO CITY",
 "MISSION 2 - OLD GARAGE",
 "MISSION 3 - ALEX MORGAN",
 "MISSION 4 - BLACK WOLVES",
 "MISSION 5 - THE ROOFTOP"
};
static const char *mission_hints[5]={
 "Reach the city entrance",
 "Meet Jack at the old garage",
 "Meet Alex in the parking lot",
 "Listen to Alex's warning",
 "Go to the rooftop"
};

static qword_t *draw_box(qword_t *q,int x,int y,int w,int h,int r,int g,int b) {
 rect_t a;
 a.v0.x=x; a.v0.y=y; a.v0.z=1;
 a.v1.x=x+w; a.v1.y=y+h; a.v1.z=1;
 a.color.r=r; a.color.g=g; a.color.b=b; a.color.a=128; a.color.q=1.0f;
 return draw_rect_filled(q,0,&a);
}

/* Small built-in bitmap font; no external assets or runtime file loading. */
static const unsigned char font[38][7]={
 {14,17,19,21,25,17,14},{4,12,4,4,4,4,14},{14,17,1,2,4,8,31},
 {30,1,1,14,1,1,30},{2,6,10,18,31,2,2},{31,16,16,30,1,1,30},
 {6,8,16,30,17,17,14},{31,1,2,4,8,8,8},{14,17,17,14,17,17,14},
 {14,17,17,15,1,2,12},
 {14,17,17,31,17,17,17},{30,17,17,30,17,17,30},{14,17,16,16,16,17,14},
 {30,17,17,17,17,17,30},{31,16,16,30,16,16,31},{31,16,16,30,16,16,16},
 {14,17,16,23,17,17,15},{17,17,17,31,17,17,17},{14,4,4,4,4,4,14},
 {7,2,2,2,18,18,12},{16,16,16,16,16,16,31},{17,17,17,17,17,10,4},
 {17,17,17,17,17,17,14},{17,17,17,10,4,10,17},{17,17,17,10,4,4,4},
 {31,1,2,4,8,16,31},
 {0,0,0,31,0,0,0},{1,2,2,4,8,8,16},{0,0,0,0,0,0,0},
 {14,17,17,17,17,17,14},{30,17,17,30,16,16,16},
 {17,17,17,17,17,17,31},{30,17,17,30,17,17,30},
 {14,17,16,16,16,17,14},{31,16,16,30,16,16,31},
 {17,17,10,4,10,17,17}
};
static int glyph(char c) {
 if(c>='0'&&c<='9') return c-'0';
 if(c>='A'&&c<='Z') return 10+c-'A';
 if(c=='-') return 36;
 return 37;
}
static qword_t *draw_text(qword_t *q,int x,int y,int scale,const char *s,int r,int g,int b) {
 int i,row,col;
 for(i=0;s[i];i++) {
  int gi=glyph(s[i]);
  for(row=0;row<7;row++) for(col=0;col<5;col++)
   if(font[gi][row]&(1<<(4-col)))
    q=draw_box(q,x+i*6*scale+col*scale,y+row*scale,scale,scale,r,g,b);
 }
 return q;
}
static void graphics_init(void) {
 packet_t *p;
 qword_t *q;
 frame.width=SCREEN_W; frame.height=SCREEN_H; frame.mask=0; frame.psm=GS_PSM_32;
 frame.address=graph_vram_allocate(SCREEN_W,SCREEN_H,GS_PSM_32,GRAPH_ALIGN_PAGE);
 zbuf.enable=DRAW_DISABLE; zbuf.mask=0; zbuf.method=ZTEST_METHOD_GREATER_EQUAL; zbuf.zsm=GS_ZBUF_32;
 zbuf.address=0;
 graph_initialize(frame.address,SCREEN_W,SCREEN_H,GS_PSM_32,0,0);
 p=packet_init(64,PACKET_NORMAL);
 q=p->data;
 q=draw_setup_environment(q,0,&frame,&zbuf);
 q=draw_primitive_xyoffset(q,0,2048-SCREEN_W/2,2048-SCREEN_H/2);
 q=draw_finish(q);
 dma_channel_send_normal(DMA_CHANNEL_GIF,p->data,q-p->data,0,0);
 dma_wait_fast();
 packet_free(p);
}
static void controller_init(void) {
 int i;
 if(padInit(0)!=1) return;
 for(i=0;i<2;i++) if(padPortOpen(i,0,pad_buffer)) { pad_port=i; pad_open=1; break; }
 if(!pad_open) return;
 padSetMainMode(pad_port,0,PAD_MMODE_DUALSHOCK,PAD_MMODE_LOCK);
 for(i=0;i<90;i++) {
  int st=padGetState(pad_port,0);
  if(st==PAD_STATE_STABLE) { pad_ready=1; break; }
  graph_wait_vsync();
 }
}
static int pressed(unsigned short mask) { return (pad.btns & mask)==0; }
static void controller_update(void) {
 int up=0,down=0,left=0,right=0,sel,sta,cross,circle;
 if(!pad_open) return;
 if(padGetState(pad_port,0)!=PAD_STATE_STABLE) { pad_ready=0; return; }
 if(!pad_ready) { pad_ready=1; return; }
 if(!padRead(pad_port,0,&pad)) return;
 up=pressed(PAD_UP); down=pressed(PAD_DOWN); left=pressed(PAD_LEFT); right=pressed(PAD_RIGHT);
 if(pad.ljoy_v<92) up=1; if(pad.ljoy_v>164) down=1;
 if(pad.ljoy_h<92) left=1; if(pad.ljoy_h>164) right=1;
 if(!phone_open && !map_open) {
  if(left) { px-=2; heading=1; } if(right) { px+=2; heading=2; }
  if(up) { py-=2; heading=3; } if(down) { py+=2; heading=0; }
  if(px<36)px=36; if(px>604)px=604; if(py<80)py=80; if(py>410)py=410;
 }
 sel=pressed(PAD_SELECT); sta=pressed(PAD_START);
 cross=pressed(PAD_CROSS); circle=pressed(PAD_CIRCLE);
 if(sel&&!last_select) phone_open=!phone_open;
 if(sta&&!last_start) map_open=!map_open;
 if(phone_open) {
  if(up&&!last_up) phone_cursor=(phone_cursor+3)%4;
  if(down&&!last_down) phone_cursor=(phone_cursor+1)%4;
  if(circle&&!last_circle) phone_open=0;
 }
 if(cross&&!last_cross && !phone_open && !map_open) {
  /* Story progression is gated by explicit interaction at each mission marker. */
  mission=(mission+1)%5;
 }
 last_select=sel; last_start=sta; last_cross=cross; last_circle=circle;
 last_up=up; last_down=down;
}
static void render_frame(void) {
 packet_t *p=packet_init(PACKET_WORDS,PACKET_NORMAL);
 qword_t *q;
 int i;
 if(!p) { graph_wait_vsync(); return; }
 q=p->data;
 q=draw_disable_tests(q,0,&zbuf);
 q=draw_clear(q,0,2048-SCREEN_W/2,2048-SCREEN_H/2,SCREEN_W,SCREEN_H,39,62,57);
 q=p->data;
 q=draw_disable_tests(q,0,&zbuf);
 q=draw_clear(q,0,2048-SCREEN_W/2,2048-SCREEN_H/2,SCREEN_W,SCREEN_H,39,62,57);
 /* city layers */
 q=draw_box(q,0,190,SCREEN_W,92,47,49,51);
 q=draw_box(q,280,45,88,403,47,49,51);
 q=draw_box(q,0,232,SCREEN_W,4,205,185,110);
 q=draw_box(q,322,45,4,403,205,185,110);
 for(i=0;i<CITY_BLOCKS;i++) {
  int bx=(i%4)*158+10, by=(i/4)*122+20; if(by>160)by+=105;
  q=draw_box(q,bx,by,112,62,70+(i%3)*8,75+(i%4)*5,78+(i%2)*8);
  q=draw_box(q,bx+8,by+8,96,5,110,112,105);
  q=draw_box(q,bx+12,by+22,12,12,125,160,175);
  q=draw_box(q,bx+38,by+22,12,12,125,160,175);
  q=draw_box(q,bx+64,by+22,12,12,125,160,175);
 }
 q=draw_box(q,115,208,52,25,150,35,30); q=draw_box(q,125,200,30,12,165,175,180);
 q=draw_box(q,450,292,58,25,40,80,145); q=draw_box(q,460,284,34,12,165,175,180);
 q=draw_box(q,px-7,py-20,14,25,170,35,42);
 q=draw_box(q,px-6,py-30,12,12,205,170,145);
 q=draw_box(q,px-8,py+5,6,14,25,25,30); q=draw_box(q,px+2,py+5,6,14,25,25,30);
 /* HUD */
 q=draw_box(q,12,10,616,48,12,16,24);
 q=draw_text(q,24,19,2,"RED CITY - REBUILD",240,230,210);
 q=draw_text(q,24,35,1,mission_titles[mission],245,185,125);
 q=draw_box(q,12,402,616,34,12,16,24);
 q=draw_text(q,22,413,1,mission_hints[mission],225,225,225);
 q=draw_text(q,455,413,1,"SELECT PHONE START MAP",180,195,205);
 if(phone_open) {
  q=draw_box(q,190,70,260,300,10,15,24);
  q=draw_box(q,200,80,240,32,125,28,34);
  q=draw_text(q,215,91,1,"SOPHIA PHONE",255,245,230);
  { const char *items[4]={"CONTACTS","MESSAGES","CITY MAP","SETTINGS"};
    for(i=0;i<4;i++) { q=draw_box(q,210,130+i*48,220,36,i==phone_cursor?125:42,i==phone_cursor?35:50,i==phone_cursor?42:62); q=draw_text(q,226,142+i*48,1,items[i],245,245,245); }
  }
  q=draw_text(q,210,330,1,"UP DOWN O BACK",180,195,210);
 }
 if(map_open) {
  q=draw_box(q,80,55,480,330,12,18,24);
  q=draw_box(q,94,70,452,300,40,58,52);
  for(i=0;i<6;i++) q=draw_box(q,110+i*72,82,3,270,165,165,145);
  for(i=0;i<5;i++) q=draw_box(q,105,90+i*55,430,3,165,165,145);
  q=draw_box(q,px*3/4+85,py*3/4+65,10,10,245,45,48);
  q=draw_text(q,110,345,1,"CITY MAP - START CLOSE",245,230,200);
 }
 q=draw_finish(q);
 dma_channel_send_normal(DMA_CHANNEL_GIF,p->data,q-p->data,0,0);
 dma_wait_fast();
 packet_free(p);
 graph_wait_vsync();
 frame_no++;
}
int main(void) {
 dma_channel_initialize(DMA_CHANNEL_GIF,0,0);
 dma_channel_fast_waits(DMA_CHANNEL_GIF);
 graphics_init();
 controller_init();
 for(;;) { controller_update(); render_frame(); }
 return 0;
}
