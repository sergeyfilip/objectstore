//
//  BackupManagerListener.h
//  Keepit
//
//  Created by vg on 5/7/13.
//  Copyright (c) 2013 Evalesco. All rights reserved.
//

#import <Foundation/Foundation.h>
#import "BackupManager.hh"

// objective-c++ way to get notification
//
// ObjectiveC callbacks
@protocol BackupManagerDelegate <NSObject>
@optional
- (void)didRegister;
- (void)didFailToRegister:(NSString *)explanation;

- (void)didDeregister;
- (void)didFailToDeregister:(NSString *)explanation;

- (void)didCreateUser;
- (void)didFailToCreateUser:(NSString *)explanation;

- (void)didGetUserDetails;
- (void)didFailToGetUserDetails:(NSString *)explanation;

- (void)didCreateDevice;
- (void)didFailToCreateDevice:(NSString *)explanation;

- (void)didCreateSnapshot;
- (void)didFailToCreateSnapshot:(NSString *)explanation;

- (void)didCalculateFileSize:(NSString *)path;

- (void)didGetProgressInfo;

- (void)didGetChangeInfo:(NSString *)path;

- (void)didStartMonitoring;
- (void)didFailToStartMonitoring:(NSString *)explanation;

- (void)didStopMonitoring;
- (void)didFailToStopMonitoring:(NSString *)explanation;

- (void)didStartUpload;
- (void)didFailToStartUpload:(NSString *)explanation;

- (void)didStopUpload;
- (void)didFailToStopUpload:(NSString *)explanation;
@end

class BackupManagerListenerBridge;
// ObjC Wrapper over C++ BackupManagerListenerBridge
@interface BackupManagerListenerObjC : NSObject {
  BackupManagerListenerBridge *_bridge;
}
+ (id)listenerWithDelegate:(id<BackupManagerDelegate>)aDelegate;
- (id)initWithDelegate:(id<BackupManagerDelegate>)aDelegate;
- (void)subscribe;
- (void)unsubscribe;
@end

// C++ to ObjC
class BackupManagerListenerBridge : public BackupManagerListener
{
public:
  BackupManagerListenerBridge(BackupManagerListenerObjC *parent, id<BackupManagerDelegate> delegate);
  virtual ~BackupManagerListenerBridge();
  
protected:
  // Here is made the conversion from C++ callbacks to ObjectiveC ones
  virtual void didRegister();
  virtual void didFailToRegister(const std::string explanation);
  
  virtual void didDeregister();
  virtual void didFailToDeregister(const std::string explanation);
  
  virtual void didCreateUser();
  virtual void didFailToCreateUser(const std::string explanation);

  virtual void didGetUserDetails();
  virtual void didFailToGetUserDetails(const std::string explanation);

  virtual void didCreateDevice();
  virtual void didFailToCreateDevice(const std::string explanation);
  
  virtual void didCreateSnapshot();
  virtual void didFailToCreateSnapshot(const std::string explanation);
  
  virtual void didCalculateFileSize(const std::string path);
  
  virtual void didGetProgressInfo();

  virtual void didGetChangeInfo(const std::string *path);

  virtual void didStartMonitoring();
  virtual void didFailToStartMonitoring(const std::string explanation);
  
  virtual void didStopMonitoring();
  virtual void didFailToStopMonitoring(const std::string explanation);
  
  virtual void didStartUpload();
  virtual void didFailToStartUpload(const std::string explanation);
  
  virtual void didStopUpload();
  virtual void didFailToStopUpload(const std::string explanation);

private:
  BackupManagerListenerObjC *m_parent;
  id<BackupManagerDelegate> m_delegate;
};
