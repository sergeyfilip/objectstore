#include "JSONValue.hh"

std::string JSONValue::nullString = "null";
const std::string JSONValue::trueString = "true";
const std::string JSONValue::falseString = "false";

JSONValue::JSONValue()
  : type(JSON::Null)
  , value_bool(false)
  , value_number(0.0)
{

}

JSONValue::JSONValue(JSON::Type type)
  : type(type)
  , value_bool(false)
  , value_number(0.0)
{

}
  
JSONValue::~JSONValue()
{
  clear();
}

void JSONValue::clear()
{
  value_object.clear();
  value_vector.clear();
  value_bool = false;
  value_number = 0.0;
  value_string.clear();
}

JSON::Type JSONValue::getType() const
{
  return type;
}

void JSONValue::setValue(bool value)
{
  if(JSON::Bool != type)
    throw TypeException();
  
  value_bool = value;
}

void JSONValue::setValue(double value)
{
  if(JSON::Number != type)
    throw TypeException();
    
  value_number = value;
}

void JSONValue::setValue(const std::string &value)
{
  if(JSON::String != type)
    throw TypeException();
    
  value_string = value;
}
  
void JSONValue::addValue(const JSONValue &value)
{
  if(JSON::Array != type)
    throw TypeException();
    
  value_vector.push_back(value);
}
  
void JSONValue::addValue(const std::string &name, const JSONValue &value)
{
  if(JSON::Object != type)
    throw TypeException();
    
  value_object.insert(std::pair<std::string, JSONValue>(name, value));
}
  
bool JSONValue::getBool()
{
  if(JSON::Bool != type)
    throw TypeException();
    
  return value_bool;
}

double JSONValue::getNumber()
{
  if(JSON::Number != type)
    throw TypeException();
    
  return value_number;
}

std::string & JSONValue::getString()
{
  if(JSON::Null == type)
    return nullString;
  
  if(JSON::String != type)
    throw TypeException();
    
  return value_string;
}
  
std::vector<JSONValue>::iterator JSONValue::arrayBegin()
{
  if(JSON::Array != type)
    throw TypeException();
    
  return value_vector.begin();
}

std::vector<JSONValue>::iterator JSONValue::arrayEnd()
{
  if(JSON::Array != type)
    throw TypeException();

  return value_vector.end();
}

JSON::ArrayValue & JSONValue::getArray()
{
  return value_vector;
}

std::map<std::string, JSONValue>::iterator JSONValue::objectBegin()
{
  if(JSON::Object != type)
    throw TypeException();

  return value_object.begin();
}

std::map<std::string, JSONValue>::iterator JSONValue::objectEnd()
{
  if(JSON::Object != type)
    throw TypeException();

  return value_object.end();
}

JSON::ObjectValue & JSONValue::getObject()
{
  return value_object;
}
