
#include <tamtypes.h>
#include <kernel.h>
#include <sifrpc.h>
#include <libpad.h>
#include <stdio.h>
#include <math.h>
#include <string.h>
#include <malloc.h>

#include <draw.h>
#include <graph.h>
#include <packet.h>
#include <dma.h>
#include <gs_gp.h>
#include <gs_psm.h>
#include <gif_tags.h>

#define SCREEN_W 640
#define SCREEN_H 448

#define PI 3.14159265f

static unsigned char padBuf[256] __attribute__((aligned(64)));

static framebuffer_t frame;
static zbuffer_t z;
static packet_t *packet;

static float playerX = 0.0f;
static float playerZ = 0.0f;
static float cameraYaw = 0.0f;
static float cameraDistance = 7.0f;

static int running = 1;

static float clampf(float v, float a, float b)
{
    if (v < a) return a;
    if (v > b) return b;
    return v;
}

static void initGraphics(void)
{
    frame.width = 640;
    frame.height = 448;
    frame.mask = 0;
    frame.psm = GS_PSM_32;

    frame.address = graph_vram_allocate(
        frame.width,
        frame.height,
        frame.psm,
        GRAPH_ALIGN_PAGE
    );

    z.enable = 0;
    z.address = 0;
    z.mask = 0;
    z.zsm = 0;

    graph_initialize(
        frame.address,
        frame.width,
        frame.height,
        frame.psm,
        0,
        0
    );

    graph_set_bgcolor(80, 125, 170);
    graph_enable_output();

    packet = packet_init(100, PACKET_NORMAL);

    dma_channel_initialize(DMA_CHANNEL_GIF, NULL, 0);
    dma_channel_fast_waits(DMA_CHANNEL_GIF);
}

static void drawRect(
    qword_t **q,
    int x,
    int y,
    int w,
    int h,
    int r,
    int g,
    int b
)
{
    PACK_GIFTAG(*q, GIF_SET_TAG(4, 1, 0, 0, 1, 0), GIF_REG_AD);
    (*q)++;

    PACK_GIFTAG(*q, GIF_SET_PRIM(6, 0, 0, 0, 0, 0, 0, 0, 0), GIF_REG_PRIM);
    (*q)++;

    PACK_GIFTAG(*q, GIF_SET_RGBAQ(r, g, b, 0x80, 0x3F800000), GIF_REG_RGBAQ);
    (*q)++;

    PACK_GIFTAG(*q,
        GIF_SET_XYZ((x + 2048) << 4, (y + 2048) << 4, 0),
        GIF_REG_XYZ2);
    (*q)++;

    PACK_GIFTAG(*q,
        GIF_SET_XYZ(((x + w) + 2048) << 4, ((y + h) + 2048) << 4, 0),
        GIF_REG_XYZ2);
    (*q)++;
}

static void renderScene(void)
{
    qword_t *q;

    dma_wait_fast();

    q = packet->data;

    q = draw_clear(q, 0, 0, 0, SCREEN_W, SCREEN_H, 30, 100, 150);

    /* Ground */
    drawRect(&q, 0, 280, 640, 168, 65, 105, 65);

    /* Road */
    drawRect(&q, 0, 335, 640, 75, 55, 55, 60);

    /* Road markings */
    drawRect(&q, 30, 370, 75, 5, 230, 220, 170);
    drawRect(&q, 190, 370, 75, 5, 230, 220, 170);
    drawRect(&q, 350, 370, 75, 5, 230, 220, 170);
    drawRect(&q, 510, 370, 75, 5, 230, 220, 170);

    /* Buildings */
    drawRect(&q, 40, 175, 90, 105, 110, 115, 125);
    drawRect(&q, 155, 205, 95, 75, 145, 125, 105);
    drawRect(&q, 410, 185, 90, 95, 125, 130, 145);
    drawRect(&q, 525, 150, 80, 130, 100, 105, 120);

    /* Building windows */
    drawRect(&q, 55, 190, 18, 20, 80, 175, 210);
    drawRect(&q, 90, 190, 18, 20, 80, 175, 210);
    drawRect(&q, 55, 225, 18, 20, 80, 175, 210);
    drawRect(&q, 90, 225, 18, 20, 80, 175, 210);

    drawRect(&q, 430, 200, 18, 20, 80, 175, 210);
    drawRect(&q, 465, 200, 18, 20, 80, 175, 210);

    /* Temporary Sophia character */
    drawRect(&q, 302, 260, 36, 55, 180, 45, 55);
    drawRect(&q, 306, 242, 28, 25, 220, 175, 140);
    drawRect(&q, 300, 310, 15, 30, 30, 30, 35);
    drawRect(&q, 325, 310, 15, 30, 30, 30, 35);

    /* HUD */
    drawRect(&q, 12, 12, 190, 35, 15, 20, 28);
    drawRect(&q, 440, 12, 185, 35, 15, 20, 28);

    q = draw_finish(q);

    dma_channel_send_normal(
        DMA_CHANNEL_GIF,
        packet->data,
        q - packet->data,
        0,
        0
    );

    draw_wait_finish();
    graph_wait_vsync();
}

static void updateController(void)
{
    struct padButtonStatus buttons;

    int state = padGetState(0, 0);

    if (state != PAD_STATE_STABLE)
        return;

    if (padRead(0, 0, &buttons) == 0)
        return;

    unsigned short pressed = buttons.btns;

    if (!(pressed & PAD_LEFT))
        playerX -= 0.08f;

    if (!(pressed & PAD_RIGHT))
        playerX += 0.08f;

    if (!(pressed & PAD_UP))
        playerZ += 0.08f;

    if (!(pressed & PAD_DOWN))
        playerZ -= 0.08f;

    if (!(pressed & PAD_L1))
        cameraYaw -= 0.04f;

    if (!(pressed & PAD_R1))
        cameraYaw += 0.04f;

    if (!(pressed & PAD_START))
        running = 0;

    playerX = clampf(playerX, -10.0f, 10.0f);
    playerZ = clampf(playerZ, -10.0f, 10.0f);
}

int main(void)
{
    SifInitRpc(0);

    padInit(0);
    padPortOpen(0, 0, padBuf);

    initGraphics();

    while (running)
    {
        updateController();
        renderScene();
    }

    dma_wait_fast();

    packet_free(packet);

    graph_shutdown();

    dma_channel_shutdown(DMA_CHANNEL_GIF, 0);

    padPortClose(0, 0);
    padEnd();

    SleepThread();

    return 0;
}
