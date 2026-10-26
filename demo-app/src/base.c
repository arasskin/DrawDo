#include "base.h"
bool app_append(app_buffer *b, const void *p, size_t n) { return ct_bytes_append(b, p, n); }
bool app_string(app_buffer *b, const char *p, size_t n) {
  b->size = 0;
  return app_append(b, p, n);
}
bool app_format(app_buffer *b, const char *format, ...) {
  va_list a, c;
  va_start(a, format);
  va_copy(c, a);
  int n = vsnprintf(NULL, 0, format, c);
  va_end(c);
  if (n < 0 || !app_reserve(&b->data, &b->capacity, (size_t)n + 1, 1)) {
    va_end(a);
    return false;
  }
  vsnprintf((char *)b->data, b->capacity, format, a);
  va_end(a);
  b->size = (size_t)n;
  return true;
}
void app_buffer_free(app_buffer *b) { ct_bytes_free(b); }
bool app_mkdir(const char *path) {
  char *p = strdup(path);
  if (!p)
    return false;
  bool ok = true;
  for (char *c = p + 1; *c; c++)
    if (*c == '/') {
      *c = 0;
      if (mkdir(p, 0700) && errno != EEXIST)
        ok = false;
      *c = '/';
    }
  if (mkdir(p, 0700) && errno != EEXIST)
    ok = false;
  free(p);
  return ok;
}
bool app_remove_tree(const char *path) {
  struct stat st;
  if (lstat(path, &st))
    return errno == ENOENT;
  if (!S_ISDIR(st.st_mode))
    return unlink(path) == 0;
  DIR *d = opendir(path);
  if (!d)
    return false;
  bool ok = true;
  struct dirent *e;
  while ((e = readdir(d)))
    if (strcmp(e->d_name, ".") && strcmp(e->d_name, "..")) {
      app_buffer b = {0};
      if (!app_format(&b, "%s/%s", path, e->d_name) || !app_remove_tree(app_cstr(&b)))
        ok = false;
      app_buffer_free(&b);
    }
  closedir(d);
  return rmdir(path) == 0 && ok;
}
bool app_write(const char *path, const void *p, size_t n) {
  FILE *f = fopen(path, "wb");
  if (!f)
    return false;
  bool ok = fwrite(p, 1, n, f) == n;
  return fclose(f) == 0 && ok;
}
bool app_json(FILE *f, const char *p, size_t n) {
  fputc('"', f);
  for (size_t i = 0; i < n; i++) {
    unsigned char c = (unsigned char)p[i];
    if (c == '"' || c == '\\') {
      fputc('\\', f);
      fputc(c, f);
    } else if (c < 32)
      fprintf(f, "\\u%04x", c);
    else
      fputc(c, f);
  }
  fputc('"', f);
  return !ferror(f);
}
bool app_u8(app_buffer *b, uint8_t v) { return app_append(b, &v, 1); }
bool app_u32(app_buffer *b, uint32_t v) {
  uint8_t p[4];
  app_put_be(p, v, 4);
  return app_append(b, p, 4);
}
bool app_u64(app_buffer *b, uint64_t v) {
  uint8_t p[8];
  app_put_be(p, v, 8);
  return app_append(b, p, 8);
}
bool app_blob(app_buffer *b, const void *p, size_t n) {
  return n <= UINT32_MAX && app_u32(b, (uint32_t)n) && app_append(b, p, n);
}
const uint8_t *app_take(app_reader *r, size_t n) {
  if (r->failed || n > r->size) {
    r->failed = true;
    return NULL;
  }
  const uint8_t *p = r->data;
  r->data += n;
  r->size -= n;
  return p;
}
uint64_t app_read(app_reader *r, unsigned n) {
  const uint8_t *p = app_take(r, n);
  return p ? app_be(p, n) : 0;
}
const uint8_t *app_read_blob(app_reader *r, size_t *n) {
  *n = (size_t)app_read(r, 4);
  return app_take(r, *n);
}
