#include "game.h"

#include <stdio.h>
#include <string.h>

#include "graph3.h"
#include "platform.h"
#include "timer.h"
#include "tp.h"

game_t G;

/* Player and effect sprites (set, frame) in the DS:0000 sprite slots. */
enum {
    SET_WALK_LEFT = 0,  /* 8ACA, frames 1..4 via [74DA] */
    SET_WALK_RIGHT = 1, /* 885A */
    SET_JUMP_LEFT = 2,  /* 8FAA frame 0 */
    SET_JUMP_RIGHT = 3, /* 8D3A frame 0, turn frame 1 (8DD6) */
    SET_BRICK = 4,      /* 810A frame 0 */
    SET_GEM = 10,       /* 9D12 frame 2 */
    SET_CRUMBLE = 16,   /* AA7A frames 0..3: brick -> nothing */
    SET_INVISIBLE = 17, /* 921A, the shadow line only */
    SET_DEATH = 19,     /* B1CA */
    SET_GLINT = 21,     /* B43A */
};

/* ------------------------------------------------------------------------------------------ */
/* Small helpers                                                                              */

static void pic(int set, int frame, int x, int y)
{
    const uint8_t *p = gamedata_sprite(G.data, set, frame);
    if (p)
        g3_put_pic(p, x, y);
}

static void blank(int x, int y)
{
    g3_put_pic(level_blank_pic(), x, y);
}

static int dot(int x, int y)
{
    return g3_get_dot_color(x, y);
}

static int upcase(int c)
{
    return c >= 'a' && c <= 'z' ? c - 32 : c;
}

/* 1000:1C87 Between(v, lo, hi) */
static bool between(int v, int lo, int hi)
{
    return v >= lo && v <= hi;
}

/* 1000:1C60: advance an animation frame 1..4 */
static void anim_inc(int *frame)
{
    if (++*frame == 5)
        *frame = 1;
}

static void add_score(double points)
{
    G.score += points;
}

/* Sound effects (1854:58D2, 592F, 595F, 598F, 15F0 ...) aren't decoded yet; they're silent for
 * now. Crt Sound/NoSound calls made directly by game code are real. */
static void sfx(const char *what)
{
    (void)what;
}

/* DrawStatusBar (1000:0671): the bonuses it awards are game logic and are ported; the text
 * goes to the window title until the port has a font. */
static void draw_status_bar(void)
{
    if (G.treasure == 100) {
        sfx("bonus");
        if (G.lives < 5)
            G.lives++;
        else
            add_score(50000);
        G.treasure = 0;
        G.saved_treasure = 0;
    }
    if (G.t_count == 5) {
        sfx("bonus");
        add_score(50000);
        G.t_count = 0;
        G.saved_t_count = 0;
    }
    char s[160];
    snprintf(s, sizeof s, "Level %d  Score %.0f  Lives %d  Spears %d  Treasure %d  T %d%s%s%s%s",
             G.level, G.score, G.lives, G.spears, G.treasure, G.t_count,
             G.key_cyan ? "  [cyan key]" : "", G.key_magenta ? "  [magenta key]" : "",
             G.frozen ? "  FROZEN" : "", G.cheat ? "  (cheat)" : "");
    plat_status(s);
}

/* 1854:1017 FlushKeyboard */
static void flush_keyboard(void)
{
    while (plat_key_pressed())
        plat_read_key();
}

/* ------------------------------------------------------------------------------------------ */
/* Player drawing and movement                                                                */

/* 1000:35A2 */
static void draw_player(void)
{
    if (!G.invisible)
        pic(G.facing ? SET_WALK_LEFT : SET_WALK_RIGHT, G.frame - 1, G.px, G.py);
    else
        pic(SET_INVISIBLE, G.frame - 1, G.px, G.py);
}

/* 1000:380B footstep click */
static void footstep(void)
{
    if (!G.pushed && G.frame == 1 && !G.jumping) {
        if (G.sound_on)
            plat_sound(50);
        plat_delay(0);
        plat_nosound();
    }
}

