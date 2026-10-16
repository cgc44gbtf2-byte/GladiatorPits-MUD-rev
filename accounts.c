/* Account metadata is deliberately separate from the historical player ABI. */
#define _POSIX_C_SOURCE 200809L
#include "accounts.h"
#include <sodium.h>
#include <sqlite3.h>
#include <pthread.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>

static sqlite3 *database;
static pthread_mutex_t database_lock=PTHREAD_MUTEX_INITIALIZER;
static char dummy_hash[crypto_pwhash_STRBYTES];

static int Execute(const char *sql)
{
   return sqlite3_exec(database,sql,NULL,NULL,NULL)==SQLITE_OK;
}

static sqlite3_stmt *Prepare(const char *sql)
{
   sqlite3_stmt *statement=NULL;
   if(sqlite3_prepare_v2(database,sql,-1,&statement,NULL)!=SQLITE_OK)
      return NULL;
   return statement;
}

static void Text(sqlite3_stmt *statement,int index,const char *value)
{
   sqlite3_bind_text(statement,index,value,-1,SQLITE_TRANSIENT);
}

/* Called only while the database mutex is held. Never include secret data. */
static int Audit(const char *event,int64_t id)
{
   sqlite3_stmt *statement=Prepare("INSERT INTO audit(event,account_id) VALUES(?,?)");
   int ok=0;
   if(statement)
   {
      Text(statement,1,event);
      sqlite3_bind_int64(statement,2,id);
      ok=sqlite3_step(statement)==SQLITE_DONE;
   }
   sqlite3_finalize(statement);
   return ok;
}

int AccountsOpen(const char *path)
{
   struct stat info;
   sqlite3_stmt *statement;
   int version=-1,fd;
   if(database||sodium_init()<0)
      return 0;
   /* WAL/journal paths must not be creatable by another OS user. */
   if(stat(".",&info)!=0||info.st_uid!=getuid()||(info.st_mode&0022)!=0)
      return 0;
   fd=open(path,O_CREAT|O_EXCL|O_WRONLY|O_NOFOLLOW,0600);
   if(fd>=0)
      close(fd);
   else if(errno!=EEXIST)
      return 0;
   if(lstat(path,&info)!=0||!S_ISREG(info.st_mode)||info.st_uid!=getuid()||
      (info.st_mode&0077)!=0)
      return 0;
   if(sqlite3_open_v2(path,&database,
      SQLITE_OPEN_READWRITE|SQLITE_OPEN_FULLMUTEX|SQLITE_OPEN_NOFOLLOW,
      NULL)!=SQLITE_OK)
      goto failed;
   sqlite3_busy_timeout(database,100);
   statement=Prepare("PRAGMA user_version");
   if(statement&&sqlite3_step(statement)==SQLITE_ROW)
      version=sqlite3_column_int(statement,0);
   sqlite3_finalize(statement);
   if((version!=0&&version!=1)||!Execute("PRAGMA foreign_keys=ON")||
      !Execute("PRAGMA journal_mode=WAL")||!Execute("PRAGMA synchronous=FULL")||
      !Execute("BEGIN IMMEDIATE"))
      goto failed;
   if(!Execute(
      "CREATE TABLE IF NOT EXISTS accounts("
      "id INTEGER PRIMARY KEY,name TEXT NOT NULL UNIQUE COLLATE NOCASE,"
      "password_hash TEXT NOT NULL,role TEXT NOT NULL DEFAULT 'player' "
      "CHECK(role IN ('player','builder','moderator','administrator','owner')),"
      "epoch INTEGER NOT NULL DEFAULT 1,disabled INTEGER NOT NULL DEFAULT 0 "
      "CHECK(disabled IN (0,1)));"
      "CREATE TABLE IF NOT EXISTS characters("
      "name TEXT PRIMARY KEY COLLATE NOCASE,"
      "account_id INTEGER NOT NULL REFERENCES accounts(id));"
      "CREATE TABLE IF NOT EXISTS audit("
      "id INTEGER PRIMARY KEY,event TEXT NOT NULL,account_id INTEGER NOT NULL,"
      "created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP);"
      "PRAGMA user_version=1;")||!Execute("COMMIT"))
      goto failed;
   if(crypto_pwhash_str_alg(dummy_hash,"dummy-not-a-login-password",26,
      crypto_pwhash_OPSLIMIT_INTERACTIVE,crypto_pwhash_MEMLIMIT_INTERACTIVE,
      crypto_pwhash_ALG_ARGON2ID13)!=0)
      goto failed;
   return 1;
failed:
   AccountsClose();
   return 0;
}

