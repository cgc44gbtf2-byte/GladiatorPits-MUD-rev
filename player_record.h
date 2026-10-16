#ifndef GLAD_PLAYER_RECORD_H
#define GLAD_PLAYER_RECORD_H

/* The Gladiator Pits, Copyright (C) 2000 Richard Woolcock.
 * May be used/distributed freely if this notice is retained.
 * Exact historical native save layout. Do not reorder or pack these fields. */
struct connection;
typedef struct body {
   char a_chName[16];
   int iStats[5];
   int iSpeed,iRoom,iWin,iLoss,iKill,iDam;
   struct connection *pstOpponent;
} body_t;

typedef struct {
   int stats[5];
   int speed,room,wins,losses,kills,damage;
} saved_character;

int ValidPlayerName(const char *name);
int ValidPlayer(const body_t *body,const char *name);
/* 1: valid snapshot; 0: absent; -1: invalid/unreadable. Never writes a file. */
int CharacterInspect(const char *name,saved_character *output);

#endif
