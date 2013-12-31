/*
  Module:  QYPMAIL.CPP

  Author:  William A. Rossi
           194 Peck St.
           Franklin, MA 02038

           bill@rossi.com

  Version: 1.1

  Release Date: 11-MAR-1997

  Description:  QYPMAIL is a local delivery agent for OS/2 SENDMAIL.
                It has been tested under TCP/IP version 3.0 and 3.5,
                and should work under TCP/IP version 2.0.  It is
                designed to be interoperable with LAMAIL, and the
                various shareware utilities that are also LAMAIL 
                compatible.

  *********************************************************************

  The code contained in this file is in the public domain. Specifically,
  I give to the public domain all rights for future licensing of the 
  source code, all resale rights, and all publishing rights. 

  If you find this code useful, or have any comments I can be reached by
  email and snail mail at the above addresses.

  Disclaimer of Warranty

  THIS PACKAGE IS RELEASED "AS IS" WITHOUT WARRANTY OF ANY KIND, EITHER 
  EXPRESSES OR IMPLIED, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED 
  WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
  YOU ASSUME THE ENTIRE RISK OF USING THE PROGRAM AND THE COST OF ALL 
  NECESSARY SERVICING, REPAIR OR CORRECTION.

  IN NO EVENT UNLESS REQUIRED BY APPLICABLE LAW OR AGREED TO IN WRITING
  WILL THE AUTHOR BE LIABLE TO YOU FOR DAMAGES, INCLUDING ANY 
  GENERAL, SPECIAL, INCIDENTAL OR CONSEQUENTIAL DAMAGES ARISING OUT OF 
  THE USE OR INABILITY TO USE THE PROGRAM (INCLUDING BUT NOT LIMITED
  TO LOSS OF DATA OR DATA BEING RENDERED INACCURATE OR LOSSES SUSTAINED 
  BY YOU OR THIRD PARTIES OR A FAILURE OF THE PROGRAM TO OPERATE WITH 
  ANY OTHER PROGRAMS), EVEN IF THE AUTHOR HAS BEEN ADVISED OF THE 
  POSSIBILITY OF SUCH DAMAGES.

*/


#define INCL_DOSDATETIME
#define INCL_DOSPROCESS
#define INCL_DOSFILEMGR
#include <os2.h>

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>

#ifdef TEST
#define MAIL_SOURCE "D:\\CFILES\\QYPMAIL\\TESTMAIL.TXT"
#endif      

/* Class to manage mail delivery */

class MailData
{
  private:
  void ReadSettings(char **Settings); /* Parse settings in config file */
  void SetMailFname();       /* Create a file name to store mail in */
  public:
  char MailFname[20];
  char MailPath[CCHMAXPATH]; /* Directory to deposit mail in */
  char UpdateInbox;          /* Set to true if inbox needs updating */
  char Command[256];         /* Optional command string */
  
  /* Stuff for building INBOX.NDX for LAMAIL compatibility   */
  char FromUser[9];          /* User mail is from            */
  char FromNode[9];          /* Node mail is from            */
  char MailDate[9];          /* Date received                */
  char MailTime[6];          /* Time received                */
  char RealName[128];        /* User's real name             */
  char Subject[128];         /* Subject line                 */
  char FromAddr[128];        /* From address                 */

  ULONG GetUserSettings(char *UserName); /* Read a users settings from cfg file */
  ULONG ReadMail();  /* Read in the actual mail and process it */
};

/* Globals */

char *EtcPath;


/* void Pad(char *str, int n)
 *
 * Pads a string str to length n by adding space characters.
 *
 */

void Pad(char *str, int n)
{
  int len, i;
  char *p;

  len = strlen(str);
  p = str+len;
  for (i=0; i<(n-len); i++)
    *p++=' ';
  *p = 0;
}

/* void ClearHiddenBit(HFILE fh)
 *
 * Clears the hidden attribute 
 *
 */

ULONG ClearHiddenBit(HFILE fh)
{
  FILESTATUS3 Status;
  ULONG rc;

  rc = DosQueryFileInfo(fh, FIL_STANDARD, &Status, sizeof(Status));
  Status.attrFile &= ~(ULONG)FILE_HIDDEN;
  rc = DosSetFileInfo(fh, FIL_STANDARD, &Status, sizeof(Status));
  return rc;
}

/* ULONG MailData::ReadMail()
 *
 * Reads in an email message and processes it according to the current
 * settings.
 *
 */

