/**
 * @file telemetry_view.h
 * @brief Telemetry view interface
 */

#ifndef TELEMETRY_VIEW_H
#define TELEMETRY_VIEW_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void telemetry_view_init(uint32_t width, uint32_t height);
void telemetry_view_update(float bpm, float pitch, float phase_error);

#ifdef __cplusplus
}
#endif

#endif // TELEMETRY_VIEW_H

