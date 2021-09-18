//
//  BackupManagerListener.m
//  Keepit
//
//  Created by vg on 5/7/13.
//  Copyright (c) 2013 Evalesco. All rights reserved.
//

#import "BackupManagerListener.h"

#pragma mark -
////////////////////////////////////////////////////////////////////////////////
@implementation BackupManagerListenerObjC
+ (id)listenerWithDelegate:(id<BackupManagerDelegate>)aDelegate
{
  return [[self alloc] initWithDelegate:aDelegate];
}

- (id)initWithDelegate:(id<BackupManagerDelegate>)aDelegate
{
  self = [super init];
  if (self) {
    // Initialization code here.
    _bridge = new BackupManagerListenerBridge(self, aDelegate);
  }
  
  return self;
}

- (void)subscribe
{
  BackupManager::getInst().addListener(*_bridge);
}

- (void)unsubscribe
{
  BackupManager::getInst().removeListener(*_bridge);
}

- (void)dealloc
{
  BackupManager::getInst().removeListener(*_bridge);
	delete _bridge;
  [super dealloc];
}

@end

#pragma mark -
////////////////////////////////////////////////////////////////////////////////
BackupManagerListenerBridge::BackupManagerListenerBridge(BackupManagerListenerObjC *parent, id<BackupManagerDelegate> delegate)
: m_parent(parent)
, m_delegate(delegate)
{
}

BackupManagerListenerBridge::~BackupManagerListenerBridge()
{
  //assert(!BackupManager::getInst().isSubscribed(this) && "Listener wasn't unsubscribed properly");
}

//Register
void BackupManagerListenerBridge::didRegister()
{
  if ([m_delegate respondsToSelector:@selector(didRegister)]) {
    @autoreleasepool {
      dispatch_async(dispatch_get_main_queue(), ^{
        if (!BackupManager::getInst().hasListener(*this)) return;
        [m_delegate didRegister];
      });
    }
  }
}

void BackupManagerListenerBridge::didFailToRegister(const std::string explanation)
{
  if ([m_delegate respondsToSelector:@selector(didFailToRegister:)]) {
    @autoreleasepool {
      //will be retained by dispatch_async
      NSString* message = [NSString stringWithUTF8String:explanation.c_str()];
      dispatch_async(dispatch_get_main_queue(), ^{
        if (!BackupManager::getInst().hasListener(*this)) return;
        [m_delegate didFailToRegister:message];
      });
    }
  }
}

//Deregister
void BackupManagerListenerBridge::didDeregister()
{
  if ([m_delegate respondsToSelector:@selector(didDeregister)]) {
    @autoreleasepool {
      dispatch_async(dispatch_get_main_queue(), ^{
        if (!BackupManager::getInst().hasListener(*this)) return;
        [m_delegate didDeregister];
      });
    }
  }
}

void BackupManagerListenerBridge::didFailToDeregister(const std::string explanation)
{
  if ([m_delegate respondsToSelector:@selector(didFailToDeregister:)]) {
    @autoreleasepool {
      //will be retained by dispatch_async
      NSString* message = [NSString stringWithUTF8String:explanation.c_str()];
      dispatch_async(dispatch_get_main_queue(), ^{
        if (!BackupManager::getInst().hasListener(*this)) return;
        [m_delegate didFailToDeregister:message];
      });
    }
  }
}

//CreateUser
void BackupManagerListenerBridge::didCreateUser()
{
  if ([m_delegate respondsToSelector:@selector(didCreateUser)]) {
    @autoreleasepool {
      dispatch_async(dispatch_get_main_queue(), ^{
        if (!BackupManager::getInst().hasListener(*this)) return;
        [m_delegate didCreateUser];
      });
    }
  }
}

void BackupManagerListenerBridge::didFailToCreateUser(const std::string explanation)
{
  if ([m_delegate respondsToSelector:@selector(didFailToCreateUser:)]) {
    @autoreleasepool {
      //will be retained by dispatch_async
      NSString* message = [NSString stringWithUTF8String:explanation.c_str()];
      dispatch_async(dispatch_get_main_queue(), ^{
        if (!BackupManager::getInst().hasListener(*this)) return;
        [m_delegate didFailToCreateUser:message];
      });
    }
  }
}

