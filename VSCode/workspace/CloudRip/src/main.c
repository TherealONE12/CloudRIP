#include <stdio.h>
#include <rtc/rtc.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <stdbool.h>
#include <microhttpd.h>
#include <stdlib.h>
#include <pthread.h>
#include "../include/docker.h"
#include "../include/multimanager.h"
#include "../include/api_handlers.h"
#include "../include/api_routes.h"
#include <sqlite3.h>
#include <sodium.h>

#define TOKEN_BYTES 32
#define TOKEN_HEX_LEN (TOKEN_BYTES * 2 +1)
#define HMAC_BYTES crypto_auth_hmacsha256_BYTES
#define SECRET_KEY_LEN crypto_auth_hmacsha256_keybytes
#define HASH_STR_LEN crypto_pwhash_STRBYTES


//custum stuff
enum MHD_Result onRequest(void *cls, struct MHD_Connection *connection,
              const char *url, const char *method,
              const char *version, const char *upload_data,
              size_t *upload_data_size, void **con_cls);
void escapeJsonfromBrowser(const char *src, char *dst, size_t size);
void escapeJsontoBrowser(const char *src, char *dst, size_t size);
char *fix_candidate(const char *candidate);
void init_db(sqlite3 *db);
char *db_get_games_json(sqlite3 *db);
enum MHD_Result serve_static_file(struct MHD_Connection *connection, const char *url);


int generatesessiontoken(int userid);
int verifypassword(const char *password, const char *pwhash);
int hashpassword(const char *password, char *pwhashout);







int main(){
    if(DEBUG){
        printf("IVFFrameHeader size: %zu (should be 12 )\n", sizeof(IVFFrameHeader));
        printf("IVFGlobalHeader size: %zu (should be 32 )\n", sizeof(IVFGlobalHeader));
    }
    

    //ret = system("chromium --disable-features=WebRtcHideLocalIpsWithMdns http://localhost:8000 ") / 256;

    printf("[ I ] Booting Cloudrip...\n");
    rtcInitLogger(RTC_LOG_DEBUG, NULL);
    
    //Initialisation to get the IP from the server
    printf("[ I ] Initialising STUN Server\n");
    const char *stunServer = "stun:stun.l.google.com:19302";
    g_config.iceServers = &stunServer;
    g_config.iceServersCount = 1;
    if(DEBUG) printf("[ D ] Stun Server Initialised!\n");

    printf("[ I ] Initialising Semi-Global Variables\n");
    sg_track.direction = RTC_DIRECTION_SENDONLY;
    sg_track.codec = RTC_CODEC_VP8;
    sg_track.payloadType = 96;
    sg_track.profile = NULL;
    for (int i = 0; i < MAX_USERS; i++){
        
    }
    
    printf("[ I ] Initialising Der Database\n");
    int rc = sqlite3_open("/home/jakob/VSCode/workspace/CloudRip/data/database.db", &db);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "DB open error: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        return 1;
    }
    init_db(db);
    
    // Initialize vpcid_to_userid mapping (all entries start as -1, meaning invalid)
    extern int vpcid_to_userid[1024];
    for (int i = 0; i < 1024; i++) {
        vpcid_to_userid[i] = -1;
    }

    printf("[ I ] Loading Sodium\n");
    if(sodium_init() < 0){
        fprintf(stderr, "[ E ] Sodium INIT Failed!");
        return -1;
    }


    //Homepage loading
    printf("[ I ] Loading Homepage\n");
    FILE *f = fopen("html/index.html", "r");
    if(f == NULL){
        printf("[ E ] index.html nicht gefunden!\n");
        return 1;
    }
    fread(g_html, 1, sizeof(g_html), f);
    fclose(f);

    printf("[ I ] Starting Webserver\n");
    struct MHD_Daemon *server = MHD_start_daemon(
        MHD_USE_THREAD_PER_CONNECTION,
        8000, NULL, NULL,
        &onRequest, NULL,
        MHD_OPTION_END
    );
    if(server == NULL){
        fprintf(stderr, "[ E ] MHD_start_daemon returned NULL\n");
        fprintf(stderr, "[ E ] Check if port 8000 is already in use\n");
        printf("[ E ] Error: Server Failed to start!");
        return -1;
    }



    printf("[ I ] Everything Loaded!\n");
    printf("[ I ] Starting Infinity Loop!\n");
    printf("[ I ] Server running in background mode (no stdin monitoring)\n");
    
    // Server runs indefinitely - no stdin blocking
    // (In production, use systemd or supervisor for process management)
    while(1) {
        sleep(3600);  // Sleep 1 hour at a time to keep process alive
    }
    printf("[ I ] Shutting down...\n");
    for(int i = 0; i < MAX_USERS; i++){
        rtcDeletePeerConnection(sessions[i].userid);
        docker_stop(sessions[i].useridchar);
    }
    MHD_stop_daemon(server);
    sqlite3_close(db);
    printf("[ I ] Goodbye!\n");
    return 0;
}



