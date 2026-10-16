/******************************************************************************
 Program     : The Gladiator Pits.
 Author      : Richard Woolcock (aka KaVir).
 Last Update : 28th April 2000.
 ******************************************************************************
 File        : commands.c
 Raw size    : 5928 bytes (after going through Erwin's line counter).
 Description : The commands for the Gladiator Pits mud.
 ******************************************************************************
 This code is copyright (C) 2000 by Richard Woolcock.  It may be used and
 distributed freely, as long as you don't remove this copyright notice.
 ******************************************************************************/

CMD(Quit)
{
   (void)szTxt;
   /* Exit message, if required (had to remove due to space restrictions) */

   /* Let the loop close it after input processing has returned safely. */
   pstConn->bClosing=TRUE;
}

CMD(Shutdown)
{
   (void)szTxt;
   if(!ConnectionCan(pstConn,PERM_SHUTDOWN))
      PutOutput(pstConn,TO_USER,"Permission denied.\n\r");
   else
   {
      /* Shutdown message */
      PutOutput(pstConn,TO_ALL,"[Shutdown by %s]\n\r",
         NAME(pstConn));
      Log("Shutdown by account %s.\n",pstConn->account_name);

      /* Toggle the flag used within the GameLoop function */
      bShutdown=TRUE;
   }
}

CMD(Create)
{
   int i=0;
   body_t *pstBody;
   FILE *pPlayerFile;
   bool bValid;
   struct stat stInfo;
   int iExisting;

   /* If there is no input with the command, send a message and return */
   NO_INPUT("Syntax: create <name>\n\r");

   if(strlen(szTxt)<3||strlen(szTxt)>15)
   {
      PutOutput(pstConn,TO_USER,"Names must be 3-15 letters long.\n\r");
      return;
   }
   if(!ValidPlayerName(szTxt))
   {
      PutOutput(pstConn,TO_USER,"Name must only contain letters.\n\r");
      return;
   }
   if(FindPerson(szTxt))
   {
      PutOutput(pstConn,TO_USER,"That name is in use.\n\r");
      return;
   }
   if(!ConnectionCan(pstConn,PERM_PLAY))
   {
      PutOutput(pstConn,TO_USER,"Authenticate before loading a character.\n\r");
      return;
   }
   for(i=0;szTxt[i];i++)
      szTxt[i]=(char)tolower((unsigned char)szTxt[i]);
   szTxt[0]=(char)toupper((unsigned char)szTxt[0]);
   iExisting=lstat(szTxt,&stInfo)==0;
   if((!iExisting&&errno!=ENOENT)||(iExisting&&!S_ISREG(stInfo.st_mode)))
   {
      PutOutput(pstConn,TO_USER,"Unable to inspect character file.\n\r");
      return;
   }
   if(!AccountCharacter(pstConn->account,szTxt,iExisting))
   {
      PutOutput(pstConn,TO_USER,"Character unavailable or not linked to your account. "
         "Existing unlinked saves require local staff approval.\n\r");
      return;
   }
   i=0;
   if(!(pstBody=(body_t*)malloc(sizeof(body_t))))
   {
      Log("BUG: Cannot malloc.\n");
      return;
   }
   else/* The name was acceptable and a body was created */
   {
      do /* loop through the name */
      {
         /* Convert the letters to lower case, if they're not already */
         szTxt[i]=(char)tolower((unsigned char)szTxt[i]);
      }
      while(szTxt[++i]);

      /* Make the first letter of the name uppercase */
      szTxt[0]=(char)toupper((unsigned char)szTxt[0]);

      /* Look for an existing player file */
      if((pPlayerFile=fopen(szTxt,"rb")))
      {
         /* Reject damaged or incompatible records without overwriting them. */
         bValid=fread(pstBody,sizeof(body_t),1,pPlayerFile)==1&&
            fgetc(pPlayerFile)==EOF&&!ferror(pPlayerFile)&&
            ValidPlayer(pstBody,szTxt);

         /* Close the player file */
         fclose(pPlayerFile);
         if(!bValid)
         {
            free(pstBody);
            PutOutput(pstConn,TO_USER,"Player file is damaged or incompatible; it was not changed.\n\r");
            Log("Rejected invalid player file for %s.\n",szTxt);
            return;
         }

         /* Clear their pointer variable */
         pstBody->pstOpponent=NULL;

         /* Set the connection to GLADIATOR status */
         pstConn->eStatus=GLADIATOR;

         /* Start them in the stadium */
         pstBody->iRoom=0;

         /* Log the load */
         Log("%s has rejoined.\n",szTxt);
      }
      else/* No player file exists yet for that name */
      {
         if(errno!=ENOENT)
         {
            free(pstBody);
            PutOutput(pstConn,TO_USER,"Unable to read that player file; it was not changed.\n\r");
            Log("Cannot read player file for %s.\n",szTxt);
            return;
         }
         /* Clear the body structure */
         *pstBody=stBodyEmpty;

         /* The connection is now a CITIZEN */
         pstConn->eStatus=CITIZEN;

         /* Initialise the body's stats to 1 */
         for(i=STR;i<=WIT;i++)
            pstBody->iStats[i]=1;

         /* Store the name of the body */
         strcpy(pstBody->a_chName,szTxt);

         /* Log the new body's creation */
         Log("New person '%s' created.\n",szTxt);
      }

      /* Attach only a fully initialized and validated character. */
      pstConn->pstBody=pstBody;
      /* Display the message to the user and others in the room */
      PutOutput(pstConn,TO_USER,"You step from the crowd.\n\r");
      PutOutput(pstConn,TO_ROOM,"%s steps from the crowd.\n\r",szTxt);
   }
}

