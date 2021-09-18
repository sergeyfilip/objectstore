//
//  AccountPreferencesViewController.h
//  Keepit
//
//  Created by vg on 4/23/13.
//  Copyright (c) 2013 Evalesco. All rights reserved.
//

#import <Cocoa/Cocoa.h>

@class BackupManagerListenerObjC;

@interface AccountPreferencesViewController : NSViewController {
@private
  BackupManagerListenerObjC *_backupManagerListener;
  BOOL _hasResizableWidth;
  BOOL _hasResizableHeight;
  NSInteger _loginTypeControlIndex;
  NSString *_email;
  NSString *_userName;
  NSString *_password;
  NSString *_computerName;
  BOOL _agreeWithTerms;
  NSString *_statusText;
    BOOL _logInProcess;
    NSButton *_signOutBtn;
}

- (id)init;

- (IBAction)signInPressed:(id)sender;
- (IBAction)signOutPressed:(id)sender;
- (IBAction)createAccountPressed:(id)sender;
- (IBAction)helpPressed:(id)sender;
- (IBAction)backPressed:(id)sender;
- (IBAction)createAccount:(id)sender;
- (IBAction)signIn:(id)sender;

@end
