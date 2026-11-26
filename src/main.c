#define _GNU_SOURCE
#include <gtk/gtk.h>
#include "file_picker.h"
#include "rom_compat.h"
#include "framebuffer_blit.h"
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/fb.h>
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
static uint8_t *fb_mem;
static size_t fb_mem_len;
static uint32_t fb_line_length, fb_xoffset, fb_yoffset;
static unsigned display_submit_count;
static int screen_w, screen_h;
static uint8_t *screen_buf, *game_buf;
static int game_x, game_y, game_w, game_h;
static char root_dir[MAX_PATH_LEN] = BROWSE_ROOT_DEFAULT;
static char current_dir[MAX_PATH_LEN] = BROWSE_ROOT_DEFAULT;
static struct xnes_ctx_t *nes;
static GtkWidget *main_window;
static int picker_requested;
static int in_game, paused, menu_open;
static unsigned touch_debug_events, touch_debug_reports, touch_debug_downs;
static volatile sig_atomic_t quitting;
static uint64_t next_game_update_ns;

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
            if (!(g[col] & (1u << row))) continue;
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
    int rc;

    if (!fb_mem || fb_state.bpp != 8) {
        errno = ENODEV;
        fprintf(stderr, "NES: framebuffer is not mapped for 8-bpp output\n");
        return -1;
    }
    if (nes_blit_gray8(fb_mem, fb_mem_len, fb_line_length, fb_xoffset, fb_yoffset,
                       (uint32_t)screen_w, (uint32_t)screen_h, buf, w, h, x, y,
                       fb_state.inverted_grayscale) < 0) {
        fprintf(stderr, "NES: direct framebuffer copy failed: %s\n", strerror(errno));
        fflush(stderr);
        return -1;
    }

    /* FBInk's Kindle firmware build is MINIMAL and compiles image blitting out.
       Copy pixels ourselves, then ask FBInk only to submit the e-ink refresh. */
    cfg.is_flashing = false;
    cfg.wfm_mode = waveform;
    rc = fbink_refresh(fbfd, (uint32_t)y, (uint32_t)x, (uint32_t)w, (uint32_t)h, &cfg);
    if (rc < 0) {
        fprintf(stderr, "NES: framebuffer refresh failed (%d): %s\n", rc, strerror(errno));
        fflush(stderr);
        return rc;
    }
    if (display_submit_count < 3) {
        fprintf(stderr, "NES: framebuffer refresh submitted rect=%d,%d %dx%d waveform=%d\n",
                x, y, w, h, (int)waveform);
        fflush(stderr);
    }
    display_submit_count++;

    /*
     * Fence full-quality e-ink updates before allowing subsequent A2 game
     * frames. On Kindle the EPDC queues/merges updates asynchronously.
     * Without this wait, the first partial game refresh can race the initial
     * full-screen GC16 refresh; the controller then appears only after a
     * later full refresh (e.g. when the user taps PAUSE).
     *
     * FBInk documents marker 0 as "the last update submitted by this session".
     */
    if (waveform == WFM_GC16) {
        int wait_rc = fbink_wait_for_complete(fbfd, 0U);
        if (wait_rc < 0) {
            fprintf(stderr, "NES: GC16 refresh completion wait returned %d; continuing\\n",
                    wait_rc);
            fflush(stderr);
        } else if (display_submit_count <= 3) {
            fprintf(stderr, "NES: GC16 refresh completed before next update\\n");
            fflush(stderr);
        }
    }
    return rc;
}

static int path_within_root(const char *path, const char *root) {
    size_t n = strlen(root);
    return strncmp(path, root, n) == 0 && (path[n] == '\0' || path[n] == '/');
}

