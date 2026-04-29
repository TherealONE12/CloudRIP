#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <microhttpd.h>
#include <sqlite3.h>
#include <sodium.h>
#include "../include/api_handlers.h"
#include "../include/multimanager.h"
#include "../include/docker.h"

extern sqlite3 *db;

// Admin token storage (simple in-memory storage for current session)
#define MAX_ADMIN_TOKENS 10
typedef struct {
    char token[65];
    time_t expires;
} AdminToken;

static AdminToken admin_tokens[MAX_ADMIN_TOKENS];
static int admin_token_count = 0;

// Store admin token
static void store_admin_token(const char *token) {
    if (admin_token_count < MAX_ADMIN_TOKENS) {
        strcpy(admin_tokens[admin_token_count].token, token);
        admin_tokens[admin_token_count].expires = time(NULL) + 3600; // 1 hour expiry
        admin_token_count++;
    }
}

// Validate admin token
static int validate_admin_token(const char *token) {
    if (!token) return -1;
    time_t now = time(NULL);
    for (int i = 0; i < admin_token_count; i++) {
        if (admin_tokens[i].expires > now && strcmp(admin_tokens[i].token, token) == 0) {
            return 0;
        }
    }
    return -1;
}

// Prüfe ob ein Endpoint durch die neue API behandelt werden soll
int should_handle_api_route(const char *url) {
    return strstr(url, "/api/") != NULL;
}

// Parse Authorization header
char *get_token_from_header(struct MHD_Connection *connection) {
    const char *auth = MHD_lookup_connection_value(connection, MHD_HEADER_KIND, "Authorization");
    if (!auth) return NULL;
    
    if (strncmp(auth, "Bearer ", 7) == 0) {
        return strdup(auth + 7);
    }
    return NULL;
}

// Helper function to check admin authorization (accepts both admin tokens and user admin tokens)
static int check_admin_auth(const char *token) {
    if (!token) return -1;
    
    // Try admin token first
    if (validate_admin_token(token) == 0) {
        return 0;
    }
    
    // Try user token with admin privileges
    int userid = -1, is_admin = 0;
    if (validate_token(token, &userid, &is_admin) == 0 && is_admin) {
        return 0;
    }
    
    return -1;
}

