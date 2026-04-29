#ifndef API_ROUTES_H
#define API_ROUTES_H

#include <microhttpd.h>

int should_handle_api_route(const char *url);
char *get_token_from_header(struct MHD_Connection *connection);
enum MHD_Result route_api_request(
    struct MHD_Connection *connection,
    const char *url,
    const char *method,
    const char *body,
    size_t body_size
);

#endif
