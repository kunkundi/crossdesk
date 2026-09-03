#import "CrossDeskRTCBridge.h"

#import <CoreMedia/CoreMedia.h>
#import <UIKit/UIKit.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <display_stream_id.h>
#include <file_transfer_format.h>
#include <remote_action.h>
#include <stream_names.h>
#include "minirtc.h"

namespace {

using crossdesk::ControlType;
using crossdesk::FileTransferAck;
using crossdesk::KeyFlag;
using crossdesk::MouseFlag;
using crossdesk::RemoteAction;
using crossdesk::ServiceCommandFlag;
using crossdesk::kFileAckMagic;
using crossdesk::kAudioStream;
using crossdesk::kClipboardStream;
using crossdesk::kControlStream;
using crossdesk::kDataStream;
using crossdesk::kFileChunkSize;
using crossdesk::kFileFeedbackStream;
using crossdesk::kFileStream;
using crossdesk::kKeyboardStream;
using crossdesk::kMaxClipboardBytes;
using crossdesk::kMouseStream;

struct IncomingFile {
  std::string name;
  std::string path;
  uint64_t total_size = 0;
  uint64_t received = 0;
  FILE *handle = nullptr;
};

struct OutgoingFile {
  std::string name;
  uint64_t total_size = 0;
};

enum class PeerRole { Identity, Controller };

struct CallbackContext {
  __weak CrossDeskRTCBridge *owner = nil;
  PeerRole role = PeerRole::Identity;
  uint64_t generation = 0;
};

struct RTCState {
  PeerPtr *identity_peer = nullptr;
  PeerPtr *controller_peer = nullptr;
  CallbackContext identity_context;
  CallbackContext controller_context;
  std::string signal_host;
  int signal_port = 0;
  int turn_port = 0;
  bool enable_srtp = false;
  bool hardware_acceleration = true;
  VideoDegradationPreference video_adaptation_policy =
      VideoDegradationPreference::MaintainResolution;
  bool identity_ready = false;
  bool identity_recovery_attempted = false;
  std::string identity_with_password;
  std::string identity_base;
  std::string controller_login;
  std::string pending_remote_id;
  std::string pending_password;
  std::string transmission_id;
  std::string log_path;
  std::unordered_map<uint32_t, IncomingFile> incoming_files;
  std::unordered_map<uint32_t, OutgoingFile> outgoing_files;
};

std::atomic<uint32_t> g_next_file_id{1};
char kRTCQueueSpecificKey;

bool IsReusableIdentity(const std::string &identity) {
  const size_t separator = identity.find('@');
  return separator != std::string::npos && separator > 0 &&
         separator + 1 < identity.size();
}

void CopyCString(char *destination, size_t capacity,
                 const std::string &source) {
  if (!destination || capacity == 0) return;
  std::strncpy(destination, source.c_str(), capacity - 1);
  destination[capacity - 1] = '\0';
}

std::string BaseIdentity(const std::string &identity) {
  const size_t separator = identity.find('@');
  return separator == std::string::npos ? identity
                                        : identity.substr(0, separator);
}

std::string MouseJSON(float x, float y, int wheel, int flag) {
  RemoteAction action{};
  action.type = ControlType::mouse;
  action.m = {std::clamp(x, 0.0f, 1.0f), std::clamp(y, 0.0f, 1.0f),
              wheel, static_cast<MouseFlag>(flag)};
  return action.to_json();
}

std::string KeyJSON(NSUInteger key_code, bool is_down) {
  RemoteAction action{};
  action.type = ControlType::keyboard;
  action.k = {static_cast<std::size_t>(key_code), 0, false,
              is_down ? KeyFlag::key_down : KeyFlag::key_up};
  return action.to_json();
}

struct KeyMapping {
  NSUInteger key_code = 0;
  bool shift = false;
  bool valid = false;
};

KeyMapping MapASCII(unsigned char character) {
  if (character >= 'a' && character <= 'z') {
    return {static_cast<NSUInteger>(character - 'a' + 'A'), false, true};
  }
  if (character >= 'A' && character <= 'Z') {
    return {static_cast<NSUInteger>(character), true, true};
  }
  if (character >= '0' && character <= '9') {
    return {static_cast<NSUInteger>(character), false, true};
  }
  switch (character) {
    case ' ': return {0x20, false, true};
    case '\n': return {0x0D, false, true};
    case '\t': return {0x09, false, true};
    case '-': return {0xBD, false, true};
    case '_': return {0xBD, true, true};
    case '=': return {0xBB, false, true};
    case '+': return {0xBB, true, true};
    case '[': return {0xDB, false, true};
    case '{': return {0xDB, true, true};
    case ']': return {0xDD, false, true};
    case '}': return {0xDD, true, true};
    case '\\': return {0xDC, false, true};
    case '|': return {0xDC, true, true};
    case ';': return {0xBA, false, true};
    case ':': return {0xBA, true, true};
    case '\'': return {0xDE, false, true};
    case '"': return {0xDE, true, true};
    case ',': return {0xBC, false, true};
    case '<': return {0xBC, true, true};
    case '.': return {0xBE, false, true};
    case '>': return {0xBE, true, true};
    case '/': return {0xBF, false, true};
    case '?': return {0xBF, true, true};
    case '`': return {0xC0, false, true};
    case '~': return {0xC0, true, true};
    case '!': return {'1', true, true};
    case '@': return {'2', true, true};
    case '#': return {'3', true, true};
    case '$': return {'4', true, true};
    case '%': return {'5', true, true};
    case '^': return {'6', true, true};
    case '&': return {'7', true, true};
    case '*': return {'8', true, true};
    case '(': return {'9', true, true};
    case ')': return {'0', true, true};
    default: return {};
  }
}

void DispatchMain(dispatch_block_t block) {
  if ([NSThread isMainThread]) {
    block();
  } else {
    dispatch_async(dispatch_get_main_queue(), block);
  }
}

}  // namespace

@interface CrossDeskRTCBridge ()
- (void)handleSignalState:(CrossDeskSignalState)state role:(PeerRole)role;
- (void)handleSignalMessage:(const char *)message
                       size:(size_t)size
                       role:(PeerRole)role;
- (void)handleProvisionedIdentity:(const std::string &)identity;
- (void)handleConnectionState:(CrossDeskConnectionState)state
                      remoteID:(const std::string &)remoteID
                    generation:(uint64_t)generation;
- (BOOL)isControllerGenerationActive:(uint64_t)generation;
- (BOOL)isControllerGenerationCurrent:(uint64_t)generation;
- (void)handleVideoFrame:(const XVideoFrame *)frame
                sourceID:(const char *)sourceID
            sourceIDSize:(size_t)sourceIDSize;
- (void)enqueueVideoPixelBuffer:(CVPixelBufferRef)pixelBuffer
                           width:(NSInteger)width
                          height:(NSInteger)height;
- (void)resetVideoDelivery;
- (void)handleAudio:(const char *)data size:(size_t)size;
- (void)handleData:(const char *)data
              size:(size_t)size
          sourceID:(const char *)sourceID
      sourceIDSize:(size_t)sourceIDSize;
- (void)handleStats:(const XNetTrafficStats *)stats mode:(TraversalMode)mode;
- (void)sendMessage:(const std::string &)message
            reliable:(BOOL)reliable
              stream:(const char *)stream;
- (void)createIdentityPeer;
- (void)createControllerPeer;
- (void)destroyIdentityPeer;
- (void)destroyControllerPeer;
- (void)requestKeyFrame;
@end

