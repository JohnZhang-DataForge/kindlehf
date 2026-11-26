#define _GNU_SOURCE
#include <gtk/gtk.h>
#include "file_picker.h"
#include <ctype.h>
#include <fcntl.h>
#include <linux/input.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <fbink.h>
#include <xnes.h>

#define BROWSE_ROOT_DEFAULT "/mnt/us"
#define MAX_PATH_LEN 1024
#define INPUT_SCAN_LIMIT 32
#define DISPLAY_BATCH_FRAMES 7

static int fbfd = FBFD_AUTO;
static FBInkConfig fb_cfg;
static FBInkState fb_state;
static int screen_w, screen_h;
static uint8_t *screen_buf, *game_buf;
static int game_x, game_y, game_w, game_h;
static char root_dir[MAX_PATH_LEN] = BROWSE_ROOT_DEFAULT;
static char current_dir[MAX_PATH_LEN] = BROWSE_ROOT_DEFAULT;
static struct xnes_ctx_t *nes;
static int in_game, paused;
static volatile sig_atomic_t quitting;

#define MAX_TOUCH_POINTS 10
struct TouchPoint {
    int raw_x, raw_y, have_x, have_y, active;
};
struct Touch {
    int fd, x_code, y_code, x_min, x_max, y_min, y_max;
    int tracking_code, slot_code, slot_min, slot_count, current_slot;
    int is_multitouch, was_active, swap_axes, mirror_x, mirror_y;
    struct TouchPoint points[MAX_TOUCH_POINTS];
};
static struct Touch touch = { .fd = -1, .tracking_code = -1, .slot_code = -1 };

static const uint8_t font5x7[36][5] = {
    {0x7e,0x11,0x11,0x11,0x7e}, {0x7f,0x49,0x49,0x49,0x36},
    {0x3e,0x41,0x41,0x41,0x22}, {0x7f,0x41,0x41,0x22,0x1c},
    {0x7f,0x49,0x49,0x49,0x41}, {0x7f,0x09,0x09,0x09,0x01},
    {0x3e,0x41,0x49,0x49,0x7a}, {0x7f,0x08,0x08,0x08,0x7f},
    {0x00,0x41,0x7f,0x41,0x00}, {0x20,0x40,0x41,0x3f,0x01},
    {0x7f,0x08,0x14,0x22,0x41}, {0x7f,0x40,0x40,0x40,0x40},
    {0x7f,0x02,0x0c,0x02,0x7f}, {0x7f,0x04,0x08,0x10,0x7f},
    {0x3e,0x41,0x41,0x41,0x3e}, {0x7f,0x09,0x09,0x09,0x06},
    {0x3e,0x41,0x51,0x21,0x5e}, {0x7f,0x09,0x19,0x29,0x46},
    {0x46,0x49,0x49,0x49,0x31}, {0x01,0x01,0x7f,0x01,0x01},
    {0x3f,0x40,0x40,0x40,0x3f}, {0x1f,0x20,0x40,0x20,0x1f},
    {0x3f,0x40,0x38,0x40,0x3f}, {0x63,0x14,0x08,0x14,0x63},
    {0x07,0x08,0x70,0x08,0x07}, {0x61,0x51,0x49,0x45,0x43},
    {0x3e,0x45,0x49,0x51,0x3e}, {0x00,0x42,0x7f,0x40,0x00},
    {0x62,0x51,0x49,0x49,0x46}, {0x22,0x41,0x49,0x49,0x36},
    {0x18,0x14,0x12,0x7f,0x10}, {0x2f,0x49,0x49,0x49,0x31},
    {0x3e,0x49,0x49,0x49,0x32}, {0x01,0x71,0x09,0x05,0x03},
    {0x36,0x49,0x49,0x49,0x36}, {0x26,0x49,0x49,0x29,0x1e}
};

static void on_signal(int sig) { (void)sig; quitting = 1; }

