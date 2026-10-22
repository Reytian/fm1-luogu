/* SPDX-License-Identifier: MPL-2.0 */
/* The instruments' numbers (perc.h pc_inst_t). 板鼓, 大锣, 铙钹 and 小锣: modes measured by tools/perc_fit.py fit in
 * the Beijing Opera Percussion Instrument dataset (Ajay Srinivasamurthy, Freesound pack 14056, CC BY 3.0); the
 * contact, brightness, pans and sends set by ear and by tools/perc_fit.py compare. 檀板 and 堂鼓: set by hand. */
#pragma once
static const pc_mode_t GU_M[] = {   /* 板鼓 (单皮鼓) */
    {1475.6f, 0.146f, 0.292f, 0.0000f, 0.000f, 1.000f},
    {1905.7f, 1.000f, 0.322f, 0.0000f, 0.000f, 1.000f},
    {2039.8f, 0.468f, 0.274f, 0.0000f, 0.000f, 1.000f},
    {2109.8f, 0.375f, 0.242f, 0.0000f, 0.000f, 1.000f},
    {2303.3f, 0.157f, 0.259f, 0.0000f, 0.000f, 1.000f},
    {3151.4f, 0.694f, 0.311f, 0.0000f, 0.000f, 1.000f},
    {4514.6f, 0.218f, 0.345f, 0.0000f, 0.000f, 1.000f},
    {5593.3f, 0.153f, 0.314f, 0.0000f, 0.000f, 1.000f},
};
static const pc_mode_t BAN_M[] = {   /* 檀板 (板): the clapper */
    {541.1f, 0.059f, 0.216f, 0.0000f, 0.000f, 1.000f},
    {1155.4f, 0.082f, 0.201f, 0.0000f, 0.000f, 1.000f},
    {1454.7f, 0.176f, 0.300f, 0.0000f, 0.000f, 1.000f},
    {1636.0f, 0.108f, 0.299f, 0.0000f, 0.000f, 1.000f},
    {1765.9f, 0.067f, 0.287f, 0.0000f, 0.000f, 1.000f},
    {2728.1f, 1.000f, 0.309f, 0.0000f, 0.000f, 1.000f},
};
static const pc_mode_t DALUO_M[] = {   /* 大锣: a 虎音锣, its pitch sinking as it rings */
    {184.6f, 0.445f, 8.000f, 0.5800f, 0.000f, 1.000f},
    {373.5f, 0.213f, 0.934f, 0.1559f, 0.000f, 1.000f},
    {507.7f, 0.532f, 7.583f, 0.3743f, 0.000f, 1.000f},
    {555.8f, 0.585f, 6.031f, 0.3743f, 0.000f, 1.000f},
    {579.1f, 0.509f, 8.000f, 0.3115f, 0.000f, 1.000f},
    {617.4f, 0.534f, 7.636f, 0.1422f, 0.000f, 1.000f},
    {699.4f, 0.679f, 7.995f, 0.1559f, 0.000f, 1.000f},
    {723.5f, 0.707f, 7.089f, 0.1559f, 0.000f, 1.000f},
    {758.9f, 0.756f, 5.774f, 0.1559f, 0.000f, 1.000f},
    {819.4f, 1.000f, 1.239f, 0.1559f, 0.000f, 1.000f},
    {992.3f, 0.119f, 6.908f, 0.1559f, 0.000f, 1.000f},
    {1302.2f, 0.125f, 8.000f, 0.1332f, 0.000f, 1.000f},
    {1318.3f, 0.123f, 8.000f, 0.0946f, 0.000f, 1.000f},
    {1345.2f, 0.119f, 8.000f, 0.0724f, 0.000f, 1.000f},
    {1362.4f, 0.100f, 8.000f, 0.1559f, 0.000f, 1.000f},
    {1449.9f, 0.156f, 8.000f, 0.1559f, 0.000f, 1.000f},
    {1539.8f, 0.114f, 8.000f, 0.1457f, 0.000f, 1.000f},
    {1582.5f, 0.073f, 4.540f, 0.1559f, 0.000f, 1.000f},
    {1730.1f, 0.048f, 8.000f, 0.1559f, 0.000f, 1.000f},
    {1879.9f, 0.210f, 8.000f, 0.1436f, 0.000f, 1.000f},
    {1920.8f, 0.231f, 6.013f, 0.0698f, 0.000f, 1.000f},
    {1940.6f, 0.236f, 5.527f, 0.0414f, 0.000f, 1.000f},
    {2180.7f, 0.067f, 6.752f, 0.1559f, 0.000f, 1.000f},
    {2533.6f, 0.124f, 6.107f, 0.0460f, 0.000f, 1.000f},
};
static const pc_mode_t NAOBO_M[] = {   /* 铙钹: its strongest partials, over the wash */
    {468.0f, 0.092f, 1.253f, 0.0000f, 0.000f, 1.000f},
    {483.5f, 0.040f, 6.504f, 0.0000f, 0.000f, 1.000f},
    {600.2f, 0.140f, 4.642f, 0.0000f, 0.000f, 1.000f},
    {707.2f, 0.417f, 2.625f, 0.0000f, 0.000f, 1.000f},
    {950.2f, 0.174f, 3.030f, 0.0000f, 0.000f, 1.000f},
    {1668.9f, 0.423f, 4.348f, 0.0000f, 0.000f, 1.000f},
    {2582.7f, 1.000f, 1.069f, 0.0000f, 0.000f, 1.000f},
    {3146.7f, 0.461f, 5.882f, 0.0000f, 0.000f, 1.000f},
    {3236.7f, 0.700f, 7.134f, 0.0000f, 0.000f, 1.000f},
    {3312.3f, 0.651f, 5.034f, 0.0000f, 0.000f, 1.000f},
    {3511.3f, 0.639f, 7.327f, 0.0000f, 0.000f, 1.000f},
    {3554.5f, 0.566f, 8.000f, 0.0000f, 0.000f, 1.000f},
    {3650.2f, 0.750f, 4.772f, 0.0000f, 0.000f, 1.000f},
    {3989.1f, 0.336f, 8.000f, 0.0000f, 0.000f, 1.000f},
};
static const pc_mode_t XIAOLUO_M[] = {   /* 小锣: its pitch rises as it rings */
    {657.5f, 0.542f, 2.140f, -0.1237f, 0.035f, 0.020f},
    {1300.1f, 0.348f, 0.966f, -0.0618f, 0.000f, 1.000f},
    {1539.2f, 1.000f, 0.573f, -0.0618f, 0.000f, 1.000f},
    {1606.5f, 0.724f, 0.758f, -0.0618f, 0.000f, 1.000f},
    {1848.0f, 0.750f, 0.968f, -0.0391f, 0.000f, 1.000f},
    {1877.1f, 0.827f, 0.714f, -0.0618f, 0.000f, 1.000f},
    {1909.8f, 0.718f, 0.973f, -0.0618f, 0.000f, 1.000f},
    {2003.4f, 0.636f, 1.376f, -0.0464f, 0.000f, 1.000f},
    {2343.4f, 0.330f, 1.488f, -0.0618f, 0.000f, 1.000f},
    {2370.3f, 0.393f, 1.490f, -0.0279f, 0.000f, 1.000f},
    {2662.2f, 0.379f, 1.195f, -0.0618f, 0.000f, 1.000f},
    {2793.3f, 0.401f, 1.813f, -0.0406f, 0.000f, 1.000f},
    {3002.3f, 0.639f, 1.488f, -0.0618f, 0.000f, 1.000f},
    {3035.3f, 0.672f, 1.293f, -0.0618f, 0.000f, 1.000f},
    {3077.9f, 0.663f, 1.202f, -0.0618f, 0.000f, 1.000f},
    {3131.1f, 0.817f, 1.014f, -0.0193f, 0.000f, 1.000f},
    {3361.6f, 0.571f, 1.016f, -0.0618f, 0.000f, 1.000f},
};
#define DALUO_TAU 0.150f   /* daluo's glide: its fast part (s) */
#define DALUO_SLOW_T 1.100f   /* ... its slow part (s) */
#define DALUO_SLOW 0.700f   /* ... the slow part's share */
#define XIAOLUO_TAU 0.104f   /* xiaoluo's glide: its fast part (s) */
#define XIAOLUO_SLOW_T 1.000f   /* ... its slow part (s) */
#define XIAOLUO_SLOW 0.000f   /* ... the slow part's share */
/* bangu: the recordings' shortest strokes ring 0.24 s (t60) */
/* daluo: the recordings' shortest strokes ring 4.71 s (t60) */
/* naobo: the recordings' shortest strokes ring 1.73 s (t60) */
/* xiaoluo: the recordings' shortest strokes ring 1.08 s (t60) */
/* ban: the recordings' shortest strokes ring 0.24 s (t60) */

