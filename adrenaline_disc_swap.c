/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Adrenaline Disc Swap
 * Standalone PSP multi-disc ISO/CSO switcher for Vita Adrenaline/pspemu.
 *
 * Copyright (C) 2026 shoui520
 */

#include <pspkernel.h>
#include <pspctrl.h>
#include <pspdisplay.h>
#include <pspiofilemgr.h>
#include <pspumd.h>
#include <systemctrl.h>
#include <systemctrl_se.h>
#include <stdio.h>
#include <string.h>

PSP_MODULE_INFO("ADRENALINE_DISC_SWAP", PSP_MODULE_KERNEL, 1, 0);

#define HOTKEY (PSP_CTRL_SELECT | PSP_CTRL_LTRIGGER | PSP_CTRL_RTRIGGER)
#define MENU_KEYS (PSP_CTRL_SELECT | PSP_CTRL_START | PSP_CTRL_UP | PSP_CTRL_DOWN | \
                   PSP_CTRL_LEFT | PSP_CTRL_RIGHT | PSP_CTRL_LTRIGGER | PSP_CTRL_RTRIGGER | \
                   PSP_CTRL_CIRCLE | PSP_CTRL_CROSS | PSP_CTRL_TRIANGLE | PSP_CTRL_SQUARE | \
                   PSP_CTRL_HOME)

#define MAX_ENTRIES 128
#define MAX_PATH_LEN 256
#define MAX_NAME_LEN 256
#define MAX_KEY_LEN 256
#define VISIBLE_ROWS 12

#define PSP_UMD_NOT_PRESENT 1
#define PSP_UMD_CHANGED_ONLY 5
#define PSP_UMD_READY_CHANGED 54

#define INFERNO_ISO_PATHPTR_FROM_DRV 0xA4
#define INFERNO_ISO_FD_FROM_DRV 0x74
#define INFERNO_TOTAL_SECTORS_FROM_DRV 0x78
#define INFERNO_ISO_OPENED_FROM_DRV 0x128
#define INFERNO_IS_CSO_FROM_DRV 0x12C

#ifdef DISC_CHANGE_DIAG
#define DIAG_LOG_PATH "ms0:/seplugins/discchg_menu_diag.txt"
#endif

extern unsigned char msx[];
extern void sceUmdSetDriveStatus(int mode);
extern char *GetUmdFile(void);

typedef struct DiscEntry {
    char name[MAX_NAME_LEN];
    char path[MAX_PATH_LEN];
} DiscEntry;

static volatile int g_stop;
static DiscEntry g_all_entries[MAX_ENTRIES];
static int g_entries[MAX_ENTRIES];
static int g_all_count;
static int g_entry_count;
static int g_selected;
static int g_current_index;

static int g_pwidth;
static int g_pheight;
static int g_bufferwidth;
static int g_pixelformat;
static unsigned int *g_vram32;

static char g_current_path[MAX_PATH_LEN];
static char g_current_dir[MAX_PATH_LEN];
static char g_current_name[MAX_NAME_LEN];
static char g_status[96];

static int is_kernel_ptr(const void *p);

#ifdef DISC_CHANGE_DIAG
static int diag_strlen(const char *s)
{
    int n = 0;

    while (s && s[n])
        n++;

    return n;
}

static void diag_write_opened(SceUID fd, const char *s)
{
    sceIoWrite(fd, s, diag_strlen(s));
}

static void diag_reset(void)
{
    SceUID fd = sceIoOpen(DIAG_LOG_PATH, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);

    if (fd >= 0)
        sceIoClose(fd);
}

static void diag_raw(const char *s)
{
    SceUID fd = sceIoOpen(DIAG_LOG_PATH, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_APPEND, 0777);

    if (fd < 0)
        return;

    diag_write_opened(fd, s);
    sceIoClose(fd);
}