void AccountsClose(void)
{
   if(database)
      sqlite3_close(database);
   database=NULL;
   sodium_memzero(dummy_hash,sizeof(dummy_hash));
}

int AccountNameValid(const char *name)
{
   size_t i,length=strlen(name);
   if(length<3||length>ACCOUNT_NAME_MAX)
      return 0;
   for(i=0;i<length;i++)
      if(!((name[i]>='a'&&name[i]<='z')||(name[i]>='A'&&name[i]<='Z')||
         (i>0&&((name[i]>='0'&&name[i]<='9')||name[i]=='_'))))
         return 0;
   return 1;
}

int AccountPasswordValid(const char *password)
{
   size_t length=strlen(password);
   return length>=15&&length<=ACCOUNT_PASSWORD_MAX;
}

const char *AccountRoleName(account_role role)
{
   static const char *names[]={"player","builder","moderator","administrator","owner"};
   return role>=ROLE_PLAYER&&role<=ROLE_OWNER?names[(int)role]:"none";
}

account_role AccountRoleParse(const char *name)
{
   int role;
   for(role=(int)ROLE_PLAYER;role<=(int)ROLE_OWNER;role++)
      if(strcmp(name,AccountRoleName((account_role)role))==0)
         return (account_role)role;
   return ROLE_NONE;
}

int AccountRoleCan(account_role role,account_permission permission)
{
   unsigned int capabilities;
   if(permission==PERM_PUBLIC)
      return 1;
   switch(role)
   {
      case ROLE_PLAYER: capabilities=PERM_PLAY; break;
      case ROLE_BUILDER:
         capabilities=PERM_PLAY|PERM_DASHBOARD|PERM_WORLD_EDIT; break;
      case ROLE_MODERATOR:
         capabilities=PERM_PLAY|PERM_DASHBOARD|PERM_PLAYER_MODERATE; break;
      case ROLE_ADMINISTRATOR:
         capabilities=PERM_PLAY|PERM_DASHBOARD|PERM_WORLD_EDIT|
            PERM_PLAYER_MODERATE|PERM_PLAYER_EDIT|PERM_SHUTDOWN; break;
      case ROLE_OWNER:
         capabilities=PERM_PLAY|PERM_DASHBOARD|PERM_WORLD_EDIT|
            PERM_PLAYER_MODERATE|PERM_PLAYER_EDIT|PERM_SHUTDOWN|PERM_ROLES_MANAGE; break;
      default: return 0;
   }
   return (capabilities&(unsigned int)permission)==(unsigned int)permission;
}

account_role AccountSessionRole(account_session session)
{
   account_role role=ROLE_NONE;
   sqlite3_stmt *statement;
   pthread_mutex_lock(&database_lock);
   statement=Prepare("SELECT role FROM accounts WHERE id=? AND epoch=? AND disabled=0");
   if(statement)
   {
      sqlite3_bind_int64(statement,1,session.id);
      sqlite3_bind_int64(statement,2,session.epoch);
      if(sqlite3_step(statement)==SQLITE_ROW)
         role=AccountRoleParse((const char*)sqlite3_column_text(statement,0));
   }
   sqlite3_finalize(statement);
   pthread_mutex_unlock(&database_lock);
   return role;
}

