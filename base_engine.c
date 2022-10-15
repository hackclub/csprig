#include <stdint.h>
#include "font.h"

#ifdef __wasm__

  #define WASM_EXPORT __attribute__((visibility("default")))
  
  extern void putchar(char c);
  extern void putint(int i);
  extern void oom();
  extern unsigned char __heap_base;
  #define PAGE_SIZE (1 << 16)

  typedef struct { uint8_t rgba[4]; } Color;

  #define color16(r, g, b) ((Color) { r, g, b, 255 })

#else

  static uint16_t color16(uint8_t g, uint8_t r, uint8_t b) {
    // return ((r & 0xf8) << 8) + ((g & 0xfc) << 3) + (b >> 3);
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
  }

  #define WASM_EXPORT static

  typedef uint16_t Color;

#endif

#define ARR_COUNT(arr) (sizeof(arr) / sizeof(arr[0]))

static float fabsf(float x) { return (x < 0) ? -x : x; }
static float signf(float f) {
  if (f < 0) return -1;
  if (f > 0) return 1;
  return 0;
}

#define TEXT_CHARS_MAX_X (20)
#define TEXT_CHARS_MAX_Y (16)

#define SPRITE_SIZE (16)
typedef struct {
  uint16_t pixels[SPRITE_SIZE][SPRITE_SIZE];
  uint8_t     lit[SPRITE_SIZE*SPRITE_SIZE / 8];
} Doodle;

typedef struct Sprite Sprite;
struct Sprite {
  Sprite *next;
  int x, y, dx, dy;
  char kind;
};
static void map_free(Sprite *s);
static Sprite *map_alloc(void);
static uint8_t map_active(Sprite *s, uint32_t generation);

typedef struct { Sprite *sprite; int x, y; uint8_t dirty; } MapIter;

#define PER_CHAR (255)
#define PER_DOODLE (40)
#define SPRITE_COUNT (1 << 8)

#define MAP_SIZE_X (20)
#define MAP_SIZE_Y (20)
#define SCREEN_SIZE_X (160)
#define SCREEN_SIZE_Y (128)
typedef struct {
  Color palette[PER_CHAR];
  uint8_t lit[SCREEN_SIZE_Y * SCREEN_SIZE_X / 8];
  
  int scale;

  /* some SoA v. AoS shit goin on here man */
  int doodle_index_count;
  uint8_t legend_doodled[PER_CHAR]; /* PER_CHAR = because it's used for assigning indices */
  Doodle legend[PER_DOODLE];
  Doodle legend_resized[PER_DOODLE];

} State_Render;

typedef struct {
  int width, height;

  char  text_char [TEXT_CHARS_MAX_Y][TEXT_CHARS_MAX_X];
  Color text_color[TEXT_CHARS_MAX_Y][TEXT_CHARS_MAX_X];

  uint8_t char_to_index[PER_CHAR];

  uint8_t solid[PER_CHAR];
  uint8_t push_table[PER_DOODLE * PER_DOODLE / 8];

  uint8_t  sprite_slot_active[SPRITE_COUNT];
  uint32_t sprite_slot_generation[SPRITE_COUNT];
  Sprite sprite_pool[SPRITE_COUNT];
  size_t sprite_pool_head;
  /* points into sprite_pool */
  Sprite *map[MAP_SIZE_X][MAP_SIZE_Y];

  int tile_size; /* how small tiles have to be to fit map on screen */
  char background_sprite;

  State_Render *render;

  /* this is honestly probably heap abuse and most kaluma uses of it should
     probably use stack memory instead. It started out as a way to pass
     strings across the WASM <-> JS barrier. */
  char temp_str_mem[(1 << 12)];
  MapIter temp_MapIter_mem;
} State;
static State *state = 0;

/* almost makes ya wish for generic data structures dont it :shushing_face:

   this was implemented to cut down on RAM usage before I discovered you can
   control how much RAM gets handed to JS in targets/rp2/target.cmake
   */
