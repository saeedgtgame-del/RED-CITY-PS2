
#include <kernel.h>
#include <stdlib.h>
#include <malloc.h>
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

#define SCREEN_W 640
#define SCREEN_H 512

static framebuffer_t frame;
static zbuffer_t z;
static packet_t *packet;

static void setup_screen(void)
{
    frame.width = SCREEN_W;
    frame.height = SCREEN_H;
    frame.mask = 0;
    frame.psm = GS_PSM_32;
    frame.address = graph_vram_allocate(
        frame.width, frame.height, frame.psm, GRAPH_ALIGN_PAGE);

    z.enable = DRAW_ENABLE;
    z.mask = 0;
    z.method = ZTEST_METHOD_GREATER_EQUAL;
    z.zsm = GS_ZBUF_32;
    z.address = graph_vram_allocate(
        frame.width, frame.height, z.zsm, GRAPH_ALIGN_PAGE);

    graph_initialize(frame.address, frame.width,
                     frame.height, frame.psm, 0, 0);

    packet = packet_init(100, PACKET_NORMAL);

    qword_t *q = packet->data;
    q = draw_setup_environment(q, 0, &frame, &z);
    q = draw_primitive_xyoffset(q, 0, 2048-320, 2048-256);
    q = draw_finish(q);

    dma_channel_send_normal(
        DMA_CHANNEL_GIF, packet->data,
        q - packet->data, 0, 0);

    dma_wait_fast();
}

static void rectangle(qword_t **q, float x1, float y1,
                      float x2, float y2, int r, int g, int b)
{
    PACK_GIFTAG(*q, GIF_SET_TAG(4, 1, 0, 0, 0, 1),
                GIF_REG_AD);
    (*q)++;

    PACK_GIFTAG(*q, GIF_SET_PRIM(6, 0, 0, 0, 0, 0, 0, 0, 0),
                GIF_REG_PRIM);
    (*q)++;

    PACK_GIFTAG(*q, GIF_SET_RGBAQ(r, g, b, 0x80, 0x3f800000),
                GIF_REG_RGBAQ);
    (*q)++;

    PACK_GIFTAG(*q, GIF_SET_XYZ(
        ((int)x1 + 2048) << 4,
        ((int)y1 + 2048) << 4, 100), GIF_REG_XYZ2);
    (*q)++;

    PACK_GIFTAG(*q, GIF_SET_XYZ(
        ((int)x2 + 2048) << 4,
        ((int)y2 + 2048) << 4, 100), GIF_REG_XYZ2);
    (*q)++;
}

static void render_scene(void)
{
    qword_t *q = packet->data;

    q = draw_clear(q, 0, 0, 0,
                   SCREEN_W, SCREEN_H, 22, 25, 42);

    /* زمین */
    rectangle(&q, -320, -256, 320, 256, 65, 75, 72);

    /* خیابان اصلی */
    rectangle(&q, -320, -65, 320, 65, 35, 38, 42);

    /* خط وسط خیابان */
    rectangle(&q, -300, -3, 300, 3, 230, 175, 65);

    /* ساختمان‌های سمت چپ */
    rectangle(&q, -290, -210, -190, -80, 65, 48, 43);
    rectangle(&q, -170, -220, -85, -80, 90, 58, 43);
    rectangle(&q, -65, -220, 25, -80, 55, 58, 68);

    /* ساختمان‌های سمت راست */
    rectangle(&q, 65, 80, 155, 220, 75, 52, 43);
    rectangle(&q, 180, 80, 280, 220, 55, 60, 70);
    rectangle(&q, 70, -220, 170, -80, 90, 62, 45);
    rectangle(&q, 195, -220, 295, -80, 62, 50, 48);

    /* محوطه روشن وسط شهر */
    rectangle(&q, -45, 95, 45, 210, 105, 72, 48);

    q = draw_finish(q);

    dma_wait_fast();
    dma_channel_send_normal(
        DMA_CHANNEL_GIF, packet->data,
        q - packet->data, 0, 0);

    draw_wait_finish();
    graph_wait_vsync();
}

int main(int argc, char *argv[])
{
    dma_channel_initialize(DMA_CHANNEL_GIF, NULL, 0);
    dma_channel_fast_waits(DMA_CHANNEL_GIF);

    setup_screen();

    for (;;)
    {
        render_scene();
    }

    return 0;
}
