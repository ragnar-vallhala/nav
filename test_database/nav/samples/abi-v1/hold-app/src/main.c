#include <hold_app/hold_app.h>

static volatile unsigned int ticks;

int hold_app_tick(int input_pressed) {
    if (input_pressed) {
        ticks += 1;
    }
    return (int)(ticks & 1u);
}

int main(void) {
    for (;;) {
        (void)hold_app_tick(1);
    }
    return 0;
}
