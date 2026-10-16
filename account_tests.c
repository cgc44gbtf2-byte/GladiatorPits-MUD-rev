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

static void StaffTests(account_session builder,account_session owner)
{
   account_session player,moderator;
   staff_account rows[50],self;
   staff_character characters[50];
   staff_audit history[50];
   sqlite3 *fixture=NULL;
   AccountsClose();
   Check(sqlite3_open("test-accounts.sqlite3",&fixture)==SQLITE_OK,
      "version-one migration fixture opens");
   Check(sqlite3_exec(fixture,"DROP TABLE staff_audit; PRAGMA user_version=1;",
      NULL,NULL,NULL)==SQLITE_OK,"simulate old schema without changing account records");
   sqlite3_close(fixture);
   Check(AccountsOpen("test-accounts.sqlite3")&&Scalar("PRAGMA user_version")==2,
      "version-one database upgrades additively");
   Check(AccountSessionRole(builder)==ROLE_BUILDER&&
      AccountCharacter(builder,"Newhero",0),"upgrade preserves roles/epochs/ownership");
   Check(AccountRegister("Stafftarget",test_password,&player),"staff target fixture");
   Check(AccountRegister("Modstaff",test_password,&moderator)&&
      AccountSetRole("Modstaff",ROLE_MODERATOR),"moderator fixture");
   Check(AccountStaffSelf(builder,&self)&&self.id==builder.id,
      "builder can inspect own staff session");
   Check(!AccountStaffSelf(player,&self),"player cannot enter staff dashboard");
   Check(AccountStaffList(player,0,rows,50)==-1&&
      AccountStaffList(builder,0,rows,50)==-1,"player/builder cannot list private accounts");
   Check(AccountStaffList(moderator,0,rows,50)>=4,"moderator account listing");
   Check(AccountStaffFind(moderator,"stafftarget",&self)==1&&self.id==player.id,
      "staff target lookup uses exact case-insensitive bound name");
   Check(AccountStaffFind(builder,"Stafftarget",&self)==-1&&
      AccountStaffFind(player,"Stafftarget",&self)==-1,"unprivileged staff target lookup denied");
   Check(AccountStaffFind(owner,"Unknownfixture",&self)==0&&
      AccountStaffFind(owner,"../accounts",&self)==-1,"staff lookup missing/invalid names fail safely");
   Check(AccountStaffList(owner,0,rows,0)==-1&&
      AccountStaffList(owner,-1,rows,1)==-1&&AccountStaffList(owner,0,rows,51)==-1,
      "staff list bounds enforced");
   Check(AccountCharacter(player,"Staffhero",0),"character inspection ownership fixture");
   Check(AccountStaffCharacters(moderator,"",characters,50)>=3,
      "moderator can list character metadata");
   Check(AccountStaffCharacter(moderator,"staffhero",characters)==1&&
      characters[0].account_id==player.id,"character lookup is permission checked");
   Check(AccountStaffCharacters(builder,"",characters,50)==-1,
      "builder cannot inspect private character metadata");
   Check(AccountStaffRole(moderator,player.id,ROLE_OWNER)==0,
      "moderator cannot grant roles by calling backend directly");
   Check(AccountStaffAccess(moderator,owner.id,1)==0,"moderator cannot disable staff");
   Check(!AccountStaffAccess(moderator,player.id,0)&&
      AccountSessionRole(player)==ROLE_PLAYER,"no-op access change does not revoke sessions");
   Check(AccountStaffAccess(moderator,player.id,1)&&
      AccountSessionRole(player)==ROLE_NONE,"moderator can disable regular player");
   Check(AccountStaffAccess(moderator,player.id,0),"moderator can enable regular player");
   Check(AccountSetRole("Alice",ROLE_ADMINISTRATOR),"administrator fixture");
   Check(!AccountStaffRole(builder,player.id,ROLE_OWNER)&&
      !AccountStaffAccess(builder,moderator.id,1),"administrator cannot grant roles/disable staff");
   Check(AccountStaffRole(owner,builder.id,ROLE_OWNER),"owner can grant staff role");
   Check(!AccountStaffRole(owner,owner.id,ROLE_PLAYER)&&
      !AccountStaffAccess(owner,owner.id,1),"remote self-modification forbidden");
   Check(AccountStaffRole(owner,builder.id,ROLE_BUILDER),"owner can revoke role");
   Check(!AccountStaffRole(builder,player.id,ROLE_OWNER),
      "revoked owner loses mutation authority immediately");
   Check(AccountStaffAudit(moderator,0,history,50)==-1,
      "staff role/access audit limited to owner");
   Check(AccountStaffAudit(owner,0,history,50)==4&&
      history[0].actor_id==owner.id&&history[0].target_id==builder.id&&
      strcmp(history[0].before,"owner")==0&&strcmp(history[0].after,"builder")==0,
      "successful staff writes record actor, target and before/after atomically");
   Check(sqlite3_open("test-accounts.sqlite3",&fixture)==SQLITE_OK&&
      sqlite3_exec(fixture,"CREATE TRIGGER deny_staff_log BEFORE INSERT ON staff_audit "
         "BEGIN SELECT RAISE(ABORT,'fixture'); END;",NULL,NULL,NULL)==SQLITE_OK,
      "staff audit-failure fixture");
   Check(!AccountStaffRole(owner,player.id,ROLE_BUILDER)&&
      Scalar("SELECT role='player' FROM accounts WHERE name='Stafftarget'")==1&&
      Scalar("SELECT count(*) FROM staff_audit")==4,
      "audit insertion failure rolls back role mutation and history");
   Check(sqlite3_exec(fixture,"DROP TRIGGER deny_staff_log;",NULL,NULL,NULL)==SQLITE_OK,
      "staff audit-failure fixture cleanup");
   sqlite3_close(fixture);
   Check(AccountSetRole("Bob",ROLE_BUILDER)==0,"last active owner still protected locally");
   Check(AccountSetDisabled("Modstaff",1)&&
      AccountStaffList(moderator,0,rows,50)==-1,"disabled staff session cannot read data");
}