static void diag_str(const char *tag, const char *s)
{
    char line[384];

    snprintf(line, sizeof(line), "%s %s\n", tag, s ? s : "(null)");
    line[sizeof(line) - 1] = '\0';
    diag_raw(line);
}

static void diag_hex(const char *tag, unsigned int v)
{
    char line[64];

    snprintf(line, sizeof(line), "%s %08X\n", tag, v);
    diag_raw(line);
}

static void diag_file_size(const char *tag, const char *path)
{
    SceUID fd = sceIoOpen(path, PSP_O_RDONLY, 0777);
    int size = -1;

    diag_hex(tag, (unsigned int)fd);
    if (fd >= 0) {
        size = sceIoLseek32(fd, 0, PSP_SEEK_END);
        sceIoClose(fd);
    }

    diag_hex("file_size", (unsigned int)size);
}

#ifdef DISC_CHANGE_DIAG_DISC0
static void diag_disc0_umd_data(const char *tag)
{
    static const char *paths[] = {
        "disc0:/UMD_DATA.BIN",
        "umd0:/UMD_DATA.BIN",
        "umd:/UMD_DATA.BIN",
    };
    unsigned char buf[64];
    int i;

    diag_raw(tag);
    diag_raw("\n");

    for (i = 0; i < (int)(sizeof(paths) / sizeof(paths[0])); i++) {
        SceUID fd = sceIoOpen(paths[i], PSP_O_RDONLY, 0777);
        int rd = -1;

        diag_str("disc_path", paths[i]);
        diag_hex("disc_fd", (unsigned int)fd);

        if (fd >= 0) {
            memset(buf, 0, sizeof(buf));
            rd = sceIoRead(fd, buf, sizeof(buf) - 1);
            sceIoClose(fd);
            diag_hex("disc_read", (unsigned int)rd);
            if (rd > 0)
                diag_str("disc_data", (const char *)buf);
        }
    }
}
#endif

static void diag_live_inferno(PspIoDrv *drv, const char *stage)
{
    char **pathp;
    int fd;
    int pos;
    int end;

    diag_raw("diag_live_begin\n");
    diag_str("live_stage", stage);
    diag_hex("drv", (unsigned int)drv);

    if (drv == NULL)
        return;

    diag_hex("drv_name", (unsigned int)drv->name);
    diag_str("drv_name_s", drv->name);
    diag_hex("drv_name2", (unsigned int)drv->name2);
    diag_str("drv_name2_s", drv->name2);
    diag_hex("drv_funcs", (unsigned int)drv->funcs);

    fd = *(volatile int *)((unsigned char *)drv + INFERNO_ISO_FD_FROM_DRV);
    diag_raw("diag_live_fields\n");
    diag_hex("live_fd", (unsigned int)fd);
    diag_hex("live_total_sectors", *(volatile unsigned int *)((unsigned char *)drv + INFERNO_TOTAL_SECTORS_FROM_DRV));
    diag_hex("live_opened", *(volatile unsigned int *)((unsigned char *)drv + INFERNO_ISO_OPENED_FROM_DRV));
    diag_hex("live_is_cso", *(volatile unsigned int *)((unsigned char *)drv + INFERNO_IS_CSO_FROM_DRV));

    pathp = (char **)((unsigned char *)drv + INFERNO_ISO_PATHPTR_FROM_DRV);
    diag_hex("live_path_slot", (unsigned int)pathp);
    if (is_kernel_ptr(pathp)) {
        diag_hex("live_path_ptr", (unsigned int)*pathp);
        if (is_kernel_ptr(*pathp))
            diag_str("live_path", *pathp);
    }

    if (fd < 0)
        return;

    diag_raw("diag_live_lseek_pos\n");
    pos = sceIoLseek32(fd, 0, PSP_SEEK_CUR);
    diag_raw("diag_live_lseek_end\n");
    end = sceIoLseek32(fd, 0, PSP_SEEK_END);
    diag_hex("live_fd_pos", (unsigned int)pos);
    diag_hex("live_fd_size", (unsigned int)end);

    if (pos >= 0) {
        diag_raw("diag_live_restore_pos\n");
        sceIoLseek32(fd, pos, PSP_SEEK_SET);
    }
    diag_raw("diag_live_done\n");
}
#endif