static void render_lit_write(int x, int y) {
  int i = x*SCREEN_SIZE_Y + y;
  state->render->lit[i/8] |= 1 << (i % 8);
}
static uint8_t render_lit_read(int x, int y) {
  int i = x*SCREEN_SIZE_Y + y;
  int q = 1 << (i % 8);
  return !!(state->render->lit[i/8] & q);
}

static void doodle_lit_write(Doodle *d, int x, int y) {
  int i = y*SPRITE_SIZE + x;
  d->lit[i/8] |= 1 << (i % 8);
}
static uint8_t doodle_lit_read(Doodle *d, int x, int y) {
  int i = y*SPRITE_SIZE + x;
  int q = 1 << (i % 8);
  return !!(d->lit[i/8] & q);
}

static void push_table_write(char x_char, char y_char) {
  int x = state->char_to_index[(int) x_char];
  int y = state->char_to_index[(int) y_char];

  int i = y*PER_DOODLE + x;
  state->push_table[i/8] |= 1 << (i % 8);
}
static uint8_t push_table_read(char x_char, char y_char) {
  int x = state->char_to_index[(int) x_char];
  int y = state->char_to_index[(int) y_char];

  int i = y*PER_DOODLE + x;
  int q = 1 << (i % 8);
  return !!(state->push_table[i/8] & q);
}

WASM_EXPORT void text_add(char *str, Color color, int x, int y) {
  for (; *str; str++, x++)
    state->text_char [y][x] = *str,
    state->text_color[y][x] = color;
}

WASM_EXPORT void text_clear(void) {
  __builtin_memset(state->text_char , 0, sizeof(state->text_char ));
  __builtin_memset(state->text_color, 0, sizeof(state->text_color));
}

WASM_EXPORT void init(void) {
#ifdef __wasm__
  int mem_needed = sizeof(State)/PAGE_SIZE;

  int delta = mem_needed - __builtin_wasm_memory_size(0) + 2;
  if (delta > 0) __builtin_wasm_memory_grow(0, delta);
  state = (void *)&__heap_base;

#else
  static State _state = {0};
  state = &_state;

  __builtin_memset(state, 0, sizeof(State));

  static State_Render _state_render = {0};
  state->render = &_state_render;
#endif

  /* -- error handling for when state is dynamically allocated -- */ 
  // if (state->render == 0) {
  //   state->render = malloc(sizeof(State_Render));
  //   printf("sizeof(State_Render) = %d, addr: %d\n", sizeof(State_Render), (unsigned int)state->render);
  // }
  // if (state->render == 0) puts("couldn't alloc state");

  __builtin_memset(state->render, 0, sizeof(State_Render));


  // Grey
  state->render->palette['0'] = color16(  0,   0,   0);
  state->render->palette['L'] = color16( 73,  80,  87);
  state->render->palette['1'] = color16(145, 151, 156);
  state->render->palette['2'] = color16(248, 249, 250);
  state->render->palette['3'] = color16(235,  44,  71);
  state->render->palette['C'] = color16(139,  65,  46);
  state->render->palette['7'] = color16( 25, 177, 248);
  state->render->palette['5'] = color16( 19,  21, 224);
  state->render->palette['6'] = color16(254, 230,  16);
  state->render->palette['F'] = color16(149, 140,  50);
  state->render->palette['4'] = color16( 45, 225,  62);
  state->render->palette['D'] = color16( 29, 148,  16);
  state->render->palette['8'] = color16(245, 109, 187);
  state->render->palette['H'] = color16(170,  58, 197);
  state->render->palette['9'] = color16(245, 113,  23);
  state->render->palette['.'] = color16(  0,   0,   0);

  puts("hi!");
}

WASM_EXPORT char *temp_str_mem(void) {
  __builtin_memset(&state->temp_str_mem, 0, sizeof(state->temp_str_mem));
  return state->temp_str_mem;
}

