#include "suite_control.h"
#include "taisei_game.h"
void suite_init(suite_control *s,int available){
 *s=(suite_control){.mode=APP_DEMO,.selected=0,.demo_kind=DEMO_SPRITES,.count=640,
  .texture=1,.hardware=1,.effect=4,.alpha=192,.bg_mode=available?1:2,.native_available=available!=0};
}
int suite_command(suite_control *s,unsigned char c){
 if(c=='h'){s->hardware=1;return 1;}if(c=='c'){s->hardware=0;return 1;}
 if(c=='b'){s->bg_mode=(s->bg_mode+1)%3;if(s->bg_mode==1&&!s->native_available)s->bg_mode=2;return 1;}
 if(c>='1'&&c<='3'){s->demo_kind=c-'1';s->count=s->demo_kind==DEMO_SPRITES?640:64;s->effect=s->demo_kind==DEMO_SPRITES?4:1;return 1;}
 if(c>='4'&&c<='6'){s->texture=c-'3';s->demo_kind=DEMO_SPRITES;return 1;}
 if(c=='+'){s->count=s->count<=SH_BULLETS-32?s->count+32:SH_BULLETS;return 1;}
 if(c=='-'){s->count=s->count>=32?s->count-32:0;return 1;}
 if(c=='a'){s->effect=1+((s->effect-1)^2);return 1;}
 if(c=='k'){s->effect=1+((s->effect-1)^1);return 1;}
 if(c=='['){s->alpha=s->alpha>=16?s->alpha-16:0;return 1;}
 if(c==']'){s->alpha=s->alpha<=239?s->alpha+16:255;return 1;}
 return 0;
}
