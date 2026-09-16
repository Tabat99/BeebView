#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#include "viewbbc/print.h"
#include "atomic_file.h"

#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef _WIN32
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#endif

static void set_message(char *message, size_t size, const char *text) {
    if (!message || size == 0u) return;
    (void)snprintf(message, size, "%s", text ? text : "");
}

static int write_document_text(const ViewBBCDocument *document, FILE *out) {
    size_t lines = viewbbc_document_line_count(document);
    for (size_t line = 0; line < lines; ++line) {
        size_t length = viewbbc_document_line_length(document, line);
        for (size_t col = 0; col < length; ++col) {
            if (fputc((int)viewbbc_document_char_at(document, line, col), out) == EOF) return 0;
        }
        if (line + 1u < lines && fputc('\n', out) == EOF) return 0;
    }
    return !ferror(out);
}

typedef struct { char *data; size_t len, cap; } Buffer;
static int buf_need(Buffer *b, size_t add) {
    if (add > SIZE_MAX - b->len - 1u) return 0;
    size_t need = b->len + add + 1u;
    if (need <= b->cap) return 1;
    size_t cap = b->cap ? b->cap : 1024u;
    while (cap < need) { if (cap > SIZE_MAX / 2u) { cap = need; break; } cap *= 2u; }
    char *p = (char *)realloc(b->data, cap); if (!p) return 0;
    b->data = p; b->cap = cap; return 1;
}
static int buf_addn(Buffer *b, const char *s, size_t n) { if (!buf_need(b,n)) return 0; memcpy(b->data+b->len,s,n); b->len+=n; b->data[b->len]='\0'; return 1; }
static int buf_add(Buffer *b, const char *s) { return buf_addn(b,s,strlen(s)); }
static int buf_ch(Buffer *b, char c) { return buf_addn(b,&c,1u); }
static void buf_free(Buffer *b) { free(b->data); b->data=NULL; b->len=b->cap=0; }

static int pdf_escape_char(Buffer *b, unsigned char ch) {
    if (ch == '(' || ch == ')' || ch == '\\') return buf_ch(b,'\\') && buf_ch(b,(char)ch);
    if (ch >= 32u && ch <= 126u) return buf_ch(b,(char)ch);
    return buf_ch(b,'?');
}

static int write_pdf(const ViewBBCDocument *document, FILE *out) {
    const size_t per_page = 56u;
    size_t lines = viewbbc_document_line_count(document);
    size_t pages = lines ? (lines + per_page - 1u) / per_page : 1u;
    size_t objects = 3u + pages * 2u;
    long *off = (long *)calloc(objects + 1u, sizeof(*off));
    if (!off) return 0;
#define P(...) do { if (fprintf(out, __VA_ARGS__) < 0) { free(off); return 0; } } while (0)
    P("%%PDF-1.4\n%%\xE2\xE3\xCF\xD3\n");
    off[1]=ftell(out); P("1 0 obj << /Type /Catalog /Pages 2 0 R >> endobj\n");
    off[2]=ftell(out); P("2 0 obj << /Type /Pages /Count %zu /Kids [",pages);
    for(size_t p=0;p<pages;++p) P(" %zu 0 R",3u+p*2u);
    P(" ] >> endobj\n");
    for(size_t p=0;p<pages;++p){
        size_t page_obj=3u+p*2u, content_obj=page_obj+1u;
        Buffer s={0}; if(!buf_add(&s,"BT /F1 10 Tf 72 760 Td 12 TL\n")){free(off);return 0;}
        size_t first=p*per_page, last=first+per_page; if(last>lines)last=lines;
        for(size_t line=first;line<last;++line){
            if(!buf_ch(&s,'(')){buf_free(&s);free(off);return 0;}
            size_t len=viewbbc_document_line_length(document,line);
            for(size_t c=0;c<len;++c) if(!pdf_escape_char(&s,viewbbc_document_char_at(document,line,c))){buf_free(&s);free(off);return 0;}
            if(!buf_add(&s,") Tj T*\n")){buf_free(&s);free(off);return 0;}
        }
        if(!buf_add(&s,"ET\n")){buf_free(&s);free(off);return 0;}
        off[page_obj]=ftell(out); P("%zu 0 obj << /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Resources << /Font << /F1 %zu 0 R >> >> /Contents %zu 0 R >> endobj\n",page_obj,objects,content_obj);
        off[content_obj]=ftell(out); P("%zu 0 obj << /Length %zu >> stream\n",content_obj,s.len);
        if(fwrite(s.data,1,s.len,out)!=s.len){buf_free(&s);free(off);return 0;} P("endstream\nendobj\n"); buf_free(&s);
    }
    off[objects]=ftell(out); P("%zu 0 obj << /Type /Font /Subtype /Type1 /BaseFont /Courier >> endobj\n",objects);
    long xref=ftell(out); P("xref\n0 %zu\n0000000000 65535 f \n",objects+1u);
    for(size_t i=1;i<=objects;++i) P("%010ld 00000 n \n",off[i]);
    P("trailer << /Size %zu /Root 1 0 R >>\nstartxref\n%ld\n%%%%EOF\n",objects+1u,xref);
    free(off); return !ferror(out);
#undef P
}

