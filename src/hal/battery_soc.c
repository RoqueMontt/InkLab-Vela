/**
 * @file battery_soc.c
 * @brief State-of-Charge estimator implementation.
 *
 * Algorithm:
 *   1. Tracks Δv/Δi steps to compute a running R_internal (EMA filtered).
 *   2. Removes IR-drop from the terminal voltage → Open-Circuit Voltage.
 *   3. Maps OCV to SOC via a look-up table (linear interpolation).
 *   4. Applies a heavy EMA to suppress measurement noise on the output.
 *
 * Pure C — no HAL, no Zephyr primitives required.
 */

#include "battery_soc.h"
#include "system_config.h"

#include <math.h>

/* --------------------------------------------------------------------------
 * OCV → SOC Look-up Table  (Standard Li-Ion 4.2 V chemistry)
 * -------------------------------------------------------------------------- */
typedef struct {
    float ocv;
    float soc;
} OcvSocPoint_t;

static const OcvSocPoint_t li_ion_ocv_table[] = {
    {4.20f, 100.0f}, {4.10f,  95.0f}, {4.06f,  90.0f}, {3.98f,  80.0f},
    {3.92f,  70.0f}, {3.87f,  60.0f}, {3.82f,  50.0f}, {3.79f,  40.0f},
    {3.75f,  30.0f}, {3.70f,  20.0f}, {3.60f,  10.0f}, {3.50f,   5.0f},
    {3.30f,   0.0f},
};

#define OCV_TABLE_LEN  ((int)(sizeof(li_ion_ocv_table) / sizeof(li_ion_ocv_table[0])))

/* --------------------------------------------------------------------------
 * Module state
 * -------------------------------------------------------------------------- */
static float r_internal;      /* Estimated internal resistance (Ω) */
static float last_vbat;
static float last_ibat_A;
static float filtered_soc;    /* -1 means "not yet initialised" */

/* --------------------------------------------------------------------------
 * Private helpers
 * -------------------------------------------------------------------------- */
 
 /** @brief Interpolates the Open-Circuit Voltage (OCV) to a State-of-Charge (SOC) percentage. */
static float ocv_to_soc(float ocv)
{
    if (ocv >= li_ion_ocv_table[0].ocv) {
        return 100.0f;
    }
    if (ocv <= li_ion_ocv_table[OCV_TABLE_LEN - 1].ocv) {
        return 0.0f;
    }

    for (int i = 0; i < OCV_TABLE_LEN - 1; i++) {
        if (ocv <= li_ion_ocv_table[i].ocv &&
            ocv >= li_ion_ocv_table[i + 1].ocv) {
            float v_diff = li_ion_ocv_table[i].ocv - li_ion_ocv_table[i + 1].ocv;
            float s_diff = li_ion_ocv_table[i].soc - li_ion_ocv_table[i + 1].soc;
            float v_frac = (ocv - li_ion_ocv_table[i + 1].ocv) / v_diff;
            return li_ion_ocv_table[i + 1].soc + (v_frac * s_diff);
        }
    }
    return 0.0f;
}

/* --------------------------------------------------------------------------
 * Public API
 * -------------------------------------------------------------------------- */
void BatterySOC_Init(void)
{
    r_internal   = BATT_DEFAULT_R_INT_OHMS; /* Reasonable starting estimate for Li-Ion */
    filtered_soc = -1.0f;  /* Forces hard-set on first Update() call  */
    last_vbat    = 0.0f;
    last_ibat_A  = 0.0f;
}

void BatterySOC_Update(float vbat_V, float current_mA,
                       float *out_soc, float *out_r_int)
{
    float current_A = current_mA / 1000.0f;
    float delta_i   = current_A - last_ibat_A;
    float delta_v   = vbat_V    - last_vbat;

    /* Update R_int only when the current step is large enough for a clean ΔI */
    if (last_vbat > 0.0f && fabsf(delta_i) > BATT_SOC_MIN_DELTA_I) {
        float r_calc = delta_v / delta_i;
        /* Sanity-clamp: reject noise and shorts */
        if (r_calc > BATT_SOC_MIN_R_INT && r_calc < BATT_SOC_MAX_R_INT) {
            r_internal = (BATT_SOC_R_EMA_W * r_calc) + ((1.0f - BATT_SOC_R_EMA_W) * r_internal); /* EMA */
        }
    }

    last_vbat   = vbat_V;
    last_ibat_A = current_A;

    /* Compensate terminal voltage for IR-drop to recover OCV */
    float estimated_ocv = vbat_V - (current_A * r_internal);
    float inst_soc      = ocv_to_soc(estimated_ocv);

    if (filtered_soc < 0.0f) {
        filtered_soc = inst_soc; /* Hard-set on first call */
    } else {
        filtered_soc = (BATT_SOC_SOC_EMA_W * inst_soc) + ((1.0f - BATT_SOC_SOC_EMA_W) * filtered_soc); /* Heavy EMA */
    }

    *out_soc   = filtered_soc;
    *out_r_int = r_internal;
}