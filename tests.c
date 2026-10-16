/*
 * Small regression tests for the existing Gladiator Pits rules.
 * Run in an isolated directory because the game stores player files there.
 */
static int RegressionD10(void);
#define D10 RegressionD10()
#define main glad_server_main
#include "glad.c"
#undef main
#include <stddef.h>
#include <limits.h>

static conn_t test_conn_one;
static conn_t test_conn_two;
static body_t test_body_one;
static body_t test_body_two;
static int tests_run;
static account_session regression_account;
static int fixed_d10,combat_rolls;
static int OutputContains(conn_t *pstConn,const char *szText);

static int RegressionD10(void)
{
   if(fixed_d10)
   {
      combat_rolls++;
      return fixed_d10;
   }
   return RollD10();
}

static void Check(int condition, const char *message)
{
   if (!condition)
   {
      fprintf(stderr, "FAIL: %s\n", message);
      exit(EXIT_FAILURE);
   }
   tests_run++;
}

static void ResetWorld(void)
{
   memset(&test_conn_one, 0, sizeof(test_conn_one));
   memset(&test_conn_two, 0, sizeof(test_conn_two));
   memset(&test_body_one, 0, sizeof(test_body_one));
   memset(&test_body_two, 0, sizeof(test_body_two));

   strcpy(test_body_one.a_chName, "Attacker");
   strcpy(test_body_two.a_chName, "Defender");
   test_conn_one.pstBody = &test_body_one;
   test_conn_one.eStatus = GLADIATOR;
   test_conn_one.account = regression_account;
   test_conn_one.pstNext = &test_conn_two;
   test_conn_two.pstPrev = &test_conn_one;
   test_conn_two.pstBody = &test_body_two;
   test_conn_two.eStatus = GLADIATOR;
   test_conn_two.account = regression_account;
   pstFirstConn = &test_conn_one;
   FD_ZERO(&a_sFd);
}

static void SetInput(conn_t *pstConn, const char *szInput)
{
   size_t iLen = strlen(szInput);
   Check(iLen < MAX_BUF, "test command fits input buffer");
   memcpy(pstConn->a_chInBuf, szInput, iLen + 1);
   ParseInput(pstConn);
}

static int StatTotal(const body_t *pstBody)
{
   int i;
   int iTotal = 0;

   for (i = STR; i <= WIT; i++)
      iTotal += pstBody->iStats[i];
   return iTotal;
}

static conn_t *HeapCombatPair(void)
{
   conn_t *first=calloc(1,sizeof(*first)),*second=calloc(1,sizeof(*second));
   int i;
   Check(first&&second,"combat lifecycle connection allocations");
   first->pstBody=calloc(1,sizeof(*first->pstBody));
   second->pstBody=calloc(1,sizeof(*second->pstBody));
   Check(first->pstBody&&second->pstBody,"combat lifecycle body allocations");
   strcpy(first->pstBody->a_chName,"Attacker");
   strcpy(second->pstBody->a_chName,"Defender");
   for(i=STR;i<=WIT;i++)
      first->pstBody->iStats[i]=second->pstBody->iStats[i]=1;
   first->iSocket=second->iSocket=-1;
   first->account=second->account=regression_account;
   first->eStatus=second->eStatus=GLADIATOR;
   first->pstNext=second;
   second->pstPrev=first;
   pstFirstConn=first;
   FD_ZERO(&a_sFd);
   return first;
}

static void TestCombatPendingDisconnect(void)
{
   int variant,i;
   saved_character saved;
   for(variant=0;variant<6;variant++)
   {
      conn_t *first=HeapCombatPair(),*second=first->pstNext;
      conn_t *departing=(variant&1)?second:first;
      conn_t *remaining=departing==first?second:first;
      char departed_name[16];
      strcpy(departed_name,departing->pstBody->a_chName);
      for(i=STR;i<=WIT;i++)
         first->pstBody->iStats[i]=9;
      Save(first->pstBody);
      Save(second->pstBody);
      SetInput(first,"challenge Defender");
      SetInput(second,"accept");
      if(variant<2)
         departing->bClosing=TRUE;
      else if(variant<4)
         departing->bWriteFailed=TRUE;
      else
      {
         /* Trigger a real failed FlushOutput during attack-message enqueue. */
         memset(departing->a_chOutBuf,'x',MAX_BUF-2);
         departing->iOutLen=MAX_BUF-2;
      }
      Update();
      Check(first->pstBody&&second->pstBody&&first->pstBody->iDam==0&&
         second->pstBody->iDam==0&&first->pstBody->iWin==0&&
         first->pstBody->iKill==0&&second->pstBody->iWin==0,
         "pending disconnect cannot inflict a lethal hit before surrender cleanup");
      Check(CharacterInspect("Defender",&saved)==1&&saved.damage==0,
         "pending disconnect never deletes the opponent's persisted record");
      if(variant>=4)
         Check(departing->bWriteFailed!=FALSE,"attack-message failure is detected before committing damage");
      CloseConnection(departing);
      Check(pstFirstConn==remaining&&remaining->pstBody->iWin==1&&
         remaining->pstBody->iKill==0&&remaining->pstBody->iRoom==0&&
         remaining->pstBody->pstOpponent==NULL&&remaining->eStatus==GLADIATOR,
         "closing either fighter awards one surrender win, no kill, and clears links");
      Check(CharacterInspect(departed_name,&saved)==1&&saved.losses==1,
         "departing fighter retains a valid record with one surrender loss");
      CloseConnection(remaining);
      Check(pstFirstConn==NULL,"combat disconnect fixtures release every connection");
      unlink("Attacker");
      unlink("Defender");
   }
}

static void FinishHeapCombat(void)
{
   while(pstFirstConn)
      CloseConnection(pstFirstConn);
   unlink("Attacker");
   unlink("Defender");
   fixed_d10=0;
}

static void TestChallengeDisconnectAdmission(void)
{
   int accepting,variant;
   for(accepting=1;accepting>=0;accepting--)
      for(variant=0;variant<(accepting?8:6);variant++)
      {
         conn_t *first=HeapCombatPair(),*second=first->pstNext;
         conn_t *actor=accepting?second:first,*peer=accepting?first:second;
         conn_t *departing=(variant&1)?peer:actor;
         conn_t *remaining=departing==first?second:first;
         saved_character saved;
         int unchanged;
         first->pstBody->iDam=2;
         second->pstBody->iDam=3;
         first->pstBody->iSpeed=4;
         second->pstBody->iSpeed=5;
         Save(first->pstBody);
         Save(second->pstBody);
         if(accepting)
            SetInput(first,"challenge Defender");
         first->iOutLen=second->iOutLen=0;
         if(variant<2)
            departing->bClosing=TRUE;
         else if(variant<4)
            departing->bWriteFailed=TRUE;
         else
         {
            /* The final two acceptance cases fail only on the broadcast,
             * after the individual message has filled the queue exactly. */
            size_t length=variant<6?1:
               strlen(departing==actor?"Ok.\n\r":"Defender accepts your challenge!\n\r");
            departing->iOutLen=MAX_BUF-1-(int)length;
            memset(departing->a_chOutBuf,'x',(size_t)departing->iOutLen);
         }
         SetInput(actor,accepting?"accept":"challenge Defender");
         unchanged=first->pstBody->iDam==2&&second->pstBody->iDam==3&&
            first->pstBody->iSpeed==4&&second->pstBody->iSpeed==5&&
            first->pstBody->iRoom==0&&second->pstBody->iRoom==0&&
            first->eStatus==(accepting?CHALLENGER:GLADIATOR)&&
            second->eStatus==GLADIATOR&&
            first->pstBody->pstOpponent==(accepting?second:NULL)&&
            second->pstBody->pstOpponent==(accepting?first:NULL);
         if(variant>=4)
            Check(departing->bWriteFailed!=FALSE,
               "challenge admission fixture discovers an actual output failure");
         CloseConnection(departing);
         Check(remaining->pstBody->iWin==0&&remaining->pstBody->iLoss==0&&
            remaining->pstBody->iKill==0,
            "late challenge/accept cannot fabricate a surrender win or loss");
         Check(unchanged,"failed challenge admission leaves health, timers, rooms and pending state unchanged");
         Check(remaining->eStatus==GLADIATOR&&remaining->pstBody->pstOpponent==NULL&&
            pstFirstConn==remaining&&remaining->pstPrev==NULL&&remaining->pstNext==NULL,
            "closing a rejected challenge participant safely clears reciprocal links");
         Check(CharacterInspect("Attacker",&saved)==1&&saved.wins==0&&saved.losses==0&&
            saved.kills==0&&saved.damage==2&&saved.speed==4&&saved.room==0,
            "rejected challenge preserves the first saved character");
         Check(CharacterInspect("Defender",&saved)==1&&saved.wins==0&&saved.losses==0&&
            saved.kills==0&&saved.damage==3&&saved.speed==5&&saved.room==0,
            "rejected challenge preserves the second saved character");
         FinishHeapCombat();
         Check(pstFirstConn==NULL,"challenge admission fixtures release all connection/body storage");
      }
}

