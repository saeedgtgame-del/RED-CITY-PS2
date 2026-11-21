
#include <tamtypes.h>
#include <kernel.h>
#include <sifrpc.h>
#include <libpad.h>

#include <gif_tags.h>
#include <gs_gp.h>
#include <gs_psm.h>
#include <dma.h>
#include <draw.h>
#include <graph.h>
#include <packet.h>

#define SCREEN_W 640
#define SCREEN_H 448

static unsigned char padBuf[256] __attribute__((aligned(64)));

static int playerX = 320;
static int playerY = 320;

static int gameStarted = 0;
static int paused = 0;
static int inCar = 0;

static int oldCircle = 0;
static int oldStart = 0;

static framebuffer_t frame;
static zbuffer_t z;
static packet_t *packet;

/* Draw one colored rectangle */

static qword_t *drawBox(
    qword_t *q,
    int x, int y,
    int w, int h,
    int r, int g, int b
)
{
    PACK_GIFTAG(q, GIF_SET_TAG(4, 1, 0, 0, 0, 1), GIF_REG_AD);
    q++;

    PACK_GIFTAG(q, GIF_SET_PRIM(6, 0, 0, 0, 0, 0, 0, 0, 0), GIF_REG_PRIM);
    q++;

    PACK_GIFTAG(q, GIF_SET_RGBAQ(r, g, b, 0x80, 0x3F800000), GIF_REG_RGBAQ);
    q++;

    PACK_GIFTAG(q,
        GIF_SET_XYZ((x << 4) + (2048 << 4),
                    (y << 4) + (2048 << 4), 0),
        GIF_REG_XYZ2);
    q++;

    PACK_GIFTAG(q,
        GIF_SET_XYZ(((x + w) << 4) + (2048 << 4),
                    ((y + h) << 4) + (2048 << 4), 0),
        GIF_REG_XYZ2);
    q++;

    return q;
}

/* Initialize graphics */

static void initGraphics(void)
{
    frame.width = SCREEN_W;
    frame.height = SCREEN_H;
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

    packet = packet_init(512, PACKET_NORMAL);

    dma_channel_initialize(DMA_CHANNEL_GIF, NULL, 0);
    dma_channel_fast_waits(DMA_CHANNEL_GIF);

    qword_t *q = packet->data;

    q = draw_setup_environment(q, 0, &frame, &z);
    q = draw_finish(q);

    dma_channel_send_normal(
        DMA_CHANNEL_GIF,
        packet->data,
        q - packet->data,
        0,
        0
    );

    draw_wait_finish();
}

/* City buildings */

static qword_t *drawBuilding(qword_t *q, int x, int y, int w, int h)
{
    q = drawBox(q, x, y, w, h, 35, 39, 48);

    q = drawBox(q, x + 5, y + 5, w - 10, 4, 90, 92, 100);

    int i;
    for (i = 0; i < 3; i++)
    {
        q = drawBox(q, x + 7, y + 18 + i * 19,
                    7, 9, 210, 145, 65);

        q = drawBox(q, x + 22, y + 18 + i * 19,
                    7, 9, 210, 145, 65);
    }

    return q;
}

/* Draw trees */

static qword_t *drawTree(qword_t *q, int x, int y)
{
    q = drawBox(q, x + 8, y + 12, 6, 15, 100, 65, 35);
    q = drawBox(q, x, y, 22, 20, 28, 105, 55);
    q = drawBox(q, x + 5, y - 4, 12, 10, 40, 135, 65);

    return q;
}

/* Draw the city */

static qword_t *drawCity(qword_t *q)
{
    /* Ground */

    q = drawBox(q, 0, 0, SCREEN_W, SCREEN_H, 45, 65, 48);

    /* Main roads */

    q = drawBox(q, 0, 160, SCREEN_W, 120, 48, 49, 53);
    q = drawBox(q, 245, 0, 150, SCREEN_H, 48, 49, 53);

    /* Sidewalks */

    q = drawBox(q, 0, 154, SCREEN_W, 6, 120, 120, 115);
    q = drawBox(q, 0, 280, SCREEN_W, 6, 120, 120, 115);

    q = drawBox(q, 239, 0, 6, SCREEN_H, 120, 120, 115);
    q = drawBox(q, 395, 0, 6, SCREEN_H, 120, 120, 115);

    /* Road markings */

    int i;

    for (i = 0; i < 10; i++)
    {
        q = drawBox(q, 20 + i * 65, 216, 32, 3, 220, 190, 100);
    }

    for (i = 0; i < 6; i++)
    {
        q = drawBox(q, 315, 15 + i * 70, 3, 30, 220, 190, 100);
    }

    /* Buildings */

    q = drawBuilding(q, 25, 25, 170, 105);
    q = drawBuilding(q, 430, 25, 175, 105);

    q = drawBuilding(q, 25, 320, 170, 95);
    q = drawBuilding(q, 430, 320, 175, 95);

    /* Park */

    q = drawBox(q, 420, 170, 190, 95, 35, 90, 48);

    q = drawTree(q, 445, 178);
    q = drawTree(q, 495, 225);
    q = drawTree(q, 560, 180);
    q = drawTree(q, 580, 230);

    /* Small plaza */

    q = drawBox(q, 45, 175, 150, 65, 100, 91, 75);
    q = drawBox(q, 70, 190, 100, 35, 125, 112, 88);

    return q;
}

/* Player */

