// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <sc_titanlink.h>
inline bool TlValidFrame(const tl_frame& fr) {
    if(fr.key_mode>TL_KEY_GUN || fr.width>8192 || fr.height>8192)return false;
    for(int i=0;i<2;++i)if(fr.handle[i]>0xFFFFFFFEull || (!fr.handle[i] && fr.tex_seq[i]))return false;
    if(fr.handle[0] && fr.handle[0]==fr.handle[1])return false;
    if(!fr.width || !fr.height)return !fr.width && !fr.height && !fr.handle[0] && !fr.handle[1];
    return true;
}
