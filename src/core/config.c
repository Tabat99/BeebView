#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#include "viewbbc/config.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <direct.h>
#define VIEWBBC_MKDIR(path) _mkdir(path)
#else
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#define VIEWBBC_MKDIR(path) mkdir((path), 0700)
#endif

#define ARRAY_LEN(a) (sizeof(a) / sizeof((a)[0]))

typedef struct { ViewBBCKey key; const char *name; const char *def; } KeySetting;
static const KeySetting g_keys[] = {
    {VIEWBBC_KEY_HOME,"home","Home"},{VIEWBBC_KEY_END,"end","End"},
    {VIEWBBC_KEY_PAGE_UP,"page_up","PageUp, Ctrl+Up"},{VIEWBBC_KEY_PAGE_DOWN,"page_down","PageDown, Ctrl+Down"},
    {VIEWBBC_KEY_TAB,"tab","Tab"},
    {VIEWBBC_KEY_BACKSPACE,"backspace","Backspace"},{VIEWBBC_KEY_DELETE,"delete","Delete"},
    {VIEWBBC_KEY_RETURN,"return","Enter"},
    {VIEWBBC_KEY_F0,"f0","F10"},{VIEWBBC_KEY_F1,"f1","F1"},{VIEWBBC_KEY_F2,"f2","F2"},
    {VIEWBBC_KEY_F3,"f3","F3"},{VIEWBBC_KEY_F4,"f4","F4"},{VIEWBBC_KEY_F5,"f5","F5"},
    {VIEWBBC_KEY_F6,"f6","F6"},{VIEWBBC_KEY_F7,"f7","F7"},{VIEWBBC_KEY_F8,"f8","F8"},
    {VIEWBBC_KEY_F9,"f9","F9"},
    {VIEWBBC_KEY_CTRL_F0,"ctrl_f0","Ctrl+F10"},{VIEWBBC_KEY_CTRL_F1,"ctrl_f1","Ctrl+F1"},
    {VIEWBBC_KEY_CTRL_F2,"ctrl_f2","Ctrl+F2"},{VIEWBBC_KEY_CTRL_F3,"ctrl_f3","Ctrl+F3"},
    {VIEWBBC_KEY_CTRL_F4,"ctrl_f4","Ctrl+F4, Insert"},{VIEWBBC_KEY_CTRL_F5,"ctrl_f5","Ctrl+F5"},
    {VIEWBBC_KEY_CTRL_F6,"ctrl_f6","Ctrl+F6"},{VIEWBBC_KEY_CTRL_F7,"ctrl_f7","Ctrl+F7"},
    {VIEWBBC_KEY_CTRL_F8,"ctrl_f8","Ctrl+F8"},{VIEWBBC_KEY_CTRL_F9,"ctrl_f9","Ctrl+F9"},
    {VIEWBBC_KEY_SHIFT_F0,"shift_f0","Shift+F10"},{VIEWBBC_KEY_SHIFT_F1,"shift_f1","Shift+F1"},
    {VIEWBBC_KEY_SHIFT_F2,"shift_f2","Shift+F2"},{VIEWBBC_KEY_SHIFT_F3,"shift_f3","Shift+F3"},
    {VIEWBBC_KEY_SHIFT_F4,"shift_f4","Shift+F4"},{VIEWBBC_KEY_SHIFT_F5,"shift_f5","Shift+F5"},
    {VIEWBBC_KEY_SHIFT_F6,"shift_f6","Shift+F6"},{VIEWBBC_KEY_SHIFT_F7,"shift_f7","Shift+F7"},
    {VIEWBBC_KEY_SHIFT_F8,"shift_f8","Shift+F8"},{VIEWBBC_KEY_SHIFT_F9,"shift_f9","Shift+F9"},
    {VIEWBBC_KEY_COPY,"copy","F11"},
    {VIEWBBC_KEY_FIND,"find","Ctrl+F"},{VIEWBBC_KEY_FIND_REPLACE,"find_replace","Ctrl+H"},
    {VIEWBBC_KEY_CLIPBOARD_PASTE,"paste","Ctrl+V"},{VIEWBBC_KEY_CLIPBOARD_COPY,"clipboard_copy","Ctrl+C"},
    {VIEWBBC_KEY_CLIPBOARD_CUT,"cut","Ctrl+X"},{VIEWBBC_KEY_SELECT_ALL,"select_all","Ctrl+A"},
    {VIEWBBC_KEY_QUIT,"quit","Ctrl+Q"}
};

static int eqi(const char *a, const char *b) {
    while (*a && *b) {
        if (tolower((unsigned char)*a++) != tolower((unsigned char)*b++)) return 0;
    }
    return *a == '\0' && *b == '\0';
}

static char *trim(char *s) {
    char *end;
    while (isspace((unsigned char)*s)) ++s;
    end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1])) --end;
    *end = '\0';
    return s;
}

