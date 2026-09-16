#include "viewbbc/dfs.h"
#include "viewbbc/file_io.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#define DFS_SECTOR_SIZE 256u
#define DFS_SECTORS_PER_TRACK 10u
#define DFS_TRACK_BYTES (DFS_SECTOR_SIZE * DFS_SECTORS_PER_TRACK)

static int ascii_ieq_char(char a, char b) {
    return toupper((unsigned char)a) == toupper((unsigned char)b);
}

static int ascii_ieq(const char *a, const char *b) {
    while (*a && *b) {
        if (!ascii_ieq_char(*a, *b)) return 0;
        ++a;
        ++b;
    }
    return *a == '\0' && *b == '\0';
}

static int ends_with_ci(const char *text, const char *suffix) {
    size_t a = text ? strlen(text) : 0;
    size_t b = suffix ? strlen(suffix) : 0;
    if (b > a) return 0;
    return ascii_ieq(text + a - b, suffix);
}

ViewBBCDFSType viewbbc_dfs_type_from_path(const char *path) {
    if (!path) return VIEWBBC_DFS_TYPE_UNKNOWN;
    if (ends_with_ci(path, ".ssd") || ends_with_ci(path, ".sd")) return VIEWBBC_DFS_TYPE_SSD;
    if (ends_with_ci(path, ".dsd") || ends_with_ci(path, ".dd")) return VIEWBBC_DFS_TYPE_DSD;
    return VIEWBBC_DFS_TYPE_UNKNOWN;
}

int viewbbc_dfs_path_is_image(const char *path) {
    return viewbbc_dfs_type_from_path(path) != VIEWBBC_DFS_TYPE_UNKNOWN;
}

const char *viewbbc_dfs_type_name(ViewBBCDFSType type) {
    switch (type) {
        case VIEWBBC_DFS_TYPE_SSD: return "SSD";
        case VIEWBBC_DFS_TYPE_DSD: return "DSD";
        default: return "UNKNOWN";
    }
}

static int dfs_offset(const ViewBBCDFSImage *image, int side, uint16_t logical_sector, size_t *offset_out) {
    if (!image || !offset_out || side < 0 || side >= image->side_count) return 0;
    size_t track = logical_sector / DFS_SECTORS_PER_TRACK;
    size_t sector = logical_sector % DFS_SECTORS_PER_TRACK;
    size_t offset;

    if (image->interleaved) {
        if (track > SIZE_MAX / (DFS_TRACK_BYTES * 2u)) return 0;
        offset = track * (DFS_TRACK_BYTES * 2u);
        if (side == 1) offset += DFS_TRACK_BYTES;
        if (sector > (SIZE_MAX - offset) / DFS_SECTOR_SIZE) return 0;
        offset += sector * DFS_SECTOR_SIZE;
    } else {
        offset = (size_t)logical_sector * DFS_SECTOR_SIZE;
    }

    if (offset > image->image_size || image->image_size - offset < DFS_SECTOR_SIZE) return 0;
    *offset_out = offset;
    return 1;
}

static uint32_t decode_18(uint8_t lo, uint8_t hi, uint8_t packed, unsigned shift) {
    return (uint32_t)lo | ((uint32_t)hi << 8u) | ((uint32_t)(packed & (uint8_t)(0x03u << shift)) << (16u - shift));
}

static int parse_side(ViewBBCDFSImage *image, int side) {
    size_t cat0_offset, cat1_offset;
    if (!dfs_offset(image, side, 0, &cat0_offset) || !dfs_offset(image, side, 1, &cat1_offset)) return 0;

    const uint8_t *names = image->image_data + cat0_offset;
    const uint8_t *info = image->image_data + cat1_offset;
    ViewBBCDFSSide *out = &image->sides[side];
    *out = (ViewBBCDFSSide){0};
    out->side = side;

    size_t tw = 0;
    for (size_t i = 0; i < 8 && names[i] && tw < 12; ++i) out->title[tw++] = (char)(names[i] & 0x7fu);
    for (size_t i = 0; i < 4 && info[i] && tw < 12; ++i) out->title[tw++] = (char)(info[i] & 0x7fu);
    while (tw && out->title[tw - 1] == ' ') tw--;
    out->title[tw] = '\0';

    size_t count = (size_t)(info[5] >> 3u);
    if (count > VIEWBBC_DFS_MAX_FILES_PER_SIDE) return 0;
    out->file_count = count;

    for (size_t i = 0; i < count; ++i) {
        size_t off = 8u + i * 8u;
        if (off + 7u >= DFS_SECTOR_SIZE) return 0;

        ViewBBCDFSFile *file = &out->files[i];
        *file = (ViewBBCDFSFile){0};
        file->side = side;
        uint8_t dir = names[off + 7u];
        file->locked = (dir & 0x80u) != 0;
        file->name[0] = (char)(dir & 0x7fu);
        file->name[1] = '.';
        size_t w = 2;
        for (size_t n = 0; n < 7; ++n) {
            uint8_t ch = names[off + n] & 0x7fu;
            if (ch == 0 || ch == ' ') break;
            file->name[w++] = (char)ch;
        }
        file->name[w] = '\0';

        uint8_t packed = info[off + 6u];
        /* DFS catalogue byte 6 packs the upper two bits as:
         * bits 0-1 start sector, 2-3 load, 4-5 length, 6-7 exec.
         * Keep these shifts explicit: swapping length/exec makes perfectly
         * valid (and commonly shrunk) SSD images look wildly out of range.
         */
        file->load_address = decode_18(info[off], info[off + 1u], packed, 2u);
        file->exec_address = decode_18(info[off + 2u], info[off + 3u], packed, 6u);
        file->length = decode_18(info[off + 4u], info[off + 5u], packed, 4u);
        file->start_sector = (uint16_t)info[off + 7u] | (uint16_t)((packed & 0x03u) << 8u);

        if (file->length > VIEWBBC_DFS_IMAGE_READ_LIMIT) return 0;
        if (file->length) {
            size_t sector_span = ((size_t)file->length - 1u) / DFS_SECTOR_SIZE;
            if (sector_span > (size_t)UINT16_MAX - (size_t)file->start_sector) return 0;
            size_t ignored;
            if (!dfs_offset(image, side, file->start_sector, &ignored) ||
                !dfs_offset(image, side, (uint16_t)(file->start_sector + sector_span), &ignored)) return 0;
        }
    }
    return 1;
}

