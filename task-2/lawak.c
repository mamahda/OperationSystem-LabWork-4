#define FUSE_USE_VERSION 28

#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>
#include <ctype.h>
#include <stddef.h>


#define MAX_FILTER_WORDS 100
#define LOG_PATH "/home/blackcurrent/Kuliah_Semester_2/SisOp_Praktikum_4/task-2/lawakfs.log"

static const char *source_dir = NULL;
static char *remove_extension(const char *name);
int is_within_access_time(); 
int is_secret_file(const char *filename);

//
// ------------------------ E -----------------------------------------------

typedef struct {
  char *filter_words[MAX_FILTER_WORDS];
  int filter_count;
  char secret_file_basename[256];
  int access_start_hour, access_start_min;
  int access_end_hour, access_end_min;
} lawak_config;

lawak_config config;

void parsing_config(const char *config_path) {
  FILE *fp = fopen(config_path, "r");
  if (!fp) {
    perror("Gagal membuka file konfigurasi");
    exit(EXIT_FAILURE);
  }

  char line[1024];
  while (fgets(line, sizeof(line), fp)) {
    line[strcspn(line, "\r\n")] = '\0';

    if (strncmp(line, "FILTER_WORDS=", 13) == 0) {
      char *words = line + 13;
      char *token = strtok(words, ",");
      config.filter_count = 0;
      while (token && config.filter_count < MAX_FILTER_WORDS) {
        config.filter_words[config.filter_count++] = strdup(token);
        token = strtok(NULL, ",");
      }

    } else if (strncmp(line, "SECRET_FILE_BASENAME=", 21) == 0) {
      strncpy(config.secret_file_basename, line + 21, sizeof(config.secret_file_basename) - 1);
      config.secret_file_basename[sizeof(config.secret_file_basename) - 1] = '\0';

    } else if (strncmp(line, "ACCESS_START=", 13) == 0) {
      sscanf(line + 13, "%d:%d", &config.access_start_hour, &config.access_start_min);

    } else if (strncmp(line, "ACCESS_END=", 11) == 0) {
      sscanf(line + 11, "%d:%d", &config.access_end_hour, &config.access_end_min);
    }
  }

  fclose(fp);
}

// ------------------------ E -----------------------------------------------
//
// ==========================================================================
//
// ------------------------ D -----------------------------------------------

void log_action(const char *action, const char *path) {
  if (strcmp(action, "ACCESS") == 0 || strcmp(action, "GETATTR") == 0 || strcmp(action, "READDIR") == 0) {
    return;
  }

  FILE *log_file = fopen(LOG_PATH, "a");
  if (!log_file) {
    char fallback_path[] = "./lawakfs.log";
    log_file = fopen(fallback_path, "a");
    if (!log_file) return;
  }

  time_t now = time(NULL);
  struct tm *t = localtime(&now);
  char time_str[20];
  strftime(time_str, sizeof(time_str), "%F %T", t);

  uid_t uid = getuid();
  fprintf(log_file, "[%s] [%d] [%s] [%s]\n", time_str, uid, action, path);
  fflush(log_file);
  fclose(log_file);
}

// ------------------------ D -----------------------------------------------
//
// ==========================================================================
//
// ------------------------ C -----------------------------------------------

int is_text_file(const char *buffer, size_t size) {
  for (size_t i = 0; i < size; ++i) {
    if (!(isprint(buffer[i]) || buffer[i] == '\n' || buffer[i] == '\r' || buffer[i] == '\t')) {
      return 0; 
    }
  }
  return 1; 
}

static const char base64_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

char *base64_encode(const unsigned char *src, size_t len, size_t *out_len) {
  size_t olen = 4 * ((len + 2) / 3);
  char *out = malloc(olen + 1);
  if (!out) return NULL;

  size_t i, j;
  for (i = 0, j = 0; i < len;) {
    uint32_t octet_a = i < len ? src[i++] : 0;
    uint32_t octet_b = i < len ? src[i++] : 0;
    uint32_t octet_c = i < len ? src[i++] : 0;

    uint32_t triple = (octet_a << 16) | (octet_b << 8) | octet_c;

    out[j++] = base64_table[(triple >> 18) & 0x3F];
    out[j++] = base64_table[(triple >> 12) & 0x3F];
    out[j++] = (i > len + 1) ? '=' : base64_table[(triple >> 6) & 0x3F];
    out[j++] = (i > len) ? '=' : base64_table[triple & 0x3F];
  }

  out[j] = '\0';
  if (out_len) *out_len = j;
  return out;
}

