/* Included by glad.c. Only the game thread reads live connections or applies
 * staff changes; the auth worker verifies passwords but never edits accounts. */

static int StaffReadsPlayers(conn_t *connection)
{
   return ConnectionCan(connection,PERM_PLAYER_MODERATE)||
      ConnectionCan(connection,PERM_PLAYER_EDIT)||ConnectionCan(connection,PERM_ROLES_MANAGE);
}

static void StaffDenied(conn_t *connection)
{
   PutOutput(connection,TO_USER,"Permission denied or account storage unavailable.\n\r");
}

static int StaffCursor(const char *text,int64_t *value)
{
   const char *p=text;
   long long parsed;
   if(!*p)
   {
      *value=0;
      return 1;
   }
   for(;*p;p++)
      if(*p<'0'||*p>'9')
         return 0;
   errno=0;
   parsed=strtoll(text,NULL,10);
   if(errno||parsed<0||strlen(text)>19)
      return 0;
   *value=(int64_t)parsed;
   return 1;
}

static void StaffApply(conn_t *connection,int64_t target,int operation,int value)
{
   int ok=0;
   char probe;
   ssize_t available=recv(connection->iSocket,&probe,1,MSG_PEEK|MSG_DONTWAIT);
   if(available==0||(available<0&&errno!=EAGAIN&&errno!=EWOULDBLOCK))
   {
      connection->bClosing=TRUE;
      return;
   }
   if(operation==1)
      ok=AccountStaffRole(connection->account,target,(account_role)value);
   else if(operation==2)
      ok=AccountStaffAccess(connection->account,target,value);
   if(ok)
      PutOutput(connection,TO_USER,"Staff change applied and audited.\n\r");
   else
      PutOutput(connection,TO_USER,"Staff change denied or unchanged; no change applied.\n\r");
}

static void StaffCharacter(conn_t *connection,const char *name)
{
   staff_character owned;
   saved_character saved;
   conn_t *live;
   int status=AccountStaffCharacter(connection->account,name,&owned);
   if(status<0)
   {
      StaffDenied(connection);
      return;
   }
   if(status==0)
   {
      PutOutput(connection,TO_USER,"No linked character found.\n\r");
      return;
   }
   PutOutput(connection,TO_USER,"Character: %s | Account: %s (#%lld)\n\r",
      owned.name,owned.account_name,(long long)owned.account_id);
   live=FindPerson(owned.name);
   if(live&&live->pstBody&&live->account.id==owned.account_id&&!live->bClosing)
   {
      body_t *body=live->pstBody;
      PutOutput(connection,TO_USER,"LIVE read-only snapshot | Title: %s\n\r",StatusName(body));
      memcpy(saved.stats,body->iStats,sizeof(saved.stats));
      saved.speed=body->iSpeed;
      saved.room=body->iRoom;
      saved.wins=body->iWin;
      saved.losses=body->iLoss;
      saved.kills=body->iKill;
      saved.damage=body->iDam;
      status=1;
   }
   else
   {
      status=CharacterInspect(owned.name,&saved);
      PutOutput(connection,TO_USER,"SAVED read-only snapshot (not live game state).\n\r");
   }
   if(status==1)
      PutOutput(connection,TO_USER,
         "STR:%d DEX:%d STA:%d SIZ:%d WIT:%d\n\r"
         "Wins:%d Losses:%d Kills:%d Damage:%d Room:%d Speed counter:%d\n\r",
         saved.stats[0],saved.stats[1],saved.stats[2],saved.stats[3],saved.stats[4],
         saved.wins,saved.losses,saved.kills,saved.damage,saved.room,saved.speed);
   else
      PutOutput(connection,TO_USER,status==0?
         "Owned name has no saved record (unfinished/dead characters stay reserved).\n\r":
         "Record invalid, incompatible or unreadable; file retained unchanged.\n\r");
}