static void DisabledOwnerCleanup(void)
{
   account_session disabled,enabled;
   int before=Scalar("SELECT count(*) FROM audit WHERE event LIKE 'local_role_changed:%'");
   Check(AccountRegister("Disabledowner",test_password,&disabled)&&
      AccountSetRole("Disabledowner",ROLE_OWNER),"disabled owner cleanup fixture");
   Check(AccountSetDisabled("Disabledowner",1)&&
      AccountSessionRole(disabled)==ROLE_NONE,"disabled owner session revoked");
   Check(!AccountSetRole("Bob",ROLE_PLAYER)&&!AccountSetDisabled("Bob",1),
      "disabled owner does not count toward last-active-owner protection");
   Check(AccountSetRole("Disabledowner",ROLE_PLAYER),
      "local operator can demote an already-disabled owner");
   Check(Scalar("SELECT role='player' AND disabled=1 FROM accounts "
      "WHERE name='Disabledowner'")==1&&
      Scalar("SELECT count(*) FROM accounts WHERE role='owner' AND disabled=0")==1,
      "disabled owner cleanup leaves access disabled and the active owner intact");
   Check(Scalar("SELECT count(*) FROM audit WHERE event LIKE 'local_role_changed:%'")==before+2,
      "owner assignment and disabled-owner demotion are both audited");
   Check(AccountSetDisabled("Disabledowner",0)&&
      AccountSessionRole(disabled)==ROLE_NONE,"cleanup/re-enable cannot revive an old session");
   Check(AccountLogin("Disabledowner",test_password,&enabled)&&
      AccountSessionRole(enabled)==ROLE_PLAYER,"fresh login receives only the cleaned-up role");
   Check(!AccountSetRole("Bob",ROLE_PLAYER)&&!AccountSetDisabled("Bob",1),
      "last active owner stays protected after cleanup and re-enable");
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
   StaffTests(login,bob);
   DisabledOwnerCleanup();
   AccountsClose();
   Check(chmod("test-accounts.sqlite3",0644)==0,"insecure-mode fixture");
   Check(!AccountsOpen("test-accounts.sqlite3"),"publicly readable database rejected");
   Check(chmod("test-accounts.sqlite3",0600)==0,"private mode restored");
   unlink("Legacyhero");
   unlink("test-accounts.sqlite3");
   printf("Passed %d account/security regression checks.\n",checks);
   return 0;
}
