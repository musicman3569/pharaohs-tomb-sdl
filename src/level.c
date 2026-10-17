#include "level.h"

#include <string.h>

#include "graph3.h"
#include "tp.h"

/* What DrawLevelRow does for each tile character. Decoded from the comparison chain at
 * 1000:0A58; sprite set/frame numbers are the DS:0000 sprite slots (see docs/notes.md). */
enum {
    T_MONSTER = 1 << 0,     /* draw this level's monster (DrawLevelMonster), frame 1 */
    T_RANDOM_FRAME = 1 << 1,/* draw a random frame of `set` (Random(4)) */
    T_RANDOM_MAP18 = 1 << 2,/* map code 18 with probability 1/10 (Random(10) = 1) */
    T_PLAYER = 1 << 3,      /* player start position */
    T_GLINT = 1 << 4,       /* add to the gem glint list */
    T_DRAW_UP2 = 1 << 5,    /* draw again 2 px higher */
};

typedef struct {
    char ch;
    unsigned char map;  /* tile code written to the map, 0 = none */
    signed char set, frame; /* sprite to draw, set < 0 = none */
    unsigned char flags;
    /* SpawnEnemy(type, x, y + y_off, speed, dx, dy); type 0 = no enemy */
    signed char type, speed, dx, dy, y_off;
} tile_t;

/* TILE(char, map code, sprite set, frame, flags)
 * ENEMY(char, sprite set, frame, flags, type, speed, dx, dy, y offset) */
#define TILE(c, map, set, frame, flags) {c, map, set, frame, flags, 0, 0, 0, 0, 0}
#define ENEMY(c, set, frame, flags, type, speed, dx, dy, yoff) \
    {c, 0, set, frame, flags, type, speed, dx, dy, yoff}

static const tile_t tiles[] = {
    TILE('.', 0x0A, 4, 0, 0),
    TILE('>', 0x0B, -1, 0, 0),
    TILE('~', 0, 5, 0, 0), TILE('!', 0, 5, 1, 0), TILE('@', 0, 5, 2, 0), TILE('#', 0, 5, 3, 0),
    TILE('$', 0, 6, 0, 0), TILE('%', 0, 6, 1, 0), TILE('^', 0, 6, 2, 0), TILE('&', 0, 6, 3, 0),
    TILE('*', 0, 7, 0, 0), TILE('(', 0, 7, 1, 0), TILE(')', 0, 7, 2, 0), TILE('_', 0, 7, 3, 0),
    ENEMY('+', 8, 0, 0, 1, 2, +1, 0, 0),
    ENEMY('\\', 8, 0, 0, 1, 2, -1, 0, 0),
    TILE('|', 0x09, 8, 1, 0), TILE('`', 0x09, 8, 2, 0), TILE('-', 0x09, 8, 3, 0),
    TILE('=', 0x09, 9, 2, 0),
    TILE(':', 0, 12, 0, 0),
    TILE('}', 0x04, 4, 0, 0),
    TILE('{', 0x05, 4, 0, 0),
    TILE('[', 0x19, 4, 0, 0),
    TILE('c', 0x06, 10, 2, T_GLINT),
    TILE('d', 0x02, 9, 1, 0),
    TILE('f', 0, 14, 1, 0),
    TILE('i', 0x13, -1, 0, 0),
    TILE('k', 0x01, 9, 0, 0),
    ENEMY('l', 10, 1, 0, 4, 1, 0, +1, +1),
    ENEMY('m', 10, 3, 0, 5, 1, 0, +1, +1),
    ENEMY('n', 11, 3, 0, 6, 1, 0, -1, +3),
    TILE('p', 0x03, 10, 0, 0),
    ENEMY('q', 12, 2, T_DRAW_UP2, 7, 1, 0, -1, -2),
    TILE('r', 0x07, 11, 0, 0),
    ENEMY('s', -1, 0, T_MONSTER, 2, 1, -1, 0, 0),
    TILE('t', 0x1A, 4, 1, 0),
    TILE('u', 0x1B, 4, 2, 0),
    TILE('v', 0x1C, 4, 3, 0),
    TILE('w', 0x1D, 15, 2, 0),
    TILE('x', 0, 4, 0, T_RANDOM_MAP18),
    ENEMY('z', 9, 3, 0, 3, 2, 0, 0, 0),
    ENEMY('B', 18, 0, 0, 8, 1, -1, 0, 0),
    TILE('F', 0x15, 12, 3, 0),
    ENEMY('L', 10, 1, 0, 4, 2, 0, +1, 0),
    ENEMY('M', 10, 3, 0, 5, 2, 0, +1, 0),
    ENEMY('P', 15, 0, 0, 9, -1, -1, -1, 0),
    ENEMY('O', 15, 1, 0, 10, -1, -1, -1, 0),
    TILE('R', 0x08, 11, 1, 0),
    ENEMY('S', -1, 0, T_MONSTER, 2, 2, -1, 0, 0),
    TILE('T', 0x14, 14, 0, 0),
    TILE('X', 0, 13, 0, T_RANDOM_FRAME),
    TILE('Y', 0, 1, 0, T_PLAYER),
    TILE('Z', 0xFF, -1, 0, 0),
    TILE('1', 0x0C, 11, 2, 0), TILE('2', 0x0E, 11, 2, 0), TILE('3', 0x10, 11, 2, 0),
    TILE('4', 0x0D, -1, 0, 0), TILE('5', 0x0F, -1, 0, 0), TILE('6', 0x11, -1, 0, 0),
    TILE('7', 0x12, 4, 0, 0), TILE('8', 0x16, 4, 0, 0), TILE('9', 0x17, 4, 0, 0),
    TILE('0', 0x18, 4, 0, 0),
};

