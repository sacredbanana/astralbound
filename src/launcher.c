/* SPDX-License-Identifier: Unlicense; keyboard, pointer and pad share actions. */
#include "launcher.h"
#include <stdio.h>
#include <string.h>
void launcher_init(Launcher *m, const Settings *s, int aga, int rtg) {
    memset(m,0,sizeof(*m)); m->settings=*s; m->available_aga=aga; m->available_rtg=rtg; m->test_scene=s->test_scene-1;
    if(m->settings.mode==VIDEO_AGA && !aga) m->settings.mode=VIDEO_ECS;
    if(m->settings.mode>=VIDEO_RTG && !rtg) m->settings.mode=VIDEO_ECS;
}
int launcher_rows(const Launcher *m) { return m->page==0 ? 14 : m->page==1 ? 9 : 3; }
void launcher_row(const Launcher *m, int row, char *name, size_t size, char *value, size_t value_size) {
    static const char *names[]={"START GAME","VIDEO OUTPUT","RENDERER","BEZIER RINGS","DISPLAY SIZE","GRAPHICS PRESET","CUSTOM OPTIONS","TEST SCENE","RUN PERFORMANCE TEST","TEST RESULTS","PLAY INTRO","SOUND","SAVE SETTINGS","QUIT"};
    static const char *sizes[]={"320 X 240","640 X 480","800 X 600","1024 X 768"};
    name[0]=value[0]=0;
    if(m->page==0) {
        snprintf(name,size,"%s",names[row]);
        switch(row) {
        case 1: snprintf(value,value_size,"%s",video_names[m->settings.mode]); break;
        case 2: snprintf(value,value_size,"%s",renderer_names[m->settings.renderer]); break;
        case 3: snprintf(value,value_size,"%s",m->settings.bezier ? "ON" : "OFF"); break;
        case 4: snprintf(value,value_size,"%s",m->settings.mode>=VIDEO_RTG ? sizes[m->settings.rtg_size] : m->settings.compact ? "320 X 200" : "NATIVE PAL/NTSC"); break;
        case 5: snprintf(value,value_size,"%s",quality_names[m->settings.preset]); break;
        case 6: snprintf(value,value_size,"%s",m->settings.preset==QUALITY_CUSTOM ? "OPEN" : "SELECT CUSTOM FIRST"); break;
        case 7: snprintf(value,value_size,"%s",m->test_scene<0 ? "ALL SCENES" : performance_names[m->test_scene]); break;
        case 10: snprintf(value,value_size,"%s",m->settings.intro ? "ON" : "OFF"); break;
        case 11: snprintf(value,value_size,"%s",m->settings.sound ? "ON" : "OFF"); break;
        }
    } else if(m->page==1) {
        if(row<6) {
            int option=m->group*6+row;
            snprintf(name,size,"%s",graphics_options[option].name);
            snprintf(value,value_size,"%s",graphics_options[option].values[graphics_value(&m->settings.gfx,option)]);
        } else snprintf(name,size,"%s",row==6 ? "PREVIOUS PAGE" : row==7 ? "NEXT PAGE" : "BACK");
    } else snprintf(name,size,"%s",row==0 ? "PREVIOUS PAGE" : row==1 ? "NEXT PAGE" : "BACK");
}
int launcher_key(Launcher *m, int key) {
    int direction=key==LAUNCH_LEFT ? -1 : 1;
    if(key==27) { if(m->page) { m->page=m->selected=m->group=0; return 0; } return LAUNCH_QUIT; }
    if(key==LAUNCH_UP || key==LAUNCH_DOWN) { m->selected=(m->selected+launcher_rows(m)+(key==LAUNCH_UP ? -1 : 1))%launcher_rows(m); return 0; }
    if(key!='\r' && key!='\n' && key!=' ' && key!=LAUNCH_LEFT && key!=LAUNCH_RIGHT) return 0;
    m->message[0]=0;
    if(m->page==1) {
        if(m->selected<6) graphics_change(&m->settings,m->group*6+m->selected,direction);
        else if(m->selected==8) { m->page=0; m->selected=6; m->group=0; }
        else m->group=(m->group+(m->selected==6 ? 2 : 1))%3;
    } else if(m->page==2) {
        if(m->selected==2) { m->page=0; m->selected=9; m->group=0; }
        else m->group=(m->group+(m->selected==0 ? 2 : 1))%3;
    } else switch(m->selected) {
    case 0: return LAUNCH_PLAY;
    case 1: do { m->settings.mode=(m->settings.mode+4+direction)%4; } while((m->settings.mode==VIDEO_AGA && !m->available_aga) || (m->settings.mode>=VIDEO_RTG && !m->available_rtg)); break;
    case 2: m->settings.renderer^=1; break;
    case 3:
        m->settings.bezier^=1;
        break;
    case 4: if(m->settings.mode>=VIDEO_RTG) m->settings.rtg_size=(m->settings.rtg_size+4+direction)%4; else m->settings.compact^=1; break;
    case 5: settings_preset(&m->settings,(m->settings.preset+5+direction)%5); break;
    case 6: if(m->settings.preset==QUALITY_CUSTOM) { m->page=1; m->selected=m->group=0; } else snprintf(m->message,sizeof(m->message),"CHOOSE THE CUSTOM PRESET TO EDIT DETAIL"); break;
    case 7: m->test_scene=(m->test_scene+PERFORMANCE_SCENES+2+direction)%(PERFORMANCE_SCENES+1)-1; m->settings.test_scene=m->test_scene+1; break;
    case 8: return LAUNCH_TEST;
    case 9: m->page=2; m->selected=m->group=0; break;
    case 10: m->settings.intro^=1; break;
    case 11: m->settings.sound^=1; break;
    case 12: return 4; /* platform saves while the OS is available */
    case 13: return LAUNCH_QUIT;
    }
    return 0;
}
int launcher_pointer(Launcher *m, int x, int y, int down) {
    int result=0, row=(y-LAUNCHER_ROW_TOP)/LAUNCHER_ROW_PITCH;
    if(down && !m->down && x>=6 && x<314 && y>=LAUNCHER_ROW_TOP && row<launcher_rows(m)) {
        m->selected=row;
        result=launcher_key(m,x<160 ? LAUNCH_LEFT : LAUNCH_RIGHT);
    }
    m->down=down; return result;
}
void launcher_results(Launcher *m, const PerformanceReport *report) {
    m->previous=m->latest; m->latest=*report; m->page=2; m->selected=m->group=0;
}
