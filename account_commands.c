/* Included by glad.c, like the historical commands.c. No gameplay state is
 * touched by the worker: only the main thread owns connection objects. */
#define AUTH_JOB_COUNT 8
#define AUTH_IP_BUCKETS 64

typedef enum {
   AUTH_IDLE,AUTH_LOGIN_PASSWORD,AUTH_REGISTER_PASSWORD,AUTH_REGISTER_CONFIRM,
   AUTH_OLD_PASSWORD,AUTH_NEW_PASSWORD,AUTH_NEW_CONFIRM,AUTH_WAIT,
   AUTH_STAFF_CONFIRM,AUTH_STAFF_PASSWORD
} auth_stage;

typedef struct {
   int state,operation,ok;
   uint64_t token,request_token;
   account_session session;
   int64_t staff_target;
   int staff_operation,staff_value;
   char name[ACCOUNT_NAME_MAX+1];
   char password[ACCOUNT_PASSWORD_MAX+1],old_password[ACCOUNT_PASSWORD_MAX+1];
} auth_job;

static auth_job auth_jobs[AUTH_JOB_COUNT];
static pthread_t auth_worker;
static pthread_mutex_t auth_lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t auth_ready=PTHREAD_COND_INITIALIZER;
static int auth_stopping;
static uint64_t next_connection_token;
/* Main-thread issued, never reused; zero means there is no pending request. */
static uint64_t next_auth_request_token;
static struct {
   uint32_t ip;
   time_t window;
   unsigned int attempts;
} auth_ip_limits[AUTH_IP_BUCKETS];
static time_t auth_global_window;
static unsigned int auth_global_attempts;

static time_t AuthClock(void)
{
   struct timespec now;
   if(clock_gettime(CLOCK_MONOTONIC,&now)!=0)
      return 0;
   return now.tv_sec;
}

static int ConnectionCan(conn_t *connection,account_permission permission)
{
   return permission==PERM_PUBLIC||
      AccountRoleCan(AccountSessionRole(connection->account),permission);
}

static void AccountCancel(conn_t *connection)
{
   sodium_memzero(connection->auth_password,sizeof(connection->auth_password));
   sodium_memzero(connection->auth_old_password,sizeof(connection->auth_old_password));
   connection->auth_stage=AUTH_IDLE;
   connection->auth_request_token=0;
   connection->auth_deadline=0;
   connection->staff_target=0;
   connection->staff_operation=connection->staff_value=0;
   /* IAC WONT ECHO: restore client echo after a password prompt. */
   PutOutput(connection,TO_USER,"\xff\xfc\x01");
}

static int AuthRateAllowed(uint32_t ip)
{
   size_t i,slot=0;
   time_t now=AuthClock();
   for(i=0;i<AUTH_IP_BUCKETS;i++)
   {
      if(auth_ip_limits[i].attempts&&auth_ip_limits[i].ip==ip)
      {
         slot=i;
         break;
      }
      if(auth_ip_limits[i].window<auth_ip_limits[slot].window)
         slot=i;
   }
   if(auth_ip_limits[slot].ip!=ip||now-auth_ip_limits[slot].window>=60)
   {
      auth_ip_limits[slot].ip=ip;
      auth_ip_limits[slot].window=now;
      auth_ip_limits[slot].attempts=0;
   }
   if(now-auth_global_window>=60)
   {
      auth_global_window=now;
      auth_global_attempts=0;
   }
   if(auth_ip_limits[slot].attempts>=5||auth_global_attempts>=60)
      return 0;
   auth_ip_limits[slot].attempts++;
   auth_global_attempts++;
   return 1;
}

static void *AuthWorker(void *unused)
{
   int i;
   (void)unused;
   for(;;)
   {
      auth_job job;
      pthread_mutex_lock(&auth_lock);
      for(;;)
      {
         if(auth_stopping)
         {
            pthread_mutex_unlock(&auth_lock);
            return NULL;
         }
         for(i=0;i<AUTH_JOB_COUNT;i++)
            if(auth_jobs[i].state==1)
               break;
         if(i<AUTH_JOB_COUNT)
            break;
         pthread_cond_wait(&auth_ready,&auth_lock);
      }
      job=auth_jobs[i];
      auth_jobs[i].state=2;
      sodium_memzero(auth_jobs[i].password,sizeof(auth_jobs[i].password));
      sodium_memzero(auth_jobs[i].old_password,sizeof(auth_jobs[i].old_password));
      pthread_mutex_unlock(&auth_lock);
      if(job.operation==1)
         job.ok=AccountRegister(job.name,job.password,&job.session);
      else if(job.operation==2)
         job.ok=AccountLogin(job.name,job.password,&job.session);
      else if(job.operation==3)
         job.ok=AccountPasswordChange(&job.session,job.old_password,job.password);
      else if(job.operation==4)
      {
         account_session proof={0,0};
         job.ok=AccountLogin(job.name,job.password,&proof)&&
            proof.id==job.session.id&&proof.epoch==job.session.epoch;
         sodium_memzero(&proof,sizeof(proof));
      }
      else
         job.ok=0;
      sodium_memzero(job.password,sizeof(job.password));
      sodium_memzero(job.old_password,sizeof(job.old_password));
      job.state=3;
      pthread_mutex_lock(&auth_lock);
      auth_jobs[i]=job;
      pthread_mutex_unlock(&auth_lock);
      sodium_memzero(&job,sizeof(job));
   }
}