static uint32_t crc32_bytes(const unsigned char *data,size_t len){uint32_t c=0xffffffffu;for(size_t i=0;i<len;++i){c^=data[i];for(int j=0;j<8;++j)c=(c>>1)^((0u-(c&1u))&0xedb88320u);}return c^0xffffffffu;}
static void le16(FILE*f,uint16_t v){fputc(v&255,f);fputc((v>>8)&255,f);} static void le32(FILE*f,uint32_t v){le16(f,(uint16_t)v);le16(f,(uint16_t)(v>>16));}
typedef struct { const char *name; const unsigned char *data; size_t len; uint32_t crc, offset; } ZipEntry;
static int zip_store(FILE*out,ZipEntry*e,size_t n){
    for(size_t i=0;i<n;++i){e[i].crc=crc32_bytes(e[i].data,e[i].len);long o=ftell(out);if(o<0||e[i].len>UINT32_MAX)return 0;e[i].offset=(uint32_t)o;le32(out,0x04034b50u);le16(out,20);le16(out,0);le16(out,0);le16(out,0);le16(out,0);le32(out,e[i].crc);le32(out,(uint32_t)e[i].len);le32(out,(uint32_t)e[i].len);le16(out,(uint16_t)strlen(e[i].name));le16(out,0);fwrite(e[i].name,1,strlen(e[i].name),out);if(e[i].len&&fwrite(e[i].data,1,e[i].len,out)!=e[i].len)return 0;}
    long cd=ftell(out);if(cd<0)return 0;
    for(size_t i=0;i<n;++i){le32(out,0x02014b50u);le16(out,20);le16(out,20);le16(out,0);le16(out,0);le16(out,0);le16(out,0);le32(out,e[i].crc);le32(out,(uint32_t)e[i].len);le32(out,(uint32_t)e[i].len);le16(out,(uint16_t)strlen(e[i].name));le16(out,0);le16(out,0);le16(out,0);le16(out,0);le32(out,0);le32(out,e[i].offset);fwrite(e[i].name,1,strlen(e[i].name),out);}
    long end=ftell(out);if(end<0)return 0;le32(out,0x06054b50u);le16(out,0);le16(out,0);le16(out,(uint16_t)n);le16(out,(uint16_t)n);le32(out,(uint32_t)(end-cd));le32(out,(uint32_t)cd);le16(out,0);return !ferror(out);
}
static int xml_text(Buffer*b,unsigned char ch){if(ch=='&')return buf_add(b,"&amp;");if(ch=='<')return buf_add(b,"&lt;");if(ch=='>')return buf_add(b,"&gt;");if(ch=='\"')return buf_add(b,"&quot;");if(ch=='\'')return buf_add(b,"&apos;");if(ch>=32&&ch<=126)return buf_ch(b,(char)ch);return buf_ch(b,'?');}
static int write_odt(const ViewBBCDocument*doc,FILE*out){
    static const unsigned char mime[]="application/vnd.oasis.opendocument.text";
    static const unsigned char manifest[]="<?xml version=\"1.0\" encoding=\"UTF-8\"?><manifest:manifest xmlns:manifest=\"urn:oasis:names:tc:opendocument:xmlns:manifest:1.0\" manifest:version=\"1.2\"><manifest:file-entry manifest:full-path=\"/\" manifest:version=\"1.2\" manifest:media-type=\"application/vnd.oasis.opendocument.text\"/><manifest:file-entry manifest:full-path=\"content.xml\" manifest:media-type=\"text/xml\"/></manifest:manifest>";
    Buffer b={0}; if(!buf_add(&b,"<?xml version=\"1.0\" encoding=\"UTF-8\"?><office:document-content xmlns:office=\"urn:oasis:names:tc:opendocument:xmlns:office:1.0\" xmlns:text=\"urn:oasis:names:tc:opendocument:xmlns:text:1.0\" office:version=\"1.2\"><office:body><office:text>"))return 0;
    size_t lines=viewbbc_document_line_count(doc);for(size_t l=0;l<lines;++l){if(!buf_add(&b,"<text:p>")){buf_free(&b);return 0;}size_t len=viewbbc_document_line_length(doc,l);for(size_t c=0;c<len;++c)if(!xml_text(&b,viewbbc_document_char_at(doc,l,c))){buf_free(&b);return 0;}if(!buf_add(&b,"</text:p>")){buf_free(&b);return 0;}}
    if(!buf_add(&b,"</office:text></office:body></office:document-content>")){buf_free(&b);return 0;}
    ZipEntry e[3]={{"mimetype",mime,sizeof(mime)-1u,0,0},{"META-INF/manifest.xml",manifest,sizeof(manifest)-1u,0,0},{"content.xml",(const unsigned char*)b.data,b.len,0,0}};int ok=zip_store(out,e,3);buf_free(&b);return ok;
}

