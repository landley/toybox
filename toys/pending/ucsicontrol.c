/* ucsicontrol.c - control/query USB Type-C Connector System Software Interface
 *
 * Copyright 2026 Madhu M <madhu.m@intel.com>
 *
 * Talks directly to the kernel UCSI debugfs interface exposed under
 * /sys/kernel/debug/usb/ucsi/.../{command,response}.

USE_UCSICONTROL(NEWTOY(ucsicontrol, 0, TOYFLAG_USR|TOYFLAG_SBIN|TOYFLAG_NEEDROOT))

config UCSICONTROL
  bool "ucsicontrol"
  default n
  help
    Usage: ucsicontrol [OPTIONS]
    Note: ucsicontrol must be run as root.
    Options:
      --help, -h                            Show this help message
      --conn_rst <conn_num> <soft/hard>     Reset the Connector
      --get_cap                             Get Capabilities
      --get_conn_cap <conn_num>             Get Connector Capability
      --get_conn_sts <conn_num>             Get Connector Status
      --get_cable_prop <conn_num>           Get Cable Properties
      --get_cur_cam <conn_num>              Get Current Alternate mode
      --get_alt_modes <conn_num> <conn|sop|sopprime|sopprimeprime>
                                            Get Alternate Modes
      --get_pdos <conn_num> <partner> <offset>
                 <src_snk> <type>           Get PDOS
      --set_uor <conn_num> <DFP/UFP/Accept> Set USB(Data) Operation Role
      --set_pdr <conn_num> <SRC/SNK/Accept> Set Power Direction Role
      --get_lpm_ppm_info <conn_num>         Get lpm and ppm info
      --get_error_sts <conn_num>            Get Error Status
      --set_ccom <conn_num> <Rd/Rp/DRP>     Set CC Operation Mode
      --set_new_cam <conn_num> <new_cam>
                 <am_specific> <enter|exit> Set New Current Alternate Mode
*/

#define FOR_ucsicontrol
#include "toys.h"

GLOBALS(
  int fp_command;
  int fp_response;
)

#define UCSI_MIN_MESSAGE_IN_LEN 16
#define UCSI_CONNECTOR_STATUS_LEN 16
#define UCSI_CABLE_PROPERTY_LEN 8
#define UCSI_BIT(value, bit) (((unsigned)(value)>>(bit))&1)
// UCSI connector numbers in commands are one-based.
#define UCSI_CONNECTOR_NUM(conn) ((conn)+1)
#define UCSI_CMD_CONNECTOR(conn) (UCSI_CONNECTOR_NUM(conn)<<16)

#define UCSI_CMD_PPM_RESET 0x01
#define UCSI_CMD_CANCEL 0x02
#define UCSI_CMD_CONNECTOR_RESET 0x03
#define UCSI_CMD_ACK_CC_CI 0x04
#define UCSI_CMD_SET_NOTIFICATION_ENABLE 0x05
#define UCSI_CMD_GET_CAPABILITY 0x06
#define UCSI_CMD_GET_CONNECTOR_CAPABILITY 0x07
#define UCSI_CMD_SET_CCOM 0x08
#define UCSI_CMD_SET_UOR 0x09
#define UCSI_CMD_SET_PDR 0x0b
#define UCSI_CMD_GET_ALTERNATE_MODES 0x0c
#define UCSI_CMD_GET_CURRENT_CAM 0x0e
#define UCSI_CMD_SET_NEW_CAM 0x0f
#define UCSI_CMD_GET_PDOS 0x10
#define UCSI_CMD_GET_CABLE_PROPERTY 0x11
#define UCSI_CMD_GET_CONNECTOR_STATUS 0x12
#define UCSI_CMD_GET_ERROR_STATUS 0x13
#define UCSI_CMD_GET_LPM_PPM_INFO 0x22

#define UCSI_POM_USB_DEFAULT 1
#define UCSI_POM_BC 2
#define UCSI_POM_PD 3
#define UCSI_POM_TYPEC_1_5A 4
#define UCSI_POM_TYPEC_3A 5
#define UCSI_POM_TYPEC_5A 6

#define UCSI_POWER_CONSUMER 0
#define UCSI_POWER_PROVIDER 1

#define UCSI_PARTNER_DFP 1
#define UCSI_PARTNER_UFP 2
#define UCSI_PARTNER_POWERED_CABLE_NO_UFP 3
#define UCSI_PARTNER_POWERED_CABLE_UFP 4
#define UCSI_PARTNER_DEBUG_ACCESSORY 5
#define UCSI_PARTNER_AUDIO_ACCESSORY 6

