#include "../include/multimanager.h"
#include "../include/docker.h"

#include <stdio.h>
#include <rtc/rtc.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <stdbool.h>
#include <microhttpd.h>
#include <stdlib.h>
#include <pthread.h>
#include <stdint.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

int nrofcandidates = 0;
char candidates[MAX_CANDIDATES][512];
char candidate_mids[MAX_CANDIDATES][16];

char g_html[65536] = {0}; //MAYOR SECURITY RISK! MUST!! BE REFACTORED @ PRODUKTION TO BE DYNAMIC!!!!
rtcTrackInit sg_track = {0};
sessionmanager sessions[MAX_USERS] = {0};
pthread_mutex_t sessions_mutex = PTHREAD_MUTEX_INITIALIZER;  // ✅ Thread-safe access
rtcConfiguration g_config = {0};
sqlite3 *db;

// Map from vpcid (PC handle) back to userid for callbacks
int vpcid_to_userid[1024];  // Initialized in init_databases()


const char *db_get_launch_command(sqlite3 *db, int gameid);


int handle_user(sessionmanager *session){
    //create an connection
    snprintf(session->useridchar, 100, "%i", session->userid);
    session->vpcid = rtcCreatePeerConnection(&g_config);

    if (session->vpcid < 0) {
        printf("[ E ] Failed to create peer connection for user %d\n", session->userid);
        return 0;
    }

    // Store mapping from vpcid to userid for callbacks
    if (session->vpcid < 1024) {
        vpcid_to_userid[session->vpcid] = session->userid;
    }

    //InterruptHandlers - register with vpcid (peer connection ID)
    // Callbacks will be called with vpcid as first param
    rtcSetLocalDescriptionCallback(session->vpcid, onLocalDescription);
    rtcSetLocalCandidateCallback(session->vpcid, onLocalCandidate);
    rtcSetStateChangeCallback(session->vpcid, onStateChange);

    snprintf(session->name_buf, sizeof(session->name_buf), "video_%d", session->userid);
    snprintf(session->msid_buf, sizeof(session->msid_buf), "stream_%d", session->userid);
    snprintf(session->trackid_buf, sizeof(session->trackid_buf), "video_%d", session->userid);

    
    sg_track.ssrc = (uint32_t)session->userid;
    sg_track.name = session->name_buf;
    sg_track.msid = session->msid_buf;
    sg_track.trackId = session->trackid_buf;

    session->webrtc_track = rtcAddTrackEx(session->vpcid, &sg_track);

    if(session->webrtc_track == -1){
        printf("[ E ] Track init failed! UserID: %i VPCID: %d\nName Buffer: %s\nMSID Buffer: %s\nTrackID Buffer: %s\n", session->userid, session->vpcid, session->name_buf, session->msid_buf, session->trackid_buf);
        return 0;
    }

    rtcSetLocalDescription(session->vpcid, "offer");

    return 1;
}

    

void onLocalDescription(int uid , const char *sdp, const char *type, void *user){
    if(user){}
    // uid is the vpcid, map it back to userid
    int userid = (uid < 1024) ? vpcid_to_userid[uid] : -1;
    if(userid < 0 || userid >= MAX_USERS) return;
    
    pthread_mutex_lock(&sessions_mutex);
    
    // Validate SDP length
    if (sdp && strlen(sdp) < 8192) {
        strncpy(sessions[userid].sdp.sdp, sdp, 8192 - 1);
        sessions[userid].sdp.sdp[8191] = '\0';
    } else {
        printf("[ W ] SDP too large or null for user %d\n", userid);
        pthread_mutex_unlock(&sessions_mutex);
        return;
    }
    
    // Validate type length
    if (type && strlen(type) < 16) {
        strncpy(sessions[userid].sdp.type, type, 16 - 1);
        sessions[userid].sdp.type[15] = '\0';
    }

    sessions[userid].sdp.server_id = 1;

    if(DEBUG) {
        printf("[ D ] SDP received for user %d\n", userid);
    }
    
    pthread_mutex_unlock(&sessions_mutex);
}