CMD(Say)
{
   /* If there is no input with the command, send a message and return */
   NO_INPUT("Syntax: say <sentence>\n\r");

   /* Display the message to the user and others in the room */
   PutOutput(pstConn,TO_USER,"You say '%s'\n\r",szTxt);
   PutOutput(pstConn,TO_ROOM,"%s says '%s'\n\r",NAME(pstConn),szTxt);
}

CMD(Chat)
{
   /* If there is no input with the command, send a message and return */
   NO_INPUT("Syntax: chat <sentence>\n\r");

   /* Display the message to the user and others in the room */
   PutOutput(pstConn,TO_USER,"You chat '%s'\n\r",szTxt);
   PutOutput(pstConn,TO_WORLD,"%s chats '%s'\n\r",NAME(pstConn),szTxt);
}

CMD(Emote)
{
   /* If there is no input with the command, send a message and return */
   NO_INPUT("Syntax: emote <action>\n\r");

   /* Display the message to the user and others in the room */
   PutOutput(pstConn,TO_USER,"%s %s\n\r",NAME(pstConn),szTxt);
   PutOutput(pstConn,TO_ROOM,"%s %s\n\r",NAME(pstConn),szTxt);
}

CMD(Commands)
{
   int i,iCount=0;
   (void)szTxt;

   /* Loop through all the commands in the command table */
   for(i=0;kstCmdTable[i].szCommand!=NULL;i++)
   {
      /* Display the command if the connection has access to it */
      if((pstConn->eStatus&kstCmdTable[i].eType)&&
         ConnectionCan(pstConn,kstCmdTable[i].permission))
         PutOutput(pstConn,TO_USER,"%-20s%s",kstCmdTable[i].szCommand,
            (!(++iCount%4))?"\n\r":"");
   }

   /* Add an extra newline if necessary, for formatting purposes */
   if ((iCount%4))
      PutOutput(pstConn,TO_USER,"\n\r");
}

CMD(Who)
{
   conn_t*pstUser;
   body_t*pstBody;
   int iCrowd=0,iCount=0;
   (void)szTxt;

   /* Loop through all of the connections */
   for(pstUser=pstFirstConn;pstUser;pstUser=pstUser->pstNext)
   {
      /* Check if the appropriate connection has a body */
      if ((pstBody=pstUser->pstBody)!=NULL)
      {
         /* If it DOES have a body, display it's name, wins, losses and kills */
         PutOutput(pstConn,TO_USER,"%-20s (Won:%d Lost:%d Kills:%d)\n\r", 
            pstBody->a_chName,pstBody->iWin,pstBody->iLoss,pstBody->iKill);

         /* Add one to the "people who have bodies" counter */
         iCount++;
      }
      else /* Add one to the "people who don't have bodies" counter */
         iCrowd++;
   }

   /* Display the total number of people with and without bodies */
   PutOutput(pstConn,TO_USER,"Total of %d visible %s, with %d other %s in the crowd.\n\r",
      iCount,iCount==1?"person":"people",iCrowd,iCrowd==1?"person":"people");
}

/* Revocation may precede the next poll's closing flag. Recheck current
 * permissions as well as transport state before creating combat links. */
