/*
   ktigcc - TIGCC IDE for KDE
   
   tpr handling routines adapted from tprbuilder
   Copyright (C) 2002 Romain Liévin
   Copyright (C) 2002-2006 Kevin Kofler
   Copyright (C) 2006 Joey Adams

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software Foundation,
   Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA.
*/

#include <cstdio>
#include <cstdlib>
#include <cerrno>
#include <cstring>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/dir.h>
#include <kapplication.h>
#include <kcmdlineargs.h>
#include <kaboutdata.h>
#include <qstring.h>
#include <qregexp.h>
#include <qtextcodec.h>
#include <glib.h>
#include <ticonv.h>
#include "ktigcc.h"
#include "tpr.h"
#include "preferences.h"

#define find_param(s_,t_) find_param_ex((s_),(t_),sizeof(t_)-1)

static char *find_param_ex(char *s, const char *t, size_t l)
{
  if (!strncmp(s,t,l))
    return s+l;
  else
    return NULL;
}

static char *find_numbered_param(char *s, const char*t, int *i)
{
    char *p;
    int endpos = 0;
    int ret = 0;
    char arglist[256];
    
    strcpy(arglist, t);
    strcat(arglist, "%n");
    
    ret = sscanf(s, arglist, i, &endpos);
    if(ret < 1 || !endpos) return NULL;
    
    p = s + endpos;
    return p;
}

// converts Windows paths to Unix paths if necessary.
static QString convert_path_separators(const char *file)
{
    QString s=file;
    int o;
  
#ifndef __WIN32__
     while ((o=s.find('\\',0,TRUE))>=0)
         s[o]='/';
#endif
    
    return s;
}

static char* strip(char *str)
{
    int len = strlen(str);

    if(len > 0)
        if( (str[len-1] == '\r') || (str[len-1] == '\n') )
            str[len-1] = '\0';
    
    if(len > 1)
        if( (str[len-2] == '\r') || (str[len-2] == '\n') )
            str[len-2] = '\0';

    return str;
}

/* 
   Read a line from file and do a clean-up 
*/
static int read_line(FILE *f, char *buffer, int *l)
{
    strcpy(buffer, "");
    if(feof(f)) return EOF;
    if (!fgets(buffer, 256, f)) return feof(f)?EOF:1;
    strip(buffer);
    (*l)++;
    while( (buffer[0] == '\r') || (buffer[0] == '\n') || (buffer[0] == '\0') )
    {
        if(feof(f)) return EOF;
        if (!fgets(buffer, 256, f)) return feof(f)?EOF:1;
        strip(buffer);
        (*l)++;
    }
    
    return 0;
}

