#define FUSE_USE_VERSION 26
#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <pwd.h>

static const char *file1 = "/very_spicy_info.txt";
static const char *file2 = "/upload.txt";

static int troll_getattr(const char *path, struct stat *stbuf)
{
    memset(stbuf, 0, sizeof(struct stat));
    if (strcmp(path, "/") == 0) {
        stbuf->st_mode = S_IFDIR | 0755;
        stbuf->st_nlink = 2;
    } else if (strcmp(path, file1) == 0 || strcmp(path, file2) == 0) {
        stbuf->st_mode = S_IFREG | 0644;
        stbuf->st_nlink = 1;
        stbuf->st_size = 512;
    } else {
        return -ENOENT;
    }
    return 0;
}

static int troll_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                         off_t offset, struct fuse_file_info *fi)
{
    if (strcmp(path, "/") != 0)
        return -ENOENT;

    filler(buf, ".", NULL, 0);
    filler(buf, "..", NULL, 0);
    filler(buf, file1 + 1, NULL, 0);
    filler(buf, file2 + 1, NULL, 0);
    return 0;
}

static int troll_open(const char *path, struct fuse_file_info *fi)
{
    struct fuse_context *context = fuse_get_context();
    const char *username = getpwuid(context->uid)->pw_name;

    if (strcmp(path, file2) == 0 && strcmp(username, "DainTontas") == 0) {
        printf("⚠️  ALERT: DainTontas triggered the TRAP!\n");
    }

    if (strcmp(path, file1) != 0 && strcmp(path, file2) != 0)
        return -ENOENT;

    // Perbolehkan semua mode buka file
    if ((fi->flags & O_ACCMODE) != O_RDONLY &&
        (fi->flags & O_ACCMODE) != O_WRONLY &&
        (fi->flags & O_ACCMODE) != O_RDWR)
        return -EACCES;

    return 0;
}


static int troll_read(const char *path, char *buf, size_t size, off_t offset,
                      struct fuse_file_info *fi)
{
    const char *data;
    if (strcmp(path, file1) == 0)
        data = "🔥 This is top secret spicy info!\n";
    else if (strcmp(path, file2) == 0)
        data = "📥 Ready to upload? Watch out...\n";
    else
        return -ENOENT;

    size_t len = strlen(data);
    if (offset < len) {
        if (offset + size > len)
            size = len - offset;
        memcpy(buf, data + offset, size);
    } else size = 0;

    return size;
}

static struct fuse_operations troll_oper = {
    .getattr = troll_getattr,
    .readdir = troll_readdir,
    .open    = troll_open,
    .read    = troll_read,
};

int main(int argc, char *argv[])
{
    return fuse_main(argc, argv, &troll_oper, NULL);
}
