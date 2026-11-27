#pragma once

#import <AppKit/AppKit.h>
#import <AVFoundation/AVFoundation.h>
#import <CoreGraphics/CoreGraphics.h>
#import <CoreMedia/CoreMedia.h>
#import <CoreVideo/CoreVideo.h>
#import <ScreenCaptureKit/ScreenCaptureKit.h>

#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QImage>
#include <QTimer>
#include <QWidget>

#include <algorithm>
#include <cmath>
#include <memory>
#include <stdexcept>

namespace DesktopQA {

inline void require(bool condition, const QString& message) {
    if (!condition) throw std::runtime_error(message.toStdString());
}

inline QString qtString(NSString* value) {
    return value ? QString::fromUtf8(value.UTF8String) : QString();
}

inline void pump(int milliseconds) {
    QEventLoop loop;
    QTimer::singleShot(milliseconds, &loop, &QEventLoop::quit);
    loop.exec();
}

template<class Predicate>
bool waitFor(Predicate ready, int timeout = 8000) {
    QElapsedTimer timer;
    timer.start();
    while (!ready() && timer.elapsed() < timeout) pump(20);
    return ready();
}

inline NSView* nativeView(QWidget& widget) {
    return (__bridge NSView*)reinterpret_cast<void*>(widget.winId());
}

inline NSPoint viewPoint(NSView* view, const QPoint& point) {
    return [view convertPoint:NSMakePoint(point.x(), view.isFlipped ? point.y()
                                              : NSHeight(view.bounds) - point.y()) toView:nil];
}

inline QPointF framePoint(NSWindow* window, NSPoint windowPoint) {
    NSPoint screenPoint = [window convertPointToScreen:windowPoint];
    return {screenPoint.x - NSMinX(window.frame), NSMaxY(window.frame) - screenPoint.y};
}

template<class T>
struct CaptureResult {
    T payload = nil;
    NSError* failure = nil;
    bool finished = false;
};

class API_AVAILABLE(macos(15.0)) WindowCapture {
    struct Recording {
        AVAssetWriter* writer = nil;
        AVAssetWriterInput* input = nil;
        AVAssetWriterInputPixelBufferAdaptor* pixels = nil;
        CMTime start = kCMTimeInvalid;
        CMTime last = kCMTimeZero;
        bool pending = false;
        bool closing = false;
        int frames = 0;
        int backpressureDrops = 0;
        double longestGap = 0;
        double seconds = 0;
        QString failure;
    };

public:
    explicit WindowCapture(NSWindow* window) {
        require(window.windowNumber > 0 && window.isVisible, "MainWindow has no visible NSWindow");
        const NSInteger windowNumber = window.windowNumber;
        // Ask macOS only for content it makes available to this process.
        auto result = std::make_shared<CaptureResult<SCShareableContent*>>();
        [SCShareableContent getCurrentProcessShareableContentWithCompletionHandler:
            ^(SCShareableContent* content, NSError* error) {
                dispatch_async(dispatch_get_main_queue(), ^{
                    result->payload = content;
                    result->failure = error;
                    result->finished = true;
                });
            }];
        require(waitFor([&] { return result->finished; }),
                "Timed out locating the current process window");
        SCWindow* selected = nil;
        for (SCWindow* item in result->payload.windows) {
            if (item.windowID == static_cast<CGWindowID>(windowNumber) &&
                item.owningApplication.processID == NSProcessInfo.processInfo.processIdentifier) {
                selected = item;
                break;
            }
        }
        require(selected != nil, "ScreenCaptureKit could not select this NSWindow: " +
                                 qtString(result->failure.localizedDescription));
        filter_ = [[SCContentFilter alloc] initWithDesktopIndependentWindow:selected];
        configuration_ = [[SCStreamConfiguration alloc] init];
        configuration_.width = std::lround(NSWidth(window.frame) * filter_.pointPixelScale);
        configuration_.height = std::lround(NSHeight(window.frame) * filter_.pointPixelScale);
        configuration_.pixelFormat = kCVPixelFormatType_32BGRA;
        configuration_.colorSpaceName = kCGColorSpaceSRGB;
        configuration_.showsCursor = NO;
        configuration_.capturesAudio = NO;
        configuration_.captureMicrophone = NO;
        configuration_.includeChildWindows = NO;
        // Keep the full frame, excluding only the external compositor shadow.
        configuration_.ignoreShadowsSingleWindow = YES;
        configuration_.ignoreGlobalClipSingleWindow = YES;
    }

    void start(const QString& file) {
        require(!recording_, "Recording has already been started");
        const auto url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:qUtf8Printable(file)]];
        NSError* error = nil;
        const auto recording = std::make_shared<Recording>();
        recording->writer = [[AVAssetWriter alloc] initWithURL:url fileType:AVFileTypeMPEG4 error:&error];
        require(recording->writer != nil, "Cannot create MP4 writer: " + qtString(error.localizedDescription));
        NSDictionary* settings = @{
            AVVideoCodecKey: AVVideoCodecTypeH264,
            AVVideoWidthKey: @(configuration_.width),
            AVVideoHeightKey: @(configuration_.height),
            AVVideoCompressionPropertiesKey: @{
                AVVideoAverageBitRateKey: @12000000,
                AVVideoExpectedSourceFrameRateKey: @24,
                AVVideoAllowFrameReorderingKey: @NO
            }
        };
        recording->input = [AVAssetWriterInput assetWriterInputWithMediaType:AVMediaTypeVideo outputSettings:settings];
        recording->input.expectsMediaDataInRealTime = YES;
        recording->pixels = [AVAssetWriterInputPixelBufferAdaptor
            assetWriterInputPixelBufferAdaptorWithAssetWriterInput:recording->input sourcePixelBufferAttributes:nil];
        require([recording->writer canAddInput:recording->input], "Cannot add the MP4 video input");
        [recording->writer addInput:recording->input];
        require([recording->writer startWriting], "Could not start MP4: " +
                                                  qtString(recording->writer.error.localizedDescription));
        [recording->writer startSessionAtSourceTime:kCMTimeZero];
        recording_ = recording;

