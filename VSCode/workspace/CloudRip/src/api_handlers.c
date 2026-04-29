#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sqlite3.h>
#include <sodium.h>
#include <stdint.h>
#include <unistd.h>
#include "../include/multimanager.h"
#include "../include/api_handlers.h"


// Escape string for JSON: converts \n, \r, \t, ", \ to JSON-safe versions
void json_escape_string(const char *src, char *dst, size_t dst_size) {
    if (!src || !dst || dst_size < 2) return;
    
    size_t out_pos = 0;
    for (size_t i = 0; src[i] && out_pos < dst_size - 2; i++) {
        switch (src[i]) {
            case '\n':
                if (out_pos + 2 < dst_size) {
                    dst[out_pos++] = '\\';
                    dst[out_pos++] = 'n';
                }
                break;
            case '\r':
                if (out_pos + 2 < dst_size) {
                    dst[out_pos++] = '\\';
                    dst[out_pos++] = 'r';
                }
                break;
            case '\t':
                if (out_pos + 2 < dst_size) {
                    dst[out_pos++] = '\\';
                    dst[out_pos++] = 't';
                }
                break;
            case '"':
                if (out_pos + 2 < dst_size) {
                    dst[out_pos++] = '\\';
                    dst[out_pos++] = '"';
                }
                break;
            case '\\':
                if (out_pos + 2 < dst_size) {
                    dst[out_pos++] = '\\';
                    dst[out_pos++] = '\\';
                }
                break;
            default:
                if (out_pos < dst_size - 1) {
                    dst[out_pos++] = src[i];
                }
                break;
        }
    }
    dst[out_pos] = '\0';
}

// JSON-Parser für Login/Register
int parse_auth_request(const char *body, ApiRequest *req) {
    memset(req, 0, sizeof(ApiRequest));
    
    if (!body || strlen(body) == 0) return -1;
    
    // Username: flexible parsing that allows spaces
    char *u_start = strstr(body, "username");
    if (!u_start) return -1;
    u_start = strchr(u_start, ':');
    if (!u_start) return -1;
    u_start++;
    
    // Skip whitespace and quotes
    while (*u_start && (*u_start == ' ' || *u_start == ':' || *u_start == '"')) u_start++;
    
    char *u_end = strchr(u_start, '"');
    if (!u_end) return -1;
    
    int u_len = u_end - u_start;
    if (u_len > 99) u_len = 99;
    strncpy(req->username, u_start, u_len);
    
    // Password: flexible parsing
    char *p_start = strstr(body, "password");
    if (!p_start) return -1;
    p_start = strchr(p_start, ':');
    if (!p_start) return -1;
    p_start++;
    
    // Skip whitespace and quotes
    while (*p_start && (*p_start == ' ' || *p_start == ':' || *p_start == '"')) p_start++;
    
    char *p_end = strchr(p_start, '"');
    if (!p_end) return -1;
    
    int p_len = p_end - p_start;
    if (p_len > 199) p_len = 199;
    strncpy(req->password, p_start, p_len);
    
    return 0;
}

