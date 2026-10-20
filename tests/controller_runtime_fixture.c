/* SPDX-License-Identifier: GPL-3.0-or-later
 * Compile the real native pad sampler into a process-local fixture. */
#include "../src/runtime_pad.c"
uint64_t host_monotonic_ns(void){return SDL_GetTicksNS();}
uintptr_t runtime_lookup(const RuntimeExport *table,size_t count,const char *name){
    for(size_t i=0;i<count;i++)if(!strcmp(table[i].name,name))return (uintptr_t)table[i].function;
    return 0;
}
int fixture_pad_open(void){return pad_init()==0 ? pad_open(1,0,0,NULL) : -1;}
int fixture_pad_read(BbPcPad *out){
    PadData data;if(pad_read_state(1,&data))return -1;
    *out=(BbPcPad){.buttons=data.buttons,.lx=data.left_x,.ly=data.left_y,.rx=data.right_x,.ry=data.right_y,.l2=data.l2,.r2=data.r2,.touch_count=data.touch_count};
    for(int i=0;i<data.touch_count&&i<2;i++){out->touch[i].x=data.touches[i].x;out->touch[i].y=data.touches[i].y;out->touch[i].id=data.touches[i].id;}
    return 0;
}
int fixture_pad_vibrate(void){
    if(!gamepad||strcmp(SDL_GetGamepadName(gamepad),"Application-local controller regression"))return -1;
    const uint8_t motors[2]={100,150};return pad_vibration(1,motors);
}
void fixture_pad_close(void){
    pad_close(1);if(gamepad){SDL_CloseGamepad(gamepad);gamepad=NULL;}
    for(int i=0;i<MAX_OTHER_PADS;i++)if(other_pads[i]){SDL_CloseGamepad(other_pads[i]);other_pads[i]=NULL;}
}
