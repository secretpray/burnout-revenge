// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
// Standalone, offline MetalFX API probe. Never linked into either game.
// Inputs are experimental camera-only motion, not production-ready vectors.
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <MetalFX/MetalFX.h>
#include <simd/simd.h>
#include <cstdio>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

static NSData* Read(NSString* folder, NSString* name, NSUInteger bytes)
{
    NSData* data = [NSData dataWithContentsOfFile:[folder stringByAppendingPathComponent:name]];
    if (data.length != bytes)
    {
        fprintf(stderr, "Invalid input %s: %lu bytes, expected %lu\n", name.UTF8String,
            (unsigned long)data.length, (unsigned long)bytes);
        return nil;
    }
    return data;
}

static bool Complete(id<MTLCommandBuffer> command)
{
    [command commit];
    [command waitUntilCompleted];
    if (command.status == MTLCommandBufferStatusError)
    {
        fprintf(stderr, "Metal command failed: %s\n", command.error.description.UTF8String);
        return false;
    }
    return true;
}

static id<MTLTexture> Upload(id<MTLDevice> device, id<MTLCommandQueue> queue, NSData* data,
    MTLPixelFormat format, NSUInteger width, NSUInteger height, NSUInteger bpp, MTLTextureUsage usage)
{
    if (!data) return nil;
    auto* descriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:format width:width height:height mipmapped:NO];
    descriptor.storageMode = MTLStorageModePrivate;
    descriptor.usage = usage;
    id<MTLTexture> texture = [device newTextureWithDescriptor:descriptor];
    if (!texture) return nil;
    const NSUInteger stride = (width * bpp + 255) & ~NSUInteger(255);
    id<MTLBuffer> buffer = [device newBufferWithLength:stride * height options:MTLResourceStorageModeShared];
    if (!buffer) return nil;
    for (NSUInteger y = 0; y < height; ++y)
        memcpy(static_cast<uint8_t*>(buffer.contents) + y * stride,
            static_cast<const uint8_t*>(data.bytes) + y * width * bpp, width * bpp);
    id<MTLCommandBuffer> command = [queue commandBuffer];
    id<MTLBlitCommandEncoder> blit = [command blitCommandEncoder];
    [blit copyFromBuffer:buffer sourceOffset:0 sourceBytesPerRow:stride sourceBytesPerImage:stride * height
        sourceSize:MTLSizeMake(width, height, 1) toTexture:texture destinationSlice:0 destinationLevel:0 destinationOrigin:MTLOriginMake(0,0,0)];
    [blit endEncoding];
    return Complete(command) ? texture : nil;
}

static simd_float4x4 Matrix(NSArray* rows)
{
    simd_float4x4 matrix;
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c)
            matrix.columns[c][r] = [rows[r][c] floatValue];
    return matrix;
}

