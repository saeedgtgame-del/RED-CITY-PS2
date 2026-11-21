
#include <tamtypes.h>
#include <kernel.h>
#include <sifrpc.h>
#include <debug.h>
#include <libpad.h>

static unsigned char padBuf[256] __attribute__((aligned(64)));

static int playerX = 50;
static int playerY = 50;

static int gameStarted = 0;
static int paused = 0;
static int inCar = 0;

static u32 previousButtons = 0;
static int frameCounter = 0;

static void showTitle(void)
{
    init_scr();

    scr_printf("\n");
    scr_printf("====================================\n");
    scr_printf("             RED CITY\n");
    scr_printf("             PS2 V0.2\n");
    scr_printf("====================================\n");
    scr_printf("       THE CITY IS YOURS\n");
    scr_printf("\n");
    scr_printf("         PRESS CROSS TO START\n");
    scr_printf("\n");
    scr_printf("====================================\n");
}

static void showStatus(void)
{
    scr_printf("\n");
    scr_printf("------------------------------------\n");
    scr_printf("             RED CITY\n");
    scr_printf("------------------------------------\n");

    scr_printf("PLAYER X: %d\n", playerX);
    scr_printf("PLAYER Y: %d\n", playerY);

    if (inCar)
        scr_printf("MODE: VEHICLE\n");
    else
        scr_printf("MODE: ON FOOT\n");

    if (paused)
        scr_printf("GAME: PAUSED\n");
    else
        scr_printf("GAME: RUNNING\n");

    scr_printf("------------------------------------\n");
    scr_printf("D-PAD: MOVE\n");
    scr_printf("CROSS: INTERACT / START\n");
    scr_printf("CIRCLE: ENTER / EXIT VEHICLE\n");
    scr_printf("SQUARE: CITY INTERACTION\n");
    scr_printf("TRIANGLE: CITY MAP INFO\n");
    scr_printf("START: PAUSE / RESUME\n");
    scr_printf("------------------------------------\n");
}

static void updateGame(u32 buttons)
{
    int speed = inCar ? 3 : 1;

    if (paused)
        return;

    if (buttons & PAD_UP)
        playerY -= speed;

    if (buttons & PAD_DOWN)
        playerY += speed;

    if (buttons & PAD_LEFT)
        playerX -= speed;

    if (buttons & PAD_RIGHT)
        playerX += speed;

    if (playerX < 0)
        playerX = 0;

    if (playerX > 100)
        playerX = 100;

    if (playerY < 0)
        playerY = 0;

    if (playerY > 100)
        playerY = 100;

    frameCounter++;

    if (frameCounter >= 100)
    {
        frameCounter = 0;

        scr_printf(
            "POSITION: X=%d Y=%d\n",
            playerX,
            playerY
        );
    }
}

int main(int argc, char *argv[])
{
    struct padButtonStatus buttons;
    u32 pressed;
    u32 justPressed;
    int state;
    int opened;

    (void)argc;
    (void)argv;

    SifInitRpc(0);

    padInit(0);

    showTitle();

    scr_printf("Connecting controller...\n");

    opened = padPortOpen(0, 0, padBuf);

    if (!opened)
    {
        scr_printf("ERROR: CONTROLLER NOT FOUND\n");

        for (;;)
            SleepThread();
    }

    scr_printf("CONTROLLER CONNECTED!\n");

    for (;;)
    {
        state = padGetState(0, 0);

        if (state == PAD_STATE_STABLE ||
            state == PAD_STATE_FINDCTP1)
        {
            if (padRead(0, 0, &buttons) != 0)
            {
                pressed = 0xFFFF ^ buttons.btns;

                justPressed = pressed & ~previousButtons;

                if (!gameStarted)
                {
                    if (justPressed & PAD_CROSS)
                    {
                        gameStarted = 1;

                        scr_printf("\n");
                        scr_printf("WELCOME TO RED CITY!\n");
                        scr_printf("GAME STARTED!\n");

                        showStatus();
                    }
                }
                else
                {
                    if (justPressed & PAD_START)
                    {
                        paused = !paused;

                        if (paused)
                            scr_printf("GAME PAUSED\n");
                        else
                            scr_printf("GAME RESUMED\n");
                    }

                    if (!paused)
                    {
                        if (justPressed & PAD_CIRCLE)
                        {
                            inCar = !inCar;

                            if (inCar)
                                scr_printf("VEHICLE MODE ON\n");
                            else
                                scr_printf("ON FOOT MODE\n");
                        }

                        if (justPressed & PAD_SQUARE)
                        {
                            scr_printf("CITY INTERACTION!\n");
                        }

                        if (justPressed & PAD_TRIANGLE)
                        {
                            scr_printf("\n");
                            scr_printf("====== CITY MAP INFO ======\n");
                            scr_printf("PLAYER X: %d\n", playerX);
                            scr_printf("PLAYER Y: %d\n", playerY);
                            scr_printf("CITY CENTER: X=50 Y=50\n");
                            scr_printf("===========================\n");
                        }

                        updateGame(pressed);
                    }
                }

                previousButtons = pressed;
            }
        }
        else if (state == PAD_STATE_DISCONN)
        {
            scr_printf("CONTROLLER DISCONNECTED!\n");
        }

        nopdelay();
    }

    return 0;
}