/* DrawLevelMonster (1854:59BE): the sprite set used for 's'/'S' on each level 1..20. */
static const signed char level_monster_set[MAX_LEVELS + 1] = {
    -1, 31, 27, 24, 25, 26, 23, 28, 29, 30, 39, 32, 33, 31, 34, 19, 35, 36, 37, 38, 22,
};

static void put_sprite(const gamedata_t *g, int set, int frame, int x, int y);

/* GetPic of a blank 16x16 area, taken by ResetGameState (buffer at B6AA). */
static uint8_t blank_pic[6 + 16 * 4] = {2, 0, 16, 0, 16, 0};

void level_draw_monster(const gamedata_t *g, int level, int frame, int x, int y)
{
    if (level >= 1 && level <= MAX_LEVELS)
        put_sprite(g, level_monster_set[level], frame - 1, x, y);
}

const uint8_t *level_monster_pic(const gamedata_t *g, int level, int frame)
{
    if (level < 1 || level > MAX_LEVELS)
        return NULL;
    return gamedata_sprite(g, level_monster_set[level], frame - 1);
}

const uint8_t *level_blank_pic(void)
{
    return blank_pic;
}

static const tile_t *find_tile(char ch)
{
    for (size_t i = 0; i < sizeof tiles / sizeof tiles[0]; i++)
        if (tiles[i].ch == ch)
            return &tiles[i];
    return NULL;
}

static void put_sprite(const gamedata_t *g, int set, int frame, int x, int y)
{
    const uint8_t *pic = gamedata_sprite(g, set, frame);
    if (pic)
        g3_put_pic(pic, x, y);
}

/* SpawnEnemy (1854:64FB). The counter advances even when no slot is used, as in the game. */
static void spawn_enemy(level_t *lv, int type, int x, int y, int speed, int dx, int dy)
{
    lv->spawn_count++;
    if (lv->spawn_count > MAX_ENEMIES || !lv->enemy[lv->spawn_count].free)
        return;
    enemy_t *e = &lv->enemy[lv->spawn_count];
    e->type = type;
    e->x = x;
    e->y = y;
    e->speed = speed;
    e->dx = dx;
    e->dy = dy;
    e->frame = 1;
    e->free = false;
}

/* DrawLevelRow(row, s) */
static void draw_level_row(level_t *lv, const gamedata_t *g, int row, const char *s)
{
    for (int col = 1; col <= LEVEL_COLS; col++) {
        const int x = (col - 1) * 16, y = row * 16;
        lv->map[row][col] = 0;
        const tile_t *t = find_tile(s[col - 1]);
        if (!t)
            continue;

        if (t->flags & T_RANDOM_FRAME) {
            const int frame = tp_random(4);
            put_sprite(g, t->set, frame, x, y);
            lv->tile_pic[row][col] = gamedata_sprite(g, t->set, frame);
        } else if (t->set >= 0) {
            put_sprite(g, t->set, t->frame, x, y);
            lv->tile_pic[row][col] = gamedata_sprite(g, t->set, t->frame);
        }
        if (t->flags & T_DRAW_UP2)
            put_sprite(g, t->set, t->frame, x, y - 2);
        if (t->flags & T_MONSTER)
            level_draw_monster(g, lv->level, 1, x, y);

        if (t->map)
            lv->map[row][col] = t->map;
        if ((t->flags & T_RANDOM_MAP18) && tp_random(10) == 1)
            lv->map[row][col] = 0x18;
        if ((t->flags & T_GLINT) && lv->glint_count < MAX_GLINTS) {
            lv->glint_count++;
            lv->glint_x[lv->glint_count] = x;
            lv->glint_y[lv->glint_count] = y;
        }
        if (t->flags & T_PLAYER) {
            lv->player_x = x;
            lv->player_y = y;
            lv->player_found = true;
        }
        if (t->type)
            spawn_enemy(lv, t->type, x, y + t->y_off, t->speed, t->dx, t->dy);
    }
}

void level_start(level_t *lv, const gamedata_t *g, int level)
{
    memset(lv, 0, sizeof *lv);
    lv->level = level;
    /* Free slots get the values StartLevel writes (MoveEnemies still runs them, as type 1). */
    for (int i = 1; i <= MAX_ENEMIES; i++) {
        enemy_t *e = &lv->enemy[i];
        e->free = true;
        e->frame = e->speed = e->dx = e->dy = -1;
        e->x = e->y = e->type = -32;
    }

    g3_graph_color_mode();
    if (level < 1 || level > g->num_levels)
        return;
    for (int row = 1; row <= LEVEL_ROWS; row++)
        draw_level_row(lv, g, row, g->rows[level - 1][row - 1]);

    /* Clear the status area and draw its top border: PutPic(blank, i*16, 205) for i = 0..20,
     * then Draw(0, 189, 320, 189, 3). */
    for (int i = 0; i <= 20; i++)
        g3_put_pic(blank_pic, i * 16, 205);
    g3_draw(0, 189, 320, 189, 3);
}
