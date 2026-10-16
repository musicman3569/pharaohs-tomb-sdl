#include "gamepad.h"

#include <SDL.h>
#include <stdio.h>
#include <string.h>

#include "platform.h"
#include "platform_sdl.h"
#include "settings.h"

/* The original switches the joystick off at startup (main, 1000:84A8) until the J menu turns it
 * on. The port starts with it on (main.c sets it from input.joystick in settings.h), so a
 * controller works as soon as it's plugged in. */
joy_vars_t joy_vars = {1, JOY_CENTER, JOY_CENTER};

#define MAX_PADS 8

typedef struct {
    SDL_GameController *gc; /* a recognised controller, or */
    SDL_Joystick *js;       /* any other joystick */
    SDL_JoystickID id;
} pad_t;

static pad_t pads[MAX_PADS];
static pad_mode_t mode = PAD_MENU;

/* The --script controller: present once a script has used it. */
static bool virt_present;
static bool virt_button[SDL_CONTROLLER_BUTTON_MAX];

pad_mode_t pad_set_mode(pad_mode_t m)
{
    const pad_mode_t old = mode;
    mode = m;
    return old;
}

pad_mode_t pad_get_mode(void)
{
    return mode;
}

/* --- controllers coming and going ---------------------------------------------------------- */

static pad_t *find_pad(SDL_JoystickID id)
{
    for (int i = 0; i < MAX_PADS; i++)
        if ((pads[i].gc || pads[i].js) && pads[i].id == id)
            return &pads[i];
    return NULL;
}

static void open_pad(int device)
{
    pad_t *p = NULL;
    for (int i = 0; i < MAX_PADS && !p; i++)
        if (!pads[i].gc && !pads[i].js)
            p = &pads[i];
    if (!p)
        return;
    const SDL_JoystickID id = SDL_JoystickGetDeviceInstanceID(device);
    if (find_pad(id))
        return; /* already open (a controller is reported both ways) */
    if (SDL_IsGameController(device)) {
        p->gc = SDL_GameControllerOpen(device);
        if (p->gc)
            printf("controller: %s\n", SDL_GameControllerName(p->gc));
    } else {
        p->js = SDL_JoystickOpen(device);
        if (p->js)
            printf("joystick: %s (no controller mapping; using axes 0/1 and buttons 0/1)\n",
                   SDL_JoystickName(p->js));
    }
    p->id = id;
}

static void close_pad(SDL_JoystickID id)
{
    pad_t *p = find_pad(id);
    if (!p)
        return;
    if (p->gc)
        SDL_GameControllerClose(p->gc);
    if (p->js)
        SDL_JoystickClose(p->js);
    memset(p, 0, sizeof *p);
}

void pad_init(void)
{
    if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) != 0) {
        fprintf(stderr, "ptomb: no controller support: %s\n", SDL_GetError());
        return;
    }
    char *base = SDL_GetBasePath();
    if (base) {
        char path[1024];
        snprintf(path, sizeof path, "%sgamecontrollerdb.txt", base);
        SDL_free(base);
        SDL_RWops *rw = SDL_RWFromFile(path, "rb");
        if (rw) {
            const int n = SDL_GameControllerAddMappingsFromRW(rw, 1);
            if (n > 0)
                printf("controller mappings: %d from %s\n", n, path);
        }
    }
    /* Controllers already plugged in arrive as device-added events too. */
}

/* --- reading ------------------------------------------------------------------------------- */

static bool gc_button(SDL_GameController *gc, SDL_GameControllerButton b)
{
    return SDL_GameControllerGetButton(gc, b) != 0;
}

bool joy_detect(void)
{
    if (virt_present)
        return true;
    for (int i = 0; i < MAX_PADS; i++)
        if (pads[i].gc || pads[i].js)
            return true;
    return false;
}

bool joy_button(int n)
{
    if (n == 1 && virt_button[SDL_CONTROLLER_BUTTON_A])
        return true;
    if (n == 2 && (virt_button[SDL_CONTROLLER_BUTTON_B] || virt_button[SDL_CONTROLLER_BUTTON_X]))
        return true;
    for (int i = 0; i < MAX_PADS; i++) {
        const pad_t *p = &pads[i];
        if (p->gc) {
            if (n == 1 && gc_button(p->gc, SDL_CONTROLLER_BUTTON_A))
                return true;
            if (n == 2 && (gc_button(p->gc, SDL_CONTROLLER_BUTTON_B) ||
                           gc_button(p->gc, SDL_CONTROLLER_BUTTON_X)))
                return true;
        } else if (p->js && SDL_JoystickGetButton(p->js, n - 1)) {
            return true;
        }
    }
    return false;
}

/* One axis as -32768..32767, the d-pad (or hat) counting as full travel. */
static int pad_axis(const pad_t *p, int n)
{
    if (p->gc) {
        const SDL_GameControllerButton lo = n ? SDL_CONTROLLER_BUTTON_DPAD_UP : SDL_CONTROLLER_BUTTON_DPAD_LEFT;
        const SDL_GameControllerButton hi = n ? SDL_CONTROLLER_BUTTON_DPAD_DOWN : SDL_CONTROLLER_BUTTON_DPAD_RIGHT;
        if (gc_button(p->gc, lo))
            return -32768;
        if (gc_button(p->gc, hi))
            return 32767;
        return SDL_GameControllerGetAxis(p->gc, n ? SDL_CONTROLLER_AXIS_LEFTY : SDL_CONTROLLER_AXIS_LEFTX);
    }
    if (SDL_JoystickNumHats(p->js) > 0) {
        const Uint8 hat = SDL_JoystickGetHat(p->js, 0);
        if (hat & (n ? SDL_HAT_UP : SDL_HAT_LEFT))
            return -32768;
        if (hat & (n ? SDL_HAT_DOWN : SDL_HAT_RIGHT))
            return 32767;
    }
    return SDL_JoystickNumAxes(p->js) > n ? SDL_JoystickGetAxis(p->js, n) : 0;
}

