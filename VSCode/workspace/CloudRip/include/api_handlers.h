#ifndef API_HANDLERS_H
#define API_HANDLERS_H

typedef struct {
    char username[100];
    char password[200];
    char token[256];
    int gameid;
    char command[256];
    char gamename[128];
    char gamedesc[256];
    int is_admin;
} ApiRequest;

// Password (from main.c)
int hashpassword(const char *password, char *pwhashout);
int verifypassword(const char *password, const char *pwhash);

// JSON Parser
int parse_auth_request(const char *body, ApiRequest *req);
int parse_game_request(const char *body, ApiRequest *req);

// Token & Auth
int validate_token(const char *token, int *out_userid, int *out_is_admin);
int get_userid_from_username_local(const char *username, int *out_userid);
int get_password_hash(const char *username, char *out_hash, size_t hash_size);
char *generate_new_token(int userid);

// Logging
int write_log(int userid, int gameid, const char *action, const char *details);
char *get_logs_json(int userid, int is_admin);

// User Management
int register_user(const char *username, const char *password, char *out_json, size_t json_size);

// Game Management
char *get_games_list(void);
char *get_all_games(void);
int add_game(const char *name, const char *desc, const char *cmd, char *out_json, size_t json_size);
int delete_game(int gameid, char *out_json, size_t json_size);
int toggle_game(int gameid, char *out_json, size_t json_size);

// Session Management (WebRTC)
int start_session(int userid, int gameid, char *out_sessionid, char *out_sdp, char *out_json, size_t json_size);
int answer_session(int userid, int sessionid, const char *sdp, char *out_json, size_t json_size);
int add_ice_candidate(int userid, int sessionid, const char *candidate, const char *mid, char *out_json, size_t json_size);

// Admin
char *authenticate_admin(int userid, const char *password);

#endif