static int load_rom_path(const char *path, char *error, size_t error_size) {
    FILE *f;
    long len = -1;
    uint8_t *data;
    uint8_t *core_data = NULL;
    size_t core_size = 0;
    int normalized = 0;
    unsigned mapper = 0;

    if (error && error_size) error[0] = '\0';
    fprintf(stderr, "NES: opening ROM: %s\n", path ? path : "(null)");
    fflush(stderr);
    f = fopen(path, "rb");
    if (!f) {
        if (error && error_size) snprintf(error, error_size, "Cannot open ROM: %s", strerror(errno));
        fprintf(stderr, "NES: ROM open failed: %s\n", strerror(errno));
        return -1;
    }
    if (fseek(f, 0, SEEK_END) != 0 || (len = ftell(f)) < 16 ||
        len > 64L*1024L*1024L || fseek(f, 0, SEEK_SET) != 0) {
        if (error && error_size) snprintf(error, error_size, "ROM file size is invalid or exceeds 64 MB.");
        fprintf(stderr, "NES: ROM size/seek check failed (length=%ld)\n", len);
        fclose(f);
        return -1;
    }
    fprintf(stderr, "NES: ROM size=%ld bytes\n", len);
    data = (uint8_t *)malloc((size_t)len);
    if (!data) {
        if (error && error_size) snprintf(error, error_size, "Not enough memory to read this ROM.");
        fprintf(stderr, "NES: ROM allocation failed\n");
        fclose(f);
        return -1;
    }
    if (fread(data, 1, (size_t)len, f) != (size_t)len) {
        if (error && error_size) snprintf(error, error_size, "Could not read the complete ROM file.");
        fprintf(stderr, "NES: ROM read failed\n");
        free(data);
        fclose(f);
        return -1;
    }
    fclose(f);

    fprintf(stderr, "NES: source header=%02x %02x %02x %02x %02x %02x %02x %02x\n",
            data[0], data[1], data[2], data[3], data[4], data[5], data[6], data[7]);
    if (nes_rom_prepare_for_libxnes(data, (size_t)len, &core_data, &core_size,
                                    &normalized, &mapper, error, error_size) != 0) {
        fprintf(stderr, "NES: ROM preparation failed: %s\n",
                (error && error_size && error[0]) ? error : "unknown format error");
        free(data);
        return -1;
    }
    free(data);
    fprintf(stderr, "NES: prepared ROM mapper=%u size=%lu bytes normalized=%s CHR banks=%u\n",
            mapper, (unsigned long)core_size, normalized ? "yes" : "no", core_data[5]);
    fflush(stderr);

    if (nes) {
        xnes_ctx_free(nes);
        nes = NULL;
    }
    nes = xnes_ctx_alloc(core_data, core_size);
    free(core_data);
    if (!nes) {
        if (error && error_size)
            snprintf(error, error_size, "ROM was normalized, but libxnes does not implement mapper %u or rejected this cartridge.", mapper);
        fprintf(stderr, "NES: libxnes rejected prepared ROM (mapper=%u)\n", mapper);
        return -1;
    }
    xnes_set_speed(nes, 1.0f);
    in_game = 1;
    paused = 0;
    menu_open = 0;
    fprintf(stderr, "NES: ROM core initialized successfully (mapper=%u)\n", mapper);
    fflush(stderr);
    return 0;
}

