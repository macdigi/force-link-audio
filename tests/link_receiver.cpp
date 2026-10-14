// Diagnostic receiver for integration tests; no audio-device or transport control.
#include <ableton/LinkAudio.hpp>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <thread>
#include <cmath>
int main(int argc,char**argv){
 const char* name=argc>1?argv[1]:"Force Main";int seconds=argc>2?atoi(argv[2]):45;
 FILE* raw=argc>3?fopen(argv[3],"wb"):nullptr;
 std::atomic<unsigned long long> frames{0},buffers{0},nonzero{0};std::atomic<int> peak{0};
 ableton::LinkAudio link(120,"Force Link verification");link.enable(true);link.enableLinkAudio(true);link.enableStartStopSync(false);
 std::unique_ptr<ableton::LinkAudioSource> source;
 for(int tick=0;tick<seconds*10;++tick){
  if(!source)for(auto& c:link.channels())if(c.name==name){
   printf("SUBSCRIBED %s / %s\n",c.peerName.c_str(),c.name.c_str());fflush(stdout);
   source=std::make_unique<ableton::LinkAudioSource>(link,c.id,[&](auto b){
    frames+=b.info.numFrames;++buffers;
    for(size_t i=0;i<b.info.numFrames*b.info.numChannels;++i){int v=std::abs((int)b.samples[i]);if(v)++nonzero;if(v>peak)peak=v;}
    if(raw)fwrite(b.samples,sizeof(int16_t),b.info.numFrames*b.info.numChannels,raw);
   });break;
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
 }
 source.reset();if(raw)fclose(raw);
 printf("RESULT frames=%llu buffers=%llu nonzero=%llu peak=%d\n",frames.load(),buffers.load(),nonzero.load(),peak.load());
 return frames>0 && nonzero>0?0:2;
}