int AccountRegister(const char *name,const char *password,account_session *session)
{
   char hash[crypto_pwhash_STRBYTES];
   sqlite3_stmt *statement;
   int ok=0;
   memset(session,0,sizeof(*session));
   if(!AccountNameValid(name)||!AccountPasswordValid(password)||
      crypto_pwhash_str_alg(hash,password,(unsigned long long)strlen(password),
         crypto_pwhash_OPSLIMIT_INTERACTIVE,crypto_pwhash_MEMLIMIT_INTERACTIVE,
         crypto_pwhash_ALG_ARGON2ID13)!=0)
      return 0;
   pthread_mutex_lock(&database_lock);
   if(Execute("BEGIN IMMEDIATE"))
   {
      statement=Prepare("INSERT INTO accounts(name,password_hash) VALUES(?,?)");
      if(statement)
      {
         Text(statement,1,name);
         Text(statement,2,hash);
         if(sqlite3_step(statement)==SQLITE_DONE)
         {
            session->id=sqlite3_last_insert_rowid(database);
            session->epoch=1;
            ok=Audit("register",session->id);
         }
      }
      sqlite3_finalize(statement);
      if(!ok||!Execute("COMMIT"))
      {
         Execute("ROLLBACK");
         ok=0;
      }
   }
   pthread_mutex_unlock(&database_lock);
   sodium_memzero(hash,sizeof(hash));
   if(!ok)
      memset(session,0,sizeof(*session));
   return ok;
}

int AccountLogin(const char *name,const char *password,account_session *session)
{
   char hash[crypto_pwhash_STRBYTES];
   sqlite3_stmt *statement;
   int enabled=0,ok;
   account_session candidate={0,0};
   memset(session,0,sizeof(*session));
   if(!AccountNameValid(name)||!AccountPasswordValid(password))
      return 0;
   memcpy(hash,dummy_hash,sizeof(hash));
   pthread_mutex_lock(&database_lock);
   statement=Prepare("SELECT id,epoch,password_hash,disabled FROM accounts WHERE name=?");
   if(statement)
   {
      Text(statement,1,name);
      if(sqlite3_step(statement)==SQLITE_ROW)
      {
         const char *stored=(const char*)sqlite3_column_text(statement,2);
         if(stored&&strlen(stored)<sizeof(hash))
         {
            strcpy(hash,stored);
            candidate.id=sqlite3_column_int64(statement,0);
            candidate.epoch=sqlite3_column_int64(statement,1);
            enabled=sqlite3_column_int(statement,3)==0;
         }
      }
   }
   sqlite3_finalize(statement);
   pthread_mutex_unlock(&database_lock);
   ok=crypto_pwhash_str_verify(hash,password,(unsigned long long)strlen(password))==0;
   sodium_memzero(hash,sizeof(hash));
   ok=ok&&enabled&&candidate.id>0&&AccountSessionRole(candidate)!=ROLE_NONE;
   pthread_mutex_lock(&database_lock);
   if(!Audit(ok?"login":"login_failed",candidate.id))
      ok=0;
   pthread_mutex_unlock(&database_lock);
   if(ok)
      *session=candidate;
   return ok;
}