ULONG MailData::ReadMail()
{
  FILE *InputFP;                    /* Input file pointer             */
  HFILE OutputFH;                   /* Output file handle             */
  char MailLine[256];               /* Line of input mail text        */
  char OutputFname[CCHMAXPATH];     /* Name of the output file        */
  DATETIME DateTime;                /* Current date and time          */
  char DidSubject = 0, DidFrom = 0; /* Flags indicating if we parsed From and Subject headers */
  ULONG rc;                         /* Return code                    */
  ULONG Action;                     /* Used by DosOpen                */
  ULONG BytesWritten;               /* Used by DosWrite               */

#ifdef TEST
  InputFP = fopen(MAIL_SOURCE, "r");
#else
  InputFP = stdin;                  /* Read mail from stdin */
#endif

  /* Create the output file name by adding the MailFname to the config
     file specifed MailPath */

  strcpy(OutputFname, MailPath);
  strcat(OutputFname, "\\");
  strcat(OutputFname, MailFname);

  /* Open the output file for writing.  Use DosOpen so that we can
     open the file with the hidden attribute set.  This prevents 
     other programs like a POP3 server from seeing the file before
     we are done with it. */
  
  rc = DosOpen(OutputFname,                     /* File name          */
               &OutputFH,                       /* Output file handle */
               &Action,                         /* Action taken       */
               0,                               /* File size          */
               FILE_HIDDEN,                     /* File Attribute     */
               OPEN_ACTION_CREATE_IF_NEW |      /* Open flags         */
                 OPEN_ACTION_REPLACE_IF_EXISTS,
               OPEN_SHARE_DENYREADWRITE |       /* Open mode          */
                 OPEN_ACCESS_WRITEONLY,
               NULL);                           /* EA buffer          */

  if (rc)   /* Return to caller if an error */
    return 1;

  /* Read in the mail -- one line at a time from the input file */

  while (fgets(MailLine, 256, InputFP))
  {
    /* Replace trailing '\n' with "\r\n" */
 
    if (MailLine[strlen(MailLine)-1] == '\n')
    {
      MailLine[strlen(MailLine)-1] = 0;
      strcat(MailLine, "\r\n");
    }

    /* Write the mail line out to the output file */
    DosWrite(OutputFH, MailLine, strlen(MailLine), &BytesWritten); 

    /* Process the From: header when we come across it */
    if (strncmp(MailLine, "From:", 5) == 0 && !DidFrom)
    {
      char *p, *q, *id;
      int i;

      DidFrom = 1;     /* Note that we have already processed the From line */
      *FromAddr = 0;
      *FromUser = 0;
      *FromNode = 0;
 
      /* Find the from address.  Do this by searching for the '@' character
         and then working backwards towards a whitespace character, then
         we will have the user name */

      p = strstr(MailLine, "@");  /* Search for '@' */
      if (p)
      {
        while (p!=MailLine && *p != ' ')
          p--;
        if (*p == ' ')
          p++;
    
        id = p;
        if (*p == '<')
          p++; 
        q = FromAddr;        /* Copy the From address */ 
        while ( *p > ' ')
          *q++ = *p++;
        *q = 0;
        if (*(q-1) == '>')
          *(q-1) = 0;

        p = id;             /* Get the from user */
        if (*p == '<')
          p++;
        q = FromUser;
        i = 0;
        while ( *p != '@')
        {
          *q++ = *p++;
          i++;
          if (i>7)
            break;
        }
        *q = 0;

        p++;
        q = FromNode;       /* Get the from node */
        i = 0;
        while ( *p != '.' && *p > ' ')
        {
          *q++ = *p++;
          i++;
          if (i>7)
            break;
        }
        *q = 0;
      }


      /* Try to get the real name.  Assuming that the address is 
         of the form: RealName <user@domain> */

      strcpy(RealName, FromAddr);  /* Get the from address */
      p = strstr(MailLine, "<"); 
      if (p)
      {
        *p = 0;
        strcpy(RealName, MailLine+6);
      }
    }

    /* Process the Subject: header line when we come across it */

    if (strncmp(MailLine, "Subject:", 8) == 0 && !DidSubject)
    {
      char *p;

      DidSubject = 1; 
      for (p=MailLine; *p; p++)
        if (*p < ' ')
          *p = 0;

      strncpy(Subject, MailLine+9, 100);
    }
  } /* end while() */

  /* Make sure we end the mail with a blank line */

  strcpy(MailLine, "\r\n");
  DosWrite(OutputFH, MailLine, strlen(MailLine), &BytesWritten); 


  DosResetBuffer(OutputFH);  /* Flush the file */
  ClearHiddenBit(OutputFH);  /* Clear the hidden bit */ 
  DosClose(OutputFH);   /* Close the output mail file */

  /* Update the LAMAIL style inbox.ndx file if the config file says
     we should */

  if (UpdateInbox)
  {
    FILE *InboxFP;
    char InboxFname[CCHMAXPATH];
    int i;

    DosGetDateTime(&DateTime);
    sprintf(MailDate, "%2.2d/%2.2d/%2.2d", DateTime.year%100, DateTime.month, DateTime.day);
    sprintf(MailTime, "%2.2d:%2.2d", DateTime.hours, DateTime.minutes);

    strcpy(InboxFname, MailPath);
    strcat(InboxFname, "\\INBOX.NDX");
  
    /* Inbox file may be temporarily locked by a mail reader program.
       Attempt to open it 5 times then fail */ 
    for (i=0; i<5; i++)
    {
      InboxFP = fopen(InboxFname, "a");
      if (InboxFP)
        break;
      DosSleep(1000);  /* Wait one second */
    }
 
    if (InboxFP == NULL)
      return 1;
   
    Pad(FromUser, 8);
    Pad(FromNode, 8);
    MailFname[8] = 0; 
    fprintf(InboxFP, "         %s %s %s %s              %s %s                %s"
                     "\001%s\001\001%s\n", FromUser, FromNode, MailFname,
                     MailFname+9, MailDate, MailTime, RealName, Subject,
                     FromAddr);
    MailFname[8] = '.';
    fclose(InboxFP); 
  }

#ifdef TEST
  printf("From User: %s\n", FromUser);
  printf("From Node: %s\n", FromNode);
  printf("Real Name: %s\n", RealName);
  printf("Subject:   %s\n", Subject);
  printf("From Addr: %s\n", FromAddr);
  printf("Mail Date: %s\n", MailDate);
  printf("Mail Time: %s\n", MailTime);
#endif

  if (*Command)   /* If there is a command  to be issued */
  { 
    char CommandStr[256];
    char *p, *q;

    p = Command;
    q = CommandStr;
 
    while (*p)
    {
      if (*p == '%')
      {
        switch (p[1]) 
        {
          case 'N':  /* Macro for mail file name */
            *q = 0;
            strcpy(q, OutputFname);
            q = q+strlen(q);
            p+=2;
            break;
     
          case 'F':  /* Macro for from address */
            *q = 0;
            strcpy(q, FromAddr);
            q = q+strlen(q);
            p+=2;
            break;

          default:
            *q++ = *p++;
            *q++ = *p++;
            break;
        }
      }
      else
      {
        *q++ = *p++;
      }
    }
#ifdef TEST
    printf("Command: %s\n", CommandStr);
#endif
    system(CommandStr);   /* Do the command */
  }

  return 0;
}  