static void fill_rect(uint8_t *buf, int w, int h, int x, int y, int rw, int rh, uint8_t c) {
    int yy;
    if (x < 0) { rw += x; x = 0; }
    if (y < 0) { rh += y; y = 0; }
    if (x + rw > w) rw = w - x;
    if (y + rh > h) rh = h - y;
    if (rw <= 0 || rh <= 0) return;
    for (yy = y; yy < y + rh; yy++)
        memset(buf + (size_t)yy * (size_t)w + (size_t)x, c, (size_t)rw);
}

static void rect(uint8_t *buf, int w, int h, int x, int y, int rw, int rh, uint8_t c) {
    if (rw < 2 || rh < 2) return;
    fill_rect(buf, w, h, x, y, rw, 2, c);
    fill_rect(buf, w, h, x, y + rh - 2, rw, 2, c);
    fill_rect(buf, w, h, x, y, 2, rh, c);
    fill_rect(buf, w, h, x + rw - 2, y, 2, rh, c);
}

static void circle(uint8_t *buf, int w, int h, int cx, int cy, int r, uint8_t c, int filled) {
    int x, y;
    for (y = -r; y <= r; y++) for (x = -r; x <= r; x++) {
        int d = x*x + y*y;
        if ((filled && d <= r*r) || (!filled && d <= r*r && d >= (r-3)*(r-3))) {
            int px = cx + x, py = cy + y;
            if (px >= 0 && px < w && py >= 0 && py < h) buf[(size_t)py*w + px] = c;
        }
    }
}

static const uint8_t *glyph_for(char ch) {
    static const uint8_t dash[5] = {8,8,8,8,8};
    static const uint8_t dot[5] = {0,0x60,0x60,0,0};
    static const uint8_t slash[5] = {0x60,0x18,0x06,0x01,0};
    ch = (char)toupper((unsigned char)ch);
    if (ch >= 'A' && ch <= 'Z') return font5x7[ch - 'A'];
    if (ch >= '0' && ch <= '9') return font5x7[26 + ch - '0'];
    if (ch == '-') return dash;
    if (ch == '.') return dot;
    if (ch == '/') return slash;
    return NULL;
}

static void draw_text(uint8_t *buf, int w, int h, int x, int y, const char *s, int scale, uint8_t c) {
    int i, col, row, dx, dy;
    for (i = 0; s[i]; i++, x += 6 * scale) {
        const uint8_t *g = glyph_for(s[i]);
        if (!g) continue;
        for (col = 0; col < 5; col++) for (row = 0; row < 7; row++) {
            if (!(g[col] & (1u << (6-row)))) continue;
            for (dy = 0; dy < scale; dy++) for (dx = 0; dx < scale; dx++) {
                int px = x + col*scale + dx, py = y + row*scale + dy;
                if (px >= 0 && px < w && py >= 0 && py < h) buf[(size_t)py*w + px] = c;
            }
        }
    }
}

static void center_text(uint8_t *buf, int w, int h, int y, const char *s, int scale, uint8_t c) {
    int width = (int)strlen(s) * 6 * scale - scale;
    draw_text(buf, w, h, (w - width) / 2, y, s, scale, c);
}

static int show_raw(uint8_t *buf, int w, int h, int x, int y, WFM_MODE_INDEX_T waveform) {
    FBInkConfig cfg = fb_cfg;
    cfg.ignore_alpha = true;
    cfg.is_flashing = false;
    cfg.wfm_mode = waveform;
    return fbink_print_raw_data(fbfd, buf, w, h, (size_t)w * (size_t)h,
                                (short)x, (short)y, &cfg);
}

static int path_within_root(const char *path, const char *root) {
    size_t n = strlen(root);
    return strncmp(path, root, n) == 0 && (path[n] == '\0' || path[n] == '/');
}

