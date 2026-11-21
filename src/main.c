
#include <kernel.h>
#include <stdlib.h>
#include <tamtypes.h>
#include <dma.h>
#include <graph.h>
#include <draw.h>
#include <draw2d.h>
#include <packet.h>
#include <gs_psm.h>
#include <libpad.h>

#define SW 640
#define SH 512

static framebuffer_t frame;
static zbuffer_t zbuf;
static packet_t *packet;

static unsigned char padBuffer[256]
    __attribute__((aligned(64)));

static struct padButtonStatus pad;

static int padReady = 0;
static int missionComplete = 0;

static float playerX = 320.0f;
static float playerY = 425.0f;

/* --------------------------------
   BASIC RECTANGLE
-------------------------------- */

static qword_t *box(
    qword_t *q,
    float x, float y,
    float w, float h,
    int r, int g, int b)
{
    rect_t rect;

    rect.v0.x = x;
    rect.v0.y = y;
    rect.v0.z = 1;

    rect.v1.x = x + w;
    rect.v1.y = y + h;
    rect.v1.z = 1;

    rect.color.r = r;
    rect.color.g = g;
    rect.color.b = b;
    rect.color.a = 128;
    rect.color.q = 1.0f;

    return draw_rect_filled(q, 0, &rect);
}

static qword_t *fadeBox(
    qword_t *q,
    int x, int y, int w, int h,
    int r, int g, int b, int fade)
{
    r = r * fade / 255;
    g = g * fade / 255;
    b = b * fade / 255;

    return box(q, x, y, w, h, r, g, b);
}

/* --------------------------------
   CUSTOM FONT
-------------------------------- */

static unsigned char glyph(char c, int row)
{
    static const unsigned char A[7] =
        {14,17,17,31,17,17,17};

    static const unsigned char E[7] =
        {31,16,16,30,16,16,31};

    static const unsigned char G[7] =
        {14,17,16,23,17,17,15};

    static const unsigned char M[7] =
        {17,27,21,21,17,17,17};

    static const unsigned char N[7] =
        {17,25,21,19,17,17,17};

    static const unsigned char O[7] =
        {14,17,17,17,17,17,14};

    static const unsigned char S[7] =
        {15,16,16,14,1,1,30};

    static const unsigned char X[7] =
        {17,17,10,4,10,17,17};

    if (row < 0 || row > 6)
        return 0;

    switch (c)
    {
        case 'A': return A[row];
        case 'E': return E[row];
        case 'G': return G[row];
        case 'M': return M[row];
        case 'N': return N[row];
        case 'O': return O[row];
        case 'S': return S[row];
        case 'X': return X[row];
        default: return 0;
    }
}

static qword_t *drawText(
    qword_t *q,
    const char *text,
    int x, int y,
    int scale,
    int r, int g, int b,
    int fade)
{
    int i, row, col;

    for (i = 0; text[i] != 0; i++)
    {
        for (row = 0; row < 7; row++)
        {
            unsigned char bits = glyph(text[i], row);

            for (col = 0; col < 5; col++)
            {
                if (bits & (1 << (4 - col)))
                {
                    q = fadeBox(
                        q,
                        x + i * 6 * scale + col * scale,
                        y + row * scale,
                        scale, scale,
                        r, g, b, fade);
                }
            }
        }
    }

    return q;
}

/* --------------------------------
   NEXON GAMES LOGO
-------------------------------- */

static void drawLogo(int fade)
{
    qword_t *q = packet->data;

    q = draw_clear(
        q, 0,
        2048 - 320,
        2048 - 256,
        SW, SH,
        0, 0, 0);

    /* Red logo symbol */
    q = fadeBox(q, 250, 105, 18, 105,
                235, 0, 25, fade);

    q = fadeBox(q, 270, 190, 100, 16,
                235, 0, 25, fade);

    q = fadeBox(q, 355, 105, 18, 105,
                235, 0, 25, fade);

    /* White N symbol */
    q = fadeBox(q, 260, 110, 20, 95,
                245, 245, 245, fade);

    q = fadeBox(q, 280, 110, 20, 25,
                245, 245, 245, fade);

    q = fadeBox(q, 300, 135, 20, 25,
                245, 245, 245, fade);

    q = fadeBox(q, 320, 160, 20, 25,
                245, 245, 245, fade);

    q = fadeBox(q, 340, 185, 20, 20,
                245, 245, 245, fade);

    /* Company name */
    q = drawText(q, "NEXON",
                 175, 255, 8,
                 245, 245, 245, fade);

    q = drawText(q, "GAMES",
                 205, 335, 5,
                 245, 245, 245, fade);

    /* Red lines */
    q = fadeBox(q, 130, 380, 110, 4,
                235, 0, 25, fade);

    q = fadeBox(q, 400, 380, 110, 4,
                235, 0, 25, fade);

    q = draw_finish(q);

    dma_wait_fast();

    dma_channel_send_normal(
        DMA_CHANNEL_GIF,
        packet->data,
        q - packet->data,
        0, 0);

    draw_wait_finish();
    graph_wait_vsync();
}