/* 1000:3844 PlayerWalk(dx). Probes six screen pixels at the new position, including columns
 * the player's own sprite covers. Pressing against the current facing only turns around.
 * Note dx = 0 (used to redraw after picking up keys) also turns the player to face left. */
static void player_walk(int dx)
{
    if (G.px + dx > -2 && G.px + dx + 17 < 322) {
        const int c1 = dot(G.px + dx, G.py), c2 = dot(G.px + 15 + dx, G.py);
        const int c3 = dot(G.px + dx, G.py - 15), c4 = dot(G.px + 15 + dx, G.py - 15);
        const int c5 = dot(G.px + dx, G.py - 8), c6 = dot(G.px + 15 + dx, G.py - 8);
        if (c1 == 0 && c2 == 0 && c3 == 0 && c4 == 0 && c5 == 0 && c6 == 0) {
            G.walk_blocked = false;
            if (G.pushed)
                G.px += dx;
            else if ((G.facing && dx < 0) || (!G.facing && dx > 0))
                G.px += dx;
            if (!G.pushed) {
                if (dx != 0)
                    anim_inc(&G.frame);
                G.facing = dx > 0 ? 0 : 1;
            }
            if (G.prev_facing != G.facing || (G.turn_count > 0 && G.turn_count < 3)) {
                /* Turning frame. [75E0] is only reset by ResetGameState, so this shows on the
                 * first turns of a game only, as in the original. */
                G.turn_count++;
                if (!G.invisible)
                    pic(SET_JUMP_RIGHT, 1, G.px, G.py);
                else
                    pic(SET_INVISIBLE, G.frame - 1, G.px, G.py);
                if (G.turn_count == 0)
                    G.turn_count = 1;
            } else if (!G.facing) {
                if (G.jumping && !G.invisible)
                    pic(SET_JUMP_RIGHT, 0, G.px, G.py);
                else
                    draw_player();
                G.facing = 0;
                footstep();
            } else {
                if (G.jumping && !G.invisible)
                    pic(SET_JUMP_LEFT, 0, G.px, G.py);
                else
                    draw_player();
                G.facing = 1;
                footstep();
            }
        } else {
            G.walk_blocked = true;
        }
    }
    G.prev_facing = G.facing;
}

/* 1000:4AD7: move without turning (wind) */
static void push_player(int dx)
{
    G.pushed = true;
    player_walk(dx);
    G.pushed = false;
}

/* 1000:4B66, a procedure nested in PlayerMoveHorizontal */
static void move_if(bool pressed, int dx)
{
    if (pressed) {
        player_walk(dx);
        if (G.wind_right || G.wind_left)
            push_player(dx);
    }
}

/* 1000:4B90, paced by timer counter [7F6A] */
static void player_move_horizontal(void)
{
    if (tick_counter[TICK_7F6A] <= 0)
        return;
    tick_counter[TICK_7F6A] = 0;

    const shift_keys_t sh = plat_shift_keys(); /* ReadShiftKeys */
    if (G.wind_right)
        push_player(2);
    if (G.wind_left)
        push_player(-2);

    if (!isr_keys.left && !isr_keys.right) {
        /* (joystick not supported yet) */
        if (G.key_layout == 0x1B || G.key_layout == '1') {
            move_if(sh.lshift, -2);
            move_if(sh.rshift, 2);
        } else if (G.key_layout == '2') {
            move_if(sh.alt, -2);
            move_if(sh.ctrl, 2);
        } else if (G.key_layout == '3') {
            move_if(sh.ctrl, -2);
            move_if(sh.alt, 2);
        }
    } else {
        move_if(isr_keys.left, -2);
        move_if(isr_keys.right, 2);
    }

    if (G.invis_timer != 0 && ++G.invis_timer > 300) {
        G.invisible = false;
        G.invis_timer = 0;
        draw_player();
    }
}

/* 1000:4DD4 */
static void player_jump(void)
{
    G.jump_hit = false;
    G.jumping = true;
    G.jump_steps = 0;
    G.jump_pitch = 1;
}

