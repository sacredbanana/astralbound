/* SPDX-License-Identifier: Unlicense
   Port scenery uses metres and the existing depth-tested indexed renderer.
   Identity comes from the system seed; animation never changes simulation. */
typedef struct { Vec p, right, up, front; } PortFrame;
typedef struct { Vec p; uint8_t color, radius; } PortLight;
#define PORT_LIGHT_MAX 256
static PortLight port_lights[PORT_LIGHT_MAX];
static int port_light_count;
static Vec port_point(const PortFrame *s, int x, int y, int z) {
    return add(s->p, add(scale(s->right, x, 16384),
                        add(scale(s->up, y, 16384), scale(s->front, z, 16384))));
}
static void port_quad(Vec a, Vec b, Vec c, Vec d, int color) {
    triangle(a, b, c, color); triangle(a, c, d, color);
}
static void port_block(const PortFrame *s, int x, int y, int z,
                       int w, int h, int d, int color) {
    Vec v[8];
    int j;
    for (j = 0; j < 8; j++)
        v[j] = port_point(s, x + ((j & 1) ? w : -w), y + ((j & 4) ? h : 0),
                          z + ((j & 2) ? d : -d));
    port_quad(v[0],v[1],v[3],v[2],color);
    port_quad(v[4],v[6],v[7],v[5],color+2);
    port_quad(v[0],v[4],v[5],v[1],color);
    port_quad(v[2],v[3],v[7],v[6],color+1);
    port_quad(v[0],v[2],v[6],v[4],color+1);
    port_quad(v[1],v[5],v[7],v[3],color);
}
static void port_lamp(const PortFrame *s, int x, int y, int z, int color, int phase) {
    /* Steady lamps have phase < 0; red beacons double-flash, with independent
       phases per installation. ui_clock stays readable during time warp. */
    unsigned t = (game->ui_clock + (phase < 0 ? 0U : (unsigned)phase)) % (HZ * 2);
    if (phase >= 0 && !(t < 4 || (t >= 8 && t < 12))) return;
    if (port_light_count < PORT_LIGHT_MAX)
        port_lights[port_light_count++] = (PortLight){port_point(s,x,y,z), (uint8_t)color, 3};
}
static void port_draw_lights(void) {
    int j;
    for (j = 0; j < port_light_count; j++) {
        const PortLight *l = &port_lights[j];
        Vec v = camera(l->p);
        int x, y, dx, dy, r;
        uint32_t depth;
        if (v.z < 8 || v.z > 16000) continue;
        x = screen_coord(frame->w/2 + v.x * (frame->w/2) / v.z);
        y = screen_coord(view_h/2 - v.y * (frame->w/2) / v.z);
        r = (int)(l->radius * (frame->w/2) / v.z);
        if (r < 1 && v.z < 4000) r = 1;
        if (r > 2) r = 2;
        depth = (uint32_t)(1073741824LL / v.z);
        for (dy=-r; dy<=r; dy++) for (dx=-r; dx<=r; dx++) {
            int xx=x+dx, yy=y+dy;
            if (dx*dx+dy*dy > r*r || xx<0 || xx>=frame->w || yy<0 || yy>=view_h) continue;
            if (depth >= depth_buffer[yy*frame->w+xx]) {
                int glow=l->color==14 ? 22 : l->color==13 ? 5 :
                         l->color==11 ? 8 : l->color==12 ? 21 : 7;
                frame->pixels[yy*frame->w+xx] = (uint8_t)((dx || dy) ? glow : l->color);
                depth_buffer[yy*frame->w+xx] = depth;
            }
        }
    }
}
static void port_windows(const PortFrame *s, int w, int d, int y, int color) {
    /* Floating a metre clear of the wall avoids coplanar depth flicker. */
    port_quad(port_point(s,-w+5,y,-d-1),port_point(s,w-5,y,-d-1),
              port_point(s,w-5,y+5,-d-1),port_point(s,-w+5,y+5,-d-1),color);
    port_quad(port_point(s,-w+5,y,d+1),port_point(s,w-5,y,d+1),
              port_point(s,w-5,y+5,d+1),port_point(s,-w+5,y+5,d+1),color);
    port_quad(port_point(s,-w-1,y,-d+5),port_point(s,-w-1,y,d-5),
              port_point(s,-w-1,y+5,d-5),port_point(s,-w-1,y+5,-d+5),color);
    port_quad(port_point(s,w+1,y,-d+5),port_point(s,w+1,y,d-5),
              port_point(s,w+1,y+5,d-5),port_point(s,w+1,y+5,-d+5),color);
}
static void port_tank(const PortFrame *s, int radius, int height, int dome) {
    int j;
    for (j=0;j<8;j++) {
        int a=j*128, b=(j+1)*128;
        int ax=cosi(a)*radius/16384, az=sini(a)*radius/16384;
        int bx=cosi(b)*radius/16384, bz=sini(b)*radius/16384;
        if (dome) {
            port_quad(port_point(s,ax,0,az),port_point(s,bx,0,bz),
                      port_point(s,bx*3/4,height*2/3,bz*3/4),
                      port_point(s,ax*3/4,height*2/3,az*3/4),j%2 ? 18 : 17);
            triangle(port_point(s,ax*3/4,height*2/3,az*3/4),
                     port_point(s,bx*3/4,height*2/3,bz*3/4),port_point(s,0,height,0),20);
        } else {
            port_quad(port_point(s,ax,0,az),port_point(s,bx,0,bz),
                      port_point(s,bx,height,bz),port_point(s,ax,height,az),16+j%3);
            triangle(port_point(s,ax,height,az),port_point(s,bx,height,bz),
                     port_point(s,0,height+8,0),20);
        }
    }
}
static void port_dish(const PortFrame *s, int height, int phase) {
    int j, angle=(int)(game->ui_clock/4 + (unsigned)phase)&1023;
    PortFrame dish=*s;
    port_block(s,0,0,0,5,height,5,16);
    dish.p=port_point(s,0,height,0);
    dish.right=add(scale(s->right,cosi(angle),16384),scale(s->front,sini(angle),16384));
    dish.front=sub(scale(s->front,cosi(angle),16384),scale(s->right,sini(angle),16384));
    dish.up=add(scale(s->up,14200,16384),scale(dish.front,8192,16384));
    dish.front=sub(scale(dish.front,14200,16384),scale(s->up,8192,16384));
    for(j=0;j<8;j++) {
        int a=j*128,b=(j+1)*128;
        triangle(port_point(&dish,cosi(a)*36/16384,14,sini(a)*36/16384),
                 port_point(&dish,cosi(b)*36/16384,14,sini(b)*36/16384),
                 port_point(&dish,0,0,0),j%2 ? 18 : 20);
    }
    port_block(&dish,0,0,0,2,25,2,17);
    port_lamp(&dish,0,28,0,14,phase);
}
static void port_solar(const PortFrame *s, int x, int y, int z, int w, int d, int detail) {
    int j;
    port_block(s,x,y-5,z,w+3,5,d+3,16);
    port_quad(port_point(s,x-w,y+1,z-d),port_point(s,x+w,y+1,z-d),
              port_point(s,x+w,y+1,z+d),port_point(s,x-w,y+1,z+d),25);
    if (detail) for(j=-2;j<=2;j++)
        port_quad(port_point(s,x+j*w/3-1,y+2,z-d),port_point(s,x+j*w/3+1,y+2,z-d),
                  port_point(s,x+j*w/3+1,y+2,z+d),port_point(s,x+j*w/3-1,y+2,z+d),27);
}
static void port_surface(const Port *port, PortFrame s, uint32_t seed, int detail) {
    const Planet *body=&game->system.planets[port->body];
    Vec local=sub(s.p,body->center);
    PortFrame pad=s;
    int j;
    for(j=0;j<36;j+=(graphics.ports==0 ? 3 : graphics.ports==1 ? 2 : 1)) {
        uint32_t h=hash32(seed+(unsigned)j*173U);
        int x=(j%6-3)*180+70,z=(j/6-3)*180+70;
        int kind=(int)(h%6),height=60+(int)(h>>8)%90;
        PortFrame b=s;
        if(abs(x)<150 && abs(z)<150) continue; /* launch/landing clearance */
        b.p=terrain_vertex(body,local,s.right,s.front,x,z);
        b.up=unit(sub(b.p,body->center),16384);
        /* Streets follow the same curved terrain as the buildings. */
        port_quad(terrain_vertex(body,local,s.right,s.front,x-80,z-80),
                  terrain_vertex(body,local,s.right,s.front,x+100,z-80),
                  terrain_vertex(body,local,s.right,s.front,x+100,z-65),
                  terrain_vertex(body,local,s.right,s.front,x-80,z-65),3);
        port_quad(terrain_vertex(body,local,s.right,s.front,x-80,z-80),
                  terrain_vertex(body,local,s.right,s.front,x-65,z-80),
                  terrain_vertex(body,local,s.right,s.front,x-65,z+100),
                  terrain_vertex(body,local,s.right,s.front,x-80,z+100),3);
        if(!detail) { port_block(&b,0,0,0,40,kind==0 ? height*2 : height,40,16); continue; }
        switch(kind) {
        case 0: /* stepped habitat / traffic-control tower */
            port_block(&b,0,0,0,46,height,42,16);
            port_block(&b,0,height,0,30,height/2,28,17);
            port_windows(&b,46,42,20,12);
            port_windows(&b,46,42,height-15,13);
            port_windows(&b,30,28,height+height/2-12,12);
            port_block(&b,0,height*3/2,0,2,35,2,16);
            port_lamp(&b,0,height*3/2+38,0,14,(int)(h%64));
            break;
        case 1: { /* wide hangar, pitched roof, inset door */
            int side;
            port_block(&b,0,0,0,60,48,48,16);
            for(side=-1;side<=1;side+=2) {
                port_quad(port_point(&b,side*60,48,-48),port_point(&b,side*60,48,48),
                          port_point(&b,0,76,48),port_point(&b,0,76,-48),side<0 ? 17 : 20);
                triangle(port_point(&b,-60,48,side*48),port_point(&b,60,48,side*48),
                         port_point(&b,0,76,side*48),18);
            }
            port_quad(port_point(&b,-43,0,-49),port_point(&b,43,0,-49),
                      port_point(&b,43,36,-49),port_point(&b,-43,36,-49),1);
            port_block(&b,0,38,-51,47,4,2,10);
            port_lamp(&b,-48,41,-54,12,-1); port_lamp(&b,48,41,-54,12,-1);
            break;
        }
        case 2: /* fuel / processing tanks and a pipe bridge */
            port_tank(&b,34,85,0);
            port_block(&b,0,55,38,6,8,22,19);
            port_block(&b,0,0,62,6,63,6,19);
            port_lamp(&b,0,96,0,14,(int)(h%64));
            break;
        case 3: /* glazed biosphere and its utility annex */
            port_tank(&b,58,65,1);
            {
                PortFrame annex=b;
                annex.p=port_point(&b,48,0,0);
                port_block(&annex,0,0,0,22,25,18,16);
                port_windows(&annex,22,18,12,13);
            }
            break;
        case 4: /* cargo stacks and a gantry */
            port_block(&b,-28,0,0,22,26,42,21);
            port_block(&b,28,0,12,22,26,30,16);
            port_block(&b,-28,26,8,22,26,28,19);
            port_block(&b,-65,0,0,4,100,4,10);
            port_block(&b,65,0,0,4,100,4,10);
            port_block(&b,0,100,0,69,7,5,10);
            port_block(&b,20,57,0,1,43,1,17);
            port_lamp(&b,-65,110,0,12,(int)(h%64));
            break;
        default:
            port_block(&b,0,0,0,38,30,38,16);
            port_solar(&b,0,42,0,65,45,1);
            if(j%2) port_dish(&b,65,(int)(h%1024));
            break;
        }
    }
    /* The pad remains at the existing landing datum; raised markings do not
       place scenery in the vertical approach cylinder. */
    pad.p=add(body->center,unit(local,body->radius+2));
    port_block(&pad,0,0,0,67,3,67,16);
    port_quad(port_point(&pad,-55,4,-55),port_point(&pad,55,4,-55),
              port_point(&pad,55,4,55),port_point(&pad,-55,4,55),3);
    for(j=-1;j<=1;j+=2) {
        port_block(&pad,j*22,5,0,3,1,30,12);
        port_block(&pad,0,5,0,22,1,3,12);
        port_block(&pad,j*65,0,-65,3,9,3,16);
        port_block(&pad,j*65,0,65,3,9,3,16);
        port_lamp(&pad,j*65,12,-65,13,-1); port_lamp(&pad,j*65,12,65,13,-1);
    }
    if(detail) for(j=0;j<6;j++) {
        int side;
        for(side=-1;side<=1;side+=2) {
            /* A chasing pair of approach lights leads towards the pad. */
            PortFrame lamp=pad;
            lamp.p=terrain_vertex(body,local,s.right,s.front,side*75,-100-j*24);
            port_lamp(&lamp,0,4,0,(game->ui_clock/5)%6==(unsigned)(5-j) ? 15 : 12,-1);
        }
    }
}
static void port_orbital(const Port *port, PortFrame s, uint32_t seed, int detail, int target) {
    int j, variant=(int)(seed%3),radius=210+variant*25;
    Vec right=s.right,front=s.front;
    s.right=add(scale(right,cosi(port->rotation),16384),scale(front,sini(port->rotation),16384));
    s.front=sub(scale(front,cosi(port->rotation),16384),scale(right,sini(port->rotation),16384));
    for(j=0;j<16;j++) {
        int a=j*64,b=(j+1)*64;
        PortFrame segment=s;
        Vec da=add(scale(s.right,cosi(a),16384),scale(s.front,sini(a),16384));
        Vec db=add(scale(s.right,cosi(b),16384),scale(s.front,sini(b),16384));
        Vec ai=add(s.p,scale(da,radius-45,16384)),ao=add(s.p,scale(da,radius+30,16384));
        Vec bi=add(s.p,scale(db,radius-45,16384)),bo=add(s.p,scale(db,radius+30,16384));
        Vec u=scale(s.up,36,16384);
        port_quad(add(ai,u),add(bi,u),add(bo,u),add(ao,u),j%4==0 ? 20 : 18);
        port_quad(sub(ai,u),sub(bi,u),sub(bo,u),sub(ao,u),16);
        port_quad(sub(ao,u),sub(bo,u),add(bo,u),add(ao,u),16+j%2);
        port_quad(sub(ai,u),sub(bi,u),add(bi,u),add(ai,u),19);
        if(detail) {
            Vec wa=add(s.p,scale(da,radius+31,16384)),wb=add(s.p,scale(db,radius+31,16384));
            port_quad(add(wa,scale(s.up,5,16384)),add(wb,scale(s.up,5,16384)),
                      add(wb,scale(s.up,11,16384)),add(wa,scale(s.up,11,16384)),j%3 ? 12 : 13);
        }
        if(j%4) continue;
        segment.p=add(s.p,scale(da,radius,16384));
        segment.right=da; segment.front=cross(da,s.up);
        port_block(&segment,55,-24,0,75,46,18,16);
        port_block(&segment,130,-55,0,40,110+variant*20,38,17);
        if(detail) {
            PortFrame module=segment;
            module.p=port_point(&segment,130,-55,0);
            port_windows(&module,40,38,65,13);
            port_lamp(&segment,130,60+variant*20,0,j==0 ? 11 : 14,(int)(seed%64)+j);
            if(j==0) port_dish(&module,130,(int)(seed%1024));
        }
        if(j==0 || j==8) {
            port_block(&segment,270,-8,0,90,12,7,16);
            port_solar(&segment,310,5,0,75,110+variant*25,detail);
        }
        if(variant==2 && (j==4 || j==12)) {
            PortFrame tank=segment;
            tank.p=port_point(&segment,140,-100,0);
            port_tank(&tank,42,200,0);
        }
    }
    if(variant==1) {
        /* A second habitation deck gives the relay class a distinct profile.
           Its supports and ring stay outside the central docking bore. */
        for(j=0;j<16;j++) {
            int a=j*64,b=(j+1)*64;
            Vec ai=port_point(&s,cosi(a)*(radius-55)/16384,-150,sini(a)*(radius-55)/16384);
            Vec ao=port_point(&s,cosi(a)*(radius+10)/16384,-150,sini(a)*(radius+10)/16384);
            Vec bi=port_point(&s,cosi(b)*(radius-55)/16384,-150,sini(b)*(radius-55)/16384);
            Vec bo=port_point(&s,cosi(b)*(radius+10)/16384,-150,sini(b)*(radius+10)/16384);
            Vec u=scale(s.up,25,16384);
            port_quad(ai,bi,bo,ao,18);
            port_quad(sub(ai,u),sub(bi,u),sub(bo,u),sub(ao,u),16);
            port_quad(sub(ao,u),sub(bo,u),bo,ao,17);
            port_quad(sub(ai,u),sub(bi,u),bi,ai,19);
            if(j%4==0) {
                PortFrame support=s;
                support.p=port_point(&s,cosi(a)*(radius-10)/16384,0,sini(a)*(radius-10)/16384);
                port_block(&support,0,-150,0,9,114,9,16);
            }
        }
    }
    /* Four short docking collars leave a clear, 120 m wide central opening. */
    for(j=0;j<4;j++) {
        PortFrame arm=s;
        int a=j*256;
        arm.right=add(scale(s.right,cosi(a),16384),scale(s.front,sini(a),16384));
        arm.front=cross(arm.right,s.up);
        port_block(&arm,115,-12,0,55,24,12,19);
        port_block(&arm,66,-35,0,6,80,19,17);
        port_lamp(&arm,66,48,-20,j%2 ? 11 : 14,-1);
        port_lamp(&arm,66,48,20,13,-1);
        if(target) {
            int k;
            for(k=1;k<=4;k++) {
                int c=(game->ui_clock/6)%4==(unsigned)(4-k) ? 15 : 13;
                port_lamp(&arm,66,k*160,0,c,-1);
            }
        }
    }
}
static void port_geometry(int i) {
    const Port *port=&game->ports[i];
    const Planet *body=&game->system.planets[port->body];
    PortFrame s;
    Vec local,ref;
    int64_t distance;
    uint32_t seed=hash32(game->system.seed^(unsigned)i*7919U);
    s.p=scale(port->pos,1,FP); local=sub(s.p,body->center);
    distance=length(sub(s.p,scale(camera_position,1,FP)));
    static const int ranges[]={4000,10000,40000,80000,160000};
    if(distance>ranges[graphics.port_range] && i!=game->docked) return;
    s.up=unit(local,16384);
    /* Surface streets rotate with their planet rather than sliding round a
       moving port as its latitude changes in system coordinates. */
    ref=port->surface ? body_rotate(body,vec(0,0,16384),0) : vec(0,0,16384);
    s.right=unit(cross(s.up,ref),16384);
    if(!length(s.right)) s.right=unit(cross(s.up,vec(0,16384,0)),16384);
    s.front=cross(s.right,s.up);
    if(port->surface) port_surface(port,s,seed,graphics.ports==2 && distance<6000);
    else port_orbital(port,s,seed,graphics.ports>0 && distance<(graphics.ports==2 ? 9000 : 3000),i==game->target);
}