#ifdef DISC_CHANGE_REMOUNT
static int refresh_disc0_mount(void)
{
#ifdef DISC_CHANGE_REMOUNT_DIRECT
    int value = 1;
    int ret_unassign;
    int ret_assign;

#ifdef DISC_CHANGE_DIAG
    diag_raw("direct_remount_begin\n");
#endif

    ret_unassign = sceIoUnassign("disc0:");
#ifdef DISC_CHANGE_DIAG
    diag_hex("direct_unassign_ret", (unsigned int)ret_unassign);
#endif

    sceKernelDelayThread(50000);

    ret_assign = sceIoAssign("disc0:", "umd0:", "isofs0:", IOASSIGN_RDONLY, &value, sizeof(value));
#ifdef DISC_CHANGE_DIAG
    diag_hex("direct_assign_ret", (unsigned int)ret_assign);
    diag_raw("direct_remount_end\n");
#endif

    if (ret_assign < 0)
        return ret_assign;

    if (ret_unassign < 0)
        return ret_unassign;

    return 0;
#else
    int ret_deactivate;
    int ret_activate;

#ifdef DISC_CHANGE_DIAG
    diag_raw("remount_begin\n");
#endif

    ret_deactivate = sceUmdDeactivate(1, "disc0:");
#ifdef DISC_CHANGE_DIAG
    diag_hex("remount_deactivate_ret", (unsigned int)ret_deactivate);
#endif

    sceKernelDelayThread(50000);

    ret_activate = sceUmdActivate(1, "disc0:");
#ifdef DISC_CHANGE_DIAG
    diag_hex("remount_activate_ret", (unsigned int)ret_activate);
    diag_raw("remount_end\n");
#endif

    if (ret_activate < 0)
        return ret_activate;

    if (ret_deactivate < 0)
        return ret_deactivate;

    return 0;
#endif
}
#endif

static int stricmp_ascii(const char *a, const char *b)
{
    while (*a && *b) {
        unsigned char ca = (unsigned char)*a++;
        unsigned char cb = (unsigned char)*b++;

        if (ca >= 'A' && ca <= 'Z')
            ca = (unsigned char)(ca + ('a' - 'A'));
        if (cb >= 'A' && cb <= 'Z')
            cb = (unsigned char)(cb + ('a' - 'A'));

        if (ca != cb)
            return (int)ca - (int)cb;
    }

    return (unsigned char)*a - (unsigned char)*b;
}

static int starts_with_icase(const char *s, const char *prefix)
{
    while (*prefix) {
        char a = *s++;
        char b = *prefix++;

        if (a >= 'A' && a <= 'Z')
            a = (char)(a + ('a' - 'A'));
        if (b >= 'A' && b <= 'Z')
            b = (char)(b + ('a' - 'A'));

        if (a != b)
            return 0;
    }

    return 1;
}

static int ends_with_icase(const char *s, const char *suffix)
{
    int ls = (int)strlen(s);
    int lf = (int)strlen(suffix);

    if (lf > ls)
        return 0;

    return stricmp_ascii(s + ls - lf, suffix) == 0;
}

static int is_iso_file(const char *name)
{
    return ends_with_icase(name, ".iso") || ends_with_icase(name, ".cso");
}

static void copy_string(char *dst, const char *src, int size)
{
    if (size <= 0)
        return;

    strncpy(dst, src ? src : "", size - 1);
    dst[size - 1] = '\0';
}

static void make_full_path(char *out, const char *dir, const char *name)
{
    int len;

    copy_string(out, dir, MAX_PATH_LEN);
    len = (int)strlen(out);
    if (len < MAX_PATH_LEN - 1)
        copy_string(out + len, name, MAX_PATH_LEN - len);
}

