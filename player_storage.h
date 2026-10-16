#ifndef GLAD_PLAYER_STORAGE_H
#define GLAD_PLAYER_STORAGE_H

#include "player_record.h"

#define PLAYER_STORAGE_JOURNAL ".glad-settlement"
#define PLAYER_STORAGE_LOCK ".glad-storage.lock"

/* Main-thread only; keep the working directory unchanged while open.
 * Open locks the private data directory and completes pending work first.
 * Commit stores absolute after-images: one/two writes, or one write + deletion.
 * A failed commit freezes writes until close/open recovery. Never resume the
 * old in-memory game after a failure; restart it from recovered records. */
int PlayerStorageOpen(void);
void PlayerStorageClose(void);
int PlayerStorageCommit(const body_t *first,const body_t *second,
   const char *deleted_name);

/* Fault points have no effect in production; hooks exist only in test binaries.
 * No environment variable or client command can enable fault injection. */
enum storage_fault {
   ST_JOURNAL_WRITE=1,ST_JOURNAL_SYNC,ST_JOURNAL_RENAME,ST_JOURNAL_DIRSYNC,
   ST_RECORD_WRITE,ST_RECORD_SYNC,ST_RECORD_RENAME,ST_RECORD_DIRSYNC,
   ST_DELETE,ST_CLEAR,ST_CLEAR_DIRSYNC
};
#ifdef GLAD_STORAGE_TESTING
void StorageTestFault(enum storage_fault point,unsigned int hit,int error,int crash);
#endif

#endif