static void StaffStartChange(conn_t *connection,const char *action,
   const char *account,const char *argument)
{
   staff_account target;
   account_role role;
   int operation,value;
   if(AccountStaffFind(connection->account,account,&target)!=1)
   {
      StaffDenied(connection);
      return;
   }
   if(!strcmp(action,"role"))
   {
      role=AccountRoleParse(argument);
      if(!ConnectionCan(connection,PERM_ROLES_MANAGE)||role==ROLE_NONE)
      {
         StaffDenied(connection);
         return;
      }
      operation=1;
      value=(int)role;
      if(role==target.role)
      {
         PutOutput(connection,TO_USER,"Role already assigned; no change needed.\n\r");
         return;
      }
      PutOutput(connection,TO_USER,"Pending role: %s %s -> %s.\n\r",target.name,
         AccountRoleName(target.role),AccountRoleName(role));
   }
   else
   {
      operation=2;
      value=!strcmp(action,"disable");
      if((target.role!=ROLE_PLAYER&&!ConnectionCan(connection,PERM_ROLES_MANAGE))||
         target.disabled==value)
      {
         StaffDenied(connection);
         return;
      }
      PutOutput(connection,TO_USER,"Pending access: %s -> %s. Sessions will be revoked.\n\r",
         target.name,value?"disabled":"enabled");
      PutOutput(connection,TO_USER,"A fighting player's disconnect follows the existing surrender rule.\n\r");
   }
   if(target.id==connection->account.id)
   {
      PutOutput(connection,TO_USER,"Self role/access changes are not allowed.\n\r");
      return;
   }
   connection->staff_target=target.id;
   connection->staff_operation=operation;
   connection->staff_value=value;
   connection->auth_stage=AUTH_STAFF_CONFIRM;
   connection->auth_deadline=AuthClock()+120;
   PutOutput(connection,TO_USER,"Type CONFIRM to continue, or anything else to cancel: ");
}