void MailData::SetMailFname()
{
  char Base36Set[] = "ABCDEFGHIJKLMNOPQRSTUVWXY"
		     "Z0123456789";
  int i = 0;
  time_t Time;
  DATETIME DateTime;

  Time = time(NULL);
  DosGetDateTime(&DateTime);
 
  MailFname[0] = Base36Set[DateTime.hundredths/3];

  for (i=0; i<7; i++)
  {
    MailFname[i+1] = Base36Set[Time%36];
    Time = Time / 36;
  }
  MailFname[8] = 0;
  strcat(MailFname, ".MA_");
}


void MailData::ReadSettings(char **Settings)
{
  strcpy(MailPath, Settings[1]);  /* Get Mail path */
  strupr(Settings[2]);
  if (*Settings[2] == 'Y')
    UpdateInbox = 1;
  else
    UpdateInbox = 0;

  strcpy(Command, Settings[3]);
}


ULONG MailData::GetUserSettings(char *UserName)
{
  char CfgFname[CCHMAXPATH];
  char User[80];
  char buf[80];
  char MatchFound = 0;
  char *p;
  char *Settings[4];
  int i;
  FILE *CfgFP;
  
  /* Get the user name */

  strcpy(User, UserName);
  strupr(User);

  /* Open the configuration file */
  strcpy(CfgFname, EtcPath);
  strcat(CfgFname, "\\QYPMAIL.CFG");

  CfgFP = fopen(CfgFname, "r");
  if (CfgFP == NULL)
    return 1;

  while (fgets(buf, 80, CfgFP))
  {
    for (p=buf; *p; p++)   /* Remove trailing '\n' and '\r' chars */
      if (*p == '\n' || *p == '\r')
      {
        *p = 0;
        break;
      }

    /* Remove trailing spaces */
    for (p=buf+strlen(buf)-1; *p==' '; p--)
      *p = 0;

    /* Break up config line into fields.  Line is comma delimetied */
    p = strstr(buf, ",");
    i = 0;
    Settings[0] = buf;

    while (p != NULL)  /* Replace commas with NULLs */
    {
      *p++ = 0;
      i++;
      Settings[i] = p;
      p = strstr(p, ",");
    }

    if (i != 3)   /* If all fields not specified */
      continue;

    strupr(Settings[0]);  /* First setting is the user name */
    if (strcmp(Settings[0], User) == 0)
    {
      MatchFound = 1;
      ReadSettings(Settings);
    }

    if (strcmp(Settings[0], "DEFAULT") == 0 && !MatchFound)
      ReadSettings(Settings);
  }

  fclose(CfgFP);  /* Close the configuration file */

  SetMailFname();

  return 0;
}


/* Command line arguments -- Takes two args -- the prog. name, the userid */

int main(int argc, char *argv[])
{
  char MailFname[CCHMAXPATH];
  MailData *Mail;
 
  if (argc != 3)
  {
    int i;
    printf("QYPMAIL: Invalid number of arguments\nQYPMAIL: ");
    for (i=0; i<argc; i++)
      printf(" \"%s\" ", argv[i]);
    printf("\n");
    return 1;
  }

  /* Get stuff from the environment */

  EtcPath = getenv("ETC");
  if (EtcPath == NULL)
  {
    printf("ETC environment variable not defined!!!\n");
    return 1;
  }

  Mail = new MailData;
  if (Mail->GetUserSettings(argv[2]))
  {
    delete Mail;
    return 1;
  }

  if (Mail->ReadMail())
  {
    delete Mail;
    return 1;
  }

  delete Mail;
  return 0;
}
