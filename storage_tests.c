/* Run only in the disposable .storage-test-run directory. */
#define _POSIX_C_SOURCE 200809L
#include "player_storage.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

static int checks;
static body_t first,second;

static void Check(int condition,const char *message)
{
   if(!condition)
   {
      fprintf(stderr,"FAIL: %s\n",message);
      exit(1);
   }
   checks++;
}

static body_t Body(const char *name)
{
   body_t body;
   int i;
   memset(&body,0,sizeof(body));
   strcpy(body.a_chName,name);
   for(i=0;i<5;i++)
      body.iStats[i]=1;
   body.iWin=4;
   body.iLoss=2;
   return body;
}

static void Snapshot(const body_t *expected,int absent)
{
   body_t actual;
   struct stat info;
   FILE *file;
   if(absent)
   {
      Check(lstat(expected->a_chName,&info)!=0&&errno==ENOENT,
         "committed death remains absent");
      return;
   }
   file=fopen(expected->a_chName,"rb");
   Check(file&&fread(&actual,sizeof(actual),1,file)==1&&fgetc(file)==EOF,
      "record retains exact native size");
   Check(fclose(file)==0,"snapshot descriptor closes");
   Check(!strcmp(actual.a_chName,expected->a_chName)&&
      memcmp(actual.iStats,expected->iStats,sizeof(actual.iStats))==0&&
      actual.iSpeed==expected->iSpeed&&actual.iRoom==expected->iRoom&&
      actual.iWin==expected->iWin&&actual.iLoss==expected->iLoss&&
      actual.iKill==expected->iKill&&actual.iDam==expected->iDam&&
      actual.pstOpponent==NULL,"record contains exact absolute after-image without pointers");
}

static void Baseline(void)
{
   Check(PlayerStorageCommit(&first,&second,NULL),"baseline records commit together");
}

static void Failures(void)
{
   body_t winner=first,loser=second;
   int death,point,error;
   winner.iWin++;
   loser.iLoss++;
   for(death=0;death<=1;death++)
      for(point=ST_JOURNAL_WRITE;point<=ST_CLEAR_DIRSYNC;point++)
      {
         if(!death&&point==ST_DELETE)
            continue;
         Baseline();
         error=(point==ST_JOURNAL_WRITE||point==ST_RECORD_WRITE||
            point==ST_JOURNAL_SYNC||point==ST_RECORD_SYNC)?ENOSPC:EIO;
         StorageTestFault((enum storage_fault)point,1,error,0);
         Check(!PlayerStorageCommit(&winner,death?NULL:&loser,
            death?second.a_chName:NULL)&&errno==error,"I/O fault is returned to caller");
         Check(!PlayerStorageCommit(&first,&second,NULL),"failed backend rejects further writes");
         PlayerStorageClose();
         Check(PlayerStorageOpen(),"restart recovers after the I/O fault is removed");
         Snapshot(point<=ST_JOURNAL_RENAME?&first:&winner,0);
         Snapshot(point<=ST_JOURNAL_RENAME?&second:&loser,
            death&&point>ST_JOURNAL_RENAME);
         Check(access(PLAYER_STORAGE_JOURNAL,F_OK)!=0&&errno==ENOENT,
            "only fully applied and synced intent is cleared");
         PlayerStorageClose();
         Check(PlayerStorageOpen(),"repeated recovery is idempotent");
         Snapshot(point<=ST_JOURNAL_RENAME?&first:&winner,0);
      }
}