static void show_plain_error_dialog(GtkWindow *parent, const char *message) {
    GtkWidget *dialog;
    GtkWidget *label;
    GtkWidget *area;

    dialog = gtk_dialog_new_with_buttons("NES Emulator", parent, GTK_DIALOG_DESTROY_WITH_PARENT,
                                         "OK", GTK_RESPONSE_OK, NULL);
    area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    label = gtk_label_new(message ? message : "Unknown error");
    gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
    gtk_misc_set_alignment(GTK_MISC(label), 0.0f, 0.5f);
    gtk_container_set_border_width(GTK_CONTAINER(area), 18);
    gtk_box_pack_start(GTK_BOX(area), label, TRUE, TRUE, 8);
    g_signal_connect_swapped(dialog, "response",
                             G_CALLBACK(gtk_widget_destroy), dialog);
    gtk_widget_show_all(dialog);
}
static void draw_game_ui(void) {
    int cx, cy, r;
    memset(screen_buf, 255, (size_t)screen_w * screen_h);
    fill_rect(screen_buf, screen_w, screen_h, 0, 0, screen_w, 56, 0);
    draw_text(screen_buf, screen_w, screen_h, 18, 18, "BACK", 2, 255);
    draw_text(screen_buf, screen_w, screen_h, screen_w/2 - 24, 18, "MENU", 2, 255);
    draw_text(screen_buf, screen_w, screen_h, screen_w - 132, 18, paused ? "RESUME" : "PAUSE", 2, 255);

    cx = screen_w * 23 / 100; cy = screen_h * 85 / 100; r = screen_w * 13 / 100;
    circle(screen_buf, screen_w, screen_h, cx, cy, r, 0, 0);
    fill_rect(screen_buf, screen_w, screen_h, cx - r/3, cy - r/3, 2*r/3, 2*r/3, 0);
    fill_rect(screen_buf, screen_w, screen_h, cx - r, cy - r/5, 2*r, 2*r/5, 0);
    fill_rect(screen_buf, screen_w, screen_h, cx - r/5, cy - r, 2*r/5, 2*r, 0);
    /* Put direction labels inside the black D-pad arms so they stay clear
       of the game viewport and remain legible on the e-ink display. */
    draw_text(screen_buf, screen_w, screen_h, cx - 5, cy - r/2 - 7, "U", 2, 255);
    draw_text(screen_buf, screen_w, screen_h, cx - 5, cy + r/2 - 7, "D", 2, 255);
    draw_text(screen_buf, screen_w, screen_h, cx - r/2 - 5, cy - 7, "L", 2, 255);
    draw_text(screen_buf, screen_w, screen_h, cx + r/2 - 5, cy - 7, "R", 2, 255);

    cx = screen_w * 76 / 100; cy = screen_h * 84 / 100; r = screen_w * 7 / 100;
    circle(screen_buf, screen_w, screen_h, cx, cy, r, 0, 0);
    draw_text(screen_buf, screen_w, screen_h, cx - 5, cy - 7, "A", 2, 0);
    cx = screen_w * 91 / 100;
    circle(screen_buf, screen_w, screen_h, cx, cy, r, 0, 0);
    draw_text(screen_buf, screen_w, screen_h, cx - 5, cy - 7, "B", 2, 0);

    fill_rect(screen_buf, screen_w, screen_h, screen_w*43/100, screen_h*78/100, screen_w*13/100, 30, 232);
    rect(screen_buf, screen_w, screen_h, screen_w*43/100, screen_h*78/100, screen_w*13/100, 30, 0);
    draw_text(screen_buf, screen_w, screen_h, screen_w*43/100 + (screen_w*13/100 - 70)/2, screen_h*78/100 + 8, "SELECT", 2, 0);
    fill_rect(screen_buf, screen_w, screen_h, screen_w*43/100, screen_h*84/100, screen_w*13/100, 30, 232);
    rect(screen_buf, screen_w, screen_h, screen_w*43/100, screen_h*84/100, screen_w*13/100, 30, 0);
    draw_text(screen_buf, screen_w, screen_h, screen_w*43/100 + (screen_w*13/100 - 58)/2, screen_h*84/100 + 8, "START", 2, 0);
}

static void draw_game_menu(void) {
    int pw = screen_w * 80 / 100, ph = screen_h * 48 / 100;
    int px = (screen_w - pw) / 2, py = (screen_h - ph) / 2;
    int bw = pw - 48, bh = ph / 6;
    uint8_t *panel = (uint8_t *)malloc((size_t)pw * ph);
    if (!panel) return;
    memset(panel, 255, (size_t)pw * ph);
    rect(panel, pw, ph, 0, 0, pw, ph, 0);
    center_text(panel, pw, ph, 24, "GAME OPTIONS", 2, 0);
    fill_rect(panel, pw, ph, 24, ph/4, bw, bh, 230);
    rect(panel, pw, ph, 24, ph/4, bw, bh, 0);
    center_text(panel, pw, ph, ph/4 + bh/2 - 7, "RESUME", 2, 0);
    fill_rect(panel, pw, ph, 24, ph/2 - 8, bw, bh, 230);
    rect(panel, pw, ph, 24, ph/2 - 8, bw, bh, 0);
    center_text(panel, pw, ph, ph/2 + bh/2 - 15, "BACK TO LIBRARY", 2, 0);
    fill_rect(panel, pw, ph, 24, (ph*3)/4 - 8, bw, bh, 230);
    rect(panel, pw, ph, 24, (ph*3)/4 - 8, bw, bh, 0);
    center_text(panel, pw, ph, (ph*3)/4 + bh/2 - 15, "QUIT EMULATOR", 2, 0);
    show_raw(panel, pw, ph, px, py, WFM_GC16);
    free(panel);
}

