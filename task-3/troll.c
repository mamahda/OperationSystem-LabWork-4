#define FUSE_USE_VERSION 30
#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <pwd.h>
#include <stdlib.h>
#include <sys/stat.h>

#define TRIGGER_FILE "/tmp/.troll_triggered"

static const char *file1 = "/very_spicy_info.txt";
static const char *file2 = "/upload.txt";

//ambil user yg akses
const char *siapa_user() {
    uid_t uid = fuse_get_context()->uid;
    struct passwd *pw = getpwuid(uid);
    if (pw) return pw->pw_name;
    return "unknown";
}

int cek_ketipu() {
    return access(TRIGGER_FILE, F_OK) == 0;
}

static int troll_getattr(const char *path, struct stat *st) {
    memset(st, 0, sizeof(struct stat));

    if (strcmp(path, "/") == 0) {
        st->st_mode = S_IFDIR | 0755;
        st->st_nlink = 2;
    } else if (strcmp(path, file1) == 0 || strcmp(path, file2) == 0) {
        st->st_mode = S_IFREG | 0644;
        st->st_nlink = 1;
        st->st_size = 1024;
    } else {
        return -ENOENT;
    }

    return 0;
}

static int troll_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi, enum fuse_readdir_flags flags) {
    filler(buf, ".", NULL, 0, 0);
    filler(buf, "..", NULL, 0, 0);
    filler(buf, file1 + 1, NULL, 0, 0);
    filler(buf, file2 + 1, NULL, 0, 0);
    return 0;
}

static int troll_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
    const char *isi = "";
    const char *user = siapa_user();

    if (strcmp(path, file1) == 0) {
        if (strcmp(user, "DainTontas") == 0 && !cek_ketipu()) {
            isi = "Very spicy internal developer information: leaked roadmap.docx\n";
        } else {
            isi = "DainTontas' personal secret!!.txt\n";
        }
    } else if (strcmp(path, file2) == 0) {
        if (strcmp(user, "DainTontas") == 0) {
            FILE *fp = fopen(TRIGGER_FILE, "w");
            if (fp) fclose(fp);
        }
        isi = "";
    } else if (strstr(path, ".txt") != NULL && cek_ketipu()) {
        isi = "Fell for it again reward\n";
    } else {
        return -ENOENT;
    }

    size_t len = strlen(isi);
    if (offset < len) {
        if (offset + size > len) size = len - offset;
        memcpy(buf, isi + offset, size);
    } else {
        size = 0;
    }

    return size;
}

static struct fuse_operations ops = {
    .getattr = troll_getattr,
    .readdir = troll_readdir,
    .read = troll_read,
};

int main(int argc, char *argv[]) {
    return fuse_main(argc, argv, &ops, NULL);
}
