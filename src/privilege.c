/* 
 * Copyright (c) 2015-2016, Gregory M. Kurtzer. All rights reserved.
 * 
 * “Singularity” Copyright (c) 2016, The Regents of the University of California,
 * through Lawrence Berkeley National Laboratory (subject to receipt of any
 * required approvals from the U.S. Dept. of Energy).  All rights reserved.
 * 
 * This software is licensed under a customized 3-clause BSD license.  Please
 * consult LICENSE file distributed with the sources of this project regarding
 * your rights to use or distribute this software.
 * 
 * NOTICE.  This Software was developed under funding from the U.S. Department of
 * Energy and the U.S. Government consequently retains certain rights. As such,
 * the U.S. Government has been granted for itself and others acting on its
 * behalf a paid-up, nonexclusive, irrevocable, worldwide license in the Software
 * to reproduce, distribute copies to the public, prepare derivative works, and
 * perform publicly and display publicly, and to permit other to do so. 
 * 
 */


#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <pwd.h>
#include <errno.h> 
#include <string.h>
#include <stdio.h>
#include <grp.h>
#include <linux/limits.h>
#include <sched.h>

#include "privilege.h"
#include "config.h"
#include "file.h"
#include "util.h"
#include "message.h"
#include "singularity.h"



static struct PRIV_INFO {
    int ready;
    uid_t uid;
    gid_t gid;
    gid_t *gids;
    size_t gids_count;
    int userns_ready;
    int disable_setgroups;
    uid_t orig_uid;
    uid_t orig_gid;
    pid_t orig_pid;
    int target_mode;  // Set to 1 if we are running in "target mode" (admin specifies UID/GID)
} uinfo;


void priv_init(void) {
    memset(&uinfo, '\0', sizeof(uinfo));

    // If we are *not* the setuid binary and started as root, then
    //
    long int target_uid = -1;
    long int target_gid = -1;
#ifdef SINGULARITY_NOSUID
    char *target_uid_str = NULL;
    char *target_gid_str = NULL;
    if ( getuid() == 0 ) {
        target_uid_str = getenv("SINGULARITY_TARGET_UID");
        target_gid_str = getenv("SINGULARITY_TARGET_GID");
        if ( target_uid_str && !target_gid_str ) {
            message(ERROR, "A target UID is set (%s) but a target GID is not set (SINGULARITY_TARGET_GID).  Both must be specified.\n", target_uid_str);
            ABORT(255);
        }
        if (target_uid_str) {
            if ( -1 == str2int(target_uid_str, &target_uid) ) {
                message(ERROR, "Unable to convert target UID (%s) to integer: %s\n", target_uid_str, strerror(errno));
                ABORT(255);
            }
            if (target_uid < 500) {
                message(ERROR, "Target UID (%ld) must be 500 or greater to avoid system users.\n", target_uid);
                ABORT(255);
            }
            if (target_uid > 65534) { // Avoid anything greater than the traditional overflow UID.
                message(ERROR, "Target UID (%ld) cannot be greater than 65534.\n", target_uid);
                ABORT(255);
            }
        }
        if ( !target_uid_str && target_gid_str ) {
            message(ERROR, "A target GID is set (%s) but a target UID is not set (SINGULARITY_TARGET_UID).  Both must be specified.\n", target_gid_str);
            ABORT(255);
        }
        if (target_gid_str) {
            if ( -1 == str2int(target_gid_str, &target_gid) ) {
                message(ERROR, "Unable to convert target GID (%s) to integer: %s\n", target_gid_str, strerror(errno));
                ABORT(255);
            }
            if (target_gid < 500) {
                message(ERROR, "Target GID (%ld) must be 500 or greater to avoid system groups.\n", target_gid);
                ABORT(255);
            }
            if (target_gid > 65534) { // Avoid anything greater than the traditional overflow GID.
                message(ERROR, "Target GID (%ld) cannot be greater than 65534.\n", target_gid);
                ABORT(255);
            }
        }
    }
#endif
    if ( (target_uid >= 500) && (target_gid >= 500) ) {
        uinfo.target_mode = 1;
        uinfo.uid = target_uid;
        uinfo.gid = target_gid;
        uinfo.gids_count = 0;
        uinfo.gids = NULL;
    } else {
        uinfo.uid = getuid();
        uinfo.gid = getgid();
        uinfo.gids_count = getgroups(0, NULL);

        message(DEBUG, "Called priv_init(void)\n");

        uinfo.gids = (gid_t *) malloc(sizeof(gid_t) * uinfo.gids_count);

        if ( getgroups(uinfo.gids_count, uinfo.gids) < 0 ) {
            message(ERROR, "Could not obtain current supplementary group list: %s\n", strerror(errno));
            ABORT(255);
        }
    }
    uinfo.ready = 1;

    priv_drop();

    message(DEBUG, "Returning priv_init(void)\n");
}

