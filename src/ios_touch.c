#include "lab3d.h"

#if !defined(LAB3D_IOS)

void lab3d_ios_touch_init(void) {}
void lab3d_ios_touch_set_gameplay_enabled(int enabled) { (void)enabled; }
int lab3d_ios_touch_gameplay_enabled(void) { return 0; }
void lab3d_ios_touch_process_event(SDL_Event *event) { (void)event; }
int lab3d_ios_touch_key_pressure(int keydef, int pressval, int runpressval)
{
    (void)keydef;
    (void)pressval;
    (void)runpressval;
    return 0;
}
void lab3d_ios_touch_consume_look(int *dx, int *dy) { *dx = 0; *dy = 0; }
void lab3d_ios_arm_intro_pointer_grace(void) {}
int lab3d_ios_intro_pointer_buttons_masked(void) { return 0; }

#else

#include <math.h>

#define LAB3D_NO_FINGER ((SDL_FingerID)-1)

static int touch_gameplay_enabled;
static Uint32 intro_pointer_grace_until;

static float stick_x;
static float stick_y;

static int look_dx_accum, look_dy_accum;

static SDL_FingerID move_finger;
static float move_anchor_px, move_anchor_py;
static float move_max_r;

static SDL_FingerID look_finger;

static SDL_FingerID fire_finger;
static SDL_FingerID use_finger;
static SDL_FingerID menu_finger;

static float move_deadzone = 0.14f;

static void refresh_window_metrics(int *ww, int *wh)
{
    if (mainwindow)
        SDL_GetWindowSize(mainwindow, ww, wh);
    else {
        *ww = 640;
        *wh = 480;
    }
}

static int hit_region_norm(float nx, float ny, float x0, float y0, float x1, float y1)
{
    return nx >= x0 && nx <= x1 && ny >= y0 && ny <= y1;
}

void lab3d_ios_touch_init(void)
{
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "1");
    SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "0");
    move_finger = look_finger = fire_finger = use_finger = menu_finger = LAB3D_NO_FINGER;
    stick_x = stick_y = 0;
}

void lab3d_ios_arm_intro_pointer_grace(void)
{
    intro_pointer_grace_until = SDL_GetTicks() + 500;
}

int lab3d_ios_intro_pointer_buttons_masked(void)
{
    return SDL_GetTicks() < intro_pointer_grace_until;
}

void lab3d_ios_touch_set_gameplay_enabled(int enabled)
{
    touch_gameplay_enabled = enabled;
    /* In gameplay we take raw finger events; elsewhere let SDL drive the cursor. */
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, enabled ? "0" : "1");
    if (!enabled) {
        stick_x = stick_y = 0;
        move_finger = look_finger = fire_finger = use_finger = menu_finger = LAB3D_NO_FINGER;
        look_dx_accum = look_dy_accum = 0;
    }
}

int lab3d_ios_touch_gameplay_enabled(void)
{
    return touch_gameplay_enabled;
}