static void TestCombatRules(void)
{
   static const struct {
      int roll,defender_dexterity,attacker_size,damage;
      const char *message;
   } cases[]={
      {1,2,1,0,"You miss"}, {1,1,1,2,"You punch"},
      {5,1,1,2,"You punch"}, {6,1,1,4,"You kick"},
      {10,1,1,4,"You kick"}, {1,1,2,2,"You punch"}
   };
   size_t c;
   int pulse,reverse;
   for(c=0;c<sizeof(cases)/sizeof(cases[0]);c++)
   {
      conn_t *first=HeapCombatPair(),*second=first->pstNext;
      first->pstBody->iStats[SIZ]=cases[c].attacker_size;
      second->pstBody->iStats[DEX]=cases[c].defender_dexterity;
      SetInput(first,"challenge Defender");
      SetInput(second,"accept");
      first->iOutLen=second->iOutLen=0;
      fixed_d10=cases[c].roll;
      combat_rolls=0;
      first->pstBody->iSpeed=SPEED(first->pstBody)-1;
      second->pstBody->iSpeed=-3;
      Update();
      Check(second->pstBody&&second->pstBody->iDam==cases[c].damage&&
         combat_rolls==3&&OutputContains(first,cases[c].message),
         "miss/punch/kick boundaries and odd half-damage rounding retain legacy rules");
      Check(first->pstBody->iSpeed==-3&&first->pstBody->iWin==0&&
         first->pstBody->iKill==0,"nonlethal attacks retain recovery and progression");
      FinishHeapCombat();
   }
   {
      conn_t *first=HeapCombatPair(),*second=first->pstNext;
      SetInput(first,"challenge Defender");
      SetInput(second,"accept");
      fixed_d10=1;
      combat_rolls=0;
      for(pulse=0;pulse<8;pulse++)
         Update();
      Check(combat_rolls==0&&first->pstBody->iSpeed==8,
         "speed-nine gladiators wait eight complete initial pulses");
      Update();
      Check(combat_rolls==6&&first->pstBody->iSpeed==-3&&
         first->pstBody->iDam==2&&second->pstBody->iDam==2,
         "both live opponents attack on the ninth pulse");
      for(pulse=0;pulse<11;pulse++)
         Update();
      Check(combat_rolls==6,"three-pulse recovery is preserved before the next attack");
      Update();
      Check(combat_rolls==12&&first->pstBody->iDam==4&&second->pstBody->iDam==4,
         "next attacks follow the original speed-plus-three interval");
      FinishHeapCombat();
   }
   for(reverse=0;reverse<2;reverse++)
   {
      conn_t *first=HeapCombatPair(),*second=first->pstNext;
      conn_t *attacker=reverse?second:first,*victim=reverse?first:second;
      saved_character saved;
      char victim_name[16];
      strcpy(victim_name,victim->pstBody->a_chName);
      SetInput(first,"challenge Defender");
      SetInput(second,"accept");
      Save(victim->pstBody);
      fixed_d10=10;
      victim->pstBody->iDam=HEALTH(victim->pstBody)-DAMAGE(attacker->pstBody);
      attacker->pstBody->iSpeed=SPEED(attacker->pstBody)-1;
      victim->pstBody->iSpeed=-3;
      Update();
      Check(victim->pstBody&&victim->pstBody->iDam==HEALTH(victim->pstBody)&&
         victim->eStatus==FIGHTING&&attacker->pstBody->iKill==0&&access(victim_name,F_OK)==0,
         "damage exactly equal to health remains alive under the original strict death threshold");
      attacker->pstBody->iSpeed=SPEED(attacker->pstBody)-1;
      Update();
      Check(victim->pstBody==NULL&&victim->eStatus==CROWD&&
         attacker->pstBody->iWin==1&&attacker->pstBody->iKill==1&&
         attacker->pstBody->pstOpponent==NULL&&attacker->pstBody->iRoom==0&&
         access(victim_name,F_OK)!=0&&errno==ENOENT,
         "the next lethal hit frees only the defeated body and awards one persisted victory");
      Check(CharacterInspect(attacker->pstBody->a_chName,&saved)==1&&saved.wins==1&&saved.kills==1,
         "the victory counters are persisted in a valid historical-layout record");
      FinishHeapCombat();
   }
}

static void TestRevokedChallengeAdmission(void)
{
   account_session session;
   const char *name="AdmissionPeer",*password="admission-fixture-password-only";
   int accepting,reenabled;
   Check(AccountRegister(name,password,&session),"admission account fixture registers");
   for(accepting=1;accepting>=0;accepting--)
      for(reenabled=0;reenabled<2;reenabled++)
      {
         conn_t *first=HeapCombatPair(),*second=first->pstNext;
         conn_t *actor=accepting?second:first,*peer=accepting?first:second;
         saved_character saved;
         int unchanged;
         Check(AccountLogin(name,password,&session),"admission peer obtains a current session");
         peer->account=session;
         first->pstBody->iDam=2;
         second->pstBody->iDam=3;
         Save(first->pstBody);
         Save(second->pstBody);
         if(accepting)
            SetInput(first,"challenge Defender");
         Check(AccountSetDisabled(name,1),"admission peer session is revoked before periodic polling");
         if(reenabled)
            Check(AccountSetDisabled(name,0),"re-enabling an account does not restore its old session");
         Check(AccountSessionRole(peer->account)==ROLE_NONE&&!peer->bClosing&&!peer->bWriteFailed,
            "revoked admission fixture has no pending-close flag yet");
         SetInput(actor,accepting?"accept":"challenge Defender");
         unchanged=first->pstBody->iDam==2&&second->pstBody->iDam==3&&
            first->pstBody->iRoom==0&&second->pstBody->iRoom==0&&
            first->eStatus==(accepting?CHALLENGER:GLADIATOR)&&second->eStatus==GLADIATOR&&
            first->pstBody->pstOpponent==(accepting?second:NULL)&&
            second->pstBody->pstOpponent==(accepting?first:NULL);
         CloseConnection(peer);
         Check(actor->pstBody->iWin==0&&actor->pstBody->iLoss==0&&actor->pstBody->iKill==0,
            "revoked or stale challenger cannot manufacture a surrender win before polling");
         Check(unchanged&&actor->eStatus==GLADIATOR&&actor->pstBody->pstOpponent==NULL,
            "revoked admission changes no combat state and cleans pending links safely");
         Check(CharacterInspect("Attacker",&saved)==1&&saved.wins==0&&saved.losses==0&&saved.damage==2,
            "revoked admission preserves the first saved character");
         Check(CharacterInspect("Defender",&saved)==1&&saved.wins==0&&saved.losses==0&&saved.damage==3,
            "revoked admission preserves the second saved character");
         FinishHeapCombat();
         if(!reenabled)
            Check(AccountSetDisabled(name,0),"admission account fixture re-enables for fresh login");
      }
   {
      conn_t *first=HeapCombatPair(),*second=first->pstNext;
      Check(AccountLogin(name,password,&session),"re-enabled admission peer can obtain a fresh epoch");
      first->account=session;
      SetInput(first,"challenge Defender");
      SetInput(second,"accept");
      Check(first->eStatus==FIGHTING&&second->eStatus==FIGHTING&&
         first->pstBody->iRoom>0&&first->pstBody->iRoom==second->pstBody->iRoom,
         "fresh reauthentication restores ordinary live duel admission");
      FinishHeapCombat();
   }
}

static void TestCombatRandomness(void)
{
   int first[64],second[64];
   size_t i;
   srand(12345);
   for(i=0;i<64;i++)
      first[i]=RollD10();
   srand(12345);
   for(i=0;i<64;i++)
   {
      second[i]=RollD10();
      Check(first[i]>=1&&first[i]<=10&&second[i]>=1&&second[i]<=10,
         "production combat dice remain in the inclusive D10 range");
   }
   Check(memcmp(first,second,sizeof(first))!=0,
      "resetting the public libc seed cannot replay the production combat dice stream");
}

static void TestShutdownStorageCleanup(void)
{
   conn_t *first=HeapCombatPair(),*second=first->pstNext;
   conn_t *anonymous=calloc(1,sizeof(*anonymous));
   conn_t *connections[3]={first,second,anonymous};
   const char *names[2]={"Attacker","Defender"};
   body_t before[2],after;
   int sockets[3][2],i;
   Check(anonymous!=NULL,"shutdown anonymous connection allocation");
   second->pstNext=anonymous;
   anonymous->pstPrev=second;
   anonymous->eStatus=CROWD;
   for(i=0;i<3;i++)
   {
      Check(socketpair(AF_UNIX,SOCK_STREAM,0,sockets[i])==0,"shutdown fixture sockets");
      Check(fcntl(sockets[i][0],F_SETFL,O_NONBLOCK)==0,"shutdown output stays nonblocking");
      connections[i]->iSocket=sockets[i][0];
      FD_SET(sockets[i][0],&a_sFd);
   }
   Save(first->pstBody);
   Save(second->pstBody);
   for(i=0;i<2;i++)
   {
      FILE *file=fopen(names[i],"rb");
      Check(file&&fread(&before[i],sizeof(before[i]),1,file)==1,
         "shutdown snapshot of pre-fight save");
      fclose(file);
   }
   SetInput(first,"challenge Defender");
   SetInput(second,"accept");
   first->auth_stage=AUTH_NEW_PASSWORD;
   strcpy(first->auth_password,"shutdown-fixture-password-only");
   strcpy(first->auth_old_password,"shutdown-fixture-old-password-only");
   anonymous->auth_stage=AUTH_LOGIN_PASSWORD;
   strcpy(anonymous->a_chInBuf,"shutdown-partial-fixture");
   anonymous->iInLen=(int)strlen(anonymous->a_chInBuf);
   ReleaseConnections();
   Check(pstFirstConn==NULL,"orderly shutdown releases the complete connection/body ownership list");
   for(i=0;i<3;i++)
   {
      Check(fcntl(sockets[i][0],F_GETFD)==-1&&errno==EBADF&&
         !FD_ISSET(sockets[i][0],&a_sFd),"shutdown closes and unregisters every client socket");
      close(sockets[i][1]);
   }
   for(i=0;i<2;i++)
   {
      FILE *file=fopen(names[i],"rb");
      Check(file&&fread(&after,sizeof(after),1,file)==1,"shutdown record remains readable");
      fclose(file);
      Check(memcmp(&after,&before[i],sizeof(after))==0,
         "shutdown does not invent surrender counters or overwrite pre-fight saves");
      unlink(names[i]);
   }
   ReleaseConnections();
   Check(pstFirstConn==NULL,"shutdown storage cleanup is idempotent");
}