// Route API requests
enum MHD_Result route_api_request(
    struct MHD_Connection *connection,
    const char *url,
    const char *method,
    const char *body,
    size_t body_size
) {
    char json_response[4096];
    int status = MHD_HTTP_OK;
    
    // /api/login
    if (strcmp(url, "/api/login") == 0 && strcmp(method, "POST") == 0) {
        fprintf(stderr, "[DEBUG] /api/login called, body length: %zu\n", body ? strlen(body) : 0);
        fprintf(stderr, "[DEBUG] Body: %s\n", body ? body : "(null)");
        
        ApiRequest req;
        if (parse_auth_request(body, &req) != 0) {
            fprintf(stderr, "[DEBUG] parse_auth_request failed\n");
            snprintf(json_response, sizeof(json_response), "{\"error\":\"Invalid request\"}");
            status = MHD_HTTP_BAD_REQUEST;
        } else {
            fprintf(stderr, "[DEBUG] Parsed: username=[%s], password=[%s]\n", req.username, req.password);
            int userid = -1;
            char pwhash[256] = {0};
            
            if (get_userid_from_username_local(req.username, &userid) == 0 &&
                get_password_hash(req.username, pwhash, sizeof(pwhash)) == 0 &&
                verifypassword(req.password, pwhash)) {
                fprintf(stderr, "[DEBUG] Auth success for userid %d\n", userid);
                
                char *token = generate_new_token(userid);
                if (token) {
                    snprintf(json_response, sizeof(json_response), 
                        "{\"token\":\"%s\",\"userid\":%d}", token, userid);
                    write_log(userid, -1, "login", "");
                    free(token);
                } else {
                    fprintf(stderr, "[DEBUG] Token generation failed\n");
                    snprintf(json_response, sizeof(json_response), "{\"error\":\"Token generation failed\"}");
                    status = MHD_HTTP_INTERNAL_SERVER_ERROR;
                }
            } else {
                fprintf(stderr, "[DEBUG] Auth failed\n");
                snprintf(json_response, sizeof(json_response), "{\"error\":\"Invalid credentials\"}");
                status = MHD_HTTP_UNAUTHORIZED;
            }
        }
    }
    
    // /api/register
    else if (strcmp(url, "/api/register") == 0 && strcmp(method, "POST") == 0) {
        ApiRequest req;
        if (parse_auth_request(body, &req) != 0) {
            snprintf(json_response, sizeof(json_response), "{\"error\":\"Invalid request\"}");
            status = MHD_HTTP_BAD_REQUEST;
        } else {
            register_user(req.username, req.password, json_response, sizeof(json_response));
        }
    }
    
    // /api/games
    else if (strcmp(url, "/api/games") == 0 && strcmp(method, "GET") == 0) {
        char *token = get_token_from_header(connection);
        int userid = -1, is_admin = 0;
        
        if (!token || validate_token(token, &userid, &is_admin) != 0) {
            snprintf(json_response, sizeof(json_response), "{\"error\":\"Unauthorized\"}");
            status = MHD_HTTP_UNAUTHORIZED;
            free(token);
        } else {
            char *games = get_games_list();
            if (strlen(games) < 4096) {
                strcpy(json_response, games);
            } else {
                snprintf(json_response, sizeof(json_response), "{\"error\":\"Response too large\"}");
                status = MHD_HTTP_INTERNAL_SERVER_ERROR;
            }
            free(games);
            free(token);
        }
    }
    
    // /api/admin/auth - Allow password-only authentication for admin panel
    else if (strcmp(url, "/api/admin/auth") == 0 && strcmp(method, "POST") == 0) {
        char *token = get_token_from_header(connection);
        int userid = -1, is_admin = 0;
        
        // Parse password from body
        char password[200] = {0};
        char *p_start = strstr(body, "password");
        if (p_start) {
            p_start = strchr(p_start, ':');
            if (p_start) {
                p_start++;
                while (*p_start && (*p_start == ' ' || *p_start == ':' || *p_start == '"')) p_start++;
                char *p_end = strchr(p_start, '"');
                if (p_end) {
                    int p_len = p_end - p_start;
                    if (p_len > 199) p_len = 199;
                    strncpy(password, p_start, p_len);
                }
            }
        }
        
        // If no token provided, authenticate with password only
        if (!token) {
            // Admin password check (hardcoded for now)
            const char *admin_password = "admin";
            if (strcmp(password, admin_password) == 0) {
                // Generate admin token (32 random bytes, hex-encoded)
                unsigned char random_bytes[32];
                randombytes(random_bytes, sizeof(random_bytes));
                char hex_token[65];
                sodium_bin2hex(hex_token, sizeof(hex_token), random_bytes, sizeof(random_bytes));
                
                // Store the token
                store_admin_token(hex_token);
                
                snprintf(json_response, sizeof(json_response), 
                    "{\"admin_token\":\"%s\"}", hex_token);
                status = MHD_HTTP_OK;
            } else {
                snprintf(json_response, sizeof(json_response), "{\"error\":\"Authentication failed\"}");
                status = MHD_HTTP_UNAUTHORIZED;
            }
        } else {
            // Token provided: validate it AND check password
            if (validate_token(token, &userid, &is_admin) != 0) {
                snprintf(json_response, sizeof(json_response), "{\"error\":\"Unauthorized\"}");
                status = MHD_HTTP_UNAUTHORIZED;
            } else {
                char *admin_token = authenticate_admin(userid, password);
                if (admin_token && is_admin) {
                    store_admin_token(admin_token);
                    snprintf(json_response, sizeof(json_response), 
                        "{\"admin_token\":\"%s\"}", admin_token);
                    free(admin_token);
                } else {
                    snprintf(json_response, sizeof(json_response), "{\"error\":\"Authentication failed\"}");
                    status = MHD_HTTP_UNAUTHORIZED;
                }
            }
            free(token);
        }
    }
    
    // /api/admin/games
    else if (strcmp(url, "/api/admin/games") == 0 && strcmp(method, "GET") == 0) {
        char *token = get_token_from_header(connection);
        
        // Check if this is an admin token OR a user token with admin privileges
        int is_valid = 0;
        if (token) {
            // Try admin token first
            if (validate_admin_token(token) == 0) {
                is_valid = 1;
            } else {
                // Try user token
                int userid = -1, is_admin = 0;
                if (validate_token(token, &userid, &is_admin) == 0 && is_admin) {
                    is_valid = 1;
                }
            }
        }
        
        if (!is_valid) {
            snprintf(json_response, sizeof(json_response), "{\"error\":\"Unauthorized\"}");
            status = MHD_HTTP_UNAUTHORIZED;
            free(token);
        } else {
            char *games = get_all_games();
            free(token);
            struct MHD_Response *resp = MHD_create_response_from_buffer(
                strlen(games), games, MHD_RESPMEM_MUST_FREE);
            MHD_add_response_header(resp, "Content-Type", "application/json");
            MHD_queue_response(connection, MHD_HTTP_OK, resp);
            MHD_destroy_response(resp);
            return MHD_YES;
        }
    }
    
    // /api/admin/game (POST - add game)
    else if (strcmp(url, "/api/admin/game") == 0 && strcmp(method, "POST") == 0) {
        char *token = get_token_from_header(connection);
        
        if (!token || check_admin_auth(token) != 0) {
            snprintf(json_response, sizeof(json_response), "{\"error\":\"Unauthorized\"}");
            status = MHD_HTTP_UNAUTHORIZED;
            free(token);
        } else {
            ApiRequest req;
            parse_game_request(body, &req);
            add_game(req.gamename, req.gamedesc, req.command, json_response, sizeof(json_response));
            free(token);
        }
    }
    
    // /api/admin/game/{gameid} (DELETE)
    else if (strstr(url, "/api/admin/game/") != NULL && strcmp(method, "DELETE") == 0) {
        char *token = get_token_from_header(connection);
        
        if (!token || check_admin_auth(token) != 0) {
            snprintf(json_response, sizeof(json_response), "{\"error\":\"Unauthorized\"}");
            status = MHD_HTTP_UNAUTHORIZED;
            free(token);
        } else {
            int gameid = atoi(url + strlen("/api/admin/game/"));
            delete_game(gameid, json_response, sizeof(json_response));
            free(token);
        }
    }
    
    // /api/admin/game/{gameid}/toggle (PUT)
    else if (strstr(url, "/api/admin/game/") != NULL && strstr(url, "/toggle") != NULL && strcmp(method, "PUT") == 0) {
        char *token = get_token_from_header(connection);
        
        if (!token || check_admin_auth(token) != 0) {
            snprintf(json_response, sizeof(json_response), "{\"error\":\"Unauthorized\"}");
            status = MHD_HTTP_UNAUTHORIZED;
            free(token);
        } else {
            // Parse gameid from URL like /api/admin/game/5/toggle
            int gameid = atoi(url + strlen("/api/admin/game/"));
            toggle_game(gameid, json_response, sizeof(json_response));
            free(token);
        }
    }
    
    // /api/admin/logs
    else if (strcmp(url, "/api/admin/logs") == 0 && strcmp(method, "GET") == 0) {
        char *token = get_token_from_header(connection);

        if (!token || check_admin_auth(token) != 0) {
            snprintf(json_response, sizeof(json_response), "{\"error\":\"Unauthorized\"}");
            status = MHD_HTTP_UNAUTHORIZED;
            free(token);
        } else {
            char *logs = get_logs_json(-1, 1);
            free(token);
            struct MHD_Response *resp = MHD_create_response_from_buffer(
                strlen(logs), logs, MHD_RESPMEM_MUST_FREE);
            MHD_add_response_header(resp, "Content-Type", "application/json");
            MHD_queue_response(connection, MHD_HTTP_OK, resp);
            MHD_destroy_response(resp);
            return MHD_YES;
        }
    }
    
    // /api/session/start (POST)
    else if (strcmp(url, "/api/session/start") == 0 && strcmp(method, "POST") == 0) {
        char *token = get_token_from_header(connection);
        int userid = -1, is_admin = 0;
        
        if (!token || validate_token(token, &userid, &is_admin) != 0) {
            snprintf(json_response, sizeof(json_response), "{\"error\":\"Unauthorized\"}");
            status = MHD_HTTP_UNAUTHORIZED;
            free(token);
        } else {
            // Parse gameid from body
            ApiRequest req;
            parse_game_request(body, &req);
            
            char sessionid[32] = {0};
            char dummy_sdp[32] = {0};
            
            if (start_session(userid, req.gameid, sessionid, dummy_sdp, json_response, sizeof(json_response)) == 0) {
                status = MHD_HTTP_OK;
            } else {
                status = MHD_HTTP_INTERNAL_SERVER_ERROR;
            }
            free(token);
        }
    }
    
    // /api/session/answer (POST)
    else if (strcmp(url, "/api/session/answer") == 0 && strcmp(method, "POST") == 0) {
        char *token = get_token_from_header(connection);
        int userid = -1, is_admin = 0;
        
        if (!token || validate_token(token, &userid, &is_admin) != 0) {
            snprintf(json_response, sizeof(json_response), "{\"error\":\"Unauthorized\"}");
            status = MHD_HTTP_UNAUTHORIZED;
            free(token);
        } else {
            // Parse sessionid and SDP from body
            int sessionid = userid;  // For now, sessionid = userid
            char *sdp_start = strstr(body, "\"sdp\"");
            char sdp[8192] = {0};
            
            if (sdp_start) {
                sdp_start = strchr(sdp_start, ':');
                if (sdp_start) {
                    sdp_start++;
                    while (*sdp_start && (*sdp_start == ' ' || *sdp_start == '"')) sdp_start++;
                    
                    // Find the actual end of the SDP string by scanning for unescaped closing quote
                    char *sdp_end = NULL;
                    int escaped = 0;
                    for (char *p = sdp_start; *p; p++) {
                        if (*p == '\\' && !escaped) {
                            escaped = 1;
                            continue;
                        }
                        if (*p == '"' && !escaped) {
                            sdp_end = p;
                            break;
                        }
                        escaped = 0;
                    }
                    
                    if (sdp_end) {
                        int len = sdp_end - sdp_start;
                        if (len < 8192) {
                            strncpy(sdp, sdp_start, len);
                            // Unescape the SDP (convert \\n to \n, etc.)
                            char *src = sdp, *dst = sdp;
                            while (*src) {
                                if (*src == '\\' && *(src + 1)) {
                                    switch (*(src + 1)) {
                                        case 'n': *dst = '\n'; src += 2; dst++; break;
                                        case 'r': *dst = '\r'; src += 2; dst++; break;
                                        case 't': *dst = '\t'; src += 2; dst++; break;
                                        case '"': *dst = '"'; src += 2; dst++; break;
                                        case '\\': *dst = '\\'; src += 2; dst++; break;
                                        default: *dst = *src; src++; dst++;
                                    }
                                } else {
                                    *dst = *src;
                                    src++;
                                    dst++;
                                }
                            }
                            *dst = '\0';  // Null terminate
                        }
                    }
                }
            }
            
            if (answer_session(userid, sessionid, sdp, json_response, sizeof(json_response)) == 0) {
                status = MHD_HTTP_OK;
            } else {
                status = MHD_HTTP_BAD_REQUEST;
            }
            free(token);
        }
    }
    
    // /api/session/candidate (POST)
    else if (strcmp(url, "/api/session/candidate") == 0 && strcmp(method, "POST") == 0) {
        char *token = get_token_from_header(connection);
        int userid = -1, is_admin = 0;
        
        if (!token || validate_token(token, &userid, &is_admin) != 0) {
            snprintf(json_response, sizeof(json_response), "{\"error\":\"Unauthorized\"}");
            status = MHD_HTTP_UNAUTHORIZED;
            free(token);
        } else {
            // Parse candidate and mid from body
            int sessionid = userid;
            char *cand_start = strstr(body, "\"candidate\"");
            char candidate[512] = {0};
            char mid[16] = {0};
            
            if (cand_start) {
                cand_start = strchr(cand_start, ':');
                if (cand_start) {
                    cand_start++;
                    while (*cand_start && (*cand_start == ' ' || *cand_start == '"')) cand_start++;
                    char *cand_end = strstr(cand_start, "\"");
                    if (cand_end) {
                        int len = cand_end - cand_start;
                        if (len < 512) strncpy(candidate, cand_start, len);
                    }
                }
            }
            
            // Parse mid
            char *mid_start = strstr(body, "\"mid\"");
            if (mid_start) {
                mid_start = strchr(mid_start, ':');
                if (mid_start) {
                    mid_start++;
                    while (*mid_start && (*mid_start == ' ' || *mid_start == '"')) mid_start++;
                    char *mid_end = strchr(mid_start, '"');
                    if (mid_end) {
                        int len = mid_end - mid_start;
                        if (len < 16) strncpy(mid, mid_start, len);
                    }
                }
            }
            
            if (add_ice_candidate(userid, sessionid, candidate, mid, json_response, sizeof(json_response)) == 0) {
                status = MHD_HTTP_OK;
            } else {
                status = MHD_HTTP_BAD_REQUEST;
            }
            free(token);
        }
    }
    
    // /api/session/candidates (GET) - Retrieve server-side ICE candidates for polling
    else if (strstr(url, "/api/session/candidates") != NULL && strcmp(method, "GET") == 0) {
        char *token = get_token_from_header(connection);
        int userid = -1, is_admin = 0;
        
        if (!token || validate_token(token, &userid, &is_admin) != 0) {
            snprintf(json_response, sizeof(json_response), "{\"error\":\"Unauthorized\"}");
            status = MHD_HTTP_UNAUTHORIZED;
            free(token);
        } else {
            // Parse optional 'index' parameter from URL (e.g., /api/session/candidates?index=5)
            int start_index = 0;
            const char *query_start = strchr(url, '?');
            if (query_start && strstr(query_start, "index=")) {
                const char *idx_str = strstr(query_start, "index=") + 6;
                start_index = atoi(idx_str);
            }
            
            int sessionid = userid;
            if (sessionid < 0 || sessionid >= MAX_USERS) {
                snprintf(json_response, sizeof(json_response), "{\"error\":\"Invalid session\"}");
                status = MHD_HTTP_BAD_REQUEST;
            } else {
                // Build JSON array of candidates
                char candidates_json[8192] = {0};
                int candidates_count = sessions[sessionid].candidate_count;
                int new_candidates_count = (candidates_count > start_index) ? (candidates_count - start_index) : 0;
                
                strcat(candidates_json, "[");
                for (int i = start_index; i < candidates_count; i++) {
                    if (i > start_index) strcat(candidates_json, ",");
                    strcat(candidates_json, "{\"candidate\":\"");
                    strcat(candidates_json, sessions[sessionid].candidates[i]);
                    strcat(candidates_json, "\",\"sdpMLineIndex\":0,\"sdpMid\":\"video\"}");
                }
                strcat(candidates_json, "]");
                
                snprintf(json_response, sizeof(json_response), 
                    "{\"candidates\":%s,\"total\":%d}", 
                    candidates_json, candidates_count);
                status = MHD_HTTP_OK;
            }
            free(token);
        }
    }
    
    // /api/input (POST) - Send keyboard/mouse input to Docker container
    else if (strcmp(url, "/api/input") == 0 && strcmp(method, "POST") == 0) {
        char *token = get_token_from_header(connection);
        int userid = -1, is_admin = 0;
        
        if (!token || validate_token(token, &userid, &is_admin) != 0) {
            snprintf(json_response, sizeof(json_response), "{\"error\":\"Unauthorized\"}");
            status = MHD_HTTP_UNAUTHORIZED;
            free(token);
        } else {
            // Call docker_injekt_Keystroke with the body (JSON input data)
            int ret = docker_injekt_Keystroke((char*)body, sessions[userid].vdisplay);
            if (ret == 0) {
                snprintf(json_response, sizeof(json_response), "{\"status\":\"ok\"}");
                status = MHD_HTTP_OK;
            } else {
                snprintf(json_response, sizeof(json_response), "{\"error\":\"Input injection failed\"}");
                status = MHD_HTTP_BAD_REQUEST;
            }
            free(token);
        }
    }
    
    else {
        snprintf(json_response, sizeof(json_response), "{\"error\":\"Not found\"}");
        status = MHD_HTTP_NOT_FOUND;
    }
    
    // Send response
    struct MHD_Response *response = MHD_create_response_from_buffer_copy(
        strlen(json_response), json_response);
    MHD_add_response_header(response, "Content-Type", "application/json");
    MHD_queue_response(connection, status, response);
    MHD_destroy_response(response);
    
    return MHD_YES;
}