static int make_disc_key(const char *name, char *out, int out_size)
{
    int i = 0;
    int o = 0;
    int found = 0;

    while (name[i] && o < out_size - 1) {
        if (starts_with_icase(name + i, "disc")) {
            int j = i + 4;

            while (name[j] == ' ' || name[j] == '_' || name[j] == '-')
                j++;

            if (name[j] >= '0' && name[j] <= '9') {
                if (o < out_size - 1)
                    out[o++] = 'd';
                if (o < out_size - 1)
                    out[o++] = 'i';
                if (o < out_size - 1)
                    out[o++] = 's';
                if (o < out_size - 1)
                    out[o++] = 'c';
                if (o < out_size - 1)
                    out[o++] = '#';

                while (name[j] >= '0' && name[j] <= '9')
                    j++;

                i = j;
                found = 1;
                continue;
            }
        }

        char c = name[i++];
        if (c >= 'A' && c <= 'Z')
            c = (char)(c + ('a' - 'A'));

        out[o++] = c;
    }

    out[o] = '\0';
    return found;
}

static void split_current_path(void)
{
    const char *umd = GetUmdFile();
    char *slash;

    copy_string(g_current_path, umd, sizeof(g_current_path));

    slash = strrchr(g_current_path, '/');
    if (slash == NULL) {
        g_current_dir[0] = '\0';
        copy_string(g_current_name, g_current_path, sizeof(g_current_name));
        return;
    }

    copy_string(g_current_name, slash + 1, sizeof(g_current_name));
    slash[1] = '\0';
    copy_string(g_current_dir, g_current_path, sizeof(g_current_dir));
}

static void swap_entries(DiscEntry *a, DiscEntry *b)
{
    DiscEntry tmp = *a;
    *a = *b;
    *b = tmp;
}

static void sort_entries(DiscEntry *entries, int count)
{
    int i;
    int j;

    for (i = 1; i < count; i++) {
        for (j = i; j > 0 && stricmp_ascii(entries[j - 1].name, entries[j].name) > 0; j--)
            swap_entries(&entries[j - 1], &entries[j]);
    }
}

static void scan_disc_entries(void)
{
    SceUID dfd;
    SceIoDirent dent;
    char current_key[MAX_KEY_LEN];
    int has_key;
    int i;

    g_all_count = 0;
    g_entry_count = 0;
    g_selected = 0;
    g_current_index = -1;

    split_current_path();
    has_key = make_disc_key(g_current_name, current_key, sizeof(current_key));

    memset(&dent, 0, sizeof(dent));
    dfd = sceIoDopen(g_current_dir);
    if (dfd < 0) {
        snprintf(g_status, sizeof(g_status), "Dopen failed %08X", (unsigned int)dfd);
        return;
    }

    while (sceIoDread(dfd, &dent) > 0) {
        if (!FIO_SO_ISREG(dent.d_stat.st_attr))
            continue;
        if (!is_iso_file(dent.d_name))
            continue;
        if (g_all_count >= MAX_ENTRIES)
            continue;

        copy_string(g_all_entries[g_all_count].name, dent.d_name, MAX_NAME_LEN);
        make_full_path(g_all_entries[g_all_count].path, g_current_dir, dent.d_name);
        g_all_count++;
    }

    sceIoDclose(dfd);
    sort_entries(g_all_entries, g_all_count);

    if (has_key) {
        for (i = 0; i < g_all_count && g_entry_count < MAX_ENTRIES; i++) {
            char key[MAX_KEY_LEN];

            make_disc_key(g_all_entries[i].name, key, sizeof(key));
            if (stricmp_ascii(key, current_key) == 0)
                g_entries[g_entry_count++] = i;
        }
    }

    if (g_entry_count < 2) {
        for (i = 0; i < g_all_count; i++)
            g_entries[i] = i;
        g_entry_count = g_all_count;
    }

    for (i = 0; i < g_entry_count; i++) {
        DiscEntry *entry = &g_all_entries[g_entries[i]];

        if (stricmp_ascii(entry->path, g_current_path) == 0 ||
            stricmp_ascii(entry->name, g_current_name) == 0) {
            g_selected = i;
            g_current_index = i;
            break;
        }
    }

    snprintf(g_status, sizeof(g_status), "%d disc candidate%s", g_entry_count,
             g_entry_count == 1 ? "" : "s");
}