void priv_userns_init(void) {
    uid_t uid = priv_getuid();
    gid_t gid = priv_getgid();

    {   
        message(DEBUG, "Setting setgroups to: 'deny'\n");
        char *map_file = (char *) malloc(PATH_MAX);
        snprintf(map_file, PATH_MAX-1, "/proc/%d/setgroups", getpid());
        FILE *map_fp = fopen(map_file, "w+");
        if ( map_fp != NULL ) {
            message(DEBUG, "Updating setgroups: %s\n", map_file);
            fprintf(map_fp, "deny\n");
            if ( fclose(map_fp) < 0 ) {
                message(ERROR, "Failed to write deny to setgroup file %s: %s\n", map_file, strerror(errno));
                ABORT(255);
            }
        } else {
            message(ERROR, "Could not write info to setgroups: %s\n", strerror(errno));
            ABORT(255);
        }
        free(map_file);
    }
    {   
        message(DEBUG, "Setting GID map to: '0 %i 1'\n", gid);
        char *map_file = (char *) malloc(PATH_MAX);
        snprintf(map_file, PATH_MAX-1, "/proc/%d/gid_map", getpid());
        FILE *map_fp = fopen(map_file, "w+");
        if ( map_fp != NULL ) {
            message(DEBUG, "Updating the parent gid_map: %s\n", map_file);
            fprintf(map_fp, "0 %i 1\n", gid);
            if ( fclose(map_fp) < 0 ) {
                message(ERROR, "Failed to write to GID map %s: %s\n", map_file, strerror(errno));
                ABORT(255);
            }
        } else {
            message(ERROR, "Could not write parent info to gid_map: %s\n", strerror(errno));
            ABORT(255);
        }
        free(map_file);
    }
    {   
        message(DEBUG, "Setting UID map to: '0 %i 1'\n", uid);
        char *map_file = (char *) malloc(PATH_MAX);
        snprintf(map_file, PATH_MAX-1, "/proc/%d/uid_map", getpid());
        FILE *map_fp = fopen(map_file, "w+");
        if ( map_fp != NULL ) {
            message(DEBUG, "Updating the parent uid_map: %s\n", map_file);
            fprintf(map_fp, "0 %i 1\n", uid);
            if ( fclose(map_fp) < 0 ) {
                message(ERROR, "Failed to write to UID map %s: %s\n", map_file, strerror(errno));
                ABORT(255);
            }
        } else {
            message(ERROR, "Could not write parent info to uid_map: %s\n", strerror(errno));
            ABORT(255);
        }
        free(map_file);
    }
}


void priv_escalate(void) {

    if ( getuid() != 0 ) {
        message(DEBUG, "Temporarily escalating privileges (U=%d)\n", getuid());

        if ( ( seteuid(0) < 0 ) || ( setegid(0) < 0 ) ) {
            message(ERROR, "The feature you are requesting requires privilege you do not have\n");
            ABORT(255);
        }

    } else {
        message(DEBUG, "Running as root, not changing privileges\n");
    }
}