void filter_text(char *content, size_t max_size) {
  size_t input_pos = 0, output_pos = 0;
  size_t res = strlen(content); 
  char temp[max_size + 1];
  memset(temp, 0, sizeof(temp));

  while (input_pos < res && output_pos < max_size - 1) {
    int word_matched = 0;
    for (int i = 0; i < config.filter_count; i++) {
      size_t word_len = strlen(config.filter_words[i]);
      if (input_pos + word_len <= res &&
        strncasecmp(&content[input_pos], config.filter_words[i], word_len) == 0) {

        memcpy(&temp[output_pos], "lawak", 5);
        input_pos += word_len;
        output_pos += 5;
        word_matched = 1;
        break;
      }
    }

    if (!word_matched) {
      temp[output_pos++] = content[input_pos++];
    }
  }

  temp[output_pos] = '\0'; 

  strncpy(content, temp, max_size - 1);
  content[max_size - 1] = '\0';
}

//
// ------------------------ C ----------------------------------------------
//
// ==========================================================================
// 
// ------------------------ B ----------------------------------------------- 

int is_within_access_time() {
  time_t now = time(NULL);
  struct tm *local = localtime(&now);

  int now_minutes = local->tm_hour * 60 + local->tm_min;
  int start_minutes = config.access_start_hour * 60 + config.access_start_min;
  int end_minutes   = config.access_end_hour * 60 + config.access_end_min;

  return now_minutes >= start_minutes && now_minutes <= end_minutes;
}

int is_secret_file(const char *filename) {
  const char *base = strrchr(filename, '/');
  base = base ? base + 1 : filename;

  char *name = remove_extension(base);
  int result = strcmp(name, config.secret_file_basename) == 0;
  free(name);  // Added: Free allocated memory
  return result;
}

// ------------------------ B -----------------------------------------------
// 
// ==========================================================================
// 
// ------------------------ A ----------------------------------------------- 

static char *map_path(const char *path) {
  const char *rel_path = (path[0] == '/') ? path + 1 : path;
  if (strlen(rel_path) == 0) rel_path = ".";
  char *full_path = malloc(strlen(source_dir) + strlen(rel_path) + 2);
  sprintf(full_path, "%s/%s", source_dir, rel_path);
  return full_path;
}

static char *remove_extension(const char *name) {
  char *dot = strrchr(name, '.');
  if (!dot) return strdup(name);
  size_t len = dot - name;
  char *res = malloc(len + 1);
  strncpy(res, name, len);
  res[len] = '\0';
  return res;
}

char *hidden_extension(const char *dir_path, const char *name) {
  DIR *dir = opendir(dir_path);
  if (!dir) return NULL;

  struct dirent *entry;
  while ((entry = readdir(dir))) {
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
      continue;

    if (strcmp(entry->d_name, name) == 0) {
      char *result = strdup(entry->d_name);
      closedir(dir);
      return result;
    }

    char *no_ext = remove_extension(entry->d_name);
    if (strcmp(no_ext, name) == 0) {
      char *result = strdup(entry->d_name);
      free(no_ext);
      closedir(dir);
      return result;
    }
    free(no_ext);
  }

  closedir(dir);
  return NULL;
}

// ------------------------ A -----------------------------------------------
//
// ==========================================================================
//
// ------------------------ MAIN --------------------------------------------

static int fs_access(const char *path, int mask) {
  const char *fuse_name = strrchr(path, '/');
  fuse_name = fuse_name ? fuse_name + 1 : path;

  char *dir_path = map_path("/");
  char *real_name = hidden_extension(dir_path, fuse_name);
  if (!real_name) {
    free(dir_path);
    return -ENOENT;
  }

  if (is_secret_file(real_name) && !is_within_access_time()) {
    free(dir_path);
    free(real_name);
    return -ENOENT;
  }

  size_t len = strlen(dir_path) + strlen(real_name) + 2;
  char *full_path = malloc(len);
  snprintf(full_path, len, "%s/%s", dir_path, real_name);

  int res = access(full_path, mask);

  free(dir_path);
  free(real_name);
  free(full_path);

  if (res == 0) {
    log_action("ACCESS", path);
    return 0;
  }
  return -errno;

}

static int fs_getattr(const char *path, struct stat *stbuf) {
  if (strcmp(path, "/") == 0)
    return stat(source_dir, stbuf) == 0 ? 0 : -errno;

  const char *fuse_name = strrchr(path, '/');
  fuse_name = fuse_name ? fuse_name + 1 : path;

  char *real_name = hidden_extension(source_dir, fuse_name);
  if (!real_name) {
    return -ENOENT;
  }

  if (is_secret_file(real_name) && !is_within_access_time()) {
    free(real_name);
    return -ENOENT;
  }

  char *full_path = map_path(real_name); 

  int res = stat(full_path, stbuf);
  free(real_name);
  free(full_path);
  if (res == 0) {
    log_action("GETATTR", path);  
    return 0;
  }
  return -errno;

}

