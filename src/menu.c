#include "menu.h"

#include <stdio.h>
#include <string.h>

#include "game.h"
#include "graph3.h"
#include "hiscore.h"
#include "pcx.h"
#include "platform.h"
#include "screenvm.h"
#include "text.h"
#include "tp.h"

static const gamedata_t *data;
static pcx_image_t title_pic;
static int cfg_layout;            /* [8101] from PTOMB.CFG, 0 if none */
static int marker_row = -1;       /* [75CC] row offset of a new high-score entry */

/* Text calls found in the main-program routines (see gamedata_text_ops). */
static text_op_t title_ops[24], menu_ops[32], hs_ops[8], go_ops[12];
static int n_title_ops, n_menu_ops, n_hs_ops, n_go_ops;

static int upcase(int c)
{
    return c >= 'a' && c <= 'z' ? c - 32 : c;
}

static void flush_keyboard(void)
{
    while (plat_key_pressed())
        plat_read_key();
}

static FILE *open_either_case(const char *dir, const char *name, const char *mode, char *used, size_t usedlen)
{
    char upper[64];
    snprintf(upper, sizeof upper, "%s", name);
    for (char *c = upper; *c; c++)
        if (*c >= 'a' && *c <= 'z')
            *c = (char)(*c - 32);
    const char *names[2] = {name, upper};
    for (int i = 0; i < 2; i++) {
        snprintf(used, usedlen, "%s%s", dir, names[i]);
        FILE *f = fopen(used, mode);
        if (f)
            return f;
    }
    snprintf(used, usedlen, "%s%s", dir, name);
    return NULL;
}

int menu_init(const gamedata_t *g, char *err, size_t errlen)
{
    data = g;
    char name[32], path[600];
    snprintf(name, sizeof name, "%s.mnu", g->base_name);
    FILE *f = open_either_case(g->dir, name, "rb", path, sizeof path);
    if (!f) {
        snprintf(err, errlen, "can't find %s next to the EXE (the title picture)", name);
        return -1;
    }
    fclose(f);
    if (pcx_load(path, &title_pic, err, errlen) != 0)
        return -1;

    /* LoadConfig (1854:0F28): PTOMB.cfg holds one character, the key layout. */
    if ((f = open_either_case(g->dir, "PTOMB.cfg", "rb", path, sizeof path)) != NULL) {
        const int c = fgetc(f);
        if (c != EOF)
            cfg_layout = c;
        fclose(f);
    }

    hiscore_load(g);

    if (g->fn_title_screen)
        n_title_ops = gamedata_text_ops(g, g->fn_title_screen, 0x200, 0, title_ops, 24);
    if (g->fn_main_menu)
        n_menu_ops = gamedata_text_ops(g, g->fn_main_menu, 0x1A0, 0, menu_ops, 32);
    if (g->fn_high_scores)
        n_hs_ops = gamedata_text_ops(g, g->fn_high_scores, 0x60, 0, hs_ops, 8);
    if (g->fn_game_over)
        n_go_ops = gamedata_text_ops(g, g->fn_game_over, 0x80, 0, go_ops, 12);
    return 0;
}

int menu_key_layout(void)
{
    return cfg_layout;
}

static void save_config(int layout)
{
    char path[600];
    FILE *f = open_either_case(data->dir, "PTOMB.cfg", "rb", path, sizeof path);
    if (f)
        fclose(f);
    if ((f = fopen(path, "wb")) != NULL) {
        fputc(layout, f);
        fclose(f);
    }
}

static void replay(const text_op_t *op)
{
    switch (op->kind) {
    case OP_FONT: text_select_font(data, (font_id_t)op->a); break;
    case OP_COLOR: text_color(op->a); break;
    case OP_CENTER: text_draw_centered(op->a, op->b, op->text); break;
    case OP_AT: text_draw_at(op->a, op->b, op->text); break;
    case OP_STRLOAD: break;
    }
}