//some items from the TSR might still be ignored or not shown here.
//if you need one of them, add an entry to one of the
//definition "tables".
//If you add an entry here, don't forget to add something to
//  save_tpr!
static int parse_file(FILE *f,TPRDataStruct *dest)
{
  char buffer[256];
  int l = 0;
  SectionType stype = SECTION_NONE;
  
  // Some defaults are different when opening an existing project.
  dest->settings.cc_switches="";
  dest->settings.as_switches="";
  dest->settings.a68k_switches="";
  dest->libopts.use_ti89=0;
  dest->libopts.use_ti92p=0;
  dest->libopts.use_v200=0;
  dest->libopts.use_minams=0;
  dest->libopts.save_screen=0;

  while(!feof(f)) 
    {
        char *p;
          
        // Get a line from file
        int result=read_line(f, buffer, &l);
        if (result == 1) return -1; // error
        if (result == EOF) break;

        // Search for sections
        if( !strcmp(buffer, "[Settings]") )
        {
            stype = SECTION_SETTINGS;
            continue;
        }
        
        if( !strcmp(buffer, "[Library Options]") )
        {
            stype = SECTION_LIBOPTS;
            continue;
        }
        
        if( !strcmp(buffer, "[File Editing]") )
        {
            stype = SECTION_FILEEDIT;
            continue;
        }
        
        if( !strcmp(buffer, "[Included Files]") )
        {
            stype = SECTION_FILES;
            continue;
        }
        
        // Keywords in the [Settings] section
        if(stype == SECTION_SETTINGS) {

#define boolean_param(token,setting) \
            if ( (p=find_param(buffer, token)) ) \
            { \
                if(!strcmp(p, "0")) dest->settings.setting = FALSE; \
                else if(!strcmp(p, "1")) dest->settings.setting = TRUE; \
                else return l; \
                continue; \
            } else

#define tistring_vparam(token,var) \
            if ( (p=find_param(buffer, token)) ) \
            { \
                if (*p) { \
                  unsigned short *utf16=ticonv_charset_ti_to_utf16(CALC_TI89,p); \
                  dest->var = QString::fromUcs2(utf16); \
                  g_free(utf16); \
                } \
                continue; \
            } else

#define string_param(token,setting) \
            if ( (p=find_param(buffer, token)) ) \
            { \
                if (*p) dest->settings.setting = p; \
                continue; \
            } else

#define ignore_param(token) \
            if( (p=find_param(buffer, token)) ) \
            { \
                continue; \
            } else

            boolean_param("Archive=",archive)
            boolean_param("Pack=",pack)
            string_param("Packed Variable=",pack_name)
            tistring_vparam("Project Name=",prj_name)
            string_param("GCC Switches=",cc_switches)
            string_param("Assembler Switches=",a68k_switches)
            ignore_param("Linker Switches=") // Obsolete. Ignore.
            ignore_param("GNU Linker Switches=") // Obsolete. Ignore.
            string_param("GNU Assembler Switches=",as_switches)
            ignore_param("BSR Patch=") // Obsolete. Ignore.
            boolean_param("Debug Info=",debug_info)
            boolean_param("Standard Library=",std_lib)
            string_param("Command Line=",cmd_line)
            string_param("Post-Build Process=",post_build)
            boolean_param("Use Data Variable=",use_data_var)
            string_param("Data Variable=",data_var)
            boolean_param("Copy Data Variable=",copy_data_var)
            boolean_param("Copy Data Variable if Archived=",copy_data_var_arc)
            boolean_param("Optimize NOPs=",optimize_nops)
            boolean_param("Optimize Returns=",optimize_returns)
            boolean_param("Optimize Branches=",optimize_branches)
            boolean_param("Optimize Moves=",optimize_moves)
            boolean_param("Optimize Tests=",optimize_tests)
            boolean_param("Optimize Calculations=",optimize_calcs)
            boolean_param("Remove Unused Sections=",remove_unused)
            boolean_param("Binary Output=",outputbin)
            boolean_param("Fargo=",fargo)
            boolean_param("Flash OS=",flash_os)
            boolean_param("Cut Unused Ranges=",cut_ranges)
            boolean_param("Reorder Sections=",reorder_sections)
            boolean_param("Merge Constants=",merge_constants)
            boolean_param("Initialize BSS=",initialize_bss)
            return l;

#undef boolean_param
#undef tistring_vparam
#undef string_param
#undef ignore_param
        }
        
        // Keywords in the [Library Options] section
        if(stype == SECTION_LIBOPTS) {
#define boolean_param(token,setting) \
            if ( (p=find_param(buffer, token)) ) \
            { \
                if(!strcmp(p, "0")) dest->libopts.setting = FALSE; \
                else if(!strcmp(p, "1")) dest->libopts.setting = TRUE; \
                else return l; \
                continue; \
            } else

#define reloc_param(token,setting) \
            if ( (p=find_param(buffer, token)) ) \
            { \
                if (!strcmp(p,"None")) \
                    dest->libopts.setting = RT_NONE; \
                else if (!strcmp(p,"Direct")) \
                    dest->libopts.setting = RT_DIRECT; \
                else if (!strcmp(p,"AMS")) \
                    dest->libopts.setting = RT_AMS; \
                else if (!strcmp(p, "Precomputed")) \
                    dest->libopts.setting = RT_PRECOMP; \
                else if (!strcmp(p, "Kernel")) \
                    dest->libopts.setting = RT_KERNEL; \
                else if (!strcmp(p, "Compressed")) \
                    dest->libopts.setting = RT_COMPRESSED; \
                else if (!strcmp(p, "MLink")) \
                    dest->libopts.setting = RT_MLINK; \
                else if (!strcmp(p, "F-Line")) \
                    dest->libopts.setting = RT_FLINE; \
                else if (strcmp(p, "Unknown")) \
                    return l; \
                continue; \
            } else
            
//this new macro was added to make merging with save_tpr more consistent.
#define minams_param(token,setting) \
            if ( (p=find_param(buffer, "Minimum AMS Version=")) ) \
            { \
                int major, minor; \
                if ((strlen(p)==4) \
                    && (sscanf(p,"%1d.%2d",&major,&minor)==2)) \
                    dest->libopts.minams = major * 100 + minor; \
                else \
                    return l; \
                continue; \
            } else

            boolean_param("Use TI-89=",use_ti89)
            boolean_param("Use TI-92 Plus=",use_ti92p)
            boolean_param("Use V200=",use_v200)
            boolean_param("Optimize Calc Consts=",opt_calc_consts)
            boolean_param("Use Kernel=",use_kernel)
            boolean_param("Use PreOS=",use_preos)
            boolean_param("Minimum AMS Version Defined=",use_minams)
            minams_param("Minimum AMS Version=",minams);
            boolean_param("Unofficial OS Support=",unofficial_os)
            reloc_param("Reloc Format=",reloc_format)
            reloc_param("ROM Call Format=",rom_call_format)
            reloc_param("BSS Ref Format=",bss_ref_format)
            reloc_param("Data Ref Format=",data_ref_format)
            boolean_param("Use F-Line Jumps=",use_fline_jumps)
            boolean_param("Use 4-Byte F-Line Jumps=",use_4b_fline_jumps)
            boolean_param("Use Internal F-Line Emulator=",use_internal_fline_emu)
            boolean_param("Use Return Value=",use_return_value)
            boolean_param("Enable Error Return=",enable_error_return)
            boolean_param("Save Screen=",save_screen)
            boolean_param("Optimize ROM Calls=",opt_rom_calls)
            return l;

#undef boolean_param
#undef reloc_param
#undef minams_param
        }

        // Keywords in the [File Editing] section
        if(stype == SECTION_FILEEDIT)
        {
            if ( (p=find_param(buffer, "Open File=")) ) \
            { \
                if (*p) dest->open_file = p; \
                continue; \
            } else return l;
        }

        // Keywords in the [Included Files] section
        if(stype == SECTION_FILES)
        {
            int v;

            if( (p=find_numbered_param(buffer, "C File %i=", &v)) )
            {
                QString s = convert_path_separators(p);
                dest->c_files.path << s;
                dest->c_files.folder << QString::null;

                continue;
            }
            else if( (p=find_numbered_param(buffer, "C File %i Folder=", &v)) )
            {
                dest->c_files.folder[v-1]=p;
                continue;
            }

            else if( (p=find_numbered_param(buffer, "GNU Assembler File %i=", &v)) )
            {
                QString s = convert_path_separators(p);
                dest->s_files.path << s;
                dest->s_files.folder << QString::null;
              
                continue;
            }
            else if( (p=find_numbered_param(buffer, "GNU Assembler File %i Folder=", &v)) )
            {
                dest->s_files.folder[v-1]=p;
                continue;
            }

            else if( (p=find_numbered_param(buffer, "Header File %i=", &v)) )
            {
                QString s = convert_path_separators(p);
                dest->h_files.path << s;
                dest->h_files.folder << QString::null;
                
                continue;
            }
            else if( (p=find_numbered_param(buffer, "Header File %i Folder=", &v)) )
            {
                dest->h_files.folder[v-1]=p;
                continue;
            }

            else if( (p=find_numbered_param(buffer, "Assembler File %i=", &v)) )
            {
                QString s = convert_path_separators(p);
                dest->asm_files.path << s;
                dest->asm_files.folder << QString::null;
              
                continue;
            }
            else if( (p=find_numbered_param(buffer, "Assembler File %i Folder=", &v)) )
            {
                dest->asm_files.folder[v-1]=p;
                continue;
            }

            else if( (p=find_numbered_param(buffer, "Object File %i=", &v)) )
            {
                QString s = convert_path_separators(p);
                dest->o_files.path << s;
                dest->o_files.folder << QString::null;
              
                continue;
            }
            else if( (p=find_numbered_param(buffer, "Object File %i Folder=", &v)) )
            {
                dest->o_files.folder[v-1]=p;
                continue;
            }

            else if( (p=find_numbered_param(buffer, "Archive File %i=", &v)) )
            {
                QString s = convert_path_separators(p);
                dest->a_files.path << s;
                dest->a_files.folder << QString::null;
              
                continue;
            }
            else if( (p=find_numbered_param(buffer, "Archive File %i Folder=", &v)) )
            {
                dest->a_files.folder[v-1]=p;
                continue;
            }

            else if( (p=find_numbered_param(buffer, "Text File %i=", &v)) )
            {
                QString s = convert_path_separators(p);
                dest->txt_files.path << s;
                dest->txt_files.folder << QString::null;
              
                continue;
            }
            else if( (p=find_numbered_param(buffer, "Text File %i Folder=", &v)) )
            {
                dest->txt_files.folder[v-1]=p;
                continue;
            }

            else if( (p=find_numbered_param(buffer, "Quill File %i=", &v)) )
            {
                QString s = convert_path_separators(p);
                dest->quill_files.path << s;
                dest->quill_files.folder << QString::null;

                continue;
            }
            else if( (p=find_numbered_param(buffer, "Quill File %i Folder=", &v)) )
            {
                dest->quill_files.folder[v-1]=p;
                continue;
            }

            else if( (p=find_numbered_param(buffer, "Other File %i=", &v)) )
            {
                QString s = convert_path_separators(p);
                dest->oth_files.path << s;
                dest->oth_files.folder << QString::null;
                
                continue;
            }
            else if( (p=find_numbered_param(buffer, "Other File %i Folder=", &v)) )
            {
                dest->oth_files.folder[v-1]=p;
                continue;
            }

            else return l;
        }

    }

    return 0;
}

