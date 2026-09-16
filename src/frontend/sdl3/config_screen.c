#include "config_screen.h"
#include "bitmap_font.h"
#include "viewbbc/print.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define COLS 80
#define ROWS 25
#define FIRST_ROW 3
#define LAST_ROW 21
#define VISIBLE_ROWS (LAST_ROW-FIRST_ROW+1)
#define MAX_ITEMS 80

typedef enum { ITEM_KEY, ITEM_MOUSE, ITEM_WHEEL, ITEM_PRINT_DEST, ITEM_PRINTER, ITEM_FONT, ITEM_FONT_SIZE } ItemType;
typedef struct { ItemType type; int id; char label[38]; } Item;

static unsigned mods_from_sdl(SDL_Keymod mod) {
    unsigned r = 0;
    if (mod & SDL_KMOD_CTRL) r |= VIEWBBC_MOD_CTRL;
    if (mod & SDL_KMOD_SHIFT) r |= VIEWBBC_MOD_SHIFT;
    if (mod & SDL_KMOD_ALT) r |= VIEWBBC_MOD_ALT;
    return r;
}

static ViewBBCPhysicalKey physical_from_sdl(SDL_Keycode sym, SDL_Keymod mod) {
    ViewBBCPhysicalKey p={VIEWBBC_PHYS_NONE,mods_from_sdl(mod)};
    switch(sym) {
        case SDLK_LEFT:p.base=VIEWBBC_PHYS_LEFT;break;case SDLK_RIGHT:p.base=VIEWBBC_PHYS_RIGHT;break;
        case SDLK_UP:p.base=VIEWBBC_PHYS_UP;break;case SDLK_DOWN:p.base=VIEWBBC_PHYS_DOWN;break;
        case SDLK_HOME:p.base=VIEWBBC_PHYS_HOME;break;case SDLK_END:p.base=VIEWBBC_PHYS_END;break;
        case SDLK_PAGEUP:p.base=VIEWBBC_PHYS_PAGEUP;break;case SDLK_PAGEDOWN:p.base=VIEWBBC_PHYS_PAGEDOWN;break;
        case SDLK_INSERT:p.base=VIEWBBC_PHYS_INSERT;break;case SDLK_TAB:p.base=VIEWBBC_PHYS_TAB;break;
        case SDLK_BACKSPACE:p.base=VIEWBBC_PHYS_BACKSPACE;break;case SDLK_DELETE:p.base=VIEWBBC_PHYS_DELETE;break;
        case SDLK_RETURN:case SDLK_KP_ENTER:p.base=VIEWBBC_PHYS_ENTER;break;case SDLK_ESCAPE:p.base=VIEWBBC_PHYS_ESCAPE;break;
        case SDLK_SPACE:p.base=VIEWBBC_PHYS_SPACE;break;case SDLK_MINUS:p.base=VIEWBBC_PHYS_MINUS;break;case SDLK_EQUALS:p.base=VIEWBBC_PHYS_EQUALS;break;
        case SDLK_LEFTBRACKET:p.base=VIEWBBC_PHYS_LEFTBRACKET;break;case SDLK_RIGHTBRACKET:p.base=VIEWBBC_PHYS_RIGHTBRACKET;break;
        case SDLK_BACKSLASH:p.base=VIEWBBC_PHYS_BACKSLASH;break;case SDLK_SEMICOLON:p.base=VIEWBBC_PHYS_SEMICOLON;break;
        case SDLK_APOSTROPHE:p.base=VIEWBBC_PHYS_APOSTROPHE;break;case SDLK_GRAVE:p.base=VIEWBBC_PHYS_GRAVE;break;
        case SDLK_COMMA:p.base=VIEWBBC_PHYS_COMMA;break;case SDLK_PERIOD:p.base=VIEWBBC_PHYS_PERIOD;break;case SDLK_SLASH:p.base=VIEWBBC_PHYS_SLASH;break;
        case SDLK_F1:p.base=VIEWBBC_PHYS_F1;break;case SDLK_F2:p.base=VIEWBBC_PHYS_F2;break;case SDLK_F3:p.base=VIEWBBC_PHYS_F3;break;
        case SDLK_F4:p.base=VIEWBBC_PHYS_F4;break;case SDLK_F5:p.base=VIEWBBC_PHYS_F5;break;case SDLK_F6:p.base=VIEWBBC_PHYS_F6;break;
        case SDLK_F7:p.base=VIEWBBC_PHYS_F7;break;case SDLK_F8:p.base=VIEWBBC_PHYS_F8;break;case SDLK_F9:p.base=VIEWBBC_PHYS_F9;break;
        case SDLK_F10:p.base=VIEWBBC_PHYS_F10;break;case SDLK_F11:p.base=VIEWBBC_PHYS_F11;break;case SDLK_F12:p.base=VIEWBBC_PHYS_F12;break;
        default:
            if(sym>=SDLK_0&&sym<=SDLK_9)p.base=(ViewBBCPhysicalBase)(VIEWBBC_PHYS_0+(sym-SDLK_0));
            else if(sym>=SDLK_A&&sym<=SDLK_Z)p.base=(ViewBBCPhysicalBase)(VIEWBBC_PHYS_A+(sym-SDLK_A));
            break;
    }
    return p;
}

