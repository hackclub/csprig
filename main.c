#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "pico/util/queue.h"
#include "hardware/spi.h"
#include "ST7735_TFT.h"

#define ARR_LEN(arr) (sizeof(arr) / sizeof(arr[0]))

#if 1
  #define dbg puts
  #define dbgf printf
#else
  #define dbg(...) ;
  #define dbgf(...) ;
#endif

static void oom(void) { puts("oom!"); }
#include "base_engine.c"

/* our input handling is a bit onerous, but unlike simpler solutions ... it works.
 * you see, input polling had too many missed keypresses (go figure!)
 * but IRQ, the canonical solution, had too many false positives
 * (wouldn't be surprised if our unsoldered buttons had something to do with this?)
 *
 * so in order to filter out the fake keypresses IRQ was registering, we now have
 * our audio thread poll for those and use the timing between them to filter out
 * fake ones (if the button is being held down for 5ms, that's probably just noise!)
 * 
 * so there's a lot of message passing here, unfortunately.
 * IRQ has to pass messages to the audio thread, which has to pass messages
 * to the main thread running your kaluma code. yikes!
 *
 * this is done by way of two FIFOs. you can imagine one as being "lower level"
 * than the other. the lower level one would be button_fifo; that has all of the
 * events from IRQ, some of which will be false positives. 
 *
 *                IRQ -> AUDIO THREAD -> MAIN THREAD
 *     the first arrow is button_fifo, the second is press_fifo!
 *
 */

#define PRESS_FIFO_LENGTH (32)
queue_t press_fifo;
typedef struct { uint8_t pin; } PressAction;

#define BUTTON_FIFO_LENGTH (32)
queue_t button_fifo;
typedef enum {
  ButtonActionKind_Down,
  ButtonActionKind_Up,
} ButtonActionKind;
typedef struct {
  ButtonActionKind kind;
  absolute_time_t time;
  uint8_t pindex;
} ButtonAction;

typedef struct {
  absolute_time_t last_up, last_down;
  uint8_t edge;
} ButtonState;

uint button_pins[] = {  5,  7,  6,  8, 12, 14, 13, 15 };

void gpio_callback(uint gpio, uint32_t events) {
  puts("main.c:gpio_callback");

  for (int i = 0; i < ARR_LEN(button_pins); i++)
    if (button_pins[i] == gpio) {

      dbg("is it possible? conceivable?");
      // queue_add_blocking
      if (!queue_try_add(&button_fifo, &(ButtonAction) {
        .time = get_absolute_time(),
        .kind = (events == GPIO_IRQ_EDGE_RISE) ? ButtonActionKind_Up : ButtonActionKind_Down,
        .pindex = i,
      })) puts("dropping button press :(");

      break;
    }
}

static ButtonState button_states[ARR_LEN(button_pins)] = {0};
static void button_init(void) {
  puts("main.c:button_init");

  queue_init(&button_fifo, sizeof(ButtonAction), BUTTON_FIFO_LENGTH);
  queue_init(& press_fifo, sizeof(PressAction ),  PRESS_FIFO_LENGTH);

  for (int i = 0; i < ARR_LEN(button_pins); i++) {
    ButtonState *bs = button_states + i;
    bs->edge = 1;
    bs->last_up = bs->last_down = get_absolute_time();

    gpio_set_dir(button_pins[i], GPIO_IN);
    gpio_pull_up(button_pins[i]);
    // gpio_set_input_hysteresis_enabled(button_pins[i], 1);
    // gpio_set_slew_rate(button_pins[i], GPIO_SLEW_RATE_SLOW);
    // gpio_disable_pulls(button_pins[i]);

    gpio_set_irq_enabled_with_callback(
      button_pins[i],
      GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL,
      true,
      &gpio_callback
    );
  }
}

static void button_poll(void) {
  puts("main.c:button_poll");

  ButtonAction p = {0};
  if (queue_try_remove(&button_fifo, &p)) {
    puts("main.c:button_init - button fifo remove");

    ButtonState *bs = button_states + p.pindex;
    switch (p.kind) {
      case ButtonActionKind_Down: bs->last_down = p.time; break;
      case ButtonActionKind_Up:   bs->last_up   = p.time; break;
    }
  }

  for (int i = 0; i < ARR_LEN(button_pins); i++) {
    puts("main.c:button_init - button pins poll");

    ButtonState *bs = button_states + i;

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

      puts("main.c:button_init - press fifo push");

      if (!queue_try_add(&press_fifo, &(PressAction) { .pin = button_pins[i] }))
        dbg("press_fifo full!");
    }
  }
}

static void core1_entry(void) {
  puts("main.c:core1_entry");

  button_init();

  while (1)
    button_poll();
}

int main() {
  stdio_init_all();

  // multicore_reset_core1();
  multicore_launch_core1(core1_entry);

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
    PressAction p = {0};
    // if (queue_remove_blocking(&press_fifo, &p), 1) {
    if (queue_try_remove(&press_fifo, &p)) {
      if (p.pin == 8) map_move(map_get_first('p'),  1,  0);
      if (p.pin == 6) map_move(map_get_first('p'), -1,  0);
      if (p.pin == 7) map_move(map_get_first('p'),  0,  1);
      if (p.pin == 5) map_move(map_get_first('p'),  0, -1);
    }

    uint16_t screen[160 * 128] = {0};
    render(screen);
    st7735_fill(screen);
  }
  return 0;
}