int AccountPasswordChange(account_session *session,const char *old_password,
   const char *new_password)
{
   char old_hash[crypto_pwhash_STRBYTES]={0},new_hash[crypto_pwhash_STRBYTES];
   sqlite3_stmt *statement;
   int ok=0;
   if(!AccountPasswordValid(old_password)||!AccountPasswordValid(new_password))
      return 0;
   pthread_mutex_lock(&database_lock);
   statement=Prepare("SELECT password_hash FROM accounts WHERE id=? AND epoch=? AND disabled=0");
   if(statement)
   {
      sqlite3_bind_int64(statement,1,session->id);
      sqlite3_bind_int64(statement,2,session->epoch);
      if(sqlite3_step(statement)==SQLITE_ROW)
      {
         const char *stored=(const char*)sqlite3_column_text(statement,0);
         if(stored&&strlen(stored)<sizeof(old_hash))
            strcpy(old_hash,stored);
      }
   }
   sqlite3_finalize(statement);
   pthread_mutex_unlock(&database_lock);
   if(!old_hash[0]||crypto_pwhash_str_verify(old_hash,old_password,
      (unsigned long long)strlen(old_password))!=0)
   {
      sodium_memzero(old_hash,sizeof(old_hash));
      return 0;
   }
   sodium_memzero(old_hash,sizeof(old_hash));
   if(crypto_pwhash_str_alg(new_hash,new_password,
      (unsigned long long)strlen(new_password),crypto_pwhash_OPSLIMIT_INTERACTIVE,
      crypto_pwhash_MEMLIMIT_INTERACTIVE,crypto_pwhash_ALG_ARGON2ID13)!=0)
      return 0;
   pthread_mutex_lock(&database_lock);
   if(Execute("BEGIN IMMEDIATE"))
   {
      statement=Prepare("UPDATE accounts SET password_hash=?,epoch=epoch+1 "
         "WHERE id=? AND epoch=? AND disabled=0");
      if(statement)
      {
         Text(statement,1,new_hash);
         sqlite3_bind_int64(statement,2,session->id);
         sqlite3_bind_int64(statement,3,session->epoch);
         ok=sqlite3_step(statement)==SQLITE_DONE&&sqlite3_changes(database)==1;
      }
      sqlite3_finalize(statement);
      ok=ok&&Audit("password_changed",session->id);
      if(!ok||!Execute("COMMIT"))
      {
         Execute("ROLLBACK");
         ok=0;
      }
   }
   pthread_mutex_unlock(&database_lock);
   sodium_memzero(new_hash,sizeof(new_hash));
   if(ok)
      session->epoch++;
   return ok;
}

/* These two functions are for the trusted local operator tool, not clients. */
static int LocalAccountChange(const char *name,const char *role,int disabled)
{
   sqlite3_stmt *statement;
   int64_t id=0;
   char event[64];
   int ok=0;
   if(!AccountNameValid(name))
      return 0;
   pthread_mutex_lock(&database_lock);
   if(Execute("BEGIN IMMEDIATE"))
   {
      statement=Prepare(role?
         "UPDATE accounts SET role=? WHERE name=? AND (role!='owner' OR ?='owner' "
         "OR (SELECT count(*) FROM accounts WHERE role='owner' AND disabled=0)>1)":
         "UPDATE accounts SET disabled=?,epoch=epoch+1 WHERE name=? AND "
         "(role!='owner' OR ?=0 OR "
         "(SELECT count(*) FROM accounts WHERE role='owner' AND disabled=0)>1)");
      if(statement)
      {
         if(role)
         {
            Text(statement,1,role);
            Text(statement,3,role);
         }
         else
         {
            sqlite3_bind_int(statement,1,disabled);
            sqlite3_bind_int(statement,3,disabled);
         }
         Text(statement,2,name);
         ok=sqlite3_step(statement)==SQLITE_DONE&&sqlite3_changes(database)==1;
      }
      sqlite3_finalize(statement);
      statement=Prepare("SELECT id FROM accounts WHERE name=?");
      if(statement)
      {
         Text(statement,1,name);
         if(sqlite3_step(statement)==SQLITE_ROW)
            id=sqlite3_column_int64(statement,0);
      }
      sqlite3_finalize(statement);
      snprintf(event,sizeof(event),"%s:%s",role?"local_role_changed":"local_access_changed",
         role?role:(disabled?"disabled":"enabled"));
      ok=ok&&id>0&&Audit(event,id);
      if(!ok||!Execute("COMMIT"))
      {
         Execute("ROLLBACK");
         ok=0;
      }
   }
   pthread_mutex_unlock(&database_lock);
   return ok;
}

int AccountSetRole(const char *name,account_role role)
{
   return role>=ROLE_PLAYER&&role<=ROLE_OWNER&&
      LocalAccountChange(name,AccountRoleName(role),0);
}

int AccountSetDisabled(const char *name,int disabled)
{
   return (disabled==0||disabled==1)&&LocalAccountChange(name,NULL,disabled);
}

