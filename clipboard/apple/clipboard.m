#include "clipboard/clipboard.h"
#include "text_buffers/text.h"
#import <UIKit/UIKit.h>
bool clipboard_set(const char *utf8) {
  if (!utf8)
    return false;
  NSString *text = [[NSString alloc] initWithBytes:utf8
                                            length:strlen(utf8)
                                          encoding:NSUTF8StringEncoding];
  if (!text)
    return false;
  UIPasteboard.generalPasteboard.string = text;
  return true;
}
bool clipboard_get(ct_bytes *out) {
  NSString *text = UIPasteboard.generalPasteboard.string ?: @"";
  NSData *bytes = [text dataUsingEncoding:NSUTF8StringEncoding];
  return bytes && text_valid(bytes.bytes, bytes.length) &&
         ct_bytes_set(out, bytes.bytes, bytes.length);
}