static ViewBBCPhysicalBase parse_base(const char *s) {
    static const struct { const char *name; ViewBBCPhysicalBase base; } names[] = {
        {"Left",VIEWBBC_PHYS_LEFT},{"Right",VIEWBBC_PHYS_RIGHT},{"Up",VIEWBBC_PHYS_UP},{"Down",VIEWBBC_PHYS_DOWN},
        {"Home",VIEWBBC_PHYS_HOME},{"End",VIEWBBC_PHYS_END},{"PageUp",VIEWBBC_PHYS_PAGEUP},{"PageDown",VIEWBBC_PHYS_PAGEDOWN},
        {"Insert",VIEWBBC_PHYS_INSERT},{"Tab",VIEWBBC_PHYS_TAB},{"Backspace",VIEWBBC_PHYS_BACKSPACE},{"Delete",VIEWBBC_PHYS_DELETE},
        {"Enter",VIEWBBC_PHYS_ENTER},{"Return",VIEWBBC_PHYS_ENTER},{"Escape",VIEWBBC_PHYS_ESCAPE},{"Esc",VIEWBBC_PHYS_ESCAPE}
    };
    for (size_t i=0;i<ARRAY_LEN(names);++i) if (eqi(s,names[i].name)) return names[i].base;
    if ((s[0]=='F'||s[0]=='f') && isdigit((unsigned char)s[1])) {
        char *end=NULL; long n=strtol(s+1,&end,10);
        if (*end=='\0' && n>=1 && n<=12) return (ViewBBCPhysicalBase)(VIEWBBC_PHYS_F1 + n - 1);
    }
    if (s[0] && !s[1] && isdigit((unsigned char)s[0]))
        return (ViewBBCPhysicalBase)(VIEWBBC_PHYS_0 + (s[0]-'0'));
    if (s[0] && !s[1] && isalpha((unsigned char)s[0]))
        return (ViewBBCPhysicalBase)(VIEWBBC_PHYS_A + (toupper((unsigned char)s[0])-'A'));
    if (eqi(s,"Space")) return VIEWBBC_PHYS_SPACE;
    if (eqi(s,"Minus") || strcmp(s,"-")==0) return VIEWBBC_PHYS_MINUS;
    if (eqi(s,"Equals") || strcmp(s,"=")==0) return VIEWBBC_PHYS_EQUALS;
    if (eqi(s,"LeftBracket") || strcmp(s,"[")==0) return VIEWBBC_PHYS_LEFTBRACKET;
    if (eqi(s,"RightBracket") || strcmp(s,"]")==0) return VIEWBBC_PHYS_RIGHTBRACKET;
    if (eqi(s,"Backslash") || strcmp(s,"\\")==0) return VIEWBBC_PHYS_BACKSLASH;
    if (eqi(s,"Semicolon") || strcmp(s,";")==0) return VIEWBBC_PHYS_SEMICOLON;
    if (eqi(s,"Apostrophe") || strcmp(s,"'")==0) return VIEWBBC_PHYS_APOSTROPHE;
    if (eqi(s,"Grave") || strcmp(s,"`")==0) return VIEWBBC_PHYS_GRAVE;
    if (eqi(s,"Comma") || strcmp(s,",")==0) return VIEWBBC_PHYS_COMMA;
    if (eqi(s,"Period") || strcmp(s,".")==0) return VIEWBBC_PHYS_PERIOD;
    if (eqi(s,"Slash") || strcmp(s,"/")==0) return VIEWBBC_PHYS_SLASH;
    return VIEWBBC_PHYS_NONE;
}

int viewbbc_config_parse_physical_key(const char *text, ViewBBCPhysicalKey *key) {
    char buf[64], *part, *next;
    if (!text || !key || strlen(text) >= sizeof(buf)) return 0;
    strcpy(buf,text); key->base=VIEWBBC_PHYS_NONE; key->modifiers=0;
    part=buf;
    while (part) {
        next=strchr(part,'+'); if (next) *next++='\0'; part=trim(part);
        if (eqi(part,"Ctrl")||eqi(part,"Control")) key->modifiers|=VIEWBBC_MOD_CTRL;
        else if (eqi(part,"Shift")) key->modifiers|=VIEWBBC_MOD_SHIFT;
        else if (eqi(part,"Alt")) key->modifiers|=VIEWBBC_MOD_ALT;
        else {
            ViewBBCPhysicalBase base=parse_base(part);
            if (base==VIEWBBC_PHYS_NONE || key->base!=VIEWBBC_PHYS_NONE) return 0;
            key->base=base;
        }
        part=next;
    }
    return key->base != VIEWBBC_PHYS_NONE;
}