static unsigned int adjust_alpha(unsigned int col)
{
    unsigned int alpha = col >> 24;
    unsigned int c1;
    unsigned int c2;
    unsigned char mul;

    if (alpha == 0 || alpha == 0xFF)
        return col;

    c1 = col & 0x00FF00FF;
    c2 = col & 0x0000FF00;
    mul = (unsigned char)(255 - alpha);
    c1 = ((c1 * mul) >> 8) & 0x00FF00FF;
    c2 = ((c2 * mul) >> 8) & 0x0000FF00;

    return (alpha << 24) | c1 | c2;
}

static int blit_setup(void)
{
    int mode;

    sceDisplayGetMode(&mode, &g_pwidth, &g_pheight);
    sceDisplayGetFrameBuf((void *)&g_vram32, &g_bufferwidth, &g_pixelformat, mode);

    if (g_bufferwidth == 0 || g_pixelformat != 3 || g_vram32 == NULL)
        return -1;

    return 0;
}

static void blit_rect(int sx, int sy, int w, int h, unsigned int color)
{
    unsigned int col = adjust_alpha(color);
    unsigned int alpha = col >> 24;
    int x;
    int y;

    if (g_bufferwidth == 0 || g_pixelformat != 3 || g_vram32 == NULL)
        return;

    if (sx < 0) {
        w += sx;
        sx = 0;
    }
    if (sy < 0) {
        h += sy;
        sy = 0;
    }
    if (sx + w > g_pwidth)
        w = g_pwidth - sx;
    if (sy + h > g_pheight)
        h = g_pheight - sy;
    if (w <= 0 || h <= 0)
        return;

    for (y = 0; y < h; y++) {
        int offset = (sy + y) * g_bufferwidth + sx;

        for (x = 0; x < w; x++) {
            if (alpha == 0) {
                g_vram32[offset + x] = col;
            } else if (alpha != 0xFF) {
                unsigned int c2 = g_vram32[offset + x];
                unsigned int c1 = c2 & 0x00FF00FF;
                c2 &= 0x0000FF00;
                c1 = ((c1 * alpha) >> 8) & 0x00FF00FF;
                c2 = ((c2 * alpha) >> 8) & 0x0000FF00;
                g_vram32[offset + x] = (col & 0xFFFFFF) + c1 + c2;
            }
        }
    }
}

static int blit_string(int sx, int sy, int fcolor, int bcolor, const char *msg)
{
    unsigned int fg_col = adjust_alpha((unsigned int)fcolor);
    unsigned int bg_col = adjust_alpha((unsigned int)bcolor);
    int x;

    if (g_bufferwidth == 0 || g_pixelformat != 3 || g_vram32 == NULL)
        return -1;

    if (sx < 0 || sy < 0 || sy + 8 > g_pheight)
        return -1;

    for (x = 0; msg[x] && sx + (x + 1) * 8 <= g_pwidth; x++) {
        unsigned char code = (unsigned char)msg[x] & 0x7F;
        int y;

        for (y = 0; y < 8; y++) {
            int offset = (sy + y) * g_bufferwidth + sx + x * 8;
            unsigned char font = y >= 7 ? 0x00 : msx[code * 8 + y];
            int p;

            for (p = 0; p < 8; p++) {
                unsigned int col = (font & 0x80) ? fg_col : bg_col;
                unsigned int alpha = col >> 24;

                if (alpha == 0) {
                    g_vram32[offset] = col;
                } else if (alpha != 0xFF) {
                    unsigned int c2 = g_vram32[offset];
                    unsigned int c1 = c2 & 0x00FF00FF;
                    c2 &= 0x0000FF00;
                    c1 = ((c1 * alpha) >> 8) & 0x00FF00FF;
                    c2 = ((c2 * alpha) >> 8) & 0x0000FF00;
                    g_vram32[offset] = (col & 0xFFFFFF) + c1 + c2;
                }

                font <<= 1;
                offset++;
            }
        }
    }

    return x;
}

