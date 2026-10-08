#pragma once
#include <string>
#include <strings.h>
namespace BotNoteFields {
inline std::string trim(std::string s){while(!s.empty()&&(s.back()==' '||s.back()=='\t'||s.back()=='\r'))s.pop_back();size_t i=0;while(i<s.size()&&(s[i]==' '||s[i]=='\t'))++i;return s.substr(i);}
inline const char* edit(const std::string& previous,const char* text,std::string& result){
 const std::string input=text;const size_t pipe=input.find('|'),newline=input.find('\n');
 if(pipe==std::string::npos||(newline!=std::string::npos&&newline<pipe)){result=input;return nullptr;}
 const std::string field=trim(input.substr(0,pipe)),value=trim(input.substr(pipe+1));
 const char* label=nullptr;
 for(const char* name:{"Enclosure","Antenna","Board","Firmware","RXPS"})if(!strcasecmp(field.c_str(),name)){label=name;break;}
 if(!label)return "Unknown field. Use enclosure, antenna, board, firmware or rxps";
 if(value.find_first_of("\r\n")!=std::string::npos)return "Field value must be one line";
 size_t found=std::string::npos,finish=0;
 for(size_t pos=0;pos<previous.size();){
  size_t end=previous.find('\n',pos);if(end==std::string::npos)end=previous.size();
  const std::string line=previous.substr(pos,end-pos);const size_t colon=line.find(':');
  if(colon!=std::string::npos&&!strcasecmp(trim(line.substr(0,colon)).c_str(),label)){
   if(found!=std::string::npos)return "Duplicate notes fields; edit the full note first";
   found=pos;finish=end;
  }
  pos=end+1;
 }
 const std::string replacement=std::string(label)+":"+(value.empty()?"":" ")+value;
 result=previous;
 if(found!=std::string::npos){const bool cr=finish>found&&previous[finish-1]=='\r';result.replace(found,finish-found,replacement+(cr?"\r":""));}
 else if(!strcasecmp(label,"Enclosure"))result=replacement+(previous.empty()?"":"\n")+previous;
 else {if(!result.empty()&&result.back()!='\n')result+='\n';result+=replacement;}
 return nullptr;
}
}
