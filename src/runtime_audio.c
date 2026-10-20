/* libSceAudioOut on SDL3 audio streams. sceAudioOutOutput returns once per
 * buffer period on a steady clock, like PS4 hardware: FMOD's output thread
 * reads 256-frame slots out of its mixer's 512-frame ring, and returning in
 * bursts (whenever the device pulls a quantum) lets it overtake the mixer and
 * play stale halves - a click every 10.7 ms. The lowest SDL queue level of
 * each window is steered to two buffers (silence prefill on underrun, cadence
 * corrections), which also absorbs device clock drift. Without an audio device (headless runs, BB_AUDIO=none)
 * ports only follow the clock.
 * bbport: some devices take noticeably less than 48 kHz of what they are given (89-91% seen on
 * one PC): the correction is proportional (up to 25% slower) instead of a fixed 3%, which let
 * the queue - the sound's delay - grow by a second every minute, and a queue far over the
 * target is dropped back to it at once.
 * bbport: the device's own thread (SDL's mixer) is watched too: a postmix callback counts what it
 * writes and when (BB_AUDIO_STATS) - a thread that comes late is a gap in the sound - and
 * BB_AUDIO_THREAD_PRIORITY raises it when the system left it at normal priority. */
#define _GNU_SOURCE
#include "runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <SDL3/SDL.h>
#include "guard_sound.h"

#define PORTS 25
#define ERR_NOT_OPENED ((int32_t)0x80260001)
#define ERR_INVALID_PORT ((int32_t)0x80260003)
#define ERR_INVALID_POINTER ((int32_t)0x80260004)
#define ERR_PORT_FULL ((int32_t)0x80260005)
#define ERR_INVALID_SIZE ((int32_t)0x80260006)
#define ERR_INVALID_FORMAT ((int32_t)0x80260007)
#define ERR_INVALID_FREQ ((int32_t)0x80260008)
#define ERR_INVALID_VOLUME ((int32_t)0x80260009)
#define ERR_INVALID_TYPE ((int32_t)0x8026000A)
#define ERR_ALREADY_INIT ((int32_t)0x8026000E)
#define ERR_NOT_INIT ((int32_t)0x8026000F)
#define VOLUME_0DB 32768

typedef struct {
    int used, type, channels, is_float, frames, sample_bytes, std_layout;
    int out_channels;               /* what the stream takes: 2 when 7.1 is folded down here */
    int32_t volume[8];
    SDL_AudioStream *stream;
    uint64_t next_deadline_ns; /* next return of sceAudioOutOutput */
    int64_t adjust_ns; int window_min, window_count; /* queue level control */
    uint64_t last_output_us;
    HostMutex lock;
    FILE *dump;                     /* BB_AUDIO_DUMP: raw converted PCM per port */
    int stats;
    uint64_t stat_start_ns, stat_last_ns, stat_max_gap_ns; /* BB_AUDIO_STATS */
    unsigned stat_starved, stat_buffers; int stat_min_queued;
    int stat_start_queued; uint64_t stat_bytes; /* the device's rate: what it took of what was given */
    unsigned resets;                /* queues dropped back to the target */
} Port;
typedef struct { uint16_t output; uint8_t channel, reserved; int16_t volume; uint16_t reroute; uint64_t flag, reserved64[2]; } PortState;
_Static_assert(sizeof(PortState)==32,"AudioOut port state layout");

static HostMutex table_lock=HOST_MUTEX_INIT;
static Port ports[PORTS];
static int initialized, sdl_ready=-1;
static size_t buffers_out, ports_opened;