static void shorten_middle(char *out, const char *in, int max_chars)
{
    int len = (int)strlen(in);
    int head;
    int tail;

    if (max_chars < 8) {
        copy_string(out, in, max_chars + 1);
        return;
    }

    if (len <= max_chars) {
        copy_string(out, in, max_chars + 1);
        return;
    }

    head = (max_chars - 3) / 2;
    tail = max_chars - 3 - head;

    memcpy(out, in, head);
    memcpy(out + head, "...", 3);
    memcpy(out + head + 3, in + len - tail, tail);
    out[max_chars] = '\0';
}

static void draw_menu(void)
{
    int first;
    int i;
    char line[128];

    if (blit_setup() < 0)
        return;

    first = (g_selected / VISIBLE_ROWS) * VISIBLE_ROWS;

    blit_rect(24, 18, 432, 228, 0xB0000000);
    blit_rect(24, 18, 432, 12, 0x0000A0A0);

    blit_string(168, 20, 0x00FFFFFF, 0x0000A0A0, "DISC CHANGE MENU");

    shorten_middle(line, g_current_name, 50);
    blit_string(40, 42, 0x00FFFFFF, 0xB0000000, "Current:");
    blit_string(112, 42, 0x00FFFFFF, 0xB0000000, line);

    snprintf(line, sizeof(line), "%s  Page %d/%d", g_status,
             g_entry_count == 0 ? 0 : (first / VISIBLE_ROWS) + 1,
             g_entry_count == 0 ? 0 : ((g_entry_count - 1) / VISIBLE_ROWS) + 1);
    blit_string(40, 54, 0x00FFFFFF, 0xB0000000, line);

    for (i = 0; i < VISIBLE_ROWS; i++) {
        int idx = first + i;
        int y = 72 + i * 10;
        unsigned int bg = (idx == g_selected) ? 0x003060C0 : 0xB0000000;
        char prefix[16];
        char name[64];

        blit_rect(36, y - 1, 408, 9, bg);

        if (idx >= g_entry_count)
            continue;

        snprintf(prefix, sizeof(prefix), "%c%02d ",
                 idx == g_current_index ? '*' : ' ', idx + 1);
        shorten_middle(name, g_all_entries[g_entries[idx]].name, 47);

        blit_string(44, y, 0x00FFFFFF, bg, prefix);
        blit_string(84, y, 0x00FFFFFF, bg, name);
    }

    blit_string(40, 210, 0x00FFFFFF, 0xB0000000, "UP/DOWN select  L/R page");
    blit_string(40, 222, 0x00FFFFFF, 0xB0000000, "O change  X cancel  SELECT close");
}

static unsigned int decode_jal_target(unsigned int pc, unsigned int insn)
{
    return (pc & 0xF0000000) | ((insn & 0x03FFFFFF) << 2);
}

static unsigned int find_inferno_reopen(PspIoDrv *drv)
{
    unsigned int ioopen;
    unsigned int ptr;
    int i;

    if (drv == NULL || drv->funcs == NULL || drv->funcs->IoOpen == NULL)
        return 0;

    ioopen = (unsigned int)drv->funcs->IoOpen;

    for (i = 0, ptr = ioopen; i < 64; i++, ptr += 4) {
        unsigned int insn = *(volatile unsigned int *)ptr;

        if ((insn & 0xFC000000) == 0x0C000000) {
            unsigned int target = decode_jal_target(ptr, insn);

            if (target > ioopen && target < ioopen + 0x2000)
                return target;
        }
    }

    return 0;
}

