/*
 * Authors (alphabetical order)
 * - Andre Bernet <bernet.andre@gmail.com>
 * - Andreas Weitl
 * - Bertrand Songis <bsongis@gmail.com>
 * - Bryan J. Rentoul (Gruvin) <gruvin@gmail.com>
 * - Cameron Weeks <th9xer@gmail.com>
 * - Erez Raviv
 * - Gabriel Birkus
 * - Jean-Pierre Parisy
 * - Karl Szmutny
 * - Michael Blandford
 * - Michal Hlavinka
 * - Pat Mackenzie
 * - Philip Moss
 * - Rob Thomson
 * - Romolo Manfredini <romolo.manfredini@gmail.com>
 * - Thomas Husterer
 *
 * opentx is based on code named
 * gruvin9x by Bryan J. Rentoul: http://code.google.com/p/gruvin9x/,
 * er9x by Erez Raviv: http://code.google.com/p/er9x/,
 * and the original (and ongoing) project by
 * Thomas Husterer, th9x: http://code.google.com/p/th9x/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include "opentx.h"
#include <ctype.h>

#define CLI_COMMAND_MAX_ARGS           8
#define CLI_COMMAND_MAX_LEN            256

OS_TID cliTaskId;
TaskStack<CLI_STACK_SIZE> cliStack;
Fifo<256> cliRxFifo;
uint8_t cliTracesEnabled = true;

typedef int (* CliFunction) (const char ** args);

struct CliCommand
{
  const char * name;
  CliFunction func;
  const char * args;
};

struct MemArea
{
  const char * name;
  void * start;
  int size;
};

void cliPrompt()
{
  serialPutc('>');
}

int toLongLongInt(const char ** argv, int index, long long int * val)
{
  if (*argv[index] == '\0') {
    return 0;
  }
  else {
    int base = 10;
    const char * s = argv[index];
    if (strlen(s) > 2 && s[0] == '0' && s[1] == 'x') {
      base = 16;
      s = &argv[index][2];
    }
    char * endptr = NULL;
    *val = strtoll(s, &endptr, base);
    if (*endptr == '\0')
      return 1;
    else {
      serialPrint("%s: Invalid argument \"%s\"", argv[0], argv[index]);
      return -1;
    }
  }
}

int toInt(const char ** argv, int index, int * val)
{
  long long int lval = 0;
  int result = toLongLongInt(argv, index, &lval);
  *val = (int)lval;
  return result;
}

int cliBeep(const char ** argv)
{
  int freq = BEEP_DEFAULT_FREQ;
  int duration = 100;
  if (toInt(argv, 1, &freq) >= 0 && toInt(argv, 2, &duration) >= 0) {
    audioQueue.playTone(freq, duration, 20, PLAY_NOW);
  }
  return 0;
}

int cliPlay(const char ** argv)
{
  audioQueue.playFile(argv[1], PLAY_NOW);
  return 0;
}

int cliLs(const char ** argv)
{
  FILINFO fno;
  DIR dir;
  char *fn;   /* This function is assuming non-Unicode cfg. */
#if _USE_LFN
  TCHAR lfn[_MAX_LFN + 1];
  fno.lfname = lfn;
  fno.lfsize = sizeof(lfn);
#endif

  FRESULT res = f_opendir(&dir, argv[1]);        /* Open the directory */
  if (res == FR_OK) {
    for (;;) {
      res = f_readdir(&dir, &fno);                   /* Read a directory item */
      if (res != FR_OK || fno.fname[0] == 0) break;  /* Break on error or end of dir */

#if _USE_LFN
      fn = *fno.lfname ? fno.lfname : fno.fname;
#else
      fn = fno.fname;
#endif
      serialPrint(fn);
    }
  }
  else {
    serialPrint("%s: Invalid directory \"%s\"", argv[0], argv[1]);
  }
  return 0;
}

int cliTrace(const char ** argv)
{
  if (!strcmp(argv[1], "on")) {
    cliTracesEnabled = true;
  }
  else if (!strcmp(argv[1], "off")) {
    cliTracesEnabled = false;
  }
  else {
    serialPrint("%s: Invalid argument \"%s\"", argv[0], argv[1]);
  }
  return 0;
}

int cliStackInfo(const char ** argv)
{
  int tid = 0;
  if (toInt(argv, 1, &tid) > 0) {
    int available = 0;
    int total = 0;
    switch(tid) {
      case MENU_TASK_INDEX:
        total = menusStack.size();
        available = menusStack.available();
        break;
      case MIXER_TASK_INDEX:
        total = mixerStack.size();
        available = mixerStack.available();
        break;
      case AUDIO_TASK_INDEX:
        total = audioStack.size();
        available = audioStack.available();
        break;
      case CLI_TASK_INDEX:
        total = cliStack.size();
        available = cliStack.available();
        break;
      case MAIN_TASK_INDEX:
        total = stackAvailable();
        available = stackSize();
        break;
      default:
        break;
    }
    serialPrint("%d available (%d total)", available, total);
  }
  else {
    serialPrint("%s: Invalid argument \"%s\"", argv[0], argv[1]);
  }
  return 0;
}