static uint64_t now_ns(void) { return host_monotonic_ns(); }
static void sleep_until(uint64_t deadline) { host_sleep_until_ns(deadline); }
static struct { float *samples[2]; size_t count[2]; GuardMixer mixer; unsigned pending,normal,perfect; int loaded; } guard_audio;
/* Called by the committed native block observer; no allocation, file IO or audio lock. */
void runtime_audio_guard_event(int perfect,int volume) {
    if(volume<=0)return;
    const unsigned event=(unsigned)(volume>100?100:volume)<<2 | (perfect?2u:1u);
    __atomic_store_n(&guard_audio.pending,event,__ATOMIC_RELEASE);
}
static void guard_load(void) {
    if(guard_audio.loaded)return;
    guard_audio.loaded=1;
    const char *assets=getenv("BB_PC_MENU_ASSETS");
    if(!assets)return;
    for(int i=0;i<2;++i){char path[4096];size_t bytes=0;
        snprintf(path,sizeof path,"%s/../guard/%s.f32",assets,i?"perfect":"normal");
        float *data=SDL_LoadFile(path,&bytes);
        if(data && bytes%sizeof(float)==0 && bytes<=48000u*4u*3u){guard_audio.samples[i]=data;guard_audio.count[i]=bytes/sizeof(float);}
        else SDL_free(data);
    }
    printf("Runtime: CC0 guard audio preloaded normal=%zu perfect=%zu samples at 48000Hz\n",guard_audio.count[0],guard_audio.count[1]);
}
static void guard_mix(Port *p,unsigned char *converted) {
    if(p!=&ports[0] || p->out_channels<2)return;
    const unsigned event=__atomic_exchange_n(&guard_audio.pending,0,__ATOMIC_ACQUIRE);
    if(event){const unsigned kind=(event&3u)==2u?1u:0u;
        guard_voice_start(&guard_audio.mixer,guard_audio.samples[kind],guard_audio.count[kind],(float)(event>>2)/100.0f);
        if(guard_audio.count[kind]){if(kind)++guard_audio.perfect;else ++guard_audio.normal;
            printf("Runtime: CC0 guard audio mixed %s (normal=%u perfect=%u)\n",kind?"perfect":"normal",guard_audio.normal,guard_audio.perfect);}
    }
    for(int f=0;f<p->frames;++f){const float hit=guard_voice_next(&guard_audio.mixer);if(hit==0)continue;
        for(int c=0;c<2;++c){const size_t at=(size_t)f*p->out_channels+c;
            const float gain=(float)p->volume[c]/VOLUME_0DB;
            if(p->is_float){float v;memcpy(&v,converted+at*4,4);v=guard_limit(v+hit*gain);memcpy(converted+at*4,&v,4);}
            else {int16_t v;memcpy(&v,converted+at*2,2);const float x=guard_limit((float)v/32768.0f+hit*gain);v=(int16_t)(x*32767);memcpy(converted+at*2,&v,2);}
        }
    }
}
static int sdl_audio(void) {
    if (sdl_ready<0) {
        const char *mode=getenv("BB_AUDIO");
        sdl_ready = (!mode || strcmp(mode,"none")) && SDL_InitSubSystem(SDL_INIT_AUDIO);
        printf("Runtime: audio backend %s\n", sdl_ready ? SDL_GetCurrentAudioDriver() : "timer (silent)");
        guard_load();
    }
    return sdl_ready;
}
/* As runtime_thread.c reads it: off only when it starts with 0. */
static int audio_priority(void) { const char *e=getenv("BB_AUDIO_THREAD_PRIORITY"); return !(e && e[0]=='0'); }
/* The device thread, seen from the first port's postmix callback: written frames, mixes and
   the longest wait between two, read and reset by that port's statistics. */
