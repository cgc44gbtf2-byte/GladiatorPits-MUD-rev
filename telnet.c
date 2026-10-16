/* Modernization contributions: Copyright (c) 2026 LaWaylon Battles (aKa Yami). */
/* Included by glad.c. Consume Telnet framing before interpreting game input.
 * Keep only state, never a subnegotiation buffer; payload length is untrusted.
 * The game does not use terminal/window/binary options. Existing server ECHO
 * announcements suppress local echo only during separate password prompts. */
enum {
   TELNET_DATA,TELNET_COMMAND,TELNET_OPTION,TELNET_SUBNEG,TELNET_SUBNEG_IAC
};

static int TelnetPasswordPrompt(const conn_t *connection)
{
   switch(connection->auth_stage)
   {
      case AUTH_LOGIN_PASSWORD: case AUTH_REGISTER_PASSWORD:
      case AUTH_REGISTER_CONFIRM: case AUTH_OLD_PASSWORD:
      case AUTH_NEW_PASSWORD: case AUTH_NEW_CONFIRM: case AUTH_STAFF_PASSWORD:
         return 1;
      default:
         return 0;
   }
}

/* True means this byte is application data; false means protocol control. */
static int TelnetData(conn_t *connection,unsigned char byte)
{
   switch(connection->telnet_state)
   {
      case TELNET_DATA:
         if(byte!=255) /* IAC */
            return 1;
         connection->telnet_state=TELNET_COMMAND;
         break;
      case TELNET_COMMAND:
         connection->telnet_state=TELNET_DATA;
         if(byte==255) /* Escaped literal IAC, still subject to ASCII filtering. */
            return 1;
         if(byte==250) /* SB */
            connection->telnet_state=TELNET_SUBNEG;
         else if(byte>=251&&byte<=254) /* WILL, WONT, DO, DONT */
         {
            connection->telnet_command=byte;
            connection->telnet_state=TELNET_OPTION;
         }
         /* Other single-byte commands are consumed, not interpreted as input. */
         break;
      case TELNET_OPTION:
         connection->telnet_state=TELNET_DATA;
         if(connection->telnet_command==253&&
            !(byte==1&&TelnetPasswordPrompt(connection)))
            PutOutput(connection,TO_USER,"%c%c%c",255,252,(int)byte); /* WONT */
         else if(connection->telnet_command==251)
            PutOutput(connection,TO_USER,"%c%c%c",255,254,(int)byte); /* DONT */
         /* Acknowledge our password ECHO announcement without renegotiating.
          * WONT/DONT acknowledgments need no reply, avoiding rejection loops. */
         break;
      case TELNET_SUBNEG:
         if(byte==255)
            connection->telnet_state=TELNET_SUBNEG_IAC;
         break;
      case TELNET_SUBNEG_IAC:
         connection->telnet_state=byte==240?TELNET_DATA:TELNET_SUBNEG; /* SE */
         /* IAC IAC is a payload byte, not the end of the subnegotiation.
          * Malformed embedded commands are ignored until an actual IAC SE. */
         break;
      default:
         connection->telnet_state=TELNET_DATA;
         break;
   }
   return 0;
}