int cliVolume(const char ** argv)
{
  int level = 0;
  if (toInt(argv, 1, &level) > 0) {
    setVolume(level);
  }
  else {
    serialPrint("%s: Invalid argument \"%s\"", argv[0], argv[1]);
  }

  *(uint32_t *) (SDRAM_BANK_ADDR) = level;
  return 0;
}

#if defined(PCBFLAMENCO)
int cliReadBQ24195(const char ** argv)
{
  int index = 0;
  if (toInt(argv, 1, &index) > 0) {
    serialPrint("BQ24195[%d] = 0x%02x", index, i2cReadBQ24195(index));
  }
  else {
    serialPrint("%s: Invalid arguments \"%s\" \"%s\"", argv[0], argv[1]);
  }
  return 0;
}

int cliWriteBQ24195(const char ** argv)
{
  int index = 0;
  int data = 0;
  if (toInt(argv, 1, &index) > 0 && toInt(argv, 2, &data) > 0) {
    i2cWriteBQ24195(index, data);
  }
  else {
    serialPrint("%s: Invalid arguments \"%s\" \"%s\"", argv[0], argv[1], argv[2]);
  }
  return 0;
}
#endif

const MemArea memAreas[] = {
  { "RCC", RCC, sizeof(RCC_TypeDef) },
  { "GPIOA", GPIOA, sizeof(GPIO_TypeDef) },
  { "GPIOB", GPIOB, sizeof(GPIO_TypeDef) },
  { "GPIOC", GPIOC, sizeof(GPIO_TypeDef) },
  { "GPIOD", GPIOD, sizeof(GPIO_TypeDef) },
  { "GPIOE", GPIOE, sizeof(GPIO_TypeDef) },
  { "GPIOF", GPIOF, sizeof(GPIO_TypeDef) },
  { "GPIOG", GPIOG, sizeof(GPIO_TypeDef) },
  { "USART1", USART1, sizeof(USART_TypeDef) },
  { "USART2", USART2, sizeof(USART_TypeDef) },
  { "USART3", USART3, sizeof(USART_TypeDef) },
  { NULL, NULL, 0 },
};

int cliDisplay(const char ** argv)
{
  long long int address = 0;

  for (const MemArea * area = memAreas; area->name != NULL; area++) {
    if (!strcmp(area->name, argv[1])) {
      dump((uint8_t *)area->start, area->size);
      return 0;
    }
  }

  if (!strcmp(argv[1], "keys")) {
    for (int i=0; i<TRM_BASE; i++) {
      char name[8];
      uint8_t len = STR_VKEYS[0];
      strncpy(name, STR_VKEYS+1+len*i, len);
      name[len] = '\0';
      serialPrint("[%s] = %s", name, switchState(EnumKeys(i)) ? "on" : "off");
    }
    serialPrint("[Enc.]  = %d", rotencValue / 2);
    for (int i=TRM_BASE; i<=TRM_LAST; i++) {
      serialPrint("[Trim%d] = %s", i-TRM_BASE, switchState(EnumKeys(i)) ? "on" : "off");
    }
    for (int i=MIXSRC_FIRST_SWITCH; i<=MIXSRC_LAST_SWITCH; i++) {
      mixsrc_t sw = i - MIXSRC_FIRST_SWITCH;
      if (SWITCH_EXISTS(sw)) {
        serialPrint("[S%c] = %s", 'A'+sw, (switchState((EnumKeys)(SW_BASE+(3*sw))) ? "down" : (switchState((EnumKeys)(SW_BASE+(3*sw)+1)) ? "mid" : "up")));
      }
    }
  }
  else if (!strcmp(argv[1], "adc")) {
    for (int i=0; i<NUMBER_ANALOG; i++) {
      serialPrint("adc[%d] = %04X", i, Analog_values[i]);
    }
  }
  else if (!strcmp(argv[1], "outputs")) {
    for (int i=0; i<NUM_CHNOUT; i++) {
      serialPrint("outputs[%d] = %04X", i, channelOutputs[i]);
    }
  }
#if defined(PCBFLAMENCO)
  else if (!strcmp(argv[1], "bq24195")) {
    {
      uint8_t reg = i2cReadBQ24195(0x00);
      serialPrint(reg & 0x80 ? "HIZ enable" : "HIZ disable");
    }
    {
      uint8_t reg = i2cReadBQ24195(0x08);
      serialPrint(reg & 0x01 ? "VBatt < VSysMin" : "VBatt > VSysMin");
      serialPrint(reg & 0x02 ? "Thermal sensor bad" : "Thermal sensor ok");
      serialPrint(reg & 0x04 ? "Power ok" : "Power bad");
      serialPrint(reg & 0x08 ? "Connected to charger" : "Not connected to charger");
      const char * CHARGE_STATUS[] = { "Not Charging", "Precharge", "Fast Charging", "Charge done" };
      serialPrint(CHARGE_STATUS[(reg & 0x30) >> 4]);
      const char * INPUT_STATUS[] = { "Unknown input", "USB host input", "USB adapter port input", "OTG input" };
      serialPrint(INPUT_STATUS[(reg & 0xC0) >> 6]);
    }
    {
      uint8_t reg = i2cReadBQ24195(0x09);
      if (reg & 0x80) serialPrint("Watchdog timer expiration");
      uint8_t chargerFault = (reg & 0x30) >> 4;
      if (chargerFault == 0x01)
        serialPrint("Input fault");
      else if (chargerFault == 0x02)
        serialPrint("Thermal shutdown");
      else if (chargerFault == 0x03)
        serialPrint("Charge safety timer expiration");
      if (reg & 0x08) serialPrint("Battery over voltage fault");
      uint8_t ntcFault = (reg & 0x07);
      if (ntcFault == 0x05)
        serialPrint("NTC cold");
      else if (ntcFault == 0x06)
        serialPrint("NTC hot");
    }
  }
#endif
  else if (toLongLongInt(argv, 1, &address) > 0) {
    int size = 256;
    if (toInt(argv, 2, &size) >= 0) {
      dump((uint8_t *)address, size);
    }
  }
  return 0;
}