// returns 0 on success, -1 if it can't open the TPR and a (positive) line
// number if there is an error in the TPR
int loadTPR(const QString &fileName,TPRDataStruct *dest)
{
  FILE *f;
  int ret;
  f = fopen(fileName, "r");
  if(f == NULL) {
      return -1;
  }
  ret=parse_file(f,dest);
  fclose(f);
  return ret;
}

QString loadFileText(const char *fileName)
{
  FILE *f;
  f=fopen(fileName,"rb");
  if (!f)
    return QString::null;
  fseek(f,0,SEEK_END);
  size_t flen=ftell(f);
  fseek(f,0,SEEK_SET);
  char *buffer = new(std::nothrow) char[flen+1]();
  if (!buffer)
    return QString::null;
  QString ret;
  if (fread(buffer,1,flen,f)<flen) {
    delete[] buffer;
    ret=QString::null;
  } else {
    if (preferences.useCalcCharset) {
      unsigned short *utf16=ticonv_charset_ti_to_utf16(CALC_TI89,buffer);
      delete[] buffer;
      if (!utf16)
        return QString::null;
      ret=QString::fromUcs2(utf16);
      g_free(utf16);
    } else {
      ret=buffer;
      delete[] buffer;
    }
  }
  if (!ret.isNull()) {
    // convert Windows line endings
    ret=ret.replace("\r\n","\n");
    // interpret remaining \r characters as Mac line endings
    ret=ret.replace('\r','\n');
    // remove trailing spaces if requested
    if (preferences.removeTrailingSpaces) {
      ret=ret.replace(QRegExp("((?!\n)\\s)*\n"),"\n");
      ret=ret.remove(QRegExp("((?!\n)\\s)*$"));
    }
  }
  return ret;
}