//GetUserDetails
void BackupManagerListenerBridge::didGetUserDetails()
{
  if ([m_delegate respondsToSelector:@selector(didGetUserDetails)]) {
    @autoreleasepool {
      dispatch_async(dispatch_get_main_queue(), ^{
        if (!BackupManager::getInst().hasListener(*this)) return;
        [m_delegate didGetUserDetails];
      });
    }
  }
}

void BackupManagerListenerBridge::didFailToGetUserDetails(const std::string explanation)
{
  if ([m_delegate respondsToSelector:@selector(didFailToGetUserDetails:)]) {
    @autoreleasepool {
      //will be retained by dispatch_async
      NSString* message = [NSString stringWithUTF8String:explanation.c_str()];
      dispatch_async(dispatch_get_main_queue(), ^{
        if (!BackupManager::getInst().hasListener(*this)) return;
        [m_delegate didFailToGetUserDetails:message];
      });
    }
  }
}

//CreateDevice
void BackupManagerListenerBridge::didCreateDevice()
{
  if ([m_delegate respondsToSelector:@selector(didCreateDevice)]) {
    @autoreleasepool {
      dispatch_async(dispatch_get_main_queue(), ^{
        if (!BackupManager::getInst().hasListener(*this)) return;
        [m_delegate didCreateDevice];
      });
    }
  }
}

void BackupManagerListenerBridge::didFailToCreateDevice(const std::string explanation)
{
  if ([m_delegate respondsToSelector:@selector(didFailToCreateDevice:)]) {
    @autoreleasepool {
      //will be retained by dispatch_async
      NSString* message = [NSString stringWithUTF8String:explanation.c_str()];
      dispatch_async(dispatch_get_main_queue(), ^{
        if (!BackupManager::getInst().hasListener(*this)) return;
        [m_delegate didFailToCreateDevice:message];
      });
    }
  }
}

//CreateSnapshot
void BackupManagerListenerBridge::didCreateSnapshot()
{
  if ([m_delegate respondsToSelector:@selector(didCreateSnapshot)]) {
    @autoreleasepool {
      dispatch_async(dispatch_get_main_queue(), ^{
        if (!BackupManager::getInst().hasListener(*this)) return;
        [m_delegate didCreateSnapshot];
      });
    }
  }
}

void BackupManagerListenerBridge::didFailToCreateSnapshot(const std::string explanation)
{
  if ([m_delegate respondsToSelector:@selector(didFailToCreateSnapshot:)]) {
    @autoreleasepool {
      //will be retained by dispatch_async
      NSString* message = [NSString stringWithUTF8String:explanation.c_str()];
      dispatch_async(dispatch_get_main_queue(), ^{
        if (!BackupManager::getInst().hasListener(*this)) return;
        [m_delegate didFailToCreateSnapshot:message];
      });
    }
  }
}

//CalculateFileSize
void BackupManagerListenerBridge::didCalculateFileSize(const std::string path)
{
  if ([m_delegate respondsToSelector:@selector(didCalculateFileSize:)]) {
    @autoreleasepool {
      //will be retained by dispatch_async
      NSString* aPath = [NSString stringWithUTF8String:path.c_str()];
      dispatch_async(dispatch_get_main_queue(), ^{
        if (!BackupManager::getInst().hasListener(*this)) return;
        [m_delegate didCalculateFileSize:aPath];
      });
    }
  }
}

//ProgressInfo
void BackupManagerListenerBridge::didGetProgressInfo()
{
  if ([m_delegate respondsToSelector:@selector(didGetProgressInfo)]) {
    @autoreleasepool {
      dispatch_async(dispatch_get_main_queue(), ^{
        if (!BackupManager::getInst().hasListener(*this)) return;
        [m_delegate didGetProgressInfo];
      });
    }
  }
}