/* call this when the map changes size, or when the legend changes */
static void render_resize_legend(void) {
  __builtin_memset(&state->render->legend_resized, 0, sizeof(state->render->legend_resized));

  /* how big do our tiles need to be to fit them all snugly on screen? */
  float min_tile_x = SCREEN_SIZE_X / state->width;
  float min_tile_y = SCREEN_SIZE_Y / state->height;
  state->tile_size = (min_tile_x < min_tile_y) ? min_tile_x : min_tile_y;
  if (state->tile_size > 16)
    state->tile_size = 16;

  for (int c = 0; c < PER_CHAR; c++) {
    if (!state->render->legend_doodled[c]) continue;
    int i = state->char_to_index[c];

    Doodle *rd = state->render->legend_resized + i;
    Doodle *od = state->render->legend + i;

    for (int y = 0; y < 16; y++)
      for (int x = 0; x < 16; x++) {
        int rx = (float) x / 16.0f * state->tile_size;
        int ry = (float) y / 16.0f * state->tile_size;

        if (!doodle_lit_read(od, x, y)) continue;
        rd->pixels[ry][rx] = od->pixels[y][x];
        if (doodle_lit_read(od, x, y)) doodle_lit_write(rd, rx, ry);
      }
  }
}

static void render_blit_sprite(Color *screen, int sx, int sy, char kind) {
  int scale = state->render->scale;
  Doodle *d = state->render->legend_resized + state->char_to_index[(int)kind];

  for (int x = 0; x < state->tile_size; x++)
    for (int y = 0; y < state->tile_size; y++) {

      if (!doodle_lit_read(d, x, y)) continue;
      for (  int ox = 0; ox < scale; ox++)
        for (int oy = 0; oy < scale; oy++) {
          int px = ox + sx + scale*x;
          int py = oy + sy + scale*y;
          if (render_lit_read(px, py)) continue;

          render_lit_write(px, py);

          int i = (ox + sx + scale*(state->tile_size - 1 - x)) * SCREEN_SIZE_Y + py;
          screen[i] = d->pixels[y][x];
        }
    }
}

static void render_char(Color *screen, char c, Color color, int sx, int sy) {
  for (int y = 0; y < 8; y++) {
    uint8_t bits = font_pixels[c*8 + y];
    for (int x = 0; x < 8; x++)
      if ((bits >> (7-x)) & 1) {
        int i = (SCREEN_SIZE_X - (sx+x) - 1)*SCREEN_SIZE_Y + (sy+y);
        screen[i] = color;
      }
  }
}

WASM_EXPORT void render_set_background(char kind) {
  state->background_sprite = kind;
}

WASM_EXPORT uint8_t map_get_grid(MapIter *m);
WASM_EXPORT void render(Color *screen) {
  __builtin_memset(&state->render->lit, 0, sizeof(state->render->lit));

  int scale;
  {
    int scale_x = SCREEN_SIZE_X/(state->width*16);
    int scale_y = SCREEN_SIZE_Y/(state->height*16);

    scale = (scale_x < scale_y) ? scale_x : scale_y;
    if (scale < 1) scale = 1;

    state->render->scale = scale;
  }
  int size = state->tile_size*scale;

  int pixel_width = state->width*size;
  int pixel_height = state->height*size;

  int ox = (SCREEN_SIZE_X - pixel_width)/2;
  int oy = (SCREEN_SIZE_Y - pixel_height)/2;


  puts("render: clear to white");
  for (int y = oy; y < oy+pixel_height; y++)
    for (int x = ox; x < ox+pixel_width; x++) {
      int i = (SCREEN_SIZE_X - x - 1)*SCREEN_SIZE_Y + y;
      screen[i] = color16(255, 255, 255);
    }

  puts("render: grid");
  MapIter m = {0};
  while (map_get_grid(&m))
    render_blit_sprite(screen,
                       ox + size*(state->width - 1 - m.sprite->x),
                       oy + size*m.sprite->y,
                       m.sprite->kind);

  puts("render: bg");
  if (state->background_sprite)
    for (int y = 0; y < state->height; y++)
      for (int x = 0; x < state->width; x++)
        render_blit_sprite(screen,
                           ox + size*x,
                           oy + size*y,
                           state->background_sprite);

  puts("render: text");
  for (int y = 0; y < TEXT_CHARS_MAX_Y; y++)
    for (int x = 0; x < TEXT_CHARS_MAX_X; x++) {
      char c = state->text_char[y][x];
      if (c) render_char(screen, c, state->text_color[y][x], x*8, y*8);
    }

  puts("render: done");
}