static void render_game(int full_refresh) {
    static const uint8_t bayer[4][4] = {
        {0,8,2,10}, {12,4,14,6}, {3,11,1,9}, {15,7,13,5}
    };
    static unsigned rendered_frames;
    unsigned int min_gray = 255, max_gray = 0, dark_pixels = 0;
    int x, y;
    for (y = 0; y < game_h; y++) {
        int sy = y * 240 / game_h;
        for (x = 0; x < game_w; x++) {
            uint32_t p = xnes_get_pixel(nes, x * 256 / game_w, sy);
            unsigned int r = (p >> 16) & 255, g = (p >> 8) & 255, b = p & 255;
            unsigned int gray = (r*30 + g*59 + b*11) / 100;
            unsigned int threshold = (unsigned int)bayer[y & 3][x & 3] * 16u + 8u;
            if (gray < min_gray) min_gray = gray;
            if (gray > max_gray) max_gray = gray;
            if (gray < 240) dark_pixels++;
            game_buf[(size_t)y * game_w + x] = (gray > threshold) ? 255 : 0;
        }
    }
    if (rendered_frames < 3) {
        fprintf(stderr, "NES: rendered frame %u pixels=%dx%d grayscale=%u..%u nonwhite=%u/%u\n",
                rendered_frames + 1, game_w, game_h, min_gray, max_gray, dark_pixels,
                (unsigned)(game_w * game_h));
        fflush(stderr);
    }
    rendered_frames++;
    if (full_refresh) {
        int row;
        /* Compose the first frame and controls into one full-screen update.
           Two back-to-back e-ink refreshes at ROM launch could leave the
           controller area stale until the next touch-triggered redraw. */
        for (row = 0; row < game_h; row++) {
            memcpy(screen_buf + (size_t)(game_y + row) * screen_w + game_x,
                   game_buf + (size_t)row * game_w, (size_t)game_w);
        }
        show_raw(screen_buf, screen_w, screen_h, 0, 0, WFM_GC16);
    } else {
        show_raw(game_buf, game_w, game_h, game_x, game_y, WFM_A2);
    }
    if (paused && !menu_open) {
        uint8_t overlay[250 * 84];
        memset(overlay, 255, sizeof(overlay));
        rect(overlay, 250, 84, 0, 0, 250, 84, 0);
        center_text(overlay, 250, 84, 34, "PAUSED", 3, 0);
        show_raw(overlay, 250, 84, screen_w/2 - 125, screen_h/2 - 42, WFM_GC16);
    }
    if (menu_open) draw_game_menu();
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
        char device_name[256] = "unknown";
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
        if (!bit_is_set(keybits, BTN_TOUCH) &&
            !bit_is_set(keybits, BTN_TOOL_FINGER) && track < 0) { close(fd); continue; }
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
        (void)ioctl(fd, EVIOCGNAME(sizeof(device_name)), device_name);
        fprintf(stderr, "NES: touchscreen=%s name=%s xcode=%d x=%d..%d ycode=%d y=%d..%d tracking=%d slot=%d multitouch=%d slots=%d swap=%d mirror=%d/%d btn_touch=%d btn_finger=%d\n",
                path, device_name, touch.x_code, touch.x_min, touch.x_max,
                touch.y_code, touch.y_min, touch.y_max, touch.tracking_code,
                touch.slot_code, touch.is_multitouch, touch.slot_count,
                touch.swap_axes, touch.mirror_x, touch.mirror_y,
                bit_is_set(keybits, BTN_TOUCH), bit_is_set(keybits, BTN_TOOL_FINGER));
        fflush(stderr);
        return 0;
    }
    fprintf(stderr, "NES: no compatible evdev touchscreen found\n");
    fflush(stderr);
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
    menu_open = 0;
    if (touch.fd >= 0) { close(touch.fd); touch.fd = -1; }
    touch.was_active = 0;
    fprintf(stderr, "NES: game stopped; returning to launcher window\n");
    fflush(stderr);
    if (gtk_main_level() > 0) gtk_main_quit();
}