static int is_kernel_ptr(const void *p)
{
    unsigned int v = (unsigned int)p;
    return v >= 0x88000000 && v < 0x8A000000;
}

static int inferno_reopen_path(const char *path)
{
    PspIoDrv *drv;
    unsigned int reopen;
    char **inferno_path;
    int ret;

#ifdef DISC_CHANGE_DIAG
    diag_reset();
    diag_raw("swap_begin\n");
    diag_str("selected_path", path);
    diag_file_size("selected_fd", path);
    diag_str("getumd_before", GetUmdFile());
#ifdef DISC_CHANGE_DIAG_DISC0
    diag_disc0_umd_data("disc0_before");
#endif
    diag_raw("before_finddrv\n");
#endif

    drv = sctrlHENFindDriver("umd");
#ifdef DISC_CHANGE_DIAG
    diag_raw("after_finddrv\n");
#endif
    reopen = find_inferno_reopen(drv);
#ifdef DISC_CHANGE_DIAG
    diag_raw("after_find_reopen\n");
    diag_hex("reopen_target", reopen);
    diag_live_inferno(drv, "before");
#endif
    if (reopen == 0)
        return 0x80010001;

    sceUmdSetDriveStatus(PSP_UMD_NOT_PRESENT);
#ifdef DISC_CHANGE_DIAG
    diag_raw("after_status_not_present\n");
#endif
    SetUmdFile((char *)path);
#ifdef DISC_CHANGE_DIAG
    diag_raw("after_setumd\n");
    diag_str("getumd_after_set", GetUmdFile());
#endif

    inferno_path = (char **)((unsigned char *)drv + INFERNO_ISO_PATHPTR_FROM_DRV);
    if (is_kernel_ptr(inferno_path) && is_kernel_ptr(*inferno_path)) {
#ifdef DISC_CHANGE_DIAG
        diag_str("inferno_path_pre_copy", *inferno_path);
#endif
        copy_string(*inferno_path, path, MAX_PATH_LEN);
#ifdef DISC_CHANGE_DIAG
        diag_str("inferno_path_post_copy", *inferno_path);
#endif
    }

    sceUmdSetDriveStatus(PSP_UMD_CHANGED_ONLY);
#ifdef DISC_CHANGE_DIAG
    diag_raw("before_reopen_call\n");
#endif
    ret = ((int (*)(void))reopen)();
#ifdef DISC_CHANGE_DIAG
    diag_raw("after_reopen_call\n");
    diag_hex("reopen_ret", (unsigned int)ret);
    diag_str("getumd_after_reopen", GetUmdFile());
    diag_live_inferno(drv, "after");
#ifdef DISC_CHANGE_DIAG_DISC0
    diag_disc0_umd_data("disc0_after_reopen");
#endif
#endif
    sceUmdSetDriveStatus(PSP_UMD_CHANGED_ONLY);
    sceKernelDelayThread(100000);
#ifdef DISC_CHANGE_REMOUNT
    ret = refresh_disc0_mount();
#ifdef DISC_CHANGE_DIAG
    diag_hex("refresh_disc0_ret", (unsigned int)ret);
#endif
    if (ret < 0)
        return ret;
    sceKernelDelayThread(100000);
#endif
    sceUmdSetDriveStatus(PSP_UMD_READY_CHANGED);
#ifdef DISC_CHANGE_DIAG
#ifdef DISC_CHANGE_DIAG_DISC0
    diag_disc0_umd_data("disc0_after_ready");
#endif
    diag_raw("swap_end\n");
#endif

    return ret;
}

static int buttons_are_up(void)
{
    SceCtrlData pad;
    memset(&pad, 0, sizeof(pad));
    sceCtrlPeekBufferPositive(&pad, 1);
    return (pad.Buttons & MENU_KEYS) == 0;
}