static int set_binding(ViewBBCConfig *c, ViewBBCKey key, const char *text) {
    char buf[VIEWBBC_CONFIG_BINDING_MAX + 1];
    char *part, *next;
    unsigned count = 0;
    ViewBBCPhysicalKey parsed[VIEWBBC_CONFIG_KEY_ALIASES];
    if (!text || strlen(text) >= sizeof(buf)) return 0;
    strcpy(buf, text);
    part = buf;
    while (part && count < VIEWBBC_CONFIG_KEY_ALIASES) {
        ViewBBCPhysicalKey p;
        next = strchr(part, ',');
        if (next) *next++ = '\0';
        part = trim(part);
        if (!viewbbc_config_parse_physical_key(part, &p)) return 0;
        parsed[count++] = p;
        part = next;
    }
    if (!count || (part && *trim(part))) return 0;
    memset(c->key_bindings[key], 0, sizeof(c->key_bindings[key]));
    for (unsigned i = 0; i < count; ++i) c->key_bindings[key][i] = parsed[i];
    c->key_binding_count[key] = (unsigned char)count;
    snprintf(c->key_binding_text[key], sizeof(c->key_binding_text[key]), "%s", text);
    return 1;
}

void viewbbc_config_defaults(ViewBBCConfig *c) {
    memset(c,0,sizeof(*c));
    for (size_t i=0;i<ARRAY_LEN(g_keys);++i) (void)set_binding(c,g_keys[i].key,g_keys[i].def);
    c->mouse_buttons[1]=VIEWBBC_MOUSE_SELECT;
    for (int i=1;i<=6;++i) c->mouse_button_modifiers[i]=0;
    for (int i=2;i<=6;++i) c->mouse_buttons[i]=VIEWBBC_MOUSE_NONE;
    c->mouse_wheel_lines=3;
    c->buffer_size=VIEWBBC_DEFAULT_WORKSPACE_LIMIT;
    c->line_numbers=0;
    c->rowcols=0;
    snprintf(c->print_target,sizeof(c->print_target),"printer:default");
    snprintf(c->font,sizeof(c->font),"mode7");
    c->font_size=20;
}

int viewbbc_config_default_path(char *out,size_t n) {
#ifdef _WIN32
    const char *home=getenv("USERPROFILE");
    const char *mid="/beebview/viewbeeb.conf";
#else
    const char *home=getenv("HOME");
    const char *mid="/.config/beebview/viewbeeb.conf";
#endif
    if (!home || !*home || strlen(home)+strlen(mid)+1>n) return 0;
    snprintf(out,n,"%s%s",home,mid); return 1;
}

static int make_parent_dir(const char *path) {
    char tmp[VIEWBBC_CONFIG_PATH_MAX+1]; char *slash;
    if (strlen(path)>=sizeof(tmp)) return 0;
    strcpy(tmp,path); slash=strrchr(tmp,'/');
#ifdef _WIN32
    { char *b=strrchr(tmp,'\\'); if (b && (!slash || b>slash)) slash=b; }
#endif
    if (!slash) return 1;
    *slash='\0';
#ifndef _WIN32
    char *second=strrchr(tmp,'/');
    if (second && strcmp(second+1,"beebview")==0) {
        *second='\0'; if (*tmp && VIEWBBC_MKDIR(tmp)!=0 && errno!=EEXIST) return 0; *second='/';
    }
#endif
    if (VIEWBBC_MKDIR(tmp)!=0 && errno!=EEXIST) return 0;
    return 1;
}

const char *viewbbc_config_mouse_action_name(ViewBBCMouseAction a) {
    switch(a){case VIEWBBC_MOUSE_SELECT:return "select";case VIEWBBC_MOUSE_POSITION:return "position";
    case VIEWBBC_MOUSE_COPY:return "copy";case VIEWBBC_MOUSE_MOVE:return "move";case VIEWBBC_MOUSE_DELETE:return "delete";
    case VIEWBBC_MOUSE_FORMAT:return "format";default:return "none";}
}
static int mouse_action(const char *s, ViewBBCMouseAction *a) {
    for (int i=VIEWBBC_MOUSE_NONE;i<=VIEWBBC_MOUSE_FORMAT;++i)
        if (eqi(s,viewbbc_config_mouse_action_name((ViewBBCMouseAction)i))) {*a=(ViewBBCMouseAction)i;return 1;}
    return 0;
}