/* 1000:3612 PlayerJumpStep(dy): one pixel up (dy = -1) or down (+1) */
static void player_jump_step(int dy)
{
    if (G.jump_pause != 0) {
        if (++G.jump_pause > 10)
            G.jump_pause = 0;
        return;
    }
    G.jump_pitch++;
    G.jump_steps++;
    const int feet = G.jump_steps == 1 ? 0 : 1;
    const int a = dot(G.px, G.py + dy), b = dot(G.px + 15, G.py + dy);
    const int c = dot(G.px, G.py - 15 + dy), d = dot(G.px + 15, G.py - 15 + dy);
    const int e = dot(G.px + 8, G.py + feet);
    const int f = G.falling ? 0 : dot(G.px + 8, G.py - 16);

    if (G.py - 15 + dy > 0 && a == 0 && b == 0 && c == 0 && d == 0 && e == 0 && f == 0) {
        if (G.sound_on && G.jump_steps < 10)
            plat_sound(G.jump_pitch * 1000);
        G.py += dy;
        if ((dot(G.px - 5, G.py + dy) == 0 || dot(G.px - 5, G.py - 15 + dy) == 0) && G.walk_blocked) {
            tick_counter[TICK_7F6A] = 1000;
            player_move_horizontal();
        }
        if (!G.invisible)
            pic(G.facing ? SET_JUMP_LEFT : SET_JUMP_RIGHT, 0, G.px, G.py);
        else
            draw_player();
        plat_nosound();
    } else if (G.jump_hit) {
        G.jumping = false;
        G.falling = false;
        G.frame = 1;
        draw_player();
    } else {
        G.jump_hit = true;
        G.jump_pause = 1;
        tick_counter[TICK_7F6A] = 1000;
        player_move_horizontal();
        G.jump_steps = 0x22;
    }
}

/* 1000:4AB0 */
static void jump_anim(void)
{
    if (G.jumping)
        player_jump_step(G.jump_steps >= 0x22 ? 1 : -1);
}

/* 1000:4DF0 PlayerFire: the spear is an 8-pixel line */
static void player_fire(void)
{
    G.shot = true;
    G.shot_y = G.py - 8;
    if (G.facing) {
        G.shot_x = G.px - 8;
        G.shot_left = true;
        for (int i = 1; i <= 8; i++)
            if (dot(G.shot_x + i, G.shot_y) != 0)
                G.shot = false;
    } else {
        G.shot_x = G.px + 16;
        G.shot_left = false;
        for (int i = 1; i <= 8; i++)
            if (dot(G.shot_x + 8 + i, G.shot_y) != 0)
                G.shot = false;
    }
    if (G.shot) {
        g3_draw(G.shot_x, G.shot_y, G.shot_x + 8, G.shot_y, 1);
        G.spears--;
        if (G.cheat)
            G.spears++;
        draw_status_bar();
        sfx("throw");
    }
}

/* 1000:4CF5: move the spear 4 px, stop at anything non-black or the screen edge */
static void move_shot(void)
{
    if (!G.shot)
        return;
    g3_draw(G.shot_x, G.shot_y, G.shot_x + 8, G.shot_y, 0);
    G.shot_x += G.shot_left ? -4 : 4;
    for (int i = 1; i <= 4; i++) {
        const int x = G.shot_left ? G.shot_x - i : G.shot_x + 8 + i;
        if (dot(x, G.shot_y) != 0)
            G.shot = false;
    }
    if (G.shot_x < 0 || G.shot_x + 8 > 320)
        G.shot = false;
    if (G.shot)
        g3_draw(G.shot_x, G.shot_y, G.shot_x + 8, G.shot_y, 1);
}

/* MoveEnemies (1000:1FA0) isn't ported yet: enemies stay where they spawned. Its first job,
 * clearing the "standing on a platform" flag before GameTick recomputes it, is kept. */
static void move_enemies(void)
{
    G.on_platform = false;
}