// Parser für Admin-Anfragen (Game-Upload, etc)
int parse_game_request(const char *body, ApiRequest *req) {
    memset(req, 0, sizeof(ApiRequest));
    
    // Token
    char *t_start = strstr(body, "\"token\":\"");
    if (t_start) {
        t_start += strlen("\"token\":\"");
        char *t_end = strchr(t_start, '"');
        int t_len = t_end - t_start;
        if (t_len > 255) t_len = 255;
        strncpy(req->token, t_start, t_len);
    }
    
    // Game Name
    char *n_start = strstr(body, "\"gamename\":\"");
    if (n_start) {
        n_start += strlen("\"gamename\":\"");
        char *n_end = strchr(n_start, '"');
        int n_len = n_end - n_start;
        if (n_len > 127) n_len = 127;
        strncpy(req->gamename, n_start, n_len);
    }
    
    // Description
    char *d_start = strstr(body, "\"gamedesc\":\"");
    if (d_start) {
        d_start += strlen("\"gamedesc\":\"");
        char *d_end = strchr(d_start, '"');
        int d_len = d_end - d_start;
        if (d_len > 255) d_len = 255;
        strncpy(req->gamedesc, d_start, d_len);
    }
    
    // Command
    char *c_start = strstr(body, "\"command\":\"");
    if (c_start) {
        c_start += strlen("\"command\":\"");
        char *c_end = strchr(c_start, '"');
        int c_len = c_end - c_start;
        if (c_len > 255) c_len = 255;
        strncpy(req->command, c_start, c_len);
    }
    
    // Game ID
    char *g_start = strstr(body, "\"gameid\":");
    if (g_start) {
        req->gameid = atoi(g_start + strlen("\"gameid\":"));
    }
    
    return 0;
}

// Validiere Token und hole User-ID
int validate_token(const char *token, int *out_userid, int *out_is_admin) {
    if (!token || strlen(token) == 0) return -1;
    
    sqlite3_stmt *stmt;
    const char *sql = "SELECT u.userid, u.is_admin FROM tokens t "
                      "JOIN users u ON t.userid = u.userid "
                      "WHERE t.token = ? AND t.expires_at > strftime('%s','now') LIMIT 1;";
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        return -1;
    }
    
    sqlite3_bind_text(stmt, 1, token, -1, SQLITE_STATIC);
    
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        *out_userid = sqlite3_column_int(stmt, 0);
        *out_is_admin = sqlite3_column_int(stmt, 1);
        sqlite3_finalize(stmt);
        return 0;
    }
    
    sqlite3_finalize(stmt);
    return -1;
}

// Hole User-ID von Username
int get_userid_from_username_local(const char *username, int *out_userid) {
    sqlite3_stmt *stmt;
    const char *sql = "SELECT userid FROM users WHERE username = ? LIMIT 1;";
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        return -1;
    }
    
    sqlite3_bind_text(stmt, 1, username, -1, SQLITE_STATIC);
    
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        *out_userid = sqlite3_column_int(stmt, 0);
        sqlite3_finalize(stmt);
        return 0;
    }
    
    sqlite3_finalize(stmt);
    return -1;
}

// Hole Password-Hash von Username
int get_password_hash(const char *username, char *out_hash, size_t hash_size) {
    sqlite3_stmt *stmt;
    const char *sql = "SELECT password_hash FROM users WHERE username = ? LIMIT 1;";
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        return -1;
    }
    
    sqlite3_bind_text(stmt, 1, username, -1, SQLITE_STATIC);
    
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const char *hash = (const char *)sqlite3_column_text(stmt, 0);
        strncpy(out_hash, hash, hash_size - 1);
        out_hash[hash_size - 1] = '\0';
        sqlite3_finalize(stmt);
        return 0;
    }
    
    sqlite3_finalize(stmt);
    return -1;
}

// Generiere neuen Session-Token
char *generate_new_token(int userid) {
    unsigned char raw[32];
    char *hex = malloc(65);
    
    randombytes_buf(raw, 32);
    sodium_bin2hex(hex, 65, raw, 32);
    
    uint64_t expires = (uint64_t)time(NULL) + 3600; // 1 Stunde
    
    sqlite3_stmt *stmt;
    const char *sql = "INSERT INTO tokens (userid, token, expires_at) VALUES (?, ?, ?);";
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        free(hex);
        return NULL;
    }
    
    sqlite3_bind_int(stmt, 1, userid);
    sqlite3_bind_text(stmt, 2, hex, -1, SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 3, expires);
    
    if (sqlite3_step(stmt) != SQLITE_DONE) {
        sqlite3_finalize(stmt);
        free(hex);
        return NULL;
    }
    
    sqlite3_finalize(stmt);
    return hex;
}