#define UCSI_BC_NOT_CHARGING 0
#define UCSI_BC_NOMINAL 1
#define UCSI_BC_SLOW 2
#define UCSI_BC_VERY_SLOW 3

#define UCSI_ORIENTATION_DIRECT 0
#define UCSI_ORIENTATION_FLIPPED 1

#define UCSI_CABLE_SPEED_BITS 0
#define UCSI_CABLE_SPEED_KBPS 1
#define UCSI_CABLE_SPEED_MBPS 2
#define UCSI_CABLE_SPEED_GBPS 3

#define UCSI_CABLE_PASSIVE 0
#define UCSI_CABLE_ACTIVE 1

#define UCSI_PLUG_TYPE_A 0
#define UCSI_PLUG_TYPE_B 1
#define UCSI_PLUG_TYPE_C 2
#define UCSI_PLUG_OTHER 3

// Extract UCSI bitfields from little-endian response bytes.
static unsigned get_bits(char *p, int first, int len)
{
  unsigned val = 0;
  int i;

  for (i = 0; i < len; i++)
    if (UCSI_BIT(p[(first+i)/8], (first+i)&7)) val |= 1u<<i;

  return val;
}

static int hex_to_int(char c)
{
  if (c >= '0' && c <= '9') return c-'0';
  if (c >= 'a' && c <= 'f') return c-'a'+10;
  if (c >= 'A' && c <= 'F') return c-'A'+10;

  return -1;
}

// Recursively search for "command" and "response" files under base.
static int find_ucsi_files(char *base, char *cmd_path, char *resp_path)
{
  struct dirent *dp;
  DIR *dir = opendir(base);
  int cmd_found = 0, resp_found = 0;
  char path[PATH_MAX];

  if (!dir) return -1;

  while ((dp = readdir(dir))) {
    if (!strcmp(dp->d_name, ".") || !strcmp(dp->d_name, "..")) continue;
    snprintf(path, sizeof(path), "%s/%s", base, dp->d_name);

    if (!strcmp(dp->d_name, "command")) {
      xstrncpy(cmd_path, path, PATH_MAX);
      cmd_found = 1;
    }
    if (!strcmp(dp->d_name, "response")) {
      xstrncpy(resp_path, path, PATH_MAX);
      resp_found = 1;
    }
    if (cmd_found && resp_found) {
      closedir(dir);

      return 0;
    }
    if (!find_ucsi_files(path, cmd_path, resp_path)) {
      closedir(dir);

      return 0;
    }
  }
  closedir(dir);

  return -1;
}

static void ucsi_open(void)
{
  char *base = "/sys/kernel/debug/usb/ucsi";
  char cmd_path[PATH_MAX] = {0}, resp_path[PATH_MAX] = {0};

  // The UCSI debugfs "command" and "response" files must both be present.
  if (find_ucsi_files(base, cmd_path, resp_path) ||
      access(cmd_path, F_OK) || access(resp_path, F_OK))
    error_exit("UCSI debugfs not present: no command/response file under %s",
      base);

  TT.fp_command = xopen(cmd_path, O_WRONLY);
  TT.fp_response = xopen(resp_path, O_RDONLY);
}

static void ucsi_close(void)
{
  if (TT.fp_command > 0) close(TT.fp_command);
  if (TT.fp_response > 0) close(TT.fp_response);
}

// Read a UCSI response, decode ASCII hex (skipping leading "0x"), and store
// bytes in little-endian protocol order. Returns decoded byte count or -1.
static int read_resp(char *data)
{
  char *c = toybuf, *tmp = libbuf;
  int i, n, di = 0, total = 0;

  if (TT.fp_response <= 0) return -1;

  // Read the whole response; a single read() may not return all of it.
  while (total < 1024 && (n = read(TT.fp_response, c+total, 1024-total)) > 0)
    total += n;
  lseek(TT.fp_response, 0, SEEK_SET);
  if (total <= 2) return -1;

  // Skip the leading "0x" and decode every hex pair in the response.
  for (i = 2; i+1 < total && di < 512; i += 2) {
    int hi = hex_to_int(c[i]), lo = hex_to_int(c[i+1]);

    if (hi < 0 || lo < 0) break;
    tmp[di++] = (hi<<4) | lo;
  }
  // debugfs prints 64-bit chunks as ext, high, low; restore low-first bytes.
  for (i = 0; i < di; i++) data[i] = tmp[di-1-i];

  return di;
}

// Write a preformatted command string and read back the response.
static int ucsi_xfer(char *cmd, char *resp)
{
  char buf[64] = {0};

  if (TT.fp_command <= 0) return -1;
  xstrncpy(buf, cmd, sizeof(buf));
  if (write(TT.fp_command, buf, sizeof(buf)) < 0) return -1;

  return read_resp(resp);
}