int main(int argc, char** argv)
{
    @autoreleasepool
    {
        if (argc < 2) { fprintf(stderr, "usage: probe_frame_interpolation <prepared-folder> [--no-ui] [--float16] [--legacy-camera]\n"); return 1; }
        bool withUI = true, floatColor = false, legacyCamera = false;
        for (int i = 2; i < argc; ++i)
            if (!strcmp(argv[i], "--no-ui")) withUI = false;
            else if (!strcmp(argv[i], "--float16")) floatColor = true;
            else if (!strcmp(argv[i], "--legacy-camera")) legacyCamera = true;
            else return 1;
        if (@available(macOS 26.0, *))
        {
            NSString* folder = [NSString stringWithUTF8String:argv[1]];
            NSData* json = [NSData dataWithContentsOfFile:[folder stringByAppendingPathComponent:@"inputs.json"]];
            NSDictionary* info = json ? [NSJSONSerialization JSONObjectWithData:json options:0 error:nil] : nil;
            if (!info) return 1;
            NSUInteger width = [info[@"width"] unsignedIntegerValue], height = [info[@"height"] unsignedIntegerValue];
            if (!width || !height || width > 4096 || height > 2160) return 1;
            id<MTLDevice> device = MTLCreateSystemDefaultDevice();
            if (!device || ![MTLFXFrameInterpolatorDescriptor supportsDevice:device]) return 77;
            auto* descriptor = [MTLFXFrameInterpolatorDescriptor new];
            descriptor.inputWidth = descriptor.outputWidth = width;
            descriptor.inputHeight = descriptor.outputHeight = height;
            const MTLPixelFormat colorFormat = floatColor ? MTLPixelFormatRGBA16Float : MTLPixelFormatRGBA8Unorm;
            const NSUInteger colorBpp = floatColor ? 8 : 4;
            descriptor.colorTextureFormat = descriptor.outputTextureFormat = colorFormat;
            descriptor.uiTextureFormat = colorFormat;
            descriptor.depthTextureFormat = MTLPixelFormatR32Float;
            descriptor.motionTextureFormat = MTLPixelFormatRG16Float;
            id<MTLFXFrameInterpolator> effect = [descriptor newFrameInterpolatorWithDevice:device];
            if (!effect) { fprintf(stderr, "Frame interpolator creation failed\n"); return 2; }
            id<MTLCommandQueue> queue = [device newCommandQueue];
            auto color = [&](NSString* name, MTLTextureUsage usage) {
                NSData* data = Read(folder, name, width*height*4);
                if (data && floatColor)
                {
                    NSMutableData* converted = [NSMutableData dataWithLength:width*height*8];
                    auto* dst = static_cast<_Float16*>(converted.mutableBytes);
                    const auto* src = static_cast<const uint8_t*>(data.bytes);
                    for (NSUInteger i=0; i<width*height*4; ++i) dst[i] = _Float16(src[i]/255.0f);
                    data = converted;
                }
                return Upload(device, queue, data, colorFormat, width, height, colorBpp, usage);
            };
            id<MTLTexture> previous = color(@"previous.rgba", effect.colorTextureUsage);
            id<MTLTexture> current = color(@"current.rgba", effect.colorTextureUsage);
            id<MTLTexture> previousUI = color(@"previous-ui.rgba", effect.uiTextureUsage);
            id<MTLTexture> currentUI = color(@"current-ui.rgba", effect.uiTextureUsage);
            id<MTLTexture> previousDepth = Upload(device, queue, Read(folder, @"previous-depth.f32", width*height*4),
                MTLPixelFormatR32Float, width, height, 4, effect.depthTextureUsage);
            id<MTLTexture> depth = Upload(device, queue, Read(folder, @"current-depth.f32", width*height*4),
                MTLPixelFormatR32Float, width, height, 4, effect.depthTextureUsage);
            id<MTLTexture> motion = Upload(device, queue, Read(folder, @"motion-camera-only.f16", width*height*4),
                MTLPixelFormatRG16Float, width, height, 4, effect.motionTextureUsage);
            NSMutableData* zeros = [NSMutableData dataWithLength:width*height*4];
            id<MTLTexture> zeroMotion = Upload(device, queue, zeros, MTLPixelFormatRG16Float, width, height, 4, effect.motionTextureUsage);
            auto* outDesc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:colorFormat width:width height:height mipmapped:NO];
            outDesc.storageMode = MTLStorageModePrivate;
            outDesc.usage = effect.outputTextureUsage;
            id<MTLTexture> output = [device newTextureWithDescriptor:outDesc];
            if (!previous || !current || !previousUI || !currentUI || !depth || !previousDepth || !motion || !zeroMotion || !output) return 2;
            effect.outputTexture = output;
            effect.depthReversed = YES;
            effect.motionVectorScaleX = effect.motionVectorScaleY = 1;
            effect.uiTextureComposited = withUI;
            if (@available(macOS 27.0, *))
            {
                effect.contentWidth = width; effect.contentHeight = height;
            }
            effect.deltaTime = [info[@"assumed_simulation_delta"] floatValue];
            effect.nearPlane = [info[@"near"] floatValue]; effect.farPlane = [info[@"far"] floatValue];
            effect.fieldOfView = [info[@"fov_y"] floatValue]; effect.aspectRatio = float(width)/float(height);
            NSMutableArray* timings = [NSMutableArray array];
            for (int repeat = 0; repeat < 3; ++repeat)
            {
                // Prime a real previous frame. The measured second encode
                // must use reset=NO, otherwise this would be a passthrough.
                effect.colorTexture = previous; effect.prevColorTexture = previous; effect.uiTexture = withUI ? previousUI : nil;
                effect.depthTexture = previousDepth; effect.motionTexture = zeroMotion;
                if (@available(macOS 27.0, *))
                {
                    if (!legacyCamera)
                    {
                        effect.worldToViewMatrix = Matrix(info[@"previous_view"]);
                        effect.viewToClipMatrix = Matrix(info[@"previous_projection"]);
                    }
                }
                // Repeated stationary frames provide a warm history without
                // introducing inconsistent previous-frame resources.
                for (int warm=0; warm<3; ++warm)
                {
                    effect.shouldResetHistory = warm == 0;
                    id<MTLCommandBuffer> prime = [queue commandBuffer];
                    [effect encodeToCommandBuffer:prime];
                    if (!Complete(prime)) return 2;
                }
                effect.colorTexture = current; effect.prevColorTexture = previous; effect.uiTexture = withUI ? currentUI : nil;
                effect.depthTexture = depth; effect.motionTexture = motion; effect.shouldResetHistory = NO;
                if (@available(macOS 27.0, *))
                {
                    if (!legacyCamera)
                    {
                        effect.worldToViewMatrix = Matrix(info[@"current_view"]);
                        effect.viewToClipMatrix = Matrix(info[@"current_projection"]);
                    }
                }
                id<MTLCommandBuffer> command = [queue commandBuffer];
                [effect encodeToCommandBuffer:command];
                if (!Complete(command)) return 2;
                [timings addObject:@((command.GPUEndTime-command.GPUStartTime)*1000)];
            }
            const NSUInteger stride = (width*colorBpp+255) & ~NSUInteger(255);
            id<MTLBuffer> read = [device newBufferWithLength:stride*height options:MTLResourceStorageModeShared];
            if (!read) return 2;
            id<MTLCommandBuffer> command = [queue commandBuffer];
            id<MTLBlitCommandEncoder> blit = [command blitCommandEncoder];
            [blit copyFromTexture:output sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0,0,0)
                sourceSize:MTLSizeMake(width,height,1) toBuffer:read destinationOffset:0 destinationBytesPerRow:stride destinationBytesPerImage:stride*height];
            [blit endEncoding];
            if (!Complete(command)) return 2;
            NSMutableData* rgba = [NSMutableData dataWithLength:width*height*4];
            for (NSUInteger y=0; y<height; ++y)
                if (floatColor)
                {
                    auto* src = reinterpret_cast<const _Float16*>(static_cast<uint8_t*>(read.contents)+y*stride);
                    auto* dst = static_cast<uint8_t*>(rgba.mutableBytes)+y*width*4;
                    for (NSUInteger i=0; i<width*4; ++i)
                    {
                        const float value = float(src[i]);
                        if (!std::isfinite(value))
                        {
                            fprintf(stderr, "Nonfinite interpolated output at row %lu, component %lu\n",
                                (unsigned long)y, (unsigned long)i);
                            return 2;
                        }
                        dst[i] = uint8_t(std::lround(std::clamp(value,0.0f,1.0f)*255));
                    }
                }
                else memcpy(static_cast<uint8_t*>(rgba.mutableBytes)+y*width*4, static_cast<uint8_t*>(read.contents)+y*stride, width*4);
            NSString* variant = [NSString stringWithFormat:@"%@-%@-%@", floatColor ? @"rgba16" : @"rgba8", withUI ? @"ui" : @"no-ui", legacyCamera ? @"legacy" : @"matrices"];
            NSString* outputName = [NSString stringWithFormat:@"interpolated-%@.rgba", variant];
            if (![rgba writeToFile:[folder stringByAppendingPathComponent:outputName] atomically:YES]) return 2;
            NSData* reference = Read(folder, withUI ? @"current-ui.rgba" : @"current.rgba", width*height*4);
            NSData* previousReference = Read(folder, withUI ? @"previous-ui.rgba" : @"previous.rgba", width*height*4);
            if (!reference || !previousReference) return 2;
            const bool copyOfCurrent = [rgba isEqualToData:reference];
            const bool copyOfPrevious = [rgba isEqualToData:previousReference];
            const auto* actual = static_cast<const uint8_t*>(rgba.bytes);
            const auto* expected = static_cast<const uint8_t*>(reference.bytes);
            const auto* prior = static_cast<const uint8_t*>(previousReference.bytes);
            uint64_t rgbError = 0, previousError = 0;
            for (NSUInteger i=0; i<width*height*4; ++i) if ((i&3)!=3)
            {
                rgbError += std::abs(int(actual[i])-int(expected[i]));
                previousError += std::abs(int(actual[i])-int(prior[i]));
            }
            NSDictionary* report = @{@"gpu":device.name, @"width":@(width), @"height":@(height),
                @"gpu_encode_ms":timings, @"history_reset_for_test_encode":@(effect.shouldResetHistory),
                @"output_exactly_current_input":@(copyOfCurrent),
                @"output_exactly_previous_input":@(copyOfPrevious),
                @"mean_rgb_difference_from_current":@(double(rgbError)/double(width*height*3)),
                @"mean_rgb_difference_from_previous":@(double(previousError)/double(width*height*3)),
                @"history_prime_frames":@3,
                @"variant":variant, @"output_file":outputName,
                @"camera_only_motion":info[@"camera_only_motion"] ?: @YES,
                @"motion_source":info[@"motion_source"] ?: @"synthetic-or-camera",
                @"assumed_simulation_delta":info[@"assumed_simulation_delta"],
                @"live_game_integration":@NO, @"composited_ui":@(withUI)};
            NSData* result = [NSJSONSerialization dataWithJSONObject:report options:NSJSONWritingPrettyPrinted error:nil];
            NSString* resultName = [NSString stringWithFormat:@"result-%@.json", variant];
            if (![result writeToFile:[folder stringByAppendingPathComponent:resultName] atomically:YES]) return 2;
            fprintf(stdout, "%s\n", [[NSString alloc] initWithData:result encoding:NSUTF8StringEncoding].UTF8String);
            return 0;
        }
        return 77;
    }
}
