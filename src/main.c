#include <tamtypes.h>
#include <kernel.h>
#include <sifrpc.h>
#include <debug.h>
#include <libpad.h>

static unsigned char padBuf[256] __attribute__((aligned(64)));

int main(int argc, char *argv[])
{
    struct padButtonStatus buttons;
    u32 pressed;
    u32 previous = 0;
    int state;
    int opened;

    (void)argc;
    (void)argv;

    SifInitRpc(0);
    init_scr();

    scr_printf("\n");
    scr_printf("================================\n");
    scr_printf("          RED CITY\n");
    scr_printf("       PS2 TEST V0.1\n");
    scr_printf("================================\n");
    scr_printf("TABRIZ - HOMEbrew TEST\n\n");
    scr_printf("Connecting controller 1...\n");

    padInit(0);
    opened = padPortOpen(0, 0, padBuf);

    if (!opened) {
        scr_printf("Controller port could not be opened.\n");
        scr_printf("Check controller connection.\n");
        for (;;) SleepThread();
    }

    scr_printf("Controller port opened.\n");
    scr_printf("Press D-Pad to test input.\n");
    scr_printf("Press START to exit.\n\n");

    for (;;) {
        state = padGetState(0, 0);

        if (state == PAD_STATE_STABLE || state == PAD_STATE_FINDCTP1) {
            if (padRead(0, 0, &buttons) != 0) {
                pressed = 0xFFFF ^ buttons.btns;
                if (pressed != previous) {
                    if (pressed & PAD_UP)      scr_printf("UP\n");
                    if (pressed & PAD_DOWN)    scr_printf("DOWN\n");
                    if (pressed & PAD_LEFT)    scr_printf("LEFT\n");
                    if (pressed & PAD_RIGHT)   scr_printf("RIGHT\n");
                    if (pressed & PAD_CROSS)   scr_printf("CROSS\n");
                    if (pressed & PAD_CIRCLE)  scr_printf("CIRCLE\n");
                    if (pressed & PAD_TRIANGLE)scr_printf("TRIANGLE\n");
                    if (pressed & PAD_SQUARE)  scr_printf("SQUARE\n");
                    if (pressed & PAD_START)   break;
                    previous = pressed;
                }
            }
        } else if (state == PAD_STATE_DISCONN) {
            scr_printf("Controller disconnected.\n");
        }

        nopdelay();
    }

    padPortClose(0, 0);
    padEnd();
    scr_printf("\nRED CITY TEST ENDED.\n");
    for (;;) SleepThread();

    return 0;
}