/* ------------------------------------------------------------------------------------------ */
/* Dying, treasure, level changes                                                             */

static void game_over_screen(const char *why)
{
    /* KillPlayer's game-over branch / 1000:3336 (high score entry not ported) */
    g3_graph_color_mode();
    G.quit = true;
    G.game_over = true;
    plat_message(why);
    plat_delay(36);
    plat_message("");
}

/* 1000:34C3 */
static void kill_player(void)
{
    sfx("death");
    for (int n = 1; n <= 4; n++)
        for (int f = 1; f <= 4; f++) {
            pic(SET_DEATH, f - 1, G.px, G.py);
            plat_delay(2);
        }
    G.lives--;
    if (G.cheat)
        G.lives++;
    G.loop_r = 12; /* end any tile-map loop in progress */
    G.loop_c = 20;
    if (G.lives < 0) {
        game_over_screen("GAME OVER");
    } else {
        draw_status_bar();
        game_start_level(G.level);
        flush_keyboard();
    }
}

/* 1000:3A64 AddTreasure(n) */
static void add_treasure(int n)
{
    for (int i = 1; i <= n; i++) {
        G.treasure++;
        add_score(50);
        draw_status_bar();
        if (G.sound_on)
            plat_sound(8000);
        plat_delay(1);
        plat_nosound();
    }
}

/* 1000:3AD7 CheatSkipLevel, also the exit tile's level change */
static void next_level(void)
{
    G.saved_spears = G.spears;
    G.saved_t_count = G.t_count;
    G.saved_treasure = G.treasure;
    add_score(1000);
    draw_status_bar();
    G.level++;
    G.levels_entered++;
}

/* ------------------------------------------------------------------------------------------ */
/* TouchTile (1000:3B2D)                                                                      */

/* Every map cell with code `want`: clear it (and the trigger), play set 16 forwards (crumble)
 * or backwards (build). */
static void crumble_or_build(unsigned char *trigger, int want, bool build)
{
    for (G.loop_r = 1; G.loop_r <= LEVEL_ROWS; G.loop_r++)
        for (G.loop_c = 1; G.loop_c <= LEVEL_COLS; G.loop_c++) {
            if (G.game_over || G.lv.map[G.loop_r][G.loop_c] != want)
                continue;
            *trigger = 0;
            G.lv.map[G.loop_r][G.loop_c] = 0;
            for (int i = 1; i <= 4; i++) {
                const int f = build ? 4 - i : i - 1;
                if (!G.game_over)
                    pic(SET_CRUMBLE, f, (G.loop_c - 1) * 16, G.loop_r * 16);
                plat_delay(1);
            }
            sfx("crumble");
        }
}

static void open_exit_doors(void)
{
    for (int r = 1; r <= LEVEL_ROWS; r++)
        for (int c = 1; c <= LEVEL_COLS; c++) {
            const int cx = (c - 1) * 16, cy = r * 16;
            if (G.lv.map[r][c] == 0x04) {
                /* slide 16 px right, erasing the left edge */
                for (int k = 0; k <= 16; k++) {
                    g3_draw(cx + k - 1, cy, cx + k - 1, cy - 15, 0);
                    pic(SET_BRICK, 0, cx + k, cy);
                    if (G.sound_on)
                        plat_sound(tp_random(100));
                    plat_delay(1);
                }
                plat_nosound();
                G.lv.map[r][c] = 0xFF;
            }
            if (G.lv.map[r][c] == 0x05) {
                /* slide left: grab the area k px to the right and put it back at the cell */
                static uint8_t buf[6 + 17 * 5];
                for (int k = 1; k <= 6; k++) {
                    g3_get_pic(buf, cx + k, cy, cx + k + 16, cy - 16);
                    g3_put_pic(buf, cx, cy);
                    if (G.sound_on)
                        plat_sound(tp_random(100));
                    plat_delay(2);
                }
                plat_nosound();
                G.lv.map[r][c] = 0xFF;
            }
        }
}