enum MHD_Result onRequest(void *cls, struct MHD_Connection *connection,
    const char *url, const char *method,
    const char *version, const char *upload_data,
    size_t *upload_data_size, void **con_cls) {
    if(cls){}
    if(version){}

    // Leite neue API-Requests weiter
    if (strstr(url, "/api/") != NULL) {
        // Sammle POST-Body wenn nötig
        char *body = NULL;
        if (strcmp(method, "POST") == 0 || strcmp(method, "PUT") == 0) {
            if (*con_cls == NULL) {
                char *buff = calloc(1, 16384);
                *con_cls = (void*)buff;
                return MHD_YES;
            } else if (*upload_data_size > 0) {
                strncat(*con_cls, upload_data, *upload_data_size);
                *upload_data_size = 0;
                return MHD_YES;
            }
            body = (char*)*con_cls;
        }

        enum MHD_Result result = route_api_request(connection, url, method, body ? body : "", body ? strlen(body) : 0);
        
        if (body) {
            free(body);
            *con_cls = NULL;
        }
        
        return result;
    }

    // Handle favicon without auth requirement
    if (strcmp(url, "/favicon.ico") == 0) {
        struct MHD_Response *response = MHD_create_response_from_buffer_copy(0, "");
        MHD_queue_response(connection, MHD_HTTP_NOT_FOUND, response);
        MHD_destroy_response(response);
        return MHD_YES;
    }

    // Statische Dateien servieren
    if (strcmp(method, "GET") == 0) {
        enum MHD_Result res = serve_static_file(connection, url);
        if (res == MHD_YES) {
            return MHD_YES;
        }
    }

    const char *tok = MHD_lookup_connection_value(connection, MHD_COOKIE_KIND, "session_token");
    int uid = tok ? atoi(tok) : -1;
    
    if (uid < 0 || uid >= MAX_USERS) {
        // No valid session, return 401 Unauthorized
        const char *err = "{\"error\":\"Unauthorized\"}";
        struct MHD_Response *response = MHD_create_response_from_buffer_copy(strlen(err), err);
        MHD_add_response_header(response, "Content-Type", "application/json");
        MHD_queue_response(connection, MHD_HTTP_UNAUTHORIZED, response);
        MHD_destroy_response(response);
        return MHD_YES;
    }
    if(strcmp(url, "/sdp") == 0 && strcmp(method, "GET") == 0){
        char json[16000];
        char escaped_sdp[9000];

        escapeJsontoBrowser(sessions[uid].sdp.sdp, escaped_sdp, sizeof(escaped_sdp));

        snprintf(json, sizeof(json), "{\"sdp\":\"%s\",\"type\":\"%s\"}", escaped_sdp, sessions[uid].sdp.type);

        struct MHD_Response *response = MHD_create_response_from_buffer_copy(strlen(json), json);

        MHD_queue_response(connection, MHD_HTTP_OK, response);
        MHD_destroy_response(response);

        return MHD_YES;
    }else if(strcmp(url, "/") == 0){
        handle_user(&sessions[uid]);

        struct MHD_Response *response = MHD_create_response_from_buffer_copy(strlen(g_html), g_html);
        MHD_add_response_header(response, "Content-Type", "text/html");
        MHD_queue_response(connection, MHD_HTTP_OK, response);
        MHD_destroy_response(response);
        return MHD_YES;
    }else if(strcmp(url, "/answer") == 0 && strcmp(method, "POST") == 0){
        if(*con_cls == NULL){
            char *buff = calloc(1,8192);
            *con_cls = (void*)buff;

            return MHD_YES;
        } else if(*upload_data_size > 0){
            strncat(*con_cls, upload_data, *upload_data_size);
            *upload_data_size = 0;

            return MHD_YES;
        }

        char *body = (char*)*con_cls;
        if(DEBUG) printf("[ D ] Raw body: %s\n", (char*)*con_cls);
        free(*con_cls);
        *con_cls = NULL;
                    
        char sdp_clean[8192];
        escapeJsonfromBrowser(body, sdp_clean, sizeof(sdp_clean));

        char *sdp_start = strstr(sdp_clean, "\"sdp\":\"");
        if(sdp_start){
            sdp_start += 7; // über "sdp":" drüberspringen
            char *sdp_end = strstr(sdp_start, "\",\"type\"");
            if(sdp_end) *sdp_end = '\0'; // String dort beenden
            rtcSetRemoteDescription(sessions[uid].userid, sdp_start, "answer");
        }
        if(DEBUG) printf("[ D ] Answer SDP: %s\n", sdp_start);

        struct MHD_Response *response = MHD_create_response_from_buffer_copy(2, "OK");
        MHD_queue_response(connection, MHD_HTTP_OK, response);
        MHD_destroy_response(response);
        return MHD_YES;
    }else if(strcmp(url, "/candidate") == 0 && strcmp(method, "POST") == 0){
        if(*con_cls == NULL){
            char *buff = calloc(1,4096);
            *con_cls = (void*)buff;

            return MHD_YES;
        } else if(*upload_data_size > 0){
            strncat(*con_cls, upload_data, *upload_data_size);
            *upload_data_size = 0;

            return MHD_YES;
        }

        char *body = (char*)*con_cls;
        if(DEBUG) printf("[ D ] Raw body: %s\n", (char*)*con_cls);
                    

        free(*con_cls);
        *con_cls = NULL;


        char *cand_start = strstr(body, "\"candidate\":\"");
        char *mid_start = strstr(body, "\"mid\":\"");

        if(cand_start && mid_start){
            cand_start += 13;
            char *cand_end = strstr(cand_start, "\"");
            if(cand_end) *cand_end = '\0';

            mid_start += 7;
            char *mid_end = strstr(mid_start, "\"");
            if(mid_end) *mid_end = '\0';

            if(DEBUG) printf("[ D ] Remote Candidate: %s\n", cand_start);
            if(strlen(cand_start) == 0) {
            } else {
                const char *clean = fix_candidate(cand_start);
                rtcAddRemoteCandidate(sessions[uid].userid, clean, mid_start);
            }
        }

        struct MHD_Response *response = MHD_create_response_from_buffer_copy(2, "OK");
        MHD_queue_response(connection, MHD_HTTP_OK, response);
        MHD_destroy_response(response);
        return MHD_YES;
    }  else if (strcmp(url, "/input") == 0 && strcmp(method, "POST") == 0) {
        if (*con_cls == NULL) {
            char *buff = calloc(1, 4096);
            *con_cls = (void*)buff;
            return MHD_YES;
        } else if (*upload_data_size > 0) {
            strncat(*con_cls, upload_data, *upload_data_size);
            *upload_data_size = 0;
            return MHD_YES;
        }

        char *body = (char*)*con_cls;
        docker_injekt_Keystroke(body, sessions[uid].vdisplay);
        free(*con_cls);
        *con_cls = NULL;

        struct MHD_Response *response = MHD_create_response_from_buffer_copy(2, "OK");
        MHD_queue_response(connection, MHD_HTTP_OK, response);
        MHD_destroy_response(response);
        return MHD_YES;
    }else if(strcmp(url, "/start") == 0 && strcmp(method, "GET") == 0){
        int ret = 0;
        char *tmpcmd = 0;
        snprintf(tmpcmd, 40, "Xvfb :%i -screen 0 1280x720x24 &", sessions[uid].vdisplay);
        ret = system(tmpcmd) / 256;


        struct MHD_Response *response;

        if(ret != 0){
            printf("[ E ] Starting XVFB Failed with coode %i. Did you install it?\n", ret);
            response = MHD_create_response_from_buffer_copy(strlen("{ERROR: XVFB Failed}"), "{ERROR: XVFB Failed}");
        }else{
            snprintf(tmpcmd, 120, "DISPLAY=:%i %s &", sessions[uid].vdisplay, db_get_launch_command(db, sessions[uid].gameid));
            ret = system(tmpcmd) / 256;

            if(ret != 0){
                printf("[ E ] Starting Game Failed with coode %i. U sure The Game exists?\n", ret);
                response = MHD_create_response_from_buffer_copy(strlen("{ERROR: Gamelaunch Failed}"), "{ERROR: Gamelaunch Failed}");
            }else{
                response = MHD_create_response_from_buffer_copy(strlen("{OK}"), "{OK}");
            }
        }
        MHD_queue_response(connection, MHD_HTTP_OK, response);
        MHD_destroy_response(response);

        return MHD_YES;
    }else if(strcmp(url, "/getgames") == 0 && strcmp(method, "GET") == 0){

        struct MHD_Response *response;

        char *json = db_get_games_json(db);
   
        response = MHD_create_response_from_buffer_copy(strlen(json), json);
       
        MHD_queue_response(connection, MHD_HTTP_OK, response);
        MHD_destroy_response(response);
        free(json);

        return MHD_YES;
    }else if(strcmp(url, "/register") == 0 && strcmp(method, "POST")){

        if (*con_cls == NULL) {
            char *buff = calloc(1, 4096);
            *con_cls = (void*)buff;
            return MHD_YES;
        } else if (*upload_data_size > 0) {
            strncat(*con_cls, upload_data, *upload_data_size);
            *upload_data_size = 0;
            return MHD_YES;
        }

        char *body = (char*)*con_cls;

        char username[50], password[100];

        char *start_uname = strstr(body, "username\":\"");
        char *end_uname = strstr(body, "\",\"password\":\"");

        if(!start_uname || !end_uname){
            struct MHD_Response *response;

            response = MHD_create_response_from_buffer_copy(strlen("{\"ERROR\":\"Malfunktioning body\"}"), "{\"ERROR\":\"Malfunktioning body\"}");
        
            MHD_queue_response(connection, MHD_HTTP_OK, response);
            MHD_destroy_response(response);

            return MHD_YES;
        }

        start_uname +=strlen("username\":\"");


        int maxthing = (int)(end_uname - start_uname);

        if(maxthing > 49 || maxthing < 0) maxthing = 49;

        strncpy(username, start_uname, maxthing);



        
        end_uname = strstr(body, "\",\"password\":\"");


        if(!end_uname){
            struct MHD_Response *response;

            response = MHD_create_response_from_buffer_copy(strlen("{\"ERROR\":\"Malfunktioning body\"}"), "{\"ERROR\":\"Malfunktioning body\"}");
        
            MHD_queue_response(connection, MHD_HTTP_OK, response);
            MHD_destroy_response(response);

            return MHD_YES;
        }

        end_uname +=strlen("\",\"password\":\"");


        maxthing = (int)(body - (end_uname + 2)); // for the end "}

        if(maxthing > 99 || maxthing < 0) maxthing = 99;

        strncpy(password, end_uname, maxthing);

        void *buf = malloc(128);
        char *pwhash = buf;

        hashpassword(password, pwhash);

        rwdb(db, 'w', -1, "users", "username", username, strlen(username));
        rwdb(db, 'w', -1, "users", "password_hash", pwhash, strlen(pwhash));

        struct MHD_Response *response;


   
        response = MHD_create_response_from_buffer_copy(strlen("{\"OK\"}"), "{\"OK\"}");
       
        MHD_queue_response(connection, MHD_HTTP_OK, response);
        MHD_destroy_response(response);


        return MHD_YES;
    }else if(strcmp(url, "/login") == 0 && strcmp(method, "POST")){

        if (*con_cls == NULL) {
            char *buff = calloc(1, 4096);
            *con_cls = (void*)buff;
            return MHD_YES;
        } else if (*upload_data_size > 0) {
            strncat(*con_cls, upload_data, *upload_data_size);
            *upload_data_size = 0;
            return MHD_YES;
        }

        char *body = (char*)*con_cls;

        char username[50], password[100];

        char *start_uname = strstr(body, "username\":\"");
        char *end_uname = strstr(body, "\",\"password\":\"");

        if(!start_uname || !end_uname){
            struct MHD_Response *response;

            response = MHD_create_response_from_buffer_copy(strlen("{\"ERROR\":\"Malfunktioning body\"}"), "{\"ERROR\":\"Malfunktioning body\"}");
        
            MHD_queue_response(connection, MHD_HTTP_OK, response);
            MHD_destroy_response(response);

            return MHD_YES;
        }

        start_uname +=strlen("username\":\"");


        int maxthing = (int)(end_uname - start_uname);

        if(maxthing > 49 || maxthing < 0) maxthing = 49;

        strncpy(username, start_uname, maxthing);



        
        end_uname = strstr(body, "\",\"password\":\"");


        if(!end_uname){
            struct MHD_Response *response;

            response = MHD_create_response_from_buffer_copy(strlen("{\"ERROR\":\"Malfunktioning body\"}"), "{\"ERROR\":\"Malfunktioning body\"}");
        
            MHD_queue_response(connection, MHD_HTTP_OK, response);
            MHD_destroy_response(response);

            return MHD_YES;
        }

        end_uname +=strlen("\",\"password\":\"");


        maxthing = (int)(body - (end_uname + 2)); // for the end "}

        if(maxthing > 99 || maxthing < 0) maxthing = 99;

        strncpy(password, end_uname, maxthing);

        char pwhash[128];

        rwdb(db, 'r', -1, "users", "password_hash", pwhash, 128);
        

        if(verifypassword(password, pwhash)){
            generatesessiontoken(get_userid_from_username(db, username));
        }


        struct MHD_Response *response;


   
        response = MHD_create_response_from_buffer_copy(strlen("{\"OK\"}"), "{\"OK\"}");
       
        MHD_queue_response(connection, MHD_HTTP_OK, response);
        MHD_destroy_response(response);


        return MHD_YES;
    }


    return MHD_NO;
}