void onLocalCandidate(int uid, const char *cand, const char *mid, void *user){
    if(user){}
    // uid is the vpcid, map it back to userid
    int userid = (uid < 1024) ? vpcid_to_userid[uid] : -1;
    if(userid < 0 || userid >= MAX_USERS) return;
    
    pthread_mutex_lock(&sessions_mutex);
    
    if(sessions[userid].candidate_count >= MAX_CANDIDATES) {
        printf("[ W ] Candidate buffer full for user %d (max %d)\n", userid, MAX_CANDIDATES);
        pthread_mutex_unlock(&sessions_mutex);
        return;
    }
    
    // Validate candidate length
    if (!cand || strlen(cand) >= 512) {
        printf("[ W ] Invalid candidate size for user %d\n", userid);
        pthread_mutex_unlock(&sessions_mutex);
        return;
    }
    
    strncpy(sessions[userid].candidates[sessions[userid].candidate_count], cand, 511);
    sessions[userid].candidates[sessions[userid].candidate_count][511] = '\0';
    
    if (mid) {
        strncpy(sessions[userid].candidate_mids[sessions[userid].candidate_count], mid, 31);
        sessions[userid].candidate_mids[sessions[userid].candidate_count][31] = '\0';
    }
    
    sessions[userid].candidate_count++;
    
    if(DEBUG) printf("[ D ] Candidate %d for user %d received\n", sessions[userid].candidate_count, userid);

    pthread_mutex_unlock(&sessions_mutex);
}

void onStateChange(int uid, rtcState state, void *user){
    if(user){}
    // uid is the vpcid, map it back to userid
    int userid = (uid < 1024) ? vpcid_to_userid[uid] : -1;
    if(userid < 0 || userid >= MAX_USERS) return;
    
    switch(state){
        case RTC_CONNECTING:
            if(DEBUG) printf("[ D ] Browser is connecting (user %d)...\n", userid);
            break;
        case RTC_CONNECTED: {
            pthread_mutex_lock(&sessions_mutex);
            
            sessions[userid].offer_ready = true;
            printf("[ I ] Browser connected! Streaming is starting... (user %d)\n", userid);

            // Validate display number (security: prevent injection)
            if (sessions[userid].vdisplay < 0 || sessions[userid].vdisplay > 999) {
                printf("[ E ] Invalid display number: %d (must be 0-999)\n", sessions[userid].vdisplay);
                sessions[userid].offer_ready = false;
                pthread_mutex_unlock(&sessions_mutex);
                break;
            }

            const char *launch_cmd = db_get_launch_command(db, sessions[userid].gameid);
            if (!launch_cmd || launch_cmd[0] == '\0') {
                printf("[ E ] No launch command for gameid %d — aborting stream\n", sessions[userid].gameid);
                sessions[userid].offer_ready = false;
                pthread_mutex_unlock(&sessions_mutex);
                break;
            }

            printf("[ I ] Game %d launch cmd: %s\n", sessions[userid].gameid, launch_cmd);

            // Always clean up any stale container before creating a new one
            docker_delete(sessions[userid].useridchar);

            docker_create("cloudrip-container", sessions[userid].useridchar, launch_cmd, sessions[userid].dockerid, sessions[userid].vdisplay);
            docker_start(sessions[userid].useridchar);
            
            pthread_mutex_unlock(&sessions_mutex);
            
            printf("[ I ] Waiting for container to initialize...\n");
            sleep(5);
            printf("[ I ] Starting ffmpeg streaming...\n");
            
            pthread_t stream_thread;
            if (pthread_create(&stream_thread, NULL, stream_loop, (void *)(intptr_t)userid) != 0) {
                printf("[ E ] Failed to create streaming thread\n");
                sessions[userid].offer_ready = false;
            } else {
                sessions[userid].stream_thread = stream_thread;
            }
            break;
        }
        case RTC_DISCONNECTED:
        case RTC_FAILED:
        case RTC_CLOSED: {
            pthread_mutex_lock(&sessions_mutex);
            
            sessions[userid].offer_ready = false;
            printf("[ I ] Connection ended (state %d, user %d) — cleaning up\n", state, userid);

            if (sessions[userid].dockerid) {
                docker_stop(sessions[userid].useridchar);
                docker_delete(sessions[userid].useridchar);
            }

            // Kill Xvfb safely (validate display number)
            if (sessions[userid].vdisplay >= 0 && sessions[userid].vdisplay <= 999) {
                char xvfb_kill[128];
                snprintf(xvfb_kill, sizeof(xvfb_kill), "pkill -f 'Xvfb :%d' 2>/dev/null || true", sessions[userid].vdisplay);
                system(xvfb_kill);
            }

            if (sessions[userid].vpcid >= 0) {
                rtcDeletePeerConnection(sessions[userid].vpcid);
                sessions[userid].vpcid = -1;
            }
            
            pthread_mutex_unlock(&sessions_mutex);
            break;
        }
        default:
            printf("[ E ] Default triggerd!");
            break;
    }
}

