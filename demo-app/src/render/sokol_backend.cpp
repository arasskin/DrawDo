// Platform implementation adapted from recovered junstodos/src/sokol.mm.
#define SOKOL_IMPL
#include "sokol/sokol_app.h"
#include "sokol/sokol_gfx.h"
#include "sokol/sokol_log.h"
#include "sokol/sokol_glue.h"

// Opt-in device diagnostics for black frames; disabled in ordinary runs/benchmarks.
extern "C" void demo_trace_frame() {
#if defined(__APPLE__)
    static bool enabled=getenv("DATAFRIEND_RENDER_DIAGNOSTICS")!=nullptr;
    static uint64_t frames=0;
    if(!enabled)return;
    const auto frame=++frames;
    if(frame%60==1){
        NSLog(@"Frame %llu window %@ view %@ metal %@ scene=%ld",frame,
              _sapp.ios.window,_sapp.ios.view,_sapp.ios.mtl.layer,
              (long)_sapp.ios.window.windowScene.activationState);
    }
    [_sg.mtl.cmd_buffer addCompletedHandler:^(id<MTLCommandBuffer> command){
        if(command.error)NSLog(@"GPU frame %llu failed: %@",frame,command.error);
    }];
#endif
}