static void TestCombatHeartbeat(void)
{
   time_t before=AuthClock();
   time_t combat=CombatClock();
   time_t after=AuthClock();
   Check(combat>=before&&combat<=after,
      "combat heartbeat uses the monotonic domain rather than adjustable wall time");
   Check(!CombatPulseDue(100,100)&&CombatPulseDue(100,101),
      "combat heartbeat preserves one-second pulse gating");
   Check(CombatPulseDue(100,1000)&&!CombatPulseDue(1000,1000),
      "lag coalesces to one pulse instead of replaying missed turns");
   Check(!CombatPulseDue(100,99)&&!CombatPulseDue(100,0),
      "clock failure or an unexpected backwards sample cannot create an extra pulse");
   if(sizeof(time_t)==sizeof(int64_t)&&(time_t)-1<(time_t)0)
   {
      Check(!CombatPulseDue((time_t)INT64_MAX,(time_t)INT64_MIN),
         "signed time boundaries do not overflow the pulse comparison");
      Check(CombatPulseDue((time_t)INT64_MIN,(time_t)INT64_MAX),
         "large forward samples need no signed time subtraction");
   }
}

static void TestCharacterCreation(void)
{
   conn_t stConn;

   memset(&stConn, 0, sizeof(stConn));
   stConn.eStatus = CROWD;
   stConn.account = regression_account;
   pstFirstConn = &stConn;
   SetInput(&stConn, "create ArenaTest");

   Check(stConn.pstBody != NULL, "create allocates a character");
   Check(stConn.eStatus == CITIZEN, "new character enters citizen status");
   Check(StatTotal(stConn.pstBody) == 5, "new character starts with five stat points");
   Check(strcmp(stConn.pstBody->a_chName, "Arenatest") == 0,
      "character names are normalized to initial-capital form");
   free(stConn.pstBody);
   pstFirstConn = NULL;
}

static void TestTrainingAndSave(void)
{
   int i;
   FILE *pSaved;
   body_t stLoaded;
   conn_t stReload;

   ResetWorld();
   for (i = STR; i <= WIT; i++)
      test_body_one.iStats[i] = 1;
   test_conn_one.eStatus = TRAINING;
   for (i = 0; i < 8; i++)
   {
      TrainStat(&test_conn_one, "strength", STR);
      TrainStat(&test_conn_one, "dexterity", DEX);
      TrainStat(&test_conn_one, "stamina", STA);
   }
   TrainStat(&test_conn_one, "size", SIZ);

   Check(test_body_one.iStats[STR] == 9, "training respects the per-stat maximum");
   Check(StatTotal(&test_body_one) == 30, "training respects the total point budget");

   strcpy(test_body_one.a_chName, "Savedhero");
   test_body_one.iWin = 3;
   test_body_one.pstOpponent = &test_conn_two;
   Save(&test_body_one);
   Check(test_body_one.pstOpponent==&test_conn_two,
      "saving does not change live opponent state");
   pSaved = fopen("Savedhero", "rb");
   Check(pSaved != NULL, "trained character is saved");
   Check(fread(&stLoaded, sizeof(stLoaded), 1, pSaved) == 1,
      "saved character record can be read");
   fclose(pSaved);
   Check(stLoaded.iWin == 3, "saved progression is retained");
   Check(stLoaded.pstOpponent==NULL, "save records never contain a process pointer");

   memset(&stReload,0,sizeof(stReload));
   stReload.eStatus=CROWD;
   stReload.account=regression_account;
   Check(AccountLinkLegacy("Regtest","Savedhero"),"legacy fixture is explicitly linked");
   pstFirstConn=&stReload;
   SetInput(&stReload,"create Savedhero");
   Check(stReload.pstBody!=NULL&&stReload.eStatus==GLADIATOR,
      "legacy-layout player record reloads");
   Check(stReload.pstBody->iWin==3&&StatTotal(stReload.pstBody)==30,
      "reload preserves stats and progression");
   Check(stReload.pstBody->pstOpponent==NULL&&stReload.pstBody->iRoom==0,
      "reload clears process-local combat state");
   free(stReload.pstBody);
   pstFirstConn=NULL;
   Check(remove("Savedhero") == 0, "test save can be cleaned up");
}

static void TestChallengeAcceptAndCombat(void)
{
   int i;
   body_t *pstLoser;

   ResetWorld();
   for (i = STR; i <= WIT; i++)
   {
      test_body_one.iStats[i] = 9;
      test_body_two.iStats[i] = 1;
   }
   pstLoser=(body_t*)malloc(sizeof(*pstLoser));
   Check(pstLoser!=NULL, "combat fixture allocates the defeatable body");
   *pstLoser=test_body_two;
   test_conn_two.pstBody=pstLoser;
   Save(pstLoser);

   SetInput(&test_conn_one, "challenge Defender");
   Check(test_conn_one.eStatus == CHALLENGER, "challenge marks the challenger");
   Check(test_body_one.pstOpponent == &test_conn_two,
      "challenge links both gladiators");

   SetInput(&test_conn_two, "accept");
   Check(test_conn_one.eStatus == FIGHTING &&
      test_conn_two.eStatus == FIGHTING, "accept starts the fight");
   Check(test_body_one.iRoom == pstLoser->iRoom,
      "opponents enter the same arena");

   for (i = 0; i < 20 && test_conn_two.pstBody != NULL; i++)
      Update();

   Check(test_conn_two.pstBody == NULL, "combat can defeat a gladiator");
   Check(test_conn_one.eStatus == GLADIATOR, "winner returns to gladiator status");
   Check(test_body_one.iWin == 1 && test_body_one.iKill == 1,
      "victory and kill progression are awarded");
   {
      FILE *pDefeated=fopen("Defender","rb");
      Check(pDefeated==NULL, "existing permadeath removes only the defeated test record");
      if(pDefeated)
         fclose(pDefeated);
   }
}

static void TestOutputSafety(void)
{
   conn_t stConn;
   int a_iSockets[2];
   char a_chLong[MAX_BUF*2];
   char a_chReceived[MAX_BUF*2];
   ssize_t iRead;
   size_t iTotal=0;
   size_t iExpected=MAX_BUF-1+4;

   memset(&stConn,0,sizeof(stConn));
   memset(a_chLong,'x',sizeof(a_chLong)-1);
   a_chLong[sizeof(a_chLong)-1]='\0';
   Check(socketpair(AF_UNIX,SOCK_STREAM,0,a_iSockets)==0,
      "output fixture creates local sockets");
   stConn.iSocket=a_iSockets[0];
   pstFirstConn=&stConn;
   PutOutput(&stConn,TO_USER,"%s",a_chLong);
   Check(stConn.iOutLen<MAX_BUF, "oversized output stays within its buffer");
   Check(FlushOutput(&stConn)==0, "buffered output flushes successfully");

   while(iTotal<iExpected)
   {
      iRead=read(a_iSockets[1],a_chReceived+iTotal,iExpected-iTotal);
      Check(iRead>0, "formatted output is delivered");
      iTotal+=(size_t)iRead;
   }
   Check(iTotal==iExpected, "formatted output is bounded including prompt");

   Check(fcntl(a_iSockets[0],F_SETFL,O_NONBLOCK)==0,
      "backpressure fixture uses nonblocking output");
   while(write(a_iSockets[0],a_chLong,sizeof(a_chLong))>0)
      ;
   Check(errno==EAGAIN||errno==EWOULDBLOCK, "socket backpressure is reached");
   PutOutput(&stConn,TO_USER,"Queued output");
   Check(FlushOutput(&stConn)>0, "backpressure retains queued output without spinning");
   PutOutput(&stConn,TO_USER,"%s",a_chLong);
   Check(stConn.bWriteFailed&&stConn.iOutLen<MAX_BUF,
      "a full slow-client queue is bounded and marked for disconnect");

   close(a_iSockets[1]);
   PutOutput(&stConn,TO_USER,"Disconnected peer test");
   Check(FlushOutput(&stConn)<0, "a failed write returns instead of hanging");
   close(a_iSockets[0]);
   pstFirstConn=NULL;
}

static int OutputHasBytes(conn_t *connection,const void *bytes,size_t length)
{
   size_t offset,total=(size_t)connection->iOutLen;
   if(length>total)
      return 0;
   for(offset=0;offset<=total-length;offset++)
      if(memcmp(connection->a_chOutBuf+offset,bytes,length)==0)
         return 1;
   return 0;
}

static int OutputContains(conn_t *pstConn,const char *szText)
{
   return OutputHasBytes(pstConn,szText,strlen(szText));
}