// Logs zur DB schreiben
int write_log(int userid, int gameid, const char *action, const char *details) {
    sqlite3_stmt *stmt;
    const char *sql = "INSERT INTO game_logs (userid, gameid, action, details) VALUES (?, ?, ?, ?);";
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        return -1;
    }
    
    sqlite3_bind_int(stmt, 1, userid);
    sqlite3_bind_int(stmt, 2, gameid);
    sqlite3_bind_text(stmt, 3, action, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 4, details, -1, SQLITE_STATIC);
    
    if (sqlite3_step(stmt) != SQLITE_DONE) {
        sqlite3_finalize(stmt);
        return -1;
    }
    
    sqlite3_finalize(stmt);
    return 0;
}

// Hole Logs als JSON
char *get_logs_json(int userid, int is_admin) {
    sqlite3_stmt *stmt;
    const char *sql = is_admin ? 
        "SELECT logid, userid, gameid, action, details, timestamp FROM game_logs ORDER BY timestamp DESC LIMIT 100;" :
        "SELECT logid, userid, gameid, action, details, timestamp FROM game_logs WHERE userid = ? ORDER BY timestamp DESC LIMIT 100;";
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        return NULL;
    }
    
    if (!is_admin) {
        sqlite3_bind_int(stmt, 1, userid);
    }
    
    size_t bufsize = 1024;
    char *buf = malloc(bufsize);
    size_t pos = 0;
    
    strcpy(buf, "[");
    pos = 1;
    
    int first = 1;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        if (!first) {
            if (pos + 2 >= bufsize) {
                bufsize *= 2;
                buf = realloc(buf, bufsize);
            }
            strcat(buf, ",");
            pos++;
        }
        first = 0;
        
        int logid = sqlite3_column_int(stmt, 0);
        int uid = sqlite3_column_int(stmt, 1);
        int gid = sqlite3_column_int(stmt, 2);
        const char *action = (const char *)sqlite3_column_text(stmt, 3);
        const char *details = (const char *)sqlite3_column_text(stmt, 4);
        int ts = sqlite3_column_int(stmt, 5);
        
        char entry[512];
        snprintf(entry, sizeof(entry), "{\"logid\":%d,\"userid\":%d,\"gameid\":%d,\"action\":\"%s\",\"details\":\"%s\",\"timestamp\":%d}",
            logid, uid, gid, action ? action : "", details ? details : "", ts);
        
        if (pos + strlen(entry) + 2 >= bufsize) {
            bufsize = bufsize * 2 + strlen(entry) + 10;
            buf = realloc(buf, bufsize);
        }
        
        strcat(buf, entry);
        pos += strlen(entry);
    }
    
    sqlite3_finalize(stmt);
    
    if (pos + 2 >= bufsize) {
        bufsize += 10;
        buf = realloc(buf, bufsize);
    }
    strcat(buf, "]");
    
    return buf;
}

// Neue API-Endpunkte für Frontend

// Registriere neuen Benutzer
int register_user(const char *username, const char *password, char *out_json, size_t json_size) {
    if (!username || !password || strlen(username) < 2 || strlen(password) < 4) {
        snprintf(out_json, json_size, "{\"error\":\"Invalid username or password\"}");
        return -1;
    }

    sqlite3_stmt *stmt;
    const char *check_sql = "SELECT userid FROM users WHERE username = ? LIMIT 1;";
    
    if (sqlite3_prepare_v2(db, check_sql, -1, &stmt, NULL) != SQLITE_OK) {
        snprintf(out_json, json_size, "{\"error\":\"DB error\"}");
        return -1;
    }

    sqlite3_bind_text(stmt, 1, username, -1, SQLITE_STATIC);
    
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        sqlite3_finalize(stmt);
        snprintf(out_json, json_size, "{\"error\":\"User already exists\"}");
        return -1;
    }
    sqlite3_finalize(stmt);

    char hash[256];
    if (hashpassword(password, hash) != 0) {
        snprintf(out_json, json_size, "{\"error\":\"Password hashing failed\"}");
        return -1;
    }

    const char *insert_sql = "INSERT INTO users (username, password_hash) VALUES (?, ?);";
    
    if (sqlite3_prepare_v2(db, insert_sql, -1, &stmt, NULL) != SQLITE_OK) {
        snprintf(out_json, json_size, "{\"error\":\"DB error\"}");
        return -1;
    }

    sqlite3_bind_text(stmt, 1, username, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, hash, -1, SQLITE_STATIC);
    
    if (sqlite3_step(stmt) != SQLITE_DONE) {
        sqlite3_finalize(stmt);
        snprintf(out_json, json_size, "{\"error\":\"User creation failed\"}");
        return -1;
    }

    sqlite3_finalize(stmt);
    snprintf(out_json, json_size, "{\"success\":true}");
    return 0;
}