static int ChallengeUnavailable(conn_t *first,conn_t *second)
{
   return CombatPendingDisconnect(first,second)||
      !ConnectionCan(first,PERM_PLAY)||!ConnectionCan(second,PERM_PLAY);
}

CMD(Challenge)
{
   conn_t*pstUser;

   /* If there is no input with the command, send a message and return */
   NO_INPUT("Syntax: challenge <gladiator>\n\r");

   NEED_BODY; /* Logs an error and returns, if pstConn has no body */

   if (!(pstUser=FindPerson(szTxt))||pstUser==pstConn)
      PutOutput(pstConn,TO_USER,"No such gladiator as '%s'.\n\r",szTxt);
   else if(pstUser->eStatus!=GLADIATOR&&pstUser->eStatus!=CHALLENGER)
      PutOutput(pstConn,TO_USER,"They're no gladiator!\n\r");
   else if (ROOM(pstConn)||ROOM(pstUser))
      PutOutput(pstConn,TO_USER,"You must both be in the stadium.\n\r");
   else if(ChallengeUnavailable(pstConn,pstUser))
      PutOutput(pstConn,TO_USER,"That gladiator is unavailable; no challenge made.\n\r");
   else
   {
      /* Replacing either side must not leave a dangling old challenge. */
      ClearChallenge(pstConn);
      ClearChallenge(pstUser);

      /* Send the challenge message */
      PutOutput(pstConn,TO_USER,"You challenge %s to a fight.\n\r",szTxt);
      PutOutput(pstUser,TO_USER,"%s challenges you to a fight.\n\r",
         pstConn->pstBody->a_chName);

      /* Message enqueue can discover a failed socket before admission. */
      if(ChallengeUnavailable(pstConn,pstUser))
         return;

      /* Indicate that you're now the challenger */
      pstConn->eStatus=CHALLENGER;
      pstUser->eStatus=GLADIATOR;

      /* Set the two players as opponents */
      pstConn->pstBody->pstOpponent=pstUser;
      pstUser->pstBody->pstOpponent=pstConn;
   }
}

/* Private transient cursor, not part of a saved player record. */
static int arena_sequence;

static int AvailableArena(void)
{
   int attempt;
   /* Active connections are descriptor-bounded. Within that many candidates
    * a valid world must have a free ID; keep an explicit defensive work bound. */
   for(attempt=0;attempt<FD_SETSIZE;attempt++)
   {
      conn_t *user;
      arena_sequence=arena_sequence<0||arena_sequence==INT_MAX?1:arena_sequence+1;
      for(user=pstFirstConn;user;user=user->pstNext)
         if(ROOM(user)==arena_sequence)
            break;
      if(!user)
         return arena_sequence;
   }
   return 0;
}

CMD(Accept)
{
   conn_t*pstUser;
   int iArena;
   (void)szTxt;

   NEED_BODY; /* Logs an error and returns, if pstConn has no body */

   if (!(pstUser=pstConn->pstBody->pstOpponent)||!pstUser->pstBody||
       pstUser->pstBody->pstOpponent!=pstConn||pstUser->eStatus!=CHALLENGER)
      PutOutput(pstConn,TO_USER,"You've not been challenged.\n\r");
   else if(ChallengeUnavailable(pstConn,pstUser))
      PutOutput(pstConn,TO_USER,"That challenge is no longer available.\n\r");
   else
   {
      iArena=AvailableArena();
      if(!iArena)
      {
         PutOutput(pstConn,TO_USER,"All arenas are busy; try again later.\n\r");
         return;
      }
      /* Send the acceptance message */
      PutOutput(pstConn,TO_USER,"Ok.\n\r");
      PutOutput(pstUser,TO_USER,"%s accepts your challenge!\n\r",
         pstConn->pstBody->a_chName);
      if(ChallengeUnavailable(pstConn,pstUser))
         return;
      PutOutput(pstConn,TO_ALL,"[%s and %s have entered the arena]\n\r",
         pstConn->pstBody->a_chName,pstUser->pstBody->a_chName);

      /* A full queue during the broadcast must not create a surrender win. */
      if(ChallengeUnavailable(pstConn,pstUser))
         return;

      /* Heal both gladiators */
      pstConn->pstBody->iDam=pstUser->pstBody->iDam=0;

      /* Reset both speed counters */
      pstConn->pstBody->iSpeed=pstUser->pstBody->iSpeed=0;

      /* Move both gladiators into an arena */
      pstConn->pstBody->iRoom=pstUser->pstBody->iRoom=iArena;

      /* Set both gladiators to fighting */
      pstConn->eStatus=pstUser->eStatus=FIGHTING;
   }
}

