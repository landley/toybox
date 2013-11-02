/* vconfig.c - Creates virtual ethernet devices.
 *
 * Copyright 2012 Sandeep Sharma <sandeep.jack2756@gmail.com>
 * Copyright 2012 Kyungwan Han <asura321@gmail.com>
 *
 * No standard

USE_VCONFIG(NEWTOY(vconfig, "<2>4", TOYFLAG_NEEDROOT|TOYFLAG_SBIN))

config VCONFIG
  bool "vconfig"
  default y
  help
    usage: vconfig COMMAND [OPTIONS]

       add             [interface-name] [vlan_id]
       rem             [vlan-name]
       set_flag        [interface-name] [flag-num]       [0 | 1]
       set_egress_map  [vlan-name]      [skb_priority]   [vlan_qos]
       set_ingress_map [vlan-name]      [skb_priority]   [vlan_qos]
       set_name_type   [name-type]

    Create and remove virtual ethernet devices
*/

#include "toys.h"
#include "toynet.h"
#include <linux/if_vlan.h>
#include <linux/sockios.h>

static int strtorange(char *str, int min, int max)
{
  char *end = 0;
  long val = strtol(str, &end, 10);

  if (end && *end && end != str && val >= min && val <= max) return val;

  perror_exit("%s not %d-%d\n", str, min, max);
}

void vconfig_main(void)
{
#define MAX_VLAN_ID 4094
  struct vlan_ioctl_args request;
  char *interface_name = NULL;
  unsigned int name_type = VLAN_NAME_TYPE_PLUS_VID;
  char *cmd;
  int fd = 0;
  int vlan_id = 0;

  if((fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) perror_exit("Can't open socket"); //Use socket instead of open
  memset(&request, 0, sizeof(struct vlan_ioctl_args));		// Null set all the VLAN info's.
  cmd = toys.optargs[0];					// Fetch cmd and proceed.
  if(strcmp(cmd, "set_name_type") == 0) {
    if(strcmp(toys.optargs[1], "VLAN_PLUS_VID") == 0) {
      name_type = VLAN_NAME_TYPE_PLUS_VID;
    } else if(strcmp(toys.optargs[1], "VLAN_PLUS_VID_NO_PAD") == 0) {
      name_type = VLAN_NAME_TYPE_PLUS_VID_NO_PAD;
    } else if(strcmp(toys.optargs[1], "DEV_PLUS_VID") == 0) {
      name_type = VLAN_NAME_TYPE_RAW_PLUS_VID;
    } else if(strcmp(toys.optargs[1], "DEV_PLUS_VID_NO_PAD") == 0) {
      name_type = VLAN_NAME_TYPE_RAW_PLUS_VID_NO_PAD;
    } else perror_exit("ERROR: Invalid name type");

    request.u.name_type = name_type;
    request.cmd = SET_VLAN_NAME_TYPE_CMD;
    if(ioctl(fd, SIOCSIFVLAN, &request) == 0) {
      xprintf("Successful set_name_type for VLAN subsystem\n");
      exit(EXIT_SUCCESS);
    }
    else perror_exit("Failed to set set_name_type:");
  } else {
    interface_name = toys.optargs[1]; // Store interface name.
    if(strlen(interface_name) > 15) perror_exit("ERROR:if_name length can not be greater than 15");
    strcpy(request.device1, interface_name); //we had exited if interface_name length greater than 15, so here it never overflows.
  }

  if(strcmp(cmd, "add") == 0) {
    request.cmd = ADD_VLAN_CMD;
    if(toys.optargs[2]) vlan_id = strtorange(toys.optargs[2], 0, MAX_VLAN_ID);
    else vlan_id = 0;
    request.u.VID = vlan_id;
  } else if(strcmp(cmd, "rem") == 0) {
    request.cmd = DEL_VLAN_CMD;
  } else if(strcmp(cmd, "set_flag") == 0) {
    request.cmd = SET_VLAN_FLAG_CMD;
    if(toys.optargs[2]) request.u.flag = strtorange(toys.optargs[2], 0, 1);
    else request.u.flag = 0;
    if(toys.optargs[3]) request.vlan_qos = strtorange(toys.optargs[3], 0, 7);
    else request.vlan_qos = 0;
  } else if(strcmp(cmd, "set_egress_map") == 0) {
    request.cmd = SET_VLAN_EGRESS_PRIORITY_CMD;
    if(toys.optargs[2]) request.u.skb_priority = strtorange(toys.optargs[2], 0, INT_MAX);
    else request.u.skb_priority = 0;
    if(toys.optargs[3]) request.vlan_qos = strtorange(toys.optargs[3], 0, 7);
    else request.vlan_qos = 0;
  } else if(strcmp(cmd, "set_ingress_map") == 0) {
    request.cmd = SET_VLAN_INGRESS_PRIORITY_CMD;
    if(toys.optargs[2]) request.u.skb_priority = strtorange(toys.optargs[2], 0, INT_MAX);
    else request.u.skb_priority = 0;
    //To set flag we must have to set vlan_qos
    if(toys.optargs[3]) request.vlan_qos = strtorange(toys.optargs[3], 0, 7);
    else request.vlan_qos = 0;
  } else {
    xclose(fd);
    perror_exit("Unknown command %s", cmd);
  }
  if(ioctl(fd, SIOCSIFVLAN, &request) == 0) {
    if(strcmp(cmd, "add") == 0 && vlan_id == 1)
      xprintf("WARNING: VLAN 1 does not work with many switches,consider another number if you have problems.\n");
    xprintf("Successful %s on device %s\n", cmd, interface_name);
  } else perror_exit("Failed to %s, on vlan subsystem %s.", cmd, interface_name);
}