/* Run one of the game's text screens through the interpreter. */
static void run_screen(size_t entry, unsigned cs, const uint16_t *args, int nargs)
{
    svm_vars_t vars = {(uint8_t)(cfg_layout ? cfg_layout : '3'), 0};
    char err[200];
    if (svm_run(data, entry, cs, args, nargs, &vars, err, sizeof err) != 0) {
        fprintf(stderr, "ptomb: %s\n", err);
        g3_graph_color_mode();
        text_select_font(data, FONT_MENU);
        text_draw_centered(1, 12, "SCREEN NOT AVAILABLE");
        flush_keyboard();
        while (!plat_key_pressed() && !plat_quitting())
            plat_poll();
    }
    if (vars.config_changed) {
        cfg_layout = vars.key_layout;
        save_config(cfg_layout);
    }
}

static void run_game_screen(unsigned off, const uint16_t *args, int nargs)
{
    run_screen(gamedata_addr(data, SEG_GAME, off), (unsigned)(SEG_GAME + data->seg_delta), args, nargs);
}

void menu_volume_ending(int volume)
{
    const uint16_t args[1] = {(uint16_t)volume};
    run_game_screen(GAME_VOLUME_ENDING, args, 1);
}

/* ------------------------------------------------------------------------------------------ */

void menu_title_screen(void)
{
    g3_graph_color_mode();
    pcx_show_random_effect(&title_pic);
    /* TitleScreen's text: four volume headers (one is shown), four credit lines, then the
     * "not shareware" line on the registered volumes. */
    int k = 0;
    for (int i = 0; i < n_title_ops; i++) {
        const text_op_t *op = &title_ops[i];
        if (op->kind == OP_CENTER) {
            const int idx = k++;
            if (idx < 4 && idx != data->volume - 1)
                continue;
            if (idx == 8 && strcmp(data->base_name, "ptomb1") == 0)
                continue;
            if (idx > 8)
                break;
        }
        replay(op);
    }
    flush_keyboard();
    game_title_demo(data, cfg_layout);
}

/* 1000:552E: two bats flap back and forth above the menu. */
typedef struct {
    int x, dx, frame;
} bat_t;

static void bats_step(bat_t *b1, bat_t *b2)
{
    bat_t *bats[2] = {b1, b2};
    const int lo[2] = {30, 220}, hi[2] = {80, 270};
    for (int i = 0; i < 2; i++) {
        bat_t *b = bats[i];
        b->x += b->dx;
        if (b->x < lo[i] || b->x > hi[i])
            b->dx = -b->dx;
        if (++b->frame == 5)
            b->frame = 1;
        const uint8_t *pic = gamedata_sprite(data, 18, b->frame - 1);
        if (pic)
            g3_put_pic(pic, b->x, 60);
    }
    plat_delay(1);
}

static bool menu_key_valid(int c)
{
    return c == '\r' || c == ' ' || c == '+' || (c && strchr("CEHIJOQS", c) != NULL);
}

void menu_main(bool cheat_from_command_line)
{
    bool quit = false;
    while (!quit && !plat_quitting()) {
        char title[64];
        snprintf(title, sizeof title, "Volume %d - main menu", data->volume);
        plat_status(title);
        g3_graph_color_mode();
        game_reset_state(data, cfg_layout);
        pcx_show_random_effect(&title_pic);
        for (int i = 0; i < n_menu_ops; i++) {
            if (menu_ops[i].kind == OP_STRLOAD) { /* "VER: 3.0   MAIN MENU     VOL: " + volume */
                char header[100];
                snprintf(header, sizeof header, "%s%d", menu_ops[i].text, data->volume);
                text_draw_centered(3, 11, header);
            } else {
                replay(&menu_ops[i]);
            }
        }
        flush_keyboard(); /* 1000:5C05: drops the key that ended the title demo (or a screen) */
        bat_t b1 = {50, 1, 1}, b2 = {260, 1, 1};
        int key = '!';
        for (;;) {
            bats_step(&b1, &b2);
            tp_random(0x7FFF);
            if (plat_key_pressed())
                key = plat_read_key();
            const int c = upcase(key);
            if (c == 'E' || c == ' ' || c == '\r') {
                game_play(1, cheat_from_command_line); /* PlayGame starts at [75FE] = 1 */
                g3_graph_color_mode();
            } else if (c == '+') {
                if (plat_shift_keys().scroll_lock) { /* the menu cheat */
                    game_play(1, true);
                    g3_graph_color_mode();
                }
            } else if (c == 'Q') {
                const uint16_t args[2] = {(uint16_t)data->volume, 0};
                run_game_screen(GAME_SHOW_ORDERING, args, 2);
                quit = true;
            } else if (c == 'O') {
                const uint16_t args[2] = {(uint16_t)data->volume, 1};
                run_game_screen(GAME_SHOW_ORDERING, args, 2);
            } else if (c == 'I') {
                run_game_screen(GAME_SHOW_INSTRUCTIONS, NULL, 0);
            } else if (c == 'S') {
                const uint16_t args[1] = {(uint16_t)data->volume};
                run_game_screen(GAME_SHOW_STORY, args, 1);
            } else if (c == 'H') {
                menu_show_high_scores(true);
            } else if (c == 'C') {
                run_game_screen(GAME_CONFIG_KEYBOARD, NULL, 0);
            } else if (c == 'J' && data->fn_joystick_menu) {
                run_screen(data->fn_joystick_menu, 0, NULL, 0);
            }
            if (menu_key_valid(c) || plat_quitting())
                break;
        }
        flush_keyboard();
    }
}

