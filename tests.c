/*
 * Small regression tests for the existing Gladiator Pits rules.
 * Run in an isolated directory because the game stores player files there.
 */
#define main glad_server_main
#include "glad.c"
#undef main

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
   AccountsClose();
   unlink("regression-accounts.sqlite3");
   printf("Passed %d Gladiator Pits regression checks.\n", tests_run);
   return EXIT_SUCCESS;
}
