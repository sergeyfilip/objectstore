//
//  AccountPreferencesViewController.m
//  Keepit
//
//  Created by vg on 4/23/13.
//  Copyright (c) 2013 Evalesco. All rights reserved.
//

#import "AccountPreferencesViewController.h"
#import "MASPreferencesViewController.h"
#import "AppDelegate.h"
#import "StatusManager.h"
#import "Reachability.h"
#import "StatusManager.h"
#import "BackupManager.hh"
#import "BackupManagerListener.h"
#import "SettingsManager.h"

@interface AccountPreferencesViewController () <MASPreferencesViewController, BackupManagerDelegate>
// Internal properties
@property (nonatomic, retain) BackupManagerListenerObjC* backupManagerListener;
@property (nonatomic, assign) BOOL hasResizableWidth;
@property (nonatomic, assign) BOOL hasResizableHeight;
@property (nonatomic, assign) NSInteger loginTypeControlIndex;
@property (nonatomic, retain) NSString *email;
@property (nonatomic, retain) NSString *userName;
@property (nonatomic, retain) NSString *password;
@property (nonatomic, retain) NSString *computerName;
@property (nonatomic, assign) BOOL agreeWithTerms;
@property (nonatomic, retain) NSString *statusText;
@property (nonatomic, readwrite) BOOL logInProcess;
@property (assign) IBOutlet NSButton *signOutBtn;

// Helper routines
- (void)updateGUI;

@end

@implementation AccountPreferencesViewController

// Internal properties
@synthesize backupManagerListener = _backupManagerListener;
@synthesize hasResizableWidth = _hasResizableWidth;
@synthesize hasResizableHeight = _hasResizableHeight;
@synthesize loginTypeControlIndex = _loginTypeControlIndex;
@synthesize email = _email;
@synthesize userName = _userName;
@synthesize password = _password;
@synthesize computerName = _computerName;
@synthesize agreeWithTerms = _agreeWithTerms;
@synthesize statusText = _statusText;
@synthesize logInProcess = _logInProcess;
@synthesize signOutBtn = _signOutBtn;

- (id)init
{
    return [self initWithNibName:@"AccountPreferencesView" bundle:nil];
}

- (id)initWithNibName:(NSString *)nibNameOrNil bundle:(NSBundle *)nibBundleOrNil
{
    self = [super initWithNibName:nibNameOrNil bundle:nibBundleOrNil];
    if (self) {
        // Initialization code here.
        self.hasResizableWidth = NO;
        self.hasResizableHeight = NO;
        self.backupManagerListener = [BackupManagerListenerObjC listenerWithDelegate:self];
        [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(couldSignOut:) name:@"CouldSignOut" object:nil];
    }

    return self;
}

- (void)awakeFromNib {
  [self bind:@"statusText" toObject:[StatusManager sharedInstance] withKeyPath:@"statusText" options:nil];
  self.email = [NSString stringWithUTF8String:BackupManager::getInst().getEmail().c_str()];
  self.computerName = [NSString stringWithUTF8String:BackupManager::getInst().getDeviceName().c_str()];
  if (!self.computerName || ![self.computerName length]) {
    self.computerName = [[NSHost currentHost] localizedName];
  }
  
  if (BackupManager::getInst().isRegistered()) {
    self.loginTypeControlIndex = 3;
  } else {
    self.loginTypeControlIndex = 0;
  }
}

- (void)dealloc
{
    [[NSNotificationCenter defaultCenter] removeObserver:self name:@"CouldSignOut" object:nil];
    [_statusText release];
    [_computerName release];
    [_password release];
    [_userName release];
    [_email release];
  [self unbind:@"statusText"];
  self.backupManagerListener = nil;
  [super dealloc];
}

#pragma mark -
#pragma mark MASPreferencesViewController

- (NSString *)identifier
{
  return @"AccountPreferences";
}

- (NSImage *)toolbarItemImage
{
  return [NSImage imageNamed:@"TabBarIcon-32x32"];
}

- (NSString *)toolbarItemLabel
{
  return NSLocalizedString(@"Account", @"Toolbar item name for the Account preference pane");
}

- (void)viewWillAppear
{
  DLog(@"AccountPreferencesView::viewWillAppear");
  [self.backupManagerListener subscribe];
  [self updateGUI];
}

- (void)viewDidDisappear
{
  DLog(@"AccountPreferencesView::viewDidDisappear");
  [self.backupManagerListener unsubscribe];
}

#pragma mark - IBActions section

- (IBAction)signInPressed:(id)sender {
  self.loginTypeControlIndex = 1;
  [self updateGUI];
}

- (IBAction)createAccountPressed:(id)sender {
  self.loginTypeControlIndex = 2;
    self.email = @"";
    self.password = @"";
  [self updateGUI];
}

- (IBAction)backPressed:(id)sender {
  self.loginTypeControlIndex = 0;
    self.password = @"";
    _logInProcess = NO;
  [self updateGUI];
}