static void touch_tile(int x, int y)
{
    if (G.game_over)
        return;
    const int row = y / 16 + 1, col = x / 16 + 1;
    if (row < 1 || row > LEVEL_ROWS || col < 1 || col > LEVEL_COLS)
        return; /* the original reads whatever variables follow the map */
    unsigned char *cell = &G.lv.map[row][col];
    const int cx = (col - 1) * 16, cy = row * 16;

    switch (*cell) {
    case 0x01: /* cyan key */
        G.key_cyan = true;
        *cell = 0;
        blank(cx, cy);
        sfx("pickup");
        player_walk(0);
        add_score(100);
        draw_status_bar();
        break;
    case 0x02: /* cyan keyhole: opens the exit doors */
        if (G.key_cyan) {
            sfx("unlock");
            G.key_cyan = false;
            *cell = 0;
            blank(cx, cy);
            player_walk(0);
            add_score(500);
            open_exit_doors();
            draw_status_bar();
        }
        break;
    case 0x03: /* spear */
        *cell = 0;
        blank(cx, cy);
        if (G.spears < 5) {
            G.spears++;
            sfx("pickup");
            add_score(100);
        } else {
            sfx("bonus");
            add_score(10000);
        }
        draw_status_bar();
        break;
    case 0x06: /* gem */
        *cell = 0;
        blank(cx, cy);
        add_score(50);
        draw_status_bar();
        add_treasure(1);
        for (int i = 1; i <= G.lv.glint_count; i++)
            if (G.lv.glint_x[i] == cx && G.lv.glint_y[i] == cy)
                G.lv.glint_x[i] = -32;
        break;
    case 0x07: /* magenta key */
        G.key_magenta = true;
        *cell = 0;
        blank(cx, cy);
        player_walk(0);
        sfx("pickup");
        add_score(100);
        draw_status_bar();
        break;
    case 0x08: /* magenta lock block */
        if (G.key_magenta) {
            sfx("unlock");
            G.key_magenta = false;
            *cell = 0;
            blank(cx, cy);
            player_walk(0);
            add_score(500);
            draw_status_bar();
        }
        break;
    case 0x09: /* spikes */
        kill_player();
        break;
    case 0x0B: /* hidden trigger: crumble the code-0A bricks */
        crumble_or_build(cell, 0x0A, false);
        break;
    case 0x0C: case 0x0E: case 0x10: { /* switch: build bricks at code+1 */
        add_score(100);
        sfx("pickup");
        draw_status_bar();
        const int target = *cell + 1;
        blank(cx, cy);
        *cell = 0;
        crumble_or_build(cell, target, true);
        break;
    }
    case 0x12: case 0x16: case 0x17: /* brick hiding treasure */
        pic(SET_GEM, 2, cx, cy);
        add_treasure(*cell == 0x12 ? 5 : *cell == 0x16 ? 10 : 25);
        pic(SET_BRICK, 0, cx, cy);
        *cell = 0;
        break;
    case 0x13: /* invisibility trap */
        *cell = 0;
        G.invisible = true;
        G.invis_timer = 1;
        sfx("invisible");
        blank(G.px, G.py);
        draw_player();
        break;
    case 0x14: /* "T" collectible */
        *cell = 0;
        blank(cx, cy);
        add_score(1000);
        sfx("pickup");
        G.t_count++;
        draw_status_bar();
        break;
    case 0x15: /* freeze */
        *cell = 0;
        blank(cx, cy);
        add_score(1000);
        draw_status_bar();
        sfx("freeze");
        G.frozen = true;
        G.freeze_count = 0;
        break;
    case 0x18: { /* brick hiding a 10K / 15K / 20K bonus */
        const int k = tp_random(3) + 1, n = k * 5 + 5;
        if (k == 1)
            pic(12, 1, cx, cy);
        else
            pic(14, k == 2 ? 2 : 3, cx, cy);
        for (int i = 1; i <= n; i++) {
            add_score(1000);
            if (G.sound_on)
                plat_sound(i * 100 + 1000);
            plat_delay(1);
            plat_nosound();
            draw_status_bar();
        }
        pic(SET_BRICK, 0, cx, cy);
        *cell = 0;
        break;
    }
    case 0x19: /* brick hiding an extra life, once per game */
        if (!G.life_bonus_taken) {
            pic(SET_JUMP_RIGHT, 1, cx, cy);
            if (G.lives < 5) {
                G.lives++;
                sfx("bonus");
                draw_status_bar();
            } else {
                for (int i = 1; i <= 10; i++) {
                    add_score(5000);
                    draw_status_bar();
                }
            }
            pic(SET_BRICK, 0, cx, cy);
            *cell = 0;
            G.life_bonus_taken = true;
        }
        break;
    case 0x1A: case 0x1B: case 0x1C: case 0x1D: /* artifacts */
        *cell = 0;
        blank(cx, cy);
        add_score(100000);
        sfx("artifact");
        draw_status_bar();
        break;
    case 0xFF: /* exit */
        next_level();
        if (G.level > G.data->num_levels)
            game_over_screen("VOLUME COMPLETE"); /* 1854:4E01 ending text not ported */
        else
            game_start_level(G.level);
        break;
    default:
        break;
    }
}