// Format a numeric UCSI command for debugfs.
static int ucsi_cmd(unsigned long long cmd, char *resp)
{
  sprintf(toybuf, "0x%llx", cmd);

  return ucsi_xfer(toybuf, resp);
}

static int xucsi_cmd(unsigned long long cmd, char *resp, char *name)
{
  int n;

  if ((n = ucsi_cmd(cmd, resp)) < UCSI_MIN_MESSAGE_IN_LEN)
    error_exit("%s failed", name);

  return n;
}

static void print_message_in(char *p, int len)
{
  int i;

  printf("UCSI MESSAGE_IN:\n==================\n");
  for (i = 0; i < len; i++) {
    printf("%02x ", p[i]);
    if (!((i+1)%8)) printf("\n");
  }
  printf("\n");
}

static void hex_to_decimal(unsigned version)
{
  printf("%x.%02x\n", (version>>8)&0xff, version&0xff);
}

static int get_conn(char *s)
{
  int conn = atolx(s);

  if (conn < 0) error_exit("Invalid connector number: %s", s);

  return conn;
}

static void print_capability(char *c)
{
  unsigned attrs = peek_le(c, 4);
  unsigned opt = c[5] | (c[6]<<8) | (c[7]<<16);

  printf("\nUCSI_GET_CAPABILITY_IN:\n-------------------------\n");
  printf("bmAttributes:\n");
  printf("  disabledStateSupport: %u\n", UCSI_BIT(attrs, 0));
  printf("  batteryCharging: %u\n", UCSI_BIT(attrs, 1));
  printf("  usbPowerDelivery: %u\n", UCSI_BIT(attrs, 2));
  printf("  usbTypeCCurrent: %u\n", UCSI_BIT(attrs, 6));
  printf("  bmPowerSource.acsupply: %u\n", UCSI_BIT(attrs, 8));
  printf("  bmPowerSource.other: %u\n", UCSI_BIT(attrs, 10));
  printf("  bmPowerSource.vbus: %u\n", UCSI_BIT(attrs, 14));
  printf("bNumConnectors: %x\n", get_bits(c, 32, 7));
  printf("bmOptionalFeatures:\n");
  printf("  setccomsupported: %u\n", UCSI_BIT(opt, 0));
  printf("  setpowerlevelsupported: %u\n", UCSI_BIT(opt, 1));
  printf("  altmodedetailssupported: %u\n", UCSI_BIT(opt, 2));
  printf("  altmodeoverridesupported: %u\n", UCSI_BIT(opt, 3));
  printf("  pdodetailssupported: %u\n", UCSI_BIT(opt, 4));
  printf("  cabledetailssupported: %u\n", UCSI_BIT(opt, 5));
  printf("  extsupplynotificationsupported: %u\n", UCSI_BIT(opt, 6));
  printf("  pdresetnotificationsupported: %u\n", UCSI_BIT(opt, 7));
  printf("  getpdmessagesupported: %u\n", UCSI_BIT(opt, 8));
  printf("  getattentionvdosupported: %u\n", UCSI_BIT(opt, 9));
  printf("  fwupdaterequestsupported: %u\n", UCSI_BIT(opt, 10));
  printf("  negotiatedpowerlevelchangesupported: %u\n", UCSI_BIT(opt, 11));
  printf("  securityrequestsupported: %u\n", UCSI_BIT(opt, 12));
  printf("  setretimermodesupported: %u\n", UCSI_BIT(opt, 13));
  printf("  chunkingsupportsupported: %u\n", UCSI_BIT(opt, 14));
  printf("bNumAltModes: %x\n", c[8]);
  printf("bcdBCVersion: ");
  hex_to_decimal(peek_le(c+10, 2));
  printf("bcdPDVersion: ");
  hex_to_decimal(peek_le(c+12, 2));
  printf("bcdTypeCVersion: ");
  hex_to_decimal(peek_le(c+14, 2));
}