void lab3d_ios_touch_process_event(SDL_Event *event)
{
    int ww, wh;
    float nx, ny, px, py;

    if (!touch_gameplay_enabled)
        return;

    refresh_window_metrics(&ww, &wh);
    if (ww < 1)
        ww = 1;
    if (wh < 1)
        wh = 1;

    switch (event->type) {
    case SDL_FINGERDOWN:
        nx = event->tfinger.x;
        ny = event->tfinger.y;
        px = nx * (float)ww;
        py = ny * (float)wh;

        if (hit_region_norm(nx, ny, 0.76f, 0.02f, 0.99f, 0.11f)) {
            menu_finger = event->tfinger.fingerId;
            return;
        }
        if (hit_region_norm(nx, ny, 0.68f, 0.28f, 0.99f, 0.42f)) {
            use_finger = event->tfinger.fingerId;
            return;
        }
        if (hit_region_norm(nx, ny, 0.68f, 0.60f, 0.99f, 0.92f)) {
            fire_finger = event->tfinger.fingerId;
            return;
        }

        if (nx < 0.42f && move_finger == LAB3D_NO_FINGER) {
            move_finger = event->tfinger.fingerId;
            move_max_r = fminf((float)ww, (float)wh) * 0.11f;
            if (move_max_r < 32.0f)
                move_max_r = 32.0f;
            move_anchor_px = px;
            move_anchor_py = py;
            stick_x = stick_y = 0;
            return;
        }

        if (look_finger == LAB3D_NO_FINGER && nx >= 0.36f) {
            look_finger = event->tfinger.fingerId;
            return;
        }
        break;

    case SDL_FINGERUP:
        if (event->tfinger.fingerId == move_finger) {
            move_finger = LAB3D_NO_FINGER;
            stick_x = stick_y = 0;
        }
        if (event->tfinger.fingerId == look_finger)
            look_finger = LAB3D_NO_FINGER;
        if (event->tfinger.fingerId == fire_finger)
            fire_finger = LAB3D_NO_FINGER;
        if (event->tfinger.fingerId == use_finger)
            use_finger = LAB3D_NO_FINGER;
        if (event->tfinger.fingerId == menu_finger)
            menu_finger = LAB3D_NO_FINGER;
        break;

    case SDL_FINGERMOTION:
        if (event->tfinger.fingerId == move_finger) {
            px = event->tfinger.x * (float)ww;
            py = event->tfinger.y * (float)wh;
            {
                float dx = px - move_anchor_px;
                float dy = py - move_anchor_py;
                float dist = sqrtf(dx * dx + dy * dy);
                if (dist > move_max_r && dist > 0.001f) {
                    dx = dx * (move_max_r / dist);
                    dy = dy * (move_max_r / dist);
                }
                stick_x = dx / move_max_r;
                stick_y = dy / move_max_r;
            }
        } else if (event->tfinger.fingerId == look_finger) {
            look_dx_accum += (int)(event->tfinger.dx * (float)ww * 3.5f);
            look_dy_accum += (int)(event->tfinger.dy * (float)wh * 3.5f);
        }
        break;
    default:
        break;
    }
}

static int stick_axis_pressure(float axis_mag, int pressval, int runpressval)
{
    float m;
    int running;
    if (axis_mag < move_deadzone)
        return 0;
    m = (axis_mag - move_deadzone) / (1.0f - move_deadzone);
    if (m > 1.0f)
        m = 1.0f;
    running = (action_key[ACTION_RUN] != ACTION_UNBOUND && newkeystatus(action_key[ACTION_RUN]));
    {
        int ctrl_target = running ? runpressval : pressval;
        return (int)((float)ctrl_target * m + 0.5f);
    }
}

int lab3d_ios_touch_key_pressure(int keydef, int pressval, int runpressval)
{
    if (!touch_gameplay_enabled)
        return 0;

    if (menu_finger != LAB3D_NO_FINGER && keydef == ACTION_MENU)
        return pressval ? pressval : 1;

    if (use_finger != LAB3D_NO_FINGER && keydef == ACTION_USE)
        return pressval ? pressval : 1;

    if (fire_finger != LAB3D_NO_FINGER && keydef == ACTION_FIRE)
        return pressval ? pressval : 1;

    switch (keydef) {
    case ACTION_FORWARD:
        return stick_axis_pressure(-stick_y, pressval, runpressval);
    case ACTION_BACKWARD:
        return stick_axis_pressure(stick_y, pressval, runpressval);
    case ACTION_MOVELEFT:
        return stick_axis_pressure(-stick_x, pressval, runpressval);
    case ACTION_MOVERIGHT:
        return stick_axis_pressure(stick_x, pressval, runpressval);
    default:
        return 0;
    }
}

void lab3d_ios_touch_consume_look(int *dx, int *dy)
{
    *dx = look_dx_accum;
    *dy = look_dy_accum;
    look_dx_accum = 0;
    look_dy_accum = 0;
}

#endif