namespace {

void OnVideoFrame(const XVideoFrame *frame, const char *, size_t,
                  const char *source_id, size_t source_id_size,
                  void *user_data) {
  auto *context = static_cast<CallbackContext *>(user_data);
  CrossDeskRTCBridge *owner = context ? context->owner : nil;
  if (owner && context->role == PeerRole::Controller && frame &&
      [owner isControllerGenerationActive:context->generation]) {
    [owner handleVideoFrame:frame
                   sourceID:source_id
               sourceIDSize:source_id_size];
  }
}

void OnAudioBuffer(const char *data, size_t size, const char *, size_t,
                   const char *, size_t, void *user_data) {
  auto *context = static_cast<CallbackContext *>(user_data);
  CrossDeskRTCBridge *owner = context ? context->owner : nil;
  if (owner && context->role == PeerRole::Controller && data && size > 0 &&
      [owner isControllerGenerationActive:context->generation]) {
    [owner handleAudio:data size:size];
  }
}

void OnDataBuffer(const char *data, size_t size, const char *, size_t,
                  const char *source_id, size_t source_id_size,
                  void *user_data) {
  auto *context = static_cast<CallbackContext *>(user_data);
  CrossDeskRTCBridge *owner = context ? context->owner : nil;
  if (owner && context->role == PeerRole::Controller &&
      [owner isControllerGenerationActive:context->generation]) {
    [owner handleData:data
                 size:size
             sourceID:source_id
         sourceIDSize:source_id_size];
  }
}

void OnSignalState(SignalStatus status, const char *, size_t, void *user_data) {
  auto *context = static_cast<CallbackContext *>(user_data);
  CrossDeskRTCBridge *owner = context ? context->owner : nil;
  if (owner &&
      (context->role == PeerRole::Identity ||
       [owner isControllerGenerationActive:context->generation])) {
    [owner handleSignalState:static_cast<CrossDeskSignalState>(status)
                        role:context->role];
  }
}

void OnSignalMessage(const char *message, size_t size, void *user_data) {
  auto *context = static_cast<CallbackContext *>(user_data);
  CrossDeskRTCBridge *owner = context ? context->owner : nil;
  if (owner && message && size > 0) {
    [owner handleSignalMessage:message size:size role:context->role];
  }
}

void OnConnectionState(ConnectionStatus status, const char *remote_id,
                       size_t remote_id_size, void *user_data) {
  auto *context = static_cast<CallbackContext *>(user_data);
  CrossDeskRTCBridge *owner = context ? context->owner : nil;
  if (!owner || context->role != PeerRole::Controller ||
      ![owner isControllerGenerationActive:context->generation]) {
    return;
  }
  const std::string identifier(remote_id ? remote_id : "", remote_id_size);
  [owner handleConnectionState:static_cast<CrossDeskConnectionState>(status)
                       remoteID:identifier
                     generation:context->generation];
}

void OnNetworkStats(const char *peer_id, size_t peer_id_size,
                    TraversalMode mode, const XNetTrafficStats *stats,
                    const char *, size_t, void *user_data) {
  auto *context = static_cast<CallbackContext *>(user_data);
  CrossDeskRTCBridge *owner = context ? context->owner : nil;
  if (!owner) return;
  if (context->role == PeerRole::Identity && mode == TraversalMode::UnknownMode &&
      peer_id && peer_id_size > 0) {
    [owner handleProvisionedIdentity:std::string(peer_id, peer_id_size)];
  } else if (context->role == PeerRole::Controller && stats &&
             [owner isControllerGenerationActive:context->generation]) {
    [owner handleStats:stats mode:mode];
  }
}

Params MakeParams(const RTCState &state, const std::string &user_id,
                  CallbackContext *context) {
  Params params{};
  params.use_cfg_file = false;
  CopyCString(params.signal_server_ip, sizeof(params.signal_server_ip),
              state.signal_host);
  params.signal_server_port = state.signal_port;
  CopyCString(params.stun_server_ip, sizeof(params.stun_server_ip),
              state.signal_host);
  params.stun_server_port = state.turn_port;
  CopyCString(params.turn_server_ip, sizeof(params.turn_server_ip),
              state.signal_host);
  params.turn_server_port = state.turn_port;
  params.turn_server_username[0] = '\0';
  params.turn_server_password[0] = '\0';
  CopyCString(params.log_path, sizeof(params.log_path), state.log_path);
  params.hardware_acceleration = state.hardware_acceleration;
  params.native_video_output = true;
  params.av1_encoding = false;
  params.turn_mode = TurnMode::TurnAutoUdpTcp;
  params.enable_srtp = state.enable_srtp;
  params.video_content_type = VideoContentType::ScreenContent;
  params.video_quality = VideoQuality::QualityHigh;
  params.video_frame_rate = 60;
  params.video_degradation_preference = state.video_adaptation_policy;
  params.on_receive_video_buffer = nullptr;
  params.on_receive_audio_buffer = OnAudioBuffer;
  params.on_receive_data_buffer = OnDataBuffer;
  params.on_receive_video_frame = OnVideoFrame;
  params.on_signal_status = OnSignalState;
  params.on_signal_message = OnSignalMessage;
  params.on_connection_status = OnConnectionState;
  params.on_net_status_report = OnNetworkStats;
  params.user_id = user_id.c_str();
  params.user_data = context;
  return params;
}

}  // namespace

@implementation CrossDeskRTCBridge {
  std::unique_ptr<RTCState> _state;
  dispatch_queue_t _rtcQueue;
  CVPixelBufferPoolRef _videoPool;
  size_t _videoPoolWidth;
  size_t _videoPoolHeight;
  std::mutex _videoPoolMutex;
  std::mutex _pendingVideoMutex;
  CVPixelBufferRef _pendingVideoBuffer;
  NSInteger _pendingVideoWidth;
  NSInteger _pendingVideoHeight;
  bool _videoDeliveryScheduled;
  uint64_t _videoDeliveryGeneration;
  std::mutex _fileMutex;
  std::atomic<int> _selectedDisplay;
  std::atomic<uint64_t> _receivedVideoFrames;
  std::atomic<uint64_t> _controllerGenerationCounter;
  std::atomic<uint64_t> _activeControllerGeneration;
}

- (instancetype)init {
  self = [super init];
  if (self) {
    _state = std::make_unique<RTCState>();
    _state->identity_context.owner = self;
    _state->identity_context.role = PeerRole::Identity;
    _state->controller_context.owner = self;
    _state->controller_context.role = PeerRole::Controller;
    _rtcQueue = dispatch_queue_create("cn.crossdesk.mobile.minirtc",
                                      DISPATCH_QUEUE_SERIAL);
    dispatch_queue_set_specific(_rtcQueue, &kRTCQueueSpecificKey,
                                _state.get(), nullptr);
    _videoPool = nullptr;
    _videoPoolWidth = 0;
    _videoPoolHeight = 0;
    _pendingVideoBuffer = nullptr;
    _pendingVideoWidth = 0;
    _pendingVideoHeight = 0;
    _videoDeliveryScheduled = false;
    _videoDeliveryGeneration = 0;
    _selectedDisplay.store(0);
    _receivedVideoFrames.store(0);
    _controllerGenerationCounter.store(0);
    _activeControllerGeneration.store(0);
  }
  return self;
}

- (void)dealloc {
  _activeControllerGeneration.store(0);
  _state->identity_context.owner = nil;
  _state->controller_context.owner = nil;
  if (dispatch_get_specific(&kRTCQueueSpecificKey) == _state.get()) {
    [self destroyControllerPeer];
    [self destroyIdentityPeer];
  } else {
    dispatch_sync(_rtcQueue, ^{
      [self destroyControllerPeer];
      [self destroyIdentityPeer];
    });
  }
  [self resetVideoDelivery];
  std::lock_guard<std::mutex> lock(_videoPoolMutex);
  if (_videoPool) {
    CVPixelBufferPoolRelease(_videoPool);
    _videoPool = nullptr;
  }
}