#ifndef _WIN32
static int safe_queue_name(const char *queue){if(!queue||!*queue)return 0;for(const unsigned char*p=(const unsigned char*)queue;*p;++p)if(!(isalnum((int)*p)||*p=='_'||*p=='-'||*p=='.'))return 0;return 1;}
static int write_document_fd(const ViewBBCDocument*d,int fd){size_t lines=viewbbc_document_line_count(d);for(size_t l=0;l<lines;++l){size_t len=viewbbc_document_line_length(d,l);for(size_t c=0;c<len;++c){unsigned char ch=viewbbc_document_char_at(d,l,c);ssize_t w;do{w=write(fd,&ch,1u);}while(w<0&&errno==EINTR);if(w!=1)return 0;}if(l+1u<lines){unsigned char nl='\n';if(write(fd,&nl,1u)!=1)return 0;}}return 1;}
static int run_capture(char *const argv[], char *out, size_t out_size) {
    int fds[2]; if(pipe(fds)!=0)return -1; pid_t pid=fork();
    if(pid<0){close(fds[0]);close(fds[1]);return -1;}
    if(pid==0){dup2(fds[1],STDOUT_FILENO);dup2(fds[1],STDERR_FILENO);close(fds[0]);close(fds[1]);execvp(argv[0],argv);_exit(127);}
    close(fds[1]); size_t used=0; if(out&&out_size)out[0]='\0';
    while(out&&used+1u<out_size){ssize_t r=read(fds[0],out+used,out_size-used-1u);if(r<0&&errno==EINTR)continue;if(r<=0)break;used+=(size_t)r;} if(out&&out_size)out[used]='\0';
    char sink[256]; while(read(fds[0],sink,sizeof(sink))>0){} close(fds[0]); int st=0;if(waitpid(pid,&st,0)<0)return -1;return WIFEXITED(st)?WEXITSTATUS(st):-1;
}
static int printer_status_disabled(const char *q,char*msg,size_t n){if(!q||strcmp(q,"default")==0)return 0;if(!safe_queue_name(q))return 0;char line[512];char *argv[]={"lpstat","-p",(char*)q,NULL};int st=run_capture(argv,line,sizeof(line));if(st==0&&strstr(line," disabled ")){char*nl=strchr(line,'\n');if(nl)*nl='\0';snprintf(msg,n,"Printer unavailable: %.220s",line);return 1;}return 0;}
static int print_with_lp(const ViewBBCDocument*d,const char*q,char*msg,size_t n){
    if(printer_status_disabled(q,msg,n))return 0;
    int pipefd[2];if(pipe(pipefd)!=0){snprintf(msg,n,"Cannot open print pipe: %s",strerror(errno));return 0;}pid_t pid=fork();if(pid<0){close(pipefd[0]);close(pipefd[1]);snprintf(msg,n,"Cannot start lp: %s",strerror(errno));return 0;}if(pid==0){dup2(pipefd[0],STDIN_FILENO);close(pipefd[0]);close(pipefd[1]);if(!q||strcmp(q,"default")==0)execlp("lp","lp",(char*)NULL);else execlp("lp","lp","-d",q,(char*)NULL);_exit(127);}close(pipefd[0]);void(*old)(int)=signal(SIGPIPE,SIG_IGN);int wrote=write_document_fd(d,pipefd[1]);int write_errno=errno;close(pipefd[1]);signal(SIGPIPE,old);int status=0;if(waitpid(pid,&status,0)<0){snprintf(msg,n,"Cannot get print status: %s",strerror(errno));return 0;}if(!wrote){snprintf(msg,n,"Printer/spooler stopped accepting data: %s",strerror(write_errno));return 0;}if(!WIFEXITED(status)||WEXITSTATUS(status)!=0){if(WIFEXITED(status)&&WEXITSTATUS(status)==127)set_message(msg,n,"Printing failed: lp command is not installed");else snprintf(msg,n,"Printing failed: spooler returned status %d",WIFEXITED(status)?WEXITSTATUS(status):-1);return 0;}return 1;
}
#endif