int viewbbc_dfs_open_as(ViewBBCDFSImage *image, const char *path, ViewBBCDFSType type) {
    if (!image || !path || type == VIEWBBC_DFS_TYPE_UNKNOWN) return 0;
    *image = (ViewBBCDFSImage){0};

    if (!viewbbc_file_read_all(path, &image->image_data, &image->image_size, VIEWBBC_DFS_IMAGE_READ_LIMIT)) return 0;
    image->type = type;
    image->interleaved = type == VIEWBBC_DFS_TYPE_DSD;
    image->side_count = image->interleaved ? 2 : 1;

    for (int side = 0; side < image->side_count; ++side) {
        if (!parse_side(image, side)) {
            viewbbc_dfs_close(image);
            return 0;
        }
    }
    return 1;
}

int viewbbc_dfs_open(ViewBBCDFSImage *image, const char *path) {
    return viewbbc_dfs_open_as(image, path, viewbbc_dfs_type_from_path(path));
}

void viewbbc_dfs_close(ViewBBCDFSImage *image) {
    if (!image) return;
    free(image->image_data);
    *image = (ViewBBCDFSImage){0};
}

static int dfs_name_matches(const ViewBBCDFSFile *file, const char *requested) {
    if (!file || !requested) return 0;
    const char *name = requested;
    if (name[0] == ':' && name[1] && name[2] == '.') name += 3;
    if (ascii_ieq(file->name, name)) return 1;
    if (file->name[0] == '$' && file->name[1] == '.' && ascii_ieq(file->name + 2, name)) return 1;
    return 0;
}

const ViewBBCDFSFile *viewbbc_dfs_find_file(const ViewBBCDFSImage *image, const char *name) {
    if (!image || !name || !*name) return NULL;

    int requested_side = -1;
    if (name[0] == ':' && name[1] && name[2] == '.') {
        if (name[1] == '0') requested_side = 0;
        else if (name[1] == '2') requested_side = 1;
        else return NULL;
    }

    for (int side = 0; side < image->side_count; ++side) {
        if (requested_side >= 0 && requested_side != side) continue;
        for (size_t i = 0; i < image->sides[side].file_count; ++i) {
            const ViewBBCDFSFile *file = &image->sides[side].files[i];
            if (dfs_name_matches(file, name)) return file;
        }
    }
    return NULL;
}

int viewbbc_dfs_extract_file(const ViewBBCDFSImage *image,
                             const ViewBBCDFSFile *file,
                             uint8_t **data_out,
                             size_t *length_out) {
    if (!image || !file || !data_out || !length_out) return 0;
    *data_out = NULL;
    *length_out = 0;

    uint8_t *data = malloc(file->length ? (size_t)file->length : 1u);
    if (!data) return 0;

    size_t copied = 0;
    uint16_t sector = file->start_sector;
    while (copied < file->length) {
        size_t offset;
        if (!dfs_offset(image, file->side, sector, &offset)) {
            free(data);
            return 0;
        }
        size_t remaining = (size_t)file->length - copied;
        size_t chunk = remaining < DFS_SECTOR_SIZE ? remaining : DFS_SECTOR_SIZE;
        if (offset > image->image_size || chunk > image->image_size - offset) {
            free(data);
            return 0;
        }
        memcpy(data + copied, image->image_data + offset, chunk);
        copied += chunk;
        if (sector == UINT16_MAX && copied < file->length) {
            free(data);
            return 0;
        }
        sector++;
    }

    *data_out = data;
    *length_out = copied;
    return 1;
}