int cliHelp(const char ** argv);

const CliCommand cliCommands[] = {
  { "beep", cliBeep, "[<frequency>] [<duration>]" },
  { "ls", cliLs, "<directory>" },
  { "play", cliPlay, "<filename>" },
  { "print", cliDisplay, "<address> [<size>] | <what>" },
  { "stackinfo", cliStackInfo, "<tid>" },
  { "trace", cliTrace, "on | off" },
  { "volume", cliVolume, "<level>" },
#if defined(PCBFLAMENCO)
  { "read_bq24195", cliReadBQ24195, "<register>" },
  { "write_bq24195", cliWriteBQ24195, "<register> <data>" },
#endif
  { "help", cliHelp, "[<command>]" },
  { NULL, NULL, NULL }  /* sentinel */
};

int cliHelp(const char ** argv)
{ 
  for (const CliCommand * command = cliCommands; command->name != NULL; command++) {
    if (argv[1][0] == '\0' || !strcmp(command->name, argv[0])) {
      serialPrint("%s %s", command->name, command->args);
      if (argv[1][0] != '\0') {
        return 0;
      }
    }
  }
  if (argv[1][0] != '\0') {
    serialPrint("Invalid command \"%s\"", argv[0]);
  }
  return -1;
}

int cliExecCommand(const char ** argv)
{
  if (argv[0][0] == '\0')
    return 0;

  for (const CliCommand * command = cliCommands; command->name != NULL; command++) {
    if (!strcmp(command->name, argv[0])) {
      return command->func(argv);
    }
  }
  serialPrint("Invalid command \"%s\"", argv[0]);
  return -1;
}

int cliExecLine(char * line)
{
  int len = strlen(line);
  const char * argv[CLI_COMMAND_MAX_ARGS];
  memset(argv, 0, sizeof(argv));
  int argc = 1;
  argv[0] = line;
  for (int i=0; i<len; i++) {
    if (line[i] == ' ') {
      line[i] = '\0';
      if (argc < CLI_COMMAND_MAX_ARGS) {
        argv[argc++] = &line[i+1];
      }
    }
  }
  return cliExecCommand(argv);
}

void cliTask(void * pdata)
{
  char line[CLI_COMMAND_MAX_LEN+1];
  int pos = 0;

  cliPrompt();

  for (;;) {
    uint8_t c;

    while (!cliRxFifo.pop(c)) {
      CoTickDelay(10); // 20ms
    }

    if (c == 12) {
      // clear screen
      serialPrint("\033[2J\033[1;1H");
      cliPrompt();
    }
    else if (c == 127) {
      // backspace
      if (pos) {
	line[--pos] = '\0';
	serialPutc(c);
      }
    }
    else if (c == '\r' || c == '\n') {
      // enter
      serialCrlf();
      line[pos] = '\0';
      cliExecLine(line);
      pos = 0;
      cliPrompt();
    }
    else if (isascii(c) && pos < CLI_COMMAND_MAX_LEN) {
      line[pos++] = c;
      serialPutc(c);
    }
  }
}

void cliStart()
{
  cliTaskId = CoCreateTaskEx(cliTask, NULL, 10, &cliStack.stack[CLI_STACK_SIZE-1], CLI_STACK_SIZE, 1, false);
}