static void TestInputAndQuit(void)
{
   conn_t stConn;
   int a_iSockets[2];
   char a_chLarge[MAX_BUF+16];
   const char *szCommands="commands\nwho\n";

   memset(&stConn,0,sizeof(stConn));
   stConn.eStatus=CROWD;
   Check(socketpair(AF_UNIX,SOCK_STREAM,0,a_iSockets)==0,
      "input fixture creates local sockets");
   stConn.iSocket=a_iSockets[0];
   pstFirstConn=&stConn;
   Check(write(a_iSockets[1],szCommands,strlen(szCommands))==
      (ssize_t)strlen(szCommands), "batched commands are sent");
   Check(GetInput(&stConn)==TRUE, "batched input is accepted");
   Check(OutputContains(&stConn,"commands"), "first batched command is processed");
   Check(OutputContains(&stConn,"visible"), "second batched command is processed");

   stConn.iOutLen=0;
   memset(a_chLarge,'x',sizeof(a_chLarge));
   a_chLarge[MAX_BUF+1]='\n';
   memcpy(a_chLarge+MAX_BUF+2,"who\n",4);
   Check(write(a_iSockets[1],a_chLarge,MAX_BUF+6)==MAX_BUF+6,
      "oversized input line is sent");
   Check(GetInput(&stConn)&&GetInput(&stConn),
      "oversized input is drained without overflow");
   Check(OutputContains(&stConn,"Input line too long"),
      "oversized line is rejected rather than executed in part");
   Check(OutputContains(&stConn,"visible"), "input recovers after oversized line");

   Check(write(a_iSockets[1],"quit\n",5)==5, "quit command is sent");
   Check(!GetInput(&stConn)&&stConn.bClosing,
      "quit defers freeing the connection until input handling returns");
   close(a_iSockets[0]);
   close(a_iSockets[1]);
   pstFirstConn=NULL;
}

static void InputFragment(conn_t *connection,int peer,const unsigned char *bytes,size_t length)
{
   Check(write(peer,bytes,length)==(ssize_t)length&&GetInput(connection),
      "Telnet input fragment processed");
}

static void TestTelnetInput(void)
{
   conn_t connection=stConnEmpty;
   int sockets[2],command,option;
   size_t i;
   unsigned char large[MAX_BUF],matrix[256*3+4];
   const unsigned char naws[]={255,250,31,0,80,0,24,255,240,'w','h','o','\n'};
   const unsigned char controls[]={'w',255,241,'h',255,249,'o','\r','\n'};
   const unsigned char escaped[]={
      255,250,31,255,255,240,'q','u','i','t','\n',255,240,'w','h','o','\n'
   };
   const unsigned char negative_acks[]={255,252,80,255,254,0};
   const unsigned char wont_binary[]={255,252,0},dont_naws[]={255,254,31};
   const unsigned char sub_start[]={255,250,31},sub_end[]={255,240,'w','h','o','\n'};
   const unsigned char password_controls[]={255,253,1,255,250,31,0,80,0,10,255,240};
   Check(socketpair(AF_UNIX,SOCK_STREAM,0,sockets)==0,"Telnet socket fixture");
   connection.iSocket=sockets[0];
   connection.eStatus=CROWD;
   connection.iIp=9000;
   connection.connection_token=40001;
   pstFirstConn=&connection;
   InputFragment(&connection,sockets[1],naws,sizeof(naws));
   Check(OutputContains(&connection,"visible")&&!OutputContains(&connection,"Unrecognised"),
      "valid NAWS negotiation never contaminates a command");
   connection.iOutLen=0;
   for(i=0;i<sizeof(naws);i++)
      InputFragment(&connection,sockets[1],naws+i,1);
   Check(OutputContains(&connection,"visible")&&connection.iInLen==0,
      "Telnet framing survives a read boundary after every byte");
   connection.iOutLen=0;
   InputFragment(&connection,sockets[1],controls,sizeof(controls));
   Check(OutputContains(&connection,"visible"),"single-byte Telnet controls preserve CRLF commands");
   connection.iOutLen=0;
   for(i=0;i<sizeof(escaped);i++)
      InputFragment(&connection,sockets[1],escaped+i,1);
   Check(OutputContains(&connection,"visible")&&!connection.bClosing,
      "escaped IAC and newline/quit bytes stay inside subnegotiation");
   memset(large,'x',sizeof(large));
   InputFragment(&connection,sockets[1],sub_start,sizeof(sub_start));
   InputFragment(&connection,sockets[1],large,sizeof(large));
   InputFragment(&connection,sockets[1],large,sizeof(large));
   InputFragment(&connection,sockets[1],sub_end,sizeof(sub_end));
   Check(connection.iInLen==0&&!connection.bInputTooLong,
      "oversized subnegotiations consume constant state, not the application buffer");
   for(command=251;command<=254;command++)
   {
      for(option=0;option<256;option++)
      {
         matrix[(size_t)option*3]=255;
         matrix[(size_t)option*3+1]=(unsigned char)command;
         matrix[(size_t)option*3+2]=(unsigned char)option;
      }
      memcpy(matrix+256*3,"who\n",4);
      connection.iOutLen=0;
      InputFragment(&connection,sockets[1],matrix,sizeof(matrix));
      Check(OutputContains(&connection,"visible")&&!OutputContains(&connection,"Unrecognised"),
         "every option byte is consumed by WILL/WONT/DO/DONT framing");
      if(command==251)
         Check(OutputHasBytes(&connection,dont_naws,sizeof(dont_naws)),
            "unsupported remote options are refused");
      if(command==253)
         Check(OutputHasBytes(&connection,wont_binary,sizeof(wont_binary)),
            "unsupported local binary option is refused with its NUL option byte");
   }
   connection.iOutLen=0;
   InputFragment(&connection,sockets[1],negative_acks,sizeof(negative_acks));
   Check(connection.iOutLen==0,"negative acknowledgments do not start negotiation loops");
   SetInput(&connection,"login Regtest");
   connection.iOutLen=0;
   InputFragment(&connection,sockets[1],(const unsigned char*)"regression-",11);
   for(i=0;i<sizeof(password_controls);i++)
      InputFragment(&connection,sockets[1],password_controls+i,1);
   Check(connection.auth_stage==AUTH_LOGIN_PASSWORD&&connection.iInLen==11&&
      connection.iOutLen==0,"password echo acknowledgment and NAWS newline do not advance the prompt");
   memset(auth_jobs,0,sizeof(auth_jobs));
   memset(auth_ip_limits,0,sizeof(auth_ip_limits));
   auth_global_attempts=0;
   InputFragment(&connection,sockets[1],(const unsigned char*)"test-password-only\n",19);
   Check(auth_jobs[0].state==1&&
      strcmp(auth_jobs[0].password,"regression-test-password-only")==0&&
      !OutputContains(&connection,"regression-test-password-only")&&
      connection.a_chInBuf[0]=='\0',"Telnet payload never changes, echoes or retains consumed password input");
   AccountCancel(&connection);
   sodium_memzero(auth_jobs,sizeof(auth_jobs));
   close(sockets[0]);
   close(sockets[1]);
   pstFirstConn=NULL;
}

static void TestSocketSetup(void)
{
   int iSocket;
   struct sockaddr_in stAddress;
   socklen_t iSize=sizeof(stAddress);

   FD_ZERO(&a_sFd);
   iSocket=InitSocket();
   Check(iSocket>=0&&iSocket<FD_SETSIZE, "listener is created within descriptor limits");
   Check(getsockname(iSocket,(struct sockaddr*)&stAddress,&iSize)==0,
      "listener has a valid address");
   Check(ntohs(stAddress.sin_port)>0, "listener binds an available test port");
   CloseSocket(iSocket);
   Check(!NewConnection(-1), "invalid connection descriptors are rejected");
}

static void TestInvalidPlayerInput(void)
{
   conn_t stConn;
   FILE *pDamaged;
   char a_chOriginal[4]={0};

   memset(&stConn,0,sizeof(stConn));
   stConn.eStatus=CROWD;
   stConn.account=regression_account;
   pstFirstConn=&stConn;
   SetInput(&stConn,"create Bad1");
   Check(stConn.pstBody==NULL&&stConn.eStatus==CROWD,
      "invalid character names do not attach an uninitialized body");

   pDamaged=fopen("Corrupt","wb");
   Check(pDamaged!=NULL, "damaged-save fixture is created");
   Check(fwrite("bad",3,1,pDamaged)==1, "damaged-save fixture is written");
   fclose(pDamaged);
   Check(AccountLinkLegacy("Regtest","Corrupt"),"corrupt fixture ownership linked for validation test");
   SetInput(&stConn,"create Corrupt");
   Check(stConn.pstBody==NULL&&stConn.eStatus==CROWD,
      "damaged player records are rejected safely");
   pDamaged=fopen("Corrupt","rb");
   Check(pDamaged!=NULL, "rejected player file is retained");
   Check(fread(a_chOriginal,3,1,pDamaged)==1&&
      strcmp(a_chOriginal,"bad")==0, "rejected player file is unchanged");
   fclose(pDamaged);
   remove("Corrupt");
   pstFirstConn=NULL;
}

static void TestShutdownConfiguration(void)
{
   ResetWorld();
   bShutdown=FALSE;
   setenv("GLAD_SHUTDOWN_PASSWORD","unit-test-only",1);
   SetInput(&test_conn_one,"shutdown unused");
   Check(!bShutdown, "regular player cannot shut down even with old configuration");
   Check(AccountSetRole("Regtest",ROLE_BUILDER),"builder fixture role");
   SetInput(&test_conn_one,"shutdown");
   Check(!bShutdown,"builder cannot shut down");
   Check(AccountSetRole("Regtest",ROLE_MODERATOR),"moderator fixture role");
   SetInput(&test_conn_one,"shutdown");
   Check(!bShutdown,"moderator cannot shut down");
   Check(AccountSetRole("Regtest",ROLE_ADMINISTRATOR),"administrator fixture role");
   SetInput(&test_conn_one,"shutdown");
   Check(bShutdown==TRUE,"authorized staff shutdown remains available");
   bShutdown=FALSE;
   Check(AccountSetRole("Regtest",ROLE_PLAYER),"staff role revoked");
   SetInput(&test_conn_one,"shutdown");
   Check(!bShutdown,"role revocation takes effect without reconnect");
   unsetenv("GLAD_SHUTDOWN_PASSWORD");
   bShutdown=FALSE;
}

