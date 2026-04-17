// Common station data structures shared by both data clients
#pragma once
#include <Arduino.h>

#define MAXBOARDMESSAGES 4
#define MAXMESSAGESIZE 400
#define MAXCALLINGSIZE 450
#define MAXBOARDSERVICES 9
#define MAXLOCATIONSIZE 85
#define MAXBUSTUBELOCATIONSIZE 50
#define MAXFILTERSIZE 25
#define MAXLINESIZE 20
#define MAXTUBEBUSREADSERVICES 20

#define MAXKEYNAMESIZE 50
#define MAXRESULTMESSAGESIZE 80

#define MAXWEATHERSIZE 50

#define OTHER 0
#define TRAIN 1
#define BUS 2


struct stnMessages {
    int numMessages;
    char messages[MAXBOARDMESSAGES][MAXMESSAGESIZE];
};

struct rdService {
    char sTime[6];
    char destination[MAXLOCATIONSIZE];
    char via[MAXLOCATIONSIZE];  // also used for line name for TfL
    char etd[11];
    char platform[4];
    bool isCancelled;
    bool isDelayed;
    int trainLength;
    byte classesAvailable;
    char opco[50];

    int serviceType;
    int timeToStation;  // Only for TfL
  };

  struct rdStation {
    char location[MAXLOCATIONSIZE];
    bool platformAvailable;
    int numServices;
    bool boardChanged;  // Only for TfL
    char calling[MAXCALLINGSIZE];   // Only store the calling stops for the first service returned
    char origin[MAXLOCATIONSIZE]; // Only store the origin for the first service returned
    char serviceMessage[MAXMESSAGESIZE];  // Only store the service message for the first service returned
    rdService service[MAXBOARDSERVICES];
  };

  // Rail structure for data downloads
  struct rdiService {
    char sTime[6];
    char destination[MAXLOCATIONSIZE];
    char via[MAXLOCATIONSIZE];
    char origin[MAXLOCATIONSIZE];
    char etd[11];
    char platform[4];
    bool isCancelled;
    bool isDelayed;
    int trainLength;
    byte classesAvailable;
    char opco[50];
    char calling[MAXCALLINGSIZE];
    char serviceMessage[MAXMESSAGESIZE];
    int serviceType;
    char serviceID[18];
    char sortTime[6];
  };

  struct rdiStation {
    char location[MAXLOCATIONSIZE];
    bool platformAvailable;
    int numServices;
    rdiService service[MAXBOARDSERVICES];
  };

  // Common structure for tube/bus data downloads
    struct busTubeService {
      char destinationName[MAXBUSTUBELOCATIONSIZE];
      char currentLocation[MAXBUSTUBELOCATIONSIZE];
      char lineName[MAXLINESIZE];
      int timeToStation;
      char scheduled[6];
      char expected[6];
  };

  struct busTubeStation {
      int numServices;
      busTubeService service[MAXTUBEBUSREADSERVICES];
  };

  // Common data buffers for parsing JSON
  struct sharedBufferSpace {
    char currentKey[MAXKEYNAMESIZE];
    char objectCurrentKey[MAXKEYNAMESIZE];
    char currentPath[(MAXKEYNAMESIZE*2+1)];
    char arrayName[(MAXKEYNAMESIZE*2)+1];
    char lastResultMessage[MAXRESULTMESSAGESIZE];
  };