static int validate_ines_rom(const uint8_t *data, size_t len) {
    size_t required, trainer;
    if (!data || len < 16) return 0;
    if (data[0] != 'N' || data[1] != 'E' || data[2] != 'S' || data[3] != 0x1a) return 0;
    /* libxnes legacy iNES path expects CHR-ROM bytes; reject NES 2.0 and CHR-RAM-only files. */
    if ((data[7] & 0x0c) != 0 || data[4] == 0 || data[5] == 0) return 0;
    trainer = (data[6] & 0x04) ? 512u : 0u;
    required = 16u + trainer + (size_t)data[4] * 16384u + (size_t)data[5] * 8192u;
    return len >= required;
}

static int load_rom_path(const char *path) {
    FILE *f;
    long len;
    uint8_t *data;
    f = fopen(path, "rb");
    if (!f) return -1;
    if (fseek(f, 0, SEEK_END) != 0 || (len = ftell(f)) < 16 ||
        len > 64L*1024L*1024L || fseek(f, 0, SEEK_SET) != 0) { fclose(f); return -1; }
    data = (uint8_t *)malloc((size_t)len);
    if (!data) { fclose(f); return -1; }
    if (fread(data, 1, (size_t)len, f) != (size_t)len) { free(data); fclose(f); return -1; }
    fclose(f);
    if (!validate_ines_rom(data, (size_t)len)) { free(data); return -1; }
    if (nes) xnes_ctx_free(nes);
    nes = xnes_ctx_alloc(data, (size_t)len);
    free(data);
    if (!nes) return -1;
    xnes_set_speed(nes, 1.0f);
    in_game = 1; paused = 0;
    return 0;
}
static void draw_game_ui(void) {
    int cx, cy, r;
    memset(screen_buf, 255, (size_t)screen_w * screen_h);
    fill_rect(screen_buf, screen_w, screen_h, 0, 0, screen_w, 48, 0);
    draw_text(screen_buf, screen_w, screen_h, 20, 16, "EXIT", 2, 255);
    draw_text(screen_buf, screen_w, screen_h, screen_w - 132, 16, paused ? "RESUME" : "PAUSE", 2, 255);

    cx = screen_w * 23 / 100; cy = screen_h * 85 / 100; r = screen_w * 13 / 100;
    circle(screen_buf, screen_w, screen_h, cx, cy, r, 0, 0);
    fill_rect(screen_buf, screen_w, screen_h, cx - r/3, cy - r/3, 2*r/3, 2*r/3, 0);
    fill_rect(screen_buf, screen_w, screen_h, cx - r, cy - r/5, 2*r, 2*r/5, 0);
    fill_rect(screen_buf, screen_w, screen_h, cx - r/5, cy - r, 2*r/5, 2*r, 0);
    draw_text(screen_buf, screen_w, screen_h, cx - 13, cy - 7, "D", 2, 255);

    cx = screen_w * 76 / 100; cy = screen_h * 84 / 100; r = screen_w * 7 / 100;
    circle(screen_buf, screen_w, screen_h, cx, cy, r, 0, 0);
    draw_text(screen_buf, screen_w, screen_h, cx - 7, cy - 7, "A", 2, 0);
    cx = screen_w * 91 / 100;
    circle(screen_buf, screen_w, screen_h, cx, cy, r, 0, 0);
    draw_text(screen_buf, screen_w, screen_h, cx - 7, cy - 7, "B", 2, 0);

    rect(screen_buf, screen_w, screen_h, screen_w*43/100, screen_h*78/100, screen_w*13/100, 30, 0);
    draw_text(screen_buf, screen_w, screen_h, screen_w*45/100, screen_h*78/100 + 10, "SEL", 1, 0);
    rect(screen_buf, screen_w, screen_h, screen_w*43/100, screen_h*84/100, screen_w*13/100, 30, 0);
    draw_text(screen_buf, screen_w, screen_h, screen_w*44/100, screen_h*84/100 + 10, "START", 1, 0);

    show_raw(screen_buf, screen_w, screen_h, 0, 0, WFM_GC16);
}