static const pc_band_t GU_B[] = {   /* bangu: the noise its modes leave out */
    {708.0f, 1.4f, 0.0000f, 0.21f},
    {1416.0f, 1.4f, 0.1541f, 0.36f},
    {2832.0f, 1.4f, 0.3385f, 0.27f},
    {5664.0f, 1.4f, 1.8263f, 0.29f},
    {11328.0f, 1.4f, 1.0820f, 0.26f},
};
static const pc_band_t BAN_B[] = {   /* ban: the noise its modes leave out */
    {1416.0f, 1.4f, 0.0222f, 0.23f},
    {2832.0f, 1.4f, 1.4100f, 0.19f},
    {5664.0f, 1.4f, 0.9471f, 0.32f},
};
static const pc_band_t DALUO_B[] = {   /* daluo: the noise its modes leave out */
    {354.0f, 1.4f, 0.0482f, 6.00f},
    {1416.0f, 1.4f, 0.7381f, 6.00f},
    {2832.0f, 1.4f, 6.0000f, 3.17f},
    {5664.0f, 1.4f, 6.0000f, 2.85f},
    {11328.0f, 1.4f, 0.1337f, 1.73f},
};
static const pc_band_t NAOBO_B[] = {   /* naobo: the noise its modes leave out */
    {708.0f, 1.4f, 0.0263f, 4.14f},
    {1416.0f, 1.4f, 3.7728f, 3.78f},
    {2832.0f, 1.4f, 6.0000f, 4.88f},
    {5664.0f, 1.4f, 6.0000f, 4.30f},
    {11328.0f, 1.4f, 0.0676f, 3.81f},
};
static const pc_band_t XIAOLUO_B[] = {   /* xiaoluo: the noise its modes leave out */
    {708.0f, 1.4f, 0.0000f, 1.64f},
    {1416.0f, 1.4f, 0.5673f, 2.10f},
    {2832.0f, 1.4f, 0.2919f, 1.66f},
    {5664.0f, 1.4f, 6.0000f, 1.09f},
    {11328.0f, 1.4f, 5.6587f, 0.86f},
};
#define GU_RING 1.000f
#define GU_CLICK 2.000f, 0.0080f, 14000.0f, 150.0f
#define DALUO_RING 1.342f
#define DALUO_CLICK 0.063f, 0.0160f, 14000.0f, 2500.0f
#define NAOBO_RING 1.307f
#define NAOBO_CLICK 2.000f, 0.0160f, 14000.0f, 150.0f
#define XIAOLUO_RING 1.297f
#define XIAOLUO_CLICK 0.020f, 0.0320f, 2000.0f, 150.0f
#define BAN_RING 1.190f
#define BAN_CLICK 0.036f, 0.0160f, 14000.0f, 2500.0f
/* --- set by hand (tools/perc_fit.py fit keeps this part) ------------------------------------------------------- */
#define BAN_MUTE 0.02f
/* the hand on it (闷): the ring cut to this t60 (s). The recordings' shortest strokes are only half damped */
#define BANGU_MUTE 0.10f
#define DALUO_MUTE 0.25f
#define NAOBO_MUTE 0.15f
#define XIAOLUO_MUTE 0.18f
#ifndef GU_RING                             /* (tools/perc_fit.py fit writes them above, matched to the recordings:
                                             * the ring, and the contact: level, decay, lowpass, highpass) */
