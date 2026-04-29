#ifndef MULTIMANAGER_H
#define MULTIMANAGER_H


#include <rtc/rtc.h>
#include <pthread.h>
#include <sqlite3.h>


#define MAX_USERS 256
#define MAX_CANDIDATES 16
#define MAX_GAMES 256
#define DEBUG false
#define SESSION_TIMEOUT_SEC 3600


typedef struct gs_sdpconfig{
    int server_id;
    char sdp[8192];
    char type[16];
} SdpConfig;

// IVF Header Structs
typedef struct {
    char signature[4];   
    uint16_t version;
    uint16_t header_size;
    char codec[4];       
    uint16_t width;
    uint16_t height;
    uint32_t fps_num;
    uint32_t fps_den;
    uint32_t frame_count;
    uint32_t unused;
} IVFGlobalHeader;      

typedef struct __attribute__((packed)) {
    uint32_t frame_size;
    uint64_t timestamp;
} IVFFrameHeader;    

typedef struct {
    int sessionid;
    int userid;
    char useridchar[sizeof(int)];
    int gameid;
    int vdisplay;
    int vpcid;
    int webrtc_track;

    char name_buf[64];
    char msid_buf[64];
    char trackid_buf[64];

    char offer_sdp[8192];
    char offer_type[16];
    bool offer_ready;

    char **dockerid;
    pthread_t stream_thread;

    int candidate_count;
    char candidates[MAX_CANDIDATES][512];
    char candidate_mids[MAX_CANDIDATES][32];
    void **con_cls;

    SdpConfig sdp;
    char html[65536];

    int needshandeling;
}sessionmanager;

typedef struct {
    int gameid;
    char gamename[100];
    char launchcmd[200];
}gamestruct;


extern char g_html[65536]; //MAYOR SECURITY RISK! MUST!! BE REFACTORED @ PRODUKTION TO BE DYNAMIC!!!!
extern rtcTrackInit sg_track;
extern sessionmanager sessions[MAX_USERS];
extern pthread_mutex_t sessions_mutex;  // ✅ Thread safety for sessions[]
extern rtcConfiguration g_config;
extern gamestruct g_gameslist[MAX_GAMES];
extern sqlite3 *db;


void onLocalDescription(int uid, const char *sdp, const char *type, void *user);
void onLocalCandidate(int uid, const char *cand, const char *mid, void *user);
void onStateChange(int uid, rtcState state, void *user);
void *stream_loop(void *arg);
void send_vp8_frame(int track, const char *frame, uint32_t size, uint32_t ssrc);
const char *db_get_launch_command(sqlite3 *db, int gameid);
int rwdb(sqlite3 *db, char rw, int userid,
         const char *tablename, const char *column,
         char *rwbuf, int sizeofrwbuf);
int get_userid_from_username(sqlite3 *db, const char *username);
 
int handle_user(sessionmanager *session);


#endif 