void *stream_loop(void *arg) {
    int uid = (int) (intptr_t) arg;
    char tmpcmd[256];
    static char frame_buffer[2000000];

    // Connect to ffmpeg streaming socket inside container via docker network
    // Container runs: ffmpeg | nc -l -p 9999
    // We get container IP from docker network and connect to port 9999
    char container_name[128];
    snprintf(container_name, sizeof(container_name), "%s_vm", sessions[uid].useridchar);
    
    // Get container IP address via docker inspect
    snprintf(tmpcmd, sizeof(tmpcmd),
        "docker inspect -f '{{range .NetworkSettings.Networks}}{{.IPAddress}}{{end}}' %s",
        container_name);
    
    FILE *get_ip = popen(tmpcmd, "r");
    char container_ip[32] = {0};
    if (get_ip) {
        fgets(container_ip, sizeof(container_ip), get_ip);
        pclose(get_ip);
        // Remove newline
        container_ip[strcspn(container_ip, "\n")] = 0;
    }
    
    if (!container_ip[0]) {
        printf("[ E ] Failed to get container IP for %s\n", container_name);
        return NULL;
    }
    
    printf("[ I ] Container %s IP: %s\n", container_name, container_ip);
    printf("[ I ] Connecting to ffmpeg stream at %s:9999\n", container_ip);
    
    // Connect to container ffmpeg socket
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        printf("[ E ] Socket creation failed\n");
        return NULL;
    }
    
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(9999);
    if (inet_pton(AF_INET, container_ip, &addr.sin_addr) <= 0) {
        printf("[ E ] Invalid container IP: %s\n", container_ip);
        close(sock);
        return NULL;
    }
    
    // Retry connection a few times (container might still be starting ffmpeg)
    int retries = 5;
    while (retries-- > 0 && connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        printf("[ W ] Connection attempt %d failed, retrying...\n", 6 - retries);
        sleep(1);
    }
    
    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        printf("[ E ] Failed to connect to ffmpeg socket after retries\n");
        close(sock);
        return NULL;
    }
    
    printf("[ I ] Connected to ffmpeg stream!\n");
    
    // Read IVF header
    IVFGlobalHeader gh;
    if (recv(sock, &gh, sizeof(gh), MSG_WAITALL) != sizeof(gh)) {
        printf("[ E ] IVF Header read failed!\n");
        close(sock);
        return NULL;
    }
    printf("[ I ] IVF header received, ffmpeg streaming active\n");
    printf("[ I ] Track handle: %d, SSRC: %d\n", sessions[uid].webrtc_track, uid);
    fflush(stdout);

    int frame_count = 0;
    int dropped_frames = 0;
    struct timespec t0, t1, frame_start;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    clock_gettime(CLOCK_MONOTONIC, &frame_start);
    
    const long frame_delay_us = 33333; // ~30 FPS: 1,000,000 / 30 = 33,333 microseconds

    while(sessions[uid].offer_ready){
        IVFFrameHeader fh;
        if (recv(sock, &fh, sizeof(fh), MSG_WAITALL) != sizeof(fh)) {
            printf("[ E ] Frame Header read failed! Connection closed?\n");
            break;
        }
        if (fh.frame_size == 0 || fh.frame_size > 2000000) {
            printf("[ E ] Bogus frame_size=%u, aborting\n", fh.frame_size);
            fflush(stdout);
            break;
        }
        if (recv(sock, frame_buffer, fh.frame_size, MSG_WAITALL) != (int)fh.frame_size) {
            printf("[ E ] Frame data read failed!\n");
            fflush(stdout);
            break;
        }
        
        send_vp8_frame(sessions[uid].webrtc_track, frame_buffer, fh.frame_size, (uint32_t)uid);
        frame_count++;

        // Framerate regulation: target 30 FPS with nanosecond precision
        clock_gettime(CLOCK_MONOTONIC, &t1);
        long elapsed_us = (t1.tv_sec - frame_start.tv_sec) * 1000000 + 
                          (t1.tv_nsec - frame_start.tv_nsec) / 1000;
        long sleep_us = frame_delay_us - elapsed_us;
        if (sleep_us > 0) {
            usleep(sleep_us);
        }
        clock_gettime(CLOCK_MONOTONIC, &frame_start);

        clock_gettime(CLOCK_MONOTONIC, &t1);
        double elapsed = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9;
        if (elapsed >= 2.0) {
            int buf = rtcGetBufferedAmount(sessions[uid].webrtc_track);
            printf("[ I ] Server FPS: %.1f (frame %d, size=%u bytes, buf=%d bytes, dropped=%d)\n",
                frame_count / elapsed, frame_count, fh.frame_size, buf, dropped_frames);
            fflush(stdout);
            frame_count = 0;
            dropped_frames = 0;
            clock_gettime(CLOCK_MONOTONIC, &t0);
        }
    }
    printf("[ I ] Stream Loop stopped for user %d\n", uid);
    close(sock);
    return NULL;
}