- (void)configureWithSignalHost:(NSString *)host
                     signalPort:(NSInteger)signalPort
                       turnPort:(NSInteger)turnPort
                     enableSRTP:(BOOL)enableSRTP {
  NSString *trimmed = [host stringByTrimmingCharactersInSet:
                                NSCharacterSet.whitespaceAndNewlineCharacterSet];
  const char *host_c_string = trimmed.UTF8String;
  const std::string host_value = host_c_string ? host_c_string : "";
  if (host_value.empty() || signalPort <= 0 || turnPort <= 0) return;

  NSString *cache = NSSearchPathForDirectoriesInDomains(
                        NSCachesDirectory, NSUserDomainMask, YES)
                        .firstObject;
  NSString *logDirectory = [cache stringByAppendingPathComponent:
                                     @"CrossDeskMobile/MiniRTC"];
  [[NSFileManager defaultManager] createDirectoryAtPath:logDirectory
                           withIntermediateDirectories:YES
                                            attributes:nil
                                                 error:nil];
  const char *log_c_string = logDirectory.UTF8String;
  const std::string log_path = log_c_string ? log_c_string : "logs";

  dispatch_async(_rtcQueue, ^{
    const bool unchanged = self->_state->signal_host == host_value &&
                           self->_state->signal_port == signalPort &&
                           self->_state->turn_port == turnPort &&
                           self->_state->enable_srtp == enableSRTP &&
                           self->_state->identity_peer != nullptr;
    if (unchanged) return;

    [self destroyControllerPeer];
    [self destroyIdentityPeer];
    self->_state->signal_host = host_value;
    self->_state->signal_port = static_cast<int>(signalPort);
    self->_state->turn_port = static_cast<int>(turnPort);
    self->_state->enable_srtp = enableSRTP;
    self->_state->log_path = log_path;
    self->_state->identity_ready = false;
    self->_state->identity_recovery_attempted = false;
    self->_state->identity_with_password.clear();
    self->_state->identity_base.clear();

    NSString *key = [NSString stringWithFormat:@"CrossDeskIdentity.%@.%ld",
                                                trimmed, (long)signalPort];
    NSString *cached = [[NSUserDefaults standardUserDefaults]
        stringForKey:key];
    if (cached.length > 0) {
      const std::string cached_identity = cached.UTF8String ?: "";
      if (IsReusableIdentity(cached_identity)) {
        self->_state->identity_with_password = cached_identity;
        self->_state->identity_base = BaseIdentity(cached_identity);
        DispatchMain(^{
          id<CrossDeskRTCBridgeDelegate> delegate = self.delegate;
          if ([delegate respondsToSelector:
                  @selector(rtcBridge:didProvisionIdentity:)]) {
            [delegate rtcBridge:self didProvisionIdentity:cached];
          }
        });
      } else {
        // A bare ID cannot authenticate after its first signaling session.
        // Older iOS builds stored it anyway, causing every later launch to be
        // rejected as "Incorrect password". Drop it and request a fresh
        // server-issued ID/password pair.
        [[NSUserDefaults standardUserDefaults] removeObjectForKey:key];
      }
    }
    [self createIdentityPeer];
  });
}

- (void)setHardwareAccelerationEnabled:(BOOL)enabled {
  dispatch_async(_rtcQueue, ^{
    self->_state->hardware_acceleration = enabled;
  });
}

- (void)setVideoAdaptationPolicy:(CrossDeskVideoAdaptationPolicy)policy {
  dispatch_async(_rtcQueue, ^{
    switch (policy) {
      case CrossDeskVideoAdaptationPolicyFrameRatePriority:
        self->_state->video_adaptation_policy =
            VideoDegradationPreference::MaintainFrameRate;
        break;
      case CrossDeskVideoAdaptationPolicyBalanced:
        self->_state->video_adaptation_policy =
            VideoDegradationPreference::Balanced;
        break;
      case CrossDeskVideoAdaptationPolicyQualityPriority:
      default:
        self->_state->video_adaptation_policy =
            VideoDegradationPreference::MaintainResolution;
        break;
    }
  });
}

- (void)requestPresenceForRemoteIDs:(NSArray<NSString *> *)remoteIDs {
  NSArray<NSString *> *owned_ids = [remoteIDs copy];
  dispatch_async(_rtcQueue, ^{
    if (!self->_state->identity_peer || !self->_state->identity_ready ||
        self->_state->identity_base.empty()) {
      return;
    }

    NSMutableArray<NSString *> *devices =
        [NSMutableArray arrayWithCapacity:owned_ids.count];
    for (NSString *remote_id in owned_ids) {
      if (![remote_id isKindOfClass:NSString.class]) continue;
      NSString *trimmed = [remote_id
          stringByTrimmingCharactersInSet:
              NSCharacterSet.whitespaceAndNewlineCharacterSet];
      if (trimmed.length > 0) [devices addObject:trimmed];
    }
    NSString *user_id = [NSString
        stringWithUTF8String:self->_state->identity_base.c_str()];
    NSDictionary *request = @{
      @"type" : @"recent_connections_presence",
      @"user_id" : user_id ?: @"",
      @"devices" : devices,
    };
    NSData *payload = [NSJSONSerialization dataWithJSONObject:request
                                                      options:0
                                                        error:nil];
    if (!payload) return;
    SendSignalMessage(self->_state->identity_peer,
                      static_cast<const char *>(payload.bytes),
                      payload.length);
  });
}

- (void)connectToRemoteID:(NSString *)remoteID password:(NSString *)password {
  NSString *cleanID = [remoteID
      stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceAndNewlineCharacterSet];
  NSString *cleanPassword = [password
      stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceAndNewlineCharacterSet];
  const char *identifier_c_string = cleanID.UTF8String;
  const char *password_c_string = cleanPassword.UTF8String;
  const std::string identifier =
      identifier_c_string ? identifier_c_string : "";
  const std::string password_value =
      password_c_string ? password_c_string : "";
  if (identifier.empty()) return;

  dispatch_async(_rtcQueue, ^{
    self->_state->pending_remote_id = identifier;
    self->_state->pending_password = password_value;
    [self destroyControllerPeer];
    if (self->_state->identity_ready &&
        !self->_state->identity_base.empty()) {
      [self createControllerPeer];
    }
  });
}

- (void)disconnect {
  dispatch_async(_rtcQueue, ^{
    self->_state->pending_remote_id.clear();
    self->_state->pending_password.clear();
    [self destroyControllerPeer];
  });
}

- (void)requestKeyFrame {
  dispatch_async(_rtcQueue, ^{
    if (self->_state->controller_peer) {
      const std::string stream = crossdesk::MakeDisplayStreamId(
          static_cast<size_t>(self->_selectedDisplay.load()));
      RequestVideoKeyFrame(self->_state->controller_peer, stream.c_str());
    }
  });
}

- (void)sendPointerAtX:(float)x
                     y:(float)y
                action:(CrossDeskPointerAction)action {
  const std::string message = MouseJSON(x, y, 0, static_cast<int>(action));
  // Movement is latency-sensitive and can be superseded by newer positions.
  // Button transitions must not be lost or reordered, otherwise a tap can be
  // applied at a stale pointer position or leave a button logically pressed.
  // MiniRTC reliability is configured per stream, so transitions use the
  // existing reliable control stream while movement stays on the mouse stream.
  const BOOL is_movement = action == CrossDeskPointerActionMove;
#if DEBUG
  if (!is_movement) {
    NSLog(@"CrossDesk pointer action=%ld normalized=(%.5f, %.5f)",
          static_cast<long>(action), x, y);
  }
#endif
  [self sendMessage:message
           reliable:!is_movement
             stream:is_movement ? kMouseStream : kControlStream];
}

- (void)sendScrollX:(float)x
                   y:(float)y
              deltaX:(NSInteger)deltaX
              deltaY:(NSInteger)deltaY {
  const bool vertical = std::abs(deltaY) >= std::abs(deltaX);
  const NSInteger raw_delta = vertical ? deltaY : deltaX;
  if (raw_delta == 0) return;
  const int flag = vertical ? 7 : 8;
  const int wheel = raw_delta > 0 ? 1 : -1;
  const std::string message = MouseJSON(x, y, wheel, flag);
  [self sendMessage:message reliable:NO stream:kMouseStream];
}

