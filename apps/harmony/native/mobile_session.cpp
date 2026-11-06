#include "mobile_session.h"
#include "media.h"
#include "audio.h"
#include "key_mapping.h"
#include <app_version.h>
#include <mobile_controller_protocol.h>
#include <mobile_file_transfer_session.h>
#include <mobile_video_timing.h>
#include <display_stream_id.h>
#include <libyuv.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <memory>
#include <set>

namespace crossdesk::harmony {
namespace {
using json = nlohmann::json;
using Clock = std::chrono::steady_clock;
// A separate controller-only entry point. No Capture, Input, AddVideoStream,
// host input handler or screen-recording authorization exists in this session.
class MobileSession {
 public:
  MobileSession() : renderer_([this](const uint8_t* pixels, int w, int h, uint64_t captured,
                                    uint64_t received, uint64_t generation) {
    const auto session = session_.load(), connection = connection_.load();
    if (!connected_ || generation != video_generation_) return;
    const auto now = std::chrono::duration_cast<std::chrono::microseconds>(Clock::now().time_since_epoch()).count();
    // MiniRTC uses its own monotonic clock; the offset is sampled on its worker.
    const double latency = mobile_controller::VideoLatencyMilliseconds(captured, received, now + clock_offset_);
    Event({{"type", "frame"}, {"width", w}, {"height", h}, {"latency", latency}, {"frame", ++frame_id_},
           {"videoGeneration", generation}, {"session", session}, {"connection", connection}}, true);
    std::string path;
    uint64_t preview_epoch;
    { std::lock_guard lock(preview_mutex_); path.swap(preview_path_); preview_epoch = preview_epoch_; }
    if (path.empty()) return;
    // Only the decoded video is sampled, never the device screen or controls.
    // Downscale YUV first: a 4K/8K desktop must not allocate a full-size RGBA copy on a phone.
    std::vector<uint8_t> scaled(640 * 360 * 3 / 2), preview(640 * 360 * 4);
    const int cw = std::min(w, (h * 16 / 9) & ~1), ch = std::min(h, (w * 9 / 16) & ~1);
    const int left = ((w-cw)/2)&~1, top = ((h-ch)/2)&~1;
    bool ok = cw > 0 && ch > 0 && libyuv::NV12Scale(pixels+size_t(top)*w+left,w,
      pixels+size_t(w)*h+size_t(top/2)*w+left,w,cw,ch,scaled.data(),640,
      scaled.data()+640*360,640,640,360,libyuv::kFilterBilinear) == 0;
    if (ok) ok = libyuv::NV12ToABGR(scaled.data(),640,scaled.data()+640*360,640,preview.data(),640*4,640,360) == 0;
    std::lock_guard lock(preview_mutex_);
    if (ok && connected_ && generation == video_generation_ && preview_epoch == preview_epoch_) {
      std::ofstream out(path, std::ios::binary | std::ios::trunc);
      out.write(reinterpret_cast<const char*>(preview.data()), preview.size()); out.close(); ok = bool(out);
    } else ok = false;
    if (!ok) std::remove(path.c_str());
    Event({{"type", "preview"}, {"path", path}, {"ok", ok}, {"videoGeneration", generation},
           {"session", session}, {"connection", connection}}, true);
  }), worker_([this] { Run(); }) {}
  ~MobileSession() {
    { std::lock_guard lock(queue_mutex_); stopping_ = true; pending_.clear(); ready_.notify_one(); }
    worker_.join();
  }
  void Queue(std::function<void()> fn) {
    std::lock_guard lock(queue_mutex_);
    if (!stopping_ && pending_.size() < 8192) { pending_.push_back(std::move(fn)); ready_.notify_one(); }
  }
  void Command(std::string text) { Queue([this, text = std::move(text)] { Execute(json::parse(text)); }); }
  std::string Poll() {
    std::lock_guard lock(events_mutex_);
    auto value = events_.dump(-1, ' ', false, json::error_handler_t::replace); events_ = json::array(); return value;
  }
 private:
  struct Context { MobileSession* owner; bool controller; uint64_t generation; };
  void Event(json value, bool controller = false) {
    if (!value.contains("session")) value["session"] = session_.load();
    if (controller && !value.contains("connection")) value["connection"] = connection_.load();
    std::lock_guard lock(events_mutex_);
    // Coalesce high-frequency samples, never displace identity/password or terminal events.
    if (value["type"] == "frame") value["latencyCount"] = value["latency"].get<double>() >= 0 ? 1 : 0;
    if (value["type"] == "frame" || value["type"] == "network") {
      for (auto it = events_.begin(); it != events_.end();) {
        if ((*it)["type"] == value["type"]) {
          if (value["type"] == "frame" && (*it)["session"] == value["session"] &&
              (*it)["connection"] == value["connection"] && (*it)["videoGeneration"] == value["videoGeneration"]) {
            const int previous = it->value("latencyCount", 0), count = value["latencyCount"];
            if (previous + count > 0) value["latency"] =
              (it->value("latency", 0.) * previous + value["latency"].get<double>() * count) / (previous + count);
            value["latencyCount"] = previous + count;
          }
          it = events_.erase(it);
        } else ++it;
      }
    }
    if (events_.size() < 1024) events_.push_back(std::move(value));
  }
  bool Current(bool controller, uint64_t generation) const {
    return generation == (controller ? connection_.load() : session_.load());
  }
  template<typename F> static void Dispatch(void* user, F fn) {
    auto* c = static_cast<Context*>(user); auto* self = c->owner;
    self->Queue([self, controller = c->controller, generation = c->generation, fn] {
      if (self->Current(controller, generation)) fn(self, controller);
    });
  }
  static void Signal(SignalStatus status, const char*, size_t, void* user) {
    Dispatch(user, [status](MobileSession* s, bool controller) {
      if (!controller) {
        s->online_ = status == SignalConnected;
        if (s->signal_status_ != status) {
          ++s->signal_generation_;
          if (s->signal_status_ == SignalConnected) s->signal_started_ = Clock::now();
        }
        s->signal_status_ = status;
        if (s->online_) {
          constexpr char info[] = R"({"type":"client_info","version":"harmony-mobile","platform":"harmonyos"})";
          SendSignalMessage(s->identity_, info, sizeof(info)-1);
        }
      }
      s->Event({{"type", "signal"}, {"controller", controller}, {"status", status},
                {"generation", s->signal_generation_}, {"id", s->login_.substr(0, s->login_.find('@'))}});
      if (controller && status >= SignalFailed && status != SignalReconnecting) s->Disconnect("远程会话信令连接失败");
    });
  }
  static void SignalMessage(const char* data, size_t size, void* user) {
    if (!data || size > 1024*1024) return;
    const auto value = json::parse(data, data+size, nullptr, false);
    if (!value.is_object()) return;
    Dispatch(user, [value](MobileSession* s, bool controller) {
      if (controller || !s->online_) return;
      const auto type = value.value("type", "");
      if (type == "announcements" || type == "announcements_changed" || type == "presence" || type == "presence_update")
        s->Event({{"type", "signalMessage"}, {"message", value}, {"generation", s->signal_generation_}});
    });
  }
  static void Connection(ConnectionStatus status, const char* id, size_t size, void* user) {
    const std::string remote(id ? id : "", id ? size : 0);
    Dispatch(user, [status, remote](MobileSession* s, bool controller) {
      if (!controller) {
        // Defense in depth if a server tries to attach a guest to our identity.
        if (status == Connected && s->identity_)
          DisconnectPeerConnection(s->identity_, s->login_.substr(0,s->login_.find('@')).c_str(), remote.c_str());
        return;
      }
      s->Event({{"type", "connection"}, {"status", status}}, true);
      if (status == Connected && !s->connected_) {
        s->connecting_ = false; s->connected_ = true;
        auto info = MakeHostInformation("CrossDesk HarmonyOS Mobile", {}, true, s->version_, HostPlatform::HarmonyOS);
        info.i.supports_video_codec_status = true;
        s->Send(info); FreeRemoteAction(info);
        RemoteAction audio{}; audio.type = audio_capture; audio.a = !s->muted_; s->Send(audio);
        s->audio_.Start();
      } else if (status >= Disconnected) s->Disconnect("");
    });
  }
  static void Network(const char* id, size_t size, TraversalMode mode, const MiniRtcNetTrafficStats* stats,
                      const char*, size_t, void* user) {
    if (mode == UnknownMode && id && size) {
      const std::string login(id, size);
      Dispatch(user, [login](MobileSession* s, bool controller) {
        if (!controller) {
          // Reconnect reports may contain only the public ID. Never replace a
          // usable ID@password with that public-only report (same as Android).
          if (login.find('@') != std::string::npos || s->login_.rfind(login + "@", 0) != 0) s->login_ = login;
          s->Event({{"type", "identity"}, {"identity", s->login_}});
        }
      });
    } else if (stats) {
      auto report = mobile_controller::NetworkReport(*stats, mode);
      Dispatch(user, [report](MobileSession* s, bool controller) {
        if (controller && s->connected_) s->Event({{"type", "network"}, {"report", report}}, true);
      });
    }
  }
  static void Data(const char* data, size_t size, const char*, size_t, const char* stream, size_t length, void* user) {
    if (!data || !stream || size > 1024*1024) return;
    const std::string bytes(data,size), name(stream,length);
    Dispatch(user, [bytes,name](MobileSession* s, bool controller) {
      // The data channel can deliver host information/file chunks before the
      // local ICE Connected notification. Keep those authenticated packets.
      if (!controller || (!s->connected_ && !s->connecting_)) return;
      if (name == kFileStream && s->files_) s->files_->Receive(bytes.data(), bytes.size());
      else if (name == kFileFeedbackStream && s->files_) s->files_->Ack(bytes.data(), bytes.size());
      else if (name == kClipboardStream && bytes.size() <= kMaxClipboardBytes)
        s->Event({{"type", "clipboard"}, {"text", bytes}}, true);
      else if (name == kControlStream) {
        RemoteAction action{};
        if (action.from_json(bytes)) {
          const auto value = json::parse(action.to_json()); FreeRemoteAction(action);
          s->Event({{"type", "data"}, {"message", value}}, true);
        }
      }
    });
  }
  static void Video(const MiniRtcVideoFrame* frame, const char*, size_t, const char* stream, size_t size, void* user) {
    auto* c = static_cast<Context*>(user); auto* s = c->owner;
    if (c->controller && c->generation == s->connection_ && s->connected_ && !s->suspended_ && frame && stream &&
        std::string(stream,size) == MakeDisplayStreamId(s->display_)) s->renderer_.Video(*frame, s->video_generation_);
  }
  static void Audio(const char* data, size_t size, const char*, size_t, const char*, size_t, void* user) {
    auto* c = static_cast<Context*>(user); auto* s = c->owner;
    if (c->controller && c->generation == s->connection_ && s->connected_ && !s->muted_ && !s->suspended_) s->audio_.Push(data,size);
  }
  Params Parameters(bool controller) {
    Params p{};
    std::snprintf(p.signal_server_ip,sizeof(p.signal_server_ip),"%s",server_.c_str());
    std::snprintf(p.log_path,sizeof(p.log_path),"%s",logs_.c_str());
    p.signal_server_port = port_; p.enable_srtp = true; p.turn_mode = TurnAutoUdpTcp;
    p.video_content_type = VideoContentType::ScreenContent; p.video_quality = QualityMedium; p.video_frame_rate = 30;
    p.video_degradation_preference = VideoDegradationPreference::MaintainResolution;
    p.hardware_acceleration = true;
    p.user_id = controller ? controller_login_.c_str() : login_.c_str();
    p.user_data = controller ? control_context_.get() : identity_context_.get();
    p.on_signal_status = Signal; p.on_signal_message = SignalMessage; p.on_connection_status = Connection;
    p.on_net_status_report = Network; p.on_receive_data_buffer = Data; p.on_receive_video_frame = Video; p.on_receive_audio_buffer = Audio;
    return p;
  }
  void Send(const RemoteAction& action, const char* stream = kControlStream) {
    if (!controller_) return;
    const auto bytes = action.to_json(); SendReliableDataFrame(controller_,bytes.data(),bytes.size(),stream);
  }
  void Release() {
    for (int key : keys_) { RemoteAction a{}; a.type=keyboard; a.k={size_t(key),0,false,key_up}; Send(a,kKeyboardStream); }
    for (int i=0;i<3;i++) if (buttons_ & (1u<<i)) {
      RemoteAction a{}; a.type=mouse; a.m={x_,y_,0,static_cast<MouseFlag>(2+i*2)}; Send(a,kMouseStream);
    }
    keys_.clear(); buttons_=0;
  }
  void CancelPreview() {
    std::lock_guard lock(preview_mutex_); preview_path_.clear(); ++preview_epoch_;
    std::error_code error;
    if (preview_directory_.empty()) return;
    for (const auto& file : std::filesystem::directory_iterator(preview_directory_, error)) {
      const auto name = file.path().filename().string();
      if (name.rfind("preview-", 0) == 0 && file.path().extension() == ".rgba") std::filesystem::remove(file.path(), error);
    }
  }
  void Disconnect(const std::string& reason) {
    const bool active = controller_ != nullptr;
    Release(); connected_=false; connecting_=false; ++video_generation_;
    CancelPreview();
    if (controller_) { LeaveConnection(controller_,remote_.c_str()); DestroyPeer(&controller_); }
    files_.reset(); renderer_.Clear(); audio_.Stop();
    if (active) Event({{"type","ended"},{"reason",reason}},true);
    ++connection_; display_=0; frame_id_=0;
  }
  void Stop() {
    Disconnect(""); online_=false;
    if (identity_) DestroyPeer(&identity_);
    ++session_; login_.clear(); signal_status_=-1;
  }
  void Execute(const json& v) {
    const auto op=v.at("op").get<std::string>();
    if (op=="start") {
      Stop(); session_=v.at("session").get<uint64_t>();
      server_=v.at("server").get<std::string>(); port_=v.at("port").get<int>();
      logs_=v.at("logs").get<std::string>(); login_=v.value("identity",""); version_=v.value("version","0.1.0");
      const auto roots=v.at("certificates").get<std::string>(); directory_=v.at("directory").get<std::string>();
      preview_directory_=v.at("previewDirectory").get<std::string>();
      if (server_.empty()||server_.size()>=256||server_.find('@')!=std::string::npos||port_<1||port_>65535||
          logs_.size()>=256||roots.empty()||directory_.empty()) throw std::runtime_error("Invalid configuration");
      setenv("SSL_CERT_DIR",roots.c_str(),1);
      if (ConfigureMiniRtcLogging(MiniRtcLogOff, nullptr, nullptr) != 0)
        throw std::runtime_error("Unable to configure MiniRTC logging");
      identity_context_=std::make_unique<Context>(Context{this,false,session_});
      auto p=Parameters(false); identity_=CreatePeer(&p); signal_started_=Clock::now();
      if (!identity_||Init(identity_)!=0) { throw std::runtime_error("Identity initialization failed"); }
    } else if (op=="stop") Stop();
    else if (op=="version") {
      const auto body=v.at("body").get<std::string>(), current=v.value("version","");
      const bool local=v.value("local",false);
      std::optional<std::string> result;
      if (local) result=CheckPlatformAppUpdate(current,body,"harmony-arm64");
      else if (const auto release=ParseVersionInfoJSON(body)) result=AvailableAppUpdate(current,*release).value_or("");
      Event({{"type","version"},{"request",v.at("request")},{"ok",result.has_value()},{"latest",result.value_or("")}});
    } else if (op=="surface") renderer_.Surface(std::stoull(v.value("id","0")));
    else if (op=="connect") {
      if (!online_||login_.empty()) throw std::runtime_error("Not signed in");
      Disconnect(""); connection_=v.at("connection").get<uint64_t>();
      remote_=v.at("remote").get<std::string>(); const auto password=v.at("password").get<std::string>();
      if (remote_.size()!=9||!std::all_of(remote_.begin(),remote_.end(),::isdigit)||password.size()>128||password.find('@')!=std::string::npos)
        throw std::runtime_error("Invalid remote credentials");
      muted_=false; suspended_=false; controller_login_="C-"+login_.substr(0,login_.find('@'));
      control_context_=std::make_unique<Context>(Context{this,true,connection_});
      auto p=Parameters(true); controller_=CreatePeer(&p); connecting_=true; connection_started_=Clock::now();
      if (!controller_) throw std::runtime_error("Peer creation failed");
      AddAudioStream(controller_,kAudioStream); AddDataStream(controller_,kDataStream,false);
      for (auto stream : {kMouseStream,kKeyboardStream,kControlStream,kClipboardStream,kFileStream,kFileFeedbackStream})
        AddDataStream(controller_,stream,true);
      files_=std::make_unique<mobile_controller::FileTransferSession>(
        [this](const char* stream,const char* data,size_t size) { return controller_?SendReliableDataFrame(controller_,data,size,stream):-1; },
        [this](const std::string& name,double progress,bool sending,const std::string& path) {
          Event({{"type","file"},{"name",name},{"progress",progress},{"sending",sending},{"path",path}},true);
        });
      files_->SetDirectory(directory_+"/received");
      if (Init(controller_)!=0||JoinConnection(controller_,(remote_+"@"+password).c_str())!=0) Disconnect("无法发起连接");
    } else if (op=="disconnect") Disconnect("");
    else if (op=="release") Release();
    else if (op=="signal" && online_ && identity_) {
      auto request=v.at("message"); const auto type=request.value("type","");
      if (type!="announcements_list"&&type!="recent_connections_presence") return;
      request["user_id"]=login_.substr(0,login_.find('@'));
      const auto bytes=request.dump(); const bool ok=SendSignalMessage(identity_,bytes.data(),bytes.size())==0;
      if (!ok) Event({{"type","requestFailed"},{"request",request.value("request_id","")}});
    } else if (connected_) {
      if (op=="pointer") {
        const int flag=v.at("flag").get<int>(); const float x=v.at("x").get<float>(),y=v.at("y").get<float>();
        if (flag<0||flag>8||!std::isfinite(x)||!std::isfinite(y)) return;
        x_=std::clamp(x,0.f,1.f); y_=std::clamp(y,0.f,1.f);
        if (flag>=1&&flag<=6) { unsigned bit=1u<<((flag-1)/2); if (flag&1) buttons_|=bit; else buttons_&=~bit; }
        RemoteAction a{}; a.type=mouse; a.m={x_,y_,std::clamp(v.value("scroll",0),-120,120),static_cast<MouseFlag>(flag)}; Send(a,kMouseStream);
      } else if (op=="key"||op=="virtualKey") {
        const int key=op=="key"?ToVirtualKey(v.at("code").get<int>()):v.at("code").get<int>();
        if (key<=0||key>255) return;
        const bool down=v.at("down").get<bool>(); if (down) keys_.insert(key); else keys_.erase(key);
        RemoteAction a{}; a.type=keyboard; a.k={size_t(key),0,false,down?key_down:key_up}; Send(a,kKeyboardStream);
      } else if (op=="display") {
        const int index=v.at("index").get<int>(); if(index<0||index>=32)return;
        Release(); display_=index; ++video_generation_; renderer_.Clear();
        RemoteAction a{}; a.type=display_id; a.d=index; Send(a);
      } else if (op=="visible") {
        suspended_=!v.at("visible").get<bool>();
        if (suspended_) { renderer_.Clear(); audio_.Stop(); } else if (!muted_) audio_.Start();
      } else if (op=="mute") {
        muted_=v.at("muted").get<bool>(); if (muted_||suspended_) audio_.Stop(); else audio_.Start();
        RemoteAction a{}; a.type=audio_capture; a.a=!muted_; Send(a);
      } else if (op=="clipboard") {
        const auto text=v.at("text").get<std::string>();
        if(!text.empty()&&text.size()<=kMaxClipboardBytes) SendReliableDataFrame(controller_,text.data(),text.size(),kClipboardStream);
      } else if (op=="videoSettings") {
        const int q=v.at("quality"),fps=v.at("frameRate"),pref=v.at("preference");
        if(q<0||q>2||(fps!=30&&fps!=60)||pref<0||pref>2)return;
        auto bytes=mobile_controller::VideoSettingsMessage(q,fps,pref,v.at("request").get<uint32_t>());
        if(SendReliableDataFrame(controller_,bytes.data(),bytes.size(),kControlStream)!=0)
          Event({{"type","settingsFailed"},{"request",v.at("request")}},true);
      } else if (op=="privacy") {
        const auto bytes = mobile_controller::PrivacyMessage(
            v.at("flag").get<int>(), v.at("blockLocalInput").get<bool>());
        if (bytes.empty() ||
            SendReliableDataFrame(controller_, bytes.data(), bytes.size(), kControlStream) != 0)
          Event({{"type", "privacyFailed"}, {"request", v.at("request")}}, true);
      } else if (op=="sas") {
        const auto bytes=mobile_controller::SecureAttentionMessage();
        const bool ok=SendReliableDataFrame(controller_,bytes.data(),bytes.size(),kControlStream)==0;
        Event({{"type","feedback"},{"text",ok?"已发送 Ctrl+Alt+Del":"指令发送失败，请重试"}},true);
      } else if (op=="sendFile") {
        const auto path=v.at("path").get<std::string>(),name=v.at("name").get<std::string>();
        // Only disposable staged files owned by this session may be deleted by the transfer engine.
        const auto parent=std::filesystem::weakly_canonical(std::filesystem::path(path).parent_path());
        const bool staged=parent==std::filesystem::weakly_canonical(directory_+"/outgoing");
        if(!staged||!files_||!files_->Start(path,name)) {
          if (staged) { std::error_code error; std::filesystem::remove(path, error); }
          Event({{"type","file"},{"name",name},{"progress",-1},{"sending",true},{"path",""}},true);
        }
      } else if (op=="preview") {
        std::lock_guard lock(preview_mutex_);
        preview_path_=preview_directory_+"/preview-"+std::to_string(video_generation_)+"-"+std::to_string(++preview_epoch_)+".rgba";
      } else if (op=="cancelPreview") CancelPreview();
    }
  }
  void Tick() {
    const auto now=Clock::now();
    if(identity_&&!online_&&now-signal_started_>std::chrono::seconds(40)) {
      Event({{"type","error"},{"text","服务器连接超时，请检查地址和网络"}}); Stop();
    }
    if(connecting_&&now-connection_started_>std::chrono::seconds(40)) Disconnect("连接超时，请确认设备在线");
    if(!connected_)return;
    if(files_)files_->Pump();
    if(now-keyboard_sent_>std::chrono::milliseconds(200)) {
      RemoteAction a{}; a.type=keyboard_state; a.ks.seq=++keyboard_sequence_;
      for(int key:keys_) { if(a.ks.pressed_count>=kMaxKeyboardStateKeys)break; a.ks.pressed_keys[a.ks.pressed_count++]={size_t(key),0,false}; }
      Send(a,kKeyboardStream); keyboard_sent_=now;
      const auto local=std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count();
      const auto rtc=GetSystemTimeMicros(controller_); if(rtc>0)clock_offset_=rtc-local;
    }
  }
  void Run() {
    for(;;) {
      std::function<void()> fn;
      { std::unique_lock lock(queue_mutex_); ready_.wait_for(lock,std::chrono::milliseconds(10),[this]{return stopping_||!pending_.empty();});
        if(stopping_)break; if(!pending_.empty()){fn=std::move(pending_.front());pending_.pop_front();} }
      try { if(fn)fn(); Tick(); }
      catch(const std::exception&) { Event({{"type","error"},{"text","操作失败，请检查连接参数或重新连接"}}); }
    }
    Stop();
  }
  std::mutex queue_mutex_,events_mutex_,preview_mutex_;
  uint64_t preview_epoch_ = 0;
  std::condition_variable ready_;
  std::deque<std::function<void()>> pending_;
  json events_=json::array();
  bool stopping_=false,online_=false,connecting_=false;
  std::atomic<bool> connected_{false},muted_{false},suspended_{false};
  std::atomic<uint64_t> session_{0},connection_{0},video_generation_{1},frame_id_{0};
  std::atomic<int64_t> clock_offset_{0};
  std::atomic<int> display_{0};
  PeerPtr* identity_=nullptr;
  PeerPtr* controller_=nullptr;
  std::unique_ptr<Context> identity_context_,control_context_;
  std::unique_ptr<mobile_controller::FileTransferSession> files_;
  std::string server_,logs_,login_,controller_login_,remote_,directory_,preview_directory_,preview_path_,version_;
  int port_=9099,signal_status_=-1;
  uint64_t signal_generation_=0;
  std::set<int> keys_;
  unsigned buttons_=0;
  uint32_t keyboard_sequence_=0;
  float x_=.5f,y_=.5f;
  Clock::time_point signal_started_,connection_started_,keyboard_sent_;
  AudioPlayer audio_;
  Renderer renderer_;
  std::thread worker_;
};
std::mutex mobile_mutex;
std::unique_ptr<MobileSession> mobile;
}
void MobileCommand(const std::string& text) {
  if(text.size()>1024*1024)return;
  std::lock_guard lock(mobile_mutex);
  if(!mobile)mobile=std::make_unique<MobileSession>();
  mobile->Command(text);
}
std::string MobilePoll() { std::lock_guard lock(mobile_mutex); return mobile?mobile->Poll():"[]"; }
void MobileShutdown() { std::lock_guard lock(mobile_mutex); mobile.reset(); }
}
