#include "accounts.h"
#include <stdio.h>
#include <string.h>

int main(int argc,char **argv)
{
   int ok=0;
   if(argc<3||argc>4)
   {
      fprintf(stderr,"Usage (run in the game's private data directory):\n"
         "  glad-admin role ACCOUNT player|builder|moderator|administrator|owner\n"
         "  glad-admin link ACCOUNT Character\n"
         "  glad-admin disable ACCOUNT\n  glad-admin enable ACCOUNT\n");
      return 1;
   }
   if(!AccountsOpen("accounts.sqlite3"))
   {
      fprintf(stderr,"Cannot open private account database; no change made.\n");
      return 1;
   }
   if(argc==4&&strcmp(argv[1],"role")==0)
      ok=AccountSetRole(argv[2],AccountRoleParse(argv[3]));
   else if(argc==4&&strcmp(argv[1],"link")==0)
      ok=AccountLinkLegacy(argv[2],argv[3]);
   else if(argc==3&&strcmp(argv[1],"disable")==0)
      ok=AccountSetDisabled(argv[2],1);
   else if(argc==3&&strcmp(argv[1],"enable")==0)
      ok=AccountSetDisabled(argv[2],0);
   AccountsClose();
   if(!ok)
      fprintf(stderr,"Change rejected: check names, ownership, role, database access "
         "and last-owner protection. No character file was changed.\n");
   else
      puts("Account metadata updated. No character file was changed.");
   return ok?0:1;
}