static void TestAccountCommandGates(void)
{
   conn_t connection;
   int sockets[2];
   const char *input="register Prompttest\nprompt-test-password-only\n";
   memset(&connection,0,sizeof(connection));
   connection.eStatus=CROWD;
   pstFirstConn=&connection;
   SetInput(&connection,"create Unowned");
   Check(connection.pstBody==NULL,"unauthenticated creation denied centrally");
   Check(OutputContains(&connection,"Permission denied"),"permission denial is explicit");
   connection.account=regression_account;
   connection.iOutLen=0;
   SetInput(&connection,"commands");
   Check(!OutputContains(&connection,"shutdown"),"staff commands hidden from players");
   memset(&connection.account,0,sizeof(connection.account));
   connection.iOutLen=0;
   Check(socketpair(AF_UNIX,SOCK_STREAM,0,sockets)==0,"password prompt socket fixture");
   connection.iSocket=sockets[0];
   Check(write(sockets[1],input,strlen(input))==(ssize_t)strlen(input),
      "password prompt input fixture");
   Check(GetInput(&connection)==TRUE,"password prompt handles batched input");
   Check(connection.auth_stage==AUTH_REGISTER_CONFIRM,"registration requests confirmation");
   Check(connection.a_chInBuf[0]=='\0',"consumed password input is wiped");
   AccountCancel(&connection);
   SetInput(&connection,"prompt-test-password-only");
   Check(!OutputContains(&connection,"prompt-test-password-only"),
      "password never echoed, even when pasted outside a prompt");
   Check(connection.auth_password[0]=='\0'&&connection.auth_old_password[0]=='\0',
      "cancelled password buffers are wiped");
   close(sockets[0]);
   close(sockets[1]);
   pstFirstConn=NULL;
}

static void TestAuthenticationLimits(void)
{
   conn_t connections[AUTH_JOB_COUNT+1];
   int i;
   memset(connections,0,sizeof(connections));
   memset(auth_ip_limits,0,sizeof(auth_ip_limits));
   auth_global_attempts=0;
   auth_global_window=AuthClock();
   for(i=0;i<5;i++)
      Check(AuthRateAllowed(1234),"per-IP authentication budget");
   Check(!AuthRateAllowed(1234),"reconnecting does not bypass per-IP limit");
   for(i=0;i<=AUTH_JOB_COUNT;i++)
   {
      connections[i].iIp=(uint32_t)(2000+i);
      connections[i].connection_token=(uint64_t)(i+1);
      strcpy(connections[i].account_name,"Queuefixture");
      pstFirstConn=&connections[i];
      AccountQueue(&connections[i],2,"queued-test-password-only");
   }
   Check(connections[AUTH_JOB_COUNT-1].auth_stage==AUTH_WAIT,
      "bounded queue accepts available slots");
   Check(connections[AUTH_JOB_COUNT].auth_stage==AUTH_IDLE&&
      OutputContains(&connections[AUTH_JOB_COUNT],"busy"),
      "full authentication queue fails safely without blocking");
   sodium_memzero(auth_jobs,sizeof(auth_jobs));
   memset(&connections[0],0,sizeof(connections[0]));
   connections[0].auth_stage=AUTH_REGISTER_CONFIRM;
   connections[0].auth_deadline=AuthClock()-1;
   strcpy(connections[0].auth_password,"expired-test-password-only");
   pstFirstConn=&connections[0];
   AccountPoll();
   Check(connections[0].bClosing&&connections[0].auth_password[0]=='\0',
      "expired authentication wipes credentials and closes session");
   pstFirstConn=NULL;
}

static void TestSavedInspection(void)
{
   struct historical_body {
      char name[16];
      int stats[5];
      int speed,room,wins,losses,kills,damage;
      conn_t *opponent;
   };
   saved_character snapshot;
   body_t before,after;
   FILE *file;
   Check(sizeof(body_t)==sizeof(struct historical_body)&&
      offsetof(body_t,iStats)==offsetof(struct historical_body,stats)&&
      offsetof(body_t,iSpeed)==offsetof(struct historical_body,speed)&&
      offsetof(body_t,iRoom)==offsetof(struct historical_body,room)&&
      offsetof(body_t,iWin)==offsetof(struct historical_body,wins)&&
      offsetof(body_t,iLoss)==offsetof(struct historical_body,losses)&&
      offsetof(body_t,iKill)==offsetof(struct historical_body,kills)&&
      offsetof(body_t,iDam)==offsetof(struct historical_body,damage)&&
      offsetof(body_t,pstOpponent)==offsetof(struct historical_body,opponent),
      "shared record header exactly preserves historical native ABI");
   ResetWorld();
   strcpy(test_body_one.a_chName,"Inspecthero");
   test_body_one.iStats[0]=4;
   test_body_one.iStats[1]=5;
   test_body_one.iStats[2]=6;
   test_body_one.iStats[3]=7;
   test_body_one.iStats[4]=8;
   test_body_one.iWin=42;
   Save(&test_body_one);
   file=fopen("Inspecthero","rb");
   Check(file!=NULL&&fread(&before,sizeof(before),1,file)==1,"snapshot fixture read");
   fclose(file);
   Check(CharacterInspect("Inspecthero",&snapshot)==1&&snapshot.wins==42&&
      snapshot.stats[4]==8,"native saved character inspection");
   file=fopen("Inspecthero","rb");
   Check(file!=NULL&&fread(&after,sizeof(after),1,file)==1&&
      memcmp(&before,&after,sizeof(before))==0,"inspection does not modify save bytes");
   fclose(file);
   Check(CharacterInspect("../Inspecthero",&snapshot)==-1&&
      CharacterInspect("Missinghero",&snapshot)==0,"unsafe/missing inspection paths");
   Check(symlink("Inspecthero","Inspectlink")==0,"inspection symlink fixture");
   Check(CharacterInspect("Inspectlink",&snapshot)==-1,"inspection refuses symlinks");
   unlink("Inspectlink");
   unlink("Inspecthero");
   pstFirstConn=NULL;
}

static void TestInGameStaff(void)
{
   account_session owner;
   int sockets[2];
   ResetWorld();
   test_conn_one.iOutLen=0;
   SetInput(&test_conn_one,"dashboard");
   Check(OutputContains(&test_conn_one,"Permission denied"),"players denied in-game dashboard");
   Check(AccountSetRole("Regtest",ROLE_BUILDER),"builder dashboard fixture");
   test_conn_one.iOutLen=0;
   SetInput(&test_conn_one,"staff");
   Check(OutputContains(&test_conn_one,"STAFF DASHBOARD")&&
      !OutputContains(&test_conn_one,"staff accounts"),"builder sees capability-filtered menu");
   test_conn_one.iOutLen=0;
   SetInput(&test_conn_one,"staff accounts");
   Check(OutputContains(&test_conn_one,"Permission denied"),"builder cannot bypass menu");
   Check(AccountRegister("Staffowner","regression-test-password-only",&owner)&&
      AccountSetRole("Staffowner",ROLE_OWNER),"owner dashboard fixture");
   test_conn_one.account=owner;
   strcpy(test_conn_one.account_name,"Staffowner");
   test_conn_one.connection_token=10001;
   Check(socketpair(AF_UNIX,SOCK_STREAM,0,sockets)==0,"staff confirmation socket fixture");
   test_conn_one.iSocket=sockets[0];
   Check(AccountSetRole("Regtest",ROLE_PLAYER),"staff target fixture");
   test_conn_one.iOutLen=0;
   SetInput(&test_conn_one,"staff role Regtest builder extra");
   Check(test_conn_one.auth_stage==AUTH_IDLE,"staff arguments cannot contain passwords");
   SetInput(&test_conn_one,"staff role Regtest builder");
   Check(test_conn_one.auth_stage==AUTH_STAFF_CONFIRM,"role change requires explicit confirmation");
   SetInput(&test_conn_one,"cancel");
   Check(test_conn_one.auth_stage==AUTH_IDLE&&AccountSessionRole(regression_account)==ROLE_PLAYER,
      "cancelled role change leaves account untouched");
   memset(auth_jobs,0,sizeof(auth_jobs));
   memset(auth_ip_limits,0,sizeof(auth_ip_limits));
   auth_global_attempts=0;
   SetInput(&test_conn_one,"staff role Regtest builder");
   SetInput(&test_conn_one,"CONFIRM");
   Check(test_conn_one.auth_stage==AUTH_STAFF_PASSWORD,"confirmation opens separate password prompt");
   SetInput(&test_conn_one,"regression-test-password-only");
   Check(auth_jobs[0].state==1&&auth_jobs[0].operation==4&&
      AccountSessionRole(regression_account)==ROLE_PLAYER,
      "password proof queues without applying staff mutation");
   auth_jobs[0].state=3;
   auth_jobs[0].ok=1;
   AccountPoll();
   Check(AccountSessionRole(regression_account)==ROLE_BUILDER&&
      test_conn_one.auth_stage==AUTH_IDLE,"verified result applies on game thread");
   SetInput(&test_conn_one,"staff role Regtest moderator");
   SetInput(&test_conn_one,"CONFIRM");
   SetInput(&test_conn_one,"regression-test-password-only");
   auth_jobs[0].state=3;
   auth_jobs[0].ok=1;
   test_conn_one.auth_deadline=AuthClock()-1;
   AccountPoll();
   Check(test_conn_one.bClosing&&AccountSessionRole(regression_account)==ROLE_BUILDER,
      "expired completed proof cannot apply role mutation");
   test_conn_one.bClosing=FALSE;
   SetInput(&test_conn_one,"staff role Regtest moderator");
   SetInput(&test_conn_one,"CONFIRM");
   SetInput(&test_conn_one,"regression-test-password-only");
   auth_jobs[0].state=3;
   auth_jobs[0].ok=1;
   auth_jobs[0].token++;
   AccountPoll();
   Check(AccountSessionRole(regression_account)==ROLE_BUILDER,
      "stale connection token cannot apply queued staff action");
   AccountCancel(&test_conn_one);
   SetInput(&test_conn_one,"staff role Regtest moderator");
   SetInput(&test_conn_one,"CONFIRM");
   SetInput(&test_conn_one,"regression-test-password-only");
   auth_jobs[0].state=3;
   auth_jobs[0].ok=1;
   close(sockets[1]);
   AccountPoll();
   Check(test_conn_one.bClosing&&AccountSessionRole(regression_account)==ROLE_BUILDER,
      "known-disconnected socket cannot apply completed proof");
   close(sockets[0]);
   Check(AccountSetRole("Regtest",ROLE_PLAYER),"restore gameplay fixture role");
   memset(auth_jobs,0,sizeof(auth_jobs));
   pstFirstConn=NULL;
}

