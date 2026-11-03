// libFuzzer harness for libopenmpt: module loading + header probing + a short render, derived from
// upstream's contrib/fuzzing/fuzz.cpp (an AFL persistent-mode driver) with the same API contract checks.
#include <cstddef>
#include <cstdint>
#include <cstdlib>

#include <libopenmpt/libopenmpt.h>

#include "common/mptRandom.h"

#define BUFFERSIZE 450  // shouldn't match OpenMPT's internal mix buffer size (512)
#define SAMPLERATE 22050

static int16_t buffer[BUFFERSIZE];

static int ErrFunc(int error, void *) {
  switch (error) {
    case OPENMPT_ERROR_INVALID_ARGUMENT:
    case OPENMPT_ERROR_OUT_OF_RANGE:
    case OPENMPT_ERROR_LENGTH:
    case OPENMPT_ERROR_DOMAIN:
    case OPENMPT_ERROR_LOGIC:
    case OPENMPT_ERROR_UNDERFLOW:
    case OPENMPT_ERROR_OVERFLOW:
    case OPENMPT_ERROR_RANGE:
    case OPENMPT_ERROR_RUNTIME:
    case OPENMPT_ERROR_EXCEPTION:
      std::abort();
    default:
      return OPENMPT_ERROR_FUNC_RESULT_NONE;
  }
}

extern "C" int LLVMFuzzerInitialize(int *, char ***) {
  // one throwaway load so lazily-initialised global tables are built before fuzzing starts
  openmpt_module *warm = openmpt_module_create_from_memory2(buffer, BUFFERSIZE, nullptr, nullptr, nullptr, nullptr,
                                                            nullptr, nullptr, nullptr);
  if (warm) openmpt_module_destroy(warm);
  return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  OpenMPT::mpt::reinit_global_random();
  openmpt_module *mod = openmpt_module_create_from_memory2(data, size, nullptr, nullptr, ErrFunc, nullptr, nullptr,
                                                           nullptr, nullptr);
  if (!mod) return -1;

  // API contract: a file that loads must also pass header probing
  if (openmpt_probe_file_header(OPENMPT_PROBE_FILE_HEADER_FLAGS_DEFAULT, data, size, size, nullptr, nullptr, ErrFunc,
                                nullptr, nullptr, nullptr) == OPENMPT_PROBE_FILE_HEADER_RESULT_FAILURE)
    std::abort();

  openmpt_module_ctl_set(mod, "render.resampler.emulate_amiga", (openmpt_module_get_num_orders(mod) & 1) ? "0" : "1");
  // render about a second of the module to exercise the mix routines
  for (int i = 0; i < 50; i++) {
    if (openmpt_module_read_mono(mod, SAMPLERATE, BUFFERSIZE, buffer) == 0) break;
  }
  openmpt_module_set_position_seconds(mod, 1.0);
  openmpt_module_read_mono(mod, SAMPLERATE, BUFFERSIZE, buffer);
  openmpt_module_set_position_order_row(mod, 3, 16);
  openmpt_module_read_mono(mod, SAMPLERATE, BUFFERSIZE, buffer);

  openmpt_free_string(openmpt_module_get_metadata(mod, "date"));
  openmpt_free_string(openmpt_module_get_metadata(mod, "message"));
  openmpt_module_destroy(mod);
  return 0;
}