CMD(Staff)
{
   char action[16]="",argument[33]="",value[20]="",extra[2]="";
   int words=sscanf(szTxt,"%15s %32s %19s %1s",action,argument,value,extra);
   int64_t cursor=0;
   int count,i;
   conn_t *peer;
   if(!ConnectionCan(pstConn,PERM_DASHBOARD))
   {
      StaffDenied(pstConn);
      return;
   }
   if(words<=0)
   {
      PutOutput(pstConn,TO_USER,
         "\n\r+------------ GLADIATOR PITS STAFF DASHBOARD ------------+\n\r"
         "Account: %s | Role: %s\n\r"
         "staff world                 World-system inventory\n\r",
         pstConn->account_name,AccountRoleName(AccountSessionRole(pstConn->account)));
      if(StaffReadsPlayers(pstConn))
         PutOutput(pstConn,TO_USER,
            "staff accounts [after-id]    Account/access directory\n\r"
            "staff characters [after]    Owned character names\n\r"
            "staff online                Connected players (first 10)\n\r"
            "staff inspect Character     Live or saved read-only details\n\r"
            "staff disable Account       Disable regular Player access\n\r"
            "staff enable Account        Enable regular Player access\n\r");
      if(ConnectionCan(pstConn,PERM_ROLES_MANAGE))
         PutOutput(pstConn,TO_USER,
            "staff role Account Role     Assign player/builder/moderator/\n\r"
            "                            administrator/owner; also manage staff access\n\r"
            "staff audit [before-id]     Staff role/access change history\n\r");
      PutOutput(pstConn,TO_USER,
         "Changes require CONFIRM and a separate password prompt.\n\r"
         "World/player editing is not implemented. No web console.\n\r"
         "+-------------------------------------------------------+\n\r");
      return;
   }
   if(!strcmp(action,"world")&&words==1)
   {
      count=0;
      for(peer=pstFirstConn;peer;peer=peer->pstNext)
         if(peer->eStatus==FIGHTING&&!peer->bClosing)
            count++;
      PutOutput(pstConn,TO_USER,
         "WORLD SYSTEMS (read-only)\n\rStadium: room 0\n\rTraining room: -1\n\r"
         "Combat arenas: allocated during fights; fighting connections: %d\n\r"
         "No persistent area/item/mob definitions or world editor exist.\n\r",count);
      return;
   }
   if(!strcmp(action,"audit")&&words<=2&&StaffCursor(argument,&cursor))
   {
      staff_audit rows[10];
      count=AccountStaffAudit(pstConn->account,cursor,rows,10);
      if(count<0) {StaffDenied(pstConn);return;}
      PutOutput(pstConn,TO_USER,"STAFF CHANGE AUDIT (UTC; local operator/auth events are separate)\n\r");
      for(i=0;i<count;i++)
         PutOutput(pstConn,TO_USER,"#%lld %s | #%lld -> #%lld %s %s -> %s\n\r",
            (long long)rows[i].id,rows[i].created_at,(long long)rows[i].actor_id,
            (long long)rows[i].target_id,rows[i].action,rows[i].before,rows[i].after);
      if(count==10)
         PutOutput(pstConn,TO_USER,"Older: staff audit %lld\n\r",(long long)rows[count-1].id);
      else PutOutput(pstConn,TO_USER,"End of staff audit.\n\r");
      return;
   }
   if(!StaffReadsPlayers(pstConn))
   {
      StaffDenied(pstConn);
      return;
   }
   if(!strcmp(action,"accounts")&&words<=2&&StaffCursor(argument,&cursor))
   {
      staff_account rows[10];
      count=AccountStaffList(pstConn->account,cursor,rows,10);
      if(count<0) {StaffDenied(pstConn);return;}
      PutOutput(pstConn,TO_USER,"ACCOUNT DIRECTORY\n\r");
      for(i=0;i<count;i++)
         PutOutput(pstConn,TO_USER,"#%lld %-32s %-13s %-8s owned names:%lld\n\r",
            (long long)rows[i].id,rows[i].name,AccountRoleName(rows[i].role),
            rows[i].disabled?"disabled":"enabled",(long long)rows[i].characters);
      if(count==10)
         PutOutput(pstConn,TO_USER,"Next: staff accounts %lld\n\r",(long long)rows[count-1].id);
      else PutOutput(pstConn,TO_USER,"End of account directory.\n\r");
   }
   else if(!strcmp(action,"characters")&&words<=2)
   {
      staff_character rows[10];
      count=AccountStaffCharacters(pstConn->account,argument,rows,10);
      if(count<0) {StaffDenied(pstConn);return;}
      PutOutput(pstConn,TO_USER,"LINKED CHARACTER NAMES (not all have saves)\n\r");
      for(i=0;i<count;i++)
         PutOutput(pstConn,TO_USER,"%-15s -> %s (#%lld)\n\r",rows[i].name,
            rows[i].account_name,(long long)rows[i].account_id);
      if(count==10)
         PutOutput(pstConn,TO_USER,"Next: staff characters %s\n\r",rows[count-1].name);
      else PutOutput(pstConn,TO_USER,"End of linked names.\n\r");
   }
   else if(!strcmp(action,"online")&&words==1)
   {
      PutOutput(pstConn,TO_USER,"CONNECTED PLAYERS (first 10; no IPs or credentials)\n\r");
      for(peer=pstFirstConn,count=0;peer&&count<10;peer=peer->pstNext)
         if(!peer->bClosing&&peer->account.id)
         {
            PutOutput(pstConn,TO_USER,"%s | %s | %s | room %d\n\r",peer->account_name,
               AccountRoleName(AccountSessionRole(peer->account)),NAME(peer),ROOM(peer));
            count++;
         }
      PutOutput(pstConn,TO_USER,"End of connected-player preview.\n\r");
   }
   else if(!strcmp(action,"inspect")&&words==2)
      StaffCharacter(pstConn,argument);
   else if((!strcmp(action,"role")&&words==3)||
      ((!strcmp(action,"enable")||!strcmp(action,"disable"))&&words==2))
      StaffStartChange(pstConn,action,argument,value);
   else
      PutOutput(pstConn,TO_USER,"Invalid staff syntax. Type staff for the dashboard.\n\r");
}
