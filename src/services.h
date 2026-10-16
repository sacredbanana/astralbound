/* SPDX-License-Identifier: Unlicense */
#ifndef ASTRALBOUND_SERVICES_H
#define ASTRALBOUND_SERVICES_H
#include "astralbound.h"
#define PORT_SERVICES 6
#define SERVICE_DIRECTORY 13000
#define SERVICE_CALL 13001
#define SERVICE_SELECT 13010
#define MISSION_OPEN 13100
#define MISSION_LIST 13101
#define MISSION_MAP 13102
#define MISSION_RETURN 13103
#define MISSION_TAB 13110
extern const char *service_names[PORT_SERVICES];
extern const char *service_roles[PORT_SERVICES];
extern const char *service_greetings[PORT_SERVICES];
uint32_t service_identity(const Game *g, int service);
void contact_name(uint32_t identity, char *out, size_t size);
const Mission *selected_mission(const Game *g);
const char *mission_refusal(const Game *g, int index);
void mission_port_name(const Game *g, const Mission *m, char *out, size_t size);
const char *mission_story(const Mission *m);
const char *mission_orders(const Mission *m);
int services_key(Game *g, int key);
#endif