static Sprite *map_alloc(void) {
  for (int i = 0; i < SPRITE_COUNT; i++) {
    if (state->sprite_slot_active[i] == 0) {
      state->sprite_slot_active[i] = 1;
      return state->sprite_pool + i;
    }
  }
  oom();
  return 0;
}
static void map_free(Sprite *s) {
  memset(s, 0, sizeof(Sprite));
  size_t i = s - state->sprite_pool;
  state->sprite_slot_active    [i] = 0;
  state->sprite_slot_generation[i]++;
}
static uint8_t map_active(Sprite *s, uint32_t generation) {
  if (s == NULL) return 0;
  size_t i = s - state->sprite_pool;
  return state->sprite_slot_generation[i] == generation;
}
WASM_EXPORT uint32_t sprite_generation(Sprite *s) {
  size_t i = s - state->sprite_pool;
  return state->sprite_slot_generation[i];
}

/* removes the canonical reference to this sprite from the spatial grid.
   it is your responsibility to subsequently free the sprite. */
static void map_pluck(Sprite *s) {
  Sprite *top = state->map[s->x][s->y];
  // assert(top != 0);

  if (top == s) {
    state->map[s->x][s->y] = s->next;
    return;
  }

  for (Sprite *t = top; t->next; t = t->next) {
    if (t->next == s) {
      t->next = s->next;
      return;
    }
  }

  state->map[s->x][s->y] = 0;
}

/* inserts pointer to sprite into the spritestack at this x and y,
 * such that rendering z-order is preserved
 * (as expressed in order of legend_doodle_set calls)
 * 
 * see map_plop about caller's responsibility */
static void map_plop(Sprite *sprite) {
  Sprite *top = state->map[sprite->x][sprite->y];
  printf("top at (%d, %d) is: %ld/%d\n", sprite->x, sprite->y, (long int)(top - state->sprite_pool), SPRITE_COUNT);

  /* we want the sprite with the lowest z-order on the top. */

  #define Z_ORDER(sprite) (state->char_to_index[(int)(sprite)->kind])
  if (top == 0 || Z_ORDER(top) >= Z_ORDER(sprite)) {
    sprite->next = state->map[sprite->x][sprite->y];
    state->map[sprite->x][sprite->y] = sprite;
    puts("top's me, early ret");
    return;
  }

  Sprite *insert_after = top;
  while (insert_after->next && Z_ORDER(insert_after->next) < Z_ORDER(sprite))
    insert_after = insert_after->next;
  #undef Z_ORDER

  printf("insert_after's : %ld/%d\n", (long int)(insert_after - state->sprite_pool), SPRITE_COUNT);
  if (insert_after->next) printf("insert_after->next's : %ld/%d\n", (long int)(insert_after->next - state->sprite_pool), SPRITE_COUNT);
  printf("sprite's: %ld/%d ofc\n", (long int)(sprite - state->sprite_pool), SPRITE_COUNT);
  sprite->next = insert_after->next;
  insert_after->next = sprite;
}


WASM_EXPORT Sprite *map_add(int x, int y, char kind) {
  Sprite *s = map_alloc();
  printf("alloc ret: %ld/%d\n", (long int)(s - state->sprite_pool), SPRITE_COUNT);
  *s = (Sprite) { .x = x, .y = y, .kind = kind };
  puts("assigned to that mf");
  map_plop(s);
  puts("stuck 'em on map, returning now");
  return s;
}

WASM_EXPORT void map_set(char *str) {
  __builtin_memset(&state->map, 0, sizeof(state->map));

  for (int i = 0; i < SPRITE_COUNT; i++)
    map_free(state->sprite_pool + i);

  int tx = 0, ty = 0;
  do {
    switch (*str) {
      case '\n': ty++, tx = 0; break;
      case  '.': tx++;         break;
      case '\0':               break;
      default: {
        state->map[tx][ty] = map_alloc();
        *state->map[tx][ty] = (Sprite) { .x = tx, .y = ty, .kind = *str };
        tx++;
      } break;
    }
  } while (*str++);
  state->width = tx;
  state->height = ty+1;

  render_resize_legend();
}