static void print_connector_capability(char *c)
{
  unsigned op = c[0], ext = get_bits(c, 14, 8);
  unsigned misc = get_bits(c, 22, 4);

  printf("\nGET_CONNECTOR_CAPABILITY:\n-------------------------\n");
  printf("OperationMode: 0x%x\n", op);
  printf("  Rponly: %d\n", UCSI_BIT(op, 0));
  printf("  Rdonly: %d\n", UCSI_BIT(op, 1));
  printf("  Drp: %d\n", UCSI_BIT(op, 2));
  printf("  AnalogAudioAccessorymode: %d\n", UCSI_BIT(op, 3));
  printf("  DebugAccessorymode: %d\n", UCSI_BIT(op, 4));
  printf("  Usb2: %d\n", UCSI_BIT(op, 5));
  printf("  Usb3: %d\n", UCSI_BIT(op, 6));
  printf("  AlternateMode: %d\n", UCSI_BIT(op, 7));
  printf("Provider: %d\n", get_bits(c, 8, 1));
  printf("Consumer: %d\n", get_bits(c, 9, 1));
  printf("SwapToDfp: %d\n", get_bits(c, 10, 1));
  printf("SwapToUfp: %d\n", get_bits(c, 11, 1));
  printf("SwapToSrc: %d\n", get_bits(c, 12, 1));
  printf("SwapToSnk: %d\n", get_bits(c, 13, 1));
  printf("ExtendedOperationMode: 0x%x\n", ext);
  printf("  Usb4Gen2: %d\n", UCSI_BIT(ext, 0));
  printf("  EprSrc: %d\n", UCSI_BIT(ext, 1));
  printf("  EprSnk: %d\n", UCSI_BIT(ext, 2));
  printf("  Usb4Gen3: %d\n", UCSI_BIT(ext, 3));
  printf("  Usb4Gen4: %d\n", UCSI_BIT(ext, 4));
  printf("MiscellaneousCapabilities: 0x%x\n", misc);
  printf("  FwUpdate: %d\n", UCSI_BIT(misc, 0));
  printf("  Security: %d\n", UCSI_BIT(misc, 1));
  printf("ReverseCurrentProtectionSupport: %d\n", get_bits(c, 26, 1));
  printf("PartnerPDRevision: %d\n", get_bits(c, 27, 2));
}

static void print_connector_status(char *c)
{
  unsigned change = peek_le(c, 2), flags = get_bits(c, 21, 8),
           pom = get_bits(c, 16, 3), pdir = get_bits(c, 20, 1),
           bc = get_bits(c, 64, 2), limit = get_bits(c, 66, 4),
           cptype = get_bits(c, 29, 3), rdo = get_bits(c, 32, 32),
           orient = get_bits(c, 86, 1), scale = get_bits(c, 90, 3),
           voltage_scale = get_bits(c, 125, 4), voltage = get_bits(c, 129, 16);

  printf("\nUCSI_GET_CONNECTOR_STATUS:\n-------------------------\n");
  printf("ConnectorStatusChange: 0x%x\n", change);
  printf("  ExternalSupplyChange: %d\n", UCSI_BIT(change, 1));
  printf("  Attention: %d\n", UCSI_BIT(change, 3));
  printf("  SupportedProviderCapabilitiesChange: %d\n", UCSI_BIT(change, 5));
  printf("  NegotiatedPowerLevelChange: %d\n", UCSI_BIT(change, 6));
  printf("  PDResetComplete: %d\n", UCSI_BIT(change, 7));
  printf("  SupportedCAMChange: %d\n", UCSI_BIT(change, 8));
  printf("  BatteryChargingStatusChange: %d\n", UCSI_BIT(change, 9));
  printf("  ConnectorPartnerChanged: %d\n", UCSI_BIT(change, 11));
  printf("  PowerDirectionChanged: %d\n", UCSI_BIT(change, 12));
  printf("  SinkPathStatusChange: %d\n", UCSI_BIT(change, 13));
  printf("  ConnectChange: %d\n", UCSI_BIT(change, 14));
  printf("  Error: %d\n", UCSI_BIT(change, 15));
  printf("PowerOperationMode: %d\n", pom);
  printf("  UsbDefaultOperation: %d\n", pom == UCSI_POM_USB_DEFAULT);
  printf("  BC: %d\n", pom == UCSI_POM_BC);
  printf("  PD: %d\n", pom == UCSI_POM_PD);
  printf("  UsbTypecCurrent1.5A: %d\n", pom == UCSI_POM_TYPEC_1_5A);
  printf("  UsbTypecCurrent3A: %d\n", pom == UCSI_POM_TYPEC_3A);
  printf("  UsbTypecCurrent5A: %d\n", pom == UCSI_POM_TYPEC_5A);
  printf("ConnectStatus: %d\n", get_bits(c, 19, 1));
  printf("PowerDirection: %d\n", pdir);
  printf("  Consumer: %d\n", pdir == UCSI_POWER_CONSUMER);
  printf("  Provider: %d\n", pdir == UCSI_POWER_PROVIDER);
  printf("ConnectorPartnerFlags: 0x%x\n", flags);
  printf("  Usb: %d\n", UCSI_BIT(flags, 0));
  printf("  Dp: %d\n", UCSI_BIT(flags, 1));
  printf("  Tbt: %d\n", UCSI_BIT(flags, 2));
  printf("  Usb4: %d\n", UCSI_BIT(flags, 3));
  printf("ConnectorPartnerType: %d\n", cptype);
  printf("  DFPattached: %d\n", cptype == UCSI_PARTNER_DFP);
  printf("  UFPattached: %d\n", cptype == UCSI_PARTNER_UFP);
  printf("  PoweredCableNoUFPattached: %d\n",
    cptype == UCSI_PARTNER_POWERED_CABLE_NO_UFP);
  printf("  PoweredCableUFPattached: %d\n",
    cptype == UCSI_PARTNER_POWERED_CABLE_UFP);
  printf("  DebugAccessoryattched: %d\n",
    cptype == UCSI_PARTNER_DEBUG_ACCESSORY);
  printf("  AudioAccessoryAttached: %d\n",
    cptype == UCSI_PARTNER_AUDIO_ACCESSORY);
  printf("RequestDataObject: 0x%x\n", rdo);
  printf("BatteryChargingCapabilityStatus: %d\n", bc);
  printf("  NotCharging: %d\n", bc == UCSI_BC_NOT_CHARGING);
  printf("  NominalChargingRate: %d\n", bc == UCSI_BC_NOMINAL);
  printf("  SlowChargingRate: %d\n", bc == UCSI_BC_SLOW);
  printf("  VerySlowCharingRate: %d\n", bc == UCSI_BC_VERY_SLOW);
  printf("ProviderCapabilitiesLimitedReason: %d\n", limit);
  printf("bcdPDVersionOperationMode: ");
  hex_to_decimal(get_bits(c, 70, 16));
  printf("Orientation: %d\n", orient);
  printf("  DirectOrientation : %d\n", orient == UCSI_ORIENTATION_DIRECT);
  printf("  FlippedOrientation : %d\n", orient == UCSI_ORIENTATION_FLIPPED);
  printf("SinkPathStatus: %d\n", get_bits(c, 87, 1));
  printf("ReverseCurrentProtectionStatus: %d\n", get_bits(c, 88, 1));
  printf("PowerReadingReady: %d\n", get_bits(c, 89, 1));
  printf("CurrentScale: %d\n", scale);
  printf("PeakCurrent: %d\n", get_bits(c, 93, 16));
  printf("AverageCurrent: %d\n", get_bits(c, 109, 16));
  printf("VoltageScale: %d\n", voltage_scale);
  printf("VoltageReading: %d\n", voltage);
  printf("Voltage: %d\n", voltage * voltage_scale * 5);
}