static void render_game(void) {
    static const uint8_t bayer[4][4] = {
        {0,8,2,10}, {12,4,14,6}, {3,11,1,9}, {15,7,13,5}
    };
    int x, y;
    for (y = 0; y < game_h; y++) {
        int sy = y * 240 / game_h;
        for (x = 0; x < game_w; x++) {
            uint32_t p = xnes_get_pixel(nes, x * 256 / game_w, sy);
            unsigned int r = (p >> 16) & 255, g = (p >> 8) & 255, b = p & 255;
            unsigned int gray = (r*30 + g*59 + b*11) / 100;
            unsigned int threshold = (unsigned int)bayer[y & 3][x & 3] * 16u + 8u;
            game_buf[(size_t)y * game_w + x] = (gray > threshold) ? 255 : 0;
        }
    }
    show_raw(game_buf, game_w, game_h, game_x, game_y, WFM_A2);
    if (paused) {
        uint8_t overlay[250 * 84];
        memset(overlay, 255, sizeof(overlay));
        rect(overlay, 250, 84, 0, 0, 250, 84, 0);
        center_text(overlay, 250, 84, 34, "PAUSED", 3, 0);
        show_raw(overlay, 250, 84, screen_w/2 - 125, screen_h/2 - 42, WFM_GC16);
    }
}

static void release_controller(void) {
    if (nes) {
        uint8_t current = nes->ctl.joystick.p1.key;
        if (current) xnes_controller_joystick_p1(&nes->ctl, 0, current);
    }
}

static void set_controller_mask(uint8_t mask) {
    uint8_t current, down, up;
    if (!nes) return;
    if (paused) mask = 0;
    current = nes->ctl.joystick.p1.key;
    down = (uint8_t)(mask & (uint8_t)~current);
    up = (uint8_t)(current & (uint8_t)~mask);
    if (down || up) xnes_controller_joystick_p1(&nes->ctl, down, up);
}

static uint8_t game_mask_at(int x, int y) {
    int cx = screen_w * 23 / 100, cy = screen_h * 85 / 100;
    int dx = x - cx, dy = y - cy, radius = screen_w * 16 / 100;
    uint8_t mask = 0;
    if (dx*dx + dy*dy <= radius*radius) {
        if (dx < -radius/4) mask |= XNES_JOYSTICK_LEFT;
        if (dx > radius/4) mask |= XNES_JOYSTICK_RIGHT;
        if (dy < -radius/4) mask |= XNES_JOYSTICK_UP;
        if (dy > radius/4) mask |= XNES_JOYSTICK_DOWN;
    }
    if (x > screen_w * 68 / 100 && y > screen_h * 72 / 100)
        mask |= (x < screen_w * 84 / 100) ? XNES_JOYSTICK_A : XNES_JOYSTICK_B;
    if (x > screen_w*40/100 && x < screen_w*60/100) {
        if (y > screen_h*76/100 && y < screen_h*83/100) mask |= XNES_JOYSTICK_SELECT;
        if (y >= screen_h*83/100 && y < screen_h*91/100) mask |= XNES_JOYSTICK_START;
    }
    return mask;
}

static int bit_is_set(const unsigned long *bits, int bit) {
    return (int)((bits[bit / (8 * (int)sizeof(unsigned long))] >>
                  (bit % (8 * (int)sizeof(unsigned long)))) & 1UL);
}