int viewbbc_config_write_defaults(const ViewBBCConfig *c,const char *path) {
    FILE *f; if (!make_parent_dir(path)) return 0; f=fopen(path,"wb"); if(!f) return 0;
    fprintf(f,"# BeebView configuration\n# Written by BeebView. You may also edit values by hand.\n\n");
    fprintf(f,"# Keyboard bindings. Syntax: Ctrl+/Shift+/Alt+ plus key name.\n");
    for(size_t i=0;i<ARRAY_LEN(g_keys);++i) fprintf(f,"key.%s = %s\n",g_keys[i].name,c->key_binding_text[g_keys[i].key]);
    fprintf(f,"\n# Mouse button actions: none, select, position, copy, move, delete, format\n");
    for(int i=1;i<=6;++i) {
        fprintf(f,"mouse.button%d = %s\n",i,viewbbc_config_mouse_action_name(c->mouse_buttons[i]));
        fprintf(f,"mouse.button%d.modifiers = %s%s%s%s\n", i,
                c->mouse_button_modifiers[i] ? "" : "none",
                (c->mouse_button_modifiers[i] & VIEWBBC_MOD_CTRL) ? "Ctrl" : "",
                (c->mouse_button_modifiers[i] & VIEWBBC_MOD_SHIFT) ? ((c->mouse_button_modifiers[i] & VIEWBBC_MOD_CTRL) ? "+Shift" : "Shift") : "",
                (c->mouse_button_modifiers[i] & VIEWBBC_MOD_ALT) ? ((c->mouse_button_modifiers[i] & (VIEWBBC_MOD_CTRL|VIEWBBC_MOD_SHIFT)) ? "+Alt" : "Alt") : "");
    }
    fprintf(f,"mouse.wheel_lines = %d\n\n",c->mouse_wheel_lines);
    { char size_text[48]; viewbbc_workspace_format_size(c->buffer_size,size_text,sizeof(size_text));
      fprintf(f,"# File buffer maximum. Range: 32 KB to 100 MB.\nbuffersize = %s\n\n",size_text); }
    fprintf(f,"# Optional display-only line numbers.\nlinenums = %s\n\n", c->line_numbers ? "on" : "off");
    fprintf(f,"# Optional cursor row/column overlay on the ruler.\nrowcols = %s\n\n", c->rowcols ? "on" : "off");
    fprintf(f,"# PRINT destination. Use printer:default, printer:<name>, or file. File asks for type/name when PRINT is used.\n");
    fprintf(f,"print.target = %s\n\n",c->print_target);
    fprintf(f,"# Display font. mode7 is currently the only built-in font.\nfont = %s\nfont.size = %d\n",c->font,c->font_size);
    return fclose(f)==0;
}

static int parse_modifiers(const char *text, unsigned *mods) {
    char buf[64], *part, *next;
    if (!text || !mods || strlen(text) >= sizeof(buf)) return 0;
    if (eqi(text, "none") || !*text) { *mods = 0; return 1; }
    strcpy(buf, text); *mods = 0; part = buf;
    while (part) {
        next = strchr(part, '+'); if (next) *next++ = '\0'; part = trim(part);
        if (eqi(part,"Ctrl")||eqi(part,"Control")) *mods |= VIEWBBC_MOD_CTRL;
        else if (eqi(part,"Shift")) *mods |= VIEWBBC_MOD_SHIFT;
        else if (eqi(part,"Alt")) *mods |= VIEWBBC_MOD_ALT;
        else return 0;
        part = next;
    }
    return 1;
}

static int append_binding_text(ViewBBCConfig *c, ViewBBCKey key, const char *value);

