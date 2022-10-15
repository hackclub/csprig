#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "ST7735_TFT.h"

static void oom(void) { puts("oom!"); }
#include "base_engine.c"

static void init_screen() {

  /* send initialization over SPI */
}

uint16_t screen[160][128] = {0};
static uint16_t pixel(int x, int y) {
  return screen[x][y];
  // return (x%2 == y%2) ? ST7735_RED : ST7735_BLUE;
}

int main() {
  stdio_init_all();

  st7735_init();

  sleep_ms(1000);
  puts("estoy quedarse");

  init(); /* gosh i should namespace base engine */

  puts("setting legend");
  legend_doodle_set('p', 
    "................\n"
    "................\n"
    "................\n"
    ".......0........\n"
    ".....00.000.....\n"
    "....0.....00....\n"
    "....0.0.0..0....\n"
    "....0......0....\n"
    "....0......0....\n"
    "....00....0.....\n"
    "......00000.....\n"
    "......0...0.....\n"
    "....000...000...\n"
    "................\n"
    "................\n"
    "................\n"
  );
  legend_doodle_set('w', 
    "0000000000000000\n"
    "0000000000000000\n"
    "0000000000000000\n"
    "0000000000000000\n"
    "0000000000000000\n"
    "0000000000000000\n"
    "0000000000000000\n"
    "0000000000000000\n"
    "0000000000000000\n"
    "0000000000000000\n"
    "0000000000000000\n"
    "0000000000000000\n"
    "0000000000000000\n"
    "0000000000000000\n"
    "0000000000000000\n"
    "0000000000000000\n"
  );
  puts("legend set");
  legend_prepare();
  puts("legend prepared");

  map_set("w.w\n"  
          ".p.\n"  
          "w.w");
  puts("map set");

  puts("rendering");
  render((uint16_t *)screen);

  st7735_fill(pixel);

  while(1) puts("uhhh"), sleep_ms(500);
  return 0;
}
