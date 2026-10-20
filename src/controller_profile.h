/* SPDX-License-Identifier: GPL-3.0-or-later
 * Small portable profile shared by the startup editor and native SDL sampler. */
#ifndef BB_CONTROLLER_PROFILE_H
#define BB_CONTROLLER_PROFILE_H
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct {char guid[33],icons[16];int manual,deadzone,center[4];} BbControllerProfile;
static inline int bb_controller_guid_valid(const char *s) {
    if(!s || strlen(s)!=32)return 0;
    for(int i=0;i<32;i++)if(!((s[i]>='0'&&s[i]<='9')||(s[i]>='a'&&s[i]<='f')||(s[i]>='A'&&s[i]<='F')))return 0;
    return 1;
}
static inline int bb_controller_integer(const char *s,int low,int high,int fallback) {
    char *end;const long n=strtol(s,&end,10);return !*s||*end||n<low||n>high?fallback:(int)n;
}
static inline BbControllerProfile bb_controller_profile_load(const char *path) {
    BbControllerProfile p;memset(&p,0,sizeof p);strcpy(p.icons,"auto");
    if(!path||!*path)return p;
    size_t size=0;char *data=(char*)SDL_LoadFile(path,&size);
    if(!data){SDL_ClearError();return p;}
    if(size>8192){SDL_free(data);return p;}
    for(char *line=data,*next;line&&*line;line=next){
        next=strchr(line,'\n');if(next)*next++=0;
        char *cr=strchr(line,'\r');if(cr)*cr=0;
        char *value=strchr(line,'=');if(!value)continue;*value++=0;
        if(!strcmp(line,"preferred_guid")&&bb_controller_guid_valid(value))strcpy(p.guid,value);
        else if(!strcmp(line,"manual_mode"))p.manual=bb_controller_integer(value,0,1,0);
        else if(!strcmp(line,"deadzone"))p.deadzone=bb_controller_integer(value,0,30,0);
        else if(!strcmp(line,"pad_icons")&&(!strcmp(value,"auto")||!strcmp(value,"playstation")||!strcmp(value,"xbox")))strcpy(p.icons,value);
        else for(int a=0;a<4;a++){
            const char *keys[]={"center_lx","center_ly","center_rx","center_ry"};
            if(!strcmp(line,keys[a]))p.center[a]=bb_controller_integer(value,-8000,8000,0);
        }
    }
    SDL_free(data);return p;
}
static inline int bb_controller_profile_matches(const BbControllerProfile *p,SDL_JoystickID id) {
    if(!*p->guid)return 0;
    char guid[33];SDL_GUIDToString(SDL_GetJoystickGUIDForID(id),guid,sizeof guid);
    return SDL_strcasecmp(p->guid,guid)==0;
}
static inline Sint16 bb_controller_calibrated_axis(Sint16 value,int center,int deadzone) {
    if(!center&&!deadzone)return value;
    const float span=value>=center?32767.f-center:32768.f+center;
    float x=(value-center)/span;const float dz=deadzone*.01f;
    if(x>=-dz&&x<=dz)return 0;
    x=x>0?(x-dz)/(1.f-dz):(x+dz)/(1.f-dz);
    if(x>1)x=1;if(x<-1)x=-1;
    return (Sint16)(x*(x>=0?32767.f:32768.f));
}
static inline int bb_controller_load_mappings(const char *path) {
    if(!path||!*path)return 0;
    size_t bytes=0;void *data=SDL_LoadFile(path,&bytes);
    if(!data){SDL_ClearError();return 0;}
    if(bytes>(1u<<20)){SDL_free(data);return -1;}
    SDL_IOStream *io=SDL_IOFromConstMem(data,bytes);
    const int count=io?SDL_AddGamepadMappingsFromIO(io,true):-1;
    SDL_free(data);return count;
}
#endif
