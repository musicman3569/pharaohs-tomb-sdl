#include "screenvm.h"

#include <stdio.h>
#include <string.h>

#include "graph3.h"
#include "platform.h"
#include "text.h"

typedef struct {
    const gamedata_t *g;
    svm_vars_t *vars;
    size_t pc;       /* image offset */
    unsigned cs;     /* paragraph of the routine's code segment */
    uint16_t ax, di, bp, sp;
    int zf;
    uint8_t stack[2048];
    char *err;
    size_t errlen;
} vm_t;

static unsigned rd16(const uint8_t *p) { return p[0] | (p[1] << 8); }

static void push(vm_t *v, uint16_t w)
{
    v->sp -= 2;
    v->stack[v->sp % sizeof v->stack] = (uint8_t)w;
    v->stack[(v->sp + 1) % sizeof v->stack] = (uint8_t)(w >> 8);
}

static uint16_t pop(vm_t *v)
{
    const uint16_t w = (uint16_t)(v->stack[v->sp % sizeof v->stack] | (v->stack[(v->sp + 1) % sizeof v->stack] << 8));
    v->sp += 2;
    return w;
}

static uint8_t *bp_byte(vm_t *v, int disp)
{
    return &v->stack[(uint16_t)(v->bp + disp) % sizeof v->stack];
}

static uint16_t bp_word(vm_t *v, int disp)
{
    return (uint16_t)(bp_byte(v, disp)[0] | (bp_byte(v, disp + 1)[0] << 8));
}

static int fail(vm_t *v, const char *what)
{
    snprintf(v->err, v->errlen, "screen code at %05zX: %s (byte %02X)", v->pc, what,
             v->pc < v->g->image_len ? v->g->image[v->pc] : 0);
    return -1;
}

/* Data-segment variables these screens touch. */
static uint8_t *ds_byte(vm_t *v, unsigned addr)
{
    switch (addr) {
    case 0x7995: return &v->vars->joystick_enabled;
    case 0x7996: return &v->vars->joystick_detected;
    case 0x8101: return &v->vars->key_layout;
    default: return NULL;
    }
}

static void pstring(vm_t *v, uint16_t seg, uint16_t off, char *out, size_t outlen)
{
    const size_t p = (size_t)seg * 16 + off;
    size_t n = p < v->g->image_len ? v->g->image[p] : 0;
    if (n > outlen - 1)
        n = outlen - 1;
    memcpy(out, v->g->image + p + 1, n);
    out[n] = '\0';
}

static void wait_key(void)
{
    while (plat_key_pressed())
        plat_read_key();
    while (!plat_key_pressed() && !plat_quitting())
        plat_poll();
}

