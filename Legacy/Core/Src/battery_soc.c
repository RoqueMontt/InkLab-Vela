#include "battery_soc.h"
#include <math.h>

typedef struct {
    float ocv;
    float soc;
} OcvSocPoint_t;

// Standard Li-Ion 4.2V Discharge Curve
static const OcvSocPoint_t li_ion_ocv_table[] = {
    {4.20f, 100.0f}, {4.10f,  95.0f}, {4.06f,  90.0f}, {3.98f,  80.0f},
    {3.92f,  70.0f}, {3.87f,  60.0f}, {3.82f,  50.0f}, {3.79f,  40.0f},
    {3.75f,  30.0f}, {3.70f,  20.0f}, {3.60f,  10.0f}, {3.50f,   5.0f},
    {3.30f,   0.0f}
};

static float r_internal = 0.150f;
static float last_vbat = 0.0f;
static float last_ibat_A = 0.0f;
static float filtered_soc = -1.0f;

void BatterySOC_Init(void) {
    r_internal = 0.150f;
    filtered_soc = -1.0f;
    last_vbat = 0.0f;
    last_ibat_A = 0.0f;
}

static float Get_SOC_From_OCV(float ocv) {
    if (ocv >= li_ion_ocv_table[0].ocv) return 100.0f;
    if (ocv <= li_ion_ocv_table[12].ocv) return 0.0f;

    for (int i = 0; i < 12; i++) {
        if (ocv <= li_ion_ocv_table[i].ocv && ocv >= li_ion_ocv_table[i+1].ocv) {
            float v_diff = li_ion_ocv_table[i].ocv - li_ion_ocv_table[i+1].ocv;
            float s_diff = li_ion_ocv_table[i].soc - li_ion_ocv_table[i+1].soc;
            float v_frac = (ocv - li_ion_ocv_table[i+1].ocv) / v_diff;
            return li_ion_ocv_table[i+1].soc + (v_frac * s_diff);
        }
    }
    return 0.0f;
}

void BatterySOC_Update(float vbat_V, float current_mA, float* out_soc, float* out_r_int) {
    float current_A = current_mA / 1000.0f;
    float delta_i = current_A - last_ibat_A;
    float delta_v = vbat_V - last_vbat;

    // Only update R_int if the current step is large enough to get a clean delta
    if (last_vbat > 0.0f && fabs(delta_i) > 0.2f) {
        float r_calc = delta_v / delta_i;
        if (r_calc > 0.02f && r_calc < 0.5f) {
            r_internal = (0.2f * r_calc) + (0.8f * r_internal); // EMA Filter
        }
    }

    last_vbat = vbat_V;
    last_ibat_A = current_A;

    // Compensate for voltage sag under load
    float estimated_ocv = vbat_V - (current_A * r_internal);
    float inst_soc = Get_SOC_From_OCV(estimated_ocv);

    if (filtered_soc < 0.0f) {
        filtered_soc = inst_soc; // Initialize immediately on first boot
    } else {
        filtered_soc = (0.05f * inst_soc) + (0.95f * filtered_soc); // Heavy EMA for stability
    }

    *out_soc = filtered_soc;
    *out_r_int = r_internal;
}
