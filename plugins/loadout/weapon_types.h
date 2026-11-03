// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <string_view>
namespace weapon_types {
enum Type {All,Rifle,Smg,Sniper,MachineGun,Shotgun,GrenadeLauncher,Crossbow,Pistol,Medical,Tool,Other,Count};
inline Type Classify(std::string_view c){
 const auto maker=c.find('_');if(maker==c.npos)return Other;c.remove_prefix(maker+1);
 const auto family=c.substr(0,c.find('_'));
 // Only the model family counts: a pistol's later "sniper" variant tag is not its type.
 if(family=="medgun")return Medical;
 if(family=="multitool"||family=="tractor"||c.find("salvage_repair_")==0)return Tool;
 if(family=="sniper")return Sniper;
 if(family=="rifle")return Rifle;
 if(family=="smg")return Smg;
 if(family=="lmg"||family=="hmg")return MachineGun;
 if(family=="shotgun")return Shotgun;
 if(family=="glauncher")return GrenadeLauncher;
 if(family=="crossbow")return Crossbow;
 if(family=="pistol")return Pistol;
 return Other;
}
inline bool Matches(Type t,std::string_view c){return t==All||Classify(c)==t;}
inline const char* Label(Type t){
 static const char* const names[]={"All types","Rifles","Submachine guns","Sniper rifles","Machine guns","Shotguns","Grenade launchers","Crossbows","Pistols","Medical devices","Tools","Other"};
 return (t>=All&&t<Count)?names[t]:names[All];
}
}