static qword_t *drawPlayer(qword_t *q)
{
    if (inCar)
    {
        /* Vehicle shadow */

        q = drawBox(q, playerX - 17, playerY - 8,
                    38, 23, 20, 20, 20);

        /* Wheels */

        q = drawBox(q, playerX - 17, playerY - 11,
                    8, 6, 10, 10, 10);

        q = drawBox(q, playerX + 11, playerY - 11,
                    8, 6, 10, 10, 10);

        q = drawBox(q, playerX - 17, playerY + 10,
                    8, 6, 10, 10, 10);

        q = drawBox(q, playerX + 11, playerY + 10,
                    8, 6, 10, 10, 10);

        /* Car body */

        q = drawBox(q, playerX - 15, playerY - 8,
                    34, 22, 175, 25, 25);

        q = drawBox(q, playerX - 8, playerY - 5,
                    18, 9, 65, 100, 120);

        q = drawBox(q, playerX + 16, playerY - 3,
                    3, 10, 255, 190, 90);
    }
    else
    {
        /* Character */

        q = drawBox(q, playerX - 7, playerY - 7,
                    15, 22, 175, 25, 25);

        q = drawBox(q, playerX - 5, playerY - 17,
                    11, 11, 205, 160, 125);

        q = drawBox(q, playerX - 8, playerY - 19,
                    16, 5, 35, 35, 35);
    }

    return q;
}

/* HUD */

static qword_t *drawHUD(qword_t *q)
{
    /* Top panel */

    q = drawBox(q, 10, 10, 210, 45, 15, 17, 23);

    /* Health bar */

    q = drawBox(q, 22, 23, 130, 9, 65, 65, 65);
    q = drawBox(q, 22, 23, 105, 9, 190, 35, 35);

    /* Money indicator */

    q = drawBox(q, 165, 20, 40, 20, 190, 145, 55);

    /* Mini map */

    q = drawBox(q, 525, 10, 105, 90, 12, 15, 20);

    q = drawBox(q, 570, 15, 8, 80, 65, 68, 75);
    q = drawBox(q, 530, 50, 95, 7, 65, 68, 75);

    q = drawBox(q, 570, 52, 8, 7, 220, 35, 35);

    /* Player position */

    q = drawBox(q, 573, 53, 5, 5, 255, 210, 80);

    return q;
}

/* Start screen */

static qword_t *drawStartScreen(qword_t *q)
{
    q = drawBox(q, 0, 0, SCREEN_W, SCREEN_H, 10, 12, 19);

    q = drawBox(q, 180, 105, 280, 185, 25, 27, 35);

    /* Red logo symbol */

    q = drawBox(q, 245, 145, 150, 13, 190, 25, 25);
    q = drawBox(q, 245, 145, 13, 65, 190, 25, 25);
    q = drawBox(q, 382, 145, 13, 65, 190, 25, 25);

    /* Start button */

    q = drawBox(q, 255, 240, 130, 24, 180, 30, 30);

    /* Gold line */

    q = drawBox(q, 205, 305, 230, 4, 205, 155, 65);

    return q;
}

/* Pause screen */

static qword_t *drawPauseScreen(qword_t *q)
{
    q = drawBox(q, 170, 130, 300, 170, 15, 17, 23);

    q = drawBox(q, 220, 180, 200, 18, 190, 30, 30);

    q = drawBox(q, 245, 235, 150, 12, 190, 155, 65);

    return q;
}

/* Controller input */

static void updateGame(void)
{
    if (padGetState(0, 0) != PAD_STATE_STABLE)
        return;

    padButtonStatus buttons;

    if (!padRead(0, 0, &buttons))
        return;

    int left  = !(buttons.btns & PAD_LEFT);
    int right = !(buttons.btns & PAD_RIGHT);
    int up    = !(buttons.btns & PAD_UP);
    int down  = !(buttons.btns & PAD_DOWN);

    int cross = !(buttons.btns & PAD_CROSS);
    int circle = !(buttons.btns & PAD_CIRCLE);
    int start = !(buttons.btns & PAD_START);

    if (!gameStarted)
    {
        if (cross)
            gameStarted = 1;

        return;
    }

    if (start && !oldStart)
        paused = !paused;

    oldStart = start;

    if (paused)
        return;

    if (circle && !oldCircle)
        inCar = !inCar;

    oldCircle = circle;

    int speed = inCar ? 3 : 2;

    if (cross)
        speed++;

    if (left)
        playerX -= speed;

    if (right)
        playerX += speed;

    if (up)
        playerY -= speed;

    if (down)
        playerY += speed;

    /* Keep player inside the city screen */

    if (playerX < 15) playerX = 15;
    if (playerX > 620) playerX = 620;

    if (playerY < 65) playerY = 65;
    if (playerY > 425) playerY = 425;
}

/* Render one frame */

static void renderGame(void)
{
    dma_wait_fast();

    qword_t *q = packet->data;

    q = draw_clear(
        q,
        0,
        0,
        0,
        SCREEN_W,
        SCREEN_H,
        10,
        12,
        18
    );

    if (!gameStarted)
    {
        q = drawStartScreen(q);
    }
    else
    {
        q = drawCity(q);
        q = drawPlayer(q);
        q = drawHUD(q);

        if (paused)
            q = drawPauseScreen(q);
    }

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

/* Main */

int main(void)
{
    SifInitRpc(0);

    while (!SifIopSync())
    {
    }

    padInit(0);

    if (!padPortOpen(0, 0, padBuf))
    {
        return 1;
    }

    initGraphics();

    while (1)
    {
        updateGame();
        renderGame();
    }

    return 0;
}
    
