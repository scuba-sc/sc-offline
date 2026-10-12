// SPDX-License-Identifier: GPL-3.0-only
#include "titanlink.h"
extern "C" SCO_EXPORT const sco_plugin_info* sco_plugin_query(){return TitanLinkQuery();}
extern "C" SCO_EXPORT sco_result sco_plugin_load(const sco_api* a,sco_plugin* s){return TitanLinkLoad(a,s);}
extern "C" SCO_EXPORT void sco_plugin_unload(){TitanLinkUnload();}
