#import <XCTest/XCTest.h>
@interface EditorVerification : XCTestCase
@end
@implementation EditorVerification
- (NSArray<NSValue *> *)lines:(UIImage *)image region:(CGRect)region scale:(CGFloat)scale {
  CGRect pixels = CGRectMake(floor(region.origin.x * scale), floor(region.origin.y * scale),
                             floor(region.size.width * scale), floor(region.size.height * scale));
  CGImageRef crop = CGImageCreateWithImageInRect(image.CGImage, pixels);
  size_t w = CGImageGetWidth(crop), h = CGImageGetHeight(crop);
  uint8_t *data = calloc(w * h, 4);
  CGColorSpaceRef space = CGColorSpaceCreateDeviceRGB();
  CGContextRef context = CGBitmapContextCreate(
      data, w, h, 8, w * 4, space, kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big);
  CGContextDrawImage(context, CGRectMake(0, 0, w, h), crop);
  NSMutableArray<NSValue *> *lines = [NSMutableArray array];
  CGRect line = CGRectNull;
  for (size_t y = 0; y < h; y++) {
    size_t lo = w, hi = 0;
    for (size_t x = 0; x < w; x++) {
      uint8_t *p = data + (y * w + x) * 4;
      if (p[0] > 150 && p[1] > 150 && p[2] > 150) {
        lo = MIN(lo, x);
        hi = MAX(hi, x);
      }
    }
    if (lo < w)
      line = CGRectUnion(line, CGRectMake(lo, y, hi - lo + 1, 1));
    else if (!CGRectIsNull(line)) {
      [lines addObject:[NSValue valueWithCGRect:line]];
      line = CGRectNull;
    }
  }
  if (!CGRectIsNull(line))
    [lines addObject:[NSValue valueWithCGRect:line]];
  CGContextRelease(context);
  CGColorSpaceRelease(space);
  CGImageRelease(crop);
  free(data);
  return lines;
}
- (void)capture:(NSString *)name {
  XCTAttachment *image = [XCTAttachment attachmentWithScreenshot:XCUIScreen.mainScreen.screenshot];
  image.name = name;
  image.lifetime = XCTAttachmentLifetimeKeepAlways;
  [self addAttachment:image];
}
// Locate the blue insertion caret in the document, excluding keyboard chrome.
- (CGRect)caret:(UIImage *)image region:(CGRect)region scale:(CGFloat)scale {
  CGRect pixels = CGRectMake(floor(region.origin.x * scale), floor(region.origin.y * scale),
                             floor(region.size.width * scale), floor(region.size.height * scale));
  CGImageRef crop = CGImageCreateWithImageInRect(image.CGImage, pixels);
  size_t w = CGImageGetWidth(crop), h = CGImageGetHeight(crop);
  uint8_t *data = calloc(w * h, 4);
  CGColorSpaceRef space = CGColorSpaceCreateDeviceRGB();
  CGContextRef context = CGBitmapContextCreate(
      data, w, h, 8, w * 4, space, kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big);
  CGContextDrawImage(context, CGRectMake(0, 0, w, h), crop);
  CGRect caret = CGRectNull;
  for (size_t x = 0; x < w; x++) {
    size_t count = 0, lo = h, hi = 0;
    for (size_t y = 0; y < h; y++) {
      uint8_t *p = data + (y * w + x) * 4;
      if (p[0] < 100 && p[2] > 150 && p[2] > p[1] * 1.3) {
        count++;
        lo = MIN(lo, y);
        hi = MAX(hi, y);
      }
    }
    // Reject isolated blue antialiasing pixels at glyph edges.
    if (count >= 10 * scale)
      caret = CGRectUnion(caret, CGRectMake(x, lo, 1, hi - lo + 1));
  }
  CGContextRelease(context);
  CGColorSpaceRelease(space);
  CGImageRelease(crop);
  free(data);
  return caret;
}
- (void)testInitialTap {
  self.continueAfterFailure = NO;
  XCUIApplication *app = [[XCUIApplication alloc] initWithBundleIdentifier:@"dev.drawdo.sokol"];
  [app activate];
  XCTAssertTrue([app.buttons[@"Type / edit"] waitForExistenceWithTimeout:5]);
  CGFloat scale = app.frame.size.width / 400.0;
  CGRect region = CGRectMake(28 * scale, 47 + 230 * scale, 344 * scale, 100 * scale);
  UIImage *before = XCUIScreen.mainScreen.screenshot.image;
  CGFloat pixels = CGImageGetWidth(before.CGImage) / app.frame.size.width;
  NSArray<NSValue *> *lines = [self lines:before region:region scale:pixels];
  XCTAssertGreaterThan(lines.count, 0);
  XCUICoordinate *origin = [app coordinateWithNormalizedOffset:CGVectorMake(0, 0)];
  NSString *original = nil;
  for (NSUInteger i = 0; i < MIN((NSUInteger)2, lines.count); i++) {
    CGRect line = lines[i].CGRectValue;
    CGPoint target = CGPointMake(CGRectGetMidX(line), CGRectGetMidY(line));
    [[origin coordinateWithOffset:CGVectorMake(region.origin.x + target.x / pixels,
                                               region.origin.y + target.y / pixels)] tap];
    XCUIElement *editor = app.textViews[@"Shared document"];
    XCTAssertTrue([editor waitForExistenceWithTimeout:5]);
    XCTAssertTrue([app.keyboards.firstMatch waitForExistenceWithTimeout:5]);
    if (!original)
      original = editor.value;
    XCTAssertEqualObjects(editor.value, original);
    CGRect caret = CGRectNull;
    for (int attempt = 0; attempt < 8 && CGRectIsNull(caret); attempt++)
      caret = [self caret:XCUIScreen.mainScreen.screenshot.image region:region scale:pixels];
    [self capture:[NSString stringWithFormat:@"ios-tap-line-%lu", (unsigned long)i + 1]];
    NSLog(@"Tap %@, caret %@", NSStringFromCGPoint(target), NSStringFromCGRect(caret));
    XCTAssertFalse(CGRectIsNull(caret));
    XCTAssertEqualWithAccuracy(CGRectGetMidX(caret), target.x, 12 * scale * pixels);
    XCTAssertEqualWithAccuracy(CGRectGetMidY(caret), target.y, 8 * scale * pixels);
    [app.buttons[@"Done"] tap];
    NSPredicate *gone = [NSPredicate predicateWithFormat:@"exists == NO"];
    XCTNSPredicateExpectation *closed =
        [[XCTNSPredicateExpectation alloc] initWithPredicate:gone object:editor];
    XCTAssertEqual([XCTWaiter waitForExpectations:@[ closed ] timeout:5], XCTWaiterResultCompleted);
  }
}
- (void)testKeyboardLayout {
  self.continueAfterFailure = NO;
  XCUIApplication *app = [[XCUIApplication alloc] initWithBundleIdentifier:@"dev.drawdo.sokol"];
  [app activate];
  XCTAssertTrue([app.buttons[@"Type / edit"] waitForExistenceWithTimeout:5]);
  [self capture:@"ios-before-keyboard"];
  UIImage *before = XCUIScreen.mainScreen.screenshot.image;
  // The canvas is laid out in 400 units. Tap the document's Type/edit button.
  CGFloat scale = app.frame.size.width / 400.0;
  CGFloat top = 47.0; // Portrait iPhone 13 safe area; native frame assertions below.
  XCUICoordinate *origin = [app coordinateWithNormalizedOffset:CGVectorMake(0, 0)];
  [[origin coordinateWithOffset:CGVectorMake(70 * scale, top + 135 * scale)] tap];
  XCUIElement *editor = app.textViews[@"Shared document"];
  XCTAssertTrue([editor waitForExistenceWithTimeout:5]);
  XCTAssertTrue([app.keyboards.firstMatch waitForExistenceWithTimeout:5]);
  CGRect opened = editor.frame;
  [self capture:@"ios-keyboard-open"];
  UIImage *after = XCUIScreen.mainScreen.screenshot.image;
  XCTAssertEqualWithAccuracy(opened.origin.x, 28 * scale, 1);
  XCTAssertEqualWithAccuracy(opened.origin.y, top + 230 * scale, 1);
  CGRect visible = opened;
  visible.size.height =
      MIN(visible.size.height, app.keyboards.firstMatch.frame.origin.y - visible.origin.y - 50);
  CGFloat pixels = CGImageGetWidth(before.CGImage) / app.frame.size.width;
  NSArray<NSValue *> *a = [self lines:before region:visible scale:pixels];
  NSArray<NSValue *> *b = [self lines:after region:visible scale:pixels];
  NSLog(@"Passive glyph bounds: %@; native glyph bounds: %@", a, b);
  XCTAssertGreaterThan(a.count, 0);
  XCTAssertEqual(a.count, b.count);
  for (NSUInteger i = 0; i < MIN(a.count, b.count); i++) {
    CGRect x = a[i].CGRectValue, y = b[i].CGRectValue;
    XCTAssertEqualWithAccuracy(x.origin.x, y.origin.x, 2);
    XCTAssertEqualWithAccuracy(x.origin.y, y.origin.y, 2);
    XCTAssertEqualWithAccuracy(CGRectGetMaxX(x), CGRectGetMaxX(y), 2);
    XCTAssertEqualWithAccuracy(CGRectGetMaxY(x), CGRectGetMaxY(y), 2);
  }
  [app.buttons[@"Done"] tap];
  NSPredicate *gone = [NSPredicate predicateWithFormat:@"exists == NO"];
  XCTNSPredicateExpectation *closed = [[XCTNSPredicateExpectation alloc] initWithPredicate:gone
                                                                                    object:editor];
  XCTAssertEqual([XCTWaiter waitForExpectations:@[ closed ] timeout:5], XCTWaiterResultCompleted);
  [self capture:@"ios-keyboard-closed"];
}
@end