static int open_touch_device(void) {
    int i;
    for (i = 0; i < INPUT_SCAN_LIMIT; i++) {
        char path[64];
        int fd, xcode = -1, ycode = -1, track = -1, slot = -1;
        unsigned long evbits[(EV_MAX + 8*sizeof(unsigned long))/(8*sizeof(unsigned long))];
        unsigned long absbits[(ABS_MAX + 8*sizeof(unsigned long))/(8*sizeof(unsigned long))];
        unsigned long keybits[(KEY_MAX + 8*sizeof(unsigned long))/(8*sizeof(unsigned long))];
        struct input_absinfo ax, ay, aslot;
        snprintf(path, sizeof(path), "/dev/input/event%d", i);
        fd = open(path, O_RDONLY | O_NONBLOCK);
        if (fd < 0) continue;
        memset(evbits, 0, sizeof(evbits)); memset(absbits, 0, sizeof(absbits)); memset(keybits, 0, sizeof(keybits));
        if (ioctl(fd, EVIOCGBIT(0, sizeof(evbits)), evbits) < 0 ||
            !bit_is_set(evbits, EV_ABS) ||
            ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(absbits)), absbits) < 0) {
            close(fd); continue;
        }
        if (bit_is_set(absbits, ABS_MT_POSITION_X) && bit_is_set(absbits, ABS_MT_POSITION_Y)) {
            xcode = ABS_MT_POSITION_X; ycode = ABS_MT_POSITION_Y;
            if (bit_is_set(absbits, ABS_MT_TRACKING_ID)) track = ABS_MT_TRACKING_ID;
            if (bit_is_set(absbits, ABS_MT_SLOT)) slot = ABS_MT_SLOT;
        } else if (bit_is_set(absbits, ABS_X) && bit_is_set(absbits, ABS_Y)) {
            xcode = ABS_X; ycode = ABS_Y;
        } else { close(fd); continue; }
        ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keybits)), keybits);
        if (!bit_is_set(keybits, BTN_TOUCH) && track < 0) { close(fd); continue; }
        if (ioctl(fd, EVIOCGABS(xcode), &ax) < 0 || ioctl(fd, EVIOCGABS(ycode), &ay) < 0) {
            close(fd); continue;
        }
        memset(&touch, 0, sizeof(touch));
        touch.fd = fd; touch.x_code = xcode; touch.y_code = ycode;
        touch.tracking_code = track; touch.slot_code = slot;
        touch.is_multitouch = (xcode == ABS_MT_POSITION_X);
        touch.slot_count = 1;
        if (slot >= 0 && ioctl(fd, EVIOCGABS(slot), &aslot) == 0) {
            touch.slot_min = aslot.minimum;
            touch.slot_count = aslot.maximum - aslot.minimum + 1;
            if (touch.slot_count < 1) touch.slot_count = 1;
            if (touch.slot_count > MAX_TOUCH_POINTS) touch.slot_count = MAX_TOUCH_POINTS;
        } else {
            touch.slot_code = -1;
            touch.slot_min = 0;
        }
        touch.current_slot = 0;
        touch.x_min = ax.minimum; touch.x_max = ax.maximum;
        touch.y_min = ay.minimum; touch.y_max = ay.maximum;
        touch.swap_axes = fb_state.touch_swap_axes;
        touch.mirror_x = fb_state.touch_mirror_x;
        touch.mirror_y = fb_state.touch_mirror_y;
        return 0;
    }
    return -1;
}

static int scale_axis(int v, int minv, int maxv, int outmax) {
    if (maxv <= minv) return 0;
    if (v < minv) v = minv;
    if (v > maxv) v = maxv;
    return (int)(((long long)(v - minv) * outmax) / (maxv - minv));
}

static void exit_game(void) {
    release_controller();
    if (nes) xnes_ctx_free(nes);
    nes = NULL;
    in_game = 0;
    paused = 0;
    if (touch.fd >= 0) { close(touch.fd); touch.fd = -1; }
    touch.was_active = 0;
}

static void process_touch_point(int x, int y) {
    if (!in_game) return;
    if (y < screen_h * 7 / 100) {
        if (x < screen_w * 22 / 100) exit_game();
        else if (x > screen_w * 70 / 100) {
            paused = !paused;
            release_controller();
            draw_game_ui();
            render_game();
        }
    }
}

