/* Recoverable native player storage for The Gladiator Pits.
 * Original player layout: Copyright (C) 2000 Richard Woolcock; retained.
 * Gameplay/account metadata are not changed by journal replay. */
#define _POSIX_C_SOURCE 200809L
#include "player_storage.h"
#include <sodium.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct {
   uint32_t operation;
   body_t body;
} storage_entry;

typedef struct {
   char magic[8];
   uint32_t body_size,count;
   storage_entry entries[2];
   unsigned char checksum[crypto_generichash_BYTES];
} storage_journal;

static int directory_fd=-1,lock_fd=-1,storage_failed;

#ifdef GLAD_STORAGE_TESTING
static enum storage_fault fault_point;
static unsigned int fault_hit;
static int fault_error,fault_crash;

void StorageTestFault(enum storage_fault point,unsigned int hit,int error,int crash)
{
   fault_point=point;
   fault_hit=hit?hit:1;
   fault_error=error;
   fault_crash=crash;
}

static int StorageFault(enum storage_fault point)
{
   if(fault_point!=point||--fault_hit!=0)
      return 0;
   fault_point=(enum storage_fault)0;
   if(fault_crash)
      _exit(86);
   errno=fault_error;
   return 1;
}
#else
#define StorageFault(point) 0
#endif

static int Sync(int fd,enum storage_fault point)
{
   (void)point;
   if(StorageFault(point))
      return 0;
   while(fsync(fd)!=0)
      if(errno!=EINTR)
         return 0;
   return 1;
}

static int Transfer(int fd,void *buffer,size_t length,int writing)
{
   unsigned char *bytes=buffer;
   while(length)
   {
      ssize_t count=writing?write(fd,bytes,length):read(fd,bytes,length);
      if(count<0&&errno==EINTR)
         continue;
      if(count<=0)
      {
         if(count==0)
            errno=EIO;
         return 0;
      }
      bytes+=(size_t)count;
      length-=(size_t)count;
   }
   return 1;
}

static int SafeTarget(const char *name)
{
   struct stat info;
   if(fstatat(directory_fd,name,&info,AT_SYMLINK_NOFOLLOW)!=0)
      return errno==ENOENT;
   if(S_ISREG(info.st_mode)&&info.st_uid==getuid())
      return 1;
   errno=EPERM;
   return 0;
}

static int AtomicWrite(const char *name,const void *buffer,size_t length,int journal)
{
   char temporary[]=".glad-tmp.XXXXXX";
   int fd=-1,error;
   if(!SafeTarget(name)||(fd=mkstemp(temporary))<0)
      return 0;
   if(StorageFault(journal?ST_JOURNAL_WRITE:ST_RECORD_WRITE)||
      !Transfer(fd,(void*)buffer,length,1)||
      !Sync(fd,journal?ST_JOURNAL_SYNC:ST_RECORD_SYNC))
      goto failed;
   if(close(fd)!=0)
   {
      fd=-1;
      goto failed;
   }
   fd=-1;
   if(StorageFault(journal?ST_JOURNAL_RENAME:ST_RECORD_RENAME)||
      renameat(directory_fd,temporary,directory_fd,name)!=0)
      goto failed;
   return Sync(directory_fd,journal?ST_JOURNAL_DIRSYNC:ST_RECORD_DIRSYNC);
failed:
   error=errno;
   if(fd>=0)
      close(fd);
   unlinkat(directory_fd,temporary,0);
   errno=error;
   return 0;
}

static int BodyValid(const body_t *body)
{
   return memchr(body->a_chName,'\0',sizeof(body->a_chName))&&
      ValidPlayerName(body->a_chName)&&ValidPlayer(body,body->a_chName);
}

static void Normalize(body_t *output,const body_t *input)
{
   memset(output,0,sizeof(*output));
   memcpy(output->a_chName,input->a_chName,strlen(input->a_chName)+1);
   memcpy(output->iStats,input->iStats,sizeof(output->iStats));
   output->iSpeed=input->iSpeed;
   output->iRoom=input->iRoom;
   output->iWin=input->iWin;
   output->iLoss=input->iLoss;
   output->iKill=input->iKill;
   output->iDam=input->iDam;
}

/* -1: invalid/I/O failure; 0: absent; 1: fully validated intent. */
static int ReadJournal(storage_journal *journal)
{
   struct stat info;
   unsigned char checksum[crypto_generichash_BYTES];
   uint32_t i;
   int fd=openat(directory_fd,PLAYER_STORAGE_JOURNAL,
      O_RDONLY|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC),ok,error;
   if(fd<0)
      return errno==ENOENT?0:-1;
   ok=fstat(fd,&info)==0&&S_ISREG(info.st_mode)&&info.st_uid==getuid()&&
      (info.st_mode&0077)==0&&info.st_nlink==1&&
      info.st_size==(off_t)sizeof(*journal);
   if(!ok)
      errno=EINVAL;
   else
      ok=Transfer(fd,journal,sizeof(*journal),0);
   error=errno;
   if(close(fd)!=0&&ok)
   {
      ok=0;
      error=errno;
   }
   errno=error;
   if(!ok)
      return -1;
   crypto_generichash(checksum,sizeof(checksum),(const unsigned char*)journal,
      offsetof(storage_journal,checksum),NULL,0);
   if(memcmp(journal->magic,"GLADTX1",8)!=0||
      journal->body_size!=sizeof(body_t)||journal->count<1||journal->count>2||
      sodium_memcmp(checksum,journal->checksum,sizeof(checksum))!=0)
      goto invalid;
   for(i=0;i<journal->count;i++)
   {
      storage_entry *entry=&journal->entries[i];
      if(!memchr(entry->body.a_chName,'\0',sizeof(entry->body.a_chName))||
         !ValidPlayerName(entry->body.a_chName)||entry->body.pstOpponent||
         (entry->operation!=1&&entry->operation!=2)||
         (entry->operation==1&&!BodyValid(&entry->body))||
         (i&&strcmp(entry->body.a_chName,journal->entries[0].body.a_chName)==0))
         goto invalid;
   }
   return 1;
invalid:
   errno=EINVAL;
   return -1;
}