- (void)sendWindowsKeyCode:(NSUInteger)keyCode isDown:(BOOL)isDown {
  [self sendMessage:KeyJSON(keyCode, isDown)
           reliable:YES
             stream:kKeyboardStream];
}

- (void)sendText:(NSString *)text {
  NSData *utf8 = [text dataUsingEncoding:NSUTF8StringEncoding];
  if (!utf8.length) return;
  const std::string bytes(static_cast<const char *>(utf8.bytes), utf8.length);
  dispatch_async(_rtcQueue, ^{
    if (!self->_state->controller_peer) return;
    for (unsigned char character : bytes) {
      KeyMapping mapping = MapASCII(character);
      if (!mapping.valid) continue;
      if (mapping.shift) {
        const std::string shift_down = KeyJSON(0x10, true);
        SendReliableDataFrame(self->_state->controller_peer,
                              shift_down.data(), shift_down.size(),
                              kKeyboardStream);
      }
      const std::string key_down = KeyJSON(mapping.key_code, true);
      const std::string key_up = KeyJSON(mapping.key_code, false);
      SendReliableDataFrame(self->_state->controller_peer, key_down.data(),
                            key_down.size(), kKeyboardStream);
      SendReliableDataFrame(self->_state->controller_peer, key_up.data(),
                            key_up.size(), kKeyboardStream);
      if (mapping.shift) {
        const std::string shift_up = KeyJSON(0x10, false);
        SendReliableDataFrame(self->_state->controller_peer, shift_up.data(),
                              shift_up.size(), kKeyboardStream);
      }
    }
  });
}

- (void)switchToDisplay:(NSInteger)displayIndex {
  if (displayIndex < 0 || displayIndex >= 8) return;
  [self resetVideoDelivery];
  _selectedDisplay.store(static_cast<int>(displayIndex));
  _receivedVideoFrames.store(0);
  RemoteAction action{};
  action.type = ControlType::display_id;
  action.d = static_cast<int>(displayIndex);
  [self sendMessage:action.to_json() reliable:YES stream:kControlStream];
  [self requestKeyFrame];
}

- (void)setAudioEnabled:(BOOL)enabled {
  RemoteAction action{};
  action.type = ControlType::audio_capture;
  action.a = enabled;
  [self sendMessage:action.to_json() reliable:YES stream:kControlStream];
}

- (void)sendClipboardText:(NSString *)text {
  NSData *utf8 = [text dataUsingEncoding:NSUTF8StringEncoding];
  if (!utf8.length || utf8.length > kMaxClipboardBytes) return;
  const std::string bytes(static_cast<const char *>(utf8.bytes), utf8.length);
  [self sendMessage:bytes reliable:YES stream:kClipboardStream];
}

- (void)sendFileAtURL:(NSURL *)fileURL {
  if (!fileURL.isFileURL) return;
  NSURL *url = [fileURL copy];
  // Acquire the document picker's sandbox extension before its completion
  // callback returns; the actual reads happen on the serialized RTC queue.
  const BOOL scoped = [url startAccessingSecurityScopedResource];
  dispatch_async(_rtcQueue, ^{
    if (!self->_state->controller_peer) {
      if (scoped) [url stopAccessingSecurityScopedResource];
      return;
    }
    const char *path_bytes = url.path.fileSystemRepresentation;
    FILE *input = path_bytes ? std::fopen(path_bytes, "rb") : nullptr;
    if (!input) {
      if (scoped) [url stopAccessingSecurityScopedResource];
      return;
    }

    if (fseeko(input, 0, SEEK_END) != 0) {
      std::fclose(input);
      if (scoped) [url stopAccessingSecurityScopedResource];
      return;
    }
    const off_t end = ftello(input);
    rewind(input);
    if (end < 0) {
      std::fclose(input);
      if (scoped) [url stopAccessingSecurityScopedResource];
      return;
    }

    NSString *last_component = url.lastPathComponent.length > 0
        ? url.lastPathComponent
        : @"file";
    NSData *name_data = [last_component dataUsingEncoding:NSUTF8StringEncoding];
    if (name_data.length == 0 ||
        name_data.length > std::numeric_limits<uint16_t>::max()) {
      std::fclose(input);
      if (scoped) [url stopAccessingSecurityScopedResource];
      return;
    }

    const uint32_t file_id = g_next_file_id.fetch_add(1);
    const uint64_t total_size = static_cast<uint64_t>(end);
    {
      std::lock_guard<std::mutex> lock(self->_fileMutex);
      self->_state->outgoing_files[file_id] = {
          std::string(last_component.UTF8String ?: "file"), total_size};
    }
    DispatchMain(^{
      id<CrossDeskRTCBridgeDelegate> delegate = self.delegate;
      if ([delegate respondsToSelector:
              @selector(rtcBridge:didUpdateFileTransfer:progress:sending:)]) {
        [delegate rtcBridge:self
            didUpdateFileTransfer:last_component
                         progress:0
                          sending:YES];
      }
    });

    std::vector<char> payload(kFileChunkSize);
    uint64_t offset = 0;
    bool first = true;
    int send_result = 0;
    do {
      const size_t to_read = static_cast<size_t>(
          std::min<uint64_t>(kFileChunkSize, total_size - offset));
      const size_t bytes_read = to_read > 0
          ? std::fread(payload.data(), 1, to_read, input)
          : 0;
      if (to_read > 0 && bytes_read == 0) {
        send_result = -1;
        break;
      }
      const bool last = offset + bytes_read >= total_size;
      const std::string file_name(last_component.UTF8String ?: "file");
      const std::string* file_name_pointer = first ? &file_name : nullptr;
      std::vector<char> chunk = crossdesk::EncodeFileChunk(
          file_id, offset, total_size, payload.data(),
          static_cast<uint32_t>(bytes_read), file_name_pointer, first, last);
      if (chunk.empty()) {
        send_result = -1;
        break;
      }
      send_result = SendReliableDataFrame(self->_state->controller_peer,
                                          chunk.data(), chunk.size(),
                                          kFileStream);
      offset += bytes_read;
      first = false;
      if (send_result != 0) break;
    } while (offset < total_size);

    std::fclose(input);
    if (scoped) [url stopAccessingSecurityScopedResource];
    if (send_result != 0) {
      {
        std::lock_guard<std::mutex> lock(self->_fileMutex);
        self->_state->outgoing_files.erase(file_id);
      }
      DispatchMain(^{
        id<CrossDeskRTCBridgeDelegate> delegate = self.delegate;
        if ([delegate respondsToSelector:
                @selector(rtcBridge:didUpdateFileTransfer:progress:sending:)]) {
          [delegate rtcBridge:self
              didUpdateFileTransfer:last_component
                           progress:-1
                            sending:YES];
        }
      });
    }
  });
}

- (void)sendSecureAttentionSequence {
  RemoteAction action{};
  action.type = ControlType::service_command;
  action.c.flag = ServiceCommandFlag::send_sas;
  [self sendMessage:action.to_json() reliable:YES stream:kControlStream];
}

- (void)sendMessage:(const std::string &)message
            reliable:(BOOL)reliable
              stream:(const char *)stream {
  // This method always hops to the RTC queue. Own both values before the
  // caller's stack frame disappears; several callers pass local/temporary
  // std::strings.
  const std::string owned_message(message);
  const std::string owned_stream(stream ? stream : "");
  dispatch_async(_rtcQueue, ^{
    if (!self->_state->controller_peer) return;
    if (reliable) {
      SendReliableDataFrame(self->_state->controller_peer,
                            owned_message.data(), owned_message.size(),
                            owned_stream.c_str());
    } else {
      SendDataFrame(self->_state->controller_peer, owned_message.data(),
                    owned_message.size(), owned_stream.c_str());
    }
  });
}