static int parse_setting(ViewBBCConfig *c,const char *name,const char *value,int line) {
    /* Arrow keys and Escape are intentionally hard-wired and old settings are ignored. */
    if (eqi(name,"key.left") || eqi(name,"key.right") || eqi(name,"key.up") ||
        eqi(name,"key.down") || eqi(name,"key.escape")) return 1;
    for(size_t i=0;i<ARRAY_LEN(g_keys);++i) {
        char full[48]; snprintf(full,sizeof(full),"key.%s",g_keys[i].name);
        if(eqi(name,full)) { if(!set_binding(c,g_keys[i].key,value)) fprintf(stderr,"viewbeeb.conf:%d: invalid key binding '%s'\n",line,value); return 1; }
    }
    if (strlen(name)==23 && strncmp(name,"mouse.button",12)==0 && name[12]>='1'&&name[12]<='6' && strcmp(name+13,".modifiers")==0) {
        unsigned mods=0; if(!parse_modifiers(value,&mods)){fprintf(stderr,"viewbeeb.conf:%d: invalid mouse modifiers '%s'\n",line,value);return 1;} c->mouse_button_modifiers[name[12]-'0']=mods; return 1;
    }
    if (strlen(name)==13 && strncmp(name,"mouse.button",12)==0 && name[12]>='1'&&name[12]<='6') {
        ViewBBCMouseAction a; if(!mouse_action(value,&a)){fprintf(stderr,"viewbeeb.conf:%d: invalid mouse action '%s'\n",line,value);return 1;} c->mouse_buttons[name[12]-'0']=a; return 1;
    }
    if(eqi(name,"mouse.wheel_lines")){char *e;long v=strtol(value,&e,10);if(*e||v<1||v>100){fprintf(stderr,"viewbeeb.conf:%d: mouse.wheel_lines must be 1..100\n",line);return 1;}c->mouse_wheel_lines=(int)v;return 1;}
    if(eqi(name,"buffersize")){size_t bytes=0;if(!viewbbc_workspace_parse_size(value,&bytes)||bytes<VIEWBBC_MIN_WORKSPACE_LIMIT||bytes>VIEWBBC_MAX_WORKSPACE_LIMIT){fprintf(stderr,"viewbeeb.conf:%d: buffersize must be 32 KB..100 MB\n",line);return 1;}c->buffer_size=bytes;return 1;}
    if(eqi(name,"linenums")){if(eqi(value,"on")||eqi(value,"yes")||strcmp(value,"1")==0)c->line_numbers=1;else if(eqi(value,"off")||eqi(value,"no")||strcmp(value,"0")==0)c->line_numbers=0;else fprintf(stderr,"viewbeeb.conf:%d: linenums must be on or off\n",line);return 1;}
    if(eqi(name,"rowcols")){if(eqi(value,"on")||eqi(value,"yes")||strcmp(value,"1")==0)c->rowcols=1;else if(eqi(value,"off")||eqi(value,"no")||strcmp(value,"0")==0)c->rowcols=0;else fprintf(stderr,"viewbeeb.conf:%d: rowcols must be on or off\n",line);return 1;}
    if(eqi(name,"print.target")){if(strlen(value)>VIEWBBC_CONFIG_VALUE_MAX){fprintf(stderr,"viewbeeb.conf:%d: print target too long\n",line);return 1;}snprintf(c->print_target,sizeof(c->print_target),"%s",value);return 1;}
    if(eqi(name,"font")){if(strlen(value)>VIEWBBC_CONFIG_FONT_MAX){fprintf(stderr,"viewbeeb.conf:%d: font name too long\n",line);return 1;}snprintf(c->font,sizeof(c->font),"%s",value);return 1;}
    if(eqi(name,"font.size")){char *e;long v=strtol(value,&e,10);if(*e||v<8||v>80){fprintf(stderr,"viewbeeb.conf:%d: font.size must be 8..80\n",line);return 1;}c->font_size=(int)v;return 1;}
    fprintf(stderr,"viewbeeb.conf:%d: unknown setting '%s' ignored\n",line,name); return 1;
}

int viewbbc_config_load_file(ViewBBCConfig *c,const char *path) {
    FILE *f=fopen(path,"rb"); char linebuf[2048]; char legacy_insert[VIEWBBC_CONFIG_BINDING_MAX+1]={0}; int line=0;
    if(!f) return 0;
    while(fgets(linebuf,sizeof(linebuf),f)){char *s,*eq,*hash,*name,*value; ++line; s=trim(linebuf); if(!*s||*s=='#')continue; hash=strchr(s,'#');if(hash){*hash='\0';s=trim(s);} eq=strchr(s,'=');if(!eq){fprintf(stderr,"viewbeeb.conf:%d: expected name = value\n",line);continue;}*eq++='\0';name=trim(s);value=trim(eq);
        /* Migrate 0.5.27-0.5.29's separate Insert alias into Insert Mode. */
        if(eqi(name,"key.insert")){snprintf(legacy_insert,sizeof(legacy_insert),"%s",value);continue;}
        parse_setting(c,name,value,line);}
    if(ferror(f)){fclose(f);return 0;} fclose(f);
    if(legacy_insert[0]&&!append_binding_text(c,VIEWBBC_KEY_CTRL_F4,legacy_insert))
        fprintf(stderr,"viewbeeb.conf: could not migrate legacy key.insert binding '%s'\n",legacy_insert);
    snprintf(c->path,sizeof(c->path),"%s",path); return 1;
}

int viewbbc_config_load_or_create(ViewBBCConfig *c) {
    char path[VIEWBBC_CONFIG_PATH_MAX+1]; viewbbc_config_defaults(c);
    if(!viewbbc_config_default_path(path,sizeof(path))) return 0;
    if(viewbbc_config_load_file(c,path)) return 1;
    if(errno!=ENOENT) return 0;
    snprintf(c->path,sizeof(c->path),"%s",path);
    if(!viewbbc_config_write_defaults(c,path)) return 0;
    return 1;
}