// Hole alle aktiven Games als JSON
char *get_games_list() {
    sqlite3_stmt *stmt;
    const char *sql = "SELECT gameid, gamename, gamedescription FROM games WHERE is_active = 1 ORDER BY gameid;";
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        char *empty = malloc(3);
        strcpy(empty, "[]");
        return empty;
    }

    size_t bufsize = 4096;
    char *buf = malloc(bufsize);
    int len = 0;
    len += snprintf(buf + len, bufsize - len, "[");

    int first = 1;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        int gid = sqlite3_column_int(stmt, 0);
        const char *name = (const char *)sqlite3_column_text(stmt, 1);
        const char *desc = (const char *)sqlite3_column_text(stmt, 2);

        len += snprintf(buf + len, bufsize - len, "%s{\"gameid\":%d,\"gamename\":\"%s\",\"gamedescription\":\"%s\"}",
            first ? "" : ",", gid, name ? name : "", desc ? desc : "");
        first = 0;
    }

    sqlite3_finalize(stmt);
    len += snprintf(buf + len, bufsize - len, "]");

    return buf;
}

// Admin: Alle Games (auch inaktive)
char *get_all_games() {
    sqlite3_stmt *stmt;
    const char *sql = "SELECT gameid, gamename, gamedescription, launch_command, is_active FROM games ORDER BY gameid;";
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        char *empty = malloc(3);
        strcpy(empty, "[]");
        return empty;
    }

    size_t bufsize = 4096;
    char *buf = malloc(bufsize);
    int len = 0;
    len += snprintf(buf + len, bufsize - len, "[");

    int first = 1;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        int gid = sqlite3_column_int(stmt, 0);
        const char *name = (const char *)sqlite3_column_text(stmt, 1);
        const char *desc = (const char *)sqlite3_column_text(stmt, 2);
        const char *cmd  = (const char *)sqlite3_column_text(stmt, 3);
        int active = sqlite3_column_int(stmt, 4);

        char ename[256], edesc[512], ecmd[512];
        json_escape_string(name ? name : "", ename, sizeof(ename));
        json_escape_string(desc ? desc : "", edesc, sizeof(edesc));
        json_escape_string(cmd  ? cmd  : "", ecmd,  sizeof(ecmd));

        len += snprintf(buf + len, bufsize - len,
            "%s{\"gameid\":%d,\"gamename\":\"%s\",\"gamedescription\":\"%s\",\"launch_command\":\"%s\",\"is_active\":%d}",
            first ? "" : ",", gid, ename, edesc, ecmd, active);
        first = 0;
    }

    sqlite3_finalize(stmt);
    len += snprintf(buf + len, bufsize - len, "]");

    return buf;
}