- (void)createIdentityPeer {
  if (_state->identity_peer || _state->signal_host.empty()) return;
  Params params = MakeParams(*_state, _state->identity_with_password,
                             &_state->identity_context);
  _state->identity_peer = CreatePeer(&params);
  if (!_state->identity_peer || Init(_state->identity_peer) != 0) {
    [self destroyIdentityPeer];
    [self handleSignalState:CrossDeskSignalStateFailed
                       role:PeerRole::Identity];
  }
}

- (void)createControllerPeer {
  if (_state->controller_peer || _state->pending_remote_id.empty() ||
      _state->identity_base.empty()) {
    return;
  }
  const uint64_t generation = _controllerGenerationCounter.fetch_add(1) + 1;
  _state->controller_context.generation = generation;
  _activeControllerGeneration.store(generation);
  _state->controller_login = "C-" + _state->identity_base;
  Params params = MakeParams(*_state, _state->controller_login,
                             &_state->controller_context);
  _state->controller_peer = CreatePeer(&params);
  if (!_state->controller_peer) {
    _activeControllerGeneration.store(0);
    [self handleConnectionState:CrossDeskConnectionStateFailed
                        remoteID:_state->pending_remote_id
                      generation:generation];
    return;
  }

  // This native iOS peer is controller-only and does not publish a local
  // display. Remote display receivers are created from the host's SDP.
  AddAudioStream(_state->controller_peer, kAudioStream);
  AddDataStream(_state->controller_peer, kDataStream, false);
  AddDataStream(_state->controller_peer, kMouseStream, false);
  AddDataStream(_state->controller_peer, kKeyboardStream, true);
  AddDataStream(_state->controller_peer, kControlStream, true);
  AddDataStream(_state->controller_peer, kFileStream, true);
  AddDataStream(_state->controller_peer, kFileFeedbackStream, true);
  AddDataStream(_state->controller_peer, kClipboardStream, true);

  if (Init(_state->controller_peer) != 0) {
    [self destroyControllerPeer];
    [self handleConnectionState:CrossDeskConnectionStateFailed
                        remoteID:_state->pending_remote_id
                      generation:generation];
    return;
  }
  const std::string join_id = _state->pending_remote_id + "@" +
                              _state->pending_password;
  JoinConnection(_state->controller_peer, join_id.c_str());

  // Joining authenticates with "remote-id@password", while leaving is routed
  // by the remote peer ID alone (the same contract used by the desktop app).
  _state->transmission_id = _state->pending_remote_id;
}

- (void)destroyIdentityPeer {
  if (_state->identity_peer) {
    DestroyPeer(&_state->identity_peer);
  }
  _state->identity_ready = false;
}

- (void)destroyControllerPeer {
  _activeControllerGeneration.store(0);
  if (_state->controller_peer) {
    if (!_state->transmission_id.empty()) {
      LeaveConnection(_state->controller_peer,
                      _state->transmission_id.c_str());
    }
    DestroyPeer(&_state->controller_peer);
  }
  _state->controller_login.clear();
  _state->transmission_id.clear();
  {
    std::lock_guard<std::mutex> lock(_fileMutex);
    for (auto &entry : _state->incoming_files) {
      if (entry.second.handle) std::fclose(entry.second.handle);
    }
    _state->incoming_files.clear();
    _state->outgoing_files.clear();
  }
  _selectedDisplay.store(0);
  _receivedVideoFrames.store(0);
  [self resetVideoDelivery];
}

- (void)handleSignalState:(CrossDeskSignalState)state role:(PeerRole)role {
  dispatch_async(_rtcQueue, ^{
    if (role == PeerRole::Identity) {
      self->_state->identity_ready = state == CrossDeskSignalStateConnected;
      if (state == CrossDeskSignalStateFailed &&
          !self->_state->identity_with_password.empty() &&
          !self->_state->identity_recovery_attempted) {
        // The saved credential can become invalid after a server-side reset or
        // an interrupted password change. Retry once with an empty identity so
        // the signaling server provisions a new credential.
        self->_state->identity_recovery_attempted = true;
        NSString *host = [NSString stringWithUTF8String:
                                  self->_state->signal_host.c_str()];
        NSString *key = [NSString stringWithFormat:@"CrossDeskIdentity.%@.%d",
                                                   host,
                                                   self->_state->signal_port];
        [[NSUserDefaults standardUserDefaults] removeObjectForKey:key];
        [self destroyIdentityPeer];
        self->_state->identity_with_password.clear();
        self->_state->identity_base.clear();
        [self createIdentityPeer];
      }
      if (self->_state->identity_ready &&
          !self->_state->identity_base.empty() &&
          !self->_state->pending_remote_id.empty() &&
          !self->_state->controller_peer) {
        [self createControllerPeer];
      }
    }
    // The home-page signal indicator represents the long-lived identity peer.
    // A controller peer is created and destroyed for each session; forwarding
    // its shutdown would incorrectly make the server look disconnected.
    if (role == PeerRole::Identity) {
      DispatchMain(^{
        id<CrossDeskRTCBridgeDelegate> delegate = self.delegate;
        if ([delegate respondsToSelector:
                @selector(rtcBridge:didChangeSignalState:)]) {
          [delegate rtcBridge:self didChangeSignalState:state];
        }
      });
    }
  });
}

- (void)handleSignalMessage:(const char *)message
                       size:(size_t)size
                       role:(PeerRole)role {
  constexpr size_t kMaxPresenceMessageSize = 256 * 1024;
  if (role != PeerRole::Identity || !message || size == 0 ||
      size > kMaxPresenceMessageSize) {
    return;
  }

  NSData *payload = [NSData dataWithBytes:message length:size];
  id decoded = [NSJSONSerialization JSONObjectWithData:payload
                                                options:0
                                                  error:nil];
  if (![decoded isKindOfClass:NSDictionary.class]) return;
  NSDictionary *object = static_cast<NSDictionary *>(decoded);
  NSString *type = object[@"type"];
  if (![type isKindOfClass:NSString.class]) return;

  NSMutableDictionary<NSString *, NSNumber *> *presence =
      [NSMutableDictionary dictionary];
  if ([type isEqualToString:@"presence"]) {
    NSArray *devices = object[@"devices"];
    if (![devices isKindOfClass:NSArray.class]) return;
    for (id value in devices) {
      if (![value isKindOfClass:NSDictionary.class]) continue;
      NSDictionary *device = static_cast<NSDictionary *>(value);
      NSString *remote_id = device[@"id"];
      NSNumber *online = device[@"online"];
      if ([remote_id isKindOfClass:NSString.class] && remote_id.length > 0 &&
          [online isKindOfClass:NSNumber.class]) {
        presence[remote_id] = @([online boolValue]);
      }
    }
  } else if ([type isEqualToString:@"presence_update"]) {
    NSString *remote_id = object[@"id"];
    NSNumber *online = object[@"online"];
    if ([remote_id isKindOfClass:NSString.class] && remote_id.length > 0 &&
        [online isKindOfClass:NSNumber.class]) {
      presence[remote_id] = @([online boolValue]);
    }
  } else {
    return;
  }
  if (presence.count == 0) return;

  NSDictionary<NSString *, NSNumber *> *result = [presence copy];
  DispatchMain(^{
    id<CrossDeskRTCBridgeDelegate> delegate = self.delegate;
    if ([delegate respondsToSelector:
            @selector(rtcBridge:didReceivePresence:)]) {
      [delegate rtcBridge:self didReceivePresence:result];
    }
  });
}