ViewBBCKey viewbbc_config_key_for_physical(const ViewBBCConfig *c,ViewBBCPhysicalKey p) {
    if(p.base==VIEWBBC_PHYS_LEFT && p.modifiers==VIEWBBC_MOD_SHIFT) return VIEWBBC_KEY_SHIFT_LEFT;
    if(p.base==VIEWBBC_PHYS_RIGHT && p.modifiers==VIEWBBC_MOD_SHIFT) return VIEWBBC_KEY_SHIFT_RIGHT;
    if(p.base==VIEWBBC_PHYS_LEFT && p.modifiers==0) return VIEWBBC_KEY_LEFT;
    if(p.base==VIEWBBC_PHYS_RIGHT && p.modifiers==0) return VIEWBBC_KEY_RIGHT;
    if(p.base==VIEWBBC_PHYS_UP && p.modifiers==0) return VIEWBBC_KEY_UP;
    if(p.base==VIEWBBC_PHYS_DOWN && p.modifiers==0) return VIEWBBC_KEY_DOWN;
    if(p.base==VIEWBBC_PHYS_ESCAPE && p.modifiers==0) return VIEWBBC_KEY_ESCAPE;
    if(!c||p.base==VIEWBBC_PHYS_NONE) return VIEWBBC_KEY_NONE;
    for(size_t i=0;i<ARRAY_LEN(g_keys);++i){ViewBBCKey k=g_keys[i].key;for(unsigned j=0;j<c->key_binding_count[k];++j){ViewBBCPhysicalKey b=c->key_bindings[k][j];if(b.base==p.base&&b.modifiers==p.modifiers)return k;}}
    return VIEWBBC_KEY_NONE;
}


const char *viewbbc_config_key_setting_name(ViewBBCKey key) {
    for (size_t i=0;i<ARRAY_LEN(g_keys);++i) if (g_keys[i].key==key) return g_keys[i].name;
    return NULL;
}

const char *viewbbc_config_key_setting_description(ViewBBCKey key) {
    switch (key) {
        case VIEWBBC_KEY_LEFT: return "Cursor left"; case VIEWBBC_KEY_RIGHT: return "Cursor right";
        case VIEWBBC_KEY_UP: return "Cursor up"; case VIEWBBC_KEY_DOWN: return "Cursor down";
        case VIEWBBC_KEY_HOME: return "Beginning of line"; case VIEWBBC_KEY_END: return "End of line";
        case VIEWBBC_KEY_PAGE_UP: return "Page up"; case VIEWBBC_KEY_PAGE_DOWN: return "Page down";
        case VIEWBBC_KEY_INSERT: return "Insert mode"; case VIEWBBC_KEY_TAB: return "Next ruler tab";
        case VIEWBBC_KEY_BACKSPACE: return "VIEW black DELETE"; case VIEWBBC_KEY_DELETE: return "Delete character";
        case VIEWBBC_KEY_RETURN: return "Return / next line"; case VIEWBBC_KEY_ESCAPE: return "COMMAND/TEXT toggle";
        case VIEWBBC_KEY_F0: return "Format block"; case VIEWBBC_KEY_F1: return "Top of text";
        case VIEWBBC_KEY_F2: return "Bottom of text"; case VIEWBBC_KEY_F3: return "Delete end of line";
        case VIEWBBC_KEY_F4: return "Beginning of line (f4)"; case VIEWBBC_KEY_F5: return "End of line (f5)";
        case VIEWBBC_KEY_F6: return "Insert line"; case VIEWBBC_KEY_F7: return "Delete line";
        case VIEWBBC_KEY_F8: return "Insert character"; case VIEWBBC_KEY_F9: return "Delete character (f9)";
        case VIEWBBC_KEY_CTRL_F0: return "Delete block"; case VIEWBBC_KEY_CTRL_F1: return "Next match";
        case VIEWBBC_KEY_CTRL_F2: return "Format mode"; case VIEWBBC_KEY_CTRL_F3: return "Justify mode";
        case VIEWBBC_KEY_CTRL_F4: return "Insert mode toggle"; case VIEWBBC_KEY_CTRL_F5: return "Default ruler";
        case VIEWBBC_KEY_CTRL_F6: return "Split line"; case VIEWBBC_KEY_CTRL_F7: return "Concatenate lines";
        case VIEWBBC_KEY_CTRL_F8: return "Mark as ruler"; case VIEWBBC_KEY_CTRL_F9: return "Ctrl-f9";
        case VIEWBBC_KEY_SHIFT_F0: return "Move block"; case VIEWBBC_KEY_SHIFT_F1: return "Swap case";
        case VIEWBBC_KEY_SHIFT_F2: return "Shift-f2"; case VIEWBBC_KEY_SHIFT_F3: return "Delete up to character";
        case VIEWBBC_KEY_SHIFT_F4: return "Shift-f4"; case VIEWBBC_KEY_SHIFT_F5: return "Shift-f5";
        case VIEWBBC_KEY_SHIFT_F6: return "Go to marker"; case VIEWBBC_KEY_SHIFT_F7: return "Set marker";
        case VIEWBBC_KEY_SHIFT_F8: return "Edit command"; case VIEWBBC_KEY_SHIFT_F9: return "Delete command";
        case VIEWBBC_KEY_COPY: return "COPY block";
        case VIEWBBC_KEY_FIND: return "Find";
        case VIEWBBC_KEY_FIND_REPLACE: return "Find and replace";
        case VIEWBBC_KEY_CLIPBOARD_PASTE: return "Paste";
        case VIEWBBC_KEY_CLIPBOARD_COPY: return "Clipboard copy";
        case VIEWBBC_KEY_CLIPBOARD_CUT: return "Cut";
        case VIEWBBC_KEY_SELECT_ALL: return "Select all";
        case VIEWBBC_KEY_CLS: return "Clear screen (CLS)";
        case VIEWBBC_KEY_BREAK: return "BREAK";
        case VIEWBBC_KEY_QUIT: return "Quit BeebView";
        default: return "Key binding";
    }
}