- (IBAction)helpPressed:(id)sender {
  AppDelegate* keepitAppDelegate = (AppDelegate*)[NSApplication sharedApplication].delegate;
  [keepitAppDelegate openWebFiles];
}

- (IBAction)createAccount:(id)sender {
    [self commitEditing];
    
    if ([self couldConnectUser]) {
        // Create new user
        std::string email = self.email?[self.email cStringUsingEncoding:NSUTF8StringEncoding]:"";
        std::string password = self.password?[self.password cStringUsingEncoding:NSUTF8StringEncoding]:"";
        std::string userName = self.userName?[self.userName cStringUsingEncoding:NSUTF8StringEncoding]:"";
        BackupManager::getInst().cmdNewUser(email, password, userName);
        [self updateGUI];
    }
}

- (IBAction)signIn:(id)sender {
  [self commitEditing];

    if ([self couldConnectUser]) {
        // Register on server, i.e. get new access token
        std::string email = self.email?[self.email cStringUsingEncoding:NSUTF8StringEncoding]:"";
        std::string password = self.password?[self.password cStringUsingEncoding:NSUTF8StringEncoding]:"";
        BackupManager::getInst().cmdRegister(email, password);
        [self updateGUI];
    }
}

- (IBAction)signOutPressed:(id)sender {
  // Deregister - invalidate access token
  BackupManager::getInst().cmdDeregister();
}

#pragma mark - Private methods

- (BOOL)couldConnectUser
{
    if (_logInProcess) return NO;
    if (![self checkInternetState]) return NO;

    [self checkUserEmail];
    _logInProcess = YES;
    return YES;
}

- (BOOL)checkInternetState
{
    AppDelegate* delegate = (AppDelegate* )[[NSApplication sharedApplication] delegate];
    
    if ([delegate chectInternetSate]) return YES;
    
    [self showNoInternetConnectionAlertInWindow:[delegate currentWindow]];
    return NO;
}

- (void)showNoInternetConnectionAlertInWindow:(NSWindow* )aWindow
{
    NSAlert* msgBox = [[[NSAlert alloc] init] autorelease];
    [msgBox setInformativeText:NSLocalizedString(@"Could not connect to the server.\nPlease check your internet connection or try again later!", nil)];
    [msgBox setMessageText:NSLocalizedString(@"Sorry", nil)];
    [msgBox addButtonWithTitle:NSLocalizedString(@"OK", nil)];
    if (aWindow) [msgBox beginSheetModalForWindow:aWindow modalDelegate:nil didEndSelector:nil contextInfo:nil];
    else [msgBox runModal];
}

- (void)checkUserEmail
{
    BOOL isTestHost = NO;
    
    NSString* checkMail = [NSString stringWithString:self.email];
    if (checkMail.length) {
        NSArray* components = [checkMail componentsSeparatedByString:@"@"];
        if (components.count) {
            for (NSString* component in components) {
                if ([component isKindOfClass:[NSString class]]) {
                    if ([component isEqualToString:@"test-keepit.com"]) {
                        isTestHost = YES;
                        break;
                    }
                }
            }
        }
    }
    
    NSString* apiHostKey = @"apiHost";
    
    NSUserDefaults* defaults = [NSUserDefaults standardUserDefaults];
    
    if (isTestHost) [defaults setObject:@"ws-test.keepit.com" forKey:apiHostKey];
    else [defaults setObject:@"ws.keepit.com" forKey:apiHostKey];
    
    [defaults synchronize];
    
    AppDelegate* keepitAppDelegate = (AppDelegate*)[NSApplication sharedApplication].delegate;
    [keepitAppDelegate updateConfigApiHost];
}

- (void)updateGUI
{
  if (BackupManager::getInst().isRegistered()) {
    UserDetails userDetails = BackupManager::getInst().getUserDetails();
    if (userDetails.email.isSet()) {
      self.email = [NSString stringWithUTF8String:userDetails.email.get().c_str()];
    }
    if (userDetails.fullname.isSet()) {
      self.userName = [NSString stringWithUTF8String:userDetails.fullname.get().c_str()];
    }
    self.computerName = [NSString stringWithUTF8String:BackupManager::getInst().getDeviceName().c_str()];
  }
}

- (NSString* )findErrorMessageInExplanation:(NSString* )explanation
{
    NSString* message = explanation;
    
    NSArray* explanations = [explanation componentsSeparatedByString:@"=> "];
    if (explanations.count) {
        id something = [explanations lastObject];
        if ([something isKindOfClass:[NSString class]]) {
            message = [NSString stringWithString:(NSString* )something];
        }
    }
    
    return message;
}

- (void)clearCache
{
    BackupManager::getInst().changeCache();
}

#pragma mark - BackupManagerDelegate protocol