/* --------------------------------
   SEVEN SECOND INTRO
-------------------------------- */

static void companyIntro(void)
{
    int frameNumber;
    int fade;

    /*
       420 frames at approximately
       60 frames per second = 7 seconds.

       First 300 frames: full logo.
       Last 120 frames: fade out.
    */

    for (frameNumber = 0;
         frameNumber < 420;
         frameNumber++)
    {
        fade = 255;

        if (frameNumber >= 300)
        {
            fade =
                255 * (419 - frameNumber) / 120;
        }

        drawLogo(fade);
    }
}

/* --------------------------------
   GRAPHICS INITIALIZATION
-------------------------------- */

static void initGraphics(void)
{
    frame.width = SW;
    frame.height = SH;
    frame.mask = 0;
    frame.psm = GS_PSM_32;

    frame.address = graph_vram_allocate(
        SW, SH, GS_PSM_32, GRAPH_ALIGN_PAGE);

    zbuf.enable = DRAW_ENABLE;
    zbuf.mask = 0;
    zbuf.method = ZTEST_METHOD_GREATER_EQUAL;
    zbuf.zsm = GS_ZBUF_32;

    zbuf.address = graph_vram_allocate(
        SW, SH, GS_ZBUF_32, GRAPH_ALIGN_PAGE);

    graph_initialize(
        frame.address,
        SW, SH,
        GS_PSM_32,
        0, 0);

    packet_t *setup = packet_init(32, PACKET_NORMAL);

    qword_t *q = setup->data;

    q = draw_setup_environment(
        q, 0, &frame, &zbuf);

    q = draw_primitive_xyoffset(
        q, 0, 2048 - 320, 2048 - 256);

    q = draw_finish(q);

    dma_channel_send_normal(
        DMA_CHANNEL_GIF,
        setup->data,
        q - setup->data,
        0, 0);

    dma_wait_fast();

    packet_free(setup);

    packet = packet_init(1024, PACKET_NORMAL);
}

/* --------------------------------
   CONTROLLER
-------------------------------- */

static void initController(void)
{
    if (padInit(0) == 1)
    {
        if (padPortOpen(0, 0, padBuffer))
            padReady = 1;
    }
}

static void updatePlayer(void)
{
    if (!padReady || missionComplete)
        return;

    if (padGetState(0, 0) != PAD_STATE_STABLE)
        return;

    if (!padRead(0, 0, &pad))
        return;

    if (!(pad.btns & PAD_UP))
        playerY -= 2.5f;

    if (!(pad.btns & PAD_DOWN))
        playerY += 2.5f;

    if (!(pad.btns & PAD_LEFT))
        playerX -= 2.5f;

    if (!(pad.btns & PAD_RIGHT))
        playerX += 2.5f;

    if (playerX < 20) playerX = 20;
    if (playerX > 620) playerX = 620;

    if (playerY < 45) playerY = 45;
    if (playerY > 485) playerY = 485;

    if (playerX > 280 && playerX < 360 &&
        playerY > 60 && playerY < 120)
    {
        missionComplete = 1;
    }
}

/* --------------------------------
   CITY
-------------------------------- */

