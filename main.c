#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "ST7735_TFT.h"

#define ARR_LEN(arr) (sizeof(arr) / sizeof(arr[0]))

#if 0
  #define dbg puts
  #define dbgf printf
#else
  #define printf(...) ;
  #define dbg(...) ;
  #define dbgf(...) ;
#endif

static void oom(void) { puts("oom!"); }
#include "base_engine.c"

typedef struct {
  absolute_time_t last_up, last_down;
  uint8_t last_state, edge;
} ButtonState;
uint button_pins[] = {  5,  7,  6,  8, 12, 14, 13, 15 };
static ButtonState button_states[ARR_LEN(button_pins)] = {0};

static void button_init(void) {
  for (int i = 0; i < ARR_LEN(button_pins); i++) {
    ButtonState *bs = button_states + i;
    bs->edge = 1;
    bs->last_up = bs->last_down = get_absolute_time();

    gpio_set_dir(button_pins[i], GPIO_IN);
    gpio_pull_up(button_pins[i]);
    // gpio_set_input_hysteresis_enabled(button_pins[i], 1);
    // gpio_set_slew_rate(button_pins[i], GPIO_SLEW_RATE_SLOW);
    // gpio_disable_pulls(button_pins[i]);
  }
}

static void button_poll(void) {
  for (int i = 0; i < ARR_LEN(button_pins); i++) {
    ButtonState *bs = button_states + i;

    uint8_t state = gpio_get(button_pins[i]);
    if (state != bs->last_state) {
      bs->last_state = state;

      if (state) bs->last_up   = get_absolute_time();
      else       bs->last_down = get_absolute_time();
    }

    absolute_time_t when_up_cooldown = delayed_by_ms(bs->last_up  , 70);
    absolute_time_t light_when_start = delayed_by_ms(bs->last_down, 20);
    absolute_time_t light_when_stop  = delayed_by_ms(bs->last_down, 40);

    uint8_t on = absolute_time_diff_us(get_absolute_time(), light_when_start) < 0 &&
                 absolute_time_diff_us(get_absolute_time(), light_when_stop ) > 0 &&
                 absolute_time_diff_us(get_absolute_time(), when_up_cooldown) < 0  ;

    // if (on) dbg("BRUH");

    if (!on && !bs->edge) bs->edge = 1;
    if (on && bs->edge) {
      bs->edge = 0;

      if (button_pins[i] == 8) map_move(map_get_first('p'),  1,  0);
      if (button_pins[i] == 6) map_move(map_get_first('p'), -1,  0);
      if (button_pins[i] == 7) map_move(map_get_first('p'),  0,  1);
      if (button_pins[i] == 5) map_move(map_get_first('p'),  0, -1);
    }
  }
}

int main() {
  stdio_init_all();

  // multicore_reset_core1();
  // multicore_launch_core1(core1_entry);
  button_init();

  init(); /* gosh i should namespace base engine */

  {
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
    legend_prepare();

    solids_push('w');
    solids_push('p');

    map_set("w.w\n"  
            ".p.\n"  
            "w.w");
  }

  st7735_init();


  while(1) {
    button_poll();

    uint16_t screen[160 * 128] = {0};
    render(screen);
    st7735_fill(screen);
  }
  return 0;
}