#define GU_RING 1.0f
#define DALUO_RING 1.0f
#define NAOBO_RING 1.0f
#define XIAOLUO_RING 1.0f
#define BAN_RING 1.0f
#define BAN_CLICK 1.00f, 0.002f, 8000.0f, 500.0f
#define GU_CLICK 0.80f, 0.003f, 6000.0f, 300.0f
#define DALUO_CLICK 0.30f, 0.004f, 3000.0f, 200.0f
#define NAOBO_CLICK 1.00f, 0.003f, 9000.0f, 500.0f
#define XIAOLUO_CLICK 0.40f, 0.002f, 5000.0f, 400.0f
#endif
static const pc_mode_t TANGGU_M[] = {       /* 堂鼓: a deeper barrel drum */
    {110.0f, 1.00f, 0.45f, 0.15f, 0.0f, 1.0f}, {175.0f, 0.50f, 0.30f, 0.10f, 0.0f, 1.0f},
    {240.0f, 0.30f, 0.20f, 0.05f, 0.0f, 1.0f}, {330.0f, 0.20f, 0.15f, 0.0f, 0.0f, 1.0f}};

#define N_(a) (uint8_t)(sizeof(a) / sizeof((a)[0]))
static const pc_inst_t PC_INST[I_NPERC] = {
    /* modes, bands, click level/decay/lowpass/highpass, bright, mute t60, glide tau, light, roll/s, pan, send,
     * highpass, ring (GU_RING ...: tools/perc_fit.py fit), the noise's lowpass, its floor's life, the tail's lowpass.
     * The glide: fast tau, slow tau, slow share */
    [I_GU] = {GU_M, N_(GU_M), GU_B, N_(GU_B), GU_CLICK, 1.0f, BANGU_MUTE, 0.05f, 1.0f, 0.0f, 0.45f, 22.0f, 0.0f, 0.20f, 300.0f, GU_RING, 8000.0f, 0.04f, 8000.0f},
    [I_BAN] = {BAN_M, N_(BAN_M), BAN_B, N_(BAN_B), BAN_CLICK, 0.8f, BAN_MUTE, 0.05f, 1.0f, 0.0f, 0.50f, 18.0f, 0.10f, 0.20f, 400.0f, BAN_RING, 8000.0f, 0.04f, 6000.0f},
    [I_DALUO] = {DALUO_M, N_(DALUO_M), DALUO_B, N_(DALUO_B), DALUO_CLICK, 1.2f, DALUO_MUTE, DALUO_TAU, DALUO_SLOW_T, DALUO_SLOW, 0.40f, 16.0f, -0.35f,
                 0.35f, 80.0f, DALUO_RING, 3500.0f, 0.04f, 4000.0f},
    [I_NAOBO] = {NAOBO_M, N_(NAOBO_M), NAOBO_B, N_(NAOBO_B), NAOBO_CLICK, 1.0f, NAOBO_MUTE, 0.50f, 1.0f, 0.0f, 0.45f,
                 16.0f, 0.35f, 0.30f, 450.0f, NAOBO_RING, 10000.0f, 0.04f, 4000.0f},
    [I_XIAOLUO] = {XIAOLUO_M, N_(XIAOLUO_M), XIAOLUO_B, N_(XIAOLUO_B), XIAOLUO_CLICK, 1.0f, XIAOLUO_MUTE, XIAOLUO_TAU, XIAOLUO_SLOW_T, XIAOLUO_SLOW, 0.40f,
                   16.0f, -0.15f, 0.30f, 520.0f, XIAOLUO_RING, 6000.0f, 0.04f, 5000.0f},
    [I_TANGGU] = {TANGGU_M, N_(TANGGU_M), 0, 0, 0.30f, 0.003f, 2500.0f, 80.0f, 1.0f, 0.060f, 0.06f, 1.0f, 0.0f, 0.45f, 14.0f, 0.15f,
                  0.30f, 40.0f, 1.0f, 0.0f, 0.0f, 0.0f},
};