/* ------------------------------------------------------------------------------------------ */
/* GameTick                                                                                   */

/* Fire keys: the set at CS:4F49, the bottom keyboard row plus F. */
static bool is_fire_key(int c)
{
    return c && strchr(",./<>?BCFMNVXZbcfmnvxz", c) != NULL;
}

static void wait_any_key(const char *msg)
{
    plat_message(msg);
    while (!plat_key_pressed() && !plat_quitting())
        plat_poll();
    plat_message("");
    draw_status_bar();
}

void game_tick(void)
{
    level_t *lv = &G.lv;

    /* Now and then make a random gem glint. Random(count + 1) can pick entry 0, which sits at
     * (0, 0): an original off-by-one, kept. */
    if (!G.glint_on && lv->glint_count > 0 && tp_random(100) < 5) {
        G.glint_pick = tp_random(lv->glint_count + 1);
        if (G.glint_pick <= lv->glint_count && lv->glint_x[G.glint_pick] != -32)
            G.glint_on = true;
        G.glint_frame = 1;
    }

    if (tick_counter[TICK_7F6C] > 0) {
        move_enemies();
        tick_counter[TICK_7F6C] = 0;
        move_shot();
        if (G.glint_on) {
            const int gx = lv->glint_x[G.glint_pick], gy = lv->glint_y[G.glint_pick];
            pic(SET_GLINT, G.glint_frame - 1, gx, gy);
            if (++G.glint_frame == 5) {
                pic(SET_GEM, 2, gx, gy);
                G.glint_on = false;
            }
        }
    }

    for (int i = 1; i <= MAX_ENEMIES; i++)
        if (lv->enemy[i].y - 16 == G.py && between(G.px + 8, lv->enemy[i].x, lv->enemy[i].x + 15))
            G.on_platform = true;

    /* gravity */
    if (!G.on_platform && dot(G.px, G.py + 2) == 0 && dot(G.px + 14, G.py + 2) == 0 && !G.jumping) {
        G.py++;
        if (!G.invisible)
            pic(G.facing ? SET_JUMP_LEFT : SET_JUMP_RIGHT, 0, G.px, G.py);
        else
            draw_player();
        G.falling = true;
        G.jump_hit = false;
        G.jumping = true;
        G.jump_steps = 0x22;
    }

    player_move_horizontal();

    if (plat_key_pressed()) {
        G.last_key = plat_read_key();
        if (G.last_key == 0) {
            plat_read_key();  /* extended key: its scancode is discarded... */
            G.last_key = '|'; /* ...and it becomes '|' */
        }
        if (G.last_key == '+' && plat_shift_keys().scroll_lock && G.level < G.data->num_levels) {
            next_level(); /* CheatSkipLevel (1000:3AD7) */
            game_start_level(G.level);
        }
        if (G.last_key == ' ' && !G.jumping)
            player_jump();
        if (upcase(G.last_key) == 'Q' || G.last_key == 27) {
            plat_message("QUIT (Y/N)?");
            G.last_key = plat_read_key();
            if (upcase(G.last_key) == 'Y')
                G.quit = true;
            plat_message("");
            draw_status_bar();
            G.last_key = 'w';
        }
        if (upcase(G.last_key) == 'P')
            wait_any_key("GAME PAUSED - PRESS ANY KEY");
        /* 'S' / 'R' save and restore: not ported yet */
        if (upcase(G.last_key) == 'E') {
            G.sound_on = !G.sound_on;
            wait_any_key(G.sound_on ? "SOUND EFFECTS ON" : "SOUND EFFECTS OFF");
        }
        if (is_fire_key(G.last_key) && !G.shot && G.spears > 0)
            player_fire();
    }

    if (tick_counter[TICK_7F6E] > 0) {
        for (int i = 0; i < 4; i++)
            jump_anim();
        tick_counter[TICK_7F6E] = 0;
    }

    if (G.jumping)
        touch_tile(G.px + 8, G.py - 17);
    touch_tile(G.px, G.py - 15);
    touch_tile(G.px + 8, G.py - 15);
    touch_tile(G.px + 15, G.py - 15);
    touch_tile(G.px, G.py - 8);
    touch_tile(G.px + 15, G.py - 8);
    touch_tile(G.px, G.py - 1);
    touch_tile(G.px + 8, G.py - 1);
    touch_tile(G.px + 15, G.py - 1);
}

