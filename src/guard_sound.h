/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef BB_GUARD_SOUND_H
#define BB_GUARD_SOUND_H
#include <stddef.h>
enum { GUARD_VOICES=12 };
typedef struct { const float *samples; size_t count,cursor; float gain; } GuardVoice;
typedef struct { GuardVoice voices[GUARD_VOICES]; unsigned next; } GuardMixer;
static inline void guard_voice_start(GuardMixer *m,const float *s,size_t n,float gain) {
    if (!s || !n || gain<=0) return;
    unsigned at=m->next++%GUARD_VOICES;
    for(unsigned i=0;i<GUARD_VOICES;++i)if(m->voices[i].cursor>=m->voices[i].count){at=i;break;}
    const GuardVoice voice={s,n,0,gain};m->voices[at]=voice;
}
static inline float guard_voice_next(GuardMixer *m) {
    float v=0;
    for(unsigned i=0;i<GUARD_VOICES;++i){GuardVoice *a=&m->voices[i];if(a->cursor<a->count)v+=a->samples[a->cursor++]*a->gain;}
    return v;
}
static inline float guard_limit(float v) { return v>1?1:v< -1?-1:v; }
#endif