static QString convert_path_separators_save(QString s)
{
    int o;
    
#ifndef __WIN32__
    while ((o=s.find('/',0,TRUE))>=0)
        s[o]='\\';
#endif
    
    return s;
}

//this is here because QString::ascii() returns "(null)" on a null string.
const char *smartAscii(const QString &s)
{
  if (s.isNull())
    return "";
  return s.ascii();
}

static int save_tpr(FILE *f,TPRDataStruct *dest)
{
    unsigned i=0,e;
    QString tmp;
    
#define boolean_param(token,setting) if (fprintf(f,token "%d\r\n",!!dest->settings.setting)<0) return -2;
#define tistring_vparam(token,var) { \
        char *ti=ticonv_charset_utf16_to_ti(CALC_TI89,dest->var.ucs2()); \
        if (fprintf(f,token "%s\r\n",ti)<0) {g_free(ti); return -2;} \
        g_free(ti); \
    }
#define string_param(token,setting) if (fprintf(f,token "%s\r\n",smartAscii(dest->settings.setting))<0) return -2;
#define ignore_param(token) /**/
    
    if (fputs("[Settings]\r\n",f)<0) return -2;
    boolean_param("Archive=",archive)
    boolean_param("Pack=",pack)
    string_param("Packed Variable=",pack_name)
    tistring_vparam("Project Name=",prj_name)
    string_param("GCC Switches=",cc_switches)
    string_param("Assembler Switches=",a68k_switches)
    ignore_param("Linker Switches=") // Obsolete. Ignore.
    ignore_param("GNU Linker Switches=") // Obsolete. Ignore.
    string_param("GNU Assembler Switches=",as_switches)
    ignore_param("BSR Patch=") // Obsolete. Ignore.
    boolean_param("Debug Info=",debug_info)
    boolean_param("Standard Library=",std_lib)
    string_param("Command Line=",cmd_line)
    string_param("Post-Build Process=",post_build)
    boolean_param("Use Data Variable=",use_data_var)
    string_param("Data Variable=",data_var)
    boolean_param("Copy Data Variable=",copy_data_var)
    boolean_param("Copy Data Variable if Archived=",copy_data_var_arc)
    boolean_param("Optimize NOPs=",optimize_nops)
    boolean_param("Optimize Returns=",optimize_returns)
    boolean_param("Optimize Branches=",optimize_branches)
    boolean_param("Optimize Moves=",optimize_moves)
    boolean_param("Optimize Tests=",optimize_tests)
    boolean_param("Optimize Calculations=",optimize_calcs)
    boolean_param("Remove Unused Sections=",remove_unused)
    boolean_param("Binary Output=",outputbin)
    boolean_param("Fargo=",fargo)
    boolean_param("Flash OS=",flash_os)
    boolean_param("Cut Unused Ranges=",cut_ranges)
    boolean_param("Reorder Sections=",reorder_sections)
    boolean_param("Merge Constants=",merge_constants)
    boolean_param("Initialize BSS=",initialize_bss)
    