static void print_cable_property(char *c)
{
  unsigned speed = peek_le(c, 2), speed_unit = get_bits(c, 0, 2),
           current = get_bits(c, 16, 8), cable_type = get_bits(c, 25, 1),
           plug = get_bits(c, 27, 2);

  printf("\nGET_CABLE_PROPERTY:\n-------------------------\n");
  printf("bmSpeedSupported: 0x%x\n", speed);
  printf("  Bits/s: %d\n", speed_unit == UCSI_CABLE_SPEED_BITS);
  printf("  Kb/s: %d\n", speed_unit == UCSI_CABLE_SPEED_KBPS);
  printf("  Mb/s: %d\n", speed_unit == UCSI_CABLE_SPEED_MBPS);
  printf("  Gb/s: %d\n", speed_unit == UCSI_CABLE_SPEED_GBPS);
  printf("bCurrentCapability: %d mA\n", current * 50);
  printf("VBUSInCable: %d\n", get_bits(c, 24, 1));
  printf("CableType: %d\n", cable_type);
  printf("  PassiveCable: %d\n", cable_type == UCSI_CABLE_PASSIVE);
  printf("  ActiveCable: %d\n", cable_type == UCSI_CABLE_ACTIVE);
  printf("Directionality: %d\n", get_bits(c, 26, 1));
  printf("PlugEndType: %d\n", plug);
  printf("  USBtypeA: %d\n", plug == UCSI_PLUG_TYPE_A);
  printf("  USBtypeB: %d\n", plug == UCSI_PLUG_TYPE_B);
  printf("  USBtypeC: %d\n", plug == UCSI_PLUG_TYPE_C);
  printf("  Other: %d\n", plug == UCSI_PLUG_OTHER);
  printf("ModeSupport: %d\n", get_bits(c, 29, 1));
  printf("CablePDRevision: %d\n", get_bits(c, 30, 2));
  printf("Latency: %d\n", get_bits(c, 32, 4));
}

