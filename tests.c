/*
 * Small regression tests for the existing Gladiator Pits rules.
 * Run in an isolated directory because the game stores player files there.
 */
#define main glad_server_main
#include "glad.c"
#undef main
#include <stddef.h>

static conn_t test_conn_one;
static conn_t test_conn_two;
static body_t test_body_one;
static body_t test_body_two;
static int tests_run;
static account_session regression_account;

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

static int OutputContains(conn_t *pstConn,const char *szText)
{
   char a_chOutput[MAX_BUF];
   memcpy(a_chOutput,pstConn->a_chOutBuf,(size_t)pstConn->iOutLen);
   a_chOutput[pstConn->iOutLen]='\0';
   return strstr(a_chOutput,szText)!=NULL;
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
   AccountsClose();
   unlink("regression-accounts.sqlite3");
   printf("Passed %d Gladiator Pits regression checks.\n", tests_run);
   return EXIT_SUCCESS;
}