WASM_EXPORT int map_width(void) { return state->width; }
WASM_EXPORT int map_height(void) { return state->height; }

WASM_EXPORT Sprite *map_get_first(char kind) {
  for (int y = 0; y < state->height; y++)
    for (int x = 0; x < state->width; x++) {
      Sprite *top = state->map[x][y];

      for (; top; top = top->next)
        if (top->kind == kind)
          return top;
    }
  return 0;
}

WASM_EXPORT uint8_t map_get_grid(MapIter *m) {
  if (m->sprite && m->sprite->next) {
    m->sprite = m->sprite->next;
    return 1;
  }

  while (1) {
    if (!m->dirty)
      m->dirty = 1;
    else {
      m->x++;
      if (m->x >= state->width) {
        m->x = 0;
        m->y++;
        if (m->y >= state->height) return 0;
      }
    }

    if (state->map[m->x][m->y]) {
      m->sprite = state->map[m->x][m->y];
      return 1;
    }
  }
}

/* you could easily do this in JS, but I suspect there is a
 * great perf benefit to avoiding all of the calls back and forth
 */
WASM_EXPORT uint8_t map_get_all(MapIter *m, char kind) {
  while (map_get_grid(m))
    if (m->sprite->kind == kind)
      return 1;
  return 0;
}

WASM_EXPORT uint8_t map_tiles_with(MapIter *m, char *kinds) {
  char kinds_needed[255] = {0};
  int kinds_len = 0;
  for (; *kinds; kinds++) {
    int c = (int)*kinds;
    
    /* filters out duplicates! */
    if (kinds_needed[c] != 0) continue;

    kinds_len++;
    kinds_needed[c] = 1;
  }

  while (1) {
    if (!m->dirty)
      m->dirty = 1;
    else {
      m->x++;
      if (m->x >= state->width) {
        m->x = 0;
        m->y++;
        if (m->y >= state->height) return 0;
      }
    }

    if (state->map[m->x][m->y]) {
      int kinds_found = 0;

      for (Sprite *s = state->map[m->x][m->y]; s; s = s->next)
        kinds_found += kinds_needed[(int)s->kind];

      if (kinds_found == kinds_len) {
        m->sprite = state->map[m->x][m->y];
        return 1;
      }
    }
  }
}

WASM_EXPORT void map_remove(Sprite *s) {
  map_pluck(s);
  map_free(s);
}

/* removes all of the sprites at a given location */
WASM_EXPORT void map_drill(int x, int y) {
  Sprite *top = state->map[x][y];
  for (; top; top = top->next) {
    map_free(top);
  }
  state->map[x][y] = 0;
}


/* move a sprite by one unit along the specified axis
   returns how much it was moved on that axis (may be 0 if path obstructed) */
static int _map_move(Sprite *s, int big_dx, int big_dy) {
  int dx = signf(big_dx);
  int dy = signf(big_dy);

  /* expected input: x and y aren't both 0, either x or y is non-zero (not both) */
  if (dx == 0 && dy == 0) return 0;

  int prog = 0;
  int goal = (fabsf(big_dx) > fabsf(big_dy)) ? big_dx : big_dy;

  while (prog != goal) {
    int x = s->x+dx;
    int y = s->y+dy;

    /* no moving off of the map! */
    if (x < 0) return prog;
    if (y < 0) return prog;
    if (x >= state->width) return prog;
    if (y >= state->height) return prog;

    if (state->solid[(int)s->kind]) {
      /* no moving into a solid! */
      Sprite *n = state->map[x][y];

      for (; n; n = n->next)
        if (state->solid[(int)n->kind]) {
          /* unless you can push them out of the way ig */
          if (push_table_read(s->kind, n->kind)) {
            if (_map_move(n, dx, dy) == 0)
              return prog;
          }
          else
            return prog;
        }
    }

    map_pluck(s);
    s->x += dx;
    s->y += dy;
    map_plop(s);
    prog += (fabsf(dx) > fabsf(dy)) ? dx : dy;
  }

  return prog;
}

