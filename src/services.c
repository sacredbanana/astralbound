/* SPDX-License-Identifier: Unlicense */
#include "services.h"
#include "galaxy.h"
#include <stdio.h>
const char *service_names[PORT_SERVICES] = {"BULLETIN BOARD", "COMMODITY EXCHANGE", "SHIP BROKER", "OUTFITTER", "REPAIR AND REFUEL", "CREW AND AUTHORITY"};
const char *service_roles[PORT_SERVICES] = {"CONTRACT AGENT", "TRADE FACTOR", "HULL BROKER", "EQUIPMENT DEALER", "CHIEF ENGINEER", "PORT LIAISON"};
const char *service_greetings[PORT_SERVICES] = {
    "Independent captains wanted. Read the notices, speak to a client and agree your terms.",
    "Buying or selling? Our bonded warehouse has local prices and available stock.",
    "Looking for a new ship? Browse the hulls and check the crew and cargo requirements.",
    "Cabins, mining gear and defensive equipment. Tell me what your ship needs.",
    "We can restore your hull, shields and fuel for 100 CR. Authorise the work when ready.",
    "Find crew for your next voyage, review your standing or settle an outstanding fine."};
uint32_t service_identity(const Game *g, int service) {
    return hash32(g->system.seed ^ (uint32_t)(g->docked+1)*7919U ^ (uint32_t)service*104729U);
}
void contact_name(uint32_t id, char *out, size_t size) {
    static const char *first[] = {"MARA","ELIAS","SANA","IVAN","LEIA","OMAR","NIA","TOMAS","YARA","JULES","ADA","REN"};
    static const char *last[] = {"VALE","OKORO","CHEN","SINGH","VOSS","REYES","KATO","HALE","IDRIS","WARD","SATO","QUINN"};
    snprintf(out,size,"%s %s",first[id%12],last[(id/12)%12]);
}
const Mission *selected_mission(const Game *g) {
    if(g->selection<0 || g->selection>=MAX_MISSIONS) return NULL;
    return g->page==LOG || (g->page==MAP && g->mission_map_return==LOG) ? &g->contracts[g->selection] : &g->board[g->selection];
}
const char *mission_refusal(const Game *g, int index) {
    const Mission *m; int i, slot=0;
    if(index<0 || index>=MAX_MISSIONS || g->dead) return "UNAVAILABLE";
    m=&g->board[index];
    if(g->docked<0) return "DOCK TO ACCEPT";
    if(m->state!=OFFERED) return "ALREADY TAKEN";
    if(g->tick>=m->deadline) return "OFFER EXPIRED";
    if(cargo_used(g)+m->quantity>ship_capacity[g->ship]) return "INSUFFICIENT HOLD SPACE";
    if(passengers_used(g)+m->passengers>g->cabins*2) return "MORE CABIN SEATS REQUIRED";
    if(g->reputation[m->faction]<m->required_rep) return "REPUTATION TOO LOW";
    if(g->wanted[m->faction]) return "SETTLE FACTION FINE FIRST";
    if(m->type==ESCORT && g->crew<ship_specs[g->ship].crew) return "FULL SHIP CREW REQUIRED";
    for(i=0;i<MAX_MISSIONS;i++) {
        if(g->contracts[i].id==m->id) return "ALREADY IN YOUR LOG";
        if(g->contracts[i].state!=ACTIVE) slot=1;
    }
    return slot?NULL:"CONTRACT LOG FULL";
}
void mission_port_name(const Game *g, const Mission *m, char *out, size_t size) {
    static uint32_t seed; static Sector sector; static int port=-1, ready;
    static char cached[24];
    if(!ready || seed!=g->seed || !same_sector(sector,m->destination) || port!=m->port) {
        System dest; int b,i,n=0;
        seed=g->seed; sector=m->destination; port=m->port; ready=1;
        generate_system(seed,sector,&dest);
        snprintf(cached,sizeof(cached),"PORT %d",port+1);
        for(b=0;b<dest.count;b++) if(dest.planets[b].settled) for(i=0;i<PORTS_PER_PLANET;i++) {
            if(dest.planets[b].type==GAS_GIANT && i%2==0) continue;
            if(n++==port) snprintf(cached,sizeof(cached),"%.12s %c %s",dest.name,'B'+b,
                (const char*[]){"HAVEN","RELAY","REACH","ANCHOR"}[i]);
        }
    }
    snprintf(out,size,"%s",cached);
}
const char *mission_story(const Mission *m) {
    static const char *stories[MISSION_TYPES][2] = {
        {"A relay failure has cut our outpost off from its families. Carry their sealed messages before the next departure window closes.", "An archivist has recovered testimony from a lost expedition. Deliver the sealed record before its witnesses leave the system."},
        {"The next supply ship never arrived. Our workshops are running out of essentials; your cargo could keep the settlement working.", "A stranded convoy has left a buyer without supplies. We need an independent captain to complete the delivery before the stores close."},
        {"Freighters report armed raiders on the orbital approach. Clear the shipping lane so our civilian traffic can move again.", "A missing patrol has left the upper approaches unguarded. Find the hostile contact before another freighter disappears."},
        {"Pilots have seen an armed intruder below the cloud deck. Our surface crews cannot work while it remains overhead.", "A settlement's rescue flights are being threatened by a low-flying raider. Intercept it in the atmosphere."},
        {"Old survey records disagree with the terrain below. We need a fresh low-altitude instrument run before the next expedition lands.", "A research team suspects the planetary charts are wrong. Fly a careful survey and bring back a usable set of measurements."},
        {"A raider has been preying on small traders. Local families have pooled this reward to end the attacks.", "A wanted pilot has returned to the system. Our informant can identify the ship; the authorities will pay for its destruction."},
        {"These travellers missed the last liner after a family emergency. They need a captain with cabin space and a reliable arrival time.", "A field medic and companions have been recalled from the frontier. Give them safe passage to their waiting colleagues."},
        {"A civilian freighter carries the last stock of a struggling cooperative. Raiders know its route. Stay close and keep its crew alive.", "Our relief ship has no combat escort. Protect it through the dangerous part of its passage; losing the ship means losing the supplies."},
        {"A broken distress call is all we have from a missing ship. Find the escape capsule and return its survivor to port.", "An emergency beacon has appeared above the planet. Someone is still alive out there. Bring them home before the beacon falls silent."},
        {"A faction intermediary wants a rival operative removed. The payment is real, but the killing will bring a rival faction fine.", "A private feud has reached the shipping lanes. Our client wants one pilot dead. Accept only if you can live with the legal consequences."},
        {"A surface outpost has stopped answering routine calls. Pass close enough to record its activity without landing.", "Our observers report unusual traffic around a remote settlement. Make a low, slow reconnaissance pass and confirm their suspicions."},
        {"A hostile installation threatens the port's approaches. Its defences are active. Destroy it so the settlement can reopen the route.", "A fortified raider site has appeared near the surface port. The local authority is paying for its destruction."},
        {"Three outposts share one fragile supply chain. Carry the dispatch through each scheduled stop; payment follows the final delivery.", "A cooperative needs a trusted captain for a three-stop delivery run. Each port will pass you the dispatch for the next leg."}
    };
    return stories[m->type][hash32(m->id)%2];
}
const char *mission_orders(const Mission *m) {
    static const char *orders[MISSION_TYPES] = {
        "Dock at the destination port before the deadline. The sealed dispatch is transferred automatically. No cargo space is required.",
        "Carry the reserved freight to the destination port and dock before the deadline. Contract cargo occupies hold space until delivery.",
        "In the destination system, fly undocked above 13 KM to encounter the hostile ship. Destroy the assigned target with your weapons.",
        "In the destination system, fly at 600-6000 M altitude to encounter the intruder. Destroy the assigned hostile ship.",
        "In the destination system, fly undocked at 500-6000 M altitude below 200 M/S relative to the surface for 20 seconds of survey time.",
        "In the destination system, fly undocked above 13 KM to locate the wanted ship. Destroy the assigned target to claim the bounty.",
        "Reserve the stated cabin seats, then dock at the destination port before the deadline. Passengers disembark automatically.",
        "Fly undocked above 13 KM in the destination system to meet the freighter. Remain within 5 KM for 120 seconds and keep it alive.",
        "Fly undocked above 13 KM in the destination system. Approach the capsule and press N to collect it. Dock at the named port with the survivor.",
        "Fly undocked above 13 KM in the destination system and destroy the assigned ship. Success incurs a 1000 CR fine with a rival faction.",
        "Fly undocked within 10 KM of the named surface port at 500-6000 M altitude below 200 M/S relative to the surface for 30 seconds.",
        "Approach within 10 KM of the named surface port while undocked. Destroy the hostile installation that appears near the port.",
        "Dock at the named port, then visit the next two ports shown in your log. Check the updated port after each leg. Payment follows the final stop."
    };
    return orders[m->type];
}
int services_key(Game *g, int k) {
    int missions=g->page==BOARD || g->page==LOG;
    if(k==SERVICE_DIRECTORY || (k=='s' && g->docked>=0 && !g->on_foot && g->page!=MAP)) { g->page=SERVICES; g->mission_map_return=0; return 1; }
    if(g->page==MAP && k==MISSION_RETURN && g->mission_map_return) {
        g->page=g->mission_map_return; g->selection=g->mission_map_selection;
        g->mission_detail=1; g->mission_map_return=0; return 1;
    }
    if(g->page==SERVICES) {
        if(k>=SERVICE_SELECT && k<SERVICE_SELECT+PORT_SERVICES) g->service_selection=k-SERVICE_SELECT;
        else if(k>='1' && k<'1'+PORT_SERVICES) g->service_selection=k-'1';
        else if(k==1001 || k==1002) g->service_selection=(g->service_selection+PORT_SERVICES+(k==1001?-1:1))%PORT_SERVICES;
        else if(k==SERVICE_CALL || k=='\r') {
            static const int pages[]={BOARD,MARKET,SHIPYARD,EQUIPMENT,SERVICES,CAREER};
            if(g->docked<0) return 1;
            if(g->service_selection==4) game_action(g,'f');
            else { g->page=pages[g->service_selection]; g->selection=0; g->mission_detail=0; }
        } else return 0;
        return 1;
    }
    if(!missions) return 0;
    if(k==MISSION_LIST) { g->mission_detail=0; return 1; }
    if(k>=MISSION_TAB && k<MISSION_TAB+3) { g->mission_tab=k-MISSION_TAB; return 1; }
    if(k==MISSION_OPEN || (k=='\r' && (!g->mission_detail || g->page==LOG))) {
        if(selected_mission(g)->id) { g->mission_detail=1; g->mission_tab=0; } return 1;
    }
    if(k==MISSION_MAP) {
        const Mission *m=selected_mission(g);
        if(!m || !m->id) return 1;
        if(!g->galaxy_ready) galaxy_reset(g);
        g->mission_map_return=g->page; g->mission_map_selection=g->selection;
        g->selected=m->destination; g->galaxy_query[0]=0; g->galaxy_searching=0;
        g->galaxy_detail=0; g->galaxy_extent=12000; galaxy_focus(g,g->selected);
        if(same_sector(g->system.sector,m->destination) && m->port<g->nports) {
            g->target=m->port; g->target_body=-1; g->resource_target=-1; g->clearance=-1; g->dock_stage=0;
        }
        g->page=MAP; return 1;
    }
    if(k==1001 || k==1002) { g->selection=(g->selection+MAX_MISSIONS+(k==1001?-1:1))%MAX_MISSIONS; g->mission_detail=0; return 1; }
    return 0;
}