static void Crashes(void)
{
   body_t winner=first,loser=second;
   static const enum storage_fault points[]={
      ST_JOURNAL_WRITE,ST_JOURNAL_DIRSYNC,ST_RECORD_RENAME,ST_DELETE,
      ST_CLEAR,ST_CLEAR_DIRSYNC
   };
   size_t i;
   int death,status;
   pid_t child;
   winner.iWin++;
   winner.iKill++;
   loser.iLoss++;
   for(death=0;death<=1;death++)
      for(i=0;i<sizeof(points)/sizeof(points[0]);i++)
      {
         if(!death&&points[i]==ST_DELETE)
            continue;
         Baseline();
         PlayerStorageClose();
         child=fork();
         Check(child>=0,"crash fixture fork");
         if(child==0)
         {
            if(!PlayerStorageOpen())
               _exit(90);
            StorageTestFault(points[i],!death&&points[i]==ST_RECORD_RENAME?2U:1U,EIO,1);
            PlayerStorageCommit(&winner,death?NULL:&loser,death?second.a_chName:NULL);
            _exit(91);
         }
         Check(waitpid(child,&status,0)==child&&WIFEXITED(status)&&WEXITSTATUS(status)==86,
            "process exits abruptly at the selected persistence boundary");
         Check(PlayerStorageOpen(),"fresh process state replays interrupted settlement");
         Snapshot(points[i]==ST_JOURNAL_WRITE?&first:&winner,0);
         Snapshot(points[i]==ST_JOURNAL_WRITE?&second:&loser,
            death&&points[i]!=ST_JOURNAL_WRITE);
         PlayerStorageClose();
         Check(PlayerStorageOpen(),"second restart never increments progression again");
         Snapshot(points[i]==ST_JOURNAL_WRITE?&first:&winner,0);
      }
}

static void InvalidAndExclusive(void)
{
   pid_t child;
   int status,fd;
   char byte=0;
   Baseline();
   child=fork();
   Check(child>=0,"exclusive storage fixture fork");
   if(child==0)
   {
      PlayerStorageClose();
      _exit(PlayerStorageOpen()?92:0);
   }
   Check(waitpid(child,&status,0)==child&&WIFEXITED(status)&&WEXITSTATUS(status)==0,
      "second process cannot share a live storage directory");
   StorageTestFault(ST_RECORD_WRITE,1,ENOSPC,0);
   Check(!PlayerStorageCommit(&first,&second,NULL),"prepare an unapplied journal fixture");
   PlayerStorageClose();
   fd=open(PLAYER_STORAGE_JOURNAL,O_RDWR);
   Check(fd>=0&&lseek(fd,-1,SEEK_END)>=0&&read(fd,&byte,1)==1&&
      lseek(fd,-1,SEEK_END)>=0,"checksum-only corruption fixture opens the test-owned journal");
   byte^=1;
   Check(write(fd,&byte,1)==1&&close(fd)==0,"corrupt the test journal");
   Check(!PlayerStorageOpen()&&access(PLAYER_STORAGE_JOURNAL,F_OK)==0,
      "invalid intent blocks startup and is retained");
   Snapshot(&first,0);
   Snapshot(&second,0);
   Check(unlink(PLAYER_STORAGE_JOURNAL)==0,"discard only the intentionally corrupted fixture");
   Check(symlink(first.a_chName,PLAYER_STORAGE_JOURNAL)==0,"journal symlink fixture");
   Check(!PlayerStorageOpen(),"journal symlink blocks startup without following it");
   Snapshot(&first,0);
   Check(unlink(PLAYER_STORAGE_JOURNAL)==0&&PlayerStorageOpen(),"restore safe journal path");
   Check(mkdir("Unsafehero",0700)==0,"unsafe target directory fixture");
   {
      body_t unsafe=Body("Unsafehero");
      Check(!PlayerStorageCommit(&unsafe,NULL,NULL),"player directory is not replaced or deleted");
      Check(rmdir("Unsafehero")==0,"unsafe target fixture remains intact");
      PlayerStorageClose();
      Check(PlayerStorageOpen(),"pending write completes only after target is made safe");
      Snapshot(&unsafe,0);
      Check(unlink("Unsafehero")==0,"unsafe fixture record cleanup");
   }
}

int main(void)
{
   umask(0077);
   first=Body("Firsthero");
   second=Body("Secondhero");
   Check(PlayerStorageOpen(),"private storage opens");
   Baseline();
   Snapshot(&first,0);
   Snapshot(&second,0);
   Failures();
   Crashes();
   InvalidAndExclusive();
   PlayerStorageClose();
   unlink(first.a_chName);
   unlink(second.a_chName);
   printf("Passed %d storage/recovery regression checks.\n",checks);
   return 0;
}