- (void)handleProvisionedIdentity:(const std::string &)identity {
  if (identity.empty()) return;
  const std::string identity_copy = identity;
  dispatch_async(_rtcQueue, ^{
    self->_state->identity_with_password = identity_copy;
    self->_state->identity_base = BaseIdentity(identity_copy);
    NSString *host = [NSString stringWithUTF8String:
                                self->_state->signal_host.c_str()];
    NSString *key = [NSString stringWithFormat:@"CrossDeskIdentity.%@.%d",
                                                host, self->_state->signal_port];
    NSString *value = [NSString stringWithUTF8String:identity_copy.c_str()];
    if (IsReusableIdentity(identity_copy)) {
      [[NSUserDefaults standardUserDefaults] setObject:value forKey:key];
    } else {
      // Keep a passwordless identity only for this process. It is valid for
      // the current login but is not safe to reuse on the next app launch.
      [[NSUserDefaults standardUserDefaults] removeObjectForKey:key];
    }
    DispatchMain(^{
      id<CrossDeskRTCBridgeDelegate> delegate = self.delegate;
      if ([delegate respondsToSelector:@selector(rtcBridge:didProvisionIdentity:)]) {
        [delegate rtcBridge:self didProvisionIdentity:value];
      }
    });
  });
}

- (void)handleConnectionState:(CrossDeskConnectionState)state
                      remoteID:(const std::string &)remoteID
                    generation:(uint64_t)generation {
  if (![self isControllerGenerationCurrent:generation]) return;
  const std::string remote_copy = remoteID;
  if (state == CrossDeskConnectionStateConnected) {
    dispatch_async(_rtcQueue, ^{
      if (![self isControllerGenerationActive:generation] ||
          !self->_state->controller_peer) {
        return;
      }
      constexpr char client_info[] =
          "{\"type\":\"client_info\",\"version\":\"ios-native\","
          "\"platform\":\"ios\"}";
      SendSignalMessage(self->_state->controller_peer, client_info,
                        sizeof(client_info) - 1);
      const std::string controller_name =
          self->_state->identity_base.empty()
              ? "iPhone"
              : "iPhone-" + self->_state->identity_base;
      RemoteAction action{};
      action.type = ControlType::host_infomation;
      CopyCString(action.i.host_name, sizeof(action.i.host_name),
                  controller_name);
      action.i.host_name_size = std::strlen(action.i.host_name);
      action.i.display_list = nullptr;
      action.i.display_num = 0;
      action.i.left = nullptr;
      action.i.top = nullptr;
      action.i.right = nullptr;
      action.i.bottom = nullptr;
      const std::string host_info = action.to_json();
      SendReliableDataFrame(self->_state->controller_peer, host_info.data(),
                            host_info.size(), kControlStream);
      const std::string stream = crossdesk::MakeDisplayStreamId(
          static_cast<size_t>(self->_selectedDisplay.load()));
      RequestVideoKeyFrame(self->_state->controller_peer, stream.c_str());
    });
  }
  DispatchMain(^{
    if (![self isControllerGenerationCurrent:generation]) return;
    if (state == CrossDeskConnectionStateConnected &&
        ![self isControllerGenerationActive:generation]) {
      return;
    }
    NSString *identifier = [NSString stringWithUTF8String:remote_copy.c_str()];
    id<CrossDeskRTCBridgeDelegate> delegate = self.delegate;
    if ([delegate respondsToSelector:
            @selector(rtcBridge:didChangeConnectionState:remoteID:)]) {
      [delegate rtcBridge:self didChangeConnectionState:state remoteID:identifier];
    }
  });
}

- (BOOL)isControllerGenerationActive:(uint64_t)generation {
  return generation != 0 &&
         _activeControllerGeneration.load() == generation;
}

- (BOOL)isControllerGenerationCurrent:(uint64_t)generation {
  if (generation == 0 ||
      _controllerGenerationCounter.load() != generation) {
    return NO;
  }
  const uint64_t active = _activeControllerGeneration.load();
  return active == 0 || active == generation;
}

- (void)handleVideoFrame:(const XVideoFrame *)frame
                sourceID:(const char *)sourceID
            sourceIDSize:(size_t)sourceIDSize {
  if (!frame || frame->width == 0 || frame->height == 0) return;
  if (sourceID && sourceIDSize > 0) {
    const std::string source(sourceID, sourceIDSize);
    const std::string expected = crossdesk::MakeDisplayStreamId(
        static_cast<size_t>(_selectedDisplay.load()));
    if (source != expected) return;
  }
  const XNativeVideoFrame *native_frame = frame->native_frame;
  const bool has_native_pixel_buffer =
      native_frame &&
      native_frame->struct_size >=
          static_cast<uint32_t>(sizeof(XNativeVideoFrame)) &&
      native_frame->type == XNativeVideoFrameCVPixelBuffer &&
      native_frame->payload.cv_pixel_buffer;
  if (has_native_pixel_buffer) {
    CVPixelBufferRef pixel_buffer =
        static_cast<CVPixelBufferRef>(
            native_frame->payload.cv_pixel_buffer);
    [self enqueueVideoPixelBuffer:pixel_buffer
                            width:frame->width
                           height:frame->height];
    return;
  }

  if (!frame->data) return;
  const size_t y_size = static_cast<size_t>(frame->width) * frame->height;
  const size_t required_size = y_size + y_size / 2;
  if (frame->size < required_size) return;

  CVPixelBufferRef pixel_buffer = nullptr;
  CVReturn result = kCVReturnError;
  {
    std::lock_guard<std::mutex> lock(_videoPoolMutex);
    if (!_videoPool || _videoPoolWidth != frame->width ||
        _videoPoolHeight != frame->height) {
      if (_videoPool) CVPixelBufferPoolRelease(_videoPool);
      _videoPool = nullptr;
      _videoPoolWidth = frame->width;
      _videoPoolHeight = frame->height;
      NSDictionary *poolAttributes = @{
        (NSString *)kCVPixelBufferPoolMinimumBufferCountKey : @3,
      };
      NSDictionary *pixelAttributes = @{
        (NSString *)kCVPixelBufferWidthKey : @(frame->width),
        (NSString *)kCVPixelBufferHeightKey : @(frame->height),
        (NSString *)kCVPixelBufferPixelFormatTypeKey :
            @(kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange),
        (NSString *)kCVPixelBufferIOSurfacePropertiesKey : @{},
        (NSString *)kCVPixelBufferMetalCompatibilityKey : @YES,
      };
      CVPixelBufferPoolCreate(kCFAllocatorDefault,
                              (__bridge CFDictionaryRef)poolAttributes,
                              (__bridge CFDictionaryRef)pixelAttributes,
                              &_videoPool);
    }
    if (_videoPool) {
      result = CVPixelBufferPoolCreatePixelBuffer(kCFAllocatorDefault,
                                                   _videoPool, &pixel_buffer);
    }
  }
  if (result != kCVReturnSuccess || !pixel_buffer) return;

  CVPixelBufferLockBaseAddress(pixel_buffer, 0);
  const uint8_t *source_y = reinterpret_cast<const uint8_t *>(frame->data);
  const uint8_t *source_uv = source_y + y_size;
  uint8_t *destination_y = static_cast<uint8_t *>(
      CVPixelBufferGetBaseAddressOfPlane(pixel_buffer, 0));
  uint8_t *destination_uv = static_cast<uint8_t *>(
      CVPixelBufferGetBaseAddressOfPlane(pixel_buffer, 1));
  const size_t y_stride = CVPixelBufferGetBytesPerRowOfPlane(pixel_buffer, 0);
  const size_t uv_stride = CVPixelBufferGetBytesPerRowOfPlane(pixel_buffer, 1);
  for (size_t row = 0; row < frame->height; ++row) {
    std::memcpy(destination_y + row * y_stride,
                source_y + row * frame->width, frame->width);
  }
  for (size_t row = 0; row < frame->height / 2; ++row) {
    std::memcpy(destination_uv + row * uv_stride,
                source_uv + row * frame->width, frame->width);
  }
  CVPixelBufferUnlockBaseAddress(pixel_buffer, 0);
  CVBufferSetAttachment(pixel_buffer, kCVImageBufferYCbCrMatrixKey,
                        kCVImageBufferYCbCrMatrix_ITU_R_601_4,
                        kCVAttachmentMode_ShouldPropagate);

  const NSInteger frame_width = frame->width;
  const NSInteger frame_height = frame->height;
  if (_receivedVideoFrames.load() == 0) {
    uint8_t minimum_luma = std::numeric_limits<uint8_t>::max();
    uint8_t maximum_luma = std::numeric_limits<uint8_t>::min();
    uint64_t luma_total = 0;
    size_t luma_samples = 0;
    const size_t row_step = std::max<size_t>(1, frame->height / 96);
    const size_t column_step = std::max<size_t>(1, frame->width / 96);
    for (size_t row = 0; row < frame->height; row += row_step) {
      for (size_t column = 0; column < frame->width; column += column_step) {
        const uint8_t luma = source_y[row * frame->width + column];
        minimum_luma = std::min(minimum_luma, luma);
        maximum_luma = std::max(maximum_luma, luma);
        luma_total += luma;
        ++luma_samples;
      }
    }
    const double average_luma = luma_samples
                                    ? static_cast<double>(luma_total) /
                                          static_cast<double>(luma_samples)
                                    : 0.0;
    NSLog(@"CrossDesk first-frame luma min=%u max=%u average=%.1f",
          minimum_luma, maximum_luma, average_luma);
  }

  [self enqueueVideoPixelBuffer:pixel_buffer
                          width:frame_width
                         height:frame_height];
  CVPixelBufferRelease(pixel_buffer);
}