static void TestAuthenticationReplay(void)
{
   account_session owner;
   conn_t connection=stConnEmpty;
   int sockets[2],success;
   char oversized[MAX_BUF];
   Check(AccountRegister("Nonceowner","regression-test-password-only",&owner)&&
      AccountSetRole("Nonceowner",ROLE_OWNER),"request isolation owner fixture");
   Check(socketpair(AF_UNIX,SOCK_STREAM,0,sockets)==0,"request isolation sockets");
   connection.iSocket=sockets[0];
   connection.eStatus=CROWD;
   connection.account=owner;
   connection.iIp=7000;
   connection.connection_token=20001;
   strcpy(connection.account_name,"Nonceowner");
   pstFirstConn=&connection;
   memset(auth_jobs,0,sizeof(auth_jobs));
   memset(auth_ip_limits,0,sizeof(auth_ip_limits));
   auth_global_attempts=0;
   SetInput(&connection,"staff role Regtest administrator");
   SetInput(&connection,"CONFIRM");
   SetInput(&connection,"regression-test-password-only");
   Check(auth_jobs[0].state==1,"old staff proof queued");
   auth_jobs[0].state=2; /* Deterministically pause the worker's completion. */
   memset(oversized,'A',sizeof(oversized));
   Check(write(sockets[1],oversized,sizeof(oversized))==(ssize_t)sizeof(oversized)&&
      GetInput(&connection),"oversized line reaches cancellation path");
   Check(write(sockets[1],"\n",1)==1&&GetInput(&connection)&&
      connection.auth_stage==AUTH_IDLE,"overflow cancels the old staff request");
   SetInput(&connection,"staff role Regtest moderator");
   SetInput(&connection,"CONFIRM");
   SetInput(&connection,"regression-test-password-only");
   Check(auth_jobs[1].state==1,"new staff proof has a separate queue slot");
   auth_jobs[0].state=3;
   auth_jobs[0].ok=1;
   AccountPoll();
   Check(AccountSessionRole(regression_account)==ROLE_PLAYER&&
      connection.auth_stage==AUTH_WAIT,
      "cancelled staff proof cannot mutate or cancel a newer action");
   auth_jobs[1].state=3;
   auth_jobs[1].ok=1;
   AccountPoll();
   Check(AccountSessionRole(regression_account)==ROLE_MODERATOR,
      "only the newer staff proof applies its requested role");
   Check(AccountSetRole("Regtest",ROLE_PLAYER),"restore replay target role");

   for(success=0;success<=1;success++)
   {
      connection.account.id=connection.account.epoch=0;
      connection.iIp=(uint32_t)(7100+success);
      connection.iOutLen=0;
      SetInput(&connection,"login Regtest");
      SetInput(&connection,"regression-test-password-only");
      auth_jobs[0].state=2;
      AccountCancel(&connection);
      SetInput(&connection,"login Nonceowner");
      SetInput(&connection,"regression-test-password-only");
      auth_jobs[0].state=3;
      auth_jobs[0].ok=success;
      auth_jobs[0].session=regression_account;
      AccountPoll();
      Check(connection.account.id==0&&connection.auth_stage==AUTH_WAIT,
         "stale successful/failed login cannot affect a newer login");
      auth_jobs[1].state=3;
      auth_jobs[1].ok=1;
      auth_jobs[1].session=owner;
      AccountPoll();
      Check(connection.account.id==owner.id&&
         strcmp(connection.account_name,"Nonceowner")==0,
         "accepted login identity matches its own account prompt");
   }
   {
      uint64_t saved_token=next_auth_request_token;
      next_auth_request_token=UINT64_MAX;
      AccountQueue(&connection,2,"regression-test-password-only");
      Check(next_auth_request_token==UINT64_MAX&&
         connection.auth_request_token==0&&connection.auth_stage==AUTH_IDLE&&
         auth_jobs[0].state==0,"request-token exhaustion fails closed without wrap/reuse");
      next_auth_request_token=saved_token;
   }
   close(sockets[0]);
   close(sockets[1]);
   memset(auth_jobs,0,sizeof(auth_jobs));
   pstFirstConn=NULL;
}

static void TestCompletedAuthenticationDeadlines(void)
{
   account_session owner;
   conn_t connection;
   int sockets[2],operation,expired,success;
   Check(AccountRegister("Expiryowner","regression-test-password-only",&owner)&&
      AccountSetRole("Expiryowner",ROLE_OWNER),"deadline owner fixture");
   Check(socketpair(AF_UNIX,SOCK_STREAM,0,sockets)==0,"deadline socket fixture");
   memset(auth_ip_limits,0,sizeof(auth_ip_limits));
   auth_global_attempts=0;
   for(operation=1;operation<=4;operation++)
      for(expired=0;expired<=1;expired++)
         for(success=0;success<=1;success++)
         {
            connection=stConnEmpty;
            connection.iSocket=sockets[0];
            connection.eStatus=CROWD;
            connection.connection_token=30001;
            connection.iIp=(uint32_t)(8000+operation);
            strcpy(connection.account_name,"Regtest");
            if(operation==3)
               connection.account=regression_account;
            if(operation==4)
            {
               connection.account=owner;
               strcpy(connection.account_name,"Expiryowner");
               connection.staff_target=regression_account.id;
               connection.staff_operation=1;
               connection.staff_value=ROLE_BUILDER;
               Check(AccountSetRole("Regtest",ROLE_PLAYER),"deadline target reset");
            }
            pstFirstConn=&connection;
            memset(auth_jobs,0,sizeof(auth_jobs));
            AccountQueue(&connection,operation,"regression-test-password-only");
            Check(auth_jobs[0].state==1,"deadline fixture queued");
            connection.auth_deadline=AuthClock()+(expired?0:120);
            auth_jobs[0].state=3;
            auth_jobs[0].ok=success;
            auth_jobs[0].session=operation==4?owner:regression_account;
            AccountPoll();
            Check(connection.auth_stage==AUTH_IDLE&&connection.auth_request_token==0&&
               connection.bClosing==(expired?TRUE:FALSE),
               "all completed results respect inclusive deadlines, including failures");
            if(operation<=2)
               Check(connection.account.id==(!expired&&success?regression_account.id:0),
                  "expired registration/login result never grants a session");
            if(operation==4)
               Check(AccountSessionRole(regression_account)==
                  (!expired&&success?ROLE_BUILDER:ROLE_PLAYER),
                  "expired staff proof never commits its pending mutation");
         }
   Check(AccountSetRole("Regtest",ROLE_PLAYER),"restore deadline target");
   memset(auth_jobs,0,sizeof(auth_jobs));
   close(sockets[0]);
   close(sockets[1]);
   pstFirstConn=NULL;
}