static const char *base_name(ViewBBCPhysicalBase base) {
    static const char *fn[] = {"F1","F2","F3","F4","F5","F6","F7","F8","F9","F10","F11","F12"};
    static const char *digit[] = {"0","1","2","3","4","5","6","7","8","9"};
    switch(base) {
        case VIEWBBC_PHYS_LEFT:return "Left";case VIEWBBC_PHYS_RIGHT:return "Right";case VIEWBBC_PHYS_UP:return "Up";case VIEWBBC_PHYS_DOWN:return "Down";
        case VIEWBBC_PHYS_HOME:return "Home";case VIEWBBC_PHYS_END:return "End";case VIEWBBC_PHYS_PAGEUP:return "PageUp";case VIEWBBC_PHYS_PAGEDOWN:return "PageDown";
        case VIEWBBC_PHYS_INSERT:return "Insert";case VIEWBBC_PHYS_TAB:return "Tab";case VIEWBBC_PHYS_BACKSPACE:return "Backspace";case VIEWBBC_PHYS_DELETE:return "Delete";
        case VIEWBBC_PHYS_ENTER:return "Enter";case VIEWBBC_PHYS_ESCAPE:return "Escape";case VIEWBBC_PHYS_SPACE:return "Space";
        case VIEWBBC_PHYS_MINUS:return "Minus";case VIEWBBC_PHYS_EQUALS:return "Equals";case VIEWBBC_PHYS_LEFTBRACKET:return "LeftBracket";case VIEWBBC_PHYS_RIGHTBRACKET:return "RightBracket";
        case VIEWBBC_PHYS_BACKSLASH:return "Backslash";case VIEWBBC_PHYS_SEMICOLON:return "Semicolon";case VIEWBBC_PHYS_APOSTROPHE:return "Apostrophe";case VIEWBBC_PHYS_GRAVE:return "Grave";
        case VIEWBBC_PHYS_COMMA:return "Comma";case VIEWBBC_PHYS_PERIOD:return "Period";case VIEWBBC_PHYS_SLASH:return "Slash";
        default: break;
    }
    if (base>=VIEWBBC_PHYS_F1 && base<=VIEWBBC_PHYS_F12) return fn[base-VIEWBBC_PHYS_F1];
    if (base>=VIEWBBC_PHYS_0 && base<=VIEWBBC_PHYS_9) return digit[base-VIEWBBC_PHYS_0];
    static char letter[2];
    if (base>=VIEWBBC_PHYS_A && base<=VIEWBBC_PHYS_Z) { letter[0]=(char)('A'+base-VIEWBBC_PHYS_A);letter[1]='\0';return letter; }
    return NULL;
}

int viewbbc_config_format_physical_key(ViewBBCPhysicalKey p, char *out, size_t n) {
    const char *base=base_name(p.base); size_t used=0;
    if (!out || n == 0 || !base) return 0;
    out[0] = '\0';
    const struct {unsigned bit;const char *name;} mods[]={{VIEWBBC_MOD_CTRL,"Ctrl+"},{VIEWBBC_MOD_SHIFT,"Shift+"},{VIEWBBC_MOD_ALT,"Alt+"}};
    for(size_t i=0;i<ARRAY_LEN(mods);++i) if(p.modifiers&mods[i].bit){size_t l=strlen(mods[i].name);if(used+l>=n)return 0;memcpy(out+used,mods[i].name,l);used+=l;}
    if (used + strlen(base) >= n) return 0;
    strcpy(out + used, base);
    return 1;
}

int viewbbc_config_physical_key_allowed(ViewBBCPhysicalKey p) {
    if (p.base==VIEWBBC_PHYS_NONE || p.base==VIEWBBC_PHYS_ESCAPE) return 0;
    if (p.base==VIEWBBC_PHYS_LEFT || p.base==VIEWBBC_PHYS_RIGHT || p.base==VIEWBBC_PHYS_UP || p.base==VIEWBBC_PHYS_DOWN) return 0;
    if (p.base == VIEWBBC_PHYS_F12) return 0; /* hard-wired BREAK */
    if (p.base>=VIEWBBC_PHYS_F1 && p.base<=VIEWBBC_PHYS_F11) return 1;
    if (p.base==VIEWBBC_PHYS_HOME || p.base==VIEWBBC_PHYS_END || p.base==VIEWBBC_PHYS_PAGEUP || p.base==VIEWBBC_PHYS_PAGEDOWN || p.base==VIEWBBC_PHYS_INSERT) return 1;
    return p.modifiers != 0;
}

