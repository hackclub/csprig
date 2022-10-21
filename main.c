#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "ST7735_TFT.h"

#define ARR_LEN(arr) (sizeof(arr) / sizeof(arr[0]))

#include "jerryscript.h"
static struct {
  jerry_value_t press_cb, frame_cb;
} spade_state = {0};

#define yell puts
#if 1
  #define dbg puts
  #define dbgf printf
#else
  #define dbg(...) ;
  #define dbgf(...) ;
#endif

static void oom(void) { puts("oom!"); }
#define puts(...) ;
#include "base_engine.c"
#undef puts

#include "module_native.c"

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

JERRYXX_FUN(console_log) {
  jerryxx_print_value(JERRYXX_GET_ARG(0));
  return jerry_create_undefined();
}

static jerry_value_t
print_handler (const jerry_value_t function_object,
               const jerry_value_t function_this,
               const jerry_value_t arguments[],
               const jerry_length_t argument_count)
{
  /* No arguments are used in this example */
  /* Print out a static string */
  puts ("Print handler was called");

  /* Return an "undefined" value to the JavaScript engine */
  return jerry_create_undefined ();
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
  {
    puts("WHAT THE ACTUAL FUCK");
  const jerry_char_t script[] = "native.solids_push('w'); native.solids_push('p'); native.addSprite(1, 1, 'w');";
  const jerry_length_t script_size = sizeof (script) - 1;

  /* Initialize engine */
  jerry_init (JERRY_INIT_MEM_STATS);

  /* add shit to global scpoe */
  {
    jerry_value_t global_object = jerry_get_global_object ();

    /* add the "console" module to the JavaScript global object */
    {
      jerry_value_t console_obj = jerry_create_object ();

      jerryxx_set_property_function(console_obj, "log", console_log);

      jerry_value_t prop_console = jerry_create_string ((const jerry_char_t *) "console");
      jerry_release_value(jerry_set_property(global_object, prop_console, console_obj));
      jerry_release_value (prop_console);

      /* Release all jerry_value_t-s */
      jerry_release_value (console_obj);
    }

    /* add the "native" module to the JavaScript global object */
    {
      jerry_value_t native_obj = jerry_create_object ();

      module_native_init(native_obj);

      jerry_value_t prop_native = jerry_create_string ((const jerry_char_t *) "native");
      jerry_release_value(jerry_set_property(global_object, prop_native, native_obj));
      jerry_release_value (prop_native);

      /* Release all jerry_value_t-s */
      jerry_release_value (native_obj);
    }

    jerry_release_value(global_object);
  }

  /* Setup Global scope code */
  puts("calling jerry parse");
  jerry_value_t parsed_code = jerry_parse (
    (jerry_char_t *)"src", sizeof("src")-1,
    script, script_size,
    JERRY_PARSE_STRICT_MODE
  );
  puts("returned jerry parse");

  if (jerry_value_is_error (parsed_code)) {
    yell("couldn't parse :(");
    jerryxx_print_error(parsed_code, 1);
    // abort();
  }

  /* Execute the parsed source code in the Global scope */
  jerry_value_t ret_value = jerry_run (parsed_code);

  if (jerry_value_is_error (ret_value)) {
    yell("couldn't run :(");
    jerryxx_print_error(ret_value, 1);
    // abort();
  }

  /* Returned value must be freed */
  jerry_release_value (ret_value);

  /* Parsed source code must be freed */
  jerry_release_value (parsed_code);

  /* Cleanup engine */
  jerry_cleanup ();
  }

           if (button_pins[i] == 8) map_move(map_get_first('p'),  1,  0);
      else if (button_pins[i] == 6) map_move(map_get_first('p'), -1,  0);
      else if (button_pins[i] == 7) map_move(map_get_first('p'),  0,  1);
      else if (button_pins[i] == 5) map_move(map_get_first('p'),  0, -1);
    }
  }
}

static void map_free_cb(Sprite *s) {
  (void *)s;
}

int main() {
  stdio_init_all();


  // multicore_reset_core1();
  // multicore_launch_core1(core1_entry);
  button_init();

  init(map_free_cb); /* gosh i should namespace base engine */

  puts("HERE GOES NOTHING MOTHERFUCKERS");

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

    map_set("..w\n"  
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