#undef boolean_param
#undef tistring_vparam
#undef string_param
#undef ignore_param
    
#define boolean_param(token,setting) if (fprintf(f,token "%d\r\n",!!dest->libopts.setting)<0) return -2;
    
#define reloc_param(token,setting) \
    switch(dest->libopts.setting) \
    { \
    case RT_NONE: \
        if (fprintf(f,token "None\r\n")<0) return -2; \
        break; \
    case RT_DIRECT: \
        if (fprintf(f,token "Direct\r\n")<0) return -2; \
        break; \
    case RT_AMS: \
        if (fprintf(f,token "AMS\r\n")<0) return -2; \
        break; \
    case RT_PRECOMP: \
        if (fprintf(f,token "Precomputed\r\n")<0) return -2; \
        break; \
    case RT_KERNEL: \
        if (fprintf(f,token "Kernel\r\n")<0) return -2; \
        break; \
    case RT_COMPRESSED: \
        if (fprintf(f,token "Compressed\r\n")<0) return -2; \
        break; \
    case RT_MLINK: \
        if (fprintf(f,token "MLink\r\n")<0) return -2; \
        break; \
    case RT_FLINE: \
        if (fprintf(f,token "F-Line\r\n")<0) return -2; \
        break; \
    }
    
#define minams_param(token,setting) \
    { \
        int major,minor; \
        major=dest->libopts.setting/100; \
        minor=dest->libopts.setting%100; \
        if (fprintf(f,token "%d.%02d\r\n",major,minor)<0) return -2; \
    }
    
    if (fputs("\r\n[Library Options]\r\n",f)<0) return -2;
    boolean_param("Use TI-89=",use_ti89)
    boolean_param("Use TI-92 Plus=",use_ti92p)
    boolean_param("Use V200=",use_v200)
    boolean_param("Optimize Calc Consts=",opt_calc_consts)
    boolean_param("Use Kernel=",use_kernel)
    boolean_param("Use PreOS=",use_preos)
    boolean_param("Minimum AMS Version Defined=",use_minams)
    minams_param("Minimum AMS Version=",minams);
    boolean_param("Unofficial OS Support=",unofficial_os)
    reloc_param("Reloc Format=",reloc_format)
    reloc_param("ROM Call Format=",rom_call_format)
    reloc_param("BSS Ref Format=",bss_ref_format)
    reloc_param("Data Ref Format=",data_ref_format)
    boolean_param("Use F-Line Jumps=",use_fline_jumps)
    boolean_param("Use 4-Byte F-Line Jumps=",use_4b_fline_jumps)
    boolean_param("Use Internal F-Line Emulator=",use_internal_fline_emu)
    boolean_param("Use Return Value=",use_return_value)
    boolean_param("Enable Error Return=",enable_error_return)
    boolean_param("Save Screen=",save_screen)
    boolean_param("Optimize ROM Calls=",opt_rom_calls)
    
#undef boolean_param
#undef reloc_param
#undef minams_param
    
    if (fprintf(f,"\r\n[File Editing]\r\nOpen File=%s\r\n\r\n[Included Files]\r\n",smartAscii(dest->open_file))<0) return -2;
    
#define filepath_param(token,filetype) \
    e=dest->filetype.path.count(); \
    for(i=0;i<e;i++) \
    { \
        tmp=convert_path_separators_save(smartAscii(dest->filetype.path[i])); \
        if (fprintf(f,token " %u=%s\r\n",i+1,smartAscii(tmp))<0) return -2; \
        if (!dest->filetype.folder[i].isEmpty()) \
        { \
          if (fprintf(f,token " %u Folder=%s\r\n",i+1,smartAscii(dest->filetype.folder[i]))<0) return -2; \
        } \
    }
    
    filepath_param("C File",c_files)
    filepath_param("GNU Assembler File",s_files)
    filepath_param("Header File",h_files)
    filepath_param("Assembler File",asm_files)
    filepath_param("Object File",o_files)
    filepath_param("Archive File",a_files)
    filepath_param("Text File",txt_files)
    filepath_param("Quill File",quill_files)
    filepath_param("Other File",oth_files)
    