        // Feed live native pixel buffers into one encoder, never reading a PNG.
        // A persistent window-sharing stream replaces the native titlebar buttons.
        timer_.setTimerType(Qt::PreciseTimer);
        QObject::connect(&timer_, &QTimer::timeout, &timer_, [this] { requestVideoFrame(); });
        timer_.start(41);
        requestVideoFrame();
        require(waitFor([&] { return recording->frames || !recording->failure.isEmpty(); }),
                "Timed out waiting for the first native video buffer");
        checkRecording();
    }

    void stop() {
        const auto recording = recording_;
        if (!recording || recording->closing) return;
        timer_.stop();
        recording->closing = true;
        require(waitFor([&] { return !recording->pending; }), "Timed out draining the native video buffer");
        if (!recording->frames) {
            [recording->writer cancelWriting];
            checkRecording();
            throw std::runtime_error("No native video buffers were written");
        }
        const CMTime end = CMTimeSubtract(CMClockGetTime(CMClockGetHostTimeClock()), recording->start);
        recording->seconds = CMTimeGetSeconds(end);
        [recording->writer endSessionAtSourceTime:end];
        [recording->input markAsFinished];
        auto finished = std::make_shared<CaptureResult<NSObject*>>();
        [recording->writer finishWritingWithCompletionHandler:^{
            dispatch_async(dispatch_get_main_queue(), ^{ finished->finished = true; });
        }];
        require(waitFor([&] { return finished->finished; }, 12000), "Timed out finalizing the MP4");
        require(recording->writer.status == AVAssetWriterStatusCompleted, "MP4 writer: " +
                        qtString(recording->writer.error.localizedDescription));
        checkRecording();
    }

    void checkRecording() const {
        if (recording_) require(recording_->failure.isEmpty(), recording_->failure);
    }

    QImage still(const QString& file) const {
        checkRecording();
        auto result = std::make_shared<CaptureResult<NSData*>>();
        [SCScreenshotManager captureImageWithFilter:filter_ configuration:configuration_
            completionHandler:^(CGImageRef image, NSError* error) {
                NSData* data = nil;
                if (image) {
                    NSBitmapImageRep* bitmap = [[NSBitmapImageRep alloc] initWithCGImage:image];
                    data = [bitmap representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
                }
                dispatch_async(dispatch_get_main_queue(), ^{
                    result->payload = data;
                    result->failure = error;
                    result->finished = true;
                });
            }];
        require(waitFor([&] { return result->finished; }), "ScreenCaptureKit screenshot timed out");
        NSData* png = result->payload;
        require(png != nil, "ScreenCaptureKit screenshot: " + qtString(result->failure.localizedDescription));
        QFile output(file);
        const auto size = static_cast<qint64>(png.length);
        require(output.open(QIODevice::WriteOnly | QIODevice::NewOnly) &&
                    output.write(static_cast<const char*>(png.bytes), size) == size,
                "Cannot write raw screenshot: " + file);
        output.close();
        QImage decoded(file);
        require(!decoded.isNull(), "Cannot read written screenshot: " + file);
        return decoded;
    }

    double recordedSeconds() const { return recording_ ? recording_->seconds : 0; }
    int frameCount() const { return recording_ ? recording_->frames : 0; }
    int droppedFrames() const { return recording_ ? recording_->backpressureDrops : 0; }
    double longestGap() const { return recording_ ? recording_->longestGap : 0; }
    float scale() const { return filter_.pointPixelScale; }

private:
    void requestVideoFrame() {
        const auto recording = recording_;
        if (recording->pending || recording->closing || !recording->failure.isEmpty()) return;
        recording->pending = true;
        [SCScreenshotManager captureSampleBufferWithFilter:filter_ configuration:configuration_
            completionHandler:^(CMSampleBufferRef sample, NSError* error) {
                const CMTime captured = CMClockGetTime(CMClockGetHostTimeClock());
                auto pixels = sample ? CMSampleBufferGetImageBuffer(sample) : nullptr;
                if (pixels) CVPixelBufferRetain(pixels);
                dispatch_async(dispatch_get_main_queue(), ^{
                    recording->pending = false;
                    if (pixels) {
                        appendFrame(*recording, pixels, captured);
                        CVPixelBufferRelease(pixels);
                    } else {
                        recording->failure = "No native video buffer: " + qtString(error.localizedDescription);
                    }
                });
            }];
    }

    static void appendFrame(Recording& recording, CVPixelBufferRef buffer, CMTime captured) {
        if (!recording.input.readyForMoreMediaData) {
            ++recording.backpressureDrops;
            return;
        }
        if (!CMTIME_IS_VALID(recording.start)) recording.start = captured;
        const CMTime pts = CMTimeSubtract(captured, recording.start);
        if (![recording.pixels appendPixelBuffer:buffer withPresentationTime:pts]) {
            recording.failure = "Cannot append native video buffer: " +
                                qtString(recording.writer.error.localizedDescription);
            return;
        }
        if (recording.frames) {
            recording.longestGap = std::max(recording.longestGap,
                CMTimeGetSeconds(CMTimeSubtract(pts, recording.last)));
        }
        recording.last = pts;
        ++recording.frames;
    }

    SCContentFilter* filter_ = nil;
    SCStreamConfiguration* configuration_ = nil;
    QTimer timer_;
    std::shared_ptr<Recording> recording_;
};

} // namespace DesktopQA
