#ifndef _X_H_
#define _X_H_

#if defined(_MSC_VER)
#define DLLAPI __declspec(dllexport)
#elif defined(__GNUC__)
#define DLLAPI __attribute__((visibility("default")))
#else
#define DLLAPI
#endif

#include <cstddef>
#include <cstdint>

enum DATA_TYPE { VIDEO = 0, AUDIO, DATA };
enum ConnectionStatus {
  Connecting = 0,
  Connected,
  Gathering,
  Disconnected,
  Failed,
  Closed,
  IncorrectPassword,
  NoSuchTransmissionId
};

enum SignalStatus {
  SignalConnecting = 0,
  SignalConnected,
  SignalFailed,
  SignalClosed,
  SignalReconnecting,
  SignalServerClosed
};

enum TraversalMode { P2P = 0, Relay, UnknownMode };

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  const char* data;
  size_t size;
  uint32_t width;
  uint32_t height;
  uint64_t captured_timestamp;
  uint64_t received_timestamp;
  uint64_t decoded_timestamp;
  uint64_t rendered_timestamp;
} XVideoFrame;

typedef struct {
  uint32_t bitrate;
  uint32_t rtp_packet_count;
  float loss_rate;
} XInboundStats;

typedef struct {
  uint32_t bitrate;
  uint32_t rtp_packet_count;
} XOutboundStats;

typedef struct {
  XInboundStats video_inbound_stats;
  XOutboundStats video_outbound_stats;
  XInboundStats audio_inbound_stats;
  XOutboundStats audio_outbound_stats;
  XInboundStats data_inbound_stats;
  XOutboundStats data_outbound_stats;
  XInboundStats total_inbound_stats;
  XOutboundStats total_outbound_stats;
} XNetTrafficStats;

typedef struct Peer PeerPtr;

typedef void (*OnReceiveBuffer)(const char*, size_t, const char*, const size_t,
                                void*);

typedef void (*OnReceiveVideoFrame)(const XVideoFrame*, const char*,
                                    const size_t, void*);

typedef void (*OnSignalStatus)(SignalStatus, void*);

typedef void (*OnConnectionStatus)(ConnectionStatus, const char*, const size_t,
                                   void*);

typedef void (*NetStatusReport)(const char*, const size_t, TraversalMode,
                                const XNetTrafficStats*, const char*,
                                const size_t, void*);

typedef struct {
  bool use_cfg_file;
  const char* cfg_path;

  const char* signal_server_ip;
  int signal_server_port;
  const char* stun_server_ip;
  int stun_server_port;
  const char* turn_server_ip;
  int turn_server_port;
  const char* turn_server_username;
  const char* turn_server_password;
  bool hardware_acceleration;
  bool av1_encoding;
  bool enable_turn;

  OnReceiveBuffer on_receive_video_buffer;
  OnReceiveBuffer on_receive_audio_buffer;
  OnReceiveBuffer on_receive_data_buffer;

  OnReceiveVideoFrame on_receive_video_frame;

  OnSignalStatus on_signal_status;
  OnConnectionStatus on_connection_status;
  NetStatusReport net_status_report;
  void* user_data;
} Params;

DLLAPI PeerPtr* CreatePeer(const Params* params);

DLLAPI void DestroyPeer(PeerPtr** peer_ptr);

DLLAPI int Init(PeerPtr* peer_ptr, const char* user_id);

DLLAPI int CreateConnection(PeerPtr* peer_ptr, const char* transmission_id,
                            const char* password);

DLLAPI int JoinConnection(PeerPtr* peer_ptr, const char* transmission_id,
                          const char* password);

DLLAPI int LeaveConnection(PeerPtr* peer_ptr, const char* transmission_id);

DLLAPI int SendVideoFrame(PeerPtr* peer_ptr, const XVideoFrame* video_frame);

DLLAPI int SendAudioFrame(PeerPtr* peer_ptr, const char* data, size_t size);

DLLAPI int SendDataFrame(PeerPtr* peer_ptr, const char* data, size_t size);

DLLAPI int64_t GetSystemTimeMicros(PeerPtr* peer_ptr);

#ifdef __cplusplus
}
#endif

#endif