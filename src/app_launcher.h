/**
 * @file app_launcher.h
 * @brief A phone-like home screen with app icons. Tap an app to enter it,
 *        press the Back button to return home.
 *
 * Demonstrates multi-screen navigation: home screen + independent app screens,
 * using lv_screen_load() to switch. Each app (music/widgets) runs on its own
 * screen so their UIs are fully isolated.
 */

#ifndef APP_LAUNCHER_H
#define APP_LAUNCHER_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Create the home screen with app icons and load it.
 * Call this after lv_init() and HAL init.
 */
void app_launcher_create(void);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*APP_LAUNCHER_H*/