WASM_EXPORT void map_move(Sprite *s, int big_dx, int big_dy) {
  int moved = _map_move(s, big_dx, big_dy);
  if (big_dx != 0) s->dx = moved;
  else             s->dy = moved;
}

#ifdef __wasm__
WASM_EXPORT int sprite_get_x(Sprite *s) { return s->x; }
WASM_EXPORT int sprite_get_y(Sprite *s) { return s->y; }
WASM_EXPORT int sprite_get_dx(Sprite *s) { return s->dx; }
WASM_EXPORT int sprite_get_dy(Sprite *s) { return s->dy; }
WASM_EXPORT char sprite_get_kind(Sprite *s) { return s->kind; }
WASM_EXPORT void sprite_set_kind(Sprite *s, char kind) { s->kind = kind; }

WASM_EXPORT void MapIter_position(MapIter *m, int x, int y) { m->x = x; m->y = y; }

WASM_EXPORT MapIter *temp_MapIter_mem(void) {
  __builtin_memset(&state->temp_MapIter_mem, 0, sizeof(state->temp_MapIter_mem));
  return &state->temp_MapIter_mem;
}

#endif

WASM_EXPORT void map_clear_deltas(void) {
  for (int y = 0; y < state->height; y++)
    for (int x = 0; x < state->width; x++) {
      Sprite *top = state->map[x][y];

      for (; top; top = top->next)
        top->dx = top->dy = 0;
    }
}

WASM_EXPORT void solids_push(char c) {
  state->solid[(int)c] = 1;
}
WASM_EXPORT void solids_clear(void) {
  __builtin_memset(&state->solid, 0, sizeof(state->solid));
}

WASM_EXPORT void legend_doodle_set(char kind, char *str) {

  int index = state->char_to_index[(int)kind];

  /* we don't want to increment if index 0 has already been assigned and this is it */
  if (index == 0 && !state->render->legend_doodled[(int)kind]) {

    if (state->render->doodle_index_count >= PER_DOODLE) puts("max doodle count exceeded.");
    index = state->render->doodle_index_count++;
  }
  state->char_to_index[(int)kind] = index;

  state->render->legend_doodled[(int)kind] = 1;
  Doodle *d = state->render->legend + index;

  int px = 0, py = 0;
  do {
    switch (*str) {
      case '\n': py++, px = 0; break;
      case  '.': px++;         break;
      case '\0':               break;
      default: {
        d->pixels[py][px] = state->render->palette[(int)*str];
        doodle_lit_write(d, px, py);
        px++;
      } break;
    }
  } while (*str++);
}
WASM_EXPORT void legend_clear(void) {
  state->render->doodle_index_count = 0;
  __builtin_memset(&state->render->legend, 0, sizeof(state->render->legend));
  __builtin_memset(&state->render->legend_resized, 0, sizeof(state->render->legend_resized));
  __builtin_memset(&state->render->legend_doodled, 0, sizeof(state->render->legend_doodled));
  __builtin_memset(&state->char_to_index, 0, sizeof(state->char_to_index));
}
WASM_EXPORT void legend_prepare(void) {
  if (state->width && state->height)
    render_resize_legend();
}

WASM_EXPORT void push_table_set(char pusher, char pushes) {
  push_table_write(pusher, pushes);
}
WASM_EXPORT void push_table_clear(void) {
  __builtin_memset(&state->push_table, 0, sizeof(state->push_table));
}

#if 0
void text_add(char *str, int x, int y, uint32_t color);
Sprite *sprite_add(int x, int y, char kind);
Sprite *sprite_next(Sprite *s);
int sprite_get_x(Sprite *s);
int sprite_get_y(Sprite *s);
char sprite_get_kind(Sprite *s);
void sprite_set_x(Sprite *s, int x);
void sprite_set_y(Sprite *s, int y);
void sprite_set_kind(Sprite *s, char kind);

void spritestack_clear(int x, int y);

void solids_push(char c);
void solids_clear();

// setPushables, 

setBackground

map: _makeTag(text => text),
bitmap: _makeTag(text => text),
tune: _makeTag(text => text),
#endif