static void draw_char(SDL_Renderer *r,int x,int y,unsigned char ch,float scale,int reverse) {
    float cw=VIEWBBC_BITMAP_FONT_WIDTH*scale,chh=VIEWBBC_BITMAP_FONT_HEIGHT*scale;
    SDL_FRect bg={(float)x*cw,(float)y*chh,cw,chh};
    Uint8 back=reverse?255:0, fore=reverse?0:255;
    SDL_SetRenderDrawColor(r,back,back,back,255); SDL_RenderFillRect(r,&bg);
    if(ch<32||ch>126)return;
    const uint16_t *g=viewbbc_bitmap_font_glyph(ch); SDL_SetRenderDrawColor(r,fore,fore,fore,255);
    for(int gy=0;gy<VIEWBBC_BITMAP_FONT_HEIGHT;++gy)for(int gx=0;gx<VIEWBBC_BITMAP_FONT_WIDTH;++gx)
        if(g[gy]&(uint16_t)(0x8000u>>gx)){SDL_FRect px={bg.x+gx*scale,bg.y+gy*scale,scale,scale};SDL_RenderFillRect(r,&px);}
}
static void text(SDL_Renderer*r,int x,int y,const char*s,float sc,int rev){for(int i=0;s&&s[i]&&x+i<COLS;++i)draw_char(r,x+i,y,(unsigned char)s[i],sc,rev);}
static void clear_row(SDL_Renderer*r,int y,float sc,int rev){for(int x=0;x<COLS;++x)draw_char(r,x,y,' ',sc,rev);}

static int build_items(Item *it) {
    int n=0;
    for(int k=VIEWBBC_KEY_LEFT;k<=VIEWBBC_KEY_QUIT&&n<MAX_ITEMS;++k){const char*name=viewbbc_config_key_setting_name((ViewBBCKey)k);if(!name)continue;it[n].type=ITEM_KEY;it[n].id=k;snprintf(it[n].label,sizeof(it[n].label),"Key: %s",viewbbc_config_key_setting_description((ViewBBCKey)k));++n;}
    for(int a=VIEWBBC_MOUSE_SELECT;a<=VIEWBBC_MOUSE_FORMAT&&n<MAX_ITEMS;++a){it[n].type=ITEM_MOUSE;it[n].id=a;snprintf(it[n].label,sizeof(it[n].label),"Mouse: %s",viewbbc_config_mouse_action_name((ViewBBCMouseAction)a));++n;}
    it[n++]=(Item){ITEM_WHEEL,0,"Mouse wheel lines"}; it[n++]=(Item){ITEM_PRINT_DEST,0,"Print destination"}; it[n++]=(Item){ITEM_PRINTER,0,"Printer"};
    it[n++]=(Item){ITEM_FONT,0,"Font"}; it[n++]=(Item){ITEM_FONT_SIZE,0,"Font size"}; return n;
}