int viewbbc_config_binding_conflicts(const ViewBBCConfig *c, ViewBBCKey key, ViewBBCPhysicalKey p) {
    if(!c)return 1;
    for(size_t i=0;i<ARRAY_LEN(g_keys);++i){ViewBBCKey k=g_keys[i].key;if(k==key)continue;for(unsigned j=0;j<c->key_binding_count[k];++j){ViewBBCPhysicalKey b=c->key_bindings[k][j];if(b.base==p.base&&b.modifiers==p.modifiers)return 1;}}
    return 0;
}

static int rebuild_binding_text(ViewBBCConfig *c, ViewBBCKey key) {
    char all[VIEWBBC_CONFIG_BINDING_MAX+1] = {0};
    for (unsigned i=0;i<c->key_binding_count[key];++i) {
        char one[64]; size_t used=strlen(all);
        if (!viewbbc_config_format_physical_key(c->key_bindings[key][i],one,sizeof(one))) return 0;
        if (used + (i?2:0) + strlen(one) >= sizeof(all)) return 0;
        if (i) strcat(all, ", ");
        strcat(all, one);
    }
    snprintf(c->key_binding_text[key],sizeof(c->key_binding_text[key]),"%s",all);
    return 1;
}

int viewbbc_config_add_key_binding(ViewBBCConfig *c, ViewBBCKey key, ViewBBCPhysicalKey p) {
    if(!c||!viewbbc_config_physical_key_allowed(p)||viewbbc_config_binding_conflicts(c,key,p))return 0;
    for(unsigned i=0;i<c->key_binding_count[key];++i) {
        ViewBBCPhysicalKey b=c->key_bindings[key][i];
        if(b.base==p.base&&b.modifiers==p.modifiers)return 1;
    }
    if(c->key_binding_count[key]>=VIEWBBC_CONFIG_KEY_ALIASES)return 0;
    c->key_bindings[key][c->key_binding_count[key]++]=p;
    return rebuild_binding_text(c,key);
}

static int append_binding_text(ViewBBCConfig *c, ViewBBCKey key, const char *value) {
    char buf[VIEWBBC_CONFIG_BINDING_MAX+1], *part, *next;
    if(!c||!value||strlen(value)>=sizeof(buf))return 0;
    strcpy(buf,value); part=buf;
    while(part){ViewBBCPhysicalKey p;next=strchr(part,',');if(next)*next++='\0';part=trim(part);
        if(!viewbbc_config_parse_physical_key(part,&p)||!viewbbc_config_add_key_binding(c,key,p)) return 0;
        part=next;
    }
    return 1;
}

int viewbbc_config_set_key_binding(ViewBBCConfig *c, ViewBBCKey key, ViewBBCPhysicalKey p) {
    char text[VIEWBBC_CONFIG_BINDING_MAX+1];
    if(!c||!viewbbc_config_physical_key_allowed(p)||viewbbc_config_binding_conflicts(c,key,p)||!viewbbc_config_format_physical_key(p,text,sizeof(text)))return 0;
    return set_binding(c,key,text);
}

int viewbbc_config_save(const ViewBBCConfig *c) {
    char tmp[VIEWBBC_CONFIG_PATH_MAX+24];
    if(!c||!c->path[0]||strlen(c->path)+16>=sizeof(tmp))return 0;
#ifdef _WIN32
    snprintf(tmp,sizeof(tmp),"%s.tmp",c->path);
#else
    snprintf(tmp,sizeof(tmp),"%s.tmp.XXXXXX",c->path);
    int fd=mkstemp(tmp); if(fd<0)return 0; (void)fchmod(fd,0600); close(fd);
#endif
    if(!viewbbc_config_write_defaults(c,tmp)){remove(tmp);return 0;}
#ifndef _WIN32
    (void)chmod(tmp,0600);
#else
    remove(c->path);
#endif
    if(rename(tmp,c->path)!=0){remove(tmp);return 0;}
    return 1;
}

int viewbbc_config_reset(ViewBBCConfig *c) {
    char path[VIEWBBC_CONFIG_PATH_MAX+1];
    if(!c)return 0;
    if(c->path[0]) snprintf(path,sizeof(path),"%s",c->path);
    else if(!viewbbc_config_default_path(path,sizeof(path))) return 0;
    viewbbc_config_defaults(c); snprintf(c->path,sizeof(c->path),"%s",path);
    return viewbbc_config_save(c);
}
