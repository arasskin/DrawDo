#include "platform/runtime.h"
#include "sokol/sokol_app.h"
#import <UIKit/UIKit.h>
void platform_keep_awake(bool enabled) {
  UIApplication.sharedApplication.idleTimerDisabled = enabled;
}
platform_insets platform_safe_area(void) {
  UIWindow *window = (__bridge UIWindow *)sapp_ios_get_window();
  UIEdgeInsets area = window.safeAreaInsets;
  float scale = window.screen.scale;
  return (platform_insets){(float)area.top * scale, (float)area.bottom * scale};
}
const char *platform_directory(void) {
  static char *directory;
  if (!directory)
    directory = strdup(
        NSSearchPathForDirectoriesInDomains(NSApplicationSupportDirectory, NSUserDomainMask, YES)
            .firstObject.UTF8String);
  return directory;
}
void platform_log(bool error, const char *tag, const char *format, ...) {
  va_list args;
  va_start(args, format);
  fprintf(stderr, "%s%s: ", error ? "ERROR " : "", tag);
  vfprintf(stderr, format, args);
  fputc('\n', stderr);
  va_end(args);
}
bool platform_profile_options(int *count, int *kind, int *warmup, int *frames) {
  NSArray *args = NSProcessInfo.processInfo.arguments;
  NSUInteger index = [args indexOfObject:@"--task-profile"];
  if (index == NSNotFound)
    return false;
  UIApplication.sharedApplication.idleTimerDisabled = YES;
  if (index + 4 >= args.count)
    return false;
  *count = [args[index + 1] intValue];
  *kind = [args[index + 2] intValue];
  *warmup = [args[index + 3] intValue];
  *frames = [args[index + 4] intValue];
  return true;
}

void platform_profile_metadata(FILE *out) {
  fprintf(out, ",\"thermal_state\":%ld,\"low_power\":%d",
          (long)NSProcessInfo.processInfo.thermalState,
          NSProcessInfo.processInfo.lowPowerModeEnabled);
}
