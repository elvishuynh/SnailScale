#ifndef PERIPH_POWER_H
#define PERIPH_POWER_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Isolates signal lines and disables the power rail to prevent parasitic drain.
 */
void periph_3v3_off(void);

/**
 * @brief Enables the power rail and restores signal line configurations.
 */
void periph_3v3_on(void);

#ifdef __cplusplus
}
#endif

#endif /* PERIPH_POWER_H */
