/* Modernization contributions: Copyright (c) 2026 LaWaylon Battles (aKa Yami). */
/* Included by glad.c. Optional, mandatory-on-that-listener PROXY v1.
 * Only an explicitly trusted IPv4 transport peer may assert a client address.
 * No fallback, headers after admission, UNKNOWN, v2, TLVs or forwarded text.
 * Logical clients may be IPv4 or IPv6; no native player record fields change. */
static int proxy_enabled;
static uint32_t trusted_proxy;

static int ProxyConfigure(void)
{
   const char *value=getenv("GLAD_TRUSTED_PROXY");
   struct in_addr address;
   proxy_enabled=0;
   trusted_proxy=0;
   if(!value)
      return 1;
   if(inet_pton(AF_INET,value,&address)!=1)
      return 0;
   trusted_proxy=ntohl(address.s_addr);
   proxy_enabled=1;
   return 1;
}

static void GreetConnection(conn_t *connection)
{
   PutOutput(connection,TO_USER,"Welcome to the Gladiator Pits!\n\r");
   PutOutput(connection,TO_USER,"Use register <account> or login <account> before create <character>.\n\r"
      "Password traffic requires a TLS gateway or secure tunnel; TCP alone is not encrypted.\n\r");
}

static int ProxyExpired(const conn_t *connection)
{
   return connection->proxy_pending&&AuthClock()>=connection->proxy_deadline;
}

static int ProxyPort(const char *text)
{
   unsigned int number=0;
   if(!*text)
      return 0;
   for(;*text;text++)
   {
      if(*text<'0'||*text>'9')
         return 0;
      number=number*10U+(unsigned int)(*text-'0');
      if(number>65535U)
         return 0;
   }
   return 1;
}

static int ProxyHeader(conn_t *connection)
{
   char magic[6],family[6],source[46],destination[46],sport[6],dport[6],extra;
   struct in_addr v4,target4;
   struct in6_addr v6,target6;
   if(strncmp(connection->proxy_header,"PROXY ",6)!=0||
      sscanf(connection->proxy_header,"%5s %5s %45s %45s %5s %5s %c",
         magic,family,source,destination,sport,dport,&extra)!=6||
      strcmp(magic,"PROXY")!=0||!ProxyPort(sport)||!ProxyPort(dport))
      return 0;
   if(strcmp(family,"TCP4")==0)
   {
      if(inet_pton(AF_INET,source,&v4)!=1||inet_pton(AF_INET,destination,&target4)!=1)
         return 0;
      connection->iIp=ntohl(v4.s_addr);
   }
   else if(strcmp(family,"TCP6")==0)
   {
      if(inet_pton(AF_INET6,source,&v6)!=1||inet_pton(AF_INET6,destination,&target6)!=1)
         return 0;
      if(IN6_IS_ADDR_V4MAPPED(&v6))
      {
         memcpy(&v4.s_addr,&v6.s6_addr[12],sizeof(v4.s_addr));
         connection->iIp=ntohl(v4.s_addr);
      }
      else
      {
         connection->proxy_ipv6=TRUE;
         memcpy(connection->proxy_address,v6.s6_addr,sizeof(connection->proxy_address));
      }
   }
   else
      return 0;
   connection->proxy_pending=FALSE;
   sodium_memzero(connection->proxy_header,sizeof(connection->proxy_header));
   connection->proxy_length=0;
   Log("Trusted proxy client %s admitted.\n",source);
   GreetConnection(connection);
   return 1;
}

static int ProxyData(conn_t *connection,unsigned char byte)
{
   if(ProxyExpired(connection))
      return 0;
   if(byte=='\n')
   {
      if(connection->proxy_length<1||
         connection->proxy_header[connection->proxy_length-1]!='\r')
         return 0;
      connection->proxy_header[connection->proxy_length-1]='\0';
      return ProxyHeader(connection);
   }
   if(connection->proxy_length>=sizeof(connection->proxy_header)-1||
      (byte!='\r'&&(byte<32||byte>126))||
      (connection->proxy_length&&connection->proxy_header[connection->proxy_length-1]=='\r'))
      return 0;
   connection->proxy_header[connection->proxy_length++]=(char)byte;
   return 1;
}