/* ------------------------------------------------------------------------------------------ */

void menu_show_high_scores(bool wait)
{
    g3_graph_color_mode();
    pcx_show_random_effect(&title_pic);
    text_select_font(data, FONT_TITLE);
    text_color(3);
    for (int i = 0; i < n_hs_ops; i++)
        if (hs_ops[i].kind == OP_CENTER) {
            replay(&hs_ops[i]); /* "H I G H   S C O R E S" */
            break;
        }
    text_select_font(data, FONT_GAME);
    marker_row = -1;
    for (int i = HISCORE_COUNT, j = 0; i >= 1; i--, j += 2) {
        text_color(2);
        text_draw_number(6 - i, 5, 15 + j);
        text_draw_at(6, 15 + j, ".");
        text_color(1);
        text_draw_number(hiscores[i].score, 8, 15 + j);
        if (strcmp(hiscores[i].name, HISCORE_MARKER) == 0) {
            marker_row = j;
            hiscores[i].name[0] = '\0';
        }
        text_color(3);
        text_draw_at(18, 15 + j, hiscores[i].name);
    }
    if (wait) {
        flush_keyboard();
        while (!plat_key_pressed() && !plat_quitting())
            plat_poll(); /* the key is left for the menu, as in the original */
    }
}

/* 1854:570D ReadLine(max): typed characters are echoed as they are (so letters show in the
 * ROM font until the name is redrawn); backspace erases. */
static void read_line(char *s, int max)
{
    int len = 0, ch = 'Z';
    s[0] = '\0';
    while (ch != 13 && !plat_quitting()) {
        ch = plat_read_key();
        if (ch < 0x20 || ch > 0x7F || len >= max) {
            if (ch == 8 && len > 0) {
                text_putc(8);
                text_putc(' ');
                text_putc(8);
                s[--len] = '\0';
            }
        } else {
            s[len++] = (char)ch;
            s[len] = '\0';
            text_putc(ch);
        }
    }
}

void menu_game_over(double score)
{
    g3_graph_color_mode();
    if (!(score > hiscores[1].score))
        return;
    int centers = 0;
    for (int i = 0; i < n_go_ops && centers < 3; i++) { /* NEW HIGH SCORE ! / ENTER YOUR NAME ... */
        if (go_ops[i].kind == OP_CENTER)
            centers++;
        replay(&go_ops[i]);
    }
    text_draw_frame(data, 1, 2, 2, 38, 24);
    plat_delay(60);

    hiscores[1].score = score;
    strcpy(hiscores[1].name, HISCORE_MARKER);
    hiscore_sort();
    flush_keyboard();
    int idx = 1;
    for (int i = 1; i <= HISCORE_COUNT; i++)
        if (strcmp(hiscores[i].name, HISCORE_MARKER) == 0)
            idx = i;
    menu_show_high_scores(false);
    text_gotoxy(18, marker_row + 15);
    text_color(1);
    char name[32];
    read_line(name, 20);
    for (char *c = name; *c; c++)
        *c = (char)upcase((unsigned char)*c);
    snprintf(hiscores[idx].name, sizeof hiscores[idx].name, "%s", name);
    hiscore_save(data);
}
