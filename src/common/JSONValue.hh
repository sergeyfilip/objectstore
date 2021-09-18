#ifndef JSONVALUE_H
#define JSONVALUE_H

#include <stdexcept>
#include <string>
#include <vector>
#include <map>

class JSONValue;

namespace JSON
{
  enum Type { Object, Array, String, Number, Bool, Null };
  
  typedef std::map<std::string, JSONValue>::iterator ObjectIterator;
  typedef std::map<std::string, JSONValue> ObjectValue;
  typedef std::vector<JSONValue>::iterator ArrayIterator;
  typedef std::vector<JSONValue> ArrayValue;
}

class TypeException : public std::runtime_error
{
  public:
    TypeException()
     : std::runtime_error("Type mismatch") {}
    virtual ~TypeException() throw() {}
};

class JSONValue
{
public:
  // Construct Null value
  JSONValue();
  JSONValue(JSON::Type type);
  
  ~JSONValue();
  
  void clear();
  
  JSON::Type getType() const;

  // Set for simple values
  void setValue(bool value);
  void setValue(double value);
  void setValue(const std::string &value);
  
  // Set for array
  void addValue(const JSONValue &value);
  
  // Set for object
  void addValue(const std::string &name, const JSONValue &value);
  
  // Get for simple values
  bool getBool();
  double getNumber();
  std::string & getString();
  
  // Get for array
  JSON::ArrayIterator arrayBegin();
  JSON::ArrayIterator arrayEnd();
  JSON::ArrayValue & getArray();

  // Get for object
  JSON::ObjectIterator objectBegin();
  JSON::ObjectIterator objectEnd();
  JSON::ObjectValue & getObject();

private:
  JSON::Type type;
  std::vector<JSONValue> value_vector;
  std::map<std::string, JSONValue> value_object;
  std::string value_string;
  bool value_bool;
  double value_number;
  
  static std::string nullString;
  static const std::string trueString;
  static const std::string falseString;
};

#endif // JSONVALUE_H