void send_vp8_frame(int track, const char *frame, uint32_t size, uint32_t ssrc){
    static uint16_t seq = 0;
    static uint32_t timestamp = 0;
    uint32_t offset = 0;

    int buffered = rtcGetBufferedAmount(track);
    // Increased buffer limit from 500KB to 5MB to accommodate more frames
    // At 30 FPS with ~80KB frames, this allows ~60 frames of buffering
    if (buffered < 0 || buffered > 5000000) {
        printf("[ W ] Buffer FULL (%d bytes), Frame DROPPED (timestamp may have gaps)\n", buffered);
        timestamp += 3000;
        return;
    }

    while(offset < size){
        uint32_t chunk = size - offset;
        if(chunk > 1100){
            chunk = 1100;
        }

        bool isfirst = (offset == 0);
        bool islast = ((offset + chunk) >= size);

        uint8_t packet[1200] = {0};

        packet[0] = 0x80;
        packet[1] = (islast ? 0x80 : 0x00) | 96;
        packet[2] = (seq >> 8) & 0xFF;
        packet[3] = seq & 0xFF;
        packet[4] = (timestamp >> 24) & 0xFF;
        packet[5] = (timestamp >> 16) & 0xFF;
        packet[6] = (timestamp >> 8) & 0xFF;
        packet[7] = timestamp & 0xFF;
        packet[8] = (ssrc >> 24) & 0xFF;
        packet[9] = (ssrc >> 16) & 0xFF;
        packet[10] = (ssrc >> 8) & 0xFF;
        packet[11] = ssrc & 0xFF;

        packet[12] = isfirst ? 0x10:0x00;

        memcpy(packet + 13, frame + offset, chunk);

        int ret = rtcSendMessage(track, (char*)packet, chunk + 13);
        if (ret < 0) {
            printf("[ W ] rtcSendMessage failed (ret=%d), Frame partially sent\n", ret);
            timestamp += 3000;
            return;
        }

        if(DEBUG) printf("[ D ] Sent Packet! (offset=%u, chunk=%u, seq=%u)\n", offset, chunk, seq);

        offset += chunk;
        seq++;

        if(islast && DEBUG) printf("[ D ] Finished sending frame packets!\n");
    }
    timestamp += 3000;
}

const char *db_get_launch_command(sqlite3 *db, int gameid) {
    const char *sql = "SELECT launch_command FROM games WHERE gameid = ?;";
    sqlite3_stmt *stmt;

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK)
        return NULL;

    sqlite3_bind_int(stmt, 1, gameid);  // ? wird durch gameid ersetzt

    const char *result = NULL;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const char *cmd = (const char *)sqlite3_column_text(stmt, 0);
        if (cmd)
            result = strdup(cmd);  // eigene Kopie, da stmt gleich weg ist
    }

    sqlite3_finalize(stmt);
    return result;  // caller macht free()!
}