int joy_read_axis(int n)
{
    n = n ? 1 : 0;
    int v = 0; /* the controller pushed furthest wins */
    if (virt_button[n ? SDL_CONTROLLER_BUTTON_DPAD_UP : SDL_CONTROLLER_BUTTON_DPAD_LEFT])
        v = -32768;
    if (virt_button[n ? SDL_CONTROLLER_BUTTON_DPAD_DOWN : SDL_CONTROLLER_BUTTON_DPAD_RIGHT])
        v = 32767;
    for (int i = 0; i < MAX_PADS; i++)
        if (pads[i].gc || pads[i].js) {
            const int a = pad_axis(&pads[i], n);
            if (SDL_abs(a) > SDL_abs(v))
                v = a;
        }
    return JOY_CENTER + v * JOY_CENTER / 32768;
}

/* --- buttons that type keys ---------------------------------------------------------------- */

static void button_down(SDL_GameControllerButton b)
{
    int key = 0;
    switch (mode) {
    case PAD_MENU:
        if (b == SDL_CONTROLLER_BUTTON_A || b == SDL_CONTROLLER_BUTTON_START)
            key = 13;
        else if (b == SDL_CONTROLLER_BUTTON_B)
            key = 27;
        else if (settings.navigation) { /* the d-pad as arrow keys: 0, then the scancode */
            static const struct { SDL_GameControllerButton b; int scan; } arrows[] = {
                {SDL_CONTROLLER_BUTTON_DPAD_UP, 0x48}, {SDL_CONTROLLER_BUTTON_DPAD_DOWN, 0x50},
                {SDL_CONTROLLER_BUTTON_DPAD_LEFT, 0x4B}, {SDL_CONTROLLER_BUTTON_DPAD_RIGHT, 0x4D},
            };
            for (size_t i = 0; i < sizeof arrows / sizeof arrows[0]; i++)
                if (b == arrows[i].b) {
                    plat_push_key(0);
                    plat_push_key(arrows[i].scan);
                }
        }
        break;
    case PAD_GAME:
        if (b == SDL_CONTROLLER_BUTTON_START)
            key = 'P';
        else if (b == SDL_CONTROLLER_BUTTON_BACK)
            key = 27;
        break;
    case PAD_YESNO:
        if (b == SDL_CONTROLLER_BUTTON_A || b == SDL_CONTROLLER_BUTTON_START)
            key = 'Y';
        else if (b == SDL_CONTROLLER_BUTTON_B || b == SDL_CONTROLLER_BUTTON_BACK)
            key = 'N';
        break;
    }
    if (key)
        plat_push_key(key);
}

void pad_event(const SDL_Event *ev)
{
    switch (ev->type) {
    case SDL_CONTROLLERDEVICEADDED:
    case SDL_JOYDEVICEADDED:
        open_pad(ev->type == SDL_JOYDEVICEADDED ? ev->jdevice.which : ev->cdevice.which);
        break;
    case SDL_JOYDEVICEREMOVED:
        close_pad(ev->jdevice.which);
        break;
    case SDL_CONTROLLERBUTTONDOWN:
        button_down((SDL_GameControllerButton)ev->cbutton.button);
        break;
    case SDL_JOYBUTTONDOWN: {
        const pad_t *p = find_pad(ev->jbutton.which);
        if (p && p->js && ev->jbutton.button <= 1) /* raw joystick: 0 = A, 1 = B */
            button_down(ev->jbutton.button ? SDL_CONTROLLER_BUTTON_B : SDL_CONTROLLER_BUTTON_A);
        break;
    }
    default:
        break;
    }
}

bool pad_script(const char *name, bool down)
{
    static const struct { const char *name; SDL_GameControllerButton b; } names[] = {
        {"pad_a", SDL_CONTROLLER_BUTTON_A}, {"pad_b", SDL_CONTROLLER_BUTTON_B},
        {"pad_x", SDL_CONTROLLER_BUTTON_X}, {"pad_start", SDL_CONTROLLER_BUTTON_START},
        {"pad_back", SDL_CONTROLLER_BUTTON_BACK}, {"pad_left", SDL_CONTROLLER_BUTTON_DPAD_LEFT},
        {"pad_right", SDL_CONTROLLER_BUTTON_DPAD_RIGHT}, {"pad_up", SDL_CONTROLLER_BUTTON_DPAD_UP},
        {"pad_down", SDL_CONTROLLER_BUTTON_DPAD_DOWN},
    };
    for (size_t i = 0; i < sizeof names / sizeof names[0]; i++)
        if (!strcmp(name, names[i].name)) {
            virt_present = true;
            if (down && !virt_button[names[i].b])
                button_down(names[i].b);
            virt_button[names[i].b] = down;
            return true;
        }
    return false;
}
