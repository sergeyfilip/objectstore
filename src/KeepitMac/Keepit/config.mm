//
/// Configuration loader
//
// $Id: config.cc,v 1.4 2013/03/20 15:14:22 joe Exp $
//

#include "config.hh"
#import "SettingsManager.h"

Config::Config()
{
  read();
}

Config &Config::read()
{
  SettingsManager* settingsManager = [SettingsManager sharedInstance];
  m_apihost = [settingsManager.apiHost cStringUsingEncoding:NSUTF8StringEncoding];
  m_email = settingsManager.email
              ?std::string([settingsManager.email cStringUsingEncoding:NSUTF8StringEncoding])
              :Optional<std::string>();
  m_token = settingsManager.token
              ?std::string([settingsManager.token cStringUsingEncoding:NSUTF8StringEncoding])
              :Optional<std::string>();
  m_password = settingsManager.password
              ?std::string([settingsManager.password cStringUsingEncoding:NSUTF8StringEncoding])
              :Optional<std::string>();
  m_device = settingsManager.device
              ?std::string([settingsManager.device cStringUsingEncoding:NSUTF8StringEncoding])
              :Optional<std::string>();
  m_deviceTokenL = settingsManager.deviceTokenL
              ?std::string([settingsManager.deviceTokenL cStringUsingEncoding:NSUTF8StringEncoding])
              :Optional<std::string>();
  m_deviceTokenP = settingsManager.deviceTokenP
              ?std::string([settingsManager.deviceTokenP cStringUsingEncoding:NSUTF8StringEncoding])
              :Optional<std::string>();
    m_userID = settingsManager.userID
    ?std::string([settingsManager.userID cStringUsingEncoding:NSUTF8StringEncoding])
    :Optional<std::string>();
    
    m_deviceID = settingsManager.deviceID
    ?std::string([settingsManager.deviceID cStringUsingEncoding:NSUTF8StringEncoding])
    :Optional<std::string>();
  
  m_cachename = [settingsManager.cacheFile cStringUsingEncoding:NSUTF8StringEncoding];
  return *this;
}

Config &Config::write()
{
  @autoreleasepool {
    SettingsManager* settingsManager = [SettingsManager sharedInstance];
    [settingsManager setEmail:m_email.isSet()
        ?[NSString stringWithUTF8String:m_email.get().c_str()]
        :nil];
    [settingsManager setToken:m_token.isSet()
        ?[NSString stringWithUTF8String:m_token.get().c_str()]
        :nil];
    [settingsManager setPassword:m_password.isSet()
        ?[NSString stringWithUTF8String:m_password.get().c_str()]
        :nil];
    [settingsManager setDevice:m_device.isSet()
        ?[NSString stringWithUTF8String:m_device.get().c_str()]
        :nil];
    [settingsManager setDeviceTokenL:m_deviceTokenL.isSet()
        ?[NSString stringWithUTF8String:m_deviceTokenL.get().c_str()]
        :nil];
    [settingsManager setDeviceTokenP:m_deviceTokenP.isSet()
        ?[NSString stringWithUTF8String:m_deviceTokenP.get().c_str()]
        :nil];
      [settingsManager setUserID:m_userID.isSet()?
       [NSString stringWithUTF8String:m_userID.get().c_str()]:
       nil];
      [settingsManager setDeviceID:m_deviceID.isSet()?
       [NSString stringWithUTF8String:m_deviceID.get().c_str()]:
       nil];
  }
  return *this;
}