CMD(Train)
{
   (void)szTxt;
   NEED_BODY; /* Logs an error and returns, if pstConn has no body */

   /* The connection is now in TRAINING */
   pstConn->eStatus=TRAINING;

   /* Display the message to the user and others in the room */
   PutOutput(pstConn,TO_USER,"You walk to the training room.  To get back, type 'leave'.\n\r");
   PutOutput(pstConn,TO_ROOM,"%s walks to the training room.\n\r",
      pstConn->pstBody->a_chName);

   /* Indicate that the connection is in the training room (-1) */
   pstConn->pstBody->iRoom=-1;

   /* Display the message to the people already in the training room */
   PutOutput(pstConn,TO_ROOM,"%s enters the training room.\n\r", 
      pstConn->pstBody->a_chName);
}

CMD(Leave)
{
   int i,iCount=0;
   (void)szTxt;

   NEED_BODY; /* Logs an error and returns, if pstConn has no body */

   /* Calculate their total number of points worth of stats */
   for(i=STR;i<=WIT;i++)
      iCount+=pstConn->pstBody->iStats[i];

   /* Persist the departure before announcing success or changing live state. */
   if(iCount>=MAX_TRAIN)
   {
      body_t saved=*pstConn->pstBody;
      saved.iRoom=0;
      if(!Save(&saved))
         return;
   }

   /* Display the message to the user and others in the room */
   PutOutput(pstConn,TO_USER,"You leave the training room.\n\r");
   PutOutput(pstConn,TO_ROOM,"%s leaves the training room.\n\r", 
      pstConn->pstBody->a_chName);

   /* Indicate that the connection is now back at the stadium (0) */
   pstConn->pstBody->iRoom=0;

   /* Display the message to the people already at the stadium */
   PutOutput(pstConn,TO_ROOM,"%s arrives from the training room.\n\r", 
      pstConn->pstBody->a_chName);

   /* If they've spent all their points, they become a gladiator */
   if(iCount>=MAX_TRAIN)
   {
      pstConn->eStatus=GLADIATOR;

   }
   else/* Otherwise they're still a CITIZEN */
      pstConn->eStatus=CITIZEN;
}

CMD(Str)
{
   (void)szTxt;
   /* Increase the connection's strength, if possible */
   TrainStat(pstConn,"strength",STR);
}

CMD(Dex)
{
   (void)szTxt;
   /* Increase the connection's dexterity, if possible */
   TrainStat(pstConn,"dexterity",DEX);
}

CMD(Sta)
{
   (void)szTxt;
   /* Increase the connection's stamina, if possible */
   TrainStat(pstConn,"stamina",STA);
}

CMD(Siz)
{
   (void)szTxt;
   /* Increase the connection's size, if possible */
   TrainStat(pstConn,"size",SIZ);
}

CMD(Wit)
{
   (void)szTxt;
   /* Increase the connection's wits, if possible */
   TrainStat(pstConn,"wits",WIT);
}

CMD(Score)
{
   body_t*pstBody=pstConn->pstBody;
   (void)szTxt;

   /* Display the score to the connection */
   PutOutput(pstConn,TO_USER,
      "<<<<<===--------[ Score ]--------===>>>>>\n\r"
      "    Name:%-15s Status:%s\n\r"
      "       Win-Loss-Kill: %2d - %2d - %2d\n\r"
      "<<<<<===-------------------------===>>>>>\n\r"
      "      Str:%d Dex:%d Sta:%d Siz:%d Wit:%d\n\r"
      "           Att:%-2d Def:%-2d Dam:%2d\n\r"
      "          Wounds:%-2d (%-2d) Speed:%d\n\r"
      "<<<<<===-------------------------===>>>>>\n\r",
      pstBody->a_chName,
      StatusName(pstBody),
      pstBody->iWin,
      pstBody->iLoss,
      pstBody->iKill,
      pstBody->iStats[STR],
      pstBody->iStats[DEX],
      pstBody->iStats[STA],
      pstBody->iStats[SIZ],
      pstBody->iStats[WIT],
      ATTACK(pstBody),
      DEFENCE(pstBody),
      DAMAGE(pstBody),
      pstBody->iDam,
      HEALTH(pstBody),
      SPEED(pstBody));
}