// Admin: Neues Game hinzufügen
int add_game(const char *name, const char *desc, const char *cmd, char *out_json, size_t json_size) {
    if (!name || !desc || !cmd || strlen(name) < 2) {
        snprintf(out_json, json_size, "{\"error\":\"Invalid input\"}");
        return -1;
    }

    sqlite3_stmt *stmt;
    const char *sql = "INSERT INTO games (gamename, gamedescription, launch_command, is_active) VALUES (?, ?, ?, 1);";
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        snprintf(out_json, json_size, "{\"error\":\"DB error\"}");
        return -1;
    }

    sqlite3_bind_text(stmt, 1, name, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, desc, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, cmd, -1, SQLITE_STATIC);
    
    if (sqlite3_step(stmt) != SQLITE_DONE) {
        sqlite3_finalize(stmt);
        snprintf(out_json, json_size, "{\"error\":\"Game already exists or DB error\"}");
        return -1;
    }

    sqlite3_finalize(stmt);
    snprintf(out_json, json_size, "{\"success\":true}");
    write_log(-1, -1, "game_added", name);
    return 0;
}

// Admin: Game löschen
int delete_game(int gameid, char *out_json, size_t json_size) {
    sqlite3_stmt *stmt;
    const char *sql = "DELETE FROM games WHERE gameid = ?;";
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        snprintf(out_json, json_size, "{\"error\":\"DB error\"}");
        return -1;
    }

    sqlite3_bind_int(stmt, 1, gameid);
    
    if (sqlite3_step(stmt) != SQLITE_DONE) {
        sqlite3_finalize(stmt);
        snprintf(out_json, json_size, "{\"error\":\"Delete failed\"}");
        return -1;
    }

    sqlite3_finalize(stmt);
    snprintf(out_json, json_size, "{\"success\":true}");
    return 0;
}

// Admin: Game aktivieren/deaktivieren
int toggle_game(int gameid, char *out_json, size_t json_size) {
    sqlite3_stmt *stmt;
    const char *sql = "UPDATE games SET is_active = (1 - is_active) WHERE gameid = ?;";
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        snprintf(out_json, json_size, "{\"error\":\"DB error\"}");
        return -1;
    }

    sqlite3_bind_int(stmt, 1, gameid);
    
    if (sqlite3_step(stmt) != SQLITE_DONE) {
        sqlite3_finalize(stmt);
        snprintf(out_json, json_size, "{\"error\":\"Toggle failed\"}");
        return -1;
    }

    sqlite3_finalize(stmt);
    snprintf(out_json, json_size, "{\"success\":true}");
    return 0;
}

// Admin: Authentifizierung (Admin-Passwort prüfen)
char *authenticate_admin(int userid, const char *password) {
    if (!password || strcmp(password, "admin") != 0) {
        return NULL;
    }

    // Prüfe ob User Admin ist
    sqlite3_stmt *stmt;
    const char *sql = "SELECT is_admin FROM users WHERE userid = ? LIMIT 1;";
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        return NULL;
    }

    sqlite3_bind_int(stmt, 1, userid);
    
    if (sqlite3_step(stmt) != SQLITE_ROW || sqlite3_column_int(stmt, 0) != 1) {
        sqlite3_finalize(stmt);
        return NULL;
    }

    sqlite3_finalize(stmt);

    // Generiere Admin-Token (speichern wir lokal im Browser, nicht in DB)
    unsigned char raw[32];
    char *hex = malloc(65);
    
    randombytes_buf(raw, 32);
    sodium_bin2hex(hex, 65, raw, 32);
    
    return hex;
}