static void TestFailedOutputAuthorization(void)
{
   account_session owner;
   conn_t connection;
   staff_audit history[1];
   char oversized[MAX_BUF];
   int sockets[2],operation,transport,before,after;
   int64_t audit_id;
   Check(AccountRegister("Outputowner","regression-test-password-only",&owner)&&
      AccountSetRole("Outputowner",ROLE_OWNER),"output-proof owner fixture");
   memset(oversized,'X',sizeof(oversized)-1);
   oversized[sizeof(oversized)-1]='\0';
   for(operation=4;operation>=1;operation--)
      for(transport=0;transport<3;transport++)
      {
         Check(socketpair(AF_UNIX,SOCK_STREAM,0,sockets)==0&&
            fcntl(sockets[0],F_SETFL,O_NONBLOCK)==0,"output-proof socket fixture");
         connection=stConnEmpty;
         connection.iSocket=sockets[0];
         connection.eStatus=CROWD;
         connection.connection_token=40001;
         connection.iIp=9000;
         strcpy(connection.account_name,operation==4?"Outputowner":"Regtest");
         if(operation>=3)
            connection.account=operation==4?owner:regression_account;
         if(operation==4)
         {
            Check(AccountSetRole("Regtest",ROLE_PLAYER),"output-proof target reset");
            connection.staff_target=regression_account.id;
            connection.staff_operation=1;
            connection.staff_value=ROLE_BUILDER;
         }
         pstFirstConn=&connection;
         memset(auth_jobs,0,sizeof(auth_jobs));
         memset(auth_ip_limits,0,sizeof(auth_ip_limits));
         auth_global_attempts=0;
         AccountQueue(&connection,operation,"regression-test-password-only");
         Check(auth_jobs[0].state==1&&connection.auth_stage==AUTH_WAIT,
            "output-proof request queues before transport failure");
         before=AccountStaffAudit(owner,0,history,1);
         Check(before>=0,"output-proof audit snapshot");
         audit_id=before?history[0].id:0;
         if(transport==1)
            connection.bClosing=TRUE;
         else if(transport==2)
         {
            while(write(sockets[0],oversized,sizeof(oversized)-1)>0)
               ;
            Check(errno==EAGAIN||errno==EWOULDBLOCK,"output-proof socket backpressure");
            PutOutput(&connection,TO_USER,"%s",oversized);
            Check(connection.bWriteFailed!=FALSE,"actual queue overflow marks output failure");
         }
         auth_jobs[0].state=3;
         auth_jobs[0].ok=1;
         auth_jobs[0].session=operation==4?owner:regression_account;
         AccountPoll();
         if(operation==4)
            Check(AccountSessionRole(regression_account)==
               (transport==0?ROLE_BUILDER:ROLE_PLAYER),
               "closing or failed-output staff proof cannot mutate roles");
         Check(connection.auth_stage==(transport==0?AUTH_IDLE:AUTH_WAIT)&&
            (transport==0?connection.auth_request_token==0:
               connection.auth_request_token!=0),
            "completed proof applies only to a healthy transport");
         if(operation<=2)
            Check(connection.account.id==(transport==0?regression_account.id:0),
               "failed-output registration/login result cannot grant a session");
         if(operation==3&&transport!=0)
            Check(!OutputContains(&connection,"Password changed"),
               "failed-output password result is not consumed as a successful reply");
         after=AccountStaffAudit(owner,0,history,1);
         Check(after>=0&&((operation==4&&transport==0)?
            (after>0&&history[0].id>audit_id):
            ((after?history[0].id:0)==audit_id)),
            "discarded proof leaves staff mutation history unchanged");
         Check(auth_jobs[0].state==0&&auth_jobs[0].request_token==0,
            "discarded completed job is wiped rather than replayable");
         close(sockets[0]);
         close(sockets[1]);
      }
   Check(socketpair(AF_UNIX,SOCK_STREAM,0,sockets)==0,"direct staff-boundary sockets");
   for(transport=0;transport<3;transport++)
   {
      connection=stConnEmpty;
      connection.iSocket=sockets[0];
      connection.account=owner;
      connection.bClosing=transport==1;
      connection.bWriteFailed=transport==2;
      pstFirstConn=&connection;
      Check(AccountSetRole("Regtest",ROLE_PLAYER),"direct staff-boundary target reset");
      StaffApply(&connection,regression_account.id,1,ROLE_BUILDER);
      Check(AccountSessionRole(regression_account)==
         (transport==0?ROLE_BUILDER:ROLE_PLAYER),
         "staff mutation boundary independently rejects closing/failed transport");
   }
   Check(AccountSetRole("Regtest",ROLE_PLAYER),"restore output-proof gameplay role");
   close(sockets[0]);
   close(sockets[1]);
   sodium_memzero(auth_jobs,sizeof(auth_jobs));
   pstFirstConn=NULL;
}

static void TestProgressionCounterLimits(void)
{
   int fight,i;
   saved_character snapshot;
   conn_t *loser;
   ResetWorld();
   for(i=STR;i<=WIT;i++)
      test_body_one.iStats[i]=9;
   test_body_one.iWin=test_body_one.iKill=INT_MAX-1;
   for(fight=0;fight<2;fight++)
   {
      body_t *body=calloc(1,sizeof(*body));
      Check(body!=NULL,"counter combat body allocation");
      strcpy(body->a_chName,"Defender");
      for(i=STR;i<=WIT;i++)
         body->iStats[i]=1;
      test_conn_two.pstBody=body;
      test_conn_two.eStatus=GLADIATOR;
      Save(body);
      SetInput(&test_conn_one,"challenge Defender");
      SetInput(&test_conn_two,"accept");
      for(i=0;i<20&&test_conn_two.pstBody;i++)
         Update();
      Check(test_conn_two.pstBody==NULL&&test_body_one.iWin==INT_MAX&&
         test_body_one.iKill==INT_MAX,"combat increments to the ceiling without wrapping beyond it");
      Check(ValidPlayer(&test_body_one,"Attacker")&&
         CharacterInspect("Attacker",&snapshot)==1&&snapshot.wins==INT_MAX&&
         snapshot.kills==INT_MAX,"ceiling progression remains valid in the unchanged save format");
      Check(access("Defender",F_OK)!=0&&errno==ENOENT,"counter ceiling preserves permadeath");
   }
   loser=calloc(1,sizeof(*loser));
   Check(loser!=NULL,"counter surrender connection allocation");
   loser->pstBody=calloc(1,sizeof(*loser->pstBody));
   Check(loser->pstBody!=NULL,"counter surrender body allocation");
   strcpy(loser->pstBody->a_chName,"Loserhero");
   for(i=STR;i<=WIT;i++)
      loser->pstBody->iStats[i]=1;
   loser->pstBody->iLoss=INT_MAX;
   loser->pstBody->pstOpponent=&test_conn_one;
   loser->pstNext=&test_conn_one;
   loser->eStatus=FIGHTING;
   loser->iSocket=-1;
   test_conn_one.pstPrev=loser;
   test_conn_one.pstNext=NULL;
   test_conn_one.iSocket=-1;
   test_conn_one.eStatus=FIGHTING;
   test_body_one.pstOpponent=loser;
   pstFirstConn=loser;
   CloseConnection(loser);
   Check(test_body_one.iWin==INT_MAX&&test_body_one.pstOpponent==NULL&&
      test_conn_one.eStatus==GLADIATOR,"surrender at the ceiling preserves winner and cleanup");
   Check(CharacterInspect("Loserhero",&snapshot)==1&&snapshot.losses==INT_MAX,
      "surrender loss counter remains loadable at the ceiling");
   Check(CharacterInspect("Attacker",&snapshot)==1&&snapshot.wins==INT_MAX&&
      snapshot.kills==INT_MAX,"surrender does not corrupt existing ceiling progression");
   unlink("Attacker");
   unlink("Loserhero");
   pstFirstConn=NULL;
}

static void TestExpiredAuthenticationInput(void)
{
   int stage,variant,sockets[2],i;
   conn_t connection;
   char large[MAX_BUF];
   const char *password="regression-test-password-only";
   const char *batched="regression-test-password-only\nwho\n";
   memset(auth_ip_limits,0,sizeof(auth_ip_limits));
   auth_global_attempts=0;
   for(stage=AUTH_LOGIN_PASSWORD;stage<=AUTH_STAFF_PASSWORD;stage++)
   {
      connection=stConnEmpty;
      connection.eStatus=CROWD;
      connection.auth_stage=stage;
      connection.auth_deadline=AuthClock();
      connection.auth_request_token=50001;
      connection.connection_token=50001;
      connection.staff_target=regression_account.id;
      connection.staff_operation=1;
      connection.staff_value=ROLE_OWNER;
      connection.iIp=(uint32_t)(10000+stage);
      strcpy(connection.account_name,"Regtest");
      strcpy(connection.auth_password,password);
      strcpy(connection.auth_old_password,password);
      strcpy(connection.a_chInBuf,"partial-sensitive-fixture");
      connection.iInLen=(int)strlen(connection.a_chInBuf);
      pstFirstConn=&connection;
      memset(auth_jobs,0,sizeof(auth_jobs));
      AccountInput(&connection,stage==AUTH_STAFF_CONFIRM?"CONFIRM":password);
      Check(connection.bClosing&&connection.auth_stage==AUTH_IDLE&&
         connection.auth_request_token==0,
         "late input cannot queue work or revive any expired authentication stage");
      Check(connection.auth_password[0]=='\0'&&connection.auth_old_password[0]=='\0'&&
         connection.a_chInBuf[0]=='\0'&&connection.iInLen==0&&connection.staff_target==0,
         "input-time expiry wipes partial credentials and pending metadata");
      for(i=0;i<AUTH_JOB_COUNT;i++)
         Check(auth_jobs[i].state==0,"expired input never queues password work");
   }
   for(variant=0;variant<3;variant++)
   {
      Check(socketpair(AF_UNIX,SOCK_STREAM,0,sockets)==0,"late wire-input sockets");
      connection=stConnEmpty;
      connection.eStatus=CROWD;
      connection.iSocket=sockets[0];
      connection.auth_stage=AUTH_LOGIN_PASSWORD;
      connection.auth_deadline=AuthClock();
      strcpy(connection.account_name,"Regtest");
      pstFirstConn=&connection;
      if(variant==0)
         Check(write(sockets[1],"part",4)==4,"late partial input fixture");
      else if(variant==1)
         Check(write(sockets[1],batched,strlen(batched))==(ssize_t)strlen(batched),
            "late batched password fixture");
      else
      {
         memset(large,'A',sizeof(large));
         Check(write(sockets[1],large,sizeof(large))==(ssize_t)sizeof(large),
            "late oversized partial input fixture");
      }
      Check(!GetInput(&connection)&&connection.bClosing&&connection.iInLen==0&&
         connection.a_chInBuf[0]=='\0'&&!OutputContains(&connection,"visible"),
         "expired wire input is rejected before partial, complete or oversized data is processed");
      close(sockets[0]);
      close(sockets[1]);
   }
   sodium_memzero(auth_jobs,sizeof(auth_jobs));
   pstFirstConn=NULL;
}

