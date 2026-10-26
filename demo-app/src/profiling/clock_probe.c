#include "profiling/clock_probe.h"
#include <arpa/inet.h>
#include <sys/socket.h>
#include <fcntl.h>
bool clock_probe_open(clock_probe *p, const char *host, uint16_t port) {
  memset(p, 0, sizeof(*p));
  p->fd = -1;
  struct sockaddr_in address = {0};
  address.sin_family = AF_INET;
  address.sin_port = htons(port);
  if (inet_pton(AF_INET, host, &address.sin_addr) != 1)
    return false;
  p->fd = socket(AF_INET, SOCK_DGRAM, 0);
  if (p->fd < 0 || fcntl(p->fd, F_SETFL, O_NONBLOCK) < 0 ||
      connect(p->fd, (struct sockaddr *)&address, sizeof(address))) {
    clock_probe_close(p);
    return false;
  }
  return true;
}
void clock_probe_close(clock_probe *p) {
  if (p->fd >= 0)
    close(p->fd);
  p->fd = -1;
}
void clock_probe_pump(clock_probe *p) {
  if (p->fd < 0)
    return;
  for (int i = 0; i < 8; i++) {
    uint8_t bytes[64];
    ssize_t n = recv(p->fd, bytes, sizeof(bytes), 0);
    uint64_t t4 = (uint64_t)app_us();
    if (n < 0)
      break;
    if (n != 28 || memcmp(bytes, "DCLK", 4))
      continue;
    uint64_t t1 = app_be(bytes + 4, 8), t2 = app_be(bytes + 12, 8), t3 = app_be(bytes + 20, 8);
    if (t1 != p->pending || t3 < t2 || t4 < t1 || t4 - t1 < t3 - t2)
      continue;
    if (p->count < 1024) {
      uint64_t sample[] = {t1, t2, t3, t4};
      memcpy(p->samples[p->count++], sample, sizeof(sample));
    }
    p->pending = 0;
  }
  uint64_t t = (uint64_t)app_us();
  if (p->pending && t - p->pending > 1000000)
    p->pending = 0;
  if (!p->pending && t - p->last >= 100000 && p->count < 1024) {
    uint8_t bytes[12];
    memcpy(bytes, "DCLK", 4);
    app_put_be(bytes + 4, t, 8);
    if (send(p->fd, bytes, 12, 0) == 12)
      p->pending = p->last = t;
  }
}
void clock_probe_json(clock_probe *p, FILE *f) {
  fputs("\"clock_samples\":[", f);
  for (size_t i = 0; i < p->count; i++) {
    uint64_t *s = p->samples[i];
    fprintf(f, "%s[%llu,%llu,%llu,%llu]", i ? "," : "", (unsigned long long)s[0],
            (unsigned long long)s[1], (unsigned long long)s[2], (unsigned long long)s[3]);
  }
  fputc(']', f);
}