void escapeJsonfromBrowser(const char *src, char *dst, size_t size){
    size_t j = 0;
    for(size_t i = 0; src[i] && j < size-1; i++){
        if(src[i] == '\\' && src[i+1] == 'n'){
            dst[j++] = '\n';
            i++;              
        } else if(src[i] == '\\' && src[i+1] == 'r'){
            dst[j++] = '\r';
            i++;
        } else {
            dst[j++] = src[i];
        }
    }
    dst[j] = '\0';
}

void escapeJsontoBrowser(const char *src, char *dst, size_t size){
    size_t j = 0;
    for(size_t i = 0; src[i] && j < size-2; i++){
        if(src[i] == '\n'){        
            dst[j++] = '\\';       
            dst[j++] = 'n';        
        }else if(src[i] == '\r'){  
            dst[j++] = '\\';
            dst[j++] = 'r';
        } else {
            dst[j++] = src[i];
        }
    }
    dst[j] = '\0';
}




char *fix_candidate(const char *candidate) {
    static char fixed[512];
    strncpy(fixed, candidate, sizeof(fixed) - 1);
    
    
    char *gen = strstr(fixed, " generation ");
    if (gen) *gen = '\0';
    
    return fixed;
}



void init_db(sqlite3 *db) {
    const char *sql =
        // --- USERS ---
        "CREATE TABLE IF NOT EXISTS users ("
        "   userid       INTEGER PRIMARY KEY AUTOINCREMENT,"
        "   username     TEXT    NOT NULL UNIQUE,"
        "   password_hash TEXT   NOT NULL,"
        "   is_admin     INTEGER DEFAULT 0,"
        "   created_at   INTEGER DEFAULT (strftime('%s','now')),"
        "   is_active    INTEGER DEFAULT 1"
        ");"

        "CREATE TABLE IF NOT EXISTS games ("
        "   gameid       INTEGER PRIMARY KEY AUTOINCREMENT,"
        "   gamename     TEXT    NOT NULL UNIQUE,"
        "   gamedescription     TEXT    NOT NULL,"
        "   launch_command TEXT   NOT NULL,"
        "   created_at   INTEGER DEFAULT (strftime('%s','now')),"
        "   is_active    INTEGER DEFAULT 0"
        ");"

        // --- TOKENS (für Login-Sessions) ---
        "CREATE TABLE IF NOT EXISTS tokens ("
        "   tokenid      INTEGER PRIMARY KEY AUTOINCREMENT,"
        "   userid       INTEGER NOT NULL,"
        "   token        TEXT    NOT NULL UNIQUE,"
        "   expires_at   INTEGER NOT NULL,"
        "   created_at   INTEGER DEFAULT (strftime('%s','now')),"
        "   FOREIGN KEY (userid) REFERENCES users(userid) ON DELETE CASCADE"
        ");"

        // --- GAME LOGS ---
        "CREATE TABLE IF NOT EXISTS game_logs ("
        "   logid        INTEGER PRIMARY KEY AUTOINCREMENT,"
        "   userid       INTEGER NOT NULL,"
        "   gameid       INTEGER NOT NULL,"
        "   action       TEXT    NOT NULL,"
        "   details      TEXT,"
        "   timestamp    INTEGER DEFAULT (strftime('%s','now')),"
        "   FOREIGN KEY (userid) REFERENCES users(userid) ON DELETE CASCADE,"
        "   FOREIGN KEY (gameid) REFERENCES games(gameid) ON DELETE CASCADE"
        ");";

    char *err = NULL;
    if (sqlite3_exec(db, sql, 0, 0, &err) != SQLITE_OK) {
        fprintf(stderr, "DB init error: %s\n", err);
        sqlite3_free(err);
    }

    // Migration: Add is_admin column if it doesn't exist (for old databases)
    const char *migration_check = "PRAGMA table_info(users);";
    sqlite3_stmt *stmt;
    int has_is_admin = 0;
    
    if (sqlite3_prepare_v2(db, migration_check, -1, &stmt, NULL) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const char *col_name = (const char *)sqlite3_column_text(stmt, 1);
            if (col_name && strcmp(col_name, "is_admin") == 0) {
                has_is_admin = 1;
                break;
            }
        }
        sqlite3_finalize(stmt);
    }
    
    if (!has_is_admin) {
        const char *add_column = "ALTER TABLE users ADD COLUMN is_admin INTEGER DEFAULT 0;";
        if (sqlite3_exec(db, add_column, 0, 0, &err) != SQLITE_OK) {
            fprintf(stderr, "Migration error: %s\n", err);
            sqlite3_free(err);
        }
    }

    // Erstelle Default-Benutzer wenn nicht vorhanden
    
    // Prüfe auf Admin
    const char *check_admin = "SELECT userid FROM users WHERE username = 'admin' LIMIT 1;";
    if (sqlite3_prepare_v2(db, check_admin, -1, &stmt, NULL) == SQLITE_OK) {
        if (sqlite3_step(stmt) != SQLITE_ROW) {
            sqlite3_finalize(stmt);
            char admin_hash[256];
            if (hashpassword("admin", admin_hash) == 0) {
                char insert_admin[512];
                snprintf(insert_admin, sizeof(insert_admin),
                    "INSERT OR IGNORE INTO users (username, password_hash, is_admin) VALUES ('admin', '%s', 1);",
                    admin_hash);
                if (sqlite3_exec(db, insert_admin, 0, 0, &err) != SQLITE_OK) {
                    fprintf(stderr, "Admin creation error: %s\n", err);
                    sqlite3_free(err);
                }
            }
        } else {
            sqlite3_finalize(stmt);
        }
    }
    
    // Prüfe auf Test-Benutzer
    const char *check_test = "SELECT userid FROM users WHERE username = 'test' LIMIT 1;";
    if (sqlite3_prepare_v2(db, check_test, -1, &stmt, NULL) == SQLITE_OK) {
        if (sqlite3_step(stmt) != SQLITE_ROW) {
            sqlite3_finalize(stmt);
            char test_hash[256];
            if (hashpassword("test", test_hash) == 0) {
                char insert_test[512];
                snprintf(insert_test, sizeof(insert_test),
                    "INSERT OR IGNORE INTO users (username, password_hash, is_admin) VALUES ('test', '%s', 0);",
                    test_hash);
                if (sqlite3_exec(db, insert_test, 0, 0, &err) != SQLITE_OK) {
                    fprintf(stderr, "Test user creation error: %s\n", err);
                    sqlite3_free(err);
                }
            }
        } else {
            sqlite3_finalize(stmt);
        }
    }
}