static void print_lpm_ppm_info(char *c)
{
  unsigned fw_upper = peek_le(c+8, 4), fw_lower = peek_le(c+12, 4);

  printf("\nGET_LPM_PPM_INFO :\n-------------------------\n");
  printf("VID: 0x%x\n", (unsigned)peek_le(c, 2));
  printf("PID: 0x%x\n", (unsigned)peek_le(c+2, 2));
  printf("XID: 0x%x\n", (unsigned)peek_le(c+4, 4));
  printf("FW Ver: %u.%u\n", fw_upper, fw_lower);
  printf("HW Ver: %u\n", (unsigned)peek_le(c+16, 4));
}

static void print_error_status(char *c)
{
  unsigned e = peek_le(c, 2);

  printf("\nGET_ERROR_STATUS :\n-------------------------\n");
  printf("ErrorInformation:\n");
  printf("  UnrecognizedCmd: %d\n", UCSI_BIT(e, 0));
  printf("  NonExistentConnectorNum: %d\n", UCSI_BIT(e, 1));
  printf("  InvalidCmdSpecificParam: %d\n", UCSI_BIT(e, 2));
  printf("  IncompatibleConnectorPartner: %d\n", UCSI_BIT(e, 3));
  printf("  CCcommunicationError: %d\n", UCSI_BIT(e, 4));
  printf("  CmdUnsuccessDeadBattery: %d\n", UCSI_BIT(e, 5));
  printf("  ContractNegotiationFailure: %d\n", UCSI_BIT(e, 6));
  printf("  OverCurrent: %d\n", UCSI_BIT(e, 7));
  printf("  Undefined: %d\n", UCSI_BIT(e, 8));
  printf("  PortPartnerRejectSwap: %d\n", UCSI_BIT(e, 9));
  printf("  HardReset: %d\n", UCSI_BIT(e, 10));
  printf("  PpmPolicyConflict: %d\n", UCSI_BIT(e, 11));
  printf("  SwapRejected: %d\n", UCSI_BIT(e, 12));
  printf("  ReverseCurrentProtection: %d\n", UCSI_BIT(e, 13));
  printf("  SetSinkPathRejected: %d\n", UCSI_BIT(e, 14));
  printf("VendorDefined: 0x%x\n", (unsigned)peek_le(c+2, 2));
}

static int role_code(char *s, char *a, char *b, char *c, int va, int vb, int vc)
{
  if (!strcmp(s, a)) return va;
  if (!strcmp(s, b)) return vb;
  if (!strcmp(s, c)) return vc;
  error_exit("Invalid type: %s", s);
}

// Common <conn_num> parser for single-connector commands.
static int required_conn(char **args, char *op)
{
  if (!args[1]) error_exit("%s needs <conn_num>", op);

  return get_conn(args[1]);
}