static void process_touch_point(int x, int y) {
    if (!in_game) return;

    if (menu_open) {
        int panel_h = screen_h * 48 / 100;
        int button_h = panel_h / 6;
        int rel_y = y - screen_h * 26 / 100;
        int resume_y = panel_h / 4;
        int library_y = panel_h / 2 - 8;
        int quit_y = (panel_h * 3) / 4 - 8;
        if (x < screen_w * 10 / 100 || x > screen_w * 90 / 100) return;
        if (rel_y >= resume_y && rel_y < resume_y + button_h) {
            menu_open = 0;
            paused = 0;
            draw_game_ui();
            render_game(1);
            fprintf(stderr, "NES: in-game menu action=resume\\n");
        } else if (rel_y >= library_y && rel_y < library_y + button_h) {
            fprintf(stderr, "NES: in-game menu action=back-to-library\\n");
            exit_game();
        } else if (rel_y >= quit_y && rel_y < quit_y + button_h) {
            fprintf(stderr, "NES: in-game menu action=quit-emulator\\n");
            quitting = 1;
            exit_game();
        }
        fflush(stderr);
        return;
    }

    if (y < screen_h * 8 / 100) {
        if (x < screen_w * 30 / 100) {
            fprintf(stderr, "NES: touchscreen action=back-to-library x=%d y=%d\n", x, y);
            fflush(stderr);
            exit_game();
        } else if (x >= screen_w * 35 / 100 && x <= screen_w * 65 / 100) {
            fprintf(stderr, "NES: touchscreen action=open-options x=%d y=%d\n", x, y);
            menu_open = 1;
            paused = 1;
            release_controller();
            draw_game_menu();
            fflush(stderr);
        } else if (x > screen_w * 70 / 100) {
            paused = !paused;
            release_controller();
            fprintf(stderr, "NES: touchscreen action=%s x=%d y=%d\n",
                    paused ? "pause" : "resume", x, y);
            fflush(stderr);
            draw_game_ui();
            render_game(1);
        }
    }
}

static void poll_touch(void) {
    struct input_event ev;
    ssize_t n;
    if (touch.fd < 0) return;
    while ((n = read(touch.fd, &ev, sizeof(ev))) == (ssize_t)sizeof(ev)) {
        if (touch_debug_events < 64) {
            fprintf(stderr, "NES: touch raw type=%u code=%u value=%d\n",
                    ev.type, ev.code, ev.value);
            fflush(stderr);
            touch_debug_events++;
        }
        if (ev.type == EV_SYN && ev.code == SYN_DROPPED) {
            int i;
            for (i = 0; i < MAX_TOUCH_POINTS; i++) touch.points[i].active = 0;
            touch.was_active = 0;
            release_controller();
            fprintf(stderr, "NES: touchscreen SYN_DROPPED; controller state reset\n");
            fflush(stderr);
            continue;
        }
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
        } else if (ev.type == EV_KEY &&
                   (ev.code == BTN_TOUCH || ev.code == BTN_TOOL_FINGER) &&
                   (!touch.is_multitouch || touch.tracking_code < 0 ||
                    ev.code == BTN_TOUCH)) {
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
                if (in_game && !menu_open) combined_mask |= game_mask_at(x, y);
            }
            if (touch_debug_reports < 32) {
                fprintf(stderr, "NES: touch report active=%d x=%d y=%d mask=0x%02x previous=%d in_game=%d menu=%d\n",
                        any_active, first_x, first_y, combined_mask,
                        touch.was_active, in_game, menu_open);
                fflush(stderr);
                touch_debug_reports++;
            }
            if (any_active && !touch.was_active) {
                if (touch_debug_downs < 32) {
                    fprintf(stderr, "NES: touch down mapped=%d,%d\n", first_x, first_y);
                    fflush(stderr);
                    touch_debug_downs++;
                }
                process_touch_point(first_x, first_y);
            }
            if (in_game) set_controller_mask(combined_mask);
            else if (!any_active && touch.was_active) set_controller_mask(0);
            touch.was_active = any_active;
        }
    }
}