int rwdb(sqlite3 *db, char rw, int userid,
         const char *tablename, const char *column,
         char *rwbuf, int sizeofrwbuf)
{
    // Tabellen/Spaltennamen kann man NICHT mit ? binden,
    // daher manuell in den SQL-String bauen.
    // Basisschutz: nur alphanumerisch + _ erlaubt
    for (int i = 0; tablename[i]; i++) {
        char c = tablename[i];
        if (!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_')) {
            fprintf(stderr, "[ E ] Ungültiger Tabellenname\n");
            return -1;
        }
    }
    for (int i = 0; column[i]; i++) {
        char c = column[i];
        if (!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_')) {
            fprintf(stderr, "[ E ] Ungültiger Spaltenname\n");
            return -1;
        }
    }

    char sql[256];
    sqlite3_stmt *stmt;

    if (rw == 'r') {
        // ------- LESEN -------
        snprintf(sql, sizeof(sql),
            "SELECT %s FROM %s WHERE userid = ?;", tablename, column);

        if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
            fprintf(stderr, "[ E ] Prepare failed: %s\n", sqlite3_errmsg(db));
            return -1;
        }
        sqlite3_bind_int(stmt, 1, userid);

        if (sqlite3_step(stmt) == SQLITE_ROW) {
            const char *val = (const char *)sqlite3_column_text(stmt, 0);
            if (val)
                strncpy(rwbuf, val, sizeofrwbuf - 1);
            else
                rwbuf[0] = '\0';
            rwbuf[sizeofrwbuf - 1] = '\0';  // null-terminator sicherstellen
        } else {
            fprintf(stderr, "[ E ] Kein Eintrag gefunden\n");
            sqlite3_finalize(stmt);
            return -1;
        }

    } else if (rw == 'w') {
        // ------- SCHREIBEN -------
        if (userid == -1) {
            // INSERT (neuer Eintrag)
            snprintf(sql, sizeof(sql),
                "INSERT INTO %s (%s) VALUES (?);", tablename, column);
            if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
                fprintf(stderr, "[ E ] Prepare failed: %s\n", sqlite3_errmsg(db));
                return -1;
            }
            sqlite3_bind_text(stmt, 1, rwbuf, -1, SQLITE_STATIC);
        } else {
            // UPDATE (bestehender Eintrag)
            snprintf(sql, sizeof(sql),
                "UPDATE %s SET %s = ? WHERE userid = ?;", tablename, column);
            if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
                fprintf(stderr, "[ E ] Prepare failed: %s\n", sqlite3_errmsg(db));
                return -1;
            }
            sqlite3_bind_text(stmt, 1, rwbuf, -1, SQLITE_STATIC);
            sqlite3_bind_int (stmt, 2, userid);
        }

        if (sqlite3_step(stmt) != SQLITE_DONE) {
            fprintf(stderr, "[ E ] Write failed: %s\n", sqlite3_errmsg(db));
            sqlite3_finalize(stmt);
            return -1;
        } 
    }else {
        fprintf(stderr, "[ E ] rw muss 'r' oder 'w' sein\n");
        return -1;
    }

    sqlite3_finalize(stmt);
    return 0;
}

int get_userid_from_username(sqlite3 *db, const char *username){
    const char *sql = "SELECT userid FROM users WHERE username = ?;";
    sqlite3_stmt *stmt;

    if(sqlite3_prepare16_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK){
        fprintf(stderr, "[ E ] SQL prepare failed: %s\n", sqlite3_errmsg(db));
        return -1;
    }

    sqlite3_bind_text(stmt, 1, username, -1, SQLITE_STATIC);

    int userid = -1;

    if(sqlite3_step(stmt) == SQLITE_ROW){
        userid = sqlite3_column_int(stmt, 0);
    }else 
        fprintf(stderr, "[ E ] User %s not found! DB Error: %s", username, sqlite3_errmsg(db));
    
    sqlite3_finalize(stmt);

    return userid;
}