/* ------------------------------------------------------------------------------------------ */
/* New game / StartLevel                                                                      */

void game_new(const gamedata_t *data, int level)
{
    const int layout = G.key_layout ? G.key_layout : '3';
    memset(&G, 0, sizeof G);
    G.data = data;
    G.key_layout = layout;
    /* ResetGameState (1000:08D7) */
    G.level = level;
    G.levels_entered = 1;
    G.lives = 4;
    G.spears = 2;
    G.sound_on = true;
    G.saved_t_count = G.t_count;
    G.saved_spears = G.spears;
    G.saved_treasure = G.treasure;
    G.frame = 1;
    tp_randseed = (uint32_t)tick_total * 2654435761u + 1u;
    game_start_level(level);
    draw_status_bar();
    flush_keyboard();
}

/* ShowLevelTitle (1854:06A5): the level name, then a short pause (or until a key). */
static void show_level_title(int level)
{
    char s[64];
    snprintf(s, sizeof s, "LEVEL %d", level);
    plat_message(s);
    flush_keyboard();
    for (int n = level; n < 0x30 && !plat_key_pressed() && !plat_quitting(); n++)
        plat_delay(1);
    flush_keyboard();
    plat_message("");
    g3_graph_color_mode();
}

void game_start_level(int level)
{
    g3_graph_color_mode();
    show_level_title(level);
    tp_randseed += (uint32_t)tick_total; /* System 2714:095C, probably Randomize */

    if (!G.restored) {
        G.t_count = G.saved_t_count;
        G.spears = G.saved_spears;
        G.treasure = G.saved_treasure;
    } else {
        G.restored = false;
    }
    G.key_cyan = G.key_magenta = false;
    G.invis_timer = 0;
    tick_counter[TICK_7F6A] = tick_counter[TICK_7F6C] = tick_counter[TICK_7F6E] = 0;
    G.frame = 1;
    G.facing = 0;
    G.shot = G.walk_blocked = G.jumping = G.frozen = G.falling = G.pushed = false;
    G.glint_on = false;
    G.level = level;
    G.wind_right = level == 10;
    G.wind_left = level == 16;
    G.invisible = level == 17;

    level_start(&G.lv, G.data, level);
    if (G.lv.player_found) {
        G.px = G.lv.player_x;
        G.py = G.lv.player_y;
    }
    draw_status_bar();
}
