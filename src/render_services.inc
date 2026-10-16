/* SPDX-License-Identifier: Unlicense
   Original procedural videophone portraits; uses the shared 32-colour palette. */
static void contact_portrait(int x,int y,int w,int h,uint32_t id) {
    int px,py,skin=(id%3==0?12:id%3==1?14:24),hair=(id>>4)%3==0?0:(id>>4)%3==1?21:12;
    int eye=20+(int)(id>>8)%4,wide=12+(int)(id%3),coat=4+(int)(id>>12)%3;
    for(py=0;py<64;py++) for(px=0;px<64;px++) {
        int dx=px-32,dy=py-27,c=py%8==0?3:2;
        if(py>49 && dx*dx*2+(py-68)*(py-68)<1100) c=coat;
        if(py>45 && py<56 && abs(dx)<7) c=skin;
        if(dx*dx*400+dy*dy*wide*wide<wide*wide*400) c=skin;
        if(dx>3 && dx*dx*400+dy*dy*wide*wide<wide*wide*400) c=14;
        if(py>7 && py<25 && dx*dx*260+(py-24)*(py-24)*225<225*260 && (py<16 || px<23 || px>42 || (id&4 && px<29))) c=hair;
        if(py>=eye && py<=eye+1 && ((px>=24 && px<=28) || (px>=36 && px<=40))) c=0;
        if(py==eye-3 && ((px>=23 && px<=28) || (px>=36 && px<=41))) c=hair;
        if(py>=eye+4 && py<eye+11 && px==32) c=12;
        if(py==41 && px>=28 && px<=36) c=21;
        if(id&8 && py>eye-3 && py<eye+5 && px>20 && px<44 && (py==eye-2 || py==eye+3 || px==21 || px==30 || px==34 || px==43)) c=17;
        if(py>52 && py<61 && abs(dx)<(61-py)/2) c=15;
        rect(x+px*w/64,y+py*h/64,(px+1)*w/64-px*w/64,(py+1)*h/64-py*h/64,c);
    }
    line(x,y,x+w-1,y,13); line(x,y,x,y+h-1,5);
    line(x+w-1,y,x+w-1,y+h-1,5); line(x,y+h-1,x+w-1,y+h-1,5);
}
/* Word-wrapped text is bounded by the content pane in every display mode. */
static void service_text(int x,int y,int columns,int bottom,int colour,const char *source) {
    char row[64];
    while(*source && y+7<=bottom) {
        int n=0,end;
        while(source[n] && n<columns) n++;
        end=n;
        if(source[n] && source[n]!=' ') { while(end>0 && source[end]!=' ') end--; if(!end) end=n; }
        memcpy(row,source,(size_t)end); row[end]=0; text(x,y,colour,row);
        source+=end; while(*source==' ') source++; y+=9;
    }
}
static void service_directory(void) {
    int i,sel=game->service_selection,h=frame->h/draw_scale;
    char s[80]; uint32_t id=service_identity(game,sel);
    text(8,25,13,game->docked>=0?game->ports[game->docked].name:"NO STATION CONNECTION - DOCK FIRST");
    for(i=0;i<PORT_SERVICES;i++) {
        snprintf(s,sizeof(s),"%d %s",i+1,service_names[i]); text(13,44+i*15,i==sel?12:15,s);
    }
    contact_portrait(214,38,72,64,id);
    contact_name(id,s,sizeof(s)); text(194,106,15,s);
    text(194,116,13,service_roles[sel]);
    text(194,126,7,game->docked>=0?"CHANNEL READY":"OFFLINE");
    if(h>=230) service_text(8,148,50,h-65,7,service_greetings[sel]);
    text(8,137,7,sel==4?"FULL HULL / SHIELDS / FUEL - 100 CR":"SELECT A CONTACT / CALL FOR SERVICE");
}
static const char *contract_state(const Mission *m) {
    return m->state==OFFERED?"OFFER":m->state==ACTIVE?"ACTIVE":m->state==COMPLETE?"PAID":"FAILED";
}
static void mission_screen(void) {
    char s[256],name[24]; int i,h=frame->h/draw_scale;
    const Mission *m=selected_mission(game);
    if(!game->mission_detail) {
        text(13,35,7,"NOTICE             PAYMENT    STATUS");
        for(i=0;i<MAX_MISSIONS;i++) {
            m=game->page==LOG?&game->contracts[i]:&game->board[i];
            if(!m->id) { text(13,44+i*12,7,"-- EMPTY --"); continue; }
            snprintf(s,sizeof(s),"%d %-14s %5d CR  %s",i+1,mission_names[m->type],m->reward,contract_state(m));
            text(13,44+i*12,i==game->selection?12:15,s);
        }
        return;
    }
    if(!m || !m->id) { text(12,45,7,"NO CONTRACT SELECTED"); return; }
    contact_portrait(8,37,56,57,hash32(m->id));
    contact_name(hash32(m->id),name,sizeof(name)); text(72,38,15,name);
    snprintf(s,sizeof(s),"%s / %s",mission_names[m->type],contract_state(m)); text(72,49,12,s);
    galaxy_name(game->seed,m->destination,name);
    snprintf(s,sizeof(s),"TO %.23s",name); text(72,60,13,s);
    mission_port_name(game,m,name,sizeof(name)); text(72,71,13,name);
    { uint32_t t=m->deadline>game->tick?(m->deadline-game->tick)/HZ:0;
      snprintf(s,sizeof(s),"%d CR / %luH %02luM LEFT",m->reward,(unsigned long)(t/3600),(unsigned long)(t/60%60)); text(72,82,15,s); }

    line(8,97,311,97,5);
    if(game->mission_tab==0) service_text(8,102,50,h-64,15,mission_story(m));
    else if(game->mission_tab==1) service_text(8,102,50,h-64,15,mission_orders(m));
    else {
        snprintf(s,sizeof(s),"%dT %s / %d CABIN SEATS / REP %d",m->quantity,goods_names[m->good],m->passengers,m->required_rep); text(8,102,15,s);
        snprintf(s,sizeof(s),"%s / LEG %d OF %d",faction_names[m->faction],m->leg+1>m->legs?m->legs:m->leg+1,m->legs); text(8,111,7,s);
        if(game->page==BOARD) {
            const char *reason=mission_refusal(game,game->selection);
            text(8,120,reason?14:11,reason?reason:"READY TO ACCEPT");
            text(8,129,7,"NEGOTIATE: REP 3 / +20% PAY -25% TIME");
        } else {
            if(m->type==SURVEY || m->type==RECON || m->type==ESCORT)
                snprintf(s,sizeof(s),"RECORDED %d / %d SEC",m->progress/HZ,m->type==SURVEY?20:m->type==RECON?30:120);
            else if(m->type==RESCUE) snprintf(s,sizeof(s),"%s",m->progress?"SURVIVOR ABOARD - RETURN TO PORT":"LOCATE AND COLLECT THE CAPSULE");
            else snprintf(s,sizeof(s),"%s TERMS / %s",m->negotiated?"NEGOTIATED":"STANDARD",contract_state(m));
            text(8,120,13,s);
            text(8,129,7,"PAYMENT ON COMPLETION / DEADLINE APPLIES");
        }
    }
}
