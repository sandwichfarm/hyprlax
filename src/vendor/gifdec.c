#include "gifdec.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

#define MIN(A, B) ((A) < (B) ? (A) : (B))
#define MAX(A, B) ((A) > (B) ? (A) : (B))

typedef struct Entry {
    uint16_t length;
    uint16_t prefix;
    uint8_t suffix;
} Entry;

typedef struct Table {
    int bulk;
    int nentries;
    Entry *entries;
} Table;

static uint16_t
read_num(int fd)
{
    uint8_t bytes[2];

    read(fd, bytes, 2);
    return bytes[0] + (((uint16_t) bytes[1]) << 8);
}

gd_GIF *
gd_open_gif(const char *fname)
{
    int fd;
    uint8_t sigver[3];
    uint16_t width, height, depth;
    uint8_t fdsz, bgidx, aspect;
    int i;
    uint8_t *bgcolor;
    int gct_sz;
    gd_GIF *gif;

    if (!fname)
        return NULL;

    fd = open(fname, O_RDONLY);
    if (fd == -1)
        return NULL;

#ifdef _WIN32
    setmode(fd, O_BINARY);
#endif

    /* Header */
    if (read(fd, sigver, 3) != 3 || memcmp(sigver, "GIF", 3) != 0) {
        fprintf(stderr, "invalid signature\n");
        goto fail;
    }

    /* Version */
    if (read(fd, sigver, 3) != 3 ||
        (memcmp(sigver, "89a", 3) != 0 && memcmp(sigver, "87a", 3) != 0)) {
        fprintf(stderr, "invalid version\n");
        goto fail;
    }

    /* Width x Height */
    width = read_num(fd);
    height = read_num(fd);

    /* FDSZ */
    read(fd, &fdsz, 1);

    /* Color Space's Depth */
    depth = ((fdsz >> 4) & 7) + 1;

    /* Ignore Sort Flag. */

    /* GCT Size */
    gct_sz = (fdsz & 0x80) ? 1 << ((fdsz & 0x07) + 1) : 0;

    /* Background Color Index */
    read(fd, &bgidx, 1);

    /* Aspect Ratio */
    read(fd, &aspect, 1);

    /* Create gd_GIF Structure. */
    gif = calloc(1, sizeof(*gif));
    if (!gif)
        goto fail;
    gif->fd = fd;
    gif->width = width;
    gif->height = height;
    gif->depth = depth;

    /* Read GCT */
    gif->gct.size = gct_sz;
    if (read(fd, gif->gct.colors, 3 * gif->gct.size) != 3 * gif->gct.size) {
        free(gif);
        goto fail;
    }
    gif->palette = &gif->gct;
    /* Without a GCT the background index is undefined; keep the black canvas. */
    gif->bgindex = gct_sz ? bgidx : 0;
    gif->frame = calloc(4, width * height);
    if (!gif->frame) {
        free(gif);
        goto fail;
    }
    gif->canvas = &gif->frame[width * height];
    if (gif->bgindex)
        memset(gif->frame, gif->bgindex, gif->width * gif->height);
    bgcolor = &gif->palette->colors[gif->bgindex*3];
    if (bgcolor[0] || bgcolor[1] || bgcolor [2])
        for (i = 0; i < gif->width * gif->height; i++)
            memcpy(&gif->canvas[i*3], bgcolor, 3);
    gif->anim_start = lseek(fd, 0, SEEK_CUR);

    goto ok;

fail:
    close(fd);
    return 0;

ok:
    return gif;
}

/* Read a complete data sub-block. Return its size, 0 for the terminator,
 * or -1 for a truncated block or read error. */
static int
read_sub_block(gd_GIF *gif, uint8_t buffer[255])
{
    uint8_t size;

    if (read(gif->fd, &size, 1) != 1)
        return -1;
    if (read(gif->fd, buffer, size) != size)
        return -1;
    return size;
}

static int
discard_sub_blocks(gd_GIF *gif)
{
    uint8_t buffer[255];
    int size;

    /* Read rather than seek: seeking can succeed beyond the end of a file. */
    do {
        size = read_sub_block(gif, buffer);
    } while (size > 0);
    return size;
}

