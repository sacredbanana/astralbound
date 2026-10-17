/* SPDX-License-Identifier: Unlicense
   OS-managed ECS setup screen, before private Copper/interrupt takeover. */
static int amiga_launcher(Launcher *menu, const char *path) {
    struct Screen *setup=NULL; struct Window *input=NULL; struct BitMap bitmap;
    Frame image={320,200,0,NULL,{0}};
    UBYTE *planar=NULL; ULONG palette[98]; struct Library *joylib=NULL;
    unsigned previous_buttons=~0U; int previous_roll=0,previous_pitch=0,action=0,i,active=1,dirty=1;
    UWORD setup_pot=0; int centre_x=128,centre_y=128;
    image.pixels=malloc(320*200); planar=AllocMem(40*200*5,MEMF_CHIP|MEMF_CLEAR);
    if(!image.pixels || !planar) { action=LAUNCH_QUIT; goto done; }
    render_palette(&image);
    setup=OpenScreenTags(NULL,SA_DisplayID,(GfxBase->DisplayFlags&PAL) ? PAL_MONITOR_ID : NTSC_MONITOR_ID,
        SA_Width,320,SA_Height,200,SA_Depth,5,SA_Type,CUSTOMSCREEN,SA_Quiet,TRUE,SA_ShowTitle,FALSE,TAG_DONE);
    if(!setup) { action=LAUNCH_QUIT; goto done; }
    input=OpenWindowTags(NULL,WA_CustomScreen,(ULONG)setup,WA_Width,320,WA_Height,200,
        WA_Flags,WFLG_BORDERLESS|WFLG_BACKDROP|WFLG_ACTIVATE|WFLG_RMBTRAP|WFLG_REPORTMOUSE,
        WA_IDCMP,IDCMP_RAWKEY|IDCMP_MOUSEBUTTONS|IDCMP_ACTIVEWINDOW|IDCMP_INACTIVEWINDOW,TAG_DONE);
    if(!input) { action=LAUNCH_QUIT; goto done; }
    palette[0]=32UL<<16;
    for(i=0;i<32;i++) { ULONG c=image.palette[i];
        palette[1+i*3]=((c>>16)&255)*0x01010101UL;
        palette[2+i*3]=((c>>8)&255)*0x01010101UL;
        palette[3+i*3]=(c&255)*0x01010101UL;
    }
    palette[97]=0; LoadRGB32(&setup->ViewPort,palette);
    InitBitMap(&bitmap,5,320,200);
    for(i=0;i<5;i++) bitmap.Planes[i]=planar+i*40*200;
    if(analog) {
        PotgoBase=OpenResource((CONST_STRPTR)POTGONAME);
        if(PotgoBase) setup_pot=AllocPotBits(0x5001);
        if((setup_pot&0x5001)==0x5001) {
            WritePotgo(1,0xf001); WaitTOF(); WaitTOF();
            centre_x=hw->pot1dat&255; centre_y=hw->pot1dat>>8;
        }
    } else joylib=OpenLibrary((CONST_STRPTR)"lowlevel.library",40);
    /* lowlevel's inline stubs use the global base. It is released before
       the flight backend acquires its own controller resources. */
    LowLevelBase=joylib;
    while(!action) {
        struct IntuiMessage *m;
        while((m=(struct IntuiMessage *)GetMsg(input->UserPort))) {
            dirty=1;
            if(m->Class==IDCMP_RAWKEY && !(m->Code&128)) action=launcher_key(menu,m->Code==0x45 ? 27 : raw_action(m->Code));
            if(m->Class==IDCMP_MOUSEBUTTONS && (m->Code==SELECTDOWN || m->Code==SELECTUP))
                action=launcher_pointer(menu,m->MouseX,m->MouseY,m->Code==SELECTDOWN);
            if(m->Class==IDCMP_INACTIVEWINDOW) active=0;
            if(m->Class==IDCMP_ACTIVEWINDOW) active=1;
            ReplyMsg((struct Message *)m);
            if(action) break;
        }
        if(active && !action) {
            ULONG state=joylib ? ReadJoyPort(1) : 0;
            ULONG type=state&JP_TYPE_MASK;
            UWORD joy=hw->joy1dat;
            int x=joystick_roll(joy),y=joystick_pitch(joy);
            unsigned buttons=!(*(volatile UBYTE *)0xbfe001&0x80);
            if(type==JP_TYPE_GAMECTLR || type==JP_TYPE_JOYSTK) {
                x=!!(state&JPF_JOY_RIGHT)-!!(state&JPF_JOY_LEFT);
                y=!!(state&JPF_JOY_DOWN)-!!(state&JPF_JOY_UP);
                buttons|=controller_buttons(state);
            } else if(type==JP_TYPE_ANALOGUE) {
                x=(state&255)>176 ? 1 : (state&255)<80 ? -1 : 0;
                y=((state>>8)&255)>176 ? 1 : ((state>>8)&255)<80 ? -1 : 0;
                buttons|=controller_buttons(state);
            }
            if((setup_pot&0x5001)==0x5001) {
                UWORD data=hw->pot1dat;
                x=(data&255)>centre_x+24 ? 1 : (data&255)<centre_x-24 ? -1 : 0;
                y=(data>>8)>centre_y+24 ? 1 : (data>>8)<centre_y-24 ? -1 : 0;
                WritePotgo(1,0xf001);
            }
            if((y && y!=previous_pitch) || (x && x!=previous_roll) || (buttons && !previous_buttons)) dirty=1;
            if(y && y!=previous_pitch) action=launcher_key(menu,y>0 ? LAUNCH_DOWN : LAUNCH_UP);
            if(x && x!=previous_roll) action=launcher_key(menu,x>0 ? LAUNCH_RIGHT : LAUNCH_LEFT);
            if(buttons && !previous_buttons) action=launcher_key(menu,'\r');
            previous_roll=x; previous_pitch=y; previous_buttons=buttons;
        }
        if(action==4) { snprintf(menu->message,sizeof(menu->message),"%s",settings_save(&menu->settings,path) ? "SETTINGS SAVED" : "SAVE FAILED - CHECK DISK"); action=0; }
        if(dirty) {
            render_launcher(menu,&image); c2p(image.pixels,planar,320,200,5);
            BltBitMapRastPort(&bitmap,0,0,input->RPort,0,0,320,200,0xc0); WaitBlit();
            dirty=0;
        }
        WaitTOF();
    }
done:
    if(setup_pot) FreePotBits(setup_pot);
    if(joylib) { SetJoyPortAttrs(1,SJA_Reinitialize,TRUE,TAG_DONE); CloseLibrary(joylib); }
    LowLevelBase=NULL;
    if(input) CloseWindow(input);
    if(setup) CloseScreen(setup);
    if(planar) FreeMem(planar,40*200*5);
    free(image.pixels);
    return action;
}