//ChangeInfo
void BackupManagerListenerBridge::didGetChangeInfo(const std::string *path)
{
  if ([m_delegate respondsToSelector:@selector(didGetChangeInfo:)]) {
    @autoreleasepool {
      //will be retained by dispatch_async
      NSString* aPath = [NSString stringWithUTF8String:path->c_str()];
      dispatch_async(dispatch_get_main_queue(), ^{
        if (!BackupManager::getInst().hasListener(*this)) return;
        [m_delegate didGetChangeInfo:aPath];
      });
    }
  }
}

//StartMonitoring
void BackupManagerListenerBridge::didStartMonitoring()
{
  if ([m_delegate respondsToSelector:@selector(didStartMonitoring)]) {
    @autoreleasepool {
      dispatch_async(dispatch_get_main_queue(), ^{
        if (!BackupManager::getInst().hasListener(*this)) return;
        [m_delegate didStartMonitoring];
      });
    }
  }
}

void BackupManagerListenerBridge::didFailToStartMonitoring(const std::string explanation)
{
  if ([m_delegate respondsToSelector:@selector(didFailToStartMonitoring:)]) {
    @autoreleasepool {
      //will be retained by dispatch_async
      NSString* message = [NSString stringWithUTF8String:explanation.c_str()];
      dispatch_async(dispatch_get_main_queue(), ^{
        if (!BackupManager::getInst().hasListener(*this)) return;
        [m_delegate didFailToStartMonitoring:message];
      });
    }
  }
}

//StopMonitoring
void BackupManagerListenerBridge::didStopMonitoring()
{
  if ([m_delegate respondsToSelector:@selector(didStopMonitoring)]) {
    @autoreleasepool {
      dispatch_async(dispatch_get_main_queue(), ^{
        if (!BackupManager::getInst().hasListener(*this)) return;
        [m_delegate didStopMonitoring];
      });
    }
  }
}

void BackupManagerListenerBridge::didFailToStopMonitoring(const std::string explanation)
{
  if ([m_delegate respondsToSelector:@selector(didFailToStopMonitoring:)]) {
    @autoreleasepool {
      //will be retained by dispatch_async
      NSString* message = [NSString stringWithUTF8String:explanation.c_str()];
      dispatch_async(dispatch_get_main_queue(), ^{
        if (!BackupManager::getInst().hasListener(*this)) return;
        [m_delegate didFailToStopMonitoring:message];
      });
    }
  }
}

//StartUpload
void BackupManagerListenerBridge::didStartUpload()
{
  if ([m_delegate respondsToSelector:@selector(didStartUpload)]) {
    @autoreleasepool {
      dispatch_async(dispatch_get_main_queue(), ^{
        if (!BackupManager::getInst().hasListener(*this)) return;
        [m_delegate didStartUpload];
      });
    }
  }
}

void BackupManagerListenerBridge::didFailToStartUpload(const std::string explanation)
{
  if ([m_delegate respondsToSelector:@selector(didFailToStartUpload:)]) {
    @autoreleasepool {
      //will be retained by dispatch_async
      NSString* message = [NSString stringWithUTF8String:explanation.c_str()];
      dispatch_async(dispatch_get_main_queue(), ^{
        if (!BackupManager::getInst().hasListener(*this)) return;
        [m_delegate didFailToStartUpload:message];
      });
    }
  }
}

//StopUpload
void BackupManagerListenerBridge::didStopUpload()
{
  if ([m_delegate respondsToSelector:@selector(didStopUpload)]) {
    @autoreleasepool {
      dispatch_async(dispatch_get_main_queue(), ^{
        if (!BackupManager::getInst().hasListener(*this)) return;
        [m_delegate didStopUpload];
      });
    }
  }
}

void BackupManagerListenerBridge::didFailToStopUpload(const std::string explanation)
{
  if ([m_delegate respondsToSelector:@selector(didFailToStopUpload:)]) {
    @autoreleasepool {
      //will be retained by dispatch_async
      NSString* message = [NSString stringWithUTF8String:explanation.c_str()];
      dispatch_async(dispatch_get_main_queue(), ^{
        if (!BackupManager::getInst().hasListener(*this)) return;
        [m_delegate didFailToStopUpload:message];
      });
    }
  }
}