static void poll_touch(void) {
    struct input_event ev;
    ssize_t n;
    if (touch.fd < 0) return;
    while ((n = read(touch.fd, &ev, sizeof(ev))) == (ssize_t)sizeof(ev)) {
        if (ev.type == EV_ABS) {
            if (touch.slot_code >= 0 && ev.code == touch.slot_code) {
                int slot = ev.value - touch.slot_min;
                if (slot >= 0 && slot < touch.slot_count) touch.current_slot = slot;
            } else {
                int slot = touch.is_multitouch ? touch.current_slot : 0;
                struct TouchPoint *point = &touch.points[slot];
                if (ev.code == touch.x_code) { point->raw_x = ev.value; point->have_x = 1; }
                if (ev.code == touch.y_code) { point->raw_y = ev.value; point->have_y = 1; }
                if (touch.tracking_code >= 0 && ev.code == touch.tracking_code)
                    point->active = ev.value >= 0;
            }
        } else if (ev.type == EV_KEY && ev.code == BTN_TOUCH &&
                   (!touch.is_multitouch || touch.tracking_code < 0)) {
            touch.points[0].active = ev.value != 0;
        } else if (ev.type == EV_SYN && ev.code == SYN_REPORT) {
            int any_active = 0, first_x = 0, first_y = 0;
            uint8_t combined_mask = 0;
            int slot;
            for (slot = 0; slot < touch.slot_count; slot++) {
                struct TouchPoint *point = &touch.points[slot];
                int rx, ry, xmin, xmax, ymin, ymax, x, y;
                if (!point->active || !point->have_x || !point->have_y) continue;
                rx = point->raw_x; ry = point->raw_y;
                xmin = touch.x_min; xmax = touch.x_max;
                ymin = touch.y_min; ymax = touch.y_max;
                if (touch.swap_axes) {
                    int t; t=rx; rx=ry; ry=t; t=xmin; xmin=ymin; ymin=t; t=xmax; xmax=ymax; ymax=t;
                }
                x = scale_axis(rx, xmin, xmax, screen_w - 1);
                y = scale_axis(ry, ymin, ymax, screen_h - 1);
                if (touch.mirror_x) x = screen_w - 1 - x;
                if (touch.mirror_y) y = screen_h - 1 - y;
                if (!any_active) { first_x = x; first_y = y; }
                any_active = 1;
                if (in_game) combined_mask |= game_mask_at(x, y);
            }
            if (any_active && !touch.was_active) process_touch_point(first_x, first_y);
            if (in_game) set_controller_mask(combined_mask);
            else if (!any_active && touch.was_active) set_controller_mask(0);
            touch.was_active = any_active;
        }
    }
}

static void sleep_ns(uint64_t ns) {
    struct timespec ts;
    if (ns > 50000000ULL) ns = 50000000ULL;
    ts.tv_sec = (time_t)(ns / 1000000000ULL);
    ts.tv_nsec = (long)(ns % 1000000000ULL);
    nanosleep(&ts, NULL);
}

