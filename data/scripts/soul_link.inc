SoulLink_EventScript_RulesDenied::
    lockall
    msgbox SoulLink_Text_RulesDenied, MSGBOX_DEFAULT
    releaseall
    end

SoulLink_Text_RulesDenied:
    .string "Invalid SOUL LINK team.\n"
    .string "Check your pairs and rules.\p"
    .string "Choose a team in the companion.$"