int viewbbc_printer_list(ViewBBCPrinterInfo *printers,size_t capacity,size_t *count,char *message,size_t message_size){if(count)*count=0;
#ifdef _WIN32
    (void)printers;(void)capacity;set_message(message,message_size,"Printer discovery is not available on Windows yet");return 0;
#else
    if(!printers||capacity==0u){set_message(message,message_size,"Printer list buffer is empty");return 0;}char output[8192];char *argv_a[]={"lpstat","-a",NULL};int rc=run_capture(argv_a,output,sizeof(output));if(rc!=0){set_message(message,message_size,"Cannot query printers (is CUPS/lpstat installed?)");return 0;}size_t n=0;char *save=NULL;for(char*line=strtok_r(output,"\n",&save);line&&n<capacity;line=strtok_r(NULL,"\n",&save)){char name[VIEWBBC_PRINTER_NAME_MAX+1]={0};if(sscanf(line,"%127s",name)==1&&safe_queue_name(name)){snprintf(printers[n].name,sizeof(printers[n].name),"%s",name);printers[n].accepting=strstr(line,"accepting requests")!=NULL;printers[n].is_default=0;snprintf(printers[n].status,sizeof(printers[n].status),"%s",printers[n].accepting?"Ready/accepting jobs":"Not accepting jobs");++n;}}
    char *argv_d[]={"lpstat","-d",NULL};if(run_capture(argv_d,output,sizeof(output))==0){char*colon=strchr(output,':');if(colon){++colon;while(*colon&&isspace((unsigned char)*colon))++colon;char*end=colon;while(*end&&!isspace((unsigned char)*end))++end;*end='\0';for(size_t i=0;i<n;++i)if(strcmp(printers[i].name,colon)==0)printers[i].is_default=1;}}
    if(count) *count=n;
    set_message(message,message_size,n?"Printers found":"No printers available");
    return 1;
#endif
}
int viewbbc_print_document(const ViewBBCDocument *document,const char *target,char *message,size_t message_size){
    if(!document||!target||!*target){set_message(message,message_size,"Print target is empty");return 0;}
    const char *path=NULL; enum {FMT_NONE,FMT_TEXT,FMT_PDF,FMT_ODT} fmt=FMT_NONE;
    if(strncmp(target,"file:",5u)==0){path=target+5u;fmt=FMT_TEXT;}else if(strncmp(target,"text:",5u)==0){path=target+5u;fmt=FMT_TEXT;}else if(strncmp(target,"pdf:",4u)==0){path=target+4u;fmt=FMT_PDF;}else if(strncmp(target,"odt:",4u)==0){path=target+4u;fmt=FMT_ODT;}
    if(fmt!=FMT_NONE){if(!*path){set_message(message,message_size,"Output file path is empty");return 0;}char tmp[4096];errno=0;FILE*out=viewbbc_atomic_open(path,tmp,sizeof(tmp));if(!out){snprintf(message,message_size,"Cannot open output file: %s",strerror(errno));return 0;}errno=0;int ok=fmt==FMT_TEXT?write_document_text(document,out):fmt==FMT_PDF?write_pdf(document,out):write_odt(document,out);if(!ok&&errno==0)errno=EIO;if(!ok){int saved=errno;viewbbc_atomic_abort(out,tmp);snprintf(message,message_size,"Write failed: %s",strerror(saved));return 0;}if(!viewbbc_atomic_commit(out,tmp,path)){int e=errno;snprintf(message,message_size,"Write failed replacing output file: %s",strerror(e));return 0;}snprintf(message,message_size,"Written to %s",path);return 1;}
    if(strcmp(target,"file")==0){set_message(message,message_size,"Choose a file type and filename");return 0;}
    if(strncmp(target,"printer:",8u)==0){const char*q=target+8u;
#ifdef _WIN32
        (void)q;set_message(message,message_size,"Printer output is not available on Windows yet");return 0;
#else
        if(strcmp(q,"default")!=0&&!safe_queue_name(q)){set_message(message,message_size,"Invalid printer queue name");return 0;}if(print_with_lp(document,q,message,message_size)){snprintf(message,message_size,"Print job accepted by %s (later device faults are handled by the OS spooler)",q);return 1;}return 0;
#endif
    }
    set_message(message,message_size,"Unknown print target");return 0;
}
