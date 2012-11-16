/* vi: set sw=4 ts=4:
 *
 * touch.c : change timestamp of a file
 *  Copyright 2012 Choubey Ji <warior.linux@gmail.com>
 *
 * See http://pubs.opengroup.org/onlinepubs/9699919799/utilities/touch.html 

USE_TOUCH(NEWTOY(touch, "mrt", TOYFLAG_BIN))

config TOUCH
  bool "th"
  default y
  help
    Usage: Usage: touch [OPTION]... FILE...
    Update the access and modification times of each FILE to the current time.
    -m                     change only the modification time
    -r, --reference=FILE   use this file's times instead of current time
    -t STAMP               use [[CC]YY]MMDDhhmm[.ss] instead of current time

*/


#include "toys.h"

time_t get_time_sec(char *);

void touch_main(void){
  int fd, touch_flag_t = 0, touch_flag_m = 0, touch_flag_r = 0;
  time_t now;
  struct utimbuf modinfo;
  struct stat filestat;    
  if(!toys.optflags){
    time(&now);
    modinfo.modtime = now;
    modinfo.actime = now;
    }else{
      if(toys.optflags & 1){
        touch_flag_t = 1;
        if((toys.optflags >> 2) & 1){
          touch_flag_m = 1;
        }
      }

      if((toys.optflags >> 1) & 1)
        touch_flag_r = 1;

      if(toys.optflags >> 2)
        touch_flag_m = 1;
    }

  if(touch_flag_t){
    if(!check_date_format(toys.optargs[0])){
      modinfo.modtime = get_time_sec((char *)toys.optargs[0]);
      modinfo.actime = get_time_sec(toys.optargs[0]);
    }else{
      perror_msg("Invalid date format, -t [yyyyMMddhhmm.ss]");
      toys.exitval = EXIT_FAILURE;
    }
  }
  if(touch_flag_r){
    if(stat(toys.optargs[0], &filestat) < 0){
      printf("Error : unable to get information for file %s\n", toys.optargs[0]);
      toys.exitval = EXIT_FAILURE;
    }
    modinfo.modtime = filestat.st_mtime;
    modinfo.actime = filestat.st_atime;
  }
  if(touch_flag_m){
    if(stat(toys.optargs[toys.optc - 1], &filestat) < 0){
      toys.exitval = EXIT_FAILURE;
      return;
    }
    modinfo.actime = filestat.st_atime;
    if(!(touch_flag_r | touch_flag_t)){
      time(&now);
      modinfo.modtime = now;
    }
  }
  if(utime(toys.optargs[toys.optc - 1], &modinfo) == -1){
    if((fd = open(toys.optargs[toys.optc - 1],O_CREAT |O_RDWR, 0644)) != -1){
      close(fd);
      utime(toys.optargs[toys.optc - 1], &modinfo);
    }else{
      perror("unable to create the file");
      toys.exitval = EXIT_FAILURE;
    }
  }
}

int check_date_format(char *date_input){
  int count_date_digit = 0;
  unsigned long long flag_b4_sec;
  int  flag_af_sec;
  char *date_store = (char *)malloc(12 * sizeof(char));
  while(date_input[count_date_digit] != '.'){
    date_store[count_date_digit] = date_input[count_date_digit];
    count_date_digit++;
  }
  date_store[count_date_digit++] = '\0';
  flag_b4_sec = atoll(date_store);
  date_store[0] = date_input[count_date_digit++];
  date_store[1] = date_input[count_date_digit];
  date_store[2] = '\0';
  flag_af_sec = atoi(date_store);
  if(date_store[0] == '0' && date_store[1] == '0')
    flag_af_sec = 1;
  if(flag_b4_sec && flag_af_sec)
    return 0;
  else
    return -1;
}

/* function to return number of seconds since epoch till the given date */
time_t get_time_sec(char *date_input){
  int count_date_digit = 0;
  char temp_date[12];
  char mm[2];
  char dd[2];
  char hh[2];
  char ss[2];
  char year[4];
  time_t time_of_modify;
  struct tm t_yyyymmddhhss;
  while(date_input[count_date_digit] != '.'){
    if(count_date_digit < 4){
      year[count_date_digit] = date_input[count_date_digit];
    }
    count_date_digit++;
    if(count_date_digit == 4){
      year[count_date_digit] = '\0';
      t_yyyymmddhhss.tm_year = atoi(year)-1900;
      break;
    }
  }
  mm[0] = date_input[4];
  mm[1] = date_input[5];
  mm[2] = '\0';
  t_yyyymmddhhss.tm_mon = (atoi(mm) - 1);
  mm[0] = date_input[6];
  mm[1] = date_input[7];
  mm[2] = '\0';
  t_yyyymmddhhss.tm_mday = atoi(mm);
  mm[0] = date_input[8];
  mm[1] = date_input[9];
  mm[2] = '\0';
  t_yyyymmddhhss.tm_hour = atoi(mm);
  mm[0] = date_input[10];
  mm[1] = date_input[11];
  mm[2] = '\0';
  t_yyyymmddhhss.tm_min = atoi(mm);
  mm[0] = date_input[13];
  mm[1] = date_input[14];
  mm[2] = '\0';
  t_yyyymmddhhss.tm_sec = atoi(mm);
  time_of_modify = mktime(&t_yyyymmddhhss);
  return time_of_modify;
}