char *db_get_games_json(sqlite3 *db){
    const char *sql = "SELECT gameid, gamename, gamedescription FROM games WHERE is_active = 1;";
    sqlite3_stmt *stmt;

    if(sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK){
        fprintf(stderr, "[ E ] Prepare of getting games FAILED: %s\n", sqlite3_errmsg(db));
        return NULL;
    }

    //make a buffer for the jsons stuff

    size_t buf_size = 256;
    char *buf = malloc(buf_size);
    size_t pos = 0;

    //realloc appending stuff

    #define APPEND(fmt, ...) \
        do { \
            int needed = snprintf(NULL, 0, fmt, ##__VA_ARGS__) +1; \
            while (pos + needed >= buf_size){ \
                buf_size *=2; \
                buf = realloc(buf, buf_size); \
            } \
            pos += snprintf(buf + pos, buf_size - pos, fmt, ##__VA_ARGS__); \
        } while(0)
    
    APPEND("[");
    int first = 1;

    while (sqlite3_step(stmt) == SQLITE_ROW){
        int gameid = sqlite3_column_int(stmt, 0);
        const char *gamename = (const char *)sqlite3_column_text(stmt, 1);
        const char *gamedesc = (const char *)sqlite3_column_text(stmt, 2);

        //NULL Handler
        if (!gamename) gamename = "";
        if (!gamedesc) gamedesc = "";

        if(!first) APPEND(",");
        first = 0;

        char escaped[512] = {0};
        char escaped2[512] = {0};

        size_t ei = 0;
        for(size_t i = 0; gamename[i] && ei < sizeof(escaped) - 2; i++){
            if(gamename[i] == '"' || gamename[i] == '\\'){
                escaped[ei++] = '\\';
            }
            escaped[ei++] = gamename[i];
        }

        ei = 0;
        for(size_t i = 0; gamedesc[i] && ei < sizeof(escaped2) - 2; i++){
            if(gamedesc[i] == '"' || gamedesc[i] == '\\'){
                escaped2[ei++] = '\\';
            }
            escaped2[ei++] = gamedesc[i];
        }

        APPEND("{\"gameid\":%d,\"gamename\":\"%s\",\"gamedesc\":\"%s\"}", gameid, escaped, escaped2);
    }

    APPEND("]");
    #undef APPEND

    sqlite3_finalize(stmt);
    return buf;

}

int hashpassword(const char *password, char *pwhashout){
    if(crypto_pwhash_str(pwhashout, password, strlen(password), crypto_pwhash_OPSLIMIT_INTERACTIVE, crypto_pwhash_MEMLIMIT_INTERACTIVE) != 0){
        fprintf(stderr, "[ E ] Not enought ram for hashing!");
        return -1;
    }

    return 0;
}

int verifypassword(const char *password, const char *pwhash){
    int result = crypto_pwhash_str_verify(pwhash, password, strlen(password));

    return result == 0 ? 1 : 0;
}

typedef struct{
    unsigned char raw[TOKEN_BYTES];
    char hex[TOKEN_HEX_LEN];
    uint64_t expires_at;
    int userid;
}SessionToken;


int generatesessiontoken(int userid){
    unsigned char tmpbuf_raw[TOKEN_BYTES];
    void *buf = malloc(TOKEN_HEX_LEN);
    char *hex = buf;
    
    if (!hex) return -1;

    // Token generieren
    randombytes_buf(tmpbuf_raw, TOKEN_BYTES);
    sodium_bin2hex(hex, TOKEN_HEX_LEN, tmpbuf_raw, TOKEN_BYTES);

    uint64_t expires_at = (uint64_t)time(NULL) + 3600;

    // In DB speichern
    if (rwdb(db, 'w', userid, "tokens", "token", hex, sizeof(hex)) != 0) {
        free(hex);
        return -1;
    }
    char buf2[50];
    sprintf(buf2, "%lui", expires_at);

    if (rwdb(db, 'w', userid, "tokens", "expires_at", buf2, sizeof(expires_at)) != 0) {
        free(hex);
        return -1;
    }

    return 1;
}


// Statische Datei servieren
enum MHD_Result serve_static_file(struct MHD_Connection *connection, const char *url) {
    const char *filepath = NULL;
    
    if (strcmp(url, "/") == 0 || strcmp(url, "/index.html") == 0) {
        filepath = "html/index.html";
    } else if (strcmp(url, "/admin") == 0 || strcmp(url, "/admin.html") == 0) {
        filepath = "html/admin.html";
    } else if (strcmp(url, "/game") == 0 || strcmp(url, "/game.html") == 0) {
        filepath = "html/game.html";
    } else {
        return MHD_NO;
    }
    
    FILE *f = fopen(filepath, "r");
    if (!f) {
        const char *err = "<html><body>404 Not Found</body></html>";
        struct MHD_Response *response = MHD_create_response_from_buffer_copy(strlen(err), err);
        MHD_queue_response(connection, MHD_HTTP_NOT_FOUND, response);
        MHD_destroy_response(response);
        return MHD_YES;
    }
    
    char buffer[65536];
    size_t bytes = fread(buffer, 1, sizeof(buffer) - 1, f);
    buffer[bytes] = '\0';
    fclose(f);
    
    struct MHD_Response *response = MHD_create_response_from_buffer_copy(bytes, buffer);
    MHD_add_response_header(response, "Content-Type", "text/html; charset=utf-8");
    MHD_queue_response(connection, MHD_HTTP_OK, response);
    MHD_destroy_response(response);
    
    return MHD_YES;
}