/* A call to `target` (linear image offset). Pops the callee's arguments like RETF n would. */
static int call(vm_t *v, size_t target)
{
    const gamedata_t *g = v->g;
    char s[256];
    uint16_t a, b, c, d, e;

#define IS(seg, off) (target == gamedata_addr(g, (seg), (off)))
    if (IS(SEG_SYSTEM, 0x0244))            /* StackCheck(AX = frame size): nothing to do */
        return 0;
    if (IS(SEG_GRAPH3, 0x002A)) {          /* GraphColorMode */
        g3_graph_color_mode();
        return 0;
    }
    if (IS(SEG_GAME, 0x1446)) {            /* DrawTextCentered(color, row, s) */
        a = pop(v); b = pop(v); c = pop(v); d = pop(v);
        pstring(v, b, a, s, sizeof s);
        text_draw_centered(d, c, s);
        return 0;
    }
    if (IS(SEG_GAME, 0x1385)) {            /* DrawTextAt(col, row, s) */
        a = pop(v); b = pop(v); c = pop(v); d = pop(v);
        pstring(v, b, a, s, sizeof s);
        text_draw_at(d, c, s);
        return 0;
    }
    if (IS(SEG_GAME, 0x114F)) {            /* DrawFrame(left, top, right, bottom, color) */
        a = pop(v); b = pop(v); c = pop(v); d = pop(v); e = pop(v);
        text_draw_frame(g, a, e, d, c, b);
        return 0;
    }
    static const struct { unsigned off; font_id_t id; } fonts[] = {
        {0x1078, FONT_FRAME}, {0x10A3, FONT_GAME}, {0x10CE, FONT_MENU}, {0x10F9, FONT_TITLE}, {0x1124, FONT_EXTRA},
    };
    for (size_t i = 0; i < sizeof fonts / sizeof fonts[0]; i++)
        if (IS(SEG_GAME, fonts[i].off)) {
            text_select_font(g, fonts[i].id);
            return 0;
        }
    if (IS(SEG_GAME, 0x1043)) {            /* WaitKey: flush, then wait for a key */
        wait_key();
        return 0;
    }
    if (IS(SEG_GAME, 0x1017)) {            /* FlushKeyboard */
        while (plat_key_pressed())
            plat_read_key();
        return 0;
    }
    if (IS(SEG_GAME, 0x0000)) {            /* Delay(ticks) */
        plat_delay(pop(v));
        return 0;
    }
    if (IS(SEG_GAME, 0x0FB7)) {            /* SaveConfig: PTOMB.CFG gets [8101] */
        v->vars->config_changed = 1;
        return 0;
    }
    if (IS(SEG_CRT, 0x0259)) {             /* TextColor(c) */
        text_color(pop(v) & 0xFF);
        return 0;
    }
    if (IS(SEG_CRT, 0x0215)) {             /* GotoXY(x, y) */
        a = pop(v); b = pop(v);
        text_gotoxy(b & 0xFF, a & 0xFF);
        return 0;
    }
    if (IS(SEG_CRT, 0x02FA)) {             /* KeyPressed */
        v->ax = (uint16_t)((v->ax & 0xFF00) | (plat_key_pressed() || plat_quitting() ? 1 : 0));
        return 0;
    }
    if (IS(SEG_CRT, 0x030C)) {             /* ReadKey */
        v->ax = (uint16_t)((v->ax & 0xFF00) | (plat_read_key() & 0xFF));
        return 0;
    }
    if (IS(SEG_SYSTEM, 0x0515)) {          /* value in set: ZF = 0 if it is */
        a = pop(v); b = pop(v); c = pop(v);
        const size_t set = (size_t)b * 16 + a;
        const unsigned val = c & 0xFF;
        v->zf = !(set + val / 8 < g->image_len && (g->image[set + val / 8] >> (val % 8)) & 1);
        return 0;
    }
    if (IS(SEG_JOY, 0x0000) || IS(SEG_JOY, 0x0040) || IS(SEG_JOY, 0x0068)) { /* no joystick */
        v->ax &= 0xFF00;
        return 0;
    }
    if (IS(SEG_JOY, 0x00E4)) {             /* ReadAxis(n) */
        pop(v);
        v->ax = 0;
        return 0;
    }
#undef IS
    char what[64];
    snprintf(what, sizeof what, "call to unknown routine at %05zX", target);
    return fail(v, what);
}

