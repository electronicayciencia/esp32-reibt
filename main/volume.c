#include <stdint.h>

// phone's exact AVRCP volume steps (16 values)
static const uint8_t avrcp_volume_steps[16] = {
    0, 9, 17, 26, 34, 43, 51, 60,
    68, 77, 85, 94, 102, 111, 119, 127
};

// Precomputed log-mapped output (0 to 65535)
static const uint16_t log_volume_map[16] = {
    0, 300, 650, 1100, 1700, 2500, 3600, 5000,
    6800, 9100, 12000, 15700, 20400, 26500, 35000, 65535
};

uint16_t interpolate_volume(uint8_t volume)
{
    if (volume == 0) {
        return 0;
    }
    if (volume >= 127) {
        return 65535;
    }

    // Find segment
    int i = 0;
    while (i < 15 && volume > avrcp_volume_steps[i + 1]) {
        i++;
    }

    uint8_t v0 = avrcp_volume_steps[i];
    uint8_t v1 = avrcp_volume_steps[i + 1];
    uint16_t y0 = log_volume_map[i];
    uint16_t y1 = log_volume_map[i + 1];

    if (v1 == v0) {
        return y0;
    }

    // Interpolate: all math fits in uint16_t safely
    uint16_t dy = y1 - y0;
    uint8_t dv = volume - v0;
    uint8_t span = v1 - v0;

    // Compute: y0 + (dy * dv) / span
    // Use 32-bit for intermediate to avoid overflow
    uint32_t temp = (uint32_t)dy * dv;
    uint16_t increment = (uint16_t)(temp / span);

    return y0 + increment;
}