- (void)enqueueVideoPixelBuffer:(CVPixelBufferRef)pixelBuffer
                           width:(NSInteger)width
                          height:(NSInteger)height {
  if (!pixelBuffer) return;

  CVPixelBufferRetain(pixelBuffer);
  bool should_schedule = false;
  uint64_t generation = 0;
  {
    std::lock_guard<std::mutex> lock(_pendingVideoMutex);
    if (_pendingVideoBuffer) {
      CVPixelBufferRelease(_pendingVideoBuffer);
    }
    _pendingVideoBuffer = pixelBuffer;
    _pendingVideoWidth = width;
    _pendingVideoHeight = height;
    generation = _videoDeliveryGeneration;
    if (!_videoDeliveryScheduled) {
      _videoDeliveryScheduled = true;
      should_schedule = true;
    }
  }

  if (!should_schedule) return;
  dispatch_async(dispatch_get_main_queue(), ^{
    CVPixelBufferRef latest_buffer = nullptr;
    NSInteger latest_width = 0;
    NSInteger latest_height = 0;
    {
      std::lock_guard<std::mutex> lock(self->_pendingVideoMutex);
      if (generation != self->_videoDeliveryGeneration) return;
      latest_buffer = self->_pendingVideoBuffer;
      latest_width = self->_pendingVideoWidth;
      latest_height = self->_pendingVideoHeight;
      self->_pendingVideoBuffer = nullptr;
      self->_pendingVideoWidth = 0;
      self->_pendingVideoHeight = 0;
      self->_videoDeliveryScheduled = false;
    }

    if (!latest_buffer) return;
    const uint64_t frame_count = ++self->_receivedVideoFrames;
    if (frame_count == 1 || frame_count % 300 == 0) {
      NSLog(@"CrossDesk delivered latest frame %llu (%ld x %ld)",
            static_cast<unsigned long long>(frame_count), (long)latest_width,
            (long)latest_height);
    }
    id<CrossDeskRTCBridgeDelegate> delegate = self.delegate;
    if ([delegate respondsToSelector:
            @selector(rtcBridge:didReceivePixelBuffer:width:height:)]) {
      [delegate rtcBridge:self
          didReceivePixelBuffer:latest_buffer
                         width:latest_width
                        height:latest_height];
    }
    CVPixelBufferRelease(latest_buffer);
  });
}

- (void)resetVideoDelivery {
  CVPixelBufferRef pending_buffer = nullptr;
  {
    std::lock_guard<std::mutex> lock(_pendingVideoMutex);
    ++_videoDeliveryGeneration;
    pending_buffer = _pendingVideoBuffer;
    _pendingVideoBuffer = nullptr;
    _pendingVideoWidth = 0;
    _pendingVideoHeight = 0;
    _videoDeliveryScheduled = false;
  }
  if (pending_buffer) CVPixelBufferRelease(pending_buffer);
}

- (void)handleAudio:(const char *)data size:(size_t)size {
  if (!data || size < sizeof(int16_t) || size % sizeof(int16_t) != 0) return;
  NSData *pcm = [NSData dataWithBytes:data length:size];
  DispatchMain(^{
    id<CrossDeskRTCBridgeDelegate> delegate = self.delegate;
    if ([delegate respondsToSelector:
            @selector(rtcBridge:didReceiveAudioPCM:)]) {
      [delegate rtcBridge:self didReceiveAudioPCM:pcm];
    }
  });
}