static int
read_plain_text_ext(gd_GIF *gif)
{
    uint8_t block[255];

    if (read_sub_block(gif, block) != 12)
        return -1;
    if (gif->plain_text) {
        uint16_t tx = block[0] | (block[1] << 8);
        uint16_t ty = block[2] | (block[3] << 8);
        uint16_t tw = block[4] | (block[5] << 8);
        uint16_t th = block[6] | (block[7] << 8);
        off_t sub_block = lseek(gif->fd, 0, SEEK_CUR);
        if (sub_block == -1)
            return -1;
        gif->plain_text(gif, tx, ty, tw, th, block[8], block[9], block[10], block[11]);
        if (lseek(gif->fd, sub_block, SEEK_SET) == -1)
            return -1;
    }
    return discard_sub_blocks(gif);
}

static int
read_graphic_control_ext(gd_GIF *gif)
{
    uint8_t block[255], terminator;

    if (read_sub_block(gif, block) != 4 ||
        read(gif->fd, &terminator, 1) != 1 || terminator != 0)
        return -1;
    gif->gce.disposal = (block[0] >> 2) & 3;
    gif->gce.input = block[0] & 2;
    gif->gce.transparency = block[0] & 1;
    gif->gce.delay = block[1] | (block[2] << 8);
    gif->gce.tindex = block[3];
    return 0;
}

static int
read_comment_ext(gd_GIF *gif)
{
    if (gif->comment) {
        off_t sub_block = lseek(gif->fd, 0, SEEK_CUR);
        if (sub_block == -1)
            return -1;
        gif->comment(gif);
        if (lseek(gif->fd, sub_block, SEEK_SET) == -1)
            return -1;
    }
    return discard_sub_blocks(gif);
}

static int
read_application_ext(gd_GIF *gif)
{
    uint8_t block[255];

    if (read_sub_block(gif, block) != 11)
        return -1;
    if (!memcmp(block, "NETSCAPE", 8)) {
        if (read_sub_block(gif, block) != 3 || block[0] != 1)
            return -1;
        gif->loop_count = block[1] | (block[2] << 8);
    } else if (gif->application) {
        off_t sub_block = lseek(gif->fd, 0, SEEK_CUR);
        if (sub_block == -1)
            return -1;
        gif->application(gif, (char *)block, (char *)block + 8);
        if (lseek(gif->fd, sub_block, SEEK_SET) == -1)
            return -1;
    }
    return discard_sub_blocks(gif);
}

static int
read_ext(gd_GIF *gif)
{
    uint8_t label;

    if (read(gif->fd, &label, 1) != 1)
        return -1;
    switch (label) {
    case 0x01:
        return read_plain_text_ext(gif);
    case 0xF9:
        return read_graphic_control_ext(gif);
    case 0xFE:
        return read_comment_ext(gif);
    case 0xFF:
        return read_application_ext(gif);
    default:
        fprintf(stderr, "unknown extension: %02X\n", label);
        return discard_sub_blocks(gif);
    }
}

static Table *
new_table(int key_size)
{
    int key;
    int init_bulk = MAX(1 << (key_size + 1), 0x100);
    Table *table = malloc(sizeof(*table) + sizeof(Entry) * init_bulk);
    if (table) {
        table->bulk = init_bulk;
        table->nentries = (1 << key_size) + 2;
        table->entries = (Entry *) &table[1];
        for (key = 0; key < (1 << key_size); key++)
            table->entries[key] = (Entry) {1, 0xFFF, key};
    }
    return table;
}

/* Add table entry. Return value:
 *  0 on success
 * +1 if key size must be incremented after this addition
 * -1 if could not realloc table */
static int
add_entry(Table **tablep, uint16_t length, uint16_t prefix, uint8_t suffix)
{
    Table *table = *tablep;
    if (table->nentries == table->bulk) {
        table->bulk *= 2;
        table = realloc(table, sizeof(*table) + sizeof(Entry) * table->bulk);
        if (!table) return -1;
        table->entries = (Entry *) &table[1];
        *tablep = table;
    }
    table->entries[table->nentries] = (Entry) {length, prefix, suffix};
    table->nentries++;
    if ((table->nentries & (table->nentries - 1)) == 0)
        return 1;
    return 0;
}

