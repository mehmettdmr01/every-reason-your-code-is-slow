// Measures buffered write() throughput to /tmp with 1 B, 64 B, 4 KiB, 1 MiB calls.
// Fresh temporary files; allocation, open, close and unlink are outside timing.
// Warm-up, minimum repeats, checked file sizes, partial writes and EINTR handling.
// Expected: tiny writes lose to syscall cost; no fsync, so this is not disk speed.
// Full 1 B case writes 4 MiB and labels its 64 MiB extrapolation; --quick is smaller.
#include "bench.h"
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct { double ns; uint64_t calls; } sample;
static sample run(const unsigned char *buffer, size_t bytes, size_t chunk) {
    char path[] = "/tmp/every-slow-XXXXXX";
    int fd = mkstemp(path);
    if (fd < 0) { perror("mkstemp"); exit(1); }
    size_t offset = 0;
    sample result = {0, 0};
    int saved_error = 0;
    uint64_t start = now_ns();
    while (offset < bytes) {
        size_t count = bytes - offset < chunk ? bytes - offset : chunk;
        ssize_t written = write(fd, buffer, count);
        ++result.calls;
        if (written < 0 && errno == EINTR) continue;
        if (written <= 0) { saved_error = written < 0 ? errno : EIO; break; }
        offset += (size_t)written;
    }
    result.ns = (double)(now_ns() - start);
    struct stat st;
    if (fstat(fd, &st) != 0) saved_error = errno;
    else if ((uint64_t)st.st_size != bytes && !saved_error) saved_error = EIO;
    if (close(fd) != 0 && !saved_error) saved_error = errno;
    if (unlink(path) != 0 && !saved_error) saved_error = errno;
    if (saved_error) { errno = saved_error; perror("temporary write"); exit(1); }
    return result;
}
int main(int argc, char **argv) {
    int quick = quick_mode(argc, argv), reps = quick ? 2 : 5;
    performance_qos();
    const size_t chunks[] = {1, 64, 4096, 1048576};
    unsigned char *buffer = allocate(1048576);
    for (size_t i = 0; i < 1048576; ++i) buffer[i] = (unsigned char)(i * 31 + 17);
    puts("chunk_bytes,actual_bytes,target_bytes,scale_factor,extrapolated,reps,quick,min_ms,MB_per_s,write_syscalls,estimated_target_ms,estimated_target_write_syscalls");
    for (int v = 0; v < 4; ++v) {
        size_t bytes = quick ? (v == 0 ? 16384 : 1048576) : (v == 0 ? 4194304 : 67108864);
        size_t target = 67108864;
        double scale = (double)target / (double)bytes;
        (void)run(buffer, bytes < 65536 ? bytes : 65536, chunks[v]);
        sample best = {1e300, 0};
        for (int r = 0; r < reps; ++r) {
            sample s = run(buffer, bytes, chunks[v]);
            if (s.ns < best.ns) best = s;
        }
        printf("%zu,%zu,%zu,%.0f,%d,%d,%d,%.4f,%.4f,%" PRIu64 ",%.4f,%.0f\n",
               chunks[v], bytes, target, scale, bytes != target, reps, quick, best.ns / 1e6,
               (double)bytes * 1000.0 / best.ns, best.calls, best.ns * scale / 1e6,
               (double)best.calls * scale);
    }
    free(buffer);
}
