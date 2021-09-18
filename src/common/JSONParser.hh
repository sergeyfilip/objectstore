#ifndef JSONPARSER_H
#define JSONPARSER_H

#include "JSONValue.hh"

class ParseException : public std::runtime_error
{
  public:
    ParseException()
     : std::runtime_error("Parsing error") {}
    virtual ~ParseException() throw() {}
};

namespace JSONToken
{
  enum TokenType
  {
    Object_Begin,
    Object_End,
    Array_Begin,
    Array_End,
    String,
    Number,
    Delim_Pair,
    Delim_Element,
    True,
    False,
    Null,
    Error
  };
}

class JSONParser
{
public:
  JSONParser();
  ~JSONParser();

  JSONValue & parse(const std::string &data);
  
private:
  JSONToken::TokenType nextToken(const std::string &data, size_t &pos);

  void parseObject(const std::string &data, size_t &begin, JSONValue &value);
  void parseArray(const std::string &data, size_t &begin, JSONValue &value);
  void parseString(const std::string &data, size_t &begin, JSONValue &value);
  void parseNumber(const std::string &data, size_t &begin, JSONValue &value);

  JSONValue root;
};

#endif // JSONPARSER_H