static uint16_t
get_key(gd_GIF *gif, int key_size, uint8_t *sub_len, uint8_t *shift, uint8_t *byte)
{
    int bits_read;
    int rpad;
    int frag_size;
    uint16_t key;

    key = 0;
    for (bits_read = 0; bits_read < key_size; bits_read += frag_size) {
        rpad = (*shift + bits_read) % 8;
        if (rpad == 0) {
            /* Update byte. */
            if (*sub_len == 0) {
                if (read(gif->fd, sub_len, 1) != 1 || *sub_len == 0)
                    return 0x1000;
            }
            if (read(gif->fd, byte, 1) != 1)
                return 0x1000;
            (*sub_len)--;
        }
        frag_size = MIN(key_size - bits_read, 8 - rpad);
        key |= ((uint16_t) ((*byte) >> rpad)) << bits_read;
    }
    /* Clear extra bits to the left. */
    key &= (1 << key_size) - 1;
    *shift = (*shift + key_size) % 8;
    return key;
}

/* Compute output index of y-th input line, in frame of height h. */
static int
interlaced_line_index(int h, int y)
{
    int p; /* number of lines in current pass */

    p = (h - 1) / 8 + 1;
    if (y < p) /* pass 1 */
        return y * 8;
    y -= p;

    p = (h - 5) / 8 + 1;
    if (y < p) /* pass 2 */
        return y * 8 + 4;
    y -= p;

    p = (h - 3) / 4 + 1;
    if (y < p) /* pass 3 */
        return y * 4 + 2;
    y -= p;

    /* pass 4 */
    return y * 2 + 1;
}

/* Decompress image pixels.
 * Return 0 on success or -1 on read, seek or LZW table allocation failure. */
static int
read_image_data(gd_GIF *gif, int interlace)
{
    uint8_t sub_len, shift, byte;
    int init_key_size, key_size, table_is_full;
    int frm_off, frm_size, str_len, i, p, x, y;
    uint16_t key, clear, stop;
    int ret;
    Table *table;
    Entry entry;
    off_t start, end;

    if (read(gif->fd, &byte, 1) != 1)
        return -1;
    key_size = (int) byte;
    if (key_size < 2 || key_size > 8)
        return -1;

    start = lseek(gif->fd, 0, SEEK_CUR);
    if (start == -1 || discard_sub_blocks(gif) == -1)
        return -1;
    end = lseek(gif->fd, 0, SEEK_CUR);
    if (end == -1 || lseek(gif->fd, start, SEEK_SET) == -1)
        return -1;

    clear = 1 << key_size;
    stop = clear + 1;
    table = new_table(key_size);
    if (!table)
        return -1;
    key_size++;
    init_key_size = key_size;
    sub_len = shift = 0;
    key = get_key(gif, key_size, &sub_len, &shift, &byte); /* clear code */
    if (key != clear) {
        free(table);
        return -1;
    }

    frm_off = 0;
    ret = 0;
    frm_size = gif->fw*gif->fh;
    while (frm_off < frm_size) {
        if (key == clear) {
            key_size = init_key_size;
            table->nentries = (1 << (key_size - 1)) + 2;
            table_is_full = 0;
        } else if (!table_is_full) {
            ret = add_entry(&table, str_len + 1, key, entry.suffix);
            if (ret == -1) {
                free(table);
                return -1;
            }
            if (table->nentries == 0x1000) {
                ret = 0;
                table_is_full = 1;
            }
        }
        key = get_key(gif, key_size, &sub_len, &shift, &byte);
        if (key == clear)
            continue;
        if (key == 0x1000) {
            free(table);
            return -1;
        }
        if (key == stop)
            break;
        if (ret == 1)
            key_size++;

        entry = table->entries[key];
        str_len = entry.length;
        for (i = 0; i < str_len; i++) {
            p = frm_off + entry.length - 1;
            x = p % gif->fw;
            y = p / gif->fw;
            if (interlace)
                y = interlaced_line_index((int) gif->fh, y);
            gif->frame[(gif->fy + y) * gif->width + gif->fx + x] = entry.suffix;
            if (entry.prefix == 0xFFF)
                break;
            else
                entry = table->entries[entry.prefix];
        }
        frm_off += str_len;
        if (key < table->nentries - 1 && !table_is_full)
            table->entries[table->nentries - 1].suffix = entry.suffix;
    }
    free(table);
    return lseek(gif->fd, end, SEEK_SET) == -1 ? -1 : 0;
}

