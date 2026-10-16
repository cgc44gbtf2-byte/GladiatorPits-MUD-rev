#define _POSIX_C_SOURCE 200809L
#include "accounts.h"
#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int checks;
static const char *test_password="account-test-password-only";
static const char *replacement="replacement-test-password-only";

static void Check(int condition,const char *description)
{
   if(!condition)
   {
      fprintf(stderr,"FAIL: %s\n",description);
      exit(1);
   }
   checks++;
}

static int Scalar(const char *sql)
{
   sqlite3 *db=NULL;
   sqlite3_stmt *statement=NULL;
   int value=-1;
   if(sqlite3_open("test-accounts.sqlite3",&db)==SQLITE_OK&&
      sqlite3_prepare_v2(db,sql,-1,&statement,NULL)==SQLITE_OK&&
      sqlite3_step(statement)==SQLITE_ROW)
      value=sqlite3_column_int(statement,0);
   sqlite3_finalize(statement);
   sqlite3_close(db);
   return value;
}

int main(void)
{
   account_session alice,bob,login,old;
   struct stat info;
   FILE *legacy;
   int role,permission;
   const unsigned int expected[]={1U,7U,11U,63U,127U};
   unlink("test-accounts.sqlite3");
   Check(AccountsOpen("test-accounts.sqlite3"),"private database opens");
   Check(stat("test-accounts.sqlite3",&info)==0&&(info.st_mode&0077)==0,
      "database is private");
   Check(!AccountNameValid("../../escape")&&!AccountNameValid("ab")&&
      !AccountNameValid("2player")&&AccountNameValid("Player_2"),
      "account names have strict bounds and safe characters");
   Check(!AccountPasswordValid("too-short"),"short passwords rejected");
   Check(AccountRegister("Alice",test_password,&alice),"account registration");
   Check(AccountSessionRole(alice)==ROLE_PLAYER,"first account is never staff");
   Check(!AccountRegister("ALICE",replacement,&login),"case-insensitive duplicate rejected");
   Check(!AccountRegister("bad'name",test_password,&login),"injection-shaped name rejected");
   Check(AccountRegister("Bob",test_password,&bob),"second account registration");
   Check(Scalar("SELECT count(*) FROM accounts WHERE password_hash LIKE '$argon2id$%'")==2,
      "only Argon2id hashes stored");
   Check(Scalar("SELECT count(DISTINCT password_hash) FROM accounts")==2,
      "same passwords have distinct random salts");
   Check(!AccountLogin("alice","incorrect-test-password",&login)&&login.id==0,
      "wrong password does not create session");
   Check(!AccountLogin("Missing",test_password,&login),"unknown account fails");
   Check(AccountLogin("ALICE",test_password,&login)&&login.id==alice.id,
      "login is case-insensitive");
   for(role=(int)ROLE_PLAYER;role<=(int)ROLE_OWNER;role++)
      for(permission=1;permission<=64;permission*=2)
         Check(AccountRoleCan((account_role)role,(account_permission)permission)==
            ((expected[role]&(unsigned int)permission)!=0),
            "explicit role/capability matrix");
   Check(!AccountRoleCan(ROLE_NONE,PERM_PLAY)&&
      !AccountRoleCan(ROLE_BUILDER,PERM_SHUTDOWN),"no implicit staff privilege inheritance");
   Check(!AccountRoleCan(ROLE_OWNER,(account_permission)128),"unknown capability denied");
   Check(AccountCharacter(alice,"Newhero",0),"new character binds to account");
   Check(!AccountCharacter(bob,"newhero",0),"other account cannot take character");
   Check(!AccountCharacter(alice,"Legacyhero",1),"unlinked legacy character is reserved");
   legacy=fopen("Legacyhero","wb");
   Check(legacy!=NULL,"legacy fixture created");
   Check(fwrite("untouched",9,1,legacy)==1,"legacy fixture written");
   fclose(legacy);
   Check(!AccountCharacter(bob,"legacyhero",0),
      "store independently reserves existing files regardless of caller hint/case");
   Check(symlink("Legacyhero","Linkhero")==0,"symlink ownership fixture");
   Check(!AccountLinkLegacy("Alice","Linkhero")&&!AccountCharacter(alice,"Linkhero",0),
      "symlink character files cannot be linked or claimed");
   unlink("Linkhero");
   Check(AccountLinkLegacy("Alice","Legacyhero"),"local operator can link legacy character");
   Check(AccountCharacter(alice,"Legacyhero",1),"linked legacy character accessible");
   Check(!AccountLinkLegacy("Bob","Legacyhero"),"local link cannot silently transfer ownership");
   Check(stat("Legacyhero",&info)==0&&info.st_size==9,"linking leaves character bytes untouched");
   Check(!AccountCharacter(alice,"../escape",0),"unsafe character paths rejected");
   Check(AccountSetRole("Alice",ROLE_OWNER),"owner bootstrap is explicit local operation");
   Check(!AccountSetRole("Alice",ROLE_PLAYER)&&!AccountSetDisabled("Alice",1),
      "last active owner cannot be removed");
   Check(AccountSetRole("Bob",ROLE_OWNER)&&AccountSetRole("Alice",ROLE_BUILDER),
      "multiple-owner staff changes allowed");
   Check(AccountSessionRole(alice)==ROLE_BUILDER,"role changes visible to existing sessions");
   old=alice;
   Check(!AccountPasswordChange(&alice,"incorrect-test-password",replacement),
      "password change requires current password");
   Check(AccountPasswordChange(&alice,test_password,replacement),"password change");
   Check(AccountSessionRole(old)==ROLE_NONE&&!AccountCharacter(old,"Newhero",0),
      "password change revokes previous sessions and ownership access");
   Check(AccountSessionRole(alice)==ROLE_BUILDER,"changing session receives new epoch");
   Check(!AccountLogin("Alice",test_password,&login),"old password rejected");
   Check(AccountLogin("Alice",replacement,&login),"new password accepted");
   Check(AccountSetDisabled("Alice",1)&&AccountSessionRole(alice)==ROLE_NONE,
      "disabled account sessions revoked");
   Check(!AccountLogin("Alice",replacement,&login),"disabled account login rejected");
   Check(AccountSetDisabled("Alice",0),"local enable");
   Check(AccountSessionRole(alice)==ROLE_NONE,"re-enable does not resurrect old sessions");
   AccountsClose();
   Check(AccountsOpen("test-accounts.sqlite3"),"database reopens after restart");
   Check(AccountLogin("Alice",replacement,&login)&&
      AccountSessionRole(login)==ROLE_BUILDER&&AccountCharacter(login,"Newhero",0),
      "password, role and character ownership persist");
   Check(Scalar("SELECT count(*) FROM audit WHERE event='password_changed'")==1,
      "security changes have audit records");
   AccountsClose();
   Check(chmod("test-accounts.sqlite3",0644)==0,"insecure-mode fixture");
   Check(!AccountsOpen("test-accounts.sqlite3"),"publicly readable database rejected");
   Check(chmod("test-accounts.sqlite3",0600)==0,"private mode restored");
   unlink("Legacyhero");
   unlink("test-accounts.sqlite3");
   printf("Passed %d account/security regression checks.\n",checks);
   return 0;
}
