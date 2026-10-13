#pragma once
// HTTPS to the game server on a background thread (curl with mbedTLS: the console's own HTTPS is too old for it).
// Requests queue up and run one at a time; the main loop picks finished ones up with net_poll() and never waits.
#include <stdbool.h>

typedef struct { int kind, status; char *body; } NetResult;   // status 0 = no connection; body is malloc'd, NUL-ended

void net_init(void);
void net_exit(void);
// queue a request; path is relative to the server ("/api/3ds/state"); body NULL for GET. auth: "Device <token>" or NULL
bool net_send(int kind, const char *method, const char *path, const char *body, const char *auth);
bool net_poll(NetResult *out);            // a finished request, if any; free out->body after
bool net_busy(void);
void net_set_server(const char *url);     // https://... with no trailing slash
const char *net_server(void);