static uint64_t monotonic_ns(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

/*
 * Keep GTK's main loop alive while the NES core runs. The old implementation
 * ran a blocking while-loop plus nanosleep after the picker returned; that
 * starved GTK/X11 and made the device appear frozen after selecting a ROM.
 * Run a bounded e-ink batch from a GLib timer instead.
 */
static gboolean game_tick(gpointer user_data) {
    int i;
    uint64_t batch_start, batch_ns = 0, now;
    (void)user_data;

    if (quitting || !in_game || !nes) return FALSE;

    poll_touch();
    if (quitting || !in_game || !nes) {
        if (gtk_main_level() > 0) gtk_main_quit();
        return FALSE;
    }
    if (paused) return TRUE;

    now = monotonic_ns();
    if (next_game_update_ns && now < next_game_update_ns) return TRUE;
    batch_start = now;

    for (i = 0; i < DISPLAY_BATCH_FRAMES && in_game && !paused && !quitting; i++) {
        uint64_t frame_ns = xnes_step_frame(nes);
        if (frame_ns == 0 || frame_ns > 50000000ULL) {
            fprintf(stderr, "NES: suspicious frame duration=%llu ns\n",
                    (unsigned long long)frame_ns);
            fflush(stderr);
            if (frame_ns == 0) frame_ns = 16666667ULL;
            if (frame_ns > 50000000ULL) frame_ns = 50000000ULL;
        }
        batch_ns += frame_ns;
        poll_touch();
    }

    if (in_game && nes && !paused && !quitting) render_game(0);

    now = monotonic_ns();
    if (batch_ns == 0) batch_ns = 116666667ULL;
    next_game_update_ns = batch_start + batch_ns;
    /* Never accumulate an unbounded catch-up backlog on a slow e-ink device. */
    if (next_game_update_ns < now) next_game_update_ns = now;
    return in_game && !quitting;
}

/*
 * Enter the GTK/GLib main loop before the first framebuffer draw. The Kindle
 * did not reliably display the startup composition when it was submitted
 * immediately after the ROM picker returned; a later PAUSE full refresh did.
 */
static gboolean begin_game_display(gpointer user_data) {
    int i;
    uint64_t initial_batch_ns = 0;
    (void)user_data;
    if (!in_game || !nes || quitting) {
        if (gtk_main_level() > 0) gtk_main_quit();
        return FALSE;
    }

    /*
     * Advance the PPU before sampling its front buffer. The old startup path
     * rendered before the first xnes_step_frame(), so the first framebuffer
     * image was the core's power-on blank frame (all pixels palette entry 0).
     */
    for (i = 0; i < DISPLAY_BATCH_FRAMES; i++) {
        uint64_t frame_ns = xnes_step_frame(nes);
        if (frame_ns > 0) initial_batch_ns += frame_ns;
    }
    if (initial_batch_ns == 0) initial_batch_ns = 116666667ULL;

    draw_game_ui();
    fprintf(stderr, "NES: initial UI composed inside GTK main loop\n");
    fflush(stderr);
    render_game(1);
    next_game_update_ns = monotonic_ns() + initial_batch_ns;
    fprintf(stderr, "NES: initial full-screen game/controller refresh submitted; starting emulation timer\n");
    fflush(stderr);
    g_timeout_add(2, game_tick, NULL);
    return FALSE;
}

static void on_open_game_clicked(GtkButton *button, gpointer user_data) {
    (void)button;
    (void)user_data;
    picker_requested = 1;
    if (gtk_main_level() > 0) gtk_main_quit();
}

static void on_quit_clicked(GtkButton *button, gpointer user_data) {
    (void)button;
    (void)user_data;
    quitting = 1;
    if (gtk_main_level() > 0) gtk_main_quit();
}

static gboolean on_main_window_delete(GtkWidget *widget, GdkEvent *event, gpointer user_data) {
    (void)widget;
    (void)event;
    (void)user_data;
    quitting = 1;
    if (gtk_main_level() > 0) gtk_main_quit();
    return TRUE;
}

static void on_fullscreen_toggled(GtkToggleButton *button, gpointer user_data) {
    GtkWindow *window = GTK_WINDOW(user_data);
    if (gtk_toggle_button_get_active(button)) gtk_window_fullscreen(window);
    else gtk_window_unfullscreen(window);
}

static void on_options_clicked(GtkButton *button, gpointer user_data) {
    GtkWidget *dialog;
    (void)button;
    dialog = gtk_message_dialog_new(GTK_WINDOW(user_data), GTK_DIALOG_DESTROY_WITH_PARENT,
                                    GTK_MESSAGE_INFO, GTK_BUTTONS_OK,
                                    "Audio is disabled. Touch controls are shown on the game screen. Use MENU for Resume, Back to Library, or Quit Emulator.");
    gtk_window_set_title(GTK_WINDOW(dialog), "NES Options");
    g_signal_connect_swapped(dialog, "response", G_CALLBACK(gtk_widget_destroy), dialog);
    gtk_widget_show(dialog);
}

static GtkWidget *create_main_window(void) {
    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    GtkWidget *vbox = gtk_vbox_new(FALSE, 8);
    GtkWidget *toolbar = gtk_hbutton_box_new();
    GtkWidget *open_button = gtk_button_new_with_label("Open Game");
    GtkWidget *options_button = gtk_button_new_with_label("Options");
    GtkWidget *fullscreen_button = gtk_toggle_button_new_with_label("Fullscreen");
    GtkWidget *quit_button = gtk_button_new_with_label("Quit");
    GtkWidget *spacer = gtk_label_new("");
    GtkWidget *welcome = gtk_label_new("NES Emulator\n\nTap Open Game to choose a .nes ROM.");
    gtk_window_set_title(GTK_WINDOW(window), "L:A_N:application_ID:nes_PC:N");
    gtk_window_set_default_size(GTK_WINDOW(window), 600, 760);
    gtk_window_set_position(GTK_WINDOW(window), GTK_WIN_POS_CENTER);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 10);
    gtk_button_box_set_layout(GTK_BUTTON_BOX(toolbar), GTK_BUTTONBOX_CENTER);
    gtk_box_set_spacing(GTK_BOX(toolbar), 6);
    gtk_box_pack_start(GTK_BOX(toolbar), open_button, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(toolbar), options_button, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(toolbar), fullscreen_button, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(toolbar), quit_button, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), toolbar, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), spacer, FALSE, FALSE, 8);
    gtk_label_set_justify(GTK_LABEL(welcome), GTK_JUSTIFY_CENTER);
    gtk_misc_set_alignment(GTK_MISC(welcome), 0.5f, 0.5f);
    gtk_box_pack_start(GTK_BOX(vbox), welcome, TRUE, TRUE, 0);
    gtk_container_add(GTK_CONTAINER(window), vbox);
    g_signal_connect(open_button, "clicked", G_CALLBACK(on_open_game_clicked), NULL);
    g_signal_connect(options_button, "clicked", G_CALLBACK(on_options_clicked), window);
    g_signal_connect(fullscreen_button, "toggled", G_CALLBACK(on_fullscreen_toggled), window);
    g_signal_connect(quit_button, "clicked", G_CALLBACK(on_quit_clicked), NULL);
    g_signal_connect(window, "delete-event", G_CALLBACK(on_main_window_delete), NULL);
    return window;
}

