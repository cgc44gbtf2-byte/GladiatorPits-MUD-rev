/* Modernization contributions: Copyright (c) 2026 LaWaylon Battles (aKa Yami). */
/* Player validation derived from The Gladiator Pits, Copyright (C) 2000
 * Richard Woolcock. May be used/distributed freely if this notice is retained. */
#define _POSIX_C_SOURCE 200809L
#include "player_record.h"
#include <ctype.h>
#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>

int ValidPlayerName(const char *name)
{
   size_t i,length=strlen(name);
   if(length<3||length>15)
      return 0;
   for(i=0;i<length;i++)
      if(!isalpha((unsigned char)name[i]))
         return 0;
   return 1;
}

int ValidPlayer(const body_t *body,const char *name)
{
   int i;
   if(!memchr(body->a_chName,'\0',sizeof(body->a_chName))||
      strcmp(body->a_chName,name)!=0)
      return 0;
   for(i=0;i<5;i++)
      if(body->iStats[i]<1||body->iStats[i]>9)
         return 0;
   return body->iWin>=0&&body->iLoss>=0&&body->iKill>=0&&
      body->iDam>=0&&body->iDam<=
         body->iStats[0]+body->iStats[2]*7+body->iStats[3]*3;
}

int CharacterInspect(const char *name,saved_character *output)
{
   body_t body;
   struct stat info;
   FILE *file;
   int fd,ok;
   if(!ValidPlayerName(name))
      return -1;
   fd=open(name,O_RDONLY|O_NOFOLLOW|O_NONBLOCK);
   if(fd<0)
      return errno==ENOENT?0:-1;
   if(fstat(fd,&info)!=0||!S_ISREG(info.st_mode)||
      info.st_size!=(off_t)sizeof(body))
   {
      close(fd);
      return -1;
   }
   file=fdopen(fd,"rb");
   if(!file)
   {
      close(fd);
      return -1;
   }
   ok=fread(&body,sizeof(body),1,file)==1&&fgetc(file)==EOF&&
      !ferror(file)&&ValidPlayer(&body,name);
   fclose(file);
   if(!ok)
      return -1;
   memcpy(output->stats,body.iStats,sizeof(output->stats));
   output->speed=body.iSpeed;
   output->room=body.iRoom;
   output->wins=body.iWin;
   output->losses=body.iLoss;
   output->kills=body.iKill;
   output->damage=body.iDam;
   return 1;
}
