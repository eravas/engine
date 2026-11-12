#include "tests/support/determinism.h"

#include <string.h>

uint64_t det_hash(const void *data, size_t size, uint64_t seed)
{
    const uint8_t *p = data;
    uint64_t h = seed;
    for (size_t i = 0; i < size; i++) {
        h ^= p[i];
        h *= 0x100000001b3ull;
    }
    return h;
}

uint64_t det_run(const det_subject_t *subject, const det_script_t *script,
                 uint64_t ticks)
{
    headless_reset();
    headless_set_input_script(script->runs, script->run_count);

    memset(subject->state, 0, subject->state_size);
    if (subject->init != NULL) {
        subject->init(subject->state);
    }
    for (uint64_t t = 0; t < ticks; t++) {
        platform_input_t in;
        platform_poll_input(&in);
        subject->tick(subject->state, t, &in);
    }
    return det_hash(subject->state, subject->state_size, DET_HASH_SEED);
}

bool det_check(const det_subject_t *subject, const det_script_t *script,
               uint64_t ticks, uint64_t *hash_out)
{
    uint64_t first = det_run(subject, script, ticks);
    uint64_t second = det_run(subject, script, ticks);
    if (hash_out != NULL) {
        *hash_out = first;
    }
    return first == second;
}
