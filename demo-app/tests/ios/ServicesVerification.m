#import <XCTest/XCTest.h>
#include "clipboard/clipboard.h"
#include "haptics/haptics.h"
#include "accessible_ui/native.h"
@interface ServicesVerification : XCTestCase
@end
@implementation ServicesVerification
- (void)testAccessibleScreens {
  self.continueAfterFailure = NO;
  XCUIApplication *app = [[XCUIApplication alloc] initWithBundleIdentifier:@"dev.drawdo.sokol"];
  [app activate];
  XCTAssertTrue([app.buttons[@"Type / edit"] waitForExistenceWithTimeout:5]);
  [app.buttons[@"Type / edit"] tap];
  XCTAssertTrue([app.textViews[@"Shared document"] waitForExistenceWithTimeout:5]);
  XCTAssertFalse(app.buttons[@"Type / edit"].exists);
  [app.buttons[@"Done"] tap];
  XCTAssertTrue([app.buttons[@"Particles"] waitForExistenceWithTimeout:5]);
  [app.buttons[@"Particles"] tap];
  XCTAssertTrue([app.buttons[@"Tasks"] waitForExistenceWithTimeout:5]);
  [app.buttons[@"Tasks"] tap];
  XCTAssertTrue([app.buttons[@"Document"] waitForExistenceWithTimeout:5]);
  XCTAssertTrue(app.buttons[@"All"].exists);
  [app.buttons[@"Document"] tap];
  XCTAssertTrue([app.buttons[@"Type / edit"] waitForExistenceWithTimeout:5]);
}
@end