static void TestArenaAllocationLimits(void)
{
   int variant,i,saved_cursor=arena_sequence;
   conn_t occupants[4];
   body_t bodies[4];
   for(variant=0;variant<3;variant++)
   {
      ResetWorld();
      for(i=STR;i<=WIT;i++)
         test_body_one.iStats[i]=test_body_two.iStats[i]=1;
      memset(occupants,0,sizeof(occupants));
      memset(bodies,0,sizeof(bodies));
      if(variant==2)
      {
         test_conn_two.pstNext=&occupants[0];
         for(i=0;i<4;i++)
         {
            occupants[i].pstBody=&bodies[i];
            occupants[i].eStatus=FIGHTING;
            occupants[i].pstPrev=i?&occupants[i-1]:&test_conn_two;
            occupants[i].pstNext=i<3?&occupants[i+1]:NULL;
            strcpy(bodies[i].a_chName,"Sentinel");
            bodies[i].iRoom=1+i/2;
            bodies[i].pstOpponent=&occupants[i^1];
         }
      }
      arena_sequence=variant==1?INT_MAX-1:INT_MAX;
      SetInput(&test_conn_one,"challenge Defender");
      SetInput(&test_conn_two,"accept");
      Check(test_body_one.iRoom==(variant==1?INT_MAX:variant==2?3:1)&&
         test_body_two.iRoom==test_body_one.iRoom&&
         test_conn_one.eStatus==FIGHTING&&test_conn_two.eStatus==FIGHTING,
         "arena boundaries allocate positive unique rooms without integer overflow");
      if(variant==2)
         for(i=0;i<4;i++)
            Check(bodies[i].iRoom==1+i/2&&bodies[i].pstOpponent==&occupants[i^1],
               "wrapping leaves existing fights and arena assignments intact");
   }
   {
      conn_t *blocked=calloc(FD_SETSIZE,sizeof(*blocked));
      body_t *blocked_bodies=calloc(FD_SETSIZE,sizeof(*blocked_bodies));
      Check(blocked!=NULL&&blocked_bodies!=NULL,"bounded arena-search fixture allocation");
      ResetWorld();
      test_conn_two.pstNext=blocked;
      for(i=0;i<FD_SETSIZE;i++)
      {
         blocked[i].pstBody=&blocked_bodies[i];
         blocked[i].pstNext=i+1<FD_SETSIZE?&blocked[i+1]:NULL;
         blocked_bodies[i].iRoom=i+1;
      }
      arena_sequence=0;
      SetInput(&test_conn_one,"challenge Defender");
      test_body_one.iDam=test_body_two.iDam=1;
      test_body_one.iSpeed=test_body_two.iSpeed=-2;
      SetInput(&test_conn_two,"accept");
      Check(test_conn_one.eStatus==CHALLENGER&&test_conn_two.eStatus==GLADIATOR&&
         test_body_one.iRoom==0&&test_body_two.iRoom==0&&
         test_body_one.iDam==1&&test_body_two.iDam==1&&
         test_body_one.iSpeed==-2&&test_body_two.iSpeed==-2&&
         test_body_one.pstOpponent==&test_conn_two&&
         OutputContains(&test_conn_two,"arenas are busy"),
         "bounded-search exhaustion rejects acceptance without changing the pending fight");
      ClearChallenge(&test_conn_one);
      test_conn_two.pstNext=NULL;
      free(blocked);
      free(blocked_bodies);
   }
   arena_sequence=saved_cursor;
   pstFirstConn=NULL;
}

static void TestReplacedChallenges(void)
{
   conn_t stThird;
   body_t stThirdBody;

   ResetWorld();
   memset(&stThird,0,sizeof(stThird));
   memset(&stThirdBody,0,sizeof(stThirdBody));
   strcpy(stThirdBody.a_chName,"Third");
   stThird.pstBody=&stThirdBody;
   stThird.eStatus=GLADIATOR;
   stThird.account=regression_account;
   stThird.pstPrev=&test_conn_two;
   test_conn_two.pstNext=&stThird;

   SetInput(&test_conn_one,"challenge Defender");
   SetInput(&test_conn_one,"challenge Third");
   Check(test_body_two.pstOpponent==NULL,
      "rechallenging detaches the old target");
   Check(test_body_one.pstOpponent==&stThird&&
      stThirdBody.pstOpponent==&test_conn_one, "new challenge is reciprocal");

   SetInput(&stThird,"challenge Defender");
   Check(test_body_one.pstOpponent==NULL&&
      test_conn_one.eStatus==GLADIATOR, "abandoned challenger returns to idle state");
   ClearChallenge(&stThird);
   Check(stThirdBody.pstOpponent==NULL&&test_body_two.pstOpponent==NULL,
      "canceling an unaccepted challenge clears both ends");
   pstFirstConn=NULL;
}

static void TestCommunicationAndPermissions(void)
{
   ResetWorld();
   SetInput(&test_conn_one,"say room-message");
   Check(OutputContains(&test_conn_one,"You say 'room-message'"),
      "say echoes to its speaker");
   Check(OutputContains(&test_conn_two,"Attacker says 'room-message'"),
      "say reaches the same room");

   test_conn_one.iOutLen=test_conn_two.iOutLen=0;
   test_body_two.iRoom=-1;
   SetInput(&test_conn_one,"say private-room-message");
   Check(!OutputContains(&test_conn_two,"private-room-message"),
      "room speech does not cross room boundaries");
   SetInput(&test_conn_one,"chat world-message");
   Check(OutputContains(&test_conn_two,"world-message"),
      "chat reaches different rooms");

   test_conn_one.iOutLen=test_conn_two.iOutLen=0;
   SetInput(&test_conn_one,"emote waves");
   Check(OutputContains(&test_conn_one,"Attacker waves"),
      "emote includes the actor name");
   Check(!OutputContains(&test_conn_two,"waves"),
      "emote respects room boundaries");

   test_conn_one.iOutLen=0;
   test_conn_one.eStatus=CROWD;
   test_conn_one.pstBody=NULL;
   SetInput(&test_conn_one,"score");
   Check(OutputContains(&test_conn_one,"Unrecognised command"),
      "crowd members cannot run body-only commands");
   pstFirstConn=NULL;
}

static void TestAllTrainingCommands(void)
{
   int i;

   ResetWorld();
   test_conn_one.eStatus=CITIZEN;
   for(i=STR;i<=WIT;i++)
      test_body_one.iStats[i]=1;
   SetInput(&test_conn_one,"train");
   Check(test_conn_one.eStatus==TRAINING&&test_body_one.iRoom==-1,
      "train enters the training room");
   SetInput(&test_conn_one,"str");
   SetInput(&test_conn_one,"dex");
   SetInput(&test_conn_one,"sta");
   SetInput(&test_conn_one,"siz");
   SetInput(&test_conn_one,"wit");
   Check(StatTotal(&test_body_one)==10,
      "all five stat commands retain their effects");
   SetInput(&test_conn_one,"leave");
   Check(test_conn_one.eStatus==CITIZEN&&test_body_one.iRoom==0,
      "unfinished training returns to citizen status");

   test_body_one.iWin=10;
   Check(strcmp(StatusName(&test_body_one),"Blooded")==0,
      "win progression retains status titles");
   test_body_one.iWin=50;
   Check(strcmp(StatusName(&test_body_one),"Master")==0,
      "status progression retains its maximum");
   test_body_one.iLoss=100;
   Check(strcmp(StatusName(&test_body_one),"Novice")==0,
      "status progression retains its minimum");
   pstFirstConn=NULL;
}

int main(void)
{
   signal(SIGPIPE,SIG_IGN);
   unlink("regression-accounts.sqlite3");
   Check(AccountsOpen("regression-accounts.sqlite3"),"regression account database opens");
   Check(AccountRegister("Regtest","regression-test-password-only",&regression_account),
      "gameplay fixtures have an authenticated account");
   TestCharacterCreation();
   TestTrainingAndSave();
   TestChallengeAcceptAndCombat();
   TestOutputSafety();
   TestInputAndQuit();
   TestSocketSetup();
   TestInvalidPlayerInput();
   TestShutdownConfiguration();
   TestReplacedChallenges();
   TestCommunicationAndPermissions();
   TestAllTrainingCommands();
   TestAccountCommandGates();
   TestAuthenticationLimits();
   TestSavedInspection();
   TestInGameStaff();
   TestAuthenticationReplay();
   TestCompletedAuthenticationDeadlines();
   TestFailedOutputAuthorization();
   TestProgressionCounterLimits();
   TestTelnetInput();
   TestExpiredAuthenticationInput();
   TestArenaAllocationLimits();
   TestCombatPendingDisconnect();
   TestChallengeDisconnectAdmission();
   TestRevokedChallengeAdmission();
   TestCombatRules();
   TestCombatRandomness();
   TestShutdownStorageCleanup();
   TestCombatHeartbeat();
   AccountsClose();
   unlink("regression-accounts.sqlite3");
   printf("Passed %d Gladiator Pits regression checks.\n", tests_run);
   return EXIT_SUCCESS;
}