static void wait_buttons_up(void)
{
    while (!g_stop && !buttons_are_up()) {
        sceDisplayWaitVblankStart();
        sceKernelDelayThread(1000);
    }
}

static void show_result(const char *message, int frames)
{
    int i;
    copy_string(g_status, message, sizeof(g_status));

    for (i = 0; i < frames && !g_stop; i++) {
        sceDisplayWaitVblankStart();
        draw_menu();
    }
}

static void run_menu(void)
{
    SceCtrlData pad;
    unsigned int old_buttons = 0;
    int done = 0;

    scan_disc_entries();
    wait_buttons_up();

    memset(&pad, 0, sizeof(pad));

    while (!g_stop && !done) {
        unsigned int pressed;

        sceDisplayWaitVblankStart();
        sceCtrlPeekBufferPositive(&pad, 1);
        pressed = pad.Buttons & ~old_buttons;
        old_buttons = pad.Buttons;

        if (g_entry_count > 0) {
            if (pressed & PSP_CTRL_UP)
                g_selected = (g_selected + g_entry_count - 1) % g_entry_count;
            if (pressed & PSP_CTRL_DOWN)
                g_selected = (g_selected + 1) % g_entry_count;
            if (pressed & (PSP_CTRL_LEFT | PSP_CTRL_LTRIGGER)) {
                g_selected -= VISIBLE_ROWS;
                if (g_selected < 0)
                    g_selected = 0;
            }
            if (pressed & (PSP_CTRL_RIGHT | PSP_CTRL_RTRIGGER)) {
                g_selected += VISIBLE_ROWS;
                if (g_selected >= g_entry_count)
                    g_selected = g_entry_count - 1;
            }
        }

        if (pressed & (PSP_CTRL_CROSS | PSP_CTRL_SELECT | PSP_CTRL_HOME)) {
            wait_buttons_up();
            done = 1;
        } else if ((pressed & PSP_CTRL_CIRCLE) && g_entry_count > 0) {
            int ret;

            wait_buttons_up();

            DiscEntry *entry = &g_all_entries[g_entries[g_selected]];

            if (stricmp_ascii(entry->path, g_current_path) == 0) {
                show_result("Already selected", 30);
                done = 1;
            } else {
                copy_string(g_status, "Changing disc...", sizeof(g_status));
                draw_menu();
                ret = inferno_reopen_path(entry->path);
                if (ret == 0) {
                    copy_string(g_current_path, entry->path, sizeof(g_current_path));
                    copy_string(g_current_name, entry->name, sizeof(g_current_name));
                    g_current_index = g_selected;
                    show_result("Disc changed", 45);
                } else {
                    snprintf(g_status, sizeof(g_status), "Change failed %08X", (unsigned int)ret);
                    show_result(g_status, 90);
                }
                done = 1;
            }
        }

        draw_menu();
    }

    wait_buttons_up();
}

static int menu_thread(SceSize args, void *argp)
{
    SceCtrlData pad;
    unsigned int hotkey_down = 0;

    (void)args;
    (void)argp;

    memset(&pad, 0, sizeof(pad));

    while (!g_stop) {
        sceCtrlPeekBufferPositive(&pad, 1);

        if ((pad.Buttons & HOTKEY) == HOTKEY) {
            if (!hotkey_down) {
                hotkey_down = 1;
                run_menu();
            }
        } else {
            hotkey_down = 0;
        }

        sceKernelDelayThread(50000);
    }

    return 0;
}

int module_start(SceSize args, void *argp)
{
    SceUID thid;

    (void)args;
    (void)argp;

    g_stop = 0;
    thid = sceKernelCreateThread("adrenaline_disc_swap", menu_thread, 32, 0x4000, 0, NULL);
    if (thid >= 0)
        sceKernelStartThread(thid, 0, NULL);

    return 0;
}

int module_stop(void)
{
    g_stop = 1;
    return 0;
}
