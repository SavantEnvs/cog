// Known-answer probe: loads one tracker module with libopenmpt and prints parsed facts plus a hash of the
// first two seconds of rendered audio, so mayhem/test.sh can compare them against exact expected values.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include <libopenmpt/libopenmpt.h>

static void print_meta(openmpt_module *mod, const char *key) {
  const char *v = openmpt_module_get_metadata(mod, key);
  std::printf("%s=%s\n", key, v ? v : "(null)");
  openmpt_free_string(v);
}

int main(int argc, char **argv) {
  if (argc != 2) return 2;
  FILE *f = std::fopen(argv[1], "rb");
  if (!f) return 3;
  std::vector<unsigned char> data;
  unsigned char tmp[4096];
  size_t n;
  while ((n = std::fread(tmp, 1, sizeof tmp, f)) > 0) data.insert(data.end(), tmp, tmp + n);
  std::fclose(f);

  openmpt_module *mod = openmpt_module_create_from_memory2(data.data(), data.size(), nullptr, nullptr, nullptr, nullptr,
                                                           nullptr, nullptr, nullptr);
  if (!mod) return 4;

  openmpt_module_ctl_set_integer(mod, "dither", 0);

  print_meta(mod, "type");
  print_meta(mod, "title");
  std::printf("orders=%d\n", openmpt_module_get_num_orders(mod));
  std::printf("patterns=%d\n", openmpt_module_get_num_patterns(mod));
  std::printf("channels=%d\n", openmpt_module_get_num_channels(mod));
  std::printf("instruments=%d\n", openmpt_module_get_num_instruments(mod));
  std::printf("samples=%d\n", openmpt_module_get_num_samples(mod));

  uint64_t ph = 1469598103934665603ull;
  for (int pat = 0; pat < openmpt_module_get_num_patterns(mod); pat++) {
    int rows = openmpt_module_get_pattern_num_rows(mod, pat);
    for (int r = 0; r < rows; r++) {
      for (int c = 0; c < openmpt_module_get_num_channels(mod); c++) {
        const char *cell = openmpt_module_format_pattern_row_channel(mod, pat, r, c, 0, 1);
        for (const char *q = cell; q && *q; q++) {
          ph ^= (unsigned char)*q;
          ph *= 1099511628211ull;
        }
        openmpt_free_string(cell);
      }
    }
  }
  std::printf("pattern_hash=%016llx\n", (unsigned long long)ph);
  std::printf("duration_ms=%d\n", (int)(openmpt_module_get_duration_seconds(mod) * 1000.0 + 0.5));

  uint64_t h = 1469598103934665603ull;
  int peak = 0;
  uint64_t frames = 0;
  static int16_t buf[1024 * 2];
  while (frames < 44100 * 2) {
    size_t got = openmpt_module_read_interleaved_stereo(mod, 44100, 1024, buf);
    if (got == 0) break;
    for (size_t i = 0; i < got * 2; i++) {
      int a = buf[i] < 0 ? -buf[i] : buf[i];
      if (a > peak) peak = a;
      h ^= (uint16_t)buf[i];
      h *= 1099511628211ull;
    }
    frames += got;
  }
  std::printf("frames=%llu\n", (unsigned long long)frames);
  std::printf("peak=%d\n", peak);
  std::printf("audio_hash=%016llx\n", (unsigned long long)h);
  openmpt_module_destroy(mod);
  return 0;
}
