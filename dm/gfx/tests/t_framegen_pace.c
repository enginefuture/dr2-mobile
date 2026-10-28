#include "../nvn/nvn_pace.h"
#include <assert.h>
#include <stdio.h>
int main(void){
 uint64_t periods[]={0,8333333,16666666,33333333,50000000};
 for(unsigned k=0;k<5;k++){assert(nvn_framegen_period(periods[k],0)==periods[k]);assert(nvn_framegen_period(periods[k],1)>=33333333);}
 for(unsigned k=0;k<3;k++){
  uint64_t period=nvn_framegen_period(periods[k],1),now=1000000000,first=now,last=0;nvn_pacer p={0};
  for(unsigned frame=0;frame<=900;frame++){uint64_t slot=now+nvn_pace(&p,now,period);if(last)assert(slot-last==33333333);last=slot;now=slot+(frame%16)*1000000;}
  assert(last-first>=29999999000ull); // 900 intervals stay 30 seconds at any display/profiling tier.
 }
 assert(nvn_framegen_period(50000000,1)==50000000); // A deliberately slower game interval is preserved.
 puts("FRAMEGEN_PACE PASS: independent real30 cap, 120/60/uncapped sources, jitter and slower intervals");
}