static int Apply(const storage_journal *journal)
{
   uint32_t i;
   for(i=0;i<journal->count;i++)
   {
      const storage_entry *entry=&journal->entries[i];
      if(entry->operation==1)
      {
         if(!AtomicWrite(entry->body.a_chName,&entry->body,sizeof(body_t),0))
            return 0;
      }
      else
      {
         if(!SafeTarget(entry->body.a_chName)||StorageFault(ST_DELETE))
            return 0;
         if(unlinkat(directory_fd,entry->body.a_chName,0)!=0&&errno!=ENOENT)
            return 0;
         if(!Sync(directory_fd,ST_RECORD_DIRSYNC))
            return 0;
      }
   }
   if(StorageFault(ST_CLEAR))
      return 0;
   if(unlinkat(directory_fd,PLAYER_STORAGE_JOURNAL,0)!=0&&errno!=ENOENT)
      return 0;
   return Sync(directory_fd,ST_CLEAR_DIRSYNC);
}

void PlayerStorageClose(void)
{
   if(lock_fd>=0)
      close(lock_fd);
   if(directory_fd>=0)
      close(directory_fd);
   directory_fd=lock_fd=-1;
   storage_failed=0;
}

int PlayerStorageOpen(void)
{
   struct stat info;
   struct flock lock;
   storage_journal journal;
   int pending,error;
   if(directory_fd>=0)
   {
      errno=EBUSY;
      return 0;
   }
   if(sodium_init()<0)
   {
      errno=EIO;
      return 0;
   }
   directory_fd=open(".",O_RDONLY|O_DIRECTORY|O_CLOEXEC);
   if(directory_fd<0)
      return 0;
   if(fstat(directory_fd,&info)!=0||info.st_uid!=getuid()||(info.st_mode&0022)!=0)
   {
      errno=EPERM;
      goto failed;
   }
   lock_fd=openat(directory_fd,PLAYER_STORAGE_LOCK,
      O_CREAT|O_RDWR|O_NOFOLLOW|O_CLOEXEC,0600);
   if(lock_fd<0)
      goto failed;
   if(fstat(lock_fd,&info)!=0||!S_ISREG(info.st_mode)||info.st_uid!=getuid()||
      (info.st_mode&0077)!=0||info.st_nlink!=1)
   {
      errno=EPERM;
      goto failed;
   }
   memset(&lock,0,sizeof(lock));
   lock.l_type=F_WRLCK;
   lock.l_whence=SEEK_SET;
   if(fcntl(lock_fd,F_SETLK,&lock)!=0)
      goto failed;
   pending=ReadJournal(&journal);
   if(pending<0||(pending&&!Apply(&journal)))
      goto failed;
   storage_failed=0;
   return 1;
failed:
   error=errno;
   PlayerStorageClose();
   errno=error;
   return 0;
}

int PlayerStorageCommit(const body_t *first,const body_t *second,const char *deleted_name)
{
   storage_journal journal;
   struct stat info;
   int error;
   if(directory_fd<0||storage_failed)
   {
      errno=EIO;
      return 0;
   }
   if(!first||!BodyValid(first)||(second&&deleted_name)||
      (second&&(!BodyValid(second)||!strcmp(first->a_chName,second->a_chName)))||
      (deleted_name&&(strnlen(deleted_name,16)>=16||!ValidPlayerName(deleted_name)||
         !strcmp(first->a_chName,deleted_name))))
   {
      errno=EINVAL;
      goto failed;
   }
   if(fstatat(directory_fd,PLAYER_STORAGE_JOURNAL,&info,AT_SYMLINK_NOFOLLOW)==0)
   {
      errno=EEXIST;
      goto failed;
   }
   if(errno!=ENOENT)
      goto failed;
   memset(&journal,0,sizeof(journal));
   memcpy(journal.magic,"GLADTX1",8);
   journal.body_size=(uint32_t)sizeof(body_t);
   journal.count=second||deleted_name?2U:1U;
   journal.entries[0].operation=1;
   Normalize(&journal.entries[0].body,first);
   if(second)
   {
      journal.entries[1].operation=1;
      Normalize(&journal.entries[1].body,second);
   }
   else if(deleted_name)
   {
      journal.entries[1].operation=2;
      memcpy(journal.entries[1].body.a_chName,deleted_name,strlen(deleted_name)+1);
   }
   crypto_generichash(journal.checksum,sizeof(journal.checksum),
      (const unsigned char*)&journal,offsetof(storage_journal,checksum),NULL,0);
   if(!AtomicWrite(PLAYER_STORAGE_JOURNAL,&journal,sizeof(journal),1)||!Apply(&journal))
      goto failed;
   return 1;
failed:
   error=errno;
   storage_failed=1;
   errno=error;
   return 0;
}
