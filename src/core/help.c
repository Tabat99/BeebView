#include "viewbbc/help.h"

#include <ctype.h>
#include <string.h>

static const ViewBBCHelpEntry help_entries[] = {
    { VIEWBBC_HELP_FILE_SYSTEM, "LOAD <filename>",        "Load from the active filesystem" },
    { VIEWBBC_HELP_FILE_SYSTEM, "SAVE [filename]",       "Save to the active filesystem" },
    { VIEWBBC_HELP_FILE_SYSTEM, "MOUNT [SSD|DSD] <file>","Mount a BBC DFS disc image read only" },
    { VIEWBBC_HELP_FILE_SYSTEM, "UNMOUNT",               "Unmount the current DFS disc image" },
    { VIEWBBC_HELP_FILE_SYSTEM, "LIST",                  "List the active filesystem" },
    { VIEWBBC_HELP_FILE_SYSTEM, "*.",                    "List the active filesystem" },
    { VIEWBBC_HELP_FILE_SYSTEM, "*CAT",                  "List the active filesystem" },
    { VIEWBBC_HELP_FILE_SYSTEM, "DIR [/A /B /W]",        "DOS-style directory listing" },
    { VIEWBBC_HELP_FILE_SYSTEM, "LS [-a -l]",            "Unix-style directory listing" },
    { VIEWBBC_HELP_FILE_SYSTEM, "CD [path]",             "Show or change current folder/directory" },

    { VIEWBBC_HELP_EDITING, "?",                         "Show this command list" },
    { VIEWBBC_HELP_EDITING, "HELP <FILE|EDIT|KEYS|CONF>",    "Show all help or a help subject" },
    { VIEWBBC_HELP_EDITING, "CLEAR",                     "Clear VIEW markers 1 and 2" },
    { VIEWBBC_HELP_EDITING, "CLS",                       "Clear the BeebView command screen" },
    { VIEWBBC_HELP_CONFIGURATION, "BUFFERSIZE [n[KB|MB]]",      "Show/set file buffer maximum (32 KB..100 MB)" },
    { VIEWBBC_HELP_CONFIGURATION, "LINENUMS [ON|OFF]",           "Show/set display-only line numbers" },
    { VIEWBBC_HELP_CONFIGURATION, "ROWCOLS [ON|OFF]",            "Show/set cursor row/column ruler overlay" },
    { VIEWBBC_HELP_CONFIGURATION, "VER",                         "Show the BeebView version" },
    { VIEWBBC_HELP_CONFIGURATION, "CONFIG",                    "Open the configuration editor" },
    { VIEWBBC_HELP_CONFIGURATION, "CONFIG LOC",                "Show configuration file location" },
    { VIEWBBC_HELP_CONFIGURATION, "CONFIG RESET",              "Reset configuration to defaults (asks first)" },
    { VIEWBBC_HELP_FILE_SYSTEM, "NEW",                       "Clear the document and start a new file" },
    { VIEWBBC_HELP_FILE_SYSTEM, "READ <filename> [marker]",           "Insert a text file at the cursor" },
    { VIEWBBC_HELP_FILE_SYSTEM, "WRITE <filename> m1 m2",       "Write marked text to a native file" },
    { VIEWBBC_HELP_FILE_SYSTEM, "PRINT [target]",               "Print/export using print.target or an override" },
    { VIEWBBC_HELP_FILE_SYSTEM, "EXPORT <filename>",            "Export the whole document as plain text" },
    { VIEWBBC_HELP_EDITING, "COUNT [m1 m2]",             "Count whole document or between markers" },
    { VIEWBBC_HELP_EDITING, "SEARCH <text> [m1 m2]",     "Find exact text, optionally between markers" },
    { VIEWBBC_HELP_EDITING, "CHANGE <old> <new> [m1 m2]", "Change matches, optionally between markers" },
    { VIEWBBC_HELP_EDITING, "REPLACE <old> <new> [m1 m2]", "Confirm each replacement with Y/N" },
    { VIEWBBC_HELP_EDITING, "R <old> <new> [m1 m2]",       "Short form of REPLACE" },
    { VIEWBBC_HELP_EDITING, "FOLD [0|1]",                 "Show or set case folding for CHANGE/REPLACE" },
    { VIEWBBC_HELP_EDITING, "FORMAT [m1 m2]",             "Format all text or a marked range" },
    { VIEWBBC_HELP_EDITING, "SCREEN",                    "Display the document on screen" },
    { VIEWBBC_HELP_CONFIGURATION, "MODE [3]",                  "Show/set VIEW screen mode (mode 3 supported)" },
    { VIEWBBC_HELP_CONFIGURATION, "SETUP [FKW]",              "Set/report Format, Justify and insert states" },
    { VIEWBBC_HELP_EDITING, "EXIT",                      "Exit BeebView" },
    { VIEWBBC_HELP_EDITING, "QUIT",                      "Exit BeebView" }
};

const ViewBBCHelpEntry *viewbbc_help_entries(size_t *count_out) {
    if (count_out) *count_out = sizeof(help_entries) / sizeof(help_entries[0]);
    return help_entries;
}

const char *viewbbc_help_group_name(ViewBBCHelpGroup group) {
    switch (group) {
        case VIEWBBC_HELP_FILE_SYSTEM: return "File system:";
        case VIEWBBC_HELP_EDITING: return "Editing:";
        case VIEWBBC_HELP_CONFIGURATION: return "Configuration:";
        default: return "Commands:";
    }
}

static int ascii_ieq_string(const char *a, const char *b) {
    if (!a || !b) return 0;
    while (*a && *b) {
        if (toupper((unsigned char)*a) != toupper((unsigned char)*b)) return 0;
        ++a;
        ++b;
    }
    return *a == '\0' && *b == '\0';
}

int viewbbc_help_topic_parse(const char *subject, ViewBBCHelpTopic *topic_out) {
    if (!subject || !topic_out) return 0;
    if (ascii_ieq_string(subject, "EDIT") || ascii_ieq_string(subject, "E")) *topic_out = VIEWBBC_HELP_TOPIC_EDIT;
    else if (ascii_ieq_string(subject, "FILE") || ascii_ieq_string(subject, "FILES") || ascii_ieq_string(subject, "F")) *topic_out = VIEWBBC_HELP_TOPIC_FILES;
    else if (ascii_ieq_string(subject, "KEYS") || ascii_ieq_string(subject, "K")) *topic_out = VIEWBBC_HELP_TOPIC_KEYS;
    else if (ascii_ieq_string(subject, "CONF") || ascii_ieq_string(subject, "CONFIG") || ascii_ieq_string(subject, "C")) *topic_out = VIEWBBC_HELP_TOPIC_CONF;
    else return 0;
    return 1;
}
