#import <AVFoundation/AVFoundation.h>
#include <cstdio>
int systemCameraIndex() {
    @autoreleasepool {
        if ([AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeVideo] == AVAuthorizationStatusNotDetermined) {
            dispatch_semaphore_t permission = dispatch_semaphore_create(0);
            [AVCaptureDevice requestAccessForMediaType:AVMediaTypeVideo completionHandler:^(BOOL granted) {
                dispatch_semaphore_signal(permission);
            }];
            dispatch_semaphore_wait(permission, dispatch_time(DISPATCH_TIME_NOW, 60 * NSEC_PER_SEC));
        }
        if ([AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeVideo] != AVAuthorizationStatusAuthorized) {
            std::fprintf(stderr, "Allow camera access in macOS Privacy & Security settings.\n");
            return -1;
        }
        // Match OpenCV 4.8's enumeration order exactly.
        NSArray<AVCaptureDevice *> *devices = [AVCaptureDevice devicesWithMediaType:AVMediaTypeVideo];
        for (NSUInteger i = 0; i < devices.count; ++i) {
            AVCaptureDevice *device = devices[i];
            if ([device.localizedName isEqualToString:@"HD CAMERA"]) {
                std::printf("System camera: %s (index %lu)\n", device.localizedName.UTF8String, (unsigned long)i);
                return (int)i;
            }
        }
        std::fprintf(stderr, "HD CAMERA not found; no fallback to the built-in camera.\n");
        return -1;
    }
}
