#include "haptics/haptics.h"
#import <UIKit/UIKit.h>
void haptic_play(enum haptic_kind kind) {
  static UISelectionFeedbackGenerator *selection;
  static UIImpactFeedbackGenerator *impact;
  static UINotificationFeedbackGenerator *notification;
  switch (kind) {
  case HAPTIC_SELECTION:
    if (!selection)
      selection = [UISelectionFeedbackGenerator new];
    [selection selectionChanged];
    break;
  case HAPTIC_IMPACT:
    if (!impact)
      impact = [[UIImpactFeedbackGenerator alloc] initWithStyle:UIImpactFeedbackStyleLight];
    [impact impactOccurred];
    break;
  case HAPTIC_SUCCESS:
    if (!notification)
      notification = [UINotificationFeedbackGenerator new];
    [notification notificationOccurred:UINotificationFeedbackTypeSuccess];
    break;
  }
}