void priv_drop(void) {

    if ( uinfo.ready != 1 ) {
        message(ERROR, "User info is not available\n");
        ABORT(255);
    }

    if ( getuid() != 0 ) {
        message(DEBUG, "Dropping privileges to UID=%d, GID=%d\n", uinfo.uid, uinfo.gid);

        if ( setegid(uinfo.gid) < 0 ) {
            message(ERROR, "Could not drop effective group privileges to gid %d: %s\n", uinfo.gid, strerror(errno));
            ABORT(255);
        }

        if ( seteuid(uinfo.uid) < 0 ) {
            message(ERROR, "Could not drop effective user privileges to uid %d: %s\n", uinfo.uid, strerror(errno));
            ABORT(255);
        }

        message(DEBUG, "Confirming we have correct UID/GID\n");
        if ( getgid() != uinfo.gid ) {
#ifdef SINGULARITY_NOSUID
            if ( uinfo.target_mode && getgid() != 0 ) {
                message(ERROR, "Non-zero real GID for target mode: %d\n", getgid());
                    ABORT(255);
                } else if ( !uinfo.target_mode )
#endif  // SINGULARITY_NOSUID
                {
                    message(ERROR, "Failed to drop effective group privileges to gid %d (currently %d)\n", uinfo.gid, getgid());
                    ABORT(255);
                }
            }

            if ( getuid() != uinfo.uid ) {
#ifdef SINGULARITY_NOSUID
            if ( uinfo.target_mode && getuid() != 0 ) {
                message(ERROR, "Non-zero real UID for target mode: %d\n", getuid());
                ABORT(255);
            } else if ( !uinfo.target_mode )
#endif  // SINGULARITY_NOSUID
            {
                message(ERROR, "Failed to drop effective user privileges to uid %d (currently %d)\n", uinfo.uid, getuid());
                ABORT(255);
            }
        }
    } else {
        message(DEBUG, "Running as root, not changing privileges\n");
    }
}