- (void)handleData:(const char *)data
              size:(size_t)size
          sourceID:(const char *)sourceID
      sourceIDSize:(size_t)sourceIDSize {
  if (!data || size == 0 || !sourceID) return;
  const std::string source(sourceID, sourceIDSize);
  if (source == kControlStream) {
    RemoteAction action{};
    if (!action.from_json(std::string(data, size)) ||
        action.type != ControlType::host_infomation) {
      return;
    }
    NSString *host_name = [[NSString alloc]
        initWithBytes:action.i.host_name
               length:action.i.host_name_size
             encoding:NSUTF8StringEncoding];
    NSMutableArray<NSString *> *display_names =
        [NSMutableArray arrayWithCapacity:action.i.display_num];
    NSMutableArray<NSValue *> *display_sizes =
        [NSMutableArray arrayWithCapacity:action.i.display_num];
    for (std::size_t index = 0; index < action.i.display_num; ++index) {
      NSString *name = action.i.display_list && action.i.display_list[index]
          ? [NSString stringWithUTF8String:action.i.display_list[index]]
          : nil;
      [display_names addObject:name ?: @""];
      const int width = action.i.left && action.i.right
          ? std::max(action.i.right[index] - action.i.left[index], 0)
          : 0;
      const int height = action.i.top && action.i.bottom
          ? std::max(action.i.bottom[index] - action.i.top[index], 0)
          : 0;
      [display_sizes addObject:[NSValue valueWithCGSize:CGSizeMake(width,
                                                                  height)]];
    }
    crossdesk::FreeRemoteAction(action);
    DispatchMain(^{
      id<CrossDeskRTCBridgeDelegate> delegate = self.delegate;
      if ([delegate respondsToSelector:
              @selector(rtcBridge:didReceiveHostName:displayNames:
                                  displaySizes:)]) {
        [delegate rtcBridge:self
            didReceiveHostName:host_name ?: @""
                  displayNames:display_names
                  displaySizes:display_sizes];
      }
    });
    return;
  }

  if (source == kMouseStream) {
    RemoteAction action{};
    if (!action.from_json(std::string(data, size)) ||
        action.type != ControlType::cursor_state) {
      return;
    }
    const BOOL visible = action.cs.visible;
    const NSInteger shape = static_cast<NSInteger>(action.cs.shape);
    const BOOL position_update = action.cs.position_update;
    const BOOL position_valid = action.cs.position_valid;
    const float position_x = action.cs.x;
    const float position_y = action.cs.y;
    const float visual_offset_x = action.cs.visual_offset_x;
    const float visual_offset_y = action.cs.visual_offset_y;
    const NSInteger display_index = action.cs.display_id;
    const uint32_t sequence = action.cs.seq;
    DispatchMain(^{
      id<CrossDeskRTCBridgeDelegate> delegate = self.delegate;
      if ([delegate respondsToSelector:
              @selector(rtcBridge:didReceiveCursorVisible:shape:
                                    positionUpdate:positionValid:x:y:
                                    visualOffsetX:visualOffsetY:displayIndex:
                                    sequence:)]) {
        [delegate rtcBridge:self
            didReceiveCursorVisible:visible
                              shape:shape
                     positionUpdate:position_update
                      positionValid:position_valid
                                  x:position_x
                                  y:position_y
                      visualOffsetX:visual_offset_x
                      visualOffsetY:visual_offset_y
                       displayIndex:display_index
                           sequence:sequence];
      }
    });
    return;
  }

  if (source == kClipboardStream) {
    if (size > kMaxClipboardBytes || std::memchr(data, '\0', size)) return;
    NSString *text = [[NSString alloc] initWithBytes:data
                                              length:size
                                            encoding:NSUTF8StringEncoding];
    if (!text) return;
    DispatchMain(^{
      id<CrossDeskRTCBridgeDelegate> delegate = self.delegate;
      if ([delegate respondsToSelector:
              @selector(rtcBridge:didReceiveClipboardText:)]) {
        [delegate rtcBridge:self didReceiveClipboardText:text];
      }
    });
    return;
  }

  if (source == kFileFeedbackStream) {
    FileTransferAck ack{};
    if (!crossdesk::DecodeFileTransferAck(data, size, &ack)) return;

    NSString *name = nil;
    double progress = 0;
    {
      std::lock_guard<std::mutex> lock(_fileMutex);
      auto it = _state->outgoing_files.find(ack.file_id);
      if (it == _state->outgoing_files.end()) return;
      name = [NSString stringWithUTF8String:it->second.name.c_str()];
      progress = ack.total_size == 0
          ? 1
          : std::clamp(static_cast<double>(ack.acked_offset) /
                           static_cast<double>(ack.total_size),
                       0.0, 1.0);
      if ((ack.flags & 0x03) != 0) {
        if ((ack.flags & 0x02) != 0) progress = -1;
        _state->outgoing_files.erase(it);
      }
    }
    DispatchMain(^{
      id<CrossDeskRTCBridgeDelegate> delegate = self.delegate;
      if ([delegate respondsToSelector:
              @selector(rtcBridge:didUpdateFileTransfer:progress:sending:)]) {
        [delegate rtcBridge:self
            didUpdateFileTransfer:name ?: @"file"
                         progress:progress
                          sending:YES];
      }
    });
    return;
  }

  if (source != kFileStream) return;
  crossdesk::FileChunkView chunk;
  if (!crossdesk::DecodeFileChunk(data, size, &chunk)) return;
  const crossdesk::FileChunkHeader& header = chunk.header;

  NSURL *completed_url = nil;
  NSString *progress_name = nil;
  double progress = 0;
  FileTransferAck ack{};
  ack.magic = kFileAckMagic;
  ack.file_id = header.file_id;
  ack.acked_offset = header.offset;
  ack.total_size = header.total_size;

  {
    std::lock_guard<std::mutex> lock(_fileMutex);
    auto it = _state->incoming_files.find(header.file_id);
    if (it == _state->incoming_files.end()) {
      if ((header.flags & 0x01) == 0) return;
      NSString *raw_name = !chunk.file_name.empty()
          ? [[NSString alloc] initWithBytes:chunk.file_name.data()
                                     length:chunk.file_name.size()
                                   encoding:NSUTF8StringEncoding]
          : nil;
      NSString *safe_name = raw_name.lastPathComponent;
      safe_name = [safe_name stringByReplacingOccurrencesOfString:@"\\"
                                                        withString:@"_"];
      safe_name = [safe_name stringByReplacingOccurrencesOfString:@":"
                                                        withString:@"_"];
      if (safe_name.length == 0 || [safe_name isEqualToString:@"."] ||
          [safe_name isEqualToString:@".."]) {
        safe_name = [NSString stringWithFormat:@"received_%u", header.file_id];
      }

      NSURL *documents = [[NSFileManager defaultManager]
          URLsForDirectory:NSDocumentDirectory
                 inDomains:NSUserDomainMask].firstObject;
      NSURL *directory = [documents URLByAppendingPathComponent:@"Received"
                                                    isDirectory:YES];
      [[NSFileManager defaultManager] createDirectoryAtURL:directory
                               withIntermediateDirectories:YES
                                                attributes:nil
                                                     error:nil];
      NSURL *target = [directory URLByAppendingPathComponent:safe_name];
      if ([[NSFileManager defaultManager] fileExistsAtPath:target.path]) {
        NSString *stem = safe_name.stringByDeletingPathExtension;
        NSString *extension = safe_name.pathExtension;
        NSString *unique = extension.length > 0
            ? [NSString stringWithFormat:@"%@_%u.%@", stem, header.file_id,
                                         extension]
            : [NSString stringWithFormat:@"%@_%u", stem, header.file_id];
        target = [directory URLByAppendingPathComponent:unique];
        safe_name = unique;
      }

      FILE *output = std::fopen(target.path.fileSystemRepresentation, "wb+");
      if (!output) return;
      IncomingFile incoming;
      incoming.name = safe_name.UTF8String ?: "file";
      incoming.path = target.path.fileSystemRepresentation ?: "";
      incoming.total_size = header.total_size;
      incoming.handle = output;
      _state->incoming_files.emplace(header.file_id, std::move(incoming));
      it = _state->incoming_files.find(header.file_id);
    }

    IncomingFile &incoming = it->second;
    bool write_ok = incoming.total_size == header.total_size &&
                    fseeko(incoming.handle, static_cast<off_t>(header.offset),
                           SEEK_SET) == 0;
    if (write_ok && chunk.payload_size > 0) {
      write_ok = std::fwrite(chunk.payload, 1, chunk.payload_size,
                             incoming.handle) == chunk.payload_size;
    }
    if (!write_ok) {
      ack.flags |= 0x02;
      std::fclose(incoming.handle);
      _state->incoming_files.erase(it);
    } else {
      ack.acked_offset = header.offset + chunk.payload_size;
      incoming.received = std::max(incoming.received, ack.acked_offset);
      progress_name = [NSString stringWithUTF8String:incoming.name.c_str()];
      progress = incoming.total_size == 0
          ? 1
          : std::clamp(static_cast<double>(incoming.received) /
                           static_cast<double>(incoming.total_size),
                       0.0, 1.0);
      const bool completed = (header.flags & 0x02) != 0 ||
                             incoming.received >= incoming.total_size;
      if (completed) {
        ack.flags |= 0x01;
        std::fflush(incoming.handle);
        std::fclose(incoming.handle);
        completed_url = [NSURL fileURLWithPath:
            [NSString stringWithUTF8String:incoming.path.c_str()]];
        _state->incoming_files.erase(it);
      }
    }
  }

  dispatch_async(_rtcQueue, ^{
    if (self->_state->controller_peer) {
      const auto encoded_ack =
          crossdesk::EncodeFileTransferAck(ack);
      SendReliableDataFrame(self->_state->controller_peer,
                            encoded_ack.data(), encoded_ack.size(),
                            kFileFeedbackStream);
    }
  });
  if (progress_name) {
    DispatchMain(^{
      id<CrossDeskRTCBridgeDelegate> delegate = self.delegate;
      if ([delegate respondsToSelector:
              @selector(rtcBridge:didUpdateFileTransfer:progress:sending:)]) {
        [delegate rtcBridge:self
            didUpdateFileTransfer:progress_name
                         progress:progress
                          sending:NO];
      }
      if (completed_url &&
          [delegate respondsToSelector:
              @selector(rtcBridge:didReceiveFileAtURL:)]) {
        [delegate rtcBridge:self didReceiveFileAtURL:completed_url];
      }
    });
  }
}

- (void)handleStats:(const XNetTrafficStats *)stats mode:(TraversalMode)mode {
  if (!stats) return;
  const NSUInteger bitrate = stats->total_inbound_stats.bitrate;
  const float loss = stats->video_inbound_stats.loss_rate;
  const BOOL using_turn = mode == TraversalMode::Relay;
  DispatchMain(^{
    id<CrossDeskRTCBridgeDelegate> delegate = self.delegate;
    if ([delegate respondsToSelector:
            @selector(rtcBridge:didUpdateBitrate:lossRate:usingTURN:)]) {
      [delegate rtcBridge:self
          didUpdateBitrate:bitrate
                  lossRate:loss
                 usingTURN:using_turn];
    }
  });
}

@end
