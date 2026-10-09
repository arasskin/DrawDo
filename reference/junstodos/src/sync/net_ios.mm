// iOS/macOS implementation of the network transport seam (net_platform.h):
// one synchronous HTTP POST per protocol request via NSURLSession.
// Synchronous-over-async with a semaphore — callers run on a background
// thread by contract (see net_platform.h).

#include "net_platform.h"

#import <Foundation/Foundation.h>

#include <stdexcept>

namespace jsync {

Transport http_transport(std::string url, int timeout_seconds) {
    return [url, timeout_seconds](std::string_view request) -> std::string {
        @autoreleasepool {
            NSURL* nsurl = [NSURL URLWithString:[NSString stringWithUTF8String:url.c_str()]];
            if (!nsurl) throw std::runtime_error("net: bad url: " + url);

            NSMutableURLRequest* req = [NSMutableURLRequest requestWithURL:nsurl];
            req.HTTPMethod = @"POST";
            req.HTTPBody = [NSData dataWithBytes:request.data() length:request.size()];
            req.timeoutInterval = (NSTimeInterval)timeout_seconds;
            [req setValue:@"application/octet-stream" forHTTPHeaderField:@"Content-Type"];

            __block NSData* body = nil;
            __block NSInteger status = 0;
            __block NSString* err_desc = nil;
            dispatch_semaphore_t sem = dispatch_semaphore_create(0);

            NSURLSessionDataTask* task = [[NSURLSession sharedSession]
                dataTaskWithRequest:req
                  completionHandler:^(NSData* data, NSURLResponse* resp, NSError* error) {
                    if (error) {
                        err_desc = [error.localizedDescription copy];
                    } else {
                        body = [data copy];
                        status = ((NSHTTPURLResponse*)resp).statusCode;
                    }
                    dispatch_semaphore_signal(sem);
                  }];
            [task resume];
            dispatch_semaphore_wait(sem, DISPATCH_TIME_FOREVER);

            if (err_desc) {
                throw std::runtime_error(std::string("net: ") + err_desc.UTF8String);
            }
            if (status != 200) {
                throw std::runtime_error("net: http status " + std::to_string((long)status));
            }
            return std::string((const char*)body.bytes, body.length);
        }
    };
}

}  // namespace jsync
