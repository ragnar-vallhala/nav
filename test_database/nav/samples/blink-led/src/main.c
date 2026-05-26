#include <stdint.h>

int main(void) {
  volatile uint32_t ticks = 0;
  while (1) {
    ticks++;
  }
}