int svm_run(const gamedata_t *g, size_t entry, unsigned cs, const uint16_t *args, int nargs,
            svm_vars_t *vars, char *err, size_t errlen)
{
    static vm_t vm;
    vm_t *v = &vm;
    memset(v, 0, sizeof *v);
    v->g = g;
    v->vars = vars;
    v->pc = entry;
    v->cs = cs;
    v->sp = (uint16_t)sizeof v->stack;
    v->err = err;
    v->errlen = errlen;
    for (int i = 0; i < nargs; i++)
        push(v, args[i]);
    push(v, 0); /* far return address */
    push(v, 0);

    const uint8_t *img = g->image;
    for (long steps = 0; steps < 10000000; steps++) {
        if (v->pc + 6 > g->image_len)
            return fail(v, "ran off the end of the image");
        const uint8_t *c = img + v->pc;
        int8_t d8;
        switch (c[0]) {
        case 0x55: push(v, v->bp); v->pc += 1; continue;
        case 0x5D: v->bp = pop(v); v->pc += 1; continue;
        case 0x50: push(v, v->ax); v->pc += 1; continue;
        case 0x57: push(v, v->di); v->pc += 1; continue;
        case 0x1E: push(v, (uint16_t)g->ds_seg); v->pc += 1; continue;
        case 0x16: push(v, 0); v->pc += 1; continue;
        case 0xB8: v->ax = (uint16_t)rd16(c + 1); v->pc += 3; continue;
        case 0xB0: v->ax = (uint16_t)((v->ax & 0xFF00) | c[1]); v->pc += 2; continue;
        case 0xBF: v->di = (uint16_t)rd16(c + 1); v->pc += 3; continue;
        case 0x3D: v->zf = v->ax == rd16(c + 1); v->pc += 3; continue;
        case 0x3C: v->zf = (v->ax & 0xFF) == c[1]; v->pc += 2; continue;
        case 0x74: v->pc += 2; if (v->zf) v->pc += (int8_t)c[1]; continue;
        case 0x75: v->pc += 2; if (!v->zf) v->pc += (int8_t)c[1]; continue;
        case 0xEB: v->pc += 2 + (int8_t)c[1]; continue;
        case 0xE9: v->pc = cs * 16u + ((unsigned)(v->pc + 3 - cs * 16u + rd16(c + 1)) & 0xFFFF); continue;
        case 0xCB: case 0xCA: case 0xC3: case 0xC2: return 0; /* the routine returned */
        case 0x0E:
            if (c[1] == 0xE8) { /* push cs / call near = call a far procedure in this segment */
                const size_t t = cs * 16u + ((unsigned)(v->pc + 4 - cs * 16u + rd16(c + 2)) & 0xFFFF);
                v->pc += 4;
                if (call(v, t) != 0)
                    return -1;
            } else {
                push(v, (uint16_t)cs);
                v->pc += 1;
            }
            continue;
        case 0xE8: {
            const size_t t = cs * 16u + ((unsigned)(v->pc + 3 - cs * 16u + rd16(c + 1)) & 0xFFFF);
            v->pc += 3;
            if (call(v, t) != 0)
                return -1;
            continue;
        }
        case 0x9A: {
            const size_t t = rd16(c + 3) * 16u + rd16(c + 1);
            v->pc += 5;
            if (call(v, t) != 0)
                return -1;
            continue;
        }
        case 0x89:
            if (c[1] == 0xE5) { v->bp = v->sp; v->pc += 2; continue; }
            if (c[1] == 0xEC) { v->sp = v->bp; v->pc += 2; continue; }
            break;
        case 0x83:
            if (c[1] == 0xEC) { v->sp -= c[2]; v->pc += 3; continue; }
            if (c[1] == 0x7E) { d8 = (int8_t)c[2]; v->zf = bp_word(v, d8) == (uint16_t)(int8_t)c[3]; v->pc += 4; continue; }
            break;
        case 0x81:
            if (c[1] == 0xEC) { v->sp = (uint16_t)(v->sp - rd16(c + 2)); v->pc += 4; continue; }
            break;
        case 0x80:
            if (c[1] == 0x7E) { d8 = (int8_t)c[2]; v->zf = *bp_byte(v, d8) == c[3]; v->pc += 4; continue; }
            if (c[1] == 0x3E) {
                const uint8_t *m = ds_byte(v, rd16(c + 2));
                if (!m)
                    return fail(v, "compare with an unknown variable");
                v->zf = *m == c[4];
                v->pc += 5;
                continue;
            }
            break;
        case 0x8B:
            if (c[1] == 0x46) { v->ax = bp_word(v, (int8_t)c[2]); v->pc += 3; continue; }
            break;
        case 0x8A:
            if (c[1] == 0x46) { v->ax = (uint16_t)((v->ax & 0xFF00) | *bp_byte(v, (int8_t)c[2])); v->pc += 3; continue; }
            break;
        case 0x88:
            if (c[1] == 0x46) { *bp_byte(v, (int8_t)c[2]) = (uint8_t)v->ax; v->pc += 3; continue; }
            break;
        case 0xC6:
            if (c[1] == 0x46) { *bp_byte(v, (int8_t)c[2]) = c[3]; v->pc += 4; continue; }
            if (c[1] == 0x06) {
                uint8_t *m = ds_byte(v, rd16(c + 2));
                if (!m)
                    return fail(v, "write to an unknown variable");
                *m = c[4];
                v->pc += 5;
                continue;
            }
            break;
        case 0xA2: case 0xA0: {
            uint8_t *m = ds_byte(v, rd16(c + 1));
            if (!m)
                return fail(v, "access to an unknown variable");
            if (c[0] == 0xA2)
                *m = (uint8_t)v->ax;
            else
                v->ax = (uint16_t)((v->ax & 0xFF00) | *m);
            v->pc += 3;
            continue;
        }
        case 0x0A:
            if (c[1] == 0xC0) { v->zf = (v->ax & 0xFF) == 0; v->pc += 2; continue; }
            break;
        case 0x31:
            if (c[1] == 0xC0) { v->ax = 0; v->zf = 1; v->pc += 2; continue; }
            break;
        default:
            break;
        }
        return fail(v, "unsupported instruction");
    }
    return fail(v, "too many steps");
}
