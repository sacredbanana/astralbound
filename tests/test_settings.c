/* SPDX-License-Identifier: Unlicense */
#include "launcher.h"
#include "intro.h"
#include "ui.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static Game g;
static void persistence(void) {
    Settings original,loaded; int i; FILE *file;
    const char *path="build/settings-test.cfg";
    settings_defaults(&original,1); assert(original.mode==VIDEO_ECS && original.preset==QUALITY_LOW && original.compact);
#ifdef ASTRALBOUND_FPU
    assert(original.renderer==RENDERER_ENHANCED); /* FPU builds default to Enhanced */
#else
    assert(original.renderer==RENDERER_CLASSIC);  /* plain 68k builds default to Classic */
#endif
    settings_defaults(&loaded,0); assert(loaded.renderer==RENDERER_ENHANCED && loaded.bezier==1);
    original.renderer^=1; original.bezier=0; /* both choices persist */
    original.test_scene=11; original.mode=VIDEO_RTG16; original.rtg_size=3; original.intro=original.sound=0;
    for(i=0;i<GRAPHICS_OPTIONS;i++) { graphics_change(&original,i,1); graphics_change(&original,i,1); }
    assert(settings_save(&original,path)); settings_defaults(&loaded,0); assert(settings_load(&loaded,path));
    assert(!memcmp(&original,&loaded,sizeof(original)));
    original.gfx.scale=3; assert(settings_save(&original,path)); assert(settings_load(&loaded,path)); assert(loaded.gfx.scale==3);
    file=fopen(path,"w"); assert(file); fputs("ASTRALBOUND_SETTINGS 1\nmode 999\npreset 4\ngraphics_0 2147483647\ngraphics_2 -1\ngraphics_7 1\ngraphics_6 2garbage\nfuture_option 7\n",file); fclose(file);
    assert(settings_load(&loaded,path)); assert(loaded.mode==VIDEO_ECS && loaded.gfx.scale==0 && loaded.gfx.ports==1);
    file=fopen(path,"w"); assert(file); fputs("ASTRALBOUND_SETTINGS 77\n",file); fclose(file);
    original=loaded; assert(!settings_load(&loaded,path)); assert(!memcmp(&original,&loaded,sizeof(original)));
    assert(!settings_save(&loaded,"build/nonexistent-settings-directory/file.cfg"));
    remove(path);
    puts("PASS settings roundtrip, replacement, bounded custom values and future version rejection");
}
static void menus(void) {
    Settings s; Launcher m; int i;
    settings_defaults(&s,1); launcher_init(&m,&s,0,0);
    m.selected=1; launcher_key(&m,LAUNCH_RIGHT); assert(m.settings.mode==VIDEO_ECS);
    /* The renderer row toggles between Enhanced and Classic. */
    m.selected=2; { int r=m.settings.renderer; launcher_key(&m,LAUNCH_RIGHT); assert(m.settings.renderer==!r);
        launcher_key(&m,LAUNCH_LEFT); assert(m.settings.renderer==r); }
    { char name[32],value[32]; launcher_row(&m,2,name,sizeof(name),value,sizeof(value));
      assert(!strcmp(name,"RENDERER") && !strcmp(value,renderer_names[m.settings.renderer])); }
    /* Curves are explicit, independent of presets, and scoped to Classic. */
    m.settings.renderer=RENDERER_CLASSIC; m.selected=3;
    assert(m.settings.bezier==1); launcher_key(&m,LAUNCH_RIGHT); assert(!m.settings.bezier);
    settings_preset(&m.settings,QUALITY_ULTRA); assert(!m.settings.bezier);
    settings_preset(&m.settings,QUALITY_LOW);
    launcher_pointer(&m,200,LAUNCHER_ROW_TOP+3*LAUNCHER_ROW_PITCH+1,1);
    assert(m.selected==3 && m.settings.bezier); launcher_pointer(&m,200,0,0);
    m.settings.renderer=RENDERER_ENHANCED; launcher_key(&m,LAUNCH_RIGHT);
    assert(m.settings.bezier && m.message[0]);
    { char name[32],value[32]; launcher_row(&m,3,name,sizeof(name),value,sizeof(value));
      assert(!strcmp(name,"BEZIER RINGS") && !strcmp(value,"CLASSIC ONLY")); }
    assert(launcher_rows(&m)==14);
    m.selected=6; launcher_key(&m,'\r'); assert(m.page==0); /* Presets gate Custom. */
    m.selected=5; for(i=0;i<4;i++) launcher_key(&m,LAUNCH_RIGHT); assert(m.settings.preset==QUALITY_CUSTOM);
    m.selected=6; launcher_key(&m,'\r'); assert(m.page==1);
    for(i=0;i<GRAPHICS_OPTIONS;i++) {
        int value=graphics_value(&m.settings.gfx,i);
        m.group=i/6; m.selected=i%6; launcher_key(&m,LAUNCH_RIGHT);
        assert(graphics_value(&m.settings.gfx,i)==(value+1)%(graphics_options[i].maximum+1));
    }
    launcher_key(&m,27); assert(m.page==0);
    assert(launcher_pointer(&m,200,49,1)==LAUNCH_PLAY);
    assert(launcher_pointer(&m,200,49,1)==0); launcher_pointer(&m,200,49,0);
    m.selected=7; m.test_scene=-1; launcher_key(&m,LAUNCH_RIGHT); assert(m.test_scene==0);
    launcher_key(&m,LAUNCH_LEFT); assert(m.test_scene==-1);
    puts("PASS capability filtering, Custom gating, every option and pointer edge activation");
}
static void rendering(void) {
    Settings s; Frame f={320,200,0,NULL,{0}}; int preset,scene,i,w,h;
    f.pixels=malloc(1024*768+16); assert(f.pixels);
    for(preset=0;preset<4;preset++) {
        settings_defaults(&s,1); settings_preset(&s,preset); render_graphics(&s.gfx);
        assert(render_init(&f));
        for(scene=0;scene<PERFORMANCE_SCENES;scene++) {
            unsigned coloured=0;
            performance_scene(&g,scene);
            if(scene==7) assert(cloud_density(&g.system.planets[g.body],sub(scale(g.pos,1,FP),g.system.planets[g.body].center),g.tick)>0);
            if(scene>=1 && scene<=3) assert(altitude_at(&g,g.pos)>100);
            memset(f.pixels+f.w*f.h,0xa5,16); render_game(&g,&f);
            for(i=0;i<f.w*f.h;i++) { assert(f.pixels[i]<32); coloured+=f.pixels[i]!=0; }
            assert(coloured>100); for(i=0;i<16;i++) assert(f.pixels[f.w*f.h+i]==0xa5);
        }
    }
    /* Both extrema of every setting, plus maximum RTG dimensions and a tiny
       3D viewport, exercise cache and depth allocation after backend changes. */
    for(i=0;i<GRAPHICS_OPTIONS;i++) { int n; for(n=0;n<2;n++) {
        settings_defaults(&s,1); s.preset=QUALITY_CUSTOM;
        *(int *)((char *)&s.gfx+graphics_options[i].offset)=n ? graphics_options[i].maximum : 0;
        s.gfx.scale=3; render_graphics(&s.gfx); assert(render_init(&f)); performance_scene(&g,1); render_game(&g,&f);
    }}
    settings_defaults(&s,0); s.mode=VIDEO_RTG; s.rtg_size=3; settings_dimensions(&s,1,&w,&h);
    assert(w==1024 && h==768); f.w=w;f.h=h;f.enhanced=1; render_graphics(&s.gfx); assert(render_init(&f)); render_game(&g,&f);
    {
        Intro in; int sample,last=0; static const int times[]={5,30,65,110,144,195};
        f.w=320; f.h=200; f.enhanced=0;
        settings_defaults(&s,1); s.preset=QUALITY_CUSTOM; s.gfx.scale=3;
        render_graphics(&s.gfx); assert(render_init(&f)); intro_start(&in,&g,0x53504143);
        for(sample=0;sample<6;sample++) {
            intro_frame(&in,&g,(times[sample]-last)*HZ); last=times[sample];
            memset(f.pixels+f.w*f.h,0xa5,16); render_intro(&in,&g,&f);
            for(i=0;i<16;i++) assert(f.pixels[f.w*f.h+i]==0xa5);
        }
    }
    free(f.pixels); render_shutdown();
    puts("PASS all presets and fixtures, Custom extrema, palette and frame bounds, 1024x768 RTG");
}
static void performance_views(void) {
    Settings s; Performance p; Frame f={320,200,0,NULL,{0}};
    uint8_t *saved=malloc(640*480); int mode,quality,scene,i,x,y;
    f.pixels=malloc(640*480+16); assert(f.pixels && saved);
    for(mode=0;mode<3;mode++) for(quality=0;quality<2;quality++) {
        f.w=mode==2 ? 640 : 320; f.h=mode==2 ? 480 : mode==1 ? 256 : 200; f.enhanced=mode!=0;
        settings_defaults(&s,1); settings_preset(&s,quality ? QUALITY_HIGH : QUALITY_LOW);
        render_graphics(&s.gfx); assert(render_init(&f));
        for(scene=6;scene<=10;scene++) {
            int scale=ui_scale(&f),first=f.h,last=0;
            performance_begin(&p,&g,&s,scene); p.report.result[scene].total_us=2500000; performance_animate(&p,&g);
            memset(f.pixels+f.w*f.h,0xa5,16);
            render_performance_scene(&g,&f); memcpy(saved,f.pixels,(size_t)f.w*f.h);
            g.ui_visible=1; g.ui_x=64; g.ui_y=f.h-10;
            snprintf(g.message,sizeof(g.message),"HIDDEN HUD MESSAGE");
            snprintf(g.system.name,sizeof(g.system.name),"HIDDEN SYSTEM NAME");
            snprintf(g.system.planets[0].name,sizeof(g.system.planets[0].name),"HIDDEN BODY NAME");
            render_performance_scene(&g,&f);
            assert(!memcmp(saved,f.pixels,(size_t)f.w*f.h));
            render_performance(&p,&f);
            for(y=0;y<32*scale;y++) for(x=0;x<f.w;x++) {
                int c=f.pixels[y*f.w+x]; assert(c==1 || c==3 || c==13 || c==15);
                if(c==15) { if(y<first) first=y; last=y; }
            }
            assert(last-first+1>=12*scale);
            for(i=0;i<16;i++) assert(f.pixels[f.w*f.h+i]==0xa5);
        }
    }
    free(saved); free(f.pixels); render_shutdown();
    puts("PASS full-frame effects hide HUD/world labels, with large scene names in ECS/AGA/RTG and Low/High");
}
static void orbit_view(void) {
    Settings s; Performance p; Frame f={320,200,0,NULL,{0}};
    uint8_t *with_rings=malloc(640*480); int mode,quality,sample,i;
    f.pixels=malloc(640*480); assert(f.pixels && with_rings);
    for(mode=0;mode<3;mode++) for(quality=0;quality<2;quality++) {
        f.w=mode==2 ? 640 : 320; f.h=mode==2 ? 480 : mode==1 ? 256 : 200; f.enhanced=mode!=0;
        settings_defaults(&s,1); settings_preset(&s,quality ? QUALITY_HIGH : QUALITY_LOW);
        render_graphics(&s.gfx); assert(render_init(&f)); performance_begin(&p,&g,&s,0);
        for(sample=0;sample<=4;sample++) {
            Planet *body=&g.system.planets[g.body]; Vec to_center,nose,at;
            int black=0,difference=0;
            p.report.result[0].total_us=(uint64_t)sample*1250000; performance_animate(&p,&g);
            to_center=sub(scale(body->center,FP,1),g.pos); nose=forward(g.yaw,g.pitch);
            assert(length(to_center)>body->radius*5LL*FP && length(to_center)<body->radius*7LL*FP);
            to_center=unit(to_center,16384);
            assert((nose.x*to_center.x+nose.y*to_center.y+nose.z*to_center.z)/16384>16300);
            at=g.pos; performance_animate(&p,&g); assert(!memcmp(&at,&g.pos,sizeof(at)));
            render_performance_scene(&g,&f); memcpy(with_rings,f.pixels,(size_t)f.w*f.h);
            body->rings=0; render_performance_scene(&g,&f); body->rings=1;
            for(i=0;i<f.w*f.h;i++) { black+=with_rings[i]==0; difference+=with_rings[i]!=f.pixels[i]; }
            assert(black>f.w*f.h/3); assert(difference>32);
        }
    }
    free(with_rings); free(f.pixels); render_shutdown();
    puts("PASS animated orbit keeps a visible planet and rings framed against space in ECS/AGA/RTG Low/High");
}
static void benchmark_audio(void) {
    Settings s; Performance suite,solo; SpaceAudio sound,reference;
    int8_t mixed[1024],solo_pcm[1024]; int scene,frame,i,jumps=0,nonzero=0;
    settings_defaults(&s,1);
    /* Match SDL's standalone startup: the observer sees an already-staged
       hyperspace Game. Its explicit entry cue must still produce PCM. */
    performance_begin(&solo,&g,&s,10); audio_init(&reference,&g);
    performance_animate(&solo,&g); assert(performance_audio(&solo,&reference,&g)==SOUND_JUMP);
    audio_mix(&reference,solo_pcm,sizeof(solo_pcm));
    for(i=0;i<(int)sizeof(solo_pcm);i++) nonzero+=solo_pcm[i]!=0;
    assert(nonzero>100);
    assert(performance_audio(&solo,&reference,&g)==SOUND_NONE && reference.effect_pos==(int)sizeof(solo_pcm));
    performance_begin(&suite,&g,&s,-1); audio_init(&sound,&g);
    for(scene=0;scene<PERFORMANCE_SCENES;scene++) {
        int effect;
        assert(suite.scene==scene); performance_animate(&suite,&g);
        effect=performance_audio(&suite,&sound,&g);
        assert(effect==(scene==10 ? SOUND_JUMP : SOUND_NONE)); jumps+=effect==SOUND_JUMP;
        assert(sound.effect==(scene==10 ? SOUND_JUMP : SOUND_NONE));
        audio_mix(&sound,mixed,sizeof(mixed));
        if(scene==10) assert(!memcmp(mixed,solo_pcm,sizeof(mixed)));
        else for(i=0;i<(int)sizeof(mixed);i++) assert(!mixed[i]);
        assert(!performance_record(&suite,&g,1,1,1));
        for(frame=0;frame<4;frame++) {
            assert(performance_audio(&suite,&sound,&g)==SOUND_NONE);
            if(scene==10) {
                int expected=(int)(suite.report.result[10].total_us*HZ/1000000);
                assert(g.hyperspace==HYPER_CHARGE+1+expected);
            }
            performance_record(&suite,&g,1,1,1300000);
            if(frame<3) performance_animate(&suite,&g);
        }
    }
    assert(jumps==1 && suite.report.complete);
    puts("PASS suite fixture changes stay silent, standalone/suite hyperspace PCM match, cue fires once and stops on exit");
}
static void timing(void) {
    Settings s; Performance p; int i, scene, result=0;
    settings_defaults(&s,1); performance_begin(&p,&g,&s,10);
    assert(!performance_record(&p,&g,300000,100000,500000)); assert(!p.report.result[10].frames);
    for(i=0;i<9;i++) assert(!performance_record(&p,&g,300000,100000,500000));
    assert(performance_record(&p,&g,300000,100000,500000)==1);
    assert(p.report.complete && p.report.result[10].frames==10);
    assert(p.report.result[10].total_us==5000000 && p.report.result[10].render_us==3000000 && p.report.result[10].output_us==1000000);
    performance_begin(&p,&g,&s,10); performance_record(&p,&g,1,1,1);
    for(i=0;i<1000;i++) assert(!performance_record(&p,&g,1,1,1000));
    assert(!p.report.complete && p.report.result[10].frames==1000);
    for(i=0;i<4;i++) result=performance_record(&p,&g,1,1,1000000);
    assert(result==1 && p.report.result[10].total_us==5000000);
    performance_begin(&p,&g,&s,10); performance_record(&p,&g,1,1,1);
    for(i=0;i<3;i++) assert(!performance_record(&p,&g,1,1,2000000));
    assert(performance_record(&p,&g,1,1,2000000)==1);
    performance_begin(&p,&g,&s,-1);
    for(scene=0;scene<PERFORMANCE_SCENES;scene++) {
        assert(p.scene==scene && p.warmup);
        assert(!performance_record(&p,&g,1,1,10000000)); /* warmup is excluded */
        for(i=0;i<9;i++) assert(!performance_record(&p,&g,1,1,500000));
        result=performance_record(&p,&g,1,1,500000);
        assert(result==(scene==PERFORMANCE_SCENES-1 ? 1 : -1));
        assert(p.report.result[scene].total_us>=PERFORMANCE_MIN_US);
    }
    puts("PASS every scene lasts five seconds, fast/slow samples, warmup exclusion and suite transitions");
}
static void weather_flights(void) {
    Settings s; Performance p; int scene,sample;
    settings_defaults(&s,0);
    for(scene=6;scene<=8;scene++) {
        int density[3]={0}; Vec start={0},last={0}; uint32_t tick;
        performance_begin(&p,&g,&s,scene); tick=g.tick;
        for(sample=0;sample<=20;sample++) {
            const Planet *body=&g.system.planets[g.body]; Vec local,up,right0,up0,roof;
            p.report.result[scene].total_us=(uint64_t)sample*250000;
            performance_animate(&p,&g);
            local=sub(scale(g.pos,1,FP),body->center); up=unit(local,16384);
            right0=vec(cosi(g.yaw),0,-sini(g.yaw));
            up0=vec(-(int64_t)sini(g.pitch)*sini(g.yaw)/16384,cosi(g.pitch),-(int64_t)sini(g.pitch)*cosi(g.yaw)/16384);
            roof=add(scale(right0,sini(g.roll),16384),scale(up0,cosi(g.roll),16384));
            assert((up.x*roof.x+up.y*roof.y+up.z*roof.z)/16384>16000);
            if(!sample) start=g.pos;
            else assert(length(sub(g.pos,last))>30*FP);
            last=g.pos;
            assert(altitude_at(&g,g.pos)>100);
            if(scene!=7) assert(precipitation(body,local,g.tick)==(scene==6 ? 1 : 2));
            else if(sample%10==0) density[sample/10]=cloud_density(body,local,g.tick);
            /* Frame count cannot alter the same timestamp's camera or rain. */
            { Vec at=g.pos; int roll=g.roll; performance_animate(&p,&g); assert(!memcmp(&at,&g.pos,sizeof(at)) && roll==g.roll); }
        }
        assert(length(sub(g.pos,start))>(scene==7 ? 2500 : 800)*FP);
        assert(g.tick==tick+5*HZ && g.ui_clock==5*HZ);
        if(scene==7) assert(density[1]>128 && density[0]<density[1] && density[2]<density[1]);
    }
    puts("PASS upright planetary horizons, timed flight through rain/snow, cloud entry/core/exit and terrain clearance");
}
int main(void) { persistence(); menus(); timing(); weather_flights(); orbit_view(); benchmark_audio(); rendering(); performance_views(); return 0; }
