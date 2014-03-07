/*
** Copyright 2013 Carnegie Mellon University / ETH Zurich
** 
** Licensed under the Apache License, Version 2.0 (the "License"); 
** you may not use this file except in compliance with the License. 
** You may obtain a copy of the License at 
** 
** http://www.apache.org/licenses/LICENSE-2.0 
** 
** Unless required by applicable law or agreed to in writing, software
** distributed under the License is distributed on an "AS IS" BASIS, 
** WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. 
** See the License for the specific language governing permissions and 
** limitations under the License.
*/

#ifndef SCIONPRINT_HEADER_HH_
#define SCIONPRINT_HEADER_HH_

#include <click/element.hh>
#include <click/glue.hh>
#include <click/atomic.hh>
#include <click/standard/scheduleinfo.hh>
#include <click/timer.hh>
#include <click/notifier.hh>
#include <click/task.hh>
#include <map>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <iostream>
#include <fstream>
#include <sys/time.h>
/*include here*/
#include "packetheader.hh"
#include "define.hh"
CLICK_DECLS
using namespace std;
enum LogType{
    EH=1,
    EM,
    EL,
    WH=101,
    WM,
    WL,
    IH=201,
    IM,
    IL,
};


class SCIONPrint{ 
 public :
  SCIONPrint();

  SCIONPrint(int msgLevel, const char* logFileName);

  ~SCIONPrint();

  void printLog(uint32_t ts, char* fmt, ...);
  void printLog(char* fmt, ...);
  void printLog(int logType, char* fmt, ...);
  void printLog(int logType, int msgType, char* fmt, ...);
  void printLog(int logType, int msgType, uint32_t ts, char* fmt, ...);
  void printLog(int logType, int msgType, uint32_t ts, uint64_t src,
      uint64_t dst, char* fmt, ...);

  void printLog(int logType, int msgType, uint32_t ts, HostAddr src,
  HostAddr dst, char* fmt, ...);
  void printMainLog();

  void printSrcDst(uint64_t src, uint64_t dst);   
  void printTimestamp(uint32_t ts); 
  void backup();

  //SL: read the current log-file size (in # of lines).
  void linecount(int &i);
 private:
  int m_iLineNum;
  int m_iMaxLineNum;
  FILE* m_logFile; // Does not need to be deleted because fclose() is
                   // called in the source file.
  char m_csLogFileName[MAX_FILE_LEN];
  void printMsgType(int msgType);
  void printLogType(int logType);
  int m_iMsgLevel;
};

CLICK_ENDDECLS
#endif