#import <CoreVideo/CoreVideo.h>
#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

typedef NS_ENUM(NSInteger, CrossDeskSignalState) {
  CrossDeskSignalStateConnecting = 0,
  CrossDeskSignalStateConnected,
  CrossDeskSignalStateFailed,
  CrossDeskSignalStateClosed,
  CrossDeskSignalStateReconnecting,
  CrossDeskSignalStateServerClosed,
  CrossDeskSignalStateTLSCertificateError,
};

typedef NS_ENUM(NSInteger, CrossDeskConnectionState) {
  CrossDeskConnectionStateConnecting = 0,
  CrossDeskConnectionStateConnected,
  CrossDeskConnectionStateGathering,
  CrossDeskConnectionStateDisconnected,
  CrossDeskConnectionStateFailed,
  CrossDeskConnectionStateClosed,
  CrossDeskConnectionStateIncorrectPassword,
  CrossDeskConnectionStateNoSuchID,
  CrossDeskConnectionStateRemoteUnavailable,
};

typedef NS_ENUM(NSInteger, CrossDeskPointerAction) {
  CrossDeskPointerActionMove = 0,
  CrossDeskPointerActionLeftDown,
  CrossDeskPointerActionLeftUp,
  CrossDeskPointerActionRightDown,
  CrossDeskPointerActionRightUp,
  CrossDeskPointerActionMiddleDown,
  CrossDeskPointerActionMiddleUp,
};

typedef NS_ENUM(NSInteger, CrossDeskVideoAdaptationPolicy) {
  CrossDeskVideoAdaptationPolicyFrameRatePriority = 0,
  CrossDeskVideoAdaptationPolicyQualityPriority,
  CrossDeskVideoAdaptationPolicyBalanced,
};

@class CrossDeskRTCBridge;

@protocol CrossDeskRTCBridgeDelegate <NSObject>
@optional
- (void)rtcBridge:(CrossDeskRTCBridge *)bridge
    didChangeSignalState:(CrossDeskSignalState)state;
- (void)rtcBridge:(CrossDeskRTCBridge *)bridge
    didChangeConnectionState:(CrossDeskConnectionState)state
                    remoteID:(NSString *)remoteID;
- (void)rtcBridge:(CrossDeskRTCBridge *)bridge
    didProvisionIdentity:(NSString *)identity;
- (void)rtcBridge:(CrossDeskRTCBridge *)bridge
    didReceivePixelBuffer:(CVPixelBufferRef)pixelBuffer
                    width:(NSInteger)width
                   height:(NSInteger)height;
- (void)rtcBridge:(CrossDeskRTCBridge *)bridge
    didReceiveHostName:(NSString *)hostName
          displayNames:(NSArray<NSString *> *)displayNames
          displaySizes:(NSArray<NSValue *> *)displaySizes;
- (void)rtcBridge:(CrossDeskRTCBridge *)bridge
    didReceivePresence:(NSDictionary<NSString *, NSNumber *> *)presence;
- (void)rtcBridge:(CrossDeskRTCBridge *)bridge
    didReceiveCursorVisible:(BOOL)visible
                      shape:(NSInteger)shape
             positionUpdate:(BOOL)positionUpdate
              positionValid:(BOOL)positionValid
                          x:(float)x
                          y:(float)y
              visualOffsetX:(float)visualOffsetX
              visualOffsetY:(float)visualOffsetY
               displayIndex:(NSInteger)displayIndex
                   sequence:(uint32_t)sequence;
- (void)rtcBridge:(CrossDeskRTCBridge *)bridge
    didReceiveAudioPCM:(NSData *)pcmData;
- (void)rtcBridge:(CrossDeskRTCBridge *)bridge
    didReceiveClipboardText:(NSString *)text;
- (void)rtcBridge:(CrossDeskRTCBridge *)bridge
    didUpdateFileTransfer:(NSString *)fileName
                 progress:(double)progress
                  sending:(BOOL)sending;
- (void)rtcBridge:(CrossDeskRTCBridge *)bridge
    didReceiveFileAtURL:(NSURL *)fileURL;
- (void)rtcBridge:(CrossDeskRTCBridge *)bridge
    didUpdateBitrate:(NSUInteger)bitsPerSecond
            lossRate:(float)lossRate
           usingTURN:(BOOL)usingTURN;
@end

/// Objective-C++ boundary around the native MiniRTC C API.
///
/// All MiniRTC ownership and calls are serialized internally. Delegate methods
/// are delivered on the main queue so SwiftUI never touches callback threads.
@interface CrossDeskRTCBridge : NSObject

@property(nonatomic, weak, nullable) id<CrossDeskRTCBridgeDelegate> delegate;

- (void)configureWithSignalHost:(NSString *)host
                     signalPort:(NSInteger)signalPort
                       turnPort:(NSInteger)turnPort
                     enableSRTP:(BOOL)enableSRTP;
- (void)setHardwareAccelerationEnabled:(BOOL)enabled;
- (void)setVideoAdaptationPolicy:(CrossDeskVideoAdaptationPolicy)policy;
- (void)requestPresenceForRemoteIDs:(NSArray<NSString *> *)remoteIDs
    NS_SWIFT_NAME(requestPresence(remoteIDs:));

- (void)connectToRemoteID:(NSString *)remoteID password:(NSString *)password;
- (void)disconnect;

- (void)sendPointerAtX:(float)x
                     y:(float)y
                action:(CrossDeskPointerAction)action
    NS_SWIFT_NAME(sendPointer(x:y:action:));
- (void)sendScrollX:(float)x y:(float)y deltaX:(NSInteger)deltaX
              deltaY:(NSInteger)deltaY;
- (void)sendWindowsKeyCode:(NSUInteger)keyCode isDown:(BOOL)isDown;
- (void)sendText:(NSString *)text;
- (void)switchToDisplay:(NSInteger)displayIndex;
- (void)setAudioEnabled:(BOOL)enabled;
- (void)sendClipboardText:(NSString *)text;
- (void)sendFileAtURL:(NSURL *)fileURL;
- (void)sendSecureAttentionSequence;

@end

NS_ASSUME_NONNULL_END