static int mouse_for_action(const ViewBBCConfig *c, int action, unsigned *mods) {
    for (int b=1;b<=6;++b) if (c->mouse_buttons[b] == (ViewBBCMouseAction)action) { if(mods)*mods=c->mouse_button_modifiers[b]; return b; }
    if (mods) *mods = 0;
    return 0;
}
static void mods_text(unsigned m,char*out,size_t n){if(!m){snprintf(out,n,"none");return;}out[0]='\0';if(m&VIEWBBC_MOD_CTRL)strcat(out,"Ctrl+");if(m&VIEWBBC_MOD_SHIFT)strcat(out,"Shift+");if(m&VIEWBBC_MOD_ALT)strcat(out,"Alt+");size_t l=strlen(out);if(l&&out[l-1]=='+')out[l-1]='\0';}
static void item_value(const ViewBBCConfig*c,const Item*i,char*out,size_t n){
    if(i->type==ITEM_KEY)snprintf(out,n,"%s",c->key_binding_text[i->id]);
    else if(i->type==ITEM_MOUSE){unsigned m;int b=mouse_for_action(c,i->id,&m);char mb[32];mods_text(m,mb,sizeof(mb));if (b) { if (m) snprintf(out,n,"%s+Button%d",mb,b); else snprintf(out,n,"Button%d",b); } else snprintf(out,n,"none");}
    else if(i->type==ITEM_WHEEL)snprintf(out,n,"%d",c->mouse_wheel_lines);
    else if(i->type==ITEM_PRINT_DEST)snprintf(out,n,"%s",strncmp(c->print_target,"printer:",8u)==0?"Printer":"File");
    else if(i->type==ITEM_PRINTER){if(strncmp(c->print_target,"printer:",8u)==0)snprintf(out,n,"%s",c->print_target+8u);else snprintf(out,n,"<choose when Printer selected>");}
    else if(i->type==ITEM_FONT)snprintf(out,n,"%s",c->font);else snprintf(out,n,"%d",c->font_size);
}

static void render(SDL_Renderer*r,const ViewBBCConfig*c,const Item*items,int count,int selected,int top,int capture,const char*status,float sc){
    SDL_SetRenderDrawColor(r,0,0,0,255);SDL_RenderClear(r);
    text(r,0,0,"BeebView Configuration",sc,0); text(r,0,1,"Up/Down navigate  Enter/click edit  Esc cancels an edit",sc,0);
    text(r,0,2,"Bindings may have aliases. Bare arrows/Escape are fixed; bare character/control keys are reserved.",sc,0);
    for(int row=0;row<VISIBLE_ROWS;++row){int idx=top+row;int y=FIRST_ROW+row;clear_row(r,y,sc,idx==selected);if(idx>=count)continue;char val[520],line[96];item_value(c,&items[idx],val,sizeof(val));snprintf(line,sizeof(line),"%-36.36s [%-38.38s]",items[idx].label,val);text(r,0,y,line,sc,idx==selected);}
    clear_row(r,22,sc,0); if(status&&*status)text(r,0,22,status,sc,0); else if(capture)text(r,0,22,"Waiting for input...",sc,0);
    clear_row(r,23,sc,0);text(r,0,23,"[ ACCEPT ]",sc,selected==count);text(r,14,23,"[ CANCEL ]",sc,selected==count+1);
    clear_row(r,24,sc,0);text(r,0,24,"Changes are written to viewbeeb.conf only when ACCEPT is chosen.",sc,0);SDL_RenderPresent(r);
}

static int mouse_cell(SDL_Renderer*r,float wx,float wy,int*wcx,int*wcy,float cw,float ch){float x,y;if(!SDL_RenderCoordinatesFromWindow(r,wx,wy,&x,&y))return 0;*wcx=(int)(x/cw);*wcy=(int)(y/ch);return 1;}

static int choose_printer(SDL_Renderer *r, ViewBBCConfig *c, float sc, char *status, size_t status_size) {
    ViewBBCPrinterInfo printers[VIEWBBC_PRINTER_LIST_MAX];
    size_t count = 0;
    char msg[192] = {0};
    if (!viewbbc_printer_list(printers, VIEWBBC_PRINTER_LIST_MAX, &count, msg, sizeof(msg))) {
        snprintf(status, status_size, "%s", msg);
        return 0;
    }
    if (count == 0u) { snprintf(status, status_size, "No printers available"); return 0; }

    int selected = 0;
    const char *cur = strncmp(c->print_target, "printer:", 8u) == 0 ? c->print_target + 8u : "default";
    for (size_t i = 0; i < count; ++i) if (strcmp(printers[i].name, cur) == 0) { selected = (int)i; break; }

    SDL_Event e;
    for (;;) {
        SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
        SDL_RenderClear(r);
        text(r, 0, 0, "Choose printer", sc, 0);
        text(r, 0, 1, "Up/Down select   Enter accepts   Esc cancels", sc, 0);
        int top = selected > 18 ? selected - 18 : 0;
        for (int row = 0; row < 19; ++row) {
            int idx = top + row;
            clear_row(r, 3 + row, sc, idx == selected);
            if (idx >= (int)count) continue;
            char line[78];
            snprintf(line, sizeof(line), "%-40s %-12s %s", printers[idx].name,
                     printers[idx].is_default ? "[default]" : "", printers[idx].status);
            text(r, 0, 3 + row, line, sc, idx == selected);
        }
        SDL_RenderPresent(r);
        if (!SDL_WaitEvent(&e)) return 0;
        if (e.type == SDL_EVENT_QUIT) return 0;
        if (e.type != SDL_EVENT_KEY_DOWN) continue;
        if (e.key.key == SDLK_ESCAPE || e.key.key == SDLK_F12) return 0;
        if (e.key.key == SDLK_UP && selected > 0) { --selected; continue; }
        if (e.key.key == SDLK_DOWN && selected + 1 < (int)count) { ++selected; continue; }
        if (e.key.key == SDLK_RETURN || e.key.key == SDLK_KP_ENTER) {
            snprintf(c->print_target, sizeof(c->print_target), "printer:%s", printers[selected].name);
            snprintf(status, status_size, "Printer set to %s", printers[selected].name);
            return 1;
        }
    }
}