/* Read image. Return 0 on success or -1 on decoding failure. */
static int
read_image(gd_GIF *gif)
{
    uint8_t fisrz;
    int interlace;

    /* Image Descriptor. */
    gif->fx = read_num(gif->fd);
    gif->fy = read_num(gif->fd);
    if (gif->fx >= gif->width || gif->fy >= gif->height)
        return -1;

    gif->fw = read_num(gif->fd);
    gif->fh = read_num(gif->fd);

    gif->fw = MIN(gif->fw, gif->width - gif->fx);
    gif->fh = MIN(gif->fh, gif->height - gif->fy);

    read(gif->fd, &fisrz, 1);
    interlace = fisrz & 0x40;
    /* Ignore Sort Flag. */

    /* Local Color Table? */
    if (fisrz & 0x80) {
        /* Read LCT */
        gif->lct.size = 1 << ((fisrz & 0x07) + 1);
        read(gif->fd, gif->lct.colors, 3 * gif->lct.size);
        gif->palette = &gif->lct;
    } else {
        if (!gif->gct.size)
            return -1;
        gif->palette = &gif->gct;
    }

    /* Image Data. */
    return read_image_data(gif, interlace);
}

static void
render_frame_rect(gd_GIF *gif, uint8_t *buffer)
{
    int i, j, k;
    uint8_t index, *color;

    i = gif->fy * gif->width + gif->fx;
    for (j = 0; j < gif->fh; j++) {
        for (k = 0; k < gif->fw; k++) {
            index = gif->frame[(gif->fy + j) * gif->width + gif->fx + k];
            color = &gif->palette->colors[index*3];
            if (!gif->gce.transparency || index != gif->gce.tindex)
                memcpy(&buffer[(i+k)*3], color, 3);
        }
        i += gif->width;
    }
}

static void
dispose(gd_GIF *gif)
{
    int i, j, k;
    uint8_t *bgcolor;

    switch (gif->gce.disposal) {
    case 2: /* Restore to background color. */
        bgcolor = &gif->gct.colors[gif->bgindex*3];
        i = gif->fy * gif->width + gif->fx;
        for (j = 0; j < gif->fh; j++) {
            for (k = 0; k < gif->fw; k++)
                memcpy(&gif->canvas[(i+k)*3], bgcolor, 3);
            i += gif->width;
        }
        break;
    case 3: /* Restore to previous, i.e., don't update canvas.*/
        break;
    default: /* Add frame non-transparent pixels to canvas. */
        render_frame_rect(gif, gif->canvas);
    }
}

/* Return 1 if got a frame; 0 if got GIF trailer; -1 if error. */
int
gd_get_frame(gd_GIF *gif)
{
    char sep;

    dispose(gif);

    if (read(gif->fd, &sep, 1) != 1)
        return -1;
    while (sep != ',') {
        if (sep == ';')
            return 0;
        if (sep != '!' || read_ext(gif) == -1)
            return -1;
        if (read(gif->fd, &sep, 1) != 1)
            return -1;
    }
    if (read_image(gif) == -1)
        return -1;

    return 1;
}

void
gd_render_frame(gd_GIF *gif, uint8_t *buffer)
{
    memcpy(buffer, gif->canvas, gif->width * gif->height * 3);
    render_frame_rect(gif, buffer);
}

int
gd_is_bgcolor(gd_GIF *gif, uint8_t color[3])
{
    return !memcmp(&gif->palette->colors[gif->bgindex*3], color, 3);
}

void
gd_rewind(gd_GIF *gif)
{
    lseek(gif->fd, gif->anim_start, SEEK_SET);
}

void
gd_close_gif(gd_GIF *gif)
{
    close(gif->fd);
    free(gif->frame);
    free(gif);
}