- (void)didRegister
{
  DLog(@"didRegister");
  NSString* message = @"Logged in";
  [StatusManager sharedInstance].statusText = message;
    _logInProcess = NO;
  // Request user contact details from server
  BackupManager::getInst().cmdGetUserDetails();

  // Access token received, attempt to create device
  std::string computerName = self.computerName?[self.computerName cStringUsingEncoding:NSUTF8StringEncoding]:"";
    std::string pass = self.password?[self.password cStringUsingEncoding:NSUTF8StringEncoding]:"";
  BackupManager::getInst().cmdNewDevice(computerName, pass);

  self.loginTypeControlIndex = 3;
  [self updateGUI];
    [[NSNotificationCenter defaultCenter] postNotificationName:@"EnableTabs" object:[NSNumber numberWithBool:YES]];
}

- (void)didFailToRegister:(NSString *)explanation
{
  DLog(@"didFailToRegister:%@", explanation);
  NSString* message = @"Login";
     _logInProcess = NO;
    NSString* errorMessage = @"";
    
    errorMessage  = [self findErrorMessageInExplanation:explanation];
    if (!errorMessage.length) errorMessage = [NSString stringWithFormat:@"%@: %@", message, explanation];
    
  [StatusManager sharedInstance].statusText =errorMessage;
    DLog(@"the error msg is = %@", errorMessage);

  [self updateGUI];
    [[NSNotificationCenter defaultCenter] postNotificationName:@"EnableTabs" object:[NSNumber numberWithBool:NO]];
}

- (void)didDeregister
{
  DLog(@"didDeregister");
  NSString* message = @"Logged out";
  [StatusManager sharedInstance].statusText = message;
  
  if (BackupManager::getInst().isMonitoring()) {
    BackupManager::getInst().cmdStopMonitor();
  }
  self.loginTypeControlIndex = 0;
  [self updateGUI];
    [[NSNotificationCenter defaultCenter] postNotificationName:@"EnableTabs" object:[NSNumber numberWithBool:NO]];
    [self clearCache];
}

- (void)didFailToDeregister:(NSString *)explanation
{
  DLog(@"didFailToDeregister:%@", explanation);
  NSString* message = @"Logout";
    NSString* errorMessage = @"";
    
    errorMessage  = [self findErrorMessageInExplanation:explanation];
    if (!errorMessage.length) errorMessage = [NSString stringWithFormat:@"%@: %@", message, explanation];
    
    [StatusManager sharedInstance].statusText =errorMessage;

  if (BackupManager::getInst().isMonitoring()) {
    BackupManager::getInst().cmdStopMonitor();
  }
  self.loginTypeControlIndex = 0;
  [self updateGUI];
    [[NSNotificationCenter defaultCenter] postNotificationName:@"EnableTabs" object:[NSNumber numberWithBool:YES]];
}

- (void)didCreateUser
{
  DLog(@"didCreateUser");
  NSString* message = @"Created user";
  [StatusManager sharedInstance].statusText = message;
   _logInProcess = NO;
  std::string email = self.email?[self.email cStringUsingEncoding:NSUTF8StringEncoding]:"";
  std::string password = self.password?[self.password cStringUsingEncoding:NSUTF8StringEncoding]:"";
  // New user created, attempt to get access token
  BackupManager::getInst().cmdRegister(email, password);
  [self updateGUI];
    [[NSNotificationCenter defaultCenter] postNotificationName:@"EnableTabs" object:[NSNumber numberWithBool:YES]];
}

- (void)didFailToCreateUser:(NSString *)explanation
{
  DLog(@"didFailToCreateUser:%@", explanation);
  NSString* message = @"Create user";
    NSString* errorMessage = @"";
     _logInProcess = NO;
    errorMessage  = [self findErrorMessageInExplanation:explanation];
    if (!errorMessage.length) errorMessage = [NSString stringWithFormat:@"%@: %@", message, explanation];
    
    [StatusManager sharedInstance].statusText =errorMessage;
    DLog(@"the error msg is = %@", errorMessage);

  
  [self updateGUI];
    [[NSNotificationCenter defaultCenter] postNotificationName:@"EnableTabs" object:[NSNumber numberWithBool:NO]];
}

- (void)didCreateDevice
{
  DLog(@"didCreateDevice");
  BackupManager::getInst().cmdMonitor();
  [self updateGUI];
}

- (void)didFailToCreateDevice:(NSString *)explanation
{
  DLog(@"didFailToCreateDevice:%@", explanation);
  BackupManager::getInst().cmdMonitor();
  [self updateGUI];
}

- (void)didGetUserDetails
{
  DLog(@"didGetUserDetails");
  [self updateGUI];
}

- (void)didFailToGetUserDetails:(NSString *)explanation
{
  DLog(@"didFailToGetUserDetails");
}

#pragma mark -
#pragma mark NSNotification methods

- (void)couldSignOut:(NSNotification* )notification
{
    if (!notification || ![notification object] || ![[notification object] isKindOfClass:[NSNumber class]]) return;
    
    BOOL btnEnabled = [[notification object] boolValue];

    [_signOutBtn setEnabled:btnEnabled];
    
    if (!btnEnabled) [StatusManager sharedInstance].statusText = @"Couldn't sign out until scanning in process";
}

@end