static struct { uint64_t last_ns, max_gap_ns, frames; unsigned calls; int freq, seen; } device_thread;
static Port *device_port;
static void SDLCALL device_mixed(void *userdata, const SDL_AudioSpec *spec, float *buffer, int buflen) {
    (void)userdata; (void)buffer;
    const uint64_t now=now_ns(), last=__atomic_load_n(&device_thread.last_ns,__ATOMIC_RELAXED);
    if (last && now-last>__atomic_load_n(&device_thread.max_gap_ns,__ATOMIC_RELAXED))
        __atomic_store_n(&device_thread.max_gap_ns,now-last,__ATOMIC_RELAXED);
    __atomic_store_n(&device_thread.last_ns,now,__ATOMIC_RELAXED);
    __atomic_add_fetch(&device_thread.calls,1u,__ATOMIC_RELAXED);
    __atomic_add_fetch(&device_thread.frames,(uint64_t)buflen/((uint64_t)spec->channels*sizeof(float)),__ATOMIC_RELAXED);
    if (!device_thread.seen) {
        device_thread.seen=1;
#ifdef _WIN32
        /* WASAPI asks Windows for "Pro Audio" scheduling (MMCSS); where that is missing the
           thread runs at normal priority, behind the game's busy threads. */
        const int before=GetThreadPriority(GetCurrentThread());
        const int raise=before<THREAD_PRIORITY_HIGHEST && audio_priority();
        if (raise) SDL_SetCurrentThreadPriority(SDL_THREAD_PRIORITY_TIME_CRITICAL);
        printf("Runtime: audio device thread priority %d%s\n",before,raise ? ", raised to time-critical" : "");
#endif
    }
}
static void watch_device(Port *p) {
    if (device_port) return;
    device_port=p;
    const SDL_AudioDeviceID id=SDL_GetAudioStreamDevice(p->stream);
    SDL_AudioSpec spec; int frames=0;
    if (SDL_GetAudioDeviceFormat(id,&spec,&frames)) {
        device_thread.freq=spec.freq;
        printf("Runtime: audio output %s: %d Hz, %d channels, %d frames per write (%.1f ms; the game: 48000 Hz, converted)\n",
               SDL_GetCurrentAudioDriver(),spec.freq,spec.channels,frames,spec.freq ? 1000.0*frames/spec.freq : 0.0);
    }
    if ((p->stats || audio_priority()) && !SDL_SetAudioPostmixCallback(id,device_mixed,NULL))
        fprintf(stderr,"Runtime: audio postmix callback failed (%s)\n",SDL_GetError());
}
static int port_range(int type, int *first, int *last) {
    switch (type) {
    case 0: *first=0; *last=7; return 1;      /* main */
    case 1: *first=8; *last=8; return 1;      /* bgm */
    case 2: *first=9; *last=12; return 1;     /* voice */
    case 3: *first=13; *last=16; return 1;    /* personal */
    case 4: *first=17; *last=20; return 1;    /* pad speaker */
    case 126: *first=21; *last=22; return 1;  /* audio3d */
    case 127: *first=23; *last=24; return 1;  /* aux */
    default: return 0;
    }
}
static Port *port_of(int32_t handle, int32_t *error) {
    int id=handle & 0xff;
    if ((handle & 0x3f000000)!=0x20000000 || id>=PORTS) { *error=ERR_INVALID_PORT; return NULL; }
    if (!ports[id].used) { *error=ERR_NOT_OPENED; return NULL; }
    return &ports[id];
}