static void drawCity(qword_t **q)
{
    int y, x;

    /* Grass and ground */
    *q = box(*q, 0, 0, SW, SH,
             72, 82, 69);

    /* Main road */
    *q = box(*q, 225, 0, 190, SH,
             38, 40, 44);

    /* Sidewalks */
    *q = box(*q, 205, 0, 20, SH,
             145, 135, 115);

    *q = box(*q, 415, 0, 20, SH,
             145, 135, 115);

    /* Road lines */
    for (y = 10; y < SH; y += 70)
    {
        *q = box(*q, 318, y, 4, 34,
                 230, 190, 110);
    }

    /* Cross street */
    *q = box(*q, 0, 195, SW, 48,
             42, 44, 48);

    for (x = 15; x < SW; x += 90)
    {
        *q = box(*q, x, 217, 45, 3,
                 220, 195, 145);
    }

    /* Left buildings */
    *q = box(*q, 29, 58, 135, 115,
             105, 70, 50);

    *q = box(*q, 29, 253, 135, 115,
             85, 65, 53);

    *q = box(*q, 50, 82, 25, 25,
             185, 145, 95);

    *q = box(*q, 90, 82, 25, 25,
             185, 145, 95);

    *q = box(*q, 50, 278, 25, 25,
             175, 135, 95);

    *q = box(*q, 90, 278, 25, 25,
             175, 135, 95);

    /* Right buildings */
    *q = box(*q, 464, 58, 135, 115,
             75, 82, 95);

    *q = box(*q, 464, 253, 135, 115,
             110, 65, 48);

    *q = box(*q, 485, 82, 25, 25,
             175, 175, 165);

    *q = box(*q, 525, 82, 25, 25,
             175, 175, 165);

    *q = box(*q, 485, 278, 25, 25,
             190, 140, 85);

    *q = box(*q, 525, 278, 25, 25,
             190, 140, 85);

    /* Mission destination */
    *q = box(*q, 285, 65, 70, 7,
             30, 240, 90);

    *q = box(*q, 285, 65, 7, 48,
             30, 240, 90);

    *q = box(*q, 348, 65, 7, 48,
             30, 240, 90);

    *q = box(*q, 285, 106, 70, 7,
             30, 240, 90);
}

/* --------------------------------
   PLAYER
-------------------------------- */

static void drawPlayer(qword_t **q)
{
    *q = box(*q,
        playerX - 9,
        playerY + 7,
        20, 8,
        25, 25, 25);

    *q = box(*q,
        playerX - 7,
        playerY - 5,
        15, 22,
        185, 35, 35);

    *q = box(*q,
        playerX - 5,
        playerY - 14,
        11, 10,
        220, 175, 135);

    *q = box(*q,
        playerX - 5,
        playerY - 16,
        11, 4,
        35, 27, 25);
}

/* --------------------------------
   HUD
-------------------------------- */

static void drawHUD(qword_t **q)
{
    *q = box(*q, 0, 0, SW, 35,
             12, 15, 23);

    /* Red logo mark */
    *q = box(*q, 12, 9, 12, 18,
             230, 25, 35);

    /* Mission bar */
    *q = box(*q, 450, 12, 165, 9,
             55, 55, 60);

    if (missionComplete)
    {
        *q = box(*q, 450, 12, 165, 9,
                 40, 220, 100);
    }
    else
    {
        *q = box(*q, 450, 12, 35, 9,
                 220, 145, 35);
    }
}

/* --------------------------------
   GAME RENDER
-------------------------------- */

static void renderGame(void)
{
    qword_t *q = packet->data;

    q = draw_disable_tests(q, 0, &zbuf);

    q = draw_clear(
        q, 0,
        2048.0f - 320.0f,
        2048.0f - 256.0f,
        SW, SH,
        72, 82, 69);

    q = draw_enable_tests(q, 0, &zbuf);

    drawCity(&q);
    drawPlayer(&q);
    drawHUD(&q);

    if (missionComplete)
    {
        q = box(q, 170, 225, 300, 65,
                18, 80, 45);

        q = box(q, 190, 240, 260, 7,
                80, 240, 130);

        q = box(q, 220, 258, 200, 7,
                80, 240, 130);
    }

    q = draw_finish(q);

    dma_wait_fast();

    dma_channel_send_normal(
        DMA_CHANNEL_GIF,
        packet->data,
        q - packet->data,
        0, 0);

    draw_wait_finish();
    graph_wait_vsync();
}

/* --------------------------------
   MAIN
-------------------------------- */

int main(void)
{
    dma_channel_initialize(
        DMA_CHANNEL_GIF, NULL, 0);

    dma_channel_fast_waits(
        DMA_CHANNEL_GIF);

    initGraphics();

    /* Company logo intro */
    companyIntro();

    /* Start game */
    initController();

    for (;;)
    {
        updatePlayer();
        renderGame();
    }

    return 0;
}