static int AccountRuntimeStart(void)
{
   if(!AccountsOpen("accounts.sqlite3"))
      return 0;
   auth_stopping=0;
   if(pthread_create(&auth_worker,NULL,AuthWorker,NULL)!=0)
   {
      AccountsClose();
      return 0;
   }
   return 1;
}

static void AccountRuntimeStop(void)
{
   pthread_mutex_lock(&auth_lock);
   auth_stopping=1;
   pthread_cond_signal(&auth_ready);
   pthread_mutex_unlock(&auth_lock);
   pthread_join(auth_worker,NULL);
   sodium_memzero(auth_jobs,sizeof(auth_jobs));
   AccountsClose();
}

static void AccountQueue(conn_t *connection,int operation,const char *password)
{
   int i;
   uint64_t request_token=0;
   if(next_auth_request_token==UINT64_MAX)
   {
      AccountCancel(connection);
      PutOutput(connection,TO_USER,"Authentication unavailable; contact staff.\n\r");
      return;
   }
   if(!AuthRateAllowed(connection->iIp))
   {
      AccountCancel(connection);
      PutOutput(connection,TO_USER,"Authentication limit reached; try again in one minute.\n\r");
      return;
   }
   pthread_mutex_lock(&auth_lock);
   for(i=0;i<AUTH_JOB_COUNT;i++)
      if(auth_jobs[i].state==0)
         break;
   if(i<AUTH_JOB_COUNT)
   {
      auth_job *job=&auth_jobs[i];
      memset(job,0,sizeof(*job));
      job->state=1;
      job->operation=operation;
      job->token=connection->connection_token;
      job->request_token=request_token=++next_auth_request_token;
      job->session=connection->account;
      job->staff_target=connection->staff_target;
      job->staff_operation=connection->staff_operation;
      job->staff_value=connection->staff_value;
      strcpy(job->name,connection->account_name);
      strcpy(job->password,password);
      strcpy(job->old_password,connection->auth_old_password);
      pthread_cond_signal(&auth_ready);
   }
   pthread_mutex_unlock(&auth_lock);
   AccountCancel(connection);
   if(i==AUTH_JOB_COUNT)
      PutOutput(connection,TO_USER,"Authentication busy; try again later.\n\r");
   else
   {
      connection->auth_stage=AUTH_WAIT;
      connection->auth_request_token=request_token;
      connection->auth_deadline=AuthClock()+120;
      PutOutput(connection,TO_USER,"Authenticating...\n\r");
   }
}

static int AccountPoll(void)
{
   int i,busy=0;
   conn_t *connection;
   static time_t last_validation;
   time_t now=AuthClock();
   for(i=0;i<AUTH_JOB_COUNT;i++)
   {
      auth_job result;
      pthread_mutex_lock(&auth_lock);
      if(auth_jobs[i].state!=3)
      {
         busy|=auth_jobs[i].state!=0;
         pthread_mutex_unlock(&auth_lock);
         continue;
      }
      result=auth_jobs[i];
      sodium_memzero(&auth_jobs[i],sizeof(auth_jobs[i]));
      pthread_mutex_unlock(&auth_lock);
      for(connection=pstFirstConn;connection;connection=connection->pstNext)
         if(connection->connection_token==result.token)
            break;
      if(connection&&connection->auth_stage==AUTH_WAIT&&!connection->bClosing&&
         result.request_token!=0&&connection->auth_request_token==result.request_token)
      {
         /* Check before cancellation clears the deadline, for every operation. */
         if(now>=connection->auth_deadline)
         {
            connection->bClosing=TRUE;
            PutOutput(connection,TO_USER,result.operation==4?
               "Staff action expired; no change applied.\n\r":
               "Authentication expired; reconnect and log in.\n\r");
            AccountCancel(connection);
            sodium_memzero(&result,sizeof(result));
            continue;
         }
         if(result.operation==4)
         {
            if(result.ok&&AccountSessionRole(result.session)!=ROLE_NONE)
               StaffApply(connection,result.staff_target,result.staff_operation,result.staff_value);
            else
               PutOutput(connection,TO_USER,"Staff password verification failed; no change applied.\n\r");
            AccountCancel(connection);
            sodium_memzero(&result,sizeof(result));
            continue;
         }
         AccountCancel(connection);
         if(result.ok&&AccountSessionRole(result.session)!=ROLE_NONE)
         {
            connection->account=result.session;
            PutOutput(connection,TO_USER,result.operation==3?
               "Password changed. Other sessions have been revoked.\n\r":
               "Account authenticated. Use create <character> to enter the game.\n\r");
         }
         else
            PutOutput(connection,TO_USER,"Authentication failed; no session granted.\n\r");
      }
      sodium_memzero(&result,sizeof(result));
   }
   for(connection=pstFirstConn;connection;connection=connection->pstNext)
   {
      if(connection->auth_stage!=AUTH_IDLE&&now>=connection->auth_deadline)
      {
         AccountCancel(connection);
         connection->bClosing=TRUE;
      }
      if(now!=last_validation&&connection->auth_stage!=AUTH_WAIT&&connection->account.id&&
         AccountSessionRole(connection->account)==ROLE_NONE)
      {
         PutOutput(connection,TO_USER,"Account session revoked; reconnect and log in.\n\r");
         connection->bClosing=TRUE;
      }
   }
   last_validation=now;
   return busy;
}