void priv_drop_perm(void) {
    message(DEBUG, "Called priv_drop_perm(void)\n");

    if ( uinfo.ready != 1 ) {
        message(ERROR, "User info is not available\n");
        ABORT(255);
    }


    if ( singularity_ns_user_enabled() == 0 ) {
        uid_t uid = priv_getuid();
        gid_t gid = priv_getgid();

        {   
            message(DEBUG, "Setting setgroups to: 'deny'\n");
            char *map_file = (char *) malloc(PATH_MAX);
            snprintf(map_file, PATH_MAX-1, "/proc/%d/setgroups", getpid());
            FILE *map_fp = fopen(map_file, "w+");
            if ( map_fp != NULL ) {
                message(DEBUG, "Updating setgroups: %s\n", map_file);
                fprintf(map_fp, "deny\n");
                if ( fclose(map_fp) < 0 ) {
                    message(ERROR, "Failed to write deny to setgroup file %s: %s\n", map_file, strerror(errno));
//                    ABORT(255);
                }
            } else {
                message(ERROR, "Could not write info to setgroups: %s\n", strerror(errno));
                ABORT(255);
            }
            free(map_file);
        }
        {   
            message(DEBUG, "Setting GID map to: '%i 0 1'\n", gid);
            char *map_file = (char *) malloc(PATH_MAX);
            snprintf(map_file, PATH_MAX-1, "/proc/%d/gid_map", getpid());
            FILE *map_fp = fopen(map_file, "w+");
            if ( map_fp != NULL ) {
                message(DEBUG, "Updating the parent gid_map: %s\n", map_file);
                fprintf(map_fp, "%i 0 1\n", gid);
                if ( fclose(map_fp) < 0 ) {
                    message(ERROR, "Failed to write to GID map %s: %s\n", map_file, strerror(errno));
//                    ABORT(255);
                }
            } else {
                message(ERROR, "Could not write parent info to gid_map: %s\n", strerror(errno));
                ABORT(255);
            }
            free(map_file);
        }
        {   
            message(DEBUG, "Setting UID map to: '%i 0 1'\n", uid);
            char *map_file = (char *) malloc(PATH_MAX);
            snprintf(map_file, PATH_MAX-1, "/proc/%d/uid_map", getpid());
            FILE *map_fp = fopen(map_file, "w+");
            if ( map_fp != NULL ) {
                message(DEBUG, "Updating the parent uid_map: %s\n", map_file);
                fprintf(map_fp, "%i 0 1\n", uid);
                if ( fclose(map_fp) < 0 ) {
                    message(ERROR, "Failed to write to UID map %s: %s\n", map_file, strerror(errno));
//                    ABORT(255);
                }
            } else {
                message(ERROR, "Could not write parent info to uid_map: %s\n", strerror(errno));
                ABORT(255);
            }
            free(map_file);
        }

return;
    } else if ( priv_getuid() != 0 ) {
        if ( !uinfo.userns_ready ) {
            message(DEBUG, "Resetting supplementary groups\n");
            if ( setgroups(uinfo.gids_count, uinfo.gids) < 0 ) {
                message(ERROR, "Could not reset supplementary group list: %s\n", strerror(errno));
//                ABORT(255);
            }
        } else {
            message(DEBUG, "Not resetting supplementary groups as we are running in a user namespace.\n");
        }

        message(DEBUG, "Dropping to group ID '%d'\n", uinfo.gid);
        if ( setgid(uinfo.gid) < 0 ) {
            message(ERROR, "Could not dump group privileges: %s\n", strerror(errno));
            ABORT(255);
        }

        message(DEBUG, "Dropping real and effective privileges to GID = '%d'\n", uinfo.gid);
        if ( setregid(uinfo.gid, uinfo.gid) < 0 ) {
            message(ERROR, "Could not dump real and effective group privileges: %s\n", strerror(errno));
            ABORT(255);
        }

        message(DEBUG, "Dropping real and effective privileges to UID = '%d'\n", uinfo.uid);
        if ( setreuid(uinfo.uid, uinfo.uid) < 0 ) {
            message(ERROR, "Could not dump real and effective user privileges: %s\n", strerror(errno));
            ABORT(255);
        }

    } else {
        message(DEBUG, "Running as root, no privileges to drop\n");
    }

    message(DEBUG, "Confirming we have correct GID\n");
    if ( getgid() != uinfo.gid ) {
        message(ERROR, "Failed to drop effective group privileges to gid %d: %s\n", uinfo.gid, strerror(errno));
        ABORT(255);
    }

    message(DEBUG, "Confirming we have correct UID\n");
    if ( getuid() != uinfo.uid ) {
        message(ERROR, "Failed to drop effective user privileges to uid %d: %s\n", uinfo.uid, strerror(errno));
        ABORT(255);
    }

    message(DEBUG, "Returning priv_drop_perm(void)\n");
}



uid_t priv_getuid() {
    if ( !uinfo.ready ) {
        message(ERROR, "Invoked before privilege info initialized!\n");
        ABORT(255);
    }
    return uinfo.uid;
}


gid_t priv_getgid() {
    if ( !uinfo.ready ) {
        message(ERROR, "Invoked before privilege info initialized!\n");
        ABORT(255);
    }
    return uinfo.gid;
}


const gid_t *priv_getgids() {
    if ( !uinfo.ready ) {
        message(ERROR, "Invoked before privilege info initialized!\n");
        ABORT(255);
    }
    return uinfo.gids;
}


int priv_getgidcount() {
    if ( !uinfo.ready ) {
        message(ERROR, "Invoked before privilege info initialized!\n");
        ABORT(255);
    }
    return uinfo.gids_count;
}