// Session Management: Start a new WebRTC session (just allocate, no SDP yet)
int start_session(int userid, int gameid, char *out_sessionid, char *out_sdp, char *out_json, size_t json_size) {
    if (userid < 0 || userid >= MAX_USERS) {
        snprintf(out_json, json_size, "{\"error\":\"Invalid user\"}");
        return -1;
    }
    
    // Initialize session manager
    sessionmanager *sess = &sessions[userid];
    sess->userid = userid;
    sess->gameid = gameid;
    sess->candidate_count = 0;
    sess->offer_ready = false;
    
    // Allocate Docker container ID storage (must be at least 65 chars)
    if (sess->dockerid == NULL) {
        sess->dockerid = malloc(sizeof(char*));
        *sess->dockerid = malloc(65);
        memset(*sess->dockerid, 0, 65);
    }
    
    // Allocate X11 display number (100 + userid to avoid conflicts)
    sess->vdisplay = 100 + userid;
    
    // Start Xvfb for this session
    char xvfb_cmd[256];
    snprintf(xvfb_cmd, sizeof(xvfb_cmd),
        "Xvfb :%d -screen 0 1280x720x24 -ac > /dev/null 2>&1 &",
        sess->vdisplay);
    
    int xvfb_ret = system(xvfb_cmd);
    if (xvfb_ret != 0) {
        fprintf(stderr, "[ W ] Xvfb startup warning (may already be running)\n");
    }
    
    // Give Xvfb a moment to start
    sleep(1);
    
    // Create WebRTC peer connection (will generate offer via callback)
    if (handle_user(sess) != 1) {
        snprintf(out_json, json_size, "{\"error\":\"Failed to create WebRTC connection\"}");
        return -1;
    }
    
    // Wait for the server-generated offer SDP to be ready (callback will populate it)
    int wait_count = 0;
    while (strlen(sess->sdp.sdp) == 0 && wait_count < 50) {
        usleep(100000);  // Wait 100ms, try 50 times = 5 seconds max
        wait_count++;
    }
    
    if (strlen(sess->sdp.sdp) == 0) {
        snprintf(out_json, json_size, "{\"error\":\"Failed to generate offer SDP\"}");
        return -1;
    }
    
    // Escape SDP for JSON output
    char escaped_sdp[16384];
    json_escape_string(sess->sdp.sdp, escaped_sdp, sizeof(escaped_sdp));
    
    // Return session ID and the server's offer SDP for browser to answer
    snprintf(out_sessionid, 32, "%d", userid);
    snprintf(out_json, json_size, 
        "{\"sessionid\":%d,\"type\":\"%s\",\"sdp\":\"%s\"}", 
        userid, sess->sdp.type, escaped_sdp);
    
    write_log(userid, gameid, "session_start", "");
    return 0;
}

// Session Management: Accept the browser's offer and send server answer
int answer_session(int userid, int sessionid, const char *sdp, char *out_json, size_t json_size) {
    if (userid < 0 || userid >= MAX_USERS || userid != sessionid) {
        snprintf(out_json, json_size, "{\"error\":\"Invalid session\"}");
        return -1;
    }
    
    if (!sdp || strlen(sdp) == 0) {
        snprintf(out_json, json_size, "{\"error\":\"No SDP provided\"}");
        return -1;
    }
    
    sessionmanager *sess = &sessions[userid];
    
    // Set remote description (the browser's answer SDP)
    rtcSetRemoteDescription(sess->vpcid, sdp, "answer");
    
    // The peer connection should now be established
    // Return success and the session is now ready for streaming
    snprintf(out_json, json_size, 
        "{\"sessionid\":%d,\"status\":\"answer_received\"}", 
        userid);
    
    return 0;
}

// Session Management: Add ICE candidate
int add_ice_candidate(int userid, int sessionid, const char *candidate, const char *mid, char *out_json, size_t json_size) {
    if (userid < 0 || userid >= MAX_USERS || userid != sessionid) {
        snprintf(out_json, json_size, "{\"error\":\"Invalid session\"}");
        return -1;
    }
    
    if (!candidate || strlen(candidate) == 0) {
        snprintf(out_json, json_size, "{\"error\":\"No candidate provided\"}");
        return -1;
    }
    
    // Add ICE candidate
    sessionmanager *sess = &sessions[userid];
    rtcAddRemoteCandidate(sess->vpcid, candidate, mid ? mid : "0");
    
    snprintf(out_json, json_size, "{\"success\":true}");
    return 0;
}