static int fs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi) {
  char *full_path = map_path(path);
  DIR *dir = opendir(full_path);
  if (!dir) {
    free(full_path);
    return -errno;
  }

  filler(buf, ".", NULL, 0);
  filler(buf, "..", NULL, 0);

  struct dirent *entry;
  while ((entry = readdir(dir)) != NULL) {
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
      continue;
    }

    if (is_secret_file(entry->d_name) && !is_within_access_time()) {
      continue;
    }

    char *name_no_ext = remove_extension(entry->d_name);
    if (name_no_ext) {
      int fill_result = filler(buf, name_no_ext, NULL, 0);
      free(name_no_ext);
      
      if (fill_result != 0) {
        break;
      }
    }
  }

  closedir(dir);
  free(full_path);
  return 0;
}

static int fs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
  const char *fuse_name = strrchr(path, '/');
  fuse_name = fuse_name ? fuse_name + 1 : path;

  if (is_secret_file(fuse_name) && !is_within_access_time())
    return -ENOENT;

  char *temp_buf = malloc(size + 1);
  if (!temp_buf) return -ENOMEM;

  int res = pread(fi->fh, temp_buf, size, offset);
  if (res == -1) {
    free(temp_buf);
    return -errno;
  }

  temp_buf[res] = '\0'; 
  int is_text = is_text_file(temp_buf, res);

  if (is_text) {
    filter_text(temp_buf, size + 1);

    strncpy(buf, temp_buf, size);
    buf[size - 1] = '\0'; 
    res = strlen(buf);    
    free(temp_buf);
    log_action("READ", path);
    return res;
  } else {
    size_t encoded_len;
    char *encoded = base64_encode((unsigned char *)temp_buf, res, &encoded_len);
    free(temp_buf);
    if (!encoded) return -ENOMEM;

    if (offset >= encoded_len) {
      free(encoded);
      return 0;
    }

    size_t to_copy = encoded_len - offset;
    if (to_copy > size) to_copy = size;

    memcpy(buf, encoded + offset, to_copy);
    free(encoded);
    log_action("READ", path);
    return to_copy;
  }
}

static int fs_open(const char *path, struct fuse_file_info *fi) {
  const char *fuse_name = strrchr(path, '/');
  fuse_name = fuse_name ? fuse_name + 1 : path;


  char *real_name = hidden_extension(source_dir, fuse_name);
  if (!real_name) {
    return -ENOENT;
  }

  if (is_secret_file(real_name) && !is_within_access_time()) {
    free(real_name);
    return -ENOENT;
  }


  char *full_path = map_path(real_name);

  int fd = open(full_path, fi->flags);
  free(real_name);
  free(full_path);

  if (fd == -1) return -errno;

  fi->fh = fd;
  log_action("OPEN", path);
  return 0;
}

static int fs_release(const char *path, struct fuse_file_info *fi) {
  return close(fi->fh);
}

static int fs_write()    { return -EROFS; }
static int fs_truncate() { return -EROFS; }
static int fs_create()   { return -EROFS; }
static int fs_unlink()   { return -EROFS; }
static int fs_mkdir()    { return -EROFS; }
static int fs_rmdir()    { return -EROFS; }
static int fs_rename()   { return -EROFS; }

static struct fuse_operations hideext_oper = {
  .getattr = fs_getattr,
  .readdir = fs_readdir,
  .open    = fs_open,
  .read    = fs_read,
  .release = fs_release,
  .write   = fs_write,
  .truncate = fs_truncate,
  .create  = fs_create,
  .unlink  = fs_unlink,
  .mkdir   = fs_mkdir,
  .rmdir   = fs_rmdir,
  .rename  = fs_rename,
  .access  = fs_access,
};

int main(int argc, char *argv[]) {
  if (argc < 3) {
    fprintf(stderr, "Usage: %s <source_dir> <mountpoint>\n", argv[0]);
    return 1;
  }

  parsing_config("./lawak.conf");
  source_dir = realpath(argv[1], NULL);
  if (!source_dir) {
    perror("Invalid source directory");
    return 1;
  }

  char *fuse_argv[argc - 1];
  fuse_argv[0] = argv[0];
  for (int i = 2; i < argc; i++)
    fuse_argv[i - 1] = argv[i];

  return fuse_main(argc - 1, fuse_argv, &hideext_oper, NULL);
}