#undef filepath_param
    
    if (fputs("\r\n",f)<0) return -2;
    
    return 0;
}

void newSettings(tprSettings *settings,tprLibOpts *libopts)
{
  tprSettings newSettings;
  tprLibOpts newLibOpts;
  *settings=newSettings;
  *libopts=newLibOpts;
}

//returns 0 on success, -1 if the file couldn't be created or -2 if fprintf,
//fputs or fclose failed
int saveTPR(const QString &fileName,TPRDataStruct *src)
{
  FILE *f;
  int ret;
  f=fopen(fileName,"wb");
  if (!f)
  {
    return -1;
  }
  ret=save_tpr(f,src);
  if (fclose(f)) return -2;
  return ret;
}

static void mkdir_multi(const char *fileName)
{
  int l=strlen(fileName);
  char buffer[l+2];
  char *ptr;

#ifdef __WIN32__
  ptr=strchr(fileName,'\\');
#else
  ptr=strchr(fileName,'/');
#endif
  
  while (ptr)
  {
    memcpy(buffer,fileName,ptr-fileName);
    buffer[ptr-fileName]=0;
    mkdir(buffer,S_IRWXU | S_IRWXG | S_IRWXO);
    
#ifdef __WIN32__
    ptr=strchr(ptr+1,'\\');
#else
    ptr=strchr(ptr+1,'/');
#endif
  }
}

static int writeToFile(FILE *f, const QString &text)
{
  if (preferences.useCalcCharset) {
    const unsigned short *utf16=text.ucs2();
    if (utf16) {
      char *s=ticonv_charset_utf16_to_ti(CALC_TI89,utf16);
      size_t l=std::strlen(s);
      if (fwrite(s,1,l,f)<l) {
        g_free(s);
        return -2;
      }
      g_free(s);
    }
  } else {
    const char *s=smartAscii(text);
    size_t l=text.length();
    if (fwrite(s,1,l,f)<l) return -2;
  }
  return 0;
}

enum CharModes {cmNone, cmNormalText, cmNumber, cmMultiSymbol, cmString, cmChar,
                cmComment, cmUnchangeableLine, cmExtUnchangeableLine,
                cmExtUnchangeableLineString, cmTrigraph};