int viewbbc_config_screen_run(SDL_Window *window, SDL_Renderer *renderer, ViewBBCConfig *config,float scale,int lw,int lh) {
    (void)lw;(void)lh; if(!window||!renderer||!config)return 0;
    ViewBBCConfig work=*config; Item items[MAX_ITEMS]; int count=build_items(items),selected=0,top=0,capture=0,text_edit=0; char edit[VIEWBBC_CONFIG_VALUE_MAX+1]={0},status[160]={0};
    const float cw=VIEWBBC_BITMAP_FONT_WIDTH*scale,ch=VIEWBBC_BITMAP_FONT_HEIGHT*scale; SDL_Event e;
    for(;;){if(selected<top)top=selected;if(selected>=top+VISIBLE_ROWS)top=selected-VISIBLE_ROWS+1;if(top<0)top=0;if(top>count-VISIBLE_ROWS&&count>VISIBLE_ROWS)top=count-VISIBLE_ROWS;render(renderer,&work,items,count,selected,top,capture,status,scale);
        if(!SDL_WaitEvent(&e))return 0;
        if(e.type==SDL_EVENT_QUIT)return 0;
        if(capture && e.type==SDL_EVENT_MOUSE_BUTTON_DOWN){
            if(items[selected].type!=ITEM_MOUSE){snprintf(status,sizeof(status),"Mouse clicks can only be assigned to mouse actions");continue;}
            int b=(int)e.button.button;unsigned m=mods_from_sdl(SDL_GetModState());if(b<1||b>6){snprintf(status,sizeof(status),"Only mouse buttons 1-6 are accepted");continue;}
            int used=work.mouse_buttons[b];if(used!=VIEWBBC_MOUSE_NONE&&used!=items[selected].id){snprintf(status,sizeof(status),"Button%d is already used by %s",b,viewbbc_config_mouse_action_name((ViewBBCMouseAction)used));continue;}
            for(int j=1;j<=6;++j)if(work.mouse_buttons[j]==(ViewBBCMouseAction)items[selected].id){work.mouse_buttons[j]=VIEWBBC_MOUSE_NONE;work.mouse_button_modifiers[j]=0;}
            work.mouse_buttons[b]=(ViewBBCMouseAction)items[selected].id;work.mouse_button_modifiers[b]=m;capture=0;status[0]='\0';continue;
        }
        if(e.type==SDL_EVENT_MOUSE_BUTTON_DOWN&&!capture){int x,y;if(mouse_cell(renderer,e.button.x,e.button.y,&x,&y,cw,ch)){if(y>=FIRST_ROW&&y<=LAST_ROW){int idx=top+y-FIRST_ROW;if(idx<count){selected=idx;e.type=SDL_EVENT_KEY_DOWN;e.key.key=SDLK_RETURN;e.key.mod=0;}}else if(y==23){if(x<11){selected=count;e.type=SDL_EVENT_KEY_DOWN;e.key.key=SDLK_RETURN;e.key.mod=0;}else if(x>=14&&x<25){selected=count+1;e.type=SDL_EVENT_KEY_DOWN;e.key.key=SDLK_RETURN;e.key.mod=0;}}}}
        if(text_edit&&e.type==SDL_EVENT_TEXT_INPUT){size_t a=strlen(edit),b=strlen(e.text.text);if(a+b<sizeof(edit)){memcpy(edit+a,e.text.text,b+1);}continue;}
        if (e.type != SDL_EVENT_KEY_DOWN) continue;
        SDL_Keycode key = e.key.key;
        if (key == SDLK_F12) return 0;
        if(text_edit){if(key==SDLK_F12)return 0;if(key==SDLK_ESCAPE){text_edit=0;capture=0;status[0]='\0';continue;}if(key==SDLK_BACKSPACE){size_t n=strlen(edit);if(n)edit[n-1]='\0';continue;}if(key==SDLK_RETURN||key==SDLK_KP_ENTER){char*end=NULL;long v;
                if(items[selected].type==ITEM_WHEEL){v=strtol(edit,&end,10);if(!*edit||*end||v<1||v>100){snprintf(status,sizeof(status),"Wheel lines must be 1..100");continue;}work.mouse_wheel_lines=(int)v;}
                else if(items[selected].type==ITEM_FONT_SIZE){v=strtol(edit,&end,10);if(!*edit||*end||v<8||v>80){snprintf(status,sizeof(status),"Font size must be 8..80");continue;}work.font_size=(int)v;}
                else if(items[selected].type==ITEM_FONT){if(!*edit){snprintf(status,sizeof(status),"Font cannot be empty");continue;}snprintf(work.font,sizeof(work.font),"%.*s",VIEWBBC_CONFIG_FONT_MAX,edit);}
                text_edit=0;capture=0;status[0]='\0';continue;}continue;}
        if(capture){if(key==SDLK_F12)return 0;if(key==SDLK_ESCAPE){capture=0;status[0]='\0';continue;}if(items[selected].type!=ITEM_KEY)continue;ViewBBCPhysicalKey p=physical_from_sdl(key,e.key.mod);
            if(!viewbbc_config_physical_key_allowed(p)){snprintf(status,sizeof(status),"That key is reserved or needs Ctrl, Shift or Alt");continue;}
            if(viewbbc_config_binding_conflicts(&work,(ViewBBCKey)items[selected].id,p)){snprintf(status,sizeof(status),"That key combination is already used elsewhere");continue;}
            if(!viewbbc_config_add_key_binding(&work,(ViewBBCKey)items[selected].id,p)){snprintf(status,sizeof(status),"Cannot add binding (maximum aliases reached)");continue;}capture=0;status[0]='\0';continue;}
        if(key==SDLK_UP){
            if(selected==count||selected==count+1)selected=count-1;
            else if(selected>0)--selected;
            status[0]='\0';
            continue;
        }
        if(key==SDLK_DOWN){
            if(selected<count-1)++selected;
            else if(selected==count-1)selected=count;
            status[0]='\0';
            continue;
        }
        if(key==SDLK_LEFT){
            if(selected==count+1)selected=count;
            status[0]='\0';
            continue;
        }
        if(key==SDLK_RIGHT){
            if(selected==count)selected=count+1;
            status[0]='\0';
            continue;
        }
        if(key==SDLK_ESCAPE){selected=count+1;continue;}if(key!=SDLK_RETURN&&key!=SDLK_KP_ENTER)continue;
        if(selected==count){if(viewbbc_config_save(&work)){*config=work;return 1;}snprintf(status,sizeof(status),"Could not write viewbeeb.conf");continue;}
        if(selected==count+1)return 0;
        if(items[selected].type==ITEM_PRINT_DEST){if(strncmp(work.print_target,"printer:",8u)==0)snprintf(work.print_target,sizeof(work.print_target),"file");else snprintf(work.print_target,sizeof(work.print_target),"printer:default");status[0]='\0';continue;}
        if(items[selected].type==ITEM_PRINTER){if(strncmp(work.print_target,"printer:",8u)!=0){snprintf(status,sizeof(status),"Select Printer as the destination first");continue;}choose_printer(renderer,&work,scale,status,sizeof(status));continue;}
        if(items[selected].type==ITEM_KEY){capture=1;snprintf(status,sizeof(status),"Press an additional key binding (Esc cancels capture)");continue;}
        if(items[selected].type==ITEM_MOUSE){capture=1;snprintf(status,sizeof(status),"Click mouse button 1-6, optionally with Ctrl/Shift/Alt");continue;}
        item_value(&work,&items[selected],edit,sizeof(edit));text_edit=1;capture=1;snprintf(status,sizeof(status),"Type a new value; Enter accepts, Esc cancels");
    }
}
