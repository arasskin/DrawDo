#import <UIKit/UIKit.h>
#import <ReplayKit/ReplayKit.h>
#import <CoreImage/CoreImage.h>
#import <ImageIO/ImageIO.h>
#include "text_buffers/trace.h"

/* Benchmark only: save the changing first line, never the rest of the screen.
 * callback_ns is a conservative observation time, not a display timestamp. */
static NSString *capture_directory;
static CIContext *context;
static CGFloat width_points, top_points;
static uint64_t last_hash;
static unsigned frame_index;
static FILE *frame_log;
static bool capturing;

void timing_capture_start(const char *directory) {
  capture_directory = [NSString stringWithUTF8String:directory];
  UIWindow *window = UIApplication.sharedApplication.windows.firstObject;
  width_points = window.bounds.size.width;
  top_points = window.safeAreaInsets.top + 10;
  context = [CIContext contextWithOptions:nil];
  frame_log =
      fopen([[capture_directory stringByAppendingPathComponent:@"frames.csv"] UTF8String], "w");
  if (!frame_log)
    return;
  fprintf(frame_log, "frame,callback_ns,pts_ns\n");
  RPScreenRecorder *recorder = RPScreenRecorder.sharedRecorder;
  recorder.microphoneEnabled = NO;
  [recorder
      startCaptureWithHandler:^(CMSampleBufferRef sample, RPSampleBufferType type, NSError *error) {
        if (error || type != RPSampleBufferTypeVideo)
          return;
        uint64_t observed = text_trace_clock();
        CVPixelBufferRef buffer = CMSampleBufferGetImageBuffer(sample);
        if (!buffer || width_points <= 0)
          return;
        CGFloat scale = CVPixelBufferGetWidth(buffer) / width_points;
        CGRect crop = CGRectMake(0, CVPixelBufferGetHeight(buffer) - (top_points + 50) * scale,
                                 180 * scale, 50 * scale);
        CIImage *source = [CIImage imageWithCVPixelBuffer:buffer];
        CGImageRef picture = [context createCGImage:source fromRect:crop];
        if (!picture)
          return;
        CFDataRef pixels = CGDataProviderCopyData(CGImageGetDataProvider(picture));
        const UInt8 *bytes = CFDataGetBytePtr(pixels);
        uint64_t hash = 14695981039346656037ULL;
        for (CFIndex i = 0; i < CFDataGetLength(pixels); ++i)
          hash = (hash ^ bytes[i]) * 1099511628211ULL;
        CFRelease(pixels);
        if (hash != last_hash) {
          last_hash = hash;
          unsigned index = ++frame_index;
          NSString *name = [NSString stringWithFormat:@"frame-%04u.png", index];
          NSURL *url =
              [NSURL fileURLWithPath:[capture_directory stringByAppendingPathComponent:name]];
          CGImageDestinationRef output =
              CGImageDestinationCreateWithURL((__bridge CFURLRef)url, CFSTR("public.png"), 1, NULL);
          if (output) {
            CGImageDestinationAddImage(output, picture, NULL);
            CGImageDestinationFinalize(output);
            CFRelease(output);
          }
          CMTime pts = CMSampleBufferGetPresentationTimeStamp(sample);
          fprintf(
              frame_log, "%u,%llu,%lld\n", index, (unsigned long long)observed,
              (long long)CMTimeConvertScale(pts, 1000000000, kCMTimeRoundingMethod_Default).value);
          fflush(frame_log);
        }
        CGImageRelease(picture);
      }
      completionHandler:^(NSError *error) {
        capturing = error == nil;
        if (error)
          NSLog(@"Timing capture failed: %@", error);
      }];
}

void timing_capture_stop(void) {
  if (!capturing)
    return;
  capturing = false;
  [RPScreenRecorder.sharedRecorder stopCaptureWithHandler:^(NSError *error) {
    (void)error;
    if (frame_log) {
      fclose(frame_log);
      frame_log = NULL;
    }
  }];
}