void ucsicontrol_main(void)
{
  char **args = toys.optargs, *op = args[0], buf[256] = {0};
  int conn, n;

  // Accept both "--option" and "-h" style; show help when asked or no command.
  if (!op || !strcmp(op, "-h") || !strcmp(op, "--help")) {
    show_help(HELP_HEADER);
    xexit();
  }
  if (*op == '-') op += (op[1] == '-') ? 2 : 1;

  ucsi_open();

  if (!strcmp(op, "get_cap")) {
    n = xucsi_cmd(UCSI_CMD_GET_CAPABILITY, buf, "get_capability");
    print_message_in(buf, n);
    print_capability(buf);
  } else if (!strcmp(op, "get_conn_cap")) {
    conn = required_conn(args, "get_conn_cap");
    n = xucsi_cmd(UCSI_CMD_CONNECTOR(conn)
      | UCSI_CMD_GET_CONNECTOR_CAPABILITY, buf, "get_conn_cap");
    print_message_in(buf, n);
    print_connector_capability(buf);
  } else if (!strcmp(op, "get_conn_sts")) {
    conn = required_conn(args, "get_conn_sts");
    n = xucsi_cmd(UCSI_CMD_CONNECTOR(conn)
      | UCSI_CMD_GET_CONNECTOR_STATUS, buf, "get_conn_sts");
    print_message_in(buf, UCSI_CONNECTOR_STATUS_LEN);
    print_connector_status(buf);
  } else if (!strcmp(op, "get_cable_prop")) {
    conn = required_conn(args, "get_cable_prop");
    n = xucsi_cmd(UCSI_CMD_CONNECTOR(conn)
      | UCSI_CMD_GET_CABLE_PROPERTY, buf, "get_cable_prop");
    print_message_in(buf, UCSI_CABLE_PROPERTY_LEN);
    print_cable_property(buf);
  } else if (!strcmp(op, "get_cur_cam")) {
    conn = required_conn(args, "get_cur_cam");
    n = xucsi_cmd(UCSI_CMD_CONNECTOR(conn)
      | UCSI_CMD_GET_CURRENT_CAM, buf, "get_cur_cam");
    print_message_in(buf, n);
    printf("\nGET_CURRENT_CAM :\n-------------------------\n");
    printf("CurrentAlternateMode: 0x%x\n", buf[0]);
  } else if (!strcmp(op, "get_alt_modes")) {
    int recipient, i = 0, prevn = 0;
    unsigned short first_svid = 0;
    unsigned char prev[256];

    if (!args[1] || !args[2])
      error_exit("Usage: get_alt_modes <conn_num> "
        "[conn|sop|sopprime|sopprimeprime]");
    conn = get_conn(args[1]);
    if (!strcmp(args[2], "conn")) recipient = 0;
    else if (!strcmp(args[2], "sop")) recipient = 1;
    else if (!strcmp(args[2], "sopprime")) recipient = 2;
    else if (!strcmp(args[2], "sopprimeprime")) recipient = 3;
    else error_exit("Usage: get_alt_modes <conn_num> "
      "<conn|sop|sopprime|sopprimeprime>");
    for (;;) {
      // GET_ALTERNATE_MODES (0x0C): Recipient[16], ConnNum[24],
      // AltModeOffset[32], NumAltModes[40]=1 -> up to 2 modes per call.
      unsigned long long cmd = UCSI_CMD_GET_ALTERNATE_MODES
        | ((unsigned long long)(recipient & 0x7)  << 16)
        | ((unsigned long long)(UCSI_CONNECTOR_NUM(conn) & 0x7f) << 24)
        | ((unsigned long long)(i & 0xff)        << 32)
        | (1ULL << 40);
      unsigned short svid0, svid1;
      unsigned mid0, mid1;

      // End of list is usually an empty/short response.
      if ((n = ucsi_cmd(cmd, buf)) < UCSI_MIN_MESSAGE_IN_LEN) break;
      // Some LPMs replay the previous response with only stale svid0 changed.
      // Treat unchanged bytes after svid0 as end of list.
      if (i && n == prevn && !memcmp(buf + 2, prev + 2, n - 2)) break;
      memcpy(prev, buf, n);
      prevn = n;

      // Table 6-26: SVID[0]@0 MID[0]@2 SVID[1]@6 MID[1]@8.
      svid0 = peek_le(buf, 2);
      mid0  = peek_le(buf+2, 4);
      svid1 = peek_le(buf+6, 2);
      mid1  = peek_le(buf+8, 4);

      if (!svid0) break;
      // LPM repeats the same response when offset is exhausted; stop on wrap.
      if (first_svid && svid0 == first_svid) break;
      print_message_in(buf, n);
      if (!first_svid) {
        first_svid = svid0;
        printf("\nGET_ALTERNATE_MODE :\n-------------------------\n");
      }
      printf("Mode [%d]\n  svid: 0x%x\n  vdo : 0x%x\n", i, svid0, mid0);
      i++;

      if (!svid1 || svid1 == first_svid) break;
      printf("Mode [%d]\n  svid: 0x%x\n  vdo : 0x%x\n", i, svid1, mid1);
      i++;
    }
    if (!first_svid)
      printf("\nGET_ALTERNATE_MODE :\n-------------------------\n"
        "No alternate modes reported\n");
  } else if (!strcmp(op, "get_pdos")) {
    int partner, offset, srcsnk, type, i = 0;
    unsigned ppdo = 0, pdo;

    if (!args[1] || !args[2] || !args[3] || !args[4] || !args[5])
      error_exit("get_pdos needs <conn_num> <partner> <offset> <src_snk> "
        "<type>");
    conn = get_conn(args[1]);
    partner = atolx(args[2]);
    offset = atolx(args[3]);
    srcsnk = atolx(args[4]);
    type = atolx(args[5]);
    printf("\nGET_PDOS :\n-------------------------\n");
    for (;;) {
      unsigned long long cmd = UCSI_CMD_GET_PDOS
        | ((unsigned long long)UCSI_CMD_CONNECTOR(conn))
        | ((unsigned long long)(partner&1)<<23)
        | ((unsigned long long)((offset+i)&0xff)<<24)
        | ((unsigned long long)(srcsnk&1)<<34)
        | ((unsigned long long)(type&3)<<35);

      if ((n = ucsi_cmd(cmd, buf)) < UCSI_MIN_MESSAGE_IN_LEN) break;
      print_message_in(buf, n);
      pdo = peek_le(buf, 4);
      if (!pdo || pdo == ppdo) break;
      printf("PDO [%d]: 0x%x\n\n", i, pdo);
      ppdo = pdo;
      i++;
    }
  } else if (!strcmp(op, "get_lpm_ppm_info")) {
    conn = required_conn(args, "get_lpm_ppm_info");
    n = xucsi_cmd(UCSI_CMD_CONNECTOR(conn)
      | UCSI_CMD_GET_LPM_PPM_INFO, buf, "get_lpm_ppm_info");
    print_message_in(buf, n);
    print_lpm_ppm_info(buf);
  } else if (!strcmp(op, "get_error_sts")) {
    conn = required_conn(args, "get_error_sts");
    n = xucsi_cmd(UCSI_CMD_CONNECTOR(conn)
      | UCSI_CMD_GET_ERROR_STATUS, buf, "get_error_sts");
    print_message_in(buf, n);
    print_error_status(buf);
  } else if (!strcmp(op, "conn_rst")) {
    int rst;

    if (!args[1] || !args[2])
      error_exit("conn_rst needs <conn_num> <soft|hard>");
    conn = get_conn(args[1]);
    rst = role_code(args[2], "soft", "hard", "", 0, 1, -1);
    n = ucsi_cmd(UCSI_CMD_CONNECTOR_RESET | UCSI_CMD_CONNECTOR(conn)
      | (rst<<23), buf);
    printf("connector%d %s reset%s\n", conn, rst ? "hard" : "soft",
      n < UCSI_MIN_MESSAGE_IN_LEN ? " failed" : "");
  } else if (!strcmp(op, "set_uor")) {
    int uor;

    if (!args[1] || !args[2])
      error_exit("set_uor needs <conn_num> <DFP|UFP|Accept>");
    conn = get_conn(args[1]);
    uor = role_code(args[2], "DFP", "UFP", "Accept", 1, 2, 4);
    n = ucsi_cmd(UCSI_CMD_SET_UOR | UCSI_CMD_CONNECTOR(conn)
      | ((0x4|uor)<<23), buf);
    printf("connector%d set data role operation %s%s\n", conn, args[2],
      n < UCSI_MIN_MESSAGE_IN_LEN ? " failed" : "");
  } else if (!strcmp(op, "set_pdr")) {
    int pdr;

    if (!args[1] || !args[2])
      error_exit("set_pdr needs <conn_num> <SRC|SNK|Accept>");
    conn = get_conn(args[1]);
    pdr = role_code(args[2], "SRC", "SNK", "Accept", 1, 2, 4);
    n = ucsi_cmd(UCSI_CMD_SET_PDR | UCSI_CMD_CONNECTOR(conn) | (pdr<<23),
      buf);
    printf("connector%d power role swap operation %s%s\n", conn, args[2],
      n < UCSI_MIN_MESSAGE_IN_LEN ? " failed" : "");
  } else if (!strcmp(op, "set_ccom")) {
    int ccom;

    if (!args[1] || !args[2])
      error_exit("set_ccom needs <conn_num> <Rd|Rp|DRP>");
    conn = get_conn(args[1]);
    ccom = role_code(args[2], "Rd", "Rp", "DRP", 1, 2, 4);
    n = ucsi_cmd(UCSI_CMD_SET_CCOM | UCSI_CMD_CONNECTOR(conn) | (ccom<<23),
      buf);
    printf("connector%d set cc operation mode %s%s\n", conn, args[2],
      n < UCSI_MIN_MESSAGE_IN_LEN ? " failed" : "");
  } else if (!strcmp(op, "set_new_cam")) {
    int ee, newcam;
    unsigned amspec;

    if (!args[1] || !args[2] || !args[3] || !args[4])
      error_exit("set_new_cam needs <conn_num> <new_cam> <am_specific> "
        "<enter|exit>");
    conn = get_conn(args[1]);
    newcam = atolx(args[2]);
    amspec = atolx(args[3]);
    ee = role_code(args[4], "enter", "exit", "", 1, 0, -1);
    n = ucsi_cmd(UCSI_CMD_SET_NEW_CAM
      | ((unsigned long long)UCSI_CMD_CONNECTOR(conn))
      | ((unsigned long long)(ee&1)<<23)
      | ((unsigned long long)(newcam&0xff)<<24)
      | ((unsigned long long)amspec<<32), buf);
    printf("connector%d set new cam %s%s\n", conn, args[4],
      n < UCSI_MIN_MESSAGE_IN_LEN ? " failed" : "");
  } else {
    ucsi_close();
    error_exit("Unknown command: %s", op);
  }

  ucsi_close();
}