int main(int argc, char **argv) {
    int i;
    uint64_t frame_ns;
    const char *start_dir = (argc > 1) ? argv[1] : BROWSE_ROOT_DEFAULT;
    char resolved_start[MAX_PATH_LEN];

    if (!gtk_init_check(&argc, &argv)) {
        fprintf(stderr, "GTK2 initialization failed; check DISPLAY and Kindle X11 environment\\n");
        return 4;
    }
    if (!realpath(BROWSE_ROOT_DEFAULT, root_dir) ||
        !realpath(start_dir, resolved_start) ||
        !path_within_root(resolved_start, root_dir)) {
        fprintf(stderr, "Cannot initialize ROM picker under %s\\n", BROWSE_ROOT_DEFAULT);
        return 2;
    }
    if (snprintf(current_dir, sizeof(current_dir), "%s", resolved_start) >= (int)sizeof(current_dir)) return 2;
    mkdir("/mnt/us/nes", 0755);
    mkdir("/mnt/us/nes/saves", 0755);
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    memset(&fb_cfg, 0, sizeof(fb_cfg));
    fb_cfg.is_quiet = true;
    fb_cfg.ignore_alpha = true;
    fb_cfg.wfm_mode = WFM_GC16;
    fbfd = fbink_open();
    if (fbfd < 0 || fbink_init(fbfd, &fb_cfg) < 0) {
        fprintf(stderr, "FBInk initialization failed\\n");
        return 2;
    }
    memset(&fb_state, 0, sizeof(fb_state));
    fbink_get_state(&fb_cfg, &fb_state);
    screen_w = (int)fb_state.view_width;
    screen_h = (int)fb_state.view_height;
    if (screen_w < 200 || screen_h < 300) {
        screen_w = (int)fb_state.screen_width;
        screen_h = (int)fb_state.screen_height;
    }
    if (screen_w < 200 || screen_h < 300) {
        fprintf(stderr, "Could not determine framebuffer dimensions\\n");
        fbink_close(fbfd);
        return 2;
    }

    screen_buf = (uint8_t *)malloc((size_t)screen_w * screen_h);
    if (!screen_buf) { fbink_close(fbfd); return 2; }
    game_w = screen_w - 40;
    game_h = game_w * 240 / 256;
    if (game_h > screen_h - 260) { game_h = screen_h - 260; game_w = game_h * 256 / 240; }
    game_x = (screen_w - game_w) / 2;
    game_y = 56;
    if (game_y + game_h > screen_h - 220) {
        game_y = 30;
        game_h = screen_h - 260;
        game_w = game_h * 256 / 240;
        game_x = (screen_w - game_w) / 2;
    }
    game_buf = (uint8_t *)malloc((size_t)game_w * game_h);
    if (!game_buf) { free(screen_buf); fbink_close(fbfd); return 2; }

    while (!quitting) {
        if (!in_game) {
            char *rom_path;
            GtkWidget *dialog;
            if (touch.fd >= 0) { close(touch.fd); touch.fd = -1; }
            rom_path = nes_file_picker_run(root_dir, current_dir, sizeof(current_dir));
            if (!rom_path) { quitting = 1; break; }
            if (load_rom_path(rom_path) != 0) {
                dialog = gtk_message_dialog_new(NULL, GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR,
                                                GTK_BUTTONS_OK, "Unable to load this NES ROM.");
                gtk_window_set_title(GTK_WINDOW(dialog), "NES Emulator");
                gtk_dialog_run(GTK_DIALOG(dialog));
                gtk_widget_destroy(dialog);
                free(rom_path);
                continue;
            }
            free(rom_path);
            if (open_touch_device() < 0) {
                if (nes) xnes_ctx_free(nes);
                nes = NULL;
                in_game = 0;
                dialog = gtk_message_dialog_new(NULL, GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR,
                                                GTK_BUTTONS_OK,
                                                "Touch input was not found. The game cannot start.");
                gtk_window_set_title(GTK_WINDOW(dialog), "NES Emulator");
                gtk_dialog_run(GTK_DIALOG(dialog));
                gtk_widget_destroy(dialog);
                continue;
            }
            touch.was_active = 0;
            draw_game_ui();
            render_game();
            continue;
        }

        poll_touch();
        if (in_game && nes && !paused) {
            for (i = 0; i < DISPLAY_BATCH_FRAMES && in_game && !paused && !quitting; i++) {
                frame_ns = xnes_step_frame(nes);
                poll_touch();
                sleep_ns(frame_ns);
            }
            if (in_game && nes) render_game();
        } else {
            usleep(20000);
        }
    }

    release_controller();
    if (nes) xnes_ctx_free(nes);
    if (touch.fd >= 0) close(touch.fd);
    fbink_cls(fbfd, &fb_cfg, NULL, false);
    fbink_close(fbfd);
    free(game_buf);
    free(screen_buf);
    return 0;
}