static ABI int32_t audio_init(void) {
    host_lock(&table_lock);
    int32_t r=initialized ? ERR_ALREADY_INIT : 0;
    initialized=1;
    host_unlock(&table_lock);
    return r;
}
static ABI int32_t audio_open(int32_t user, int32_t type, int32_t index, uint32_t length, uint32_t freq, uint32_t param) {
    (void)user; (void)index;
    if (!initialized) return ERR_NOT_INIT;
    if (!length || length>2048 || (length & 0xff)) return ERR_INVALID_SIZE;
    if (freq!=48000) return ERR_INVALID_FREQ;
    uint32_t format=param & 0xff;
    if (format>7) return ERR_INVALID_FORMAT;
    int first, last;
    if (!port_range(type,&first,&last)) return ERR_INVALID_TYPE;
    static const int channels[8]={1,2,8,1,2,8,8,8};
    host_lock(&table_lock);
    int id=-1;
    for (int i=first;i<=last;++i) if (!ports[i].used) { id=i; break; }
    if (id<0) { host_unlock(&table_lock); return ERR_PORT_FULL; }
    Port *p=&ports[id];
    memset(p,0,sizeof(*p));
    host_mutex_init(&p->lock);
    p->used=1; p->type=type; p->channels=channels[format]; p->is_float=format>=3 && format!=6;
    p->sample_bytes=p->is_float ? 4 : 2; p->frames=(int)length; p->std_layout=format>=6;
    for (int c=0;c<8;++c) p->volume[c]=VOLUME_0DB;
    p->out_channels=p->channels;
    if (sdl_audio()) {
        /* bbport: 7.1 on a stereo device is folded down here, the front pair at full level (ITU:
           centre and surrounds at -3 dB, LFE left out). SDL's own 7.1-to-stereo matrix scales
           every channel by about 0.21 so that nothing can clip; with most of the game's sound in
           the front pair that played it about 13 dB quieter than shadPS4 (#27).
           BB_AUDIO_SDL_DOWNMIX=1 leaves it to SDL again. */
        SDL_AudioSpec device; int device_frames=0;
        const char *sdl_downmix=getenv("BB_AUDIO_SDL_DOWNMIX");
        if (p->channels==8 && !(sdl_downmix && sdl_downmix[0]=='1') &&
            SDL_GetAudioDeviceFormat(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,&device,&device_frames) &&
            device.channels==2)
            p->out_channels=2;
        SDL_AudioSpec spec={p->is_float ? SDL_AUDIO_F32 : SDL_AUDIO_S16, p->out_channels, 48000};
        p->stream=SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,&spec,NULL,NULL);
        if (p->stream) SDL_ResumeAudioStreamDevice(p->stream);
        else fprintf(stderr,"Runtime: SDL audio stream failed (%s); port %d uses the timer sink\n",SDL_GetError(),id);
    }
    p->stats=getenv("BB_AUDIO_STATS")!=NULL;
    if (p->stream) watch_device(p);
    const char *dump=getenv("BB_AUDIO_DUMP");
    if (dump) {
        char path[4096];
        snprintf(path,sizeof(path),"%s.port%d.%dch.%s",dump,id,p->out_channels,p->is_float ? "f32" : "s16");
        p->dump=fopen(path,"wb");
    }
    ++ports_opened;
    host_unlock(&table_lock);
    printf("Runtime: audio port %d opened (type %d, %d ch, %s, %u frames%s)\n",id,type,p->channels,p->is_float ? "float" : "s16",length,
           p->out_channels!=p->channels ? ", folded to stereo" : "");
    return (type<<16) | id | 0x20000000;
}
static ABI int32_t audio_close(int32_t handle) {
    host_lock(&table_lock);
    int32_t error=0;
    Port *p=port_of(handle,&error);
    if (p) {
        host_lock(&p->lock);
        if (p->stream) SDL_DestroyAudioStream(p->stream);
        if (p->dump) fclose(p->dump);
        p->dump=NULL;
        p->stream=NULL; p->used=0;
        host_unlock(&p->lock);
    }
    host_unlock(&table_lock);
    return p ? 0 : error;
}
/* `pace`: wait for this port's next period. sceAudioOutOutputs waits once for all its ports. */
static int32_t output_port(int32_t handle, const void *data, int pace) {
    int32_t error=0;
    host_lock(&table_lock);
    Port *p=port_of(handle,&error);
    host_unlock(&table_lock);
    if (!p) return error;
    host_lock(&p->lock);
    size_t samples=(size_t)p->frames*(size_t)p->out_channels, bytes=samples*(size_t)p->sample_bytes;
    uint64_t period=(uint64_t)p->frames*1000000000u/48000u;
    if (data) {
        /* Apply per-channel volume and map PS4 8ch (L R C LFE SL SR BL BR) to SDL 7.1 order. */
        unsigned char converted[2048*8*4];
        static const int remap[8]={0,1,2,3,6,7,4,5};
        for (size_t f=0;f<(size_t)p->frames;++f) for (int c=0;c<p->channels;++c) {
            int target=p->channels==8 && !p->std_layout ? remap[c] : c;
            size_t from=f*(size_t)p->channels+(size_t)c, to=f*(size_t)p->channels+(size_t)target;
            float gain=(float)p->volume[c]/VOLUME_0DB;
            if (p->is_float) { float v; memcpy(&v,(const char *)data+from*4,4); v*=gain; memcpy(converted+to*4,&v,4); }
            else { int16_t v; memcpy(&v,(const char *)data+from*2,2); v=(int16_t)((float)v*gain); memcpy(converted+to*2,&v,2); }
        }
        if (p->out_channels==2 && p->channels==8) {
            /* SDL's 7.1 order by now (FL FR FC LFE BL BR SL SR); each stereo frame is written over
               the start of the buffer, behind the 7.1 frame it is made of. */
            const float k=0.70710678f;
            for (size_t f=0;f<(size_t)p->frames;++f) {
                float s[8];
                for (int c=0;c<8;++c) {
                    if (p->is_float) memcpy(&s[c],converted+(f*8+(size_t)c)*4,4);
                    else { int16_t v; memcpy(&v,converted+(f*8+(size_t)c)*2,2); s[c]=(float)v; }
                }
                const float lr[2]={s[0]+k*(s[2]+s[4]+s[6]), s[1]+k*(s[2]+s[5]+s[7])};
                for (int c=0;c<2;++c) {
                    if (p->is_float) {
                        const float v=lr[c]>1.0f ? 1.0f : lr[c]<-1.0f ? -1.0f : lr[c];
                        memcpy(converted+(f*2+(size_t)c)*4,&v,4);
                    } else {
                        const float v=lr[c]>32767.0f ? 32767.0f : lr[c]<-32768.0f ? -32768.0f : lr[c];
                        const int16_t o=(int16_t)v; memcpy(converted+(f*2+(size_t)c)*2,&o,2);
                    }
                }
            }
        }
        guard_mix(p,converted);
        if (p->dump) fwrite(converted,1,bytes,p->dump);
        if (p->stats) {
            uint64_t now=now_ns();
            if (!p->stat_start_ns) {
                p->stat_start_ns=now; p->stat_min_queued=1<<30;
                p->stat_start_queued=p->stream ? SDL_GetAudioStreamQueued(p->stream) : 0; p->stat_bytes=0;
            }
            else if (now-p->stat_last_ns>p->stat_max_gap_ns) p->stat_max_gap_ns=now-p->stat_last_ns;
            p->stat_last_ns=now; ++p->stat_buffers; p->stat_bytes+=bytes;
            if (p->stream) {
                int queued=SDL_GetAudioStreamQueued(p->stream);
                if (queued<p->stat_min_queued) p->stat_min_queued=queued;
                if (queued==0) ++p->stat_starved;
            }
            if (now-p->stat_start_ns>=5000000000u) {
                /* What the device took in this window, against 48 kHz (100%: it keeps up). */
                const int queued_now=p->stream ? SDL_GetAudioStreamQueued(p->stream) : 0;
                const double taken=(double)p->stat_bytes-(double)(queued_now-p->stat_start_queued);
                const double nominal=(double)(now-p->stat_start_ns)/1e9*48000.0*p->out_channels*p->sample_bytes;
                printf("Audio stats port %d: %u buffers/5s (expected %u), max gap %.2f ms, min queued %d bytes, starved %u, "
                       "device took %.1f%% of 48 kHz, queue resets %u\n",
                       (int)(p-ports),p->stat_buffers,(unsigned)(5u*48000u/(unsigned)p->frames),p->stat_max_gap_ns/1e6,
                       p->stat_min_queued,p->stat_starved,p->stream ? 100.0*taken/nominal : 0.0,p->resets);
                if (p==device_port && device_thread.freq) {
                    const unsigned calls=__atomic_exchange_n(&device_thread.calls,0u,__ATOMIC_RELAXED);
                    const uint64_t frames=__atomic_exchange_n(&device_thread.frames,(uint64_t)0,__ATOMIC_RELAXED);
                    const uint64_t gap=__atomic_exchange_n(&device_thread.max_gap_ns,(uint64_t)0,__ATOMIC_RELAXED);
                    printf("Audio device thread: %u writes/5s, %.1f%% of %d Hz, max gap %.2f ms\n",calls,
                           100.0*(double)frames/((double)(now-p->stat_start_ns)/1e9*device_thread.freq),device_thread.freq,gap/1e6);
                }
                p->stat_start_ns=now; p->stat_max_gap_ns=0; p->stat_starved=0; p->stat_buffers=0; p->stat_min_queued=1<<30;
                p->stat_start_queued=queued_now; p->stat_bytes=0;
            }
        }
        if (pace) {
            uint64_t now=now_ns();
            if (!p->next_deadline_ns || now>p->next_deadline_ns+8*period) p->next_deadline_ns=now; /* start or stall: restart cadence */
            else sleep_until(p->next_deadline_ns);
            p->next_deadline_ns+=period;
        }
        if (p->stream) {
            /* The device drains the queue in quanta (21 ms on PipeWire), so the level
               is a sawtooth: its minimum over ~32 buffers is what gets controlled. */
            static const unsigned char silence[2048*8*4];
            int low=2*(int)bytes, queued=SDL_GetAudioStreamQueued(p->stream);
            if (queued<(int)bytes) {
                SDL_PutAudioStreamData(p->stream,silence,low-queued<(int)bytes ? low-queued : (int)bytes);
                queued=low;
            }
            /* Far over the target (a device taking less than the correction covers, or a burst):
               the delay would only grow. Back to the target at once: one skip, not seconds of lag. */
            if (queued>low+32*(int)bytes) {
                SDL_ClearAudioStream(p->stream);
                SDL_PutAudioStreamData(p->stream,silence,(int)bytes);
                SDL_PutAudioStreamData(p->stream,silence,(int)bytes);
                queued=low; p->window_count=0;
                if (++p->resets<=4)
                    printf("Runtime: audio port %d: the device took far less than it was given; queue dropped back to %d bytes\n",
                           (int)(p-ports),low);
            }
            if (!p->window_count || queued<p->window_min) p->window_min=queued;
            if (++p->window_count==32) {
                /* Proportional to the excess (a 32nd of a period per buffer of it, at most a quarter
                   slower): a device at 90% settles about 3.5 buffers over the target. */
                const int excess=p->window_min-(low+2*(int)bytes);
                int64_t adjust=excess>0 ? (int64_t)period*excess/(32*(int64_t)bytes) : p->window_min<low ? -(int64_t)(period/32) : 0;
                if (adjust>(int64_t)(period/4)) adjust=(int64_t)(period/4);
                p->adjust_ns=adjust;
                p->window_count=0;
            }
            p->next_deadline_ns+=(uint64_t)p->adjust_ns;
            SDL_PutAudioStreamData(p->stream,converted,(int)bytes);
        }
        ++buffers_out;
    }
    p->last_output_us=now_ns()/1000;
    host_unlock(&p->lock);
    return data ? (int32_t)samples : 0;
}
static ABI int32_t audio_output(int32_t handle, const void *data) { return output_port(handle,data,1); }
typedef struct { int32_t handle; const void *data; } OutputParam;
static ABI int32_t audio_outputs(const OutputParam *params, uint32_t count) {
    if (!params) return ERR_INVALID_POINTER;
    if (!count || count>PORTS) return ERR_PORT_FULL;
    int32_t result=0;
    /* One period per call for all ports (FMOD feeds main and BGM together): pacing every
       port would halve the rate and starve both. */
    for (uint32_t i=0;i<count;++i) { int32_t r=output_port(params[i].handle,params[i].data,i==0); if (r<0) return r; result=r; }
    return result;
}
static ABI int32_t audio_volume(int32_t handle, int32_t flags, const int32_t *volume) {
    int32_t error=0;
    host_lock(&table_lock);
    Port *p=port_of(handle,&error);
    host_unlock(&table_lock);
    if (!p) return error;
    if (!volume) return ERR_INVALID_POINTER;
    host_lock(&p->lock);
    for (int c=0;c<8;++c) if (flags & (1<<c)) {
        if (volume[c]<0 || volume[c]>VOLUME_0DB) { host_unlock(&p->lock); return ERR_INVALID_VOLUME; }
        p->volume[c]=volume[c];
    }
    host_unlock(&p->lock);
    return 0;
}
static ABI int32_t audio_state(int32_t handle, PortState *state) {
    int32_t error=0;
    host_lock(&table_lock);
    Port *p=port_of(handle,&error);
    host_unlock(&table_lock);
    if (!p) return error;
    if (!state) return ERR_INVALID_POINTER;
    memset(state,0,sizeof(*state));
    switch (p->type) {
    case 2: case 3: state->output=0x40; state->channel=1; break;          /* headphone */
    case 4: state->output=0x04; state->channel=1; state->volume=127; break; /* pad speaker */
    default: state->output=0x01; state->channel=(uint8_t)(p->channels>2 ? 2 : p->channels); break;
    }
    return 0;
}
static ABI int32_t audio_last_time(int32_t handle, uint64_t *time) {
    int32_t error=0;
    host_lock(&table_lock);
    Port *p=port_of(handle,&error);
    host_unlock(&table_lock);
    if (!p) return error;
    if (!time) return ERR_INVALID_POINTER;
    *time=p->last_output_us; return 0;
}

static const RuntimeExport exports[]={
    {"sceAudioOutInit",audio_init}, {"sceAudioOutOpen",audio_open}, {"sceAudioOutClose",audio_close},
    {"sceAudioOutOutput",audio_output}, {"sceAudioOutOutputs",audio_outputs},
    {"sceAudioOutSetVolume",audio_volume}, {"sceAudioOutGetPortState",audio_state},
    {"sceAudioOutGetLastOutputTime",audio_last_time},
};
uintptr_t runtime_audio_resolve(const char *name) { return RUNTIME_LOOKUP(exports,name); }
void runtime_audio_report(void) { printf("Runtime: audio ports opened=%zu, buffers output=%zu\n",ports_opened,buffers_out); }
