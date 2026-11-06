// Manual simulator integration fixture. Generates pixels/PCM only; never captures a device.
// Run in a disposable directory: <binary> <server> <port> [av1]. Login is saved in fixture-login.
#include <minirtc.h>
#include <remote_action.h>
#include <stream_names.h>
#include <mobile_file_transfer_session.h>
#include <atomic>
#include <chrono>
#include <fstream>
#include <mutex>
#include <thread>
#include <vector>
#include <sys/stat.h>
using namespace crossdesk;
struct Fixture {
  MiniRtcPeerPtr* peer=nullptr; std::atomic<bool> connected{false}; std::atomic<int> selected{0};
  std::mutex mutex; std::vector<std::pair<std::string,std::string>> messages;
  std::string remote; std::atomic<int> signal{-1}; int keys=0,mouse=0,settings=0,clipboard=0;
  void Send(RemoteAction& a) {const auto s=a.to_json();MiniRtcSendReliableDataFrame(peer,s.data(),s.size(),kControlStream);}
};
int main(int argc,char**argv) {
  if(argc<3)return 2;umask(0077);
#if defined(__OHOS__)
  setenv("SSL_CERT_DIR","/etc/security/certificates",1);
#endif
  std::string login;{std::ifstream input("fixture-login");input>>login;}
  Fixture f;MiniRtcParams p{};snprintf(p.signal_server_ip,sizeof(p.signal_server_ip),"%s",argv[1]);p.signal_server_port=atoi(argv[2]);p.user_id=login.c_str();p.user_data=&f;
  p.turn_mode=MiniRtcTurnAutoUdpTcp;p.enable_srtp=true;p.av1_encoding=argc>3;p.video_frame_rate=30;p.video_quality=MiniRtcVideoQualityMedium;
  p.video_content_type=MiniRtcVideoContentType::ScreenContent;p.video_degradation_preference=MiniRtcVideoDegradationPreference::MaintainResolution;
  p.on_signal_status=[](MiniRtcSignalStatus s,const char*,size_t,void*u){static_cast<Fixture*>(u)->signal=s;};
  p.on_net_status_report=[](const char*id,size_t n,MiniRtcTraversalMode mode,const MiniRtcNetTrafficStats*,const char*,size_t,void*){
    if(mode==MiniRtcTraversalUnknown&&id&&n&&std::string(id,n).find('@')!=std::string::npos){std::ofstream out("fixture-login");out.write(id,n);}
  };
  p.on_connection_status=[](MiniRtcConnectionStatus s,const char*id,size_t n,void*u){auto*f=static_cast<Fixture*>(u);std::lock_guard lock(f->mutex);f->remote.assign(id,n);f->connected=s==MiniRtcConnectionConnected;if(s==MiniRtcConnectionConnected)f->selected=0;};
  p.on_receive_data_buffer=[](const char*d,size_t n,const char*,size_t,const char*s,size_t l,void*u){auto*f=static_cast<Fixture*>(u);std::lock_guard lock(f->mutex);f->messages.emplace_back(std::string(s,l),std::string(d,n));};
  f.peer=MiniRtcCreatePeer(&p);MiniRtcAddVideoStream(f.peer,"Display1");MiniRtcAddVideoStream(f.peer,"Display2");MiniRtcAddAudioStream(f.peer,kAudioStream);MiniRtcAddDataStream(f.peer,kDataStream,false);
  for(auto s:{kKeyboardStream,kMouseStream,kControlStream,kClipboardStream,kFileStream,kFileFeedbackStream})MiniRtcAddDataStream(f.peer,s,true);
  if(MiniRtcInit(f.peer))return 3;
  mobile_controller::FileTransferSession files([&](const char*s,const char*b,size_t n){return MiniRtcSendReliableDataFrame(f.peer,b,n,s);},
    [](const std::string&,double p,bool,const std::string&path){if(p==1&&!path.empty())std::ofstream("received-result")<<path;});
  files.SetDirectory("received");bool info=false;int iteration=0;
  std::vector<uint8_t> pixels(640*360*3/2,128);int16_t pcm[480]{};
  const auto end=std::chrono::steady_clock::now()+std::chrono::minutes(15);
  while(std::chrono::steady_clock::now()<end){
    std::vector<std::pair<std::string,std::string>> messages;{std::lock_guard lock(f.mutex);messages.swap(f.messages);}
    if(f.connected&&!info){auto a=MakeHostInformation("Synthetic mobile fixture",{{"Test pattern",0,0,640,360},{"Second display",0,0,640,360}},true,"1.6.1",HostPlatform::Windows);a.i.supports_video_settings=true;f.Send(a);FreeRemoteAction(a);info=true;
      const std::string clip="CrossDesk synthetic clipboard";MiniRtcSendReliableDataFrame(f.peer,clip.data(),clip.size(),kClipboardStream);
      {std::ofstream out("sample.txt");out<<"Synthetic mobile file transfer\n";}files.Start("sample.txt","fixture.txt");}
    if(!f.connected)info=false;
    for(auto&[stream,bytes]:messages){
      if(stream==kFileStream)files.Receive(bytes.data(),bytes.size());else if(stream==kFileFeedbackStream)files.Ack(bytes.data(),bytes.size());
      else if(stream==kClipboardStream){f.clipboard++;MiniRtcSendReliableDataFrame(f.peer,bytes.data(),bytes.size(),kClipboardStream);}
      else {RemoteAction a{};if(!a.from_json(bytes))continue;
        if(a.type==host_infomation)info=false;
        if(a.type==keyboard)f.keys++;if(a.type==mouse)f.mouse++;if(a.type==display_id)f.selected=a.d;
        if(a.type==video_settings){f.settings++;a.type=video_settings_status;a.vs.accepted=true;f.Send(a);}FreeRemoteAction(a);}
    }
    if(f.connected){
      files.Pump();MiniRtcAudioFrame audio{};audio.data=reinterpret_cast<const char*>(pcm);audio.size=sizeof(pcm);MiniRtcSendAudioFrame(f.peer,&audio,kAudioStream);
      if(iteration%3==0){for(int y=0;y<360;y++)for(int x=0;x<640;x++)pixels[y*640+x]=uint8_t(35+(x/80)*24+((y+iteration)%180<12?30:0));
        std::fill(pixels.begin()+640*360,pixels.end(),128);for(size_t i=640*360;i<pixels.size();i+=2){pixels[i]=f.selected?80:170;pixels[i+1]=f.selected?170:80;}
        MiniRtcVideoFrame frame{};frame.width=640;frame.height=360;frame.size=pixels.size();frame.data=reinterpret_cast<const char*>(pixels.data());
        frame.captured_timestamp=std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
        MiniRtcSendVideoFrame(f.peer,&frame,f.selected?"Display2":"Display1");}
    }
    if(iteration++%100==0)std::ofstream("fixture-status")<<"signal="<<f.signal<<" connected="<<f.connected<<" mouse="<<f.mouse<<" keys="<<f.keys<<" settings="<<f.settings<<" clipboard="<<f.clipboard<<" display="<<f.selected<<"\n";
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  MiniRtcDestroyPeer(&f.peer);
}
