// Host-side tool: prints the INFO JSON the firmware sends (same settings
// table), used to check its size and to feed the companion app's simulator.
#include <stdio.h>
#include "settings.h"

void wavegen_configure(wave_t w, int32_t a, uint32_t f) { (void)w; (void)a; (void)f; }

int main(void) {
    printf("{\"proto\":1,\"fw\":\"1.0\",\"name\":\"Scopocket\",\"board\":\"v1.3\",\"points\":%d,"
           "\"pretrigger\":%d,\"deep_max\":%d,\"deep_lsb_uv\":%d,\"log_max_rate\":%d,\"settings\":[",
           ACQ_POINTS, ACQ_PRETRIGGER, ACQ_DEEP_MAX, ACQ_DEEP_LSB_UV, ACQ_STREAM_MAX_RATE);
    for (int i = 0; i < setting_count; i++) {
        const setting_item_t *it = &setting_items[i];
        printf("%s{\"id\":%d,\"key\":\"%s\",\"label\":\"%s\",\"options\":[", i ? "," : "", i, it->key, it->label);
        char buf[24];
        for (int k = 0; k < it->count; k++) {
            it->format(k, buf, sizeof buf);
            printf("%s\"%s\"", k ? "," : "", buf);
        }
        printf("]");
        if (it->numeric) {
            printf(",\"unit\":\"%s\",\"values\":[", it->unit);
            for (int k = 0; k < it->count; k++) printf("%s%ld", k ? "," : "", (long)it->numeric(k));
            printf("]");
        }
        printf("}");
    }
    printf("]}\n");
    return 0;
}
