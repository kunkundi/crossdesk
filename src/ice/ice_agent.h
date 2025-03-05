#ifndef _ICE_AGENT_H_
#define _ICE_AGENT_H_

#include <atomic>
#include <iostream>
#include <thread>

#include "gio/gnetworking.h"
#include "glib.h"
#include "nice/agent.h"

typedef enum {
  ICE_STATE_DISCONNECTED,
  ICE_STATE_GATHERING,
  ICE_STATE_CONNECTING,
  ICE_STATE_CONNECTED,
  ICE_STATE_READY,
  ICE_STATE_FAILED,
  ICE_STATE_NOT_INITIALIZED,
  ICE_STATE_DESTROYED,
  ICE_STATE_NULLPTR,
  ICE_STATE_LAST
} ICE_STATE;

typedef void (*nice_cb_state_changed_t)(NiceAgent* agent, guint stream_id,
                                        guint component_id,
                                        NiceComponentState state,
                                        gpointer data);
typedef void (*nice_cb_new_candidate_t)(NiceAgent* agent, guint stream_id,
                                        guint component_id, gchar* foundation,
                                        gpointer data);
typedef void (*nice_cb_new_selected_pair_t)(NiceAgent* agent, guint stream_id,
                                            guint component_id,
                                            const char* lfoundation,
                                            const char* rfoundation,
                                            gpointer data);
typedef void (*nice_cb_gathering_done_t)(NiceAgent* agent, guint stream_id,
                                         gpointer data);
typedef void (*nice_cb_recv_t)(NiceAgent* agent, guint stream_id,
                               guint component_id, guint size, gchar* buffer,
                               gpointer data);

typedef struct {
  void* user_ptr_1_;
  void* user_ptr_2_;
} UserPtrSt;

class IceAgent {
 public:
  IceAgent(bool offer_peer, bool use_trickle_ice, bool use_reliable_ice,
           bool enable_turn, bool force_turn, std::string& stun_ip,
           uint16_t stun_port, std::string& turn_ip, uint16_t turn_port,
           std::string& turn_username, std::string& turn_password);
  ~IceAgent();

  int CreateIceAgent(nice_cb_state_changed_t on_state_changed,
                     nice_cb_new_candidate_t on_new_candidate,
                     nice_cb_gathering_done_t on_gathering_done,
                     nice_cb_new_selected_pair_t on_new_selected_pair,
                     nice_cb_recv_t on_recv, void* user_ptr);

  int DestroyIceAgent();

  const char* GenerateLocalSdp();

  const char* GetLocalStreamSdp(uint32_t stream_id);

  int SetRemoteSdp(const char* remote_sdp);

  int GatherCandidates();

  ICE_STATE GetIceState();

  int SetRemoteGatheringDone();

  int Send(const char* data, size_t size);

 public:
  bool use_trickle_ice_ = true;
  bool use_reliable_ice_ = false;
  bool enable_turn_ = false;
  bool force_turn_ = false;

  std::string stun_ip_ = "";
  uint16_t stun_port_ = 0;
  std::string turn_ip_ = "";
  uint16_t turn_port_ = 0;
  std::string turn_username_ = "";
  std::string turn_password_ = "";

  bool has_video_stream_ = true;
  uint32_t n_video_streams_ = 1;
  std::string video_stream_sdp_;
  bool has_audio_stream_ = true;
  uint32_t n_audio_streams_ = 1;
  std::string audio_stream_sdp_;
  bool has_data_stream_ = true;
  uint32_t n_data_streams_ = 1;
  std::string data_stream_sdp_;

 public:
  std::thread nice_thread_;
  std::atomic<NiceAgent*> agent_{nullptr};
  std::atomic<GMainLoop*> gloop_{nullptr};
  std::atomic<bool> nice_inited_{false};

  gboolean exit_nice_thread_ = false;
  bool controlling_ = false;
  gchar* ice_ufrag_ = nullptr;
  gchar* ice_password_ = nullptr;
  uint32_t stream_id_ = 0;
  uint32_t n_components_ = 1;
  // char* local_sdp_ = nullptr;
  std::string local_sdp_ = "";
  ICE_STATE state_ = ICE_STATE_LAST;
  bool destroyed_ = false;
  gboolean agent_closed_ = false;

  nice_cb_state_changed_t on_state_changed_;
  nice_cb_new_selected_pair_t on_new_selected_pair_;
  nice_cb_new_candidate_t on_new_candidate_;
  nice_cb_gathering_done_t on_gathering_done_;
  nice_cb_recv_t on_recv_;
  void* user_ptr_;

  UserPtrSt user_prt_st_;

  FILE* file_in_ = nullptr;
  FILE* file_out_ = nullptr;
};

#endif