int saveAndSplitFileText(const char *fileName, const QString &fileText,
                         bool split, bool addCLineDirective,
                         bool addASMLineDirective, const QString &origFileName,
                         LineStartList *pLineStartList)
{
  FILE *f;
  LineStartList lineStartList;
  // remove trailing spaces if requested
  QString text=fileText;
  if (preferences.removeTrailingSpaces && !text.isNull()) {
    text=text.replace(QRegExp("((?!\n)\\s)*\n"),"\n");
    text=text.remove(QRegExp("((?!\n)\\s)*$"));
  }
  
  f=fopen(fileName,"wb");
  if (!f)
  {
    mkdir_multi(fileName);
    f=fopen(fileName,"wb");
    if (!f)
        return -1;
  }
  if (split && preferences.splitSourceFiles && !settings.debug_info) {
    unsigned curPos=0, curLine=0, curCol=0, l=text.length();
    bool atLineStart;
    CharModes curMode=cmNone;

    #define INSERT_CHAR(ch) do {if (writeToFile(f,QString((ch)))) {fclose(f); return -2;}} while(0)
    #define INSERT_STRING(str) do {if (writeToFile(f,(str))) {fclose(f); return -2;}} while(0)
    #define ADD_LINE() do {lineStartList.append(qMakePair(curLine,curCol)); atLineStart=TRUE;} while(0)
    #define IS_NEWLINE() (text[curPos]=='\n')
    #define ADD_LINE_NEXT() do {lineStartList.append(qMakePair(curLine+(unsigned)(IS_NEWLINE()),(IS_NEWLINE())?0u:(curCol+1u))); atLineStart=TRUE;} while(0)
    #define NEW_LINE() do {if((!(atLineStart||IS_NEWLINE())) || (text[curPos]=='#')) {INSERT_CHAR(QChar('\n')); ADD_LINE();} curMode=cmNone;} while(0)
    #define SET_MULTI_CHAR_MODE(mode) do {if (curMode!=(mode)) {NEW_LINE(); curMode=(mode);}} while(0)

    ADD_LINE(); // Line 0
    ADD_LINE(); // Line 1
    for (; curPos<l; curPos++) {
      bool noInsert=FALSE;
      QChar c=text[curPos];
      if (c=='\n')
        ADD_LINE_NEXT();
      switch (curMode) {
        case cmString:
          if (c=='\"') {
            bool b=TRUE;
            unsigned i=curPos-1;
            while (i && ((text[i]=='\\')||(i>=2&&!text.mid(i-2,3).compare("?""?""/")))) {
              b=!b;
              if (text[i]=='\\') --i; else i-=3;
            }
            if (b) {
              curMode=cmNone;
              noInsert=TRUE;
              INSERT_STRING(QString(c)+"\n");
              atLineStart=TRUE;
              ADD_LINE_NEXT();
            }
          }
          break;
        case cmChar:
          if (c=='\'') {
            bool b=TRUE;
            unsigned i=curPos-1;
            while (i && ((text[i]=='\\')||(i>=2&&!text.mid(i-2,3).compare("?""?""/")))) {
              b=!b;
              if (text[i]=='\\') --i; else i-=3;
            }
            if (b) curMode=cmNone;
          }
          break;
        case cmComment:
          if ((c=='/')&&(text[curPos-1]=='*')) curMode=cmNone;
          break;
        case cmUnchangeableLine:
          if (c=='\n') curMode=cmNone;
          break;
        case cmExtUnchangeableLine:
          if ((c=='\n')&&(text[curPos-1]!='\\')) curMode=cmNone;
          else if (c=='\"') curMode=cmExtUnchangeableLineString;
          break;
        case cmExtUnchangeableLineString:
          if (c=='\"') {
            bool b=TRUE;
            unsigned i=curPos-1;
            while (i && ((text[i]=='\\')||(i>=2&&!text.mid(i-2,3).compare("?""?""/")))) {
              b=!b;
              if (text[i]=='\\') --i; else i-=3;
            }
            if (b) curMode=cmExtUnchangeableLine;
          }
          break;
        case cmTrigraph:
          if ((c!='?')&&((curPos+1>=l)||(text[curPos+1]!='?'))) curMode=cmNone;
          break;
        default:
          if (!text.mid(curPos,2).compare("//"))
            SET_MULTI_CHAR_MODE(cmUnchangeableLine);
          else if (!text.mid(curPos,2).compare("/*"))
            SET_MULTI_CHAR_MODE(cmComment);
          else if (!text.mid(curPos,2).compare("?""?""="))
            SET_MULTI_CHAR_MODE(cmExtUnchangeableLine);
          else if (!text.mid(curPos,2).compare("?""?")&&(curPos+2<l)
                   &&QString("()/\'<>!-").contains(text[curPos+2]))
            SET_MULTI_CHAR_MODE(cmTrigraph);
          else {
            switch(c.unicode()) {
              case ' ':
              case '\t':
                if (curPos && text[curPos-1]!=' ' && text[curPos-1]!='\t')
                  NEW_LINE();
                break;
              case 'A' ... 'Z':
              case 'a' ... 'z':
              case '0' ... '9':
              case '_':
              case '$':
                if (curMode!=cmNormalText && curMode!=cmNumber) {
                  NEW_LINE();
                  if (c>=QChar('0') && c<=QChar('9'))
                    curMode=cmNumber;
                  else
                    curMode=cmNormalText;
                }
                break;
              case '\"':
                SET_MULTI_CHAR_MODE(cmString);
                break;
              case '\'':
                SET_MULTI_CHAR_MODE(cmChar);
                break;
              case '#':
                SET_MULTI_CHAR_MODE(cmExtUnchangeableLine);
                break;
              case '.':
                if (curMode!=cmNumber) {
                  if ((curPos+1<l)
                      &&(text[curPos+1]>=QChar('0')
                         &&text[curPos+1]<=QChar('9'))) {
                    NEW_LINE();
                    curMode=cmNumber;
                  } else SET_MULTI_CHAR_MODE(cmMultiSymbol);
                }
                break;
              case '+':
              case '-':
                if ((curMode!=cmNumber)||(curPos<=1)
                    ||!QString("eEpP").contains(text[curPos-1]))
                  SET_MULTI_CHAR_MODE(cmMultiSymbol);
                break;
              default:
                if (QString(",;()[]{}").contains(c)) {
                  if (curMode!=cmNone) {
                    NEW_LINE();
                    curMode=cmNone;
                  }
                  if ((curPos+1<l)&&text[curPos+1]!='\n') {
                    noInsert=TRUE;
                    INSERT_STRING(QString(c)+"\n");
                    ADD_LINE_NEXT();
                  } else SET_MULTI_CHAR_MODE(cmMultiSymbol);
                }
                break;
            }
          }
          break;
      }
      if (!noInsert) {
        INSERT_CHAR(c);
        if (c!='\n') atLineStart=FALSE;
      }
      if (c=='\n') {
        curLine++;
        curCol=0;
      } else curCol++;
    }
    NEW_LINE();
    
    #undef INSERT_CHAR
    #undef INSERT_STRING
    #undef ADD_LINE
    #undef IS_NEWLINE
    #undef ADD_LINE_NEXT
    #undef NEW_LINE
    #undef SET_MULTI_CHAR_MODE
  } else {
    if ((addCLineDirective || addASMLineDirective) && settings.debug_info) {
      QString escapedFileName=origFileName, lineDirective;
      escapedFileName.replace("\\","\\\\");
      escapedFileName.replace("\"","\\\"");
      // These have no business to be in a file name, but someone somewhere may
      // have that very bad idea...
      escapedFileName.replace("\r","\\r");
      escapedFileName.replace("\n","\\n");
      lineDirective=QString(addCLineDirective
                            ?"#line 1 \"%1\"\n"
                            :".appfile \"%1\"; .appline 1\n").arg(escapedFileName);
      // Don't use calc charset for this, it's a host file name.
      const char *s=smartAscii(escapedFileName);
      size_t l=escapedFileName.length();
      if (fwrite(s,1,l,f)<l) return -2;
    }
    if (writeToFile(f,text)) {fclose(f); return -2;}
    if (addCLineDirective) {
      if (fwrite("\n",1,1,f)<1) {fclose(f); return -2;}
    }
  }
  if (fclose(f)) return -2;
  if (pLineStartList) *pLineStartList=lineStartList;
  return 0;
}

