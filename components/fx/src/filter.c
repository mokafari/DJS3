/**
 * @file filter.c
 * @brief Digital filter implementations (lowpass, highpass, bandpass, EQ)
 */

#include "filter.h"
#include <math.h>
#include <stdlib.h>
#include "esp_log.h"

// Simple biquad filter implementation
// TODO: Implement full biquad filter with proper coefficients

