#include "text_buffers/native.h"
#import <UIKit/UIKit.h>
#ifdef TEXT_BUFFERS_TRACE
#include "text_buffers/trace.h"
@interface TracedTextView : UITextView
@end
@implementation TracedTextView
- (void)layoutSubviews {
  [super layoutSubviews];
  text_trace_record(text_trace_tag(self.text.UTF8String ?: ""), TEXT_TRACE_DRAW,
                    text_trace_clock());
}
@end
#endif
static __weak UIWindow *editor_window;
void editor_apple_init(void *window) { editor_window = (__bridge UIWindow *)window; }
static UITextField *field;
static UITextView *multiline_field;
static uint64_t token, revision;
static bool suppress, live, embedded, retain_on_submit, retained;
static editor_snapshot mailbox;
static bool present;
static void (*notify)(void);
void editor_native_notify(void (*wake)(void)) { notify = wake; }
static id<UITextInput> input(void) {
  return field ? (id<UITextInput>)field : (id<UITextInput>)multiline_field;
}
static NSString *contents(void) { return field ? field.text ?: @"" : multiline_field.text ?: @""; }
static void publish(unsigned flags) {
  id<UITextInput> control = input();
  if (!control || suppress)
    return;
  NSString *text = contents();
  const char *utf8 = text.UTF8String ?: "";
  unsigned previous = present && mailbox.id == token ? mailbox.events : 0;
  if (!ct_bytes_set(&mailbox.text, utf8, strlen(utf8)))
    return;
  mailbox.id = token;
  mailbox.revision = ++revision;
  mailbox.events = flags | previous;
  UITextRange *selected = control.selectedTextRange;
  mailbox.anchor = selected ? [control offsetFromPosition:control.beginningOfDocument
                                               toPosition:selected.start]
                            : 0;
  mailbox.caret = selected ? [control offsetFromPosition:control.beginningOfDocument
                                              toPosition:selected.end]
                           : 0;
  UITextRange *marked = control.markedTextRange;
  mailbox.marked_start = marked ? [control offsetFromPosition:control.beginningOfDocument
                                                   toPosition:marked.start]
                                : -1;
  mailbox.marked_end =
      marked ? [control offsetFromPosition:control.beginningOfDocument toPosition:marked.end] : -1;
  present = true;
  if (notify)
    notify();
}
static void close_control(bool cancelled) {
  if (!input())
    return;
  if (retained && !cancelled)
    return;
  if (!cancelled)
    [input() unmarkText];
  if (!retained)
    publish(cancelled ? EDITOR_CANCELLED : EDITOR_SUBMITTED);
  suppress = true;
  if (!cancelled && retain_on_submit) {
    retained = true;
    [field resignFirstResponder];
    [multiline_field resignFirstResponder];
    field.userInteractionEnabled = NO;
    multiline_field.userInteractionEnabled = NO;
    return;
  }
  UITextField *old_field = field;
  UITextView *old_multi = multiline_field;
  field = nil;
  multiline_field = nil;
  [old_field resignFirstResponder];
  [old_field removeFromSuperview];
  [old_multi resignFirstResponder];
  [old_multi removeFromSuperview];
  retained = false;
  suppress = false;
}
@interface DFTextDelegate : NSObject <UITextFieldDelegate, UITextViewDelegate>
- (void)done;
- (void)changed:(UITextField *)sender;
@end
@implementation DFTextDelegate
- (void)done {
  close_control(false);
}
- (void)changed:(UITextField *)sender {
  if (sender == field)
    publish(0);
}
- (BOOL)textFieldShouldReturn:(UITextField *)sender {
  if (sender == field) {
    if (embedded)
      [sender resignFirstResponder];
    else
      close_control(false);
  }
  return YES;
}
- (void)textFieldDidChangeSelection:(UITextField *)sender {
  if (sender == field)
    publish(0);
}
- (void)textFieldDidEndEditing:(UITextField *)sender {
  if (sender == field && !suppress) {
    if (embedded)
      publish(0);
    else
      close_control(!live);
  }
}
- (void)textViewDidChange:(UITextView *)sender {
  if (sender == multiline_field)
    publish(0);
}
- (void)textViewDidChangeSelection:(UITextView *)sender {
  if (sender == multiline_field)
    publish(0);
}
- (void)textViewDidEndEditing:(UITextView *)sender {
  if (sender == multiline_field && !suppress) {
    if (embedded)
      publish(0);
    else
      close_control(!live);
  }
}
@end
static DFTextDelegate *delegate;
static UIColor *rgba(uint32_t color) {
  return [UIColor colorWithRed:(color >> 24) / 255.0
                         green:((color >> 16) & 255) / 255.0
                          blue:((color >> 8) & 255) / 255.0
                         alpha:(color & 255) / 255.0];
}
static CGRect bounds(editor_rect r) {
  UIWindow *window = editor_window;
  CGFloat scale = window.screen.scale;
  return CGRectMake(r.x / scale, r.y / scale, r.w / scale, r.h / scale);
}
bool platform_editor_open(uint64_t generation, const char *text, editor_options o) {
  close_control(true);
  UIWindow *window = editor_window;
  if (!window)
    return false;
  token = generation;
  revision = 0;
  live = o.live;
  embedded = o.embedded;
  retain_on_submit = o.retain_on_submit && !o.live && !o.embedded;
  retained = false;
  suppress = true;
  if (!delegate)
    delegate = [DFTextDelegate new];
  NSString *initial = [[NSString alloc] initWithBytes:text
                                               length:strlen(text)
                                             encoding:NSUTF8StringEncoding];
  UIFont *font = [UIFont systemFontOfSize:o.font_pixels / window.screen.scale];
  NSString *label = [NSString stringWithUTF8String:o.label ? o.label : "Text"];
  UIView *view;
  if (o.multiline) {
#ifdef TEXT_BUFFERS_TRACE
    multiline_field = [[TracedTextView alloc] initWithFrame:bounds(o.bounds)];
#else
    multiline_field = [[UITextView alloc] initWithFrame:bounds(o.bounds)];
#endif
    multiline_field.delegate = delegate;
    multiline_field.text = initial;
    multiline_field.font = font;
    multiline_field.textColor = rgba(o.foreground);
    multiline_field.backgroundColor = rgba(o.background);
    multiline_field.textContainerInset = UIEdgeInsetsZero;
    multiline_field.textContainer.lineFragmentPadding = 0;
    multiline_field.accessibilityLabel = label;
    if (!embedded) {
      UIToolbar *toolbar =
          [[UIToolbar alloc] initWithFrame:CGRectMake(0, 0, o.bounds.w / window.screen.scale, 44)];
      toolbar.items = @[
        [[UIBarButtonItem alloc] initWithBarButtonSystemItem:UIBarButtonSystemItemFlexibleSpace
                                                      target:nil
                                                      action:nil],
        [[UIBarButtonItem alloc] initWithBarButtonSystemItem:UIBarButtonSystemItemDone
                                                      target:delegate
                                                      action:@selector(done)]
      ];
      multiline_field.inputAccessoryView = toolbar;
    }
    if (embedded)
      multiline_field.keyboardDismissMode = UIScrollViewKeyboardDismissModeInteractive;
    view = multiline_field;
  } else {
    field = [[UITextField alloc] initWithFrame:bounds(o.bounds)];
    field.delegate = delegate;
    field.text = initial;
    field.font = font;
    field.textColor = rgba(o.foreground);
    field.backgroundColor = rgba(o.background);
    field.borderStyle = UITextBorderStyleNone;
    field.contentVerticalAlignment = UIControlContentVerticalAlignmentTop;
    field.returnKeyType = UIReturnKeyDone;
    field.accessibilityLabel = label;
    [field addTarget:delegate
                  action:@selector(changed:)
        forControlEvents:UIControlEventEditingChanged];
    view = field;
  }
  [window addSubview:view];
  // Resolve the opening selection before focus creates the visible caret.
  // Selecting the start after focus gives UIKit an intermediate position to
  // display/animate from, even when both selections happen in the same call.
  [UIView performWithoutAnimation:^{
    [view layoutIfNeeded];
    id<UITextInput> control = input();
    UITextPosition *position = control.endOfDocument;
    if (o.place_at_point) {
      if (multiline_field)
        multiline_field.contentOffset = CGPointZero;
      position = [control closestPositionToPoint:CGPointMake(o.point_x / window.screen.scale,
                                                             o.point_y / window.screen.scale)];
      if (!position)
        position = control.endOfDocument;
    }
    control.selectedTextRange = [control textRangeFromPosition:position toPosition:position];
    [view layoutIfNeeded];
  }];
  if (!embedded)
    [view becomeFirstResponder];
  suppress = false;
  publish(0);
  return true;
}
bool platform_editor_poll(editor_snapshot *out) {
  if (!present)
    return false;
  *out = mailbox;
  memset(&mailbox, 0, sizeof(mailbox));
  present = false;
  return true;
}
bool platform_editor_set(uint64_t generation, uint64_t expected, const char *text, size_t anchor,
                         size_t caret) {
#ifdef TEXT_BUFFERS_TRACE
  uint32_t trace_tag = text_trace_tag(text);
  text_trace_record(trace_tag, TEXT_TRACE_NATIVE_BEGIN, text_trace_clock());
#endif
  id<UITextInput> control = input();
  if (!control || generation != token)
    return false;
  if (expected != revision || control.markedTextRange) {
#ifdef TEXT_BUFFERS_TRACE
    text_trace_record(trace_tag, TEXT_TRACE_REJECTED, text_trace_clock());
#endif
    publish(EDITOR_ACK | EDITOR_REJECTED);
    return true;
  }
  NSString *next = [[NSString alloc] initWithBytes:text
                                            length:strlen(text)
                                          encoding:NSUTF8StringEncoding];
  if (!next || anchor > next.length || caret > next.length)
    return false;
  suppress = true;
  NSString *current = contents();
  NSUInteger prefix = 0, old_end = current.length, new_end = next.length;
  while (prefix < old_end && prefix < new_end &&
         [current characterAtIndex:prefix] == [next characterAtIndex:prefix])
    ++prefix;
  /* Keep replacement boundaries outside UTF-16 surrogate pairs. */
  if (prefix && prefix < old_end &&
      CFStringIsSurrogateLowCharacter([current characterAtIndex:prefix]))
    --prefix;
  while (old_end > prefix && new_end > prefix &&
         [current characterAtIndex:old_end - 1] == [next characterAtIndex:new_end - 1]) {
    --old_end;
    --new_end;
  }
  if (old_end && old_end < current.length &&
      CFStringIsSurrogateLowCharacter([current characterAtIndex:old_end])) {
    ++old_end;
    ++new_end;
  }
  if (prefix != old_end || prefix != new_end) {
    UITextPosition *begin = [control positionFromPosition:control.beginningOfDocument
                                                   offset:(NSInteger)prefix];
    UITextPosition *finish = [control positionFromPosition:control.beginningOfDocument
                                                    offset:(NSInteger)old_end];
    [control replaceRange:[control textRangeFromPosition:begin toPosition:finish]
                 withText:[next substringWithRange:NSMakeRange(prefix, new_end - prefix)]];
  }
  /* UIKit ranges are ordered. The shared buffer supports directional selections;
   * UIKit reports its normalized range on the next selection event. */
  size_t lo = anchor < caret ? anchor : caret, hi = anchor > caret ? anchor : caret;
  UITextRange *selected = control.selectedTextRange;
  if (!selected ||
      [control offsetFromPosition:control.beginningOfDocument
                       toPosition:selected.start] != (NSInteger)lo ||
      [control offsetFromPosition:control.beginningOfDocument
                       toPosition:selected.end] != (NSInteger)hi) {
    UITextPosition *start = [control positionFromPosition:control.beginningOfDocument
                                                   offset:(NSInteger)lo];
    UITextPosition *end = [control positionFromPosition:control.beginningOfDocument
                                                 offset:(NSInteger)hi];
    control.selectedTextRange = [control textRangeFromPosition:start toPosition:end];
  }
  suppress = false;
#ifdef TEXT_BUFFERS_TRACE
  text_trace_record(trace_tag, TEXT_TRACE_NATIVE_SET, text_trace_clock());
#endif
  publish(EDITOR_ACK);
  return true;
}
bool platform_editor_layout(uint64_t generation, editor_rect r) {
  if (!input() || generation != token)
    return false;
  if (field)
    field.frame = bounds(r);
  else
    multiline_field.frame = bounds(r);
  return true;
}
void platform_editor_close(uint64_t generation) {
  if (generation == token)
    close_control(true);
}