int saveFileText(const char *fileName,const QString &fileText)
{
  return saveAndSplitFileText(fileName,fileText,FALSE,FALSE,FALSE,QString::null,
                              static_cast<LineStartList *>(NULL));
}

void kurlNewFileName(KURL &dir,const QString &newFileName)
{
  if (newFileName[0]=='/')
    dir.setPath(newFileName);
  else
    dir.setFileName(newFileName);
}

static QString pullOutFileSuffix(const QString &srcFileName,QString &destFileName)
{
  int a,b;
  QString ret;
  destFileName=srcFileName;
  a=destFileName.findRev('.');
  b=destFileName.findRev('/');
  if (a<0)
    return QString::null;
  if (a<b)
    return QString::null;
  ret=destFileName.mid(a+1);
  destFileName.truncate(a);
  return ret;
}

int checkFileName(const QString &fileName,const QStringList &fileNameList)
{
  int i;
  QString fileName_name,fileName_suffix;
  QString name,suffix;
  fileName_suffix=pullOutFileSuffix(fileName,fileName_name);
  for (i=fileNameList.count()-1;i>=0;i--)
  {
    suffix=pullOutFileSuffix(fileNameList[i],name);
    if (!suffix.compare("c")||!suffix.compare("s")||!suffix.compare("asm")||!suffix.compare("o")||!suffix.compare("qll"))
    {
      if (!fileName_suffix.compare("c")||!fileName_suffix.compare("s")||!fileName_suffix.compare("asm")||!fileName_suffix.compare("o")||!fileName_suffix.compare("qll"))
      {
        if (!name.compare(fileName_name))
          return 0;
      }
    }
    else
    {
      if (!fileNameList[i].compare(fileName))
        return 0;
    }
  }
  return 1;
}

// returns 0 on success, >0 on read failure, <0 on write failure
int copyFile(const char *src, const char *dest)
{
  // This doesn't load everything at once onto the stack because it may be
  // used for huge binary files, which don't fit on the stack. So we copy 1KB
  // at a time.
  FILE *sf=fopen(src,"rb");
  if (!sf) return 1;
  FILE *df=fopen(dest,"wb");
  if (!df) {fclose(sf); return -1;}
  char buffer[1024];
  while (!ferror(sf) && !feof(sf)) {
    size_t bytes_read=fread(buffer,1,1024,sf);
    if (fwrite(buffer,1,bytes_read,df)<bytes_read) {
      fclose(df);
      fclose(sf);
      return -2;
    }
  }
  if (ferror(sf)) {
    fclose(df);
    fclose(sf);
    return 2;
  }
  if (fclose(df)) {fclose(sf); return -3;}
  if (fclose(sf)) return 3;
  return 0;
}

int getPathType(const QString &thePath)
{
  struct stat statvar;
  int result=stat(thePath,&statvar);
  if (result)
    return errno==ENOENT?PATH_NOTFOUND:PATH_ERROR;
  if (statvar.st_mode&S_IFDIR)
    return PATH_FOLDER;
  if (statvar.st_mode&S_IFREG)
    return PATH_FILE;
  return PATH_ERROR;
}
