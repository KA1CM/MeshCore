#pragma once
#include <strings.h>

namespace BotAdminHelp {
inline const char* reply(const char* command) {
  if (!command || !*command) return "Admin help:\nadvert check sync add remove enable disable notes password";
  if (!strcasecmp(command,"advert")) return "Send a flood advert.\nUsage: advert";
  if (!strcasecmp(command,"check")) return "Check repeater voltage.\nUsage: check <repeater>";
  if (!strcasecmp(command,"sync")) return "Sync repeater clock.\nUsage: sync <repeater>";
  if (!strcasecmp(command,"add")) return "Add an enabled managed repeater by its full 64-digit public key.\nUsage: add <full key>";
  if (!strcasecmp(command,"remove")) return "Remove a repeater from the managed list.\nUsage: remove <repeater>";
  if (!strcasecmp(command,"enable")) return "Enable a managed repeater.\nUsage: enable <repeater>";
  if (!strcasecmp(command,"disable")) return "Disable a managed repeater.\nUsage: disable <repeater>";
  if (!strcasecmp(command,"notes")) return "View: notes <rpt>\nReplace: notes set <rpt> | <text>\nEdit field: notes set <rpt> | <field> | <value>";
  if (!strcasecmp(command,"password")) return "Add/replace a repeater login password.\nUsage: password <repeater> | <password>";
  return nullptr;
}
}