static void AccountInput(conn_t *connection,const char *input)
{
   size_t length=strlen(input);
   if(connection->auth_stage==AUTH_STAFF_CONFIRM)
   {
      if(strcmp(input,"CONFIRM")!=0)
      {
         AccountCancel(connection);
         PutOutput(connection,TO_USER,"Staff action cancelled; no change applied.\n\r");
      }
      else
      {
         connection->auth_stage=AUTH_STAFF_PASSWORD;
         connection->auth_deadline=AuthClock()+120;
         PutOutput(connection,TO_USER,"\xff\xfb\x01" "Current staff password: ");
      }
      return;
   }
   if(connection->auth_stage==AUTH_WAIT)
   {
      PutOutput(connection,TO_USER,"Please wait for authentication.\n\r");
      return;
   }
   if(!AccountPasswordValid(input))
   {
      AccountCancel(connection);
      PutOutput(connection,TO_USER,"Passwords must be 15-128 printable characters. Try again.\n\r");
      return;
   }
   switch(connection->auth_stage)
   {
      case AUTH_LOGIN_PASSWORD: AccountQueue(connection,2,input); break;
      case AUTH_STAFF_PASSWORD: AccountQueue(connection,4,input); break;
      case AUTH_REGISTER_PASSWORD:
      case AUTH_NEW_PASSWORD:
         memcpy(connection->auth_password,input,length+1);
         connection->auth_stage=connection->auth_stage==AUTH_REGISTER_PASSWORD?
            AUTH_REGISTER_CONFIRM:AUTH_NEW_CONFIRM;
         PutOutput(connection,TO_USER,"Confirm password: ");
         break;
      case AUTH_OLD_PASSWORD:
         memcpy(connection->auth_old_password,input,length+1);
         connection->auth_stage=AUTH_NEW_PASSWORD;
         PutOutput(connection,TO_USER,"New password: ");
         break;
      case AUTH_REGISTER_CONFIRM:
      case AUTH_NEW_CONFIRM:
         if(strcmp(input,connection->auth_password)!=0)
         {
            AccountCancel(connection);
            PutOutput(connection,TO_USER,"Passwords did not match. Try again.\n\r");
         }
         else
            AccountQueue(connection,
               connection->auth_stage==AUTH_REGISTER_CONFIRM?1:3,input);
         break;
      default: break;
   }
}

static void AccountBegin(conn_t *connection,char *name,int registering)
{
   if(connection->account.id)
   {
      PutOutput(connection,TO_USER,"Already authenticated; reconnect to switch accounts.\n\r");
      return;
   }
   if(!AccountNameValid(name))
   {
      PutOutput(connection,TO_USER,"Account names: 3-32 ASCII letters/digits/underscores, starting with a letter.\n\r");
      return;
   }
   strcpy(connection->account_name,name);
   connection->auth_stage=registering?AUTH_REGISTER_PASSWORD:AUTH_LOGIN_PASSWORD;
   connection->auth_deadline=AuthClock()+120;
   PutOutput(connection,TO_USER,"\xff\xfb\x01Password: ");
}

CMD(Register)
{
   AccountBegin(pstConn,szTxt,1);
}

CMD(Login)
{
   AccountBegin(pstConn,szTxt,0);
}

CMD(Password)
{
   (void)szTxt;
   pstConn->auth_stage=AUTH_OLD_PASSWORD;
   pstConn->auth_deadline=AuthClock()+120;
   PutOutput(pstConn,TO_USER,"\xff\xfb\x01" "Current password: ");
}

CMD(Account)
{
   (void)szTxt;
   if(!pstConn->account.id)
      PutOutput(pstConn,TO_USER,"Use register <account> or login <account>.\n\r");
   else
      PutOutput(pstConn,TO_USER,"Account: %s  Role: %s\n\r",pstConn->account_name,
         AccountRoleName(AccountSessionRole(pstConn->account)));
}
