#include <sstream>

#include "JSONParser.hh"

JSONParser::JSONParser()
  : root(JSON::Object)
{
}

JSONParser::~JSONParser()
{
  root.clear();
}

JSONToken::TokenType JSONParser::nextToken(const std::string &data, size_t &pos)
{
  size_t len = data.length();
  for(; pos < len; ++pos)
  {
    if(!::isspace(data[pos]))
      break;
  }
  
  char chr = data[pos++];
  switch(chr)
  {
  case '{':
    return JSONToken::Object_Begin;
  case '}':
    return JSONToken::Object_End;
  case '[':
    return JSONToken::Array_Begin;
  case ']':
    return JSONToken::Array_End;
  case '"':
    return JSONToken::String;
  case ':':
    return JSONToken::Delim_Pair;
  case ',':
    return JSONToken::Delim_Element;
  case '-': case '0':
  case '1': case '2': case '3':
  case '4': case '5': case '6':
  case '7': case '8': case '9':
    --pos;
    return JSONToken::Number;
  case 't':
    --pos;
    if("true" == data.substr(pos, 4))
    {
      pos += 4;
      return JSONToken::True;
    }
    return JSONToken::Error;
  case 'f':
    --pos;
    if("false" == data.substr(pos, 5))
    {
      pos += 5;
      return JSONToken::False;
    }
    return JSONToken::Error;
  case 'n':
    --pos;
    if("null" == data.substr(pos, 4))
    {
      pos += 4;
      return JSONToken::Null;
    }
    return JSONToken::Error;
  default:
    break;
  }
  
  return JSONToken::Error;
}

JSONValue & JSONParser::parse(const std::string &data)
{
  size_t begin = 0;
  if(JSONToken::Object_Begin != nextToken(data, begin))
    throw ParseException();
  parseObject(data, begin, root);
  
  return root;
}

void JSONParser::parseObject(const std::string &data, size_t &begin, JSONValue &value)
{
  JSONToken::TokenType next;
  while(1)
  {
    // Key
    next = nextToken(data, begin);
    if(JSONToken::String != next)
      throw ParseException();
    JSONValue key(JSON::String);
    parseString(data, begin, key);
    
    // Delimiter
    next = nextToken(data, begin);
    if(JSONToken::Delim_Pair != next)
      throw ParseException();
      
    // Value
    next = nextToken(data, begin);
    switch(next)
    {
    case JSONToken::Object_Begin:
      {
      JSONValue val(JSON::Object);
      parseObject(data, begin, val);
      value.addValue(key.getString(), val);
      }
      break;
    case JSONToken::Array_Begin:
      {
      JSONValue val(JSON::Array);
      parseArray(data, begin, val);
      value.addValue(key.getString(), val);
      }
      break;
    case JSONToken::String:
      {
      JSONValue val(JSON::String);
      parseString(data, begin, val);
      value.addValue(key.getString(), val);
      }
      break;
    case JSONToken::Number:
      {
      JSONValue val(JSON::Number);
      parseNumber(data, begin, val);
      value.addValue(key.getString(), val);
      }
      break;
    case JSONToken::True:
      {
      JSONValue val(JSON::Bool);
      val.setValue(true);
      value.addValue(key.getString(), val);
      }
      break;
    case JSONToken::False:
      {
      JSONValue val(JSON::Bool);
      val.setValue(false);
      value.addValue(key.getString(), val);
      }
      break;
    case JSONToken::Null:
      {
      JSONValue val;
      value.addValue(key.getString(), val);
      }
      break;
    default:
      throw ParseException();
    }
    
    next = nextToken(data, begin);
    if(JSONToken::Object_End == next)
    {
      break;
    }
    if(JSONToken::Delim_Element == next)
      continue;
    
    throw ParseException();
  }
}

void JSONParser::parseArray(const std::string &data, size_t &begin, JSONValue &value)
{
  JSONToken::TokenType next;
  while(1)
  {
    next = nextToken(data, begin);
    switch(next)
    {
    case JSONToken::Object_Begin:
      {
      JSONValue val(JSON::Object);
      parseObject(data, begin, val);
      value.addValue(val);
      }
      break;
    case JSONToken::Array_Begin:
      {
      JSONValue val(JSON::Array);
      parseArray(data, begin, val);
      value.addValue(val);
      }
      break;
    case JSONToken::String:
      {
      JSONValue val(JSON::String);
      parseString(data, begin, val);
      value.addValue(val);
      }
      break;
    case JSONToken::Number:
      {
      JSONValue val(JSON::Number);
      parseNumber(data, begin, val);
      value.addValue(val);
      }
      break;
    case JSONToken::True:
      {
      JSONValue val(JSON::Bool);
      val.setValue(true);
      value.addValue(val);
      }
      break;
    case JSONToken::False:
      {
      JSONValue val(JSON::Bool);
      val.setValue(false);
      value.addValue(val);
      }
      break;
    case JSONToken::Null:
      {
      JSONValue val;
      value.addValue(val);
      }
      break;
    case JSONToken::Array_End:
      return;
    default:
      throw ParseException();
    }
    
    next = nextToken(data, begin);
    if(JSONToken::Array_End == next)
    {
      break;
    }
    if(JSONToken::Delim_Element == next)
      continue;
    
    throw ParseException();
  }
}

void JSONParser::parseNumber(const std::string &data, size_t &begin, JSONValue &value)
{
  std::string valid = "+-01234567890.eE";
  size_t end = begin;
  while(valid.find(data[end]) != std::string::npos) ++end;
  
  std::stringstream ss(data.substr(begin, end - begin));
  double number;
  ss >> number;
  value.setValue(number);
  
  begin = end;
}

void JSONParser::parseString(const std::string &data, size_t &begin, JSONValue &value)
{
  const size_t len = data.length();
  size_t end = begin;
  
  for(; end < len; ++end)
  {
    if('"' == data[end])
      break;
    if('\\' == data[end])
      ++end;
  }
  
  if(end >= len)
    throw ParseException();
    
  value.setValue(data.substr(begin, end - begin));
  begin = end + 1;
}