static int CharacterNameValid(const char *name)
{
   size_t i,length=strlen(name);
   if(length<3||length>15)
      return 0;
   for(i=0;i<length;i++)
      if(!((name[i]>='a'&&name[i]<='z')||(name[i]>='A'&&name[i]<='Z')))
         return 0;
   return 1;
}

static int CanonicalCharacter(const char *name,char output[16])
{
   size_t i;
   if(!CharacterNameValid(name))
      return 0;
   for(i=0;name[i];i++)
      output[i]=(char)(name[i]>='A'&&name[i]<='Z'?name[i]-'A'+'a':name[i]);
   output[i]='\0';
   output[0]=(char)(output[0]-'a'+'A');
   return 1;
}

static int BindCharacter(int64_t id,const char *name,int reserve_existing)
{
   sqlite3_stmt *statement=Prepare("SELECT account_id FROM characters WHERE name=?");
   int step,ok=0;
   if(!statement)
      return 0;
   Text(statement,1,name);
   step=sqlite3_step(statement);
   if(step==SQLITE_ROW)
      ok=sqlite3_column_int64(statement,0)==id;
   sqlite3_finalize(statement);
   if(step!=SQLITE_DONE||reserve_existing)
      return ok;
   statement=Prepare("INSERT INTO characters(name,account_id) VALUES(?,?)");
   if(statement)
   {
      Text(statement,1,name);
      sqlite3_bind_int64(statement,2,id);
      ok=sqlite3_step(statement)==SQLITE_DONE&&Audit("character_linked",id);
   }
   sqlite3_finalize(statement);
   return ok;
}

int AccountCharacter(account_session session,const char *name,int existing_file)
{
   char canonical[16];
   struct stat info;
   sqlite3_stmt *statement;
   int ok=0;
   if(!CanonicalCharacter(name,canonical))
      return 0;
   name=canonical;
   if(lstat(name,&info)==0)
   {
      if(!S_ISREG(info.st_mode))
         return 0;
      existing_file=1;
   }
   else if(errno!=ENOENT)
      return 0;
   pthread_mutex_lock(&database_lock);
   if(Execute("BEGIN IMMEDIATE"))
   {
      statement=Prepare("SELECT id FROM accounts WHERE id=? AND epoch=? AND disabled=0");
      if(statement)
      {
         sqlite3_bind_int64(statement,1,session.id);
         sqlite3_bind_int64(statement,2,session.epoch);
         ok=sqlite3_step(statement)==SQLITE_ROW;
      }
      sqlite3_finalize(statement);
      ok=ok&&BindCharacter(session.id,name,existing_file);
      if(!ok||!Execute("COMMIT"))
      {
         Execute("ROLLBACK");
         ok=0;
      }
   }
   pthread_mutex_unlock(&database_lock);
   return ok;
}

int AccountLinkLegacy(const char *account,const char *character)
{
   char canonical[16];
   struct stat info;
   sqlite3_stmt *statement;
   int64_t id=0;
   int ok=0;
   if(!AccountNameValid(account)||!CanonicalCharacter(character,canonical))
      return 0;
   character=canonical;
   if(lstat(character,&info)!=0||!S_ISREG(info.st_mode))
      return 0;
   pthread_mutex_lock(&database_lock);
   if(Execute("BEGIN IMMEDIATE"))
   {
      statement=Prepare("SELECT id FROM accounts WHERE name=? AND disabled=0");
      if(statement)
      {
         Text(statement,1,account);
         if(sqlite3_step(statement)==SQLITE_ROW)
            id=sqlite3_column_int64(statement,0);
      }
      sqlite3_finalize(statement);
      ok=id>0&&BindCharacter(id,character,0)&&Audit("local_legacy_linked",id);
      if(!ok||!Execute("COMMIT"))
      {
         Execute("ROLLBACK");
         ok=0;
      }
   }
   pthread_mutex_unlock(&database_lock);
   return ok;
}
