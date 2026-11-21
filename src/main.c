
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
#include <libpad.h>

/* RED CITY - 3D city prototype */

#define SCREEN_W 640
#define SCREEN_H 512

static framebuffer_t frame;
static zbuffer_t z;

static VECTOR player = {0.0f, 0.0f, 0.0f, 1.0f};
static VECTOR camera = {0.0f, 0.0f, 100.0f, 1.0f};
static VECTOR rotation = {0.0f, 0.0f, 0.0f, 1.0f};

static void init_video(void)
{
    frame.width = SCREEN_W;
    frame.height = SCREEN_H;
    frame.mask = 0;
    frame.psm = GS_PSM_32;
    frame.address = graph_vram_allocate(
        SCREEN_W, SCREEN_H, GS_PSM_32, GRAPH_ALIGN_PAGE);

    z.enable = DRAW_ENABLE;
    z.mask = 0;
    z.method = ZTEST_METHOD_GREATER_EQUAL;
    z.zsm = GS_ZBUF_32;
    z.address = graph_vram_allocate(
        SCREEN_W, SCREEN_H, GS_ZBUF_32, GRAPH_ALIGN_PAGE);

    graph_initialize(frame.address, SCREEN_W, SCREEN_H,
                     GS_PSM_32, 0, 0);

    packet_t *p = packet_init(16, PACKET_NORMAL);
    qword_t *q = p->data;

    q = draw_setup_environment(q, 0, &frame, &z);
    q = draw_primitive_xyoffset(q, 0, 2048-320, 2048-256);
    q = draw_finish(q);

    dma_channel_send_normal(DMA_CHANNEL_GIF, p->data,
                            q-p->data, 0, 0);
    dma_wait_fast();
    packet_free(p);
}

static void draw_scene(void)
{
    packet_t *p = packet_init(100, PACKET_NORMAL);
    qword_t *q = p->data;

    q = draw_disable_tests(q, 0, &z);
    q = draw_clear(q, 0, 0, 0, SCREEN_W, SCREEN_H,
                   70, 100, 140);
    q = draw_enable_tests(q, 0, &z);

    q = draw_finish(q);

    dma_wait_fast();
    dma_channel_send_normal(DMA_CHANNEL_GIF, p->data,
                            q-p->data, 0, 0);
    draw_wait_finish();
    graph_wait_vsync();

    packet_free(p);
}

int main(void)
{
    dma_channel_initialize(DMA_CHANNEL_GIF, NULL, 0);
    dma_channel_fast_waits(DMA_CHANNEL_GIF);

    init_video();

    while (1)
    {
        draw_scene();
    }

    SleepThread();
    return 0;
}
