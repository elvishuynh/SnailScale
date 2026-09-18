#ifndef HAPTIC_MANAGER_H
#define HAPTIC_MANAGER_H

#ifdef __cplusplus
extern "C" {
#endif

// init haptic manager
int haptic_manager_init(void);

// reinit haptics after power rail restore
int haptic_manager_reinit(void);

// trigger click effect
int haptic_play_click(void);

// trigger subtle click effect
int haptic_play_subtle_click(void);

// trigger arm cal effect
int haptic_play_arm_cal(void);

// trigger cal confirm effect
int haptic_play_cal_confirm(void);

#ifdef __cplusplus
}
#endif

#endif