static gboolean poll_quit_signal(gpointer user_data) {
    (void)user_data;
    if (quitting) {
        if (gtk_main_level() > 0) gtk_main_quit();
        return FALSE;
    }
    return TRUE;
}

int main(int argc, char **argv) {
    const char *start_dir = (argc > 1) ? argv[1] : BROWSE_ROOT_DEFAULT;
    char resolved_start[MAX_PATH_LEN];

    fprintf(stderr, "NES: starting native GTK2 frontend\n");
    fflush(stderr);
    if (!gtk_init_check(&argc, &argv)) {
        fprintf(stderr, "GTK2 initialization failed; check DISPLAY and Kindle X11 environment\n");
        return 4;
    }
    if (!realpath(BROWSE_ROOT_DEFAULT, root_dir) ||
        !realpath(start_dir, resolved_start) ||
        !path_within_root(resolved_start, root_dir)) {
        fprintf(stderr, "Cannot initialize ROM picker under %s\n", BROWSE_ROOT_DEFAULT);
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
        fprintf(stderr, "FBInk initialization failed: %s\n", strerror(errno));
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
        fprintf(stderr, "Could not determine framebuffer dimensions\n");
        fbink_close(fbfd);
        return 2;
    }

    {
        struct fb_fix_screeninfo finfo;
        struct fb_var_screeninfo vinfo;
        memset(&finfo, 0, sizeof(finfo));
        memset(&vinfo, 0, sizeof(vinfo));
        if (ioctl(fbfd, FBIOGET_FSCREENINFO, &finfo) < 0 ||
            ioctl(fbfd, FBIOGET_VSCREENINFO, &vinfo) < 0) {
            fprintf(stderr, "NES: framebuffer info query failed: %s\n", strerror(errno));
            fbink_close(fbfd);
            return 2;
        }
        if (vinfo.bits_per_pixel != 8 || fb_state.bpp != 8 ||
            finfo.line_length == 0 || finfo.smem_len == 0) {
            fprintf(stderr, "NES: unsupported framebuffer format: vinfo=%u bpp, FBInk=%u bpp, stride=%u, memory=%u\n",
                    vinfo.bits_per_pixel, fb_state.bpp, finfo.line_length, finfo.smem_len);
            fbink_close(fbfd);
            return 2;
        }
        fb_line_length = finfo.line_length;
        fb_xoffset = vinfo.xoffset + fb_state.view_hori_origin;
        fb_yoffset = vinfo.yoffset + fb_state.view_vert_origin;
        fb_mem_len = finfo.smem_len;
        if ((uint64_t)fb_xoffset + (uint32_t)screen_w > fb_line_length) {
            fprintf(stderr, "NES: framebuffer stride too small for view (%u + %d > %u)\n",
                    fb_xoffset, screen_w, fb_line_length);
            fbink_close(fbfd);
            return 2;
        }
        fb_mem = (uint8_t *)mmap(NULL, fb_mem_len, PROT_READ | PROT_WRITE,
                                 MAP_SHARED, fbfd, 0);
        if (fb_mem == MAP_FAILED) {
            fb_mem = NULL;
            fprintf(stderr, "NES: framebuffer mmap failed: %s\n", strerror(errno));
            fbink_close(fbfd);
            return 2;
        }
        fprintf(stderr, "NES: framebuffer mapped %dx%d 8bpp stride=%u bytes=%lu offset=%u,%u inverted=%d\n",
                screen_w, screen_h, fb_line_length, (unsigned long)fb_mem_len,
                fb_xoffset, fb_yoffset, fb_state.inverted_grayscale);
        fprintf(stderr, "NES: using direct framebuffer copy + FBInk refresh (image blitter not required)\n");
        fflush(stderr);
    }

    screen_buf = (uint8_t *)malloc((size_t)screen_w * screen_h);
    if (!screen_buf) { munmap(fb_mem, fb_mem_len); fb_mem = NULL; fbink_close(fbfd); return 2; }
    /* Reserve a clean top toolbar and a separate bottom controller zone.
       The old fallback moved the game to y=30, causing it to overwrite the
       lower half of BACK/MENU/PAUSE on a 600x800 Kindle screen. */
    game_y = 60;
    game_h = screen_h - 270;
    game_w = game_h * 256 / 240;
    if (game_w > screen_w - 24) {
        game_w = screen_w - 24;
        game_h = game_w * 240 / 256;
    }
    game_x = (screen_w - game_w) / 2;
    game_buf = (uint8_t *)malloc((size_t)game_w * game_h);
    if (!game_buf) { free(screen_buf); munmap(fb_mem, fb_mem_len); fb_mem = NULL; fbink_close(fbfd); return 2; }

    main_window = create_main_window();
    fprintf(stderr, "NES: main window ready; waiting for Open Game\n");
    fflush(stderr);
    g_timeout_add(100, poll_quit_signal, NULL);
    while (!quitting) {
        if (!in_game) {
            char *rom_path;
            char rom_error[256];
            picker_requested = 0;
            gtk_widget_show_all(main_window);
            gtk_main();
            if (quitting) break;
            if (!picker_requested) continue;

            rom_path = nes_file_picker_run(GTK_WINDOW(main_window), root_dir,
                                           current_dir, sizeof(current_dir));
            if (!rom_path) {
                fprintf(stderr, "NES: ROM picker closed without a selection\n");
                fflush(stderr);
                continue;
            }
            fprintf(stderr, "NES: picker selected ROM=%s\n", rom_path);
            fflush(stderr);
            if (load_rom_path(rom_path, rom_error, sizeof(rom_error)) != 0) {
                fprintf(stderr, "NES: cannot start selected ROM: %s\n",
                        rom_error[0] ? rom_error : "unknown error");
                fflush(stderr);
                show_plain_error_dialog(GTK_WINDOW(main_window),
                                        rom_error[0] ? rom_error : "Unable to load this NES ROM.");
                free(rom_path);
                continue;
            }
            fprintf(stderr, "NES: selected ROM=%s\n", rom_path);
            fflush(stderr);
            free(rom_path);
            if (open_touch_device() < 0) {
                if (nes) xnes_ctx_free(nes);
                nes = NULL;
                in_game = 0;
                fprintf(stderr, "NES: touchscreen discovery failed; game launch cancelled\n");
                fflush(stderr);
                show_plain_error_dialog(GTK_WINDOW(main_window),
                                        "Touch input was not found. The game cannot start.");
                continue;
            }
            gtk_widget_hide(main_window);
            touch.was_active = 0;
            next_game_update_ns = 0;
            fprintf(stderr, "NES: game core ready; scheduling initial UI refresh after GTK main loop starts\n");
            fflush(stderr);
            g_idle_add(begin_game_display, NULL);
            gtk_main();
        }
    }

    release_controller();
    if (nes) xnes_ctx_free(nes);
    if (touch.fd >= 0) close(touch.fd);
    if (main_window) gtk_widget_destroy(main_window);
    fbink_cls(fbfd, &fb_cfg, NULL, false);
    if (fb_mem) { munmap(fb_mem, fb_mem_len); fb_mem = NULL; }
    fbink_close(fbfd);
    free(game_buf);
    free(screen_buf);
    return 0;
}
