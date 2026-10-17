// Burnout Revenge packaging. GPL-3.0-or-later (see COPYING).
// Normalize supplied artwork to macOS's icon canvas with a deterministic alpha mask.
#import <AppKit/AppKit.h>
int main(int argc, const char** argv) {
    @autoreleasepool {
        if (argc != 3) return 2;
        NSImage* source = [[NSImage alloc] initWithContentsOfFile:@(argv[1])];
        if (!source) return 3;
        NSBitmapImageRep* pixels = [[NSBitmapImageRep alloc] initWithBitmapDataPlanes:nil pixelsWide:1024 pixelsHigh:1024
            bitsPerSample:8 samplesPerPixel:4 hasAlpha:YES isPlanar:NO colorSpaceName:NSDeviceRGBColorSpace
            bytesPerRow:4096 bitsPerPixel:32];
        [NSGraphicsContext saveGraphicsState];
        [NSGraphicsContext setCurrentContext:[NSGraphicsContext graphicsContextWithBitmapImageRep:pixels]];
        NSRect canvas = NSMakeRect(0,0,1024,1024);
        [[NSColor clearColor] setFill]; NSRectFillUsingOperation(canvas, NSCompositingOperationCopy);
        [[NSBezierPath bezierPathWithRoundedRect:NSMakeRect(100,100,824,824) xRadius:185 yRadius:185] addClip];
        // Solid backing inside the silhouette removes accidental semitransparency in artwork.
        [[NSColor colorWithWhite:0.04 alpha:1] setFill]; NSRectFill(canvas);
        [source drawInRect:canvas fromRect:NSZeroRect operation:NSCompositingOperationSourceOver fraction:1
            respectFlipped:NO hints:@{NSImageHintInterpolation:@(NSImageInterpolationHigh)}];
        [NSGraphicsContext restoreGraphicsState];
        NSData* png = [pixels representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
        return [png writeToFile:@(argv[2]) atomically:YES] ? 0 : 4;
    